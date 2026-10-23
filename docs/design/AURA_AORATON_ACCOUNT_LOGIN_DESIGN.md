# AURA ↔ aoraton.com: account login design

Status: approved by the owner 2026-10-23 ("πες μου τι θες για aoraton να ξεκινήσουμε"); builder notes at the end. Supersedes the device-pairing route of v11 (`docs/aoraton/PAIRING_PROTOCOL.md`) for the first live connection.

## Intent

The owner wants AURA to show and send their real aoraton.com messages, kept simple, with **no change to aoraton.com** (code, schema, RLS, Edge Functions). AURA signs in with the owner's own aoraton account, the same way a new browser would, and gets room keys the same ways a new browser does.

## Decisions taken by the owner (2026-10-23)

1. **Login:** Google sign-in in the PC's browser, returning to the simulator on `127.0.0.1`. If Supabase does not accept that return address, the owner adds `http://127.0.0.1:4779/**` under Supabase → Authentication → URL Configuration → Redirect URLs. That is a project setting; aoraton.com's code is untouched and its users see no change.
2. **Room keys:** both the existing aoraton **key vault** (backup passphrase) and **invite links** (`#room=…&key=…&k=…`).
3. **Scope:** read and send. Every send needs explicit confirmation. New messages are fetched by polling, labelled as such, never called "live".
4. The owner authorizes AURA to talk to the production aoraton.com backend **with their own account, when they run it themselves**. Automated tests never contact it.

This reverses v11 decision 7 (device-owned anonymous identity plus browser grants): AURA now holds a normal session of the owner's account. It still never copies a browser session or refresh token. AURA performs its own sign-in.

## Goals

- Sign in, see the account label (email), sign out.
- Unlock room keys from the vault with the passphrase; add a key from an invite link.
- Room list with unread counts and last activity; locked rooms shown as locked.
- Read the newest 50 messages of a room; text only.
- Notifications for new incoming messages, using the existing privacy rules.
- Send a text message after the confirmation screen, with honest delivery stages.

## Non-goals (this phase)

Attachments, polls, replies, reactions, edits, view-once, calls, presence, Realtime websockets, creating or joining rooms (an invite link only unlocks a room the account already belongs to), legacy pre-E2EE messages (shown locked), and login on the ESP32 itself. The device will later sign in through the phone. This document only keeps the shared code ready for it.

## Anti-goals (a failure even if it "works")

- Any change to aoraton.com or its database.
- Tokens, room keys, passphrases or message text in logs, screenshots, evidence, the control-event log or Codex prompts.
- Sending without the confirmation screen, auto-retrying a send, or claiming "Sent" without a server acknowledgement.
- Silent fallback to mock data when live fails. Mock data stays labelled DEMO.
- Any automated test touching the production project.

## How it works

### Configuration

The Supabase URL and the public anon key (the same values aoraton.com ships to every browser) live in a git-ignored `config/aoraton.local.json`. `scripts/setup-aoraton.ps1` fills it once by reading `..\incognitochat\services\supabaseConfig.ts` (read-only) or by asking the owner to paste the two values. With the file present, `run.bat` starts the simulator in live mode (`AURA_AORATON_MODE=live`). Without it, the simulator uses mock mode with DEMO labels.

### Sign-in (Supabase PKCE, as supabase-js does it)

1. Settings → Aoraton → **Σύνδεση με Google**. The worker creates a random `code_verifier` and its S256 `code_challenge`, and opens the default browser at
   `<url>/auth/v1/authorize?provider=google&redirect_to=http://127.0.0.1:4779/aoraton/callback&code_challenge=<c>&code_challenge_method=s256`.
2. A one-shot loopback listener on `127.0.0.1:4779` waits up to 5 minutes for `GET /aoraton/callback?code=…`. It answers with a small "you can close this tab" page and then stops listening.
3. `POST <url>/auth/v1/token?grant_type=pkce` with `{auth_code, code_verifier}` and the `apikey` header returns the session (access token, refresh token, user id, email).
4. If the browser lands on aoraton.com instead (return address not allowed), the wait times out. AURA then shows exactly which address to add in Supabase.

### Session

- The refresh token is stored with DPAPI (current user), never in plain files. The access token stays in memory only.
- The worker refreshes the session when less than 60 s remain or on the first 401 (`grant_type=refresh_token`), one refresh at a time. If refreshing fails, AURA shows «Χρειάζεται νέα σύνδεση».
- **Αποσύνδεση** wipes the stored token and keys, then calls `POST /auth/v1/logout?scope=local`.
- Signing out on aoraton.com (supabase-js default `global` scope) also ends AURA's session. This is the owner's remedy if AURA is lost.

### Room keys

- **Vault:** the owner types the backup passphrase on AURA's keyboard. The worker reads `key_vault_meta` (`kdf_salt` base64url, `kdf_iters`, `check_blob`) and derives PBKDF2-SHA256 → AES-256-GCM. It checks `check_blob` equals `inco-vault-check-v1`, then decrypts every `key_vault.blob` (`v2:<iv>:<ct>`, no AAD) to `{v:1, rk, name, pin}`. RKs are stored with DPAPI per `room_key`. The passphrase and the derived key are wiped right after; new rooms need **Ενημέρωση κλειδιών** (unlock again).
- **Invite link:** pasted in the control panel (simulator) and parsed as `room`, `key`, `k`. The key is kept only if the account is already a member of `key`. Otherwise AURA shows «Μπες πρώτα στο δωμάτιο από το aoraton.com».

### Rooms, messages and sending (contract audited in `docs/aoraton/AUTH_RLS_REVIEW.md`)

- **Rooms:** `rooms?created_by=eq.<uid>` plus `subscribers?uid=eq.<uid>` → `rooms?room_key=in.(…)`. Unread and last activity come from `rpc/room_overview` with `{room_key, since}` items. `since` is AURA's own per-room read marker (like the browser's `lastRead_<room_key>`), kept on disk without message content.
- **Messages:** `messages?room_key=eq.<k>&order=created_at.desc,id.desc&limit=50`, shown oldest-first. Only `type=text` rows are decrypted (v2 codec from v11). Other types show a short "open in aoraton.com" note, and a failed decrypt shows the locked placeholder.
- **Polling:** the room list every 30 s and an open room every 5 s, paused while offline. The UI says «ανανέωση ανά λίγα δευτερόλεπτα», never "live".
- **Notifications:** an incoming message newer than the last sync goes through the existing notification path (preview rules unchanged).
- **Send:** compose → existing confirmation screen → v2 encrypt → `POST messages` with `room_key`, `uid`, `username`, `text`, `type:'text'` and a fresh `client_msg_id`.
  - 201 means Acknowledged.
  - A network error means Uncertain, reconciled by `client_msg_id` on the next fetch.
  - There is no automatic retry.
  - `username` is the name the account last used in that room (from history), else the Google display name.

## Architecture

The v11 boundary stays: the LVGL UI ↔ `RealAoratonService` (shared, UI thread) ↔ bounded `AoratonMailbox` ↔ platform worker. The pairing states are reused for the account:

- `NotPaired` means signed out.
- `WaitingForBrowser` means the Google sign-in is in progress.
- `Paired` means signed in.
- `ReauthorizationRequired` means a new sign-in is needed.
- `Error` is unchanged.

The QR/SAS screens are replaced by the sign-in, passphrase and status screens.

**Shared (`shared/core`)**, which the ESP32 will reuse:
- PKCE helpers;
- a bounded JSON reader (no recursion beyond a fixed depth, size caps);
- request builders and response parsers for auth, rooms, overview, messages, vault and insert;
- the vault blob/check decoder (uses the crypto adapter);
- the invite parser;
- the extended commands and events: `BeginSignIn`, `SignOut`, `UnlockVault`, `AddInviteKey`, `SignedIn`, `VaultUnlocked`, `KeyAdded`.

**Simulator (`simulator/app`):**
- WinHTTP TLS client;
- WinSock one-shot callback listener;
- browser launch;
- CNG SHA-256 and PBKDF2 (`BCryptDeriveKeyPBKDF2`);
- the existing DPAPI vault;
- one worker thread that serializes the auth refresh.

**Firmware:** later. It needs `esp_http_client`, mbedTLS PBKDF2 and phone-assisted sign-in.

## Verification

- **Unit:**
  - PKCE (RFC 7636 Appendix B vector);
  - JSON reader limits;
  - parsers with captured-shape synthetic rows;
  - invite parsing;
  - vault decode against synthetic vectors produced by the existing WebCrypto oracle;
  - state machine (sign-in, timeout, refresh, revoke, sign-out wipes).
- **Integration:** a local fake Supabase (Node, loopback) driven by the simulator worker covers:
  - authorize redirect and token exchange;
  - rooms, overview, messages and insert;
  - a 401 then refresh;
  - an uncertain send then reconciliation.

  It runs in `scripts/test.ps1` and never touches the network.
- **Live:** the owner runs `run.bat`, signs in, unlocks, reads and sends one message to a room of their choice. The result is recorded in `docs/VERIFICATION.md` (PASSED/FAILED). Until then, live is NOT RUN.

## Left to the builder

Exact poll intervals within the stated bounds, Greek and English copy (in `shared/localization`), file names, the JSON reader's internal design, and how the passphrase screen reuses the v11.2 keyboard.

## Builder decisions (2026-10-23, before implementation)

- **Invite links come from the PC clipboard**, through a «Επικόλληση συνδέσμου» button on AURA. The control panel would write the link, and with it the room key, into `simulator/runtime/control-events.log`.
- **One poll every 10 s** of `room_overview` for all rooms. It replaces the separate 30 s and 5 s timers. A room whose last activity changed is fetched at once, so an open room updates within about 10 s, and the UI says so.
- **Read markers are kept in memory for v1.** On first sight they baseline to the room's latest server time, as the web client does. Messages that arrived while AURA was off therefore do not count as unread after a restart. Storing markers can come later.
- **The worker logic lives in `shared/core`**, behind `IAoratonHttp`, `IAoratonSignInChannel`, `IAoratonSecretStore` and `IAoratonAccountCrypto`. It is tested in C++ with fakes. A separate smoke executable runs the real WinHTTP client, loopback listener and DPAPI store against a local Node fake of the Supabase endpoints.
- **The existing DPAPI vault format is reused unchanged:** the access token is left empty, the refresh token and room keys are stored.
- **Rows that are not shown in v1:**
  - Rows that are not `type=text` (attachments, polls, locations) are skipped.
  - Rows that fail to decrypt are skipped, typically pre-upgrade legacy messages.

  The "open in aoraton.com" note was dropped, because its copy would have had to live in the shared worker.
- **The account page** lists the account, Κλειδιά δωματίων, Πρόσκληση and Αποσύνδεση. Back returns to the rooms. Row texts were shortened until no Greek line was truncated at 480×480.

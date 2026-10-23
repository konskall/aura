# Aoraton account login: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** AURA (simulator first) signs in to aoraton.com with the owner's Google account, unlocks room keys from the aoraton key vault or an invite link, lists rooms, reads and polls messages, and sends confirmed text messages. Nothing changes on aoraton.com.

**Architecture:** The UI talks only to `RealAoratonService` (shared, UI thread). The service exchanges bounded commands and events through `AoratonMailbox` with a new shared `AoratonAccountWorker`.
- The worker owns the Supabase protocol. It reaches the platform only through four interfaces: HTTP, sign-in channel, secret store and crypto.
- The simulator implements these interfaces with WinHTTP, a WinSock loopback listener, the DPAPI vault and CNG, and runs the worker on one thread.
- Later, the ESP32 implements the same interfaces.

**Tech Stack:**
- C++17 (shared code must pass the ESP-IDF flag check: gnu++2b, no exceptions or RTTI, `-Werror`);
- LVGL 9.6;
- WinHTTP, WinSock2, CNG `bcrypt` and DPAPI `crypt32` on Windows;
- Node 22 for the fake Supabase.

**Spec:** `docs/design/AURA_AORATON_ACCOUNT_LOGIN_DESIGN.md` (including "Builder decisions").

## Global Constraints

- No change to `C:\Users\polis\Documents\incognitochat` (read-only) or to aoraton.com.
- Automated tests never contact `*.supabase.co`. Only the owner's own run of `run.bat` does.
- Tokens, room keys, passphrases, invite links and message text never appear in logs, stderr, screenshots, evidence JSON, `control-events.log` or Codex.
- Redirect address: `http://127.0.0.1:4779/aoraton/callback`. Sign-in wait: 300 s. Poll interval: 10 s. History page: 50 rows. Room cap: 64 (existing `kMaxRooms`). Plaintext cap: 1500 bytes (`kAoratonMaxPlaintextBytes`).
- Vault: `key_vault_meta(kdf_salt base64url, kdf_iters, check_blob)`. The check plaintext is `inco-vault-check-v1`. Blobs are `v2:<b64url iv>:<b64url ct+tag>`, AES-256-GCM with no AAD, and decode to JSON `{v:1, rk, name, pin}`.
- Invite: `#room=<name>&key=<room_key>&k=<b64url 32-byte RK>`. The query-string form is accepted too.
- Sends insert `{room_key, uid, username, avatar_url:null, text:<v2 wire>, type:"text", client_msg_id:"temp_aura<32 hex>", reactions:{}}` with `Prefer: return=representation` and `select=id,created_at`.
- Every REST call sends the headers `apikey: <anon key>` and `Authorization: Bearer <access token>`.
- All visible copy goes in `shared/localization` (Greek default, English fallback). Mock mode stays labelled DEMO.
- New shared sources are added to `CMakeLists.txt` `aura_core` and to `firmware/components/aura_shared/CMakeLists.txt`.
- Do not commit; the owner commits on request (AGENTS rule). Each task ends with `scripts/test.ps1` green instead.

## Review Focus

1. **The owner closes the browser tab or signs in to aoraton.com instead.** The callback never arrives, so after 300 s the screen shows the exact Redirect-URL hint, never an endless spinner. This is tested in Task 4 (`sign_in_times_out`).
2. **A refresh token expires or is revoked while AURA is asleep.** The first REST call gets 401, the refresh fails, and the result is `ReauthorizationRequired` with keys kept and no crash loop. Tested in Task 4 (`revoked_session_requires_sign_in`).
3. **A wrong vault passphrase.** It shows «Λάθος συνθηματικό», keeps previously held keys and sends nothing else. Tested in Task 2 (`vault_wrong_passphrase`) and Task 4.
4. **A message from the owner's own browser in the same room** (same uid). It shows as Outgoing/Acknowledged, never as an incoming notification. Tested in Task 4 (`own_messages_from_other_devices`).
5. **The network drops during a send.** The insert's transport fails, the result is `Uncertain`, and the next poll reconciles by `client_msg_id` to `Acknowledged` with no duplicate send. Tested in Task 4 (`uncertain_send_reconciles`).

---

### Task 1: Bounded JSON reader

**Files:**
- Create: `shared/core/include/aura/json_reader.hpp`, `shared/core/src/json_reader.cpp`
- Test: `tests/unit/json_reader_test.cpp` (CTest `aura_json_reader_tests`)

**Interfaces:**
- Produces:
  - `enum class JsonType { Null, Bool, Number, String, Array, Object }`
  - `struct JsonLimits { std::size_t max_bytes = 524288, max_depth = 8, max_nodes = 16384, max_string_bytes = 16384; }`
  - `class JsonValue` with:
    - `type()`;
    - `as_string()` (empty unless String);
    - `number_text()` (the raw token);
    - `as_int(std::int64_t fallback)`;
    - `as_bool(bool fallback)`;
    - `items()` (Array);
    - `find(std::string_view key) const -> const JsonValue*` (Object);
    - `string_at(std::string_view key) const -> std::string` (empty if absent or not a string; a Number yields `number_text()`).
  - `bool parse_json(std::string_view text, JsonValue& out, const JsonLimits& limits = {})`

- [ ] Write the tests:
  - `parses_rows`: `[{"a":"x","n":12,"b":true,"z":null}]` gives `items().size()==1`, `string_at("a")=="x"`, `string_at("n")=="12"`, `find("z")->type()==JsonType::Null`.
  - `unicode_escapes`: `"\u0391\ud83d\ude00"` decodes to UTF-8 `Α😀`. A lone surrogate is rejected.
  - `limits`: depth 9 fails; a string longer than `max_string_bytes` fails; trailing garbage `{} x` fails; `max_bytes+1` fails.
  - `rejects_invalid`: `{"a":}`, `[1,]`, an unterminated string and control characters inside a string.
- [ ] Run `.\build\native\aura_json_reader_tests.exe`. Expected: it fails to link.
- [ ] Implement an iterative or depth-bounded recursive descent parser that respects the depth limit. It has no exceptions, keeps numbers as validated raw text and decodes only `\uXXXX` with surrogate pairs.
- [ ] Run the tests and the ESP-IDF flag check through `scripts/test.ps1`. Expected: PASS.

### Task 2: Account protocol (pure functions)

**Files:**
- Create: `shared/core/include/aura/aoraton_account.hpp`, `shared/core/src/aoraton_account.cpp`
- Modify: `simulator/app/include/aura/windows_aoraton_crypto.hpp`, `simulator/app/src/windows_aoraton_crypto.cpp` (add `WindowsAoratonAccountCrypto`)
- Create: `tests/interop/make_aoraton_vault_vectors.mjs` (writes `tests/fixtures/aoraton_vault_vectors.json`, synthetic only)
- Test: `tests/unit/aoraton_account_test.cpp` (CTest `aura_aoraton_account_tests`, links `bcrypt`)

**Interfaces:**
- Consumes: `parse_json`, `JsonValue` (Task 1); `aoraton_base64url_encode/decode`, `AoratonRoomKey`, `kAoratonRoomKeyBytes` (existing).
- Produces, in namespace `aura`:
  - `class IAoratonAccountCrypto` with:
    - `random_bytes(std::uint8_t*, std::size_t)`;
    - `sha256(const std::uint8_t*, std::size_t, std::array<std::uint8_t,32>&)`;
    - `pbkdf2_sha256(const std::string& passphrase, const std::vector<std::uint8_t>& salt, std::uint32_t iterations, std::array<std::uint8_t,32>& key)`;
    - `aes_gcm_open(const std::array<std::uint8_t,32>& key, const std::string& wire, std::string& plaintext)`;
    - all return `bool`.
  - `struct AoratonAccountConfig { std::string base_url, anon_key, redirect_url; }`
  - `struct AoratonSession { std::string access_token, refresh_token, user_id, email, display_name; std::int64_t expires_at_unix{0}; void clear() noexcept; }` (the destructor zeroizes the tokens)
  - `std::string aoraton_pkce_verifier(IAoratonAccountCrypto&)`: 32 random bytes as base64url, 43 chars; empty on failure.
  - `std::string aoraton_pkce_challenge(const std::string& verifier, IAoratonAccountCrypto&)`
  - `std::string aoraton_url_encode(std::string_view)`: RFC 3986 unreserved characters kept.
  - `std::string aoraton_authorize_url(const AoratonAccountConfig&, const std::string& challenge)` returns `<base>/auth/v1/authorize?provider=google&redirect_to=<enc>&code_challenge=<c>&code_challenge_method=s256`.
  - Path and body builders:
    - `aoraton_rooms_owned_path(uid)`: `/rest/v1/rooms?select=*&created_by=eq.<uid>`;
    - `aoraton_member_keys_path(uid)`: `/rest/v1/subscribers?select=room_key&uid=eq.<uid>`;
    - `aoraton_rooms_by_keys_path(const std::vector<std::string>&)`: `/rest/v1/rooms?select=*&room_key=in.(%22k1%22,%22k2%22)`;
    - `aoraton_messages_path(room_key)`: `/rest/v1/messages?select=id,room_key,uid,username,text,type,created_at,client_msg_id&room_key=eq.<k>&order=created_at.desc,id.desc&limit=50`;
    - `aoraton_overview_body(const std::vector<std::pair<std::string,std::string>>& room_since)`: `{"p_items":[{"room_key":"..","since":".."|null}]}`;
    - `aoraton_insert_body(room_key, uid, username, wire, client_msg_id)`.
  - Parsers, each `bool f(std::string_view json, Out&)`:
    - `aoraton_parse_session(json, now_unix, AoratonSession&)`: the token response; reads `user.id`, `user.email` and `user.user_metadata.full_name`;
    - `aoraton_parse_rooms(…, std::vector<AoratonRemoteRoom>&)`, where `AoratonRemoteRoom { std::string room_key, name, expires_at; bool e2ee{false}, is_notes{false}; }` and `name` is `display_name`, else `room_name`;
    - `aoraton_parse_member_keys(…, std::vector<std::string>&)`;
    - `aoraton_parse_overview(…, std::vector<AoratonOverview>&)`, where `AoratonOverview { std::string room_key, last_message_at, last_text, last_username, last_type; std::uint32_t unread{0}; }`;
    - `aoraton_parse_messages(…, std::vector<AoratonRemoteMessage>&)`, where `AoratonRemoteMessage { std::string id, room_key, uid, username, text, type, created_at, client_msg_id; }`;
    - `aoraton_parse_vault_meta(…, AoratonVaultMeta&, bool& configured)`, where `AoratonVaultMeta { std::vector<std::uint8_t> salt; std::uint32_t iterations{600000}; std::string check_blob; }` and an empty array means not configured;
    - `aoraton_parse_vault_rows(…, std::vector<AoratonVaultRow>&)`, where `AoratonVaultRow { std::string room_key, blob; }`;
    - `aoraton_parse_insert_ack(…, std::string& id, std::string& created_at)`, which accepts an array of one or an object.
  - `bool aoraton_parse_invite(std::string_view raw, AoratonInvite&)`, where `AoratonInvite { std::string room_name, room_key; std::array<std::uint8_t,32> rk{}; }`. It requires `key` and a 32-byte `k`, wipes on failure and clears in its destructor.
  - `enum class AoratonVaultResult { Ok, NotConfigured, WrongPassphrase, Corrupt, CryptoFailure }`
  - `AoratonVaultResult aoraton_open_vault(const AoratonVaultMeta&, const std::vector<AoratonVaultRow>&, const std::string& passphrase, IAoratonAccountCrypto&, std::vector<AoratonUnlockedKey>& out)`, where `AoratonUnlockedKey { std::string room_key; std::array<std::uint8_t,32> rk{}; }`. Rows that fail to decrypt are skipped and counted; the check-blob mismatch gives `WrongPassphrase`.
  - `std::string aoraton_new_client_message_id(IAoratonAccountCrypto&)`: `temp_aura` + 32 hex characters.
  - `WindowsAoratonAccountCrypto : IAoratonAccountCrypto` uses `BCryptGenRandom`, the SHA-256 hash, `BCryptDeriveKeyPBKDF2` with HMAC-SHA256, and AES-GCM decrypt. It reuses the existing CNG helpers in `windows_aoraton_crypto.cpp`, moved to an anonymous namespace.

- [ ] Write `make_aoraton_vault_vectors.mjs`. It uses WebCrypto and the synthetic passphrase `AURA synthetic vault ✓`, a fixed salt, 1000 iterations, two rows with fixed synthetic RKs and one row sealed under a different key. Run it with `node tests/interop/make_aoraton_vault_vectors.mjs` to write the fixture.
- [ ] Write the tests:
  - `pkce_rfc7636`: verifier `dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk` gives challenge `E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM`.
  - `pbkdf2_vector`: password `passwd`, salt `salt`, c=1 gives first 32 bytes `55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc` (RFC 7914 §11).
  - `authorize_url` exact string for base `https://x.supabase.co` and the global redirect.
  - `rooms_by_keys_path` with keys `v2.AbC` and `n_1`.
  - `parse_session` (expiry = now + `expires_in`).
  - `parse_overview` (`unread_count` 3).
  - `parse_messages`, where a numeric `id` keeps its text.
  - `invite_fragment_and_query`, `invite_rejects_short_key`.
  - `vault_ok`: 2 keys, the third row skipped.
  - `vault_wrong_passphrase`, `vault_not_configured`.
- [ ] Run and confirm the tests fail. Then implement, run and confirm they pass.

### Task 3: Service API for keys and account sign-in

**Files:**
- Modify: `shared/core/include/aura/aoraton.hpp`, `shared/core/include/aura/aoraton_real.hpp`, `shared/core/src/aoraton_real.cpp`
- Test: `tests/unit/aoraton_real_service_test.cpp`

**Interfaces:**
- Produces:
  - `AoratonCommandType::UnlockVault` and `AoratonCommandType::AddInviteKey`, carried in `plaintext` and wiped by the existing destructor.
  - `AoratonEventType::KeysChanged`. Use `AoratonEvent::key_state` and `AoratonEvent::key_count`.
  - `enum class AoratonKeyState { Unknown, Unlocking, Ready, NotConfigured, WrongPassphrase, NotMember, InvalidInvite, Failed }`.
  - `AoratonSnapshot::key_state` and `AoratonSnapshot::key_count`.
  - `virtual bool unlock_keys(const std::string& passphrase)` and `virtual bool add_invite_key(const std::string& invite)` on `IAoratonService`, both defaulting to `return false`. `RealAoratonService` implements them; they require `Paired`, set `key_state=Unlocking` and enqueue the command.
  - `KeysChanged` updates the snapshot only. Room `key_available` arrives with the next `AuthorizedRooms`.
  - The Paired, Unpaired and SessionRevoked handling resets `key_state` to `Unknown`.

- [ ] Write tests:
  - `unlock_requires_pairing`;
  - `unlock_enqueues_and_wipes`: the command's plaintext equals the passphrase and the snapshot has no passphrase field;
  - `keys_changed_updates_state`.
- [ ] Run, confirm they fail, implement, run and confirm they pass.

### Task 4: Shared account worker

**Files:**
- Create: `shared/core/include/aura/aoraton_account_worker.hpp`, `shared/core/src/aoraton_account_worker.cpp`
- Test: `tests/unit/aoraton_account_worker_test.cpp` (CTest `aura_aoraton_account_worker_tests`, links `bcrypt` for the Windows crypto)

**Interfaces:**
- Consumes: Tasks 1–3, `IAoratonCryptoV2` (existing).
- Produces:
  - `struct AoratonHttpRequest { std::string method, url, body; std::vector<std::pair<std::string,std::string>> headers; }`
  - `struct AoratonHttpResponse { int status{0}; std::string body; }`, where status 0 means a transport failure.
  - `class IAoratonHttp { virtual AoratonHttpResponse send(const AoratonHttpRequest&) = 0; }`
  - `enum class AoratonCallbackStatus { Pending, Received, Failed }`
  - `class IAoratonSignInChannel` with `open(const std::string& url) -> bool`, `poll(std::string& code) -> AoratonCallbackStatus` and `close()`.
  - `struct AoratonStoredSecrets { std::string refresh_token; std::vector<AoratonUnlockedKey> keys; void clear() noexcept; }`
  - `class IAoratonSecretStore` with `load(AoratonStoredSecrets&) -> bool`, `save(const AoratonStoredSecrets&) -> bool` and `erase() -> bool`.
  - `class AoratonAccountWorker` with:
    - constructor `(AoratonMailbox&, AoratonAccountConfig, IAoratonHttp&, IAoratonAccountCrypto&, IAoratonCryptoV2&, IAoratonSecretStore&, IAoratonSignInChannel&)`;
    - `void start(std::int64_t now_ms, std::int64_t now_unix)`;
    - `void step(std::int64_t now_ms, std::int64_t now_unix)`.
- Behaviour:
  - **`start`:** loads the store. If it has a refresh token, the worker refreshes (`POST /auth/v1/token?grant_type=refresh_token`), then emits `Paired{account_label=email}` and `ConnectionChanged{Connected, realtime_active=false}`. If the refresh fails with 400 or 401, it emits `SessionRevoked` and keeps the keys. A status-0 failure gives `ConnectionChanged{Error}`, with a retry 30 s later.
  - **`step`** handles at most 8 commands, then the sign-in poll, then the 10 s poll when connected.
  - **`BeginPairing`:**
    1. Creates the PKCE pair and opens the channel with the authorize URL.
    2. Emits `PairingChallenge{seconds_remaining=300}` with an empty QR and code.
    3. When the code is received: `POST /auth/v1/token?grant_type=pkce {"auth_code","code_verifier"}`, then saves the refresh token and keeps existing keys, then emits Paired and Connected.
    4. After 300 s it emits `PairingFailed{"SIGN_IN_TIMEOUT"}`. A rejected exchange gives `PairingFailed{"SIGN_IN_REJECTED"}`.
    5. `CancelPairing` closes the channel.
  - **Authorized request:** refreshes first when `expires_at_unix - now_unix < 60`. On 401 it refreshes once and retries once. If the refresh fails, the worker emits `SessionRevoked`, wipes the refresh token from the store and keeps the keys.
  - **`RefreshRooms`** (also run every 10 s):
    - The room set is owned plus member rooms, deduplicated, without expired ones (`expires_at` < now), capped at 64.
    - Then `room_overview` with each room's marker.
    - It emits `AuthorizedRooms`, in which `key_available` means the store has the key. The preview is the decrypted `last_text`; it is empty if `last_type != "text"` or the decrypt fails.
    - `unread` comes from the overview. The first sight baselines the marker to `last_message_at` and shows 0.
    - For every room whose `last_message_at` moved past the last seen value, and that has a key, the worker fetches its messages and emits an `IncomingMessage` for each new row (by id and `client_msg_id`). Rows where `uid == session.user_id` instead become history with `direction=Outgoing`, `delivery=Acknowledged`.
  - **`FetchRecentMessages`:** emits `MessageHistory`, with only `type=="text"` rows decrypted. A failed decrypt gives the text `🔒` (the existing locked copy key is used by the UI). Each message's `author` is its `username`.
  - **`MarkRoomRead`:** marker = the newest `created_at` seen in that room.
  - **`SendText`:**
    1. Encrypts with the room's key and emits `SendSubmitted`.
    2. Inserts the row with the username, which is the last `username` the account used in that room, else `display_name`, else `AURA`.
    3. A 201 response gives `SendAcknowledged{server id, created_at}`. Status 0 or 5xx gives `SendUncertain`. Any other 4xx gives `SendFailed{"SEND_REJECTED"}`.
    4. Later polls that see the `client_msg_id` emit `SendAcknowledged`.
  - **`UnlockVault`:** reads `key_vault_meta` and `key_vault`, then calls `aoraton_open_vault`. It merges the keys, saves, emits `KeysChanged{Ready, count}`, then refreshes the rooms. Other outcomes map to `NotConfigured`, `WrongPassphrase` or `Failed`. The passphrase is wiped before return.
  - **`AddInviteKey`:** parses the invite. If the key is not in the member and owned room set, it emits `KeysChanged{NotMember}`. Otherwise it saves and emits `KeysChanged{Ready}`, then refreshes. An unparsable invite gives `InvalidInvite`.
  - **`Unpair`:** calls `POST /auth/v1/logout?scope=local`, ignoring the result. It then erases the store, wipes memory and emits `Unpaired`.
  - **`Disconnect`** stops polling and **`Connect`** resumes it.

- [ ] Write the tests with a scripted fake HTTP (an ordered list of expected `method+path-prefix` and canned responses) and a fake channel. They use the Windows crypto from Task 2, an in-memory store and a v2 wire made with `WindowsAoratonCryptoV2`:
  - `sign_in_happy_path`: the events are PairingChallenge, Paired with the email, then Connected. The store holds the refresh token. The authorize URL contains `code_challenge_method=s256`.
  - `sign_in_times_out`: after a step at 301 s, the result is `PairingFailed SIGN_IN_TIMEOUT`.
  - `restart_uses_stored_refresh`.
  - `revoked_session_requires_sign_in`: a 401 then a failed refresh give `SessionRevoked`; the store's refresh token is empty and its keys are kept.
  - `rooms_overview_and_locked_room`: two rooms, one with a key; the locked room has `key_available=false` and an empty preview.
  - `incoming_after_baseline_notifies_once`.
  - `own_messages_from_other_devices`.
  - `send_acknowledged`, `send_rejected`, `uncertain_send_reconciles`.
  - `unlock_vault_then_rooms_unlocked` (uses the Task 2 fixture), `invite_not_member`.
  - `sign_out_erases_store`.
  - `no_secret_in_events`: after a full run, no event field contains the access token, refresh token or passphrase.
- [ ] Run, confirm they fail, implement, run and confirm they pass. Also run the ESP-IDF flag check.

### Task 5: Windows platform pieces, transport thread and fake-Supabase smoke

**Files:**
- Create:
  - `simulator/app/include/aura/winhttp_client.hpp` + `src/winhttp_client.cpp` (`WinHttpClient : IAoratonHttp`: http and https, 15 s timeouts, 1 MiB response cap, no redirects, never logs bodies or headers);
  - `simulator/app/include/aura/loopback_sign_in.hpp` + `src/loopback_sign_in.cpp` (`LoopbackSignInChannel : IAoratonSignInChannel`, see below);
  - `simulator/app/include/aura/windows_aoraton_store.hpp` + `src/windows_aoraton_store.cpp` (adapter over `WindowsAoratonVault`, with an empty access token);
  - `simulator/app/include/aura/aoraton_account_transport.hpp` + `src/aoraton_account_transport.cpp` (`AoratonAccountTransport`: owns the four platform objects and the worker; one `std::thread` calls `step` every 100 ms; `stop()` joins);
  - `simulator/app/src/aoraton_config.cpp` + header (`bool load_aoraton_config(AoratonAccountConfig&)`);
  - `tests/interop/aoraton_fake_supabase.mjs` (a loopback server implementing exactly the endpoints of Task 4, with synthetic data made by WebCrypto);
  - `tests/interop/run_aoraton_account_smoke.mjs` (starts the fake, runs the smoke exe with env, checks its JSON report);
  - `tests/smoke/aoraton_account_smoke.cpp` (executable `aura_aoraton_account_smoke`, no UI; drives `RealAoratonService` through the real transport).
- Modify: `CMakeLists.txt` (link `winhttp ws2_32 bcrypt crypt32`; add the smoke test when Node exists), `simulator/app/src/main.cpp:273-293` (live/test with config gives `AoratonAccountTransport`; without config the fail-closed path stays, with the error `AORATON_CONFIG_MISSING`).

**Interfaces:**
- **`LoopbackSignInChannel`:**
  - `open(url)` binds `127.0.0.1:4779` (it fails if the port is busy) and starts accepting on its own thread.
  - It opens the default browser with `ShellExecuteW`. With env `AURA_AORATON_BROWSER=fetch` it fetches the URL with WinHTTP and follows redirects (used only by the smoke).
  - It accepts exactly one `GET /aoraton/callback?code=…` and answers with a small Greek and English HTML page ("Η σύνδεση ολοκληρώθηκε, μπορείς να κλείσεις την καρτέλα"). Any other path gets 404.
  - `poll` returns the code once.
- **`load_aoraton_config`:** reads env `AURA_AORATON_URL` and `AURA_AORATON_ANON_KEY`, else `config/aoraton.local.json` `{url, anon_key}`. The URL must be `https://` (or `http://127.0.0.1` when `AURA_AORATON_ALLOW_LOOPBACK=1`, used by the smoke). The redirect is fixed to `http://127.0.0.1:4779/aoraton/callback`.
- **Smoke report:** `{"signed_in":true,"rooms":2,"unlocked":true,"history":N,"sent":"Acknowledged","incoming_notified":1,"signed_out":true}`. It contains no secrets. The store path is a temp file deleted at the end.

- [ ] Write the fake and the runner first. Run `node tests/interop/run_aoraton_account_smoke.mjs`. Expected: it fails (no exe).
- [ ] Implement the platform pieces and the smoke executable. Run the runner. Expected: the report matches and the exit code is 0.
- [ ] Run `scripts/test.ps1`. Expected: PASS, with the new CTest entry `aura_aoraton_account_smoke`.

### Task 6: UI for sign-in, keys and invite paste

**Files:**
- Modify:
  - `shared/ui/include/aura/aura_ui.hpp`: `UiView::AoratonUnlock`; callbacks `on_aoraton_unlock(const std::string&)` and `on_aoraton_paste_invite()`;
  - `shared/ui/src/aura_ui.cpp`: `render_aoraton_rooms`, `render_aoraton_pair`, the new `render_aoraton_unlock` reusing the Wi‑Fi password keyboard pattern, swipe-back and render switch;
  - `shared/localization/...strings` (el and en);
  - `simulator/app/src/main.cpp` (wire the callbacks; the paste reads `SDL_GetClipboardText()`, passes it to `aoraton.add_invite_key` and frees the buffer with `SDL_free`; capture scenarios `aoraton-signin`, `aoraton-account`, `aoraton-unlock`; pointer checks).
- Copy (Greek, final):
  - connect button «Σύνδεση με Google»;
  - help «Ο λογαριασμός σου aoraton.com ανοίγει στον browser του υπολογιστή.»;
  - waiting «Ολοκλήρωσε τη σύνδεση στον browser του υπολογιστή»;
  - title «Λογαριασμός aoraton»;
  - sign out «Αποσύνδεση»;
  - keys row «Κλειδιά δωματίων» with detail «%u δωμάτια» / «Κλειδωμένα»;
  - unlock title «Συνθηματικό αντιγράφου»;
  - button «Ξεκλείδωμα»;
  - paste «Επικόλληση συνδέσμου»;
  - errors:
    - «Λάθος συνθηματικό»;
    - «Δεν έχεις ενεργό θησαυροφυλάκιο κλειδιών στο aoraton.com»;
    - «Δεν είσαι μέλος αυτού του δωματίου· μπες πρώτα από το aoraton.com»;
    - «Ο σύνδεσμος δεν είναι πρόσκληση aoraton»;
  - timeout detail «Αν ο browser έμεινε στο aoraton.com, πρόσθεσε το http://127.0.0.1:4779/** στις Redirect URLs του Supabase.»;
  - polling note «Ανανέωση κάθε 10 δευτερόλεπτα» (shown instead of "live").
  - English equivalents go in `en_us`.

- [ ] Add pointer checks:
  - `aoraton.sign_in` (the connect button calls `on_aoraton_pair_start`);
  - `aoraton.unlock_submit` (typing on the keyboard then «Ξεκλείδωμα» calls `on_aoraton_unlock` with the typed text);
  - `aoraton.paste_invite`;
  - `aoraton.sign_out`.

  They use a capture-override snapshot, like the existing Aoraton checks.
- [ ] Run `scripts/test.ps1`. Expected: pointer N/N at 1x and 2x. Check the new capture PNGs by eye for truncation.

### Task 7: Configuration, run.bat, docs and ledger

**Files:**
- Create: `scripts/setup-aoraton.ps1`.
  - It reads `..\incognitochat\services\supabaseConfig.ts` read-only with a regex for `SUPABASE_URL` and `SUPABASE_ANON_KEY`, else prompts.
  - It writes `config/aoraton.local.json` and prints nothing secret.
- Modify:
  - `.gitignore` (`config/*.local.json`);
  - `run.bat` (if `config\aoraton.local.json` exists, `set AURA_AORATON_MODE=live`);
  - `docs/SIMULATOR.md`, `README.md`;
  - `docs/aoraton/PROGRESS.md`, `AORATON_IMPLEMENTATION_DECISIONS.md` (decision 7 superseded, with a link to the design);
  - `docs/SECURITY_PRIVACY.md` (account session on the PC, DPAPI, global sign-out remedy);
  - `docs/VERIFICATION.md` (rows for Tasks 1–6 and "live aoraton.com sign-in: NOT RUN (owner)");
  - the design doc status.

- [ ] Run `scripts/test.ps1`. Expected: exit 0. Record the results.
- [ ] Hand over to the owner the steps for the live check: run `setup-aoraton.ps1` once, then `run.bat`, sign in, unlock, open the test room and send one message.

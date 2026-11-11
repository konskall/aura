# Security and privacy

The local bridge binds only to `127.0.0.1`, rejects non-loopback peers and unapproved browser origins, requires a bearer token, limits JSON body/prompt/output sizes, runs at most one turn at a time (a newer question replaces the one still running, and a repeated id is refused), runs speech jobs one at a time in arrival order (at most four waiting or running), and enforces timeout/cancellation. Prompts, answers, credentials and tokens are not logged by default.

Codex is spawned with an argument array and `shell: false`, receives its prompt through standard input and runs in an isolated minimal directory with read-only/ephemeral restrictions. The bridge does not read or print authentication files. Saved ChatGPT authentication is reused by the CLI and is not equivalent to production API entitlement.

The optional Groq development backend reads `GROQ_API_KEY` only from the bridge process environment. For repeat owner-requested simulator runs, the launcher may populate that environment from the repository's Git-ignored local `.env`. This file is plaintext local development state: its parser allowlists names, performs no evaluation or expansion, skips empty values, and gives explicit process values priority. The value is never returned by health checks, added to a URL, written to tracked files or logs, or compiled into firmware. Tests use an obvious test-only credential and an injected fake HTTP implementation. On the ESP32 the owner chose, on 2026-11-05, to call Groq and Azure directly ([ADR 0009](ADR/0009-device-direct-cloud-assistant.md)): the keys enter only the device's encrypted NVS, sent once from the PC, and never source, firmware images, logs or UI state. Any key pasted into a prompt or issue is treated as exposed and must be rotated.

The optional neural TTS adapter has no credential. It accepts only the bridge's two voice profiles, maps them to fixed voice identifiers, receives Base64 text through an argument array with `shell:false`, and emits at most 8 MiB of audio. Text and audio are not written to disk or logged. The external `edge-tts` client is installed only below ignored `.tools`, is never packaged into firmware, and talks to an unofficial consumer endpoint with no availability guarantee. Since 2026-10-28 it is never chosen automatically: `-TtsProvider auto` picks official Azure Speech when configured, otherwise Windows voices, and an explicit `-TtsProvider neural` prints a warning. A production device must use the authenticated gateway contract instead. See [ADR 0004](ADR/0004-neural-speech-simulator-adapter.md).

The official Azure Speech simulator provider reads its key only from the bridge process environment, optionally populated from that same ignored local `.env`. The launchers never accept it as an argument or print it. Hidden input rejects implausibly short values; the opt-in clipboard path reads the value in memory and clears the clipboard immediately. The bridge accepts only documented Azure Speech HTTPS hosts, sends bounded escaped SSML with a fixed voice identifier, caps the returned audio at 8 MiB and validates MP3 framing. Foundry project and Azure OpenAI endpoints are not accepted as Speech endpoints. A key pasted into chat is already exposed and must be revoked before use. On the device Azure is called directly with a key kept in encrypted NVS ([ADR 0009](ADR/0009-device-direct-cloud-assistant.md)). See [ADR 0005](ADR/0005-official-azure-speech-simulator-provider.md).

Since 2026-10-30 `run.bat` uses both by default: the audio of each spoken question goes to Groq for transcription, and the text AURA speaks goes to Azure. `run.bat mock` keeps speech recognition on the PC; its voice uses Azure only when an Azure key is configured.

Model, bridge and future network output remains untrusted display text. It is normalized, stripped of disallowed controls, capped and never executed. Side effects require a typed trusted handler plus explicit confirmation. The Aoraton mock likewise separates `prepare` from `confirm`; a displayed draft cannot send itself.

Web access is opt-in per turn. The shared core recognizes an explicit Greek or English search request and sends a boolean capability request; free-form model output cannot enable browsing. Only the Codex runner currently advertises a real search tool, while unsupported providers fail closed. Search pages are untrusted data, private device context remains authoritative, and search answers cannot bypass the existing typed action validator or confirmation UI. See [ADR 0006](ADR/0006-explicit-assistant-web-search.md).

Weather provider credentials remain exclusively in the separately operated `tlesdetector` server. AURA stores no Open-Meteo, Xweather, WeatherAPI or OpenWeather key and does not modify `tlesdetector`; it consumes only that project's existing JSON endpoints. Simulator HTTP is restricted to loopback, device URLs require HTTPS, response sizes are capped, and parsed values must pass the bounded shared weather validator before reaching LVGL or the cache.

Fuel data contains no credential or personal data. The updater accepts only the European Commission Weekly Oil Bulletin page and its current prices-with-taxes workbook; the bridge serves the bounded cache only after bearer authentication and fails closed on any schema/source/range error.

The Windows iPhone experiment is opt-in (`AURA_ANCS_ENABLED=1`). It uses an already paired and authorised ANCS GATT service, never prints or persists notification titles/bodies, and hands bounded events to the LVGL thread. Pairing and unpairing remain explicit Windows UI operations. No iPhone notification is fabricated as live data; the existing injected fixtures remain labelled `DEMO`.

The Aoraton source audit was read-only. AURA did not copy GPL implementation or connect to Supabase. Clean-room v2 crypto is now implemented with Windows CNG and ESP-IDF mbedTLS source; synthetic vectors contain no live room material. Windows test sessions/keys are designed for a current-user DPAPI vault with atomic rotation and zeroization. Tokens and keys never enter snapshots, LVGL, Codex, the bridge or logs. Production transport and pairing remain disabled pending an isolated backend, deployed RLS verification and a separately authorized Aoraton browser/backend patch; see [aoraton/SECURITY_BOUNDARIES.md](aoraton/SECURITY_BOUNDARIES.md) and [aoraton/PAIRING_PROTOCOL.md](aoraton/PAIRING_PROTOCOL.md).

**2026-10-23, owner decision: account sign-in** ([design](design/AURA_AORATON_ACCOUNT_LOGIN_DESIGN.md)). The simulator can now sign in to the owner's own aoraton.com account through the PC browser (Supabase PKCE, return to `127.0.0.1:4779`). This deliberately replaces the device-scoped pairing route: AURA then holds a normal session of that account, with the account's full rights.

- **Never copied from a browser:** AURA performs its own sign-in.
- **Storage on the PC:**
  - The refresh token and room keys are kept only in the current-user DPAPI vault (`%LOCALAPPDATA%\AURA\aoraton-v11.dpapi`).
  - Beside it, `aoraton-cache` keeps the encrypted attachments the simulator has fetched, as the device keeps them on its card (below).
  - The access token stays in memory.
  - The vault passphrase and invite links are wiped right after use.
  - Invite links come from the PC clipboard, never through the control-panel event log.
- **Ending the session:** **Αποσύνδεση** erases the vault and ends the session on the server. Signing out on aoraton.com (global scope) also ends AURA's session, which is the remedy for a lost device.
- **Automated tests** use a loopback fake of the Supabase endpoints and never contact the real project.

**2026-10-24, owner request: inco answers** (`shared/core/src/aoraton_inco.cpp`). On aoraton the device that sent a message mentioning inco produces the answer itself, so AURA now does the same for its own messages. It does nothing unless the owner opts in once: `"inco_enabled": true` in the git-ignored `config/aoraton.local.json`, or `AURA_INCO_ENABLED=1`. It is off by default.

What leaves end-to-end encryption:
- **Sent to aoraton's `inco-ai` edge function, which passes it to Google Gemini:** the question, the room's last 24 readable messages (decrypted, 700 characters each), the local date and time, and, only if the model asks for a location, the saved weather city.
- **Never sent:** the room name, or any LoRa budget.

The answer is written back with the owner's session and RLS:
- it is encrypted with the room key, as inco's uid, in reply to the question;
- there is no service key and no client message id;
- only https sources are kept.

What triggers it:
- AURA answers only its own message, after the insert has returned the stored row.
- It never answers another device's or person's message (that device answers it), inco's own rows, or history from before start-up.
- The room must have `ai_enabled`.
- One answer at a time; failures are logged as a code and never retried.

The log line carries message ids and codes only, never the question, the history or the answer.

Telemetry is off. Runtime logs may contain request IDs, timing, status codes and error classes, but not private prompts or messages. Fictional fixtures are labeled `DEMO` and must never be presented as live facts.

## Account realtime updates (2026-10-31)

Account message updates can now use the existing Supabase Realtime channels (owner request, 2026-10-31; [ADR 0007](ADR/0007-aoraton-client-realtime.md)). Only authorized per-room INSERT subscriptions are requested, using the existing account JWT. No backend settings or SQL are changed. Realtime frames contain untrusted data and can only request REST reconciliation; they cannot execute actions, create sends, call AI, bypass room membership or replace authenticated decryption. The adapter logs no URLs, tokens, frames or message data. Account JWTs stay out of the WebSocket URL; the public anon key is the protocol's API-key parameter. Old subscription generations are ignored after auth/membership changes and sign-out.

## Per-message help from AURA (2026-10-31)

The owner requested help from AURA, separately from the room's inco assistant. A tap on a message opens the full text; a second screen displays that exact message and explains that it leaves E2EE for the AI assistant. Only an explicit confirmation starts the request. Cancellation performs no request. Account sign-out or revocation closes this consent flow.

The simulator submits only the selected text and the localized explanation/reply instruction. It excludes device context, conversation history, room identity and credentials, disables web search, and bypasses local radio/weather/calendar/offline command routing. The reply is display text: even a valid model-generated ACTION cannot start an Aoraton draft or create another action. Neither the selected text nor its answer enters AURA's conversation memory, including on retry. Existing message sending still requires its own destination/text preview and explicit confirmation. Firmware shows the shared message reader; assistance is offered only when its platform provides the callback. Automated checks use DEMO snapshots and the mock provider, without contacting a live account or AI service.

## Local photo library (2026-10-31)

Photos are read locally from the owner's photo directory, never sent to AURA's assistant, the bridge or a network provider. Aoraton images and phone libraries are not imported. Existing photo files are never overwritten; deletion requires an explicit exact-filename confirmation in photo settings or the authenticated browser. Factory reset clears preferences and preserves files. The separately enabled local transfer service can add owner-selected photos, as described below. Only explicitly chosen slideshow timing and the ambient filename are persisted by the photo viewer.

Filenames are validated as bounded display-safe UTF-8 basenames with JPG/JPEG/PNG extensions. Traversal, absolute paths and control characters are rejected; scans do not recurse. Windows rejects directory junctions and file reparse points and validates opened handles. Only the allowlisted settings file is written through a temporary file with atomic replacement. Decoder input, output, expansion and work queues are bounded, with no LVGL allocator or object access on the file worker. Malformed, missing and oversized images produce visible errors rather than executing external content. See `shared/third_party/PHOTOS.md` for pinned sources and licenses.

## Temporary local file transfer (2026-10-31)

The owner can explicitly enable a separate HTTP upload service on the existing private Wi-Fi/LAN address. It is off after boot, expires after ten minutes and is stopped by the visible stop action, power/reset or device Wi-Fi loss. It does not expose the assistant bridge: that bridge remains loopback-only with its existing authentication. No cloud or AI endpoint receives uploaded data.

Each activation uses a cryptographically random six-digit code and independent 256-bit bearer. The QR contains only the local public address; the code is entered separately. Five incorrect codes revoke the activation. Codes and tokens remain in memory, are revoked on stop/expiry and are never logged or persisted. HTTP is plaintext: this feature is intended for a trusted local network, not a hostile/shared hotspot. No firewall rule is installed automatically.

The API checks the private peer, advertised numeric Host and same-origin browser requests. It rejects ambiguous framing, oversized headers, traversal, unsafe/Windows-reserved filenames and existing destinations. Its authenticated listing and deletion endpoints are restricted to ordinary files in the photos/files directories; lists are capped at 128 entries per page. Delete requires a validated exact area/name plus explicit boolean confirmation, shown in a browser dialog before submitting. Windows deletes a non-reparse file by its checked handle. Directories, settings, staging, downloads and other device actions are unavailable. The browser keeps its bearer only in JavaScript memory, renders filenames through textContent and sends files only after the owner's Send action. Larger photos are resized locally for the display without changing the source. Generic uploads are passive files and cannot invoke the assistant or execute code.

Files stream through bounded buffers into private staging, then flush and close before validation/rename. Photos are decoded with the same bounded decoder before admission; generic files are capped at 16 MiB and photos at 4 MiB. No existing file is replaced. Failed, interrupted or expired uploads remove their incomplete staging file, and the Windows adapter excludes reparse points and locks staging across simulator instances. The upload worker never touches LVGL; bounded snapshots trigger gallery refresh on the UI thread. See [ADR 0008](ADR/0008-local-file-transfer.md) and [usage](FILE_TRANSFER.md). Physical Wi-Fi/card/removal validation remains NEEDS HARDWARE.

## ESP32 production image

The normal `scripts/build-firmware.ps1` output remains a development image and intentionally does not burn security eFuses. Release manufacturing uses `scripts/build-firmware-production.ps1 -SigningKey <external-rsa-pem>` with ESP-IDF 5.5.5. That isolated build enables Secure Boot V2 with signed binaries, AES-256 flash encryption in release mode, encrypted NVS with a dedicated `nvs_keys` partition, strong stack protection and light heap poisoning. Its separate partition table starts at `0x10000` so the signed bootloader fits while both OTA app offsets remain unchanged. The signing key is rejected if it is stored inside the repository, the build uses `firmware/build-production`, and the script never flashes a board or modifies eFuses.

Secure Boot and release-mode flash encryption become permanent on the first correctly provisioned boot. A production signing key must therefore be generated and retained by the manufacturer outside source control, backed up securely, and validated through the ESP-IDF manufacturing procedure before any device is powered with the production image. A temporary test key proves that the profile compiles; it is never a deployable signing root.

## Development boards: encrypted NVS (2026-11-05)

Every firmware build encrypts NVS (`CONFIG_NVS_ENCRYPTION`). It holds the Wi-Fi password, the assistant's keys and the settings.

On development boards the XTS keys come from the HMAC peripheral, with a random key that the first boot burns into eFuse block 5 (`CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=5`):

- No software can read that eFuse key.
- Burning it is permanent, but it neither limits flashing or debugging nor uses the blocks secure boot and flash encryption need.
- A board erased and reflashed keeps the same eFuse key, so its NVS stays readable to AURA.

This protects a dump of the flash. It does not protect against someone who can flash their own firmware, which is what secure boot is for in production. `tests/unit/firmware_config_test.cpp` checks:

- that development builds keep this setting;
- that they never enable secure boot or flash encryption;
- that the production profile keeps both, in release mode, with NVS keys in `nvs_keys`.

## Signed updates (2026-11-05)

An update installs only with the owner's signature: the manifest must carry an ECDSA P-256 signature (`signature`, 128 hex characters) over `AURA-OTA-1`, the version, the size and the SHA-256 of the image (`update_signing_message`). The device checks it with mbedTLS against the public key it was built with (`CONFIG_AURA_OTA_PUBLIC_KEY`) before it downloads anything, then checks the image against the signed SHA-256. A build without the key installs nothing (`UPDATE_KEY_MISSING`); an unsigned or wrongly signed manifest is refused (`MANIFEST_UNSIGNED`, `MANIFEST_BAD_SIGNATURE`); a newer version is still required, so an old signed image cannot come back.

- `node scripts/update-signing.mjs keygen <file outside the repository>` makes the key pair. It refuses a path inside the repository or an existing file, saves the private key and prints the `CONFIG_AURA_OTA_PUBLIC_KEY` line for `firmware/sdkconfig.defaults`. Keep an offline copy of the private key: devices built with its public key accept updates signed with it only.
- `node scripts/update-signing.mjs sign --image <bin> --version <x.y.z> --url <https://...> --key <file> --out <manifest.json>` writes the signed manifest.
- `tests/interop/run_update_signing_test.mjs` signs with the script and verifies with the parser the device uses (the simulator's BCrypt verifier). That run covers four refusals: a changed version, a changed hash, another key and a missing signature.

This signature works without secure boot, so a compromised update server cannot install firmware even on a development board. Production adds Secure Boot V2 on top of it.

## The device's assistant keys (2026-11-05)

See [ADR 0009](ADR/0009-device-direct-cloud-assistant.md). In short:

- the keys go from the ignored `.env` to the device over the code-protected transfer session (`scripts/send-device-keys.ps1`, `POST /api/keys`);
- the format is checked before they are stored in encrypted NVS;
- no response echoes them, and no log, screen or file holds them;
- the audio and text of a question go to Groq and Azure;
- if a device is lost, revoke both keys.

## The device's aoraton.com account (2026-11-06)

See [ADR 0010](ADR/0010-device-aoraton-account.md).

- **Sign-in.** The device creates its own PKCE verifier, session and refresh token. The owner's PC, over File transfer's code-protected session, only:
  - sends the project's public URL and anon key;
  - opens Google's page;
  - hands the one-time code back. The code is useless without the device's verifier.
- **Sign-in from the phone (2026-11-07).** Without a PC, the phone does the PC's part. The device's QR leads it to Google's page. Google's page returns it to a page on aoraton.com, which sends the code on to the device over the local network.
  - **The QR's secret.** It is 16 random bytes, new for each tap, compared in constant time. Eight wrong ones end the sign-in. It also ends with the sign-in itself, with File transfer, or with the PC's sign-in.
  - **The two pages** (`/aoraton/phone`, `/aoraton/callback`) answer only the local network, at the device's own address. They run no script, send no referrer and never show the code.
  - **The project's values.** The public URL and anon key are built into the firmware from the git-ignored `config/aoraton.local.json`. Neither is a secret: aoraton.com's web app ships both.
  - **aoraton.com's relay page** (`/aura-phone/`). Supabase returns a phone to no IP address except loopback, so Google's page returns it to aoraton.com. The page forwards only to a private IPv4 address or 127.0.0.1, takes the code out of its own address and history, keeps nothing and sends no referrer. Because it forwards to any private address, a sign-in link someone else started could hand that person a session at a host on the network the phone is on, if the owner signs in through that link. PKCE does not stop this, because such a link carries the other person's own challenge. Sign in only from the QR your own device shows.
- **Secrets at rest.** The refresh token and the room keys live in encrypted NVS (namespace `aura_aoraton`) as one bounded record. The access token stays in memory. Αποσύνδεση wipes the record.
- **Room keys.** The vault passphrase is typed on the device and never crosses the network. An invite link from the PC (`--invite`) carries one room's key over the local network in plain HTTP: use a trusted Wi-Fi.
- **Files.**
  - AURA fetches attachments only from the configured project's public `attachments` bucket, at most 4 MiB each, and opens them with the room's key.
  - Voice messages it sends are sealed the same way the web client seals files: their own AES-256-GCM key, wrapped with the room key.
  - Nothing about a file (name, contents or key) is logged.
- **The microSD card.**
  - The device keeps the attachments it has fetched in `/sdcard/aura/.cache/aoraton`. They are kept exactly as the public bucket serves them to anyone: AES-256-GCM ciphertext.
  - No key, thumbnail or decrypted byte is written. Each file is named by the SHA-256 of its address.
  - At most 128 MiB, the least recently used file out first. A copy that no longer opens is dropped and fetched again.
  - Αποσύνδεση empties the cache.
  - The card does show how many files were opened, and their sizes.
- **Logs.** The PC helper prints no code, token, key, PIN or invite. On the device, the serial log carries the crypto self-test result and realtime status codes only.

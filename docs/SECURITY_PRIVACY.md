# Security and privacy

The local bridge binds only to `127.0.0.1`, rejects non-loopback peers and unapproved browser origins, requires a bearer token, limits JSON body/prompt/output sizes, runs at most one chat turn and one translation at a time (a newer question replaces the one still running, and a repeated id is refused), runs speech jobs one at a time in arrival order (at most four waiting or running), and enforces timeout/cancellation. Prompts, answers, credentials and tokens are not logged by default.

Codex is spawned with an argument array and `shell: false`, receives its prompt through standard input and runs in an isolated minimal directory with read-only/ephemeral restrictions. The bridge does not read or print authentication files. Saved ChatGPT authentication is reused by the CLI and is not equivalent to production API entitlement.

The optional Groq development backend reads `GROQ_API_KEY` only from the bridge process environment. For repeat owner-requested simulator runs, the launcher may populate that environment from the repository's Git-ignored local `.env`. This file is plaintext local development state: its parser allowlists names, performs no evaluation or expansion, skips empty values, and gives explicit process values priority. The value is never returned by health checks, added to a URL, written to tracked files or logs, or compiled into firmware. Tests use an obvious test-only credential and an injected fake HTTP implementation. The ESP32 must eventually call an authenticated HTTPS gateway with a revocable device-scoped credential; upstream provider keys never enter source, NVS, firmware images or UI state. Any key pasted into a prompt or issue is treated as exposed and must be rotated.

The optional neural TTS adapter has no credential. It accepts only the bridge's two voice profiles, maps them to fixed voice identifiers, receives Base64 text through an argument array with `shell:false`, and emits at most 8 MiB of audio. Text and audio are not written to disk or logged. The external `edge-tts` client is installed only below ignored `.tools`, is never packaged into firmware, and talks to an unofficial consumer endpoint with no availability guarantee. Since 2026-10-28 it is never chosen automatically: `-TtsProvider auto` picks official Azure Speech when configured, otherwise Windows voices, and an explicit `-TtsProvider neural` prints a warning. A production device must use the authenticated gateway contract instead. See [ADR 0004](ADR/0004-neural-speech-simulator-adapter.md).

The official Azure Speech simulator provider reads its key only from the bridge process environment, optionally populated from that same ignored local `.env`. The launchers never accept it as an argument or print it. Hidden input rejects implausibly short values; the opt-in clipboard path reads the value in memory and clears the clipboard immediately. The bridge accepts only documented Azure Speech HTTPS hosts, sends bounded escaped SSML with a fixed voice identifier, caps the returned audio at 8 MiB and validates MP3 framing. Foundry project and Azure OpenAI endpoints are not accepted as Speech endpoints. A key pasted into chat is already exposed and must be revoked before use. Direct Azure access remains simulator-only, while production hardware uses the gateway boundary described above. See [ADR 0005](ADR/0005-official-azure-speech-simulator-provider.md).

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

## ESP32 production image

The normal `scripts/build-firmware.ps1` output remains a development image and intentionally does not burn security eFuses. Release manufacturing uses `scripts/build-firmware-production.ps1 -SigningKey <external-rsa-pem>` with ESP-IDF 5.5.5. That isolated build enables Secure Boot V2 with signed binaries, AES-256 flash encryption in release mode, encrypted NVS with a dedicated `nvs_keys` partition, strong stack protection and light heap poisoning. Its separate partition table starts at `0x10000` so the signed bootloader fits while both OTA app offsets remain unchanged. The signing key is rejected if it is stored inside the repository, the build uses `firmware/build-production`, and the script never flashes a board or modifies eFuses.

Secure Boot and release-mode flash encryption become permanent on the first correctly provisioned boot. A production signing key must therefore be generated and retained by the manufacturer outside source control, backed up securely, and validated through the ESP-IDF manufacturing procedure before any device is powered with the production image. A temporary test key proves that the profile compiles; it is never a deployable signing root.

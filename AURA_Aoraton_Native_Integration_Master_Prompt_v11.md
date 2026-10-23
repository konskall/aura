# AURA × AORATON — NATIVE INTEGRATION MASTER PROMPT v11.0
## Secure account/device pairing · Real Supabase rooms · End-to-end encrypted messages · Realtime notifications

### READ THIS FIRST — MISSION AND SCOPE

You are the principal embedded engineer, application security engineer, Supabase integration engineer, and UX lead working on **AURA**, an existing AI assistant for the **Waveshare ESP32-S3-Touch-AMOLED-2.16**, with a real **480×480 LVGL touch UI**, 8 MB PSRAM, 16 MB Flash, Wi-Fi, battery power, audio, and a Windows 10 LVGL simulator.

The mission is to **replace the current `MOCK/DEMO` Aoraton messaging implementation with a real, securely paired Aoraton client** while preserving the established AURA interface, Mercury Orb, Lucide icon registry, IBM Plex Sans Greek typography, Codex host bridge, audio service, weather, and existing capabilities.

**Desired experience:** A user who has an account/session in **https://aoraton.com** pairs their AURA device using their already signed-in browser or phone. After pairing, AURA displays the rooms that this user is authorized to access *and for which the device has the corresponding E2EE room keys*, receives/decrypts incoming messages, sends encrypted messages, displays unread indicators, and shows notifications. Once paired, normal operation on the actual ESP32-S3 should require only Wi-Fi; no constantly running Windows PC or phone should be necessary if a secure compatible architecture is achievable.

**The device has no camera.** For pairing, the AURA screen should **display** a QR code for the user's phone/browser to scan, not attempt to scan one itself.

This is an **existing-project integration**, not a new chat app and not a UI redesign from zero. Do not replace the mock with fake live-looking fixtures. Do not declare production integration complete until real interoperability tests pass.

---

## 1. AUTHORITATIVE LOCAL PROJECTS AND MATERIAL

Existing AURA workspace (confirm actual repository root before editing):

`C:\Users\polis\Documents\AiCube`

Existing Aoraton source checkout:

`C:\Users\polis\Documents\incognitochat`

Audit documents in AURA, inspect rather than assuming their contents:

- `docs/aoraton/INTEGRATION_AUDIT.md`
- `docs/aoraton/PROTOCOL_ADAPTER.md`
- `docs/aoraton/E2EE_COMPATIBILITY.md`
- `docs/aoraton/SECURITY_BOUNDARIES.md`

Inspect current `IAoratonService`, `MockAoratonService`, routes/screens, mocks, tests, platform abstraction, Git status, active branch, build system, simulator, firmware components, existing auth helpers, and data persistence.

Aoraton was audited as React 18 / TypeScript / Vite PWA using Supabase Auth, Postgres, Realtime, Storage, Edge Functions, RLS and `SECURITY DEFINER` RPCs. Actual code is authoritative; recheck all findings.

**Source access policy:** Treat `incognitochat` as **read-only**. Implement all new AURA code in the AURA repository. If web Aoraton needs pairing endpoints, UI, schema, migrations, or Edge Functions, design an exact patch and reviewable change proposal inside AURA, explaining affected files, database effects, security, rollback, and deployment. **Do not modify, deploy or run migrations against the original Aoraton repository or production backend without explicit authorization.** In particular, do not edit Supabase RLS or invoke service-role mutations on production.

Make a recoverable Git baseline in AURA; preserve uncommitted changes and never use destructive resets/cleaning commands. Keep a concrete implementation status ledger.

If any local folder or source file is unavailable, identify the missing prerequisite explicitly and continue with isolated testable contracts; never invent protocol behavior.

---

## 2. PRODUCT TRUTH: AUTHENTICATION IS NOT E2EE ACCESS

Aoraton supports anonymous access by default and optional Google login. A browser account/session is not equivalent to possessing every room's E2EE key. Some rooms may only be known locally in the browser, some may not be associated with the signed-in account, and some keys may not be recoverable after losing their invite fragments.

Do not imply that “logging in” automatically retrieves or decrypts all rooms. Implement two distinct concepts:

1. **Account/device authorization:** identity or approved anonymous session, permitted access under Supabase RLS/RPCs, session expiry/revocation.
2. **Room-key authorization:** securely provision the correct 256-bit secret for *each user-selected room*, only from an already authorized holder of that secret.

When user asks to see “my rooms”, list only rooms supported by actual membership/session plus valid key availability. Display a truthful `Requires pairing/key` state for rooms that cannot be decrypted. Never bypass the real room membership or PIN gate. Treat any optional PIN as an access gate, **not** the E2EE encryption key.

Support at least the signed-in Google/Supabase user path. Investigate whether the existing anonymous room workflow can be safely paired; implement it only after validating the existing security model. Do not create a fake global contacts/accounts model where Aoraton only has anonymous rooms and aliases.

---

## 3. VERIFIED E2EE PROTOCOL AND CRYPTOGRAPHIC PARITY

Previous audit reports **E2EE v2**:

- Random 256-bit room key.
- AES-256-GCM with a fresh random 12-byte IV for each message.
- HKDF-SHA256 with distinct contexts/subkeys for messages, files and auth.
- Wire serialization: `v2:<iv>:<ciphertext+tag>`.
- Browser may receive room key through a URL **fragment** (not ordinarily sent to web server).

**Verify actual encoding, salt/info parameters, AAD rules, nonce generation, tag length, string normalization, message metadata, and error behavior from source.** Do not assume formats omitted from this summary. Review any legacy v1 path but do not implement unsafe silent downgrades.

Implement a **clean-room wire-compatible ESP32/desktop crypto adapter**, using proven maintained libraries supported by the target platform (e.g. audited mbedTLS APIs on ESP-IDF). Do not copy GPL-3.0-only Aoraton source into a differently licensed AURA project by default. Document licensing implications, including dependency licenses, generated fixtures and ownership of any source proposed for reuse. A compatibility implementation from independently specified protocol behavior and test vectors is preferred, subject to legal review.

Crypto invariants:

- Keys never reach the LLM, Codex bridge, UI, logs, analytics, telemetry or generic event bus.
- Plaintext stays in the privileged client boundary and UI only when needed.
- No plaintext chat history or room keys on unencrypted microSD.
- Fresh CSPRNG IV for every encryption; detect and prevent unsafe reuse.
- Authenticate before showing plaintext; reject corrupt tag, wrong key, malformed envelope, unexpected version and truncated payload.
- Avoid timing-sensitive comparisons and accidental copies of sensitive key material.
- Zeroize sensitive temporary buffers where supported.
- Enforce bounded plaintext and ciphertext sizes based on actual Aoraton limits; the mock's 512 UTF-8 byte limit is **not automatically** the production protocol limit.
- Correct UTF-8 handling, Greek combining characters, emoji and multiline messages.

Create **browser ↔ AURA known-answer test vectors** using only synthetic test keys/text. Test browser-encrypt/device-decrypt and device-encrypt/browser-decrypt; validate corrupted tags, invalid IV, missing fields, wrong keys, long UTF-8 and duplicate messages.

---

## 4. AUTHORIZATION AND PAIRING UX — PREFER PHONE/BROWSER

Implement a secure **Link AURA Device** flow, not a miniature Google login page or password form on the ESP32.

**Proposed UX (subject to existing auth compatibility):**

1. On AURA, user opens **Aoraton → Connect account**.
2. AURA creates a cryptographically random, short-lived, one-use pairing challenge and ephemeral ECDH keypair, and displays a QR representing only safe public pairing parameters (pairing ID, origin/endpoint, ephemeral public key, expiry). Never put room secrets, session refresh tokens, or invitation fragments in plain QR.
3. On phone/PC, user opens **aoraton.com**, authenticates through the existing supported flow, and chooses a new explicit **Pair AURA** action. If no pairing UI exists, produce a separate, reviewable patch proposal for this existing application; do not silently introduce an unrelated public site.
4. The trusted web client scans the QR or enters an alternate short pairing code and verifies user identity, room membership and existing room-key possession.
5. The browser asks for explicit consent: device display name, precise selected rooms, permissions (read, notify, send) and a clear revocation option. Avoid default permission to all rooms where not necessary.
6. Use authenticated key agreement and authenticated encryption for secure key transfer. Include mutual confirmation of pairing challenge and **human-verifiable short authentication string (SAS)** or an equivalent reviewed anti-relay mechanism. Bind session, browser, device and challenge to prevent QR substitution, replay or pairing hijacking.
7. Convey device-specific authorized credentials/session using a documented Supabase-supported or securely reviewed custom mechanism. Do **not** just copy the browser's unrestricted refresh token or service-role key to firmware. If a correctly scoped native device session cannot be established without a server-side enhancement, evaluate a tightly authorized Supabase Edge Function mediation design with robust RLS/membership checks. Do not invent unsupported Supabase token-grant APIs or mint arbitrary impersonation JWTs.
8. Provision only the room encryption keys that the user explicitly selects. Transfer them end-to-end encrypted so the pairing relay cannot read them. The receiving device verifies and stores only approved room keys.
9. Show pairing success and first live synchronization in the AURA UI.
10. Support cancel, timeout, expiry, replay rejection, wrong account, revoked room, browser logout, device reboot, recovery and secure unpair.

Prefer QR displayed by AURA and scanned by the browser phone. Since the device has no camera, never require it to scan QR. Offer a secure manual-code fallback if the phone lacks a usable scanner; manual code must not be the sole factor authorizing account takeover.

Pairing should not require the phone to remain nearby after activation. Do not make normal firmware operation depend on Codex, a local proxy or a constantly running personal PC. Existing managed Supabase/Edge Functions infrastructure may be used if justified and operated securely.

Aoraton room keys might originate in URL fragments or local browser storage. **Do not send fragments or decrypted keys to ordinary server endpoints.** Explicitly review safe browser-to-device key delivery against the app's actual key lifecycle.

---

## 5. SUPABASE AUTH, RLS, RPC AND SESSION MANAGEMENT

Build an independent `RealAoratonService` behind the existing `IAoratonService` boundary. Reuse only actual, tested Aoraton schemas and protocol rules.

Discover from source:

- Supabase project public URL/publishable key handling.
- Auth session and refresh behavior.
- Membership RPC names/permissions and exact arguments.
- RLS policies, table/column names and indexes.
- Realtime channel topics, event types and private channel authorization.
- Storage/media scope, if any (but media is **not** Phase 1).
- Message create/list APIs, ordering, pagination, idempotency fields.

Never put a **Supabase service-role/secret key** on the device or in the browser. A publishable/anon key is not a privileged secret but still requires correct RLS and authenticated user/session access. All privileged Edge Functions must authenticate the caller, check explicit room authorization, enforce minimal data return, log only safe metadata and rate-limit abuse.

Handle JWT lifetime and refresh token rotation correctly. Refresh tokens can be single-use; avoid concurrent refresh races, crashes during rotation and stale-token overwrite. Re-authorize Realtime when a new JWT is issued. Revocation and device-unpair must invalidate further API access and remove local access to keys.

Implement robust TLS certificate validation, hostname checks, accurate time bootstrap/RTC/NTP consideration, HTTP/WebSocket timeouts, bounded JSON parsing, exponential reconnection backoff with jitter, cancellation and meaningful errors. Never disable TLS verification.

If the existing auth/RLS scheme cannot express device-scoped permission safely, document the missing backend contract and implement only the secure tested part; do not claim complete account linking.

---

## 6. ROOM DISCOVERY, MESSAGE LIST AND SYNC

User-facing functionality:

- List real authorized, linked rooms with their display names/aliases, last-message preview if locally decrypted, unread counts, connection status and latest timestamp.
- Handle rooms without a key (`Locked / Pair to decrypt`) rather than inventing text.
- Select a room and load recent real messages, decrypted on device.
- Paginate older messages with bounded memory and efficient list virtualization.
- Subscribe to incoming updates when feasible; dedupe against REST/backfill using the actual server ID and `client_msg_id` as appropriate.
- Maintain correct chronological ordering, retry state, outbox/acknowledgment state and offline reconciliation.
- Track unread per actual Aoraton semantics; do not invent read receipts or claim that a remote user read a message.
- Detect revoked membership and room expiry, and immediately stop access to expired/unavailable rooms.

**Realtime strategy:** Validate native Supabase WebSocket + private channels + TLS on both Windows and ESP32-S3. On device, measure free heap, largest contiguous block, PSRAM and CPU. If continuous Realtime exceeds acceptable hardware resources or power, implement a clearly labeled fallback (adaptive polling/foreground refresh) and accurate availability semantics. Do not claim instant push notifications when only periodic polling is implemented. Use REST catch-up after reconnect to avoid missed messages.

Honor disappearing messages, deleted content, auto-expiring rooms and view-once semantics wherever relevant. Don't persist content that the app explicitly intends to disappear.

---

## 7. REAL MESSAGE COMPOSITION AND SENDING

Preserve the existing AURA Greek keyboard, composer, preview and explicit send confirmation. Improve only where necessary without losing touch behavior.

Exact flow:

`Choose room → Compose → Validate → Preview destination + exact text → Explicit confirm → Encrypt locally → Send via real authorized endpoint → Record server result → Reconcile in room`.

Requirements:

- Verify membership, encryption key and actual user-selected destination before allowing send.
- UTF-8 byte validation uses real protocol limits.
- Use new AES-GCM IV for each ciphertext.
- Preserve Aoraton's `client_msg_id` idempotency strategy. Handle retries without generating accidental duplicate messages; differentiate uncertain response from actual delivery.
- No automatic send by voice or AI without on-device confirmation.
- Never show “Sent” until the transport confirms the appropriate stage. Distinguish queued, submitted, acknowledged, failed and uncertain state.
- Allow cancel before send; do not falsely allow undo after a committed send.
- Error messages in Greek; do not leak sensitive details.

Phase 1 scope is **text messages only**. Images/files/view-once attachments remain later milestones unless already supported through a genuinely compatible, secure implementation.

---

## 8. LIVE INCOMING MESSAGES AND NOTIFICATIONS

The device must visibly react when an authorized room receives a new message:

- Update rooms list and unread badge.
- Enqueue a discreet AURA-style notification banner with Aoraton icon, room alias, timestamp and optional preview.
- Tap notification to open the correct room; preserve navigation back-stack.
- Support notification privacy settings: default show minimal metadata, optionally hide preview on ambient/locked screen.
- Show indicator for disconnected/reconnecting; on reconnect fetch missed messages.
- Optional notification sound using the existing audio engine with volume/mute/quiet-hour policy; do not disrupt AI speaking without controlled audio priority.
- Decrypt only after verified membership, key and authenticated ciphertext; no untrusted data becomes executable instructions.
- Rate-limit/coalesce bursts and avoid repeated alerts for the same message.

On ESP32, distinguish **online while awake**, **screen off but Wi-Fi active**, and **deep sleep**. Do not promise background instant notifications while radio connectivity is suspended. Measure battery costs and offer notification power modes.

---

## 9. 480×480 TOUCH UI — PRESERVE PREMIUM AURA LANGUAGE

Use existing AURA theme, Mercury Orb identity on core AI screens, Lucide icon registry, correct IBM Plex Sans Greek fonts and status bar. Aoraton should feel like a native first-party application, not an embedded browser and not a mismatched chat clone.

Build/update these states with authentic content:

**Aoraton Landing:** Not paired → primary `Σύνδεση Aoraton`, explanatory privacy text, short setup steps. Paired → real room list, unread counts, refresh/connection status.

**Pair Device:** Large QR with enough quiet zone, short-lived expiry timer, pairing state, optional safe fallback pairing code, Cancel, failure/retry, explicit trusted-browser confirmation. QR must remain scannable at 480×480; keep secure status clear.

**Room List:** Scrollable authorized rooms, connection status, unread badge, last decrypted snippet only if privacy setting permits, room locked/key-missing state, empty state and expired room state.

**Conversation:** Existing scrolling bubbles, verified decrypted text, timestamps, loading/pagination, reconnect marker, send status, compose action, top bar with real room name.

**Compose:** Existing Greek keyboard; live size counter if relevant; no underlapping keyboard/text fields; full-width tappable actions.

**Confirmation:** Visible exact room and message; Cancel/Confirm Send; countdown/cancelation if needed.

**Notifications:** Integrated global notification center; tap opens correct message/room; unread changes reflect actual service events.

**Device Access & Privacy:** Paired account identity where available, rooms shared with AURA, per-room enable/disable if supported, notification preview toggle, re-pair/unpair, safe erase local secrets, last sync and diagnostic status.

**Error/Offline/Expired:** Clear differentiated icons and actions; no fake successful state; cached content must be labeled, TTL respected.

Touch rules: every entire visible button/tile is tappable, not just its label. Targets approximately 44×44 px minimum where possible; pressed feedback; back/close actions always functional; correct focus and scroll; no overlapping invisible layers. Ensure Greek texts do not clip; test very long room titles, accented Greek, emoji, and multiline messages. Avoid gratuitous decorative icons/features.

**Remove `MOCK/DEMO` labels only where a real, verified backend is active.** Keep clearly marked mock mode for testing. Do not make production mode silently fall back to fictional conversations.

---

## 10. AI ACTION ENGINE — FUTURE VOICE / NATURAL LANGUAGE, SAFE FIRST

Integrate a typed allowlisted action layer, usable by Codex in the PC simulator and future cloud AI provider on device. Do not grant the LLM raw Supabase queries, filesystem secrets, shell execution or room-key access.

Implement/prepare tools:

- `aoraton.list_authorized_rooms`
- `aoraton.get_unread_counts`
- `aoraton.open_room`
- `aoraton.prepare_message`
- `aoraton.confirm_send` (only after a fresh explicit UI confirmation token)
- `aoraton.cancel_pending_message`
- `aoraton.get_connection_status`

Example: «AURA, στείλε στη Βάσια να με πάρει τηλέφωνο άμεσα.»

Resolution must use an **explicit user-defined local alias → authorized room mapping** only if the current Aoraton protocol supports it. If ambiguous or missing, ask which room; don't invent a contact or phone-number identity. Preview the exact text and room and wait for affirmative confirmation before encryption/send.

By default, decrypted incoming messages and histories must **not** be sent to Codex/ChatGPT/Claude. Simple unread counts and room navigation can be handled without exposing private message bodies. Any external-AI-assisted summarization/reading of private text must be opt-in with clear disclosure and minimum data. Data from incoming messages must be treated as untrusted content, not instructions. A room event must never trigger the local Codex CLI or send a message autonomously.

Full voice interaction is not a blocker to delivering real secure manual messaging. Implement messaging first, then typed actions safely.

---

## 11. PLATFORM AND STORAGE DESIGN

Reuse `IAoratonService`; add a production-oriented adapter and composition root choosing **mock/test/live** by explicit configuration.

Suggested testable components (adapt naming to existing architecture):

- `AoratonAuthSession` — authenticated device identity, refresh, expiry, revocation.
- `AoratonPairingManager` — QR challenge, browser approval, selected rooms, secure key transfer.
- `AoratonRoomKeyVault` — controlled key import, key lookup, wipe.
- `AoratonCryptoV2` — clean-room HKDF/AES-GCM/serialization.
- `AoratonTransport` — REST/RPC and optional Realtime.
- `AoratonRoomRepository` — room list/pagination/membership.
- `AoratonMessageSync` — fetch/subscription/dedupe/catch-up.
- `AoratonNotificationCoordinator` — user-visible local events.
- `AoratonService` — implements existing interface, emits typed events.

Desktop secure storage: use operating system-backed facilities where possible (e.g. Windows DPAPI/Credential Manager), avoiding hardcoded local files and secrets in logs. Hardware secure storage: evaluate ESP-IDF NVS encryption, flash encryption/secure boot **before** irreversible provisioning, with documented risk/recovery; never assert secure storage exists merely because it is planned. Explicit lock/unpair/erase functions. No plaintext room history on SD.

No filesystem or network calls inside LVGL input/event handlers; async queues and cancellation. Bound heap/PSRAM and message cache; cap network frame size and parser nesting. TLS and codec memory must be evaluated alongside existing Mercury animation, weather, AI and audio subsystems. Record real resource measurements on hardware when available.

For the simulator, use real auth + real test data against an isolated Supabase test project; for the ESP32, provide buildable functional code as possible, but mark any physically unverified component clearly.

---

## 12. ENVIRONMENTS, TEST FIXTURES AND MIGRATION SAFETY

**Required first:** isolated nonproduction Supabase/Aoraton test environment, using actual verified migrations and RLS policies from the audited project. Use Supabase local tooling or a dedicated test project as practical. Do not connect CI or end-to-end tests to production by default. Do not leak keys in screenshots, test logs or Git.

Use two synthetic identities with different authorized rooms plus an unauthorized identity. Provision actual sample E2EE room keys through the correct pairing flow. Test real browser ↔ simulator message roundtrips, not merely the AURA mock.

Any required new Aoraton app UI, database migration or Edge Function must be proposed as a separate, auditable patch set before applying it to the original `incognitochat` repository or production. This prevents breaking existing Aoraton users.

Create a configuration matrix:

| Mode | Messaging | Auth | Keys | UI marking |
|---|---|---|---|---|
| Mock | Fictional local | Fake | Fake synthetic | MOCK |
| Integration Test | Real isolated Supabase | Test account | Real synthetic E2EE keys | TEST |
| Live/Production | Real Aoraton service | Authenticated paired device | Authorized user keys only | LIVE after verification |

No implicit environment downgrade or mixed-up endpoints. Require confirmation before first production linking or any production write action.

---

## 13. REQUIRED SECURITY, FUNCTIONAL AND FAILURE TESTS

Cover with automated tests where possible:

**Auth/pairing:** signed-in authorized pairing; unsupported anonymous session handling; expired QR; reused QR; wrong-account approval; revoked pairing; stolen device; cross-room access; unauthorized room; canceled pairing; lost connection; logout; token expiry and refresh-rotation race; browser not possessing room key; SAS/challenge mismatch; no-camera device UX.

**Crypto:** AES/HKDF test vectors from existing browser; Greek text/emoji; corrupted tag, altered ciphertext/IV, wrong key, malformed `v2` envelope; no plaintext/token leaks; legacy-version rejection or explicitly compatible handling.

**Messaging:** two-client send/receive; messages created via real authenticated RPC/API; `client_msg_id` idempotency; no duplicate on retry; pagination; unread counters; reconnect REST catch-up; room expiry/deletion; logout while receiving; offline drafts; long messages; RLS-denied send; revoked membership.

**Notifications:** event while on Home, while on Aoraton, with screen off but network available; coalescing; taps open correct room; no preview on privacy mode; optional sound respects mute; no notifications for unauthorized messages; honest behavior while radio asleep.

**UI/touch:** QR/scanner flow on phone (not device); tap icon/label/tile center/edge; scroll list/conversation; Greek keyboard; confirmation; error/empty/unpair; back/home, long names; no clipping at 480×480.

**Performance:** transport + TLS memory budget; simultaneous LVGL animations/audio/weather and message arrival; 100-message pagination; reconnect storms; PSRAM fragmentation; battery impact of Realtime vs polling; watchdog stability.

Do not count generated files, static screenshots, or a successful build as proof of end-to-end functionality. Use real requests against the isolated project and inspect actual encrypted payloads, RLS results and UI events.

---

## 14. IMPLEMENTATION MILESTONES — FOLLOW IN ORDER

### Milestone 0 — Baseline and protocol confirmation
- Snapshot Git safely; inspect real `IAoratonService` and source audit.
- Reproduce existing mock navigation/tests.
- Confirm Supabase schema, Auth, RPC, realtime, E2EE serialization and license constraints.
- Write a compact `AORATON_IMPLEMENTATION_DECISIONS.md` with hard blockers and selected secure pairing method.

### Milestone 1 — Real integration test backend and crypto
- Set up isolated Supabase/Aoraton environment with real RLS.
- Implement typed clean-room E2EE v2 codec.
- Pass browser ↔ AURA known-answer tests.
- Create live REST messaging service for test users.

### Milestone 2 — Live Windows simulator messaging
- Connect authenticated test identity securely.
- Show actual authorized rooms and real decrypted messages.
- Send real encrypted messages with explicit confirmation.
- Receive updates, dedupe, backfill and notify.
- Preserve mock/test/live selection and obvious labels.

### Milestone 3 — User-friendly phone/browser pairing
- Implement QR displayed on AURA, browser account approval and selected-room key transfer.
- Provide reviewable Aoraton-side patch when needed.
- Verify expiry, anti-replay, origin/session binding, revocation and recovery.
- Complete paired rooms + account UX in simulator.

### Milestone 4 — ESP32-S3 hardware-ready integration
- Native TLS/auth/secure storage/REST/Realtimes as supported by measurements.
- Evaluate memory, power, reconnect and sleep/notification behavior.
- Keep current functional simulator. Build firmware with correct target and report hardware pending until physically tested.

### Milestone 5 — Polish and AI actions
- Premium native Aoraton iconography/Lucide, Greek fonts and responsive notification UI.
- Typed AI commands for opening rooms, unread status and prepare-send with explicit confirmation.
- Confirm privacy boundary and complete regression suite.

**Not in initial acceptance:** WebRTC audio/video calls, Meshtastic bridge, media attachments, polls, push-notification delivery while sleeping, unrestricted contacts/phone-number resolution. Document separately as future work.

---

## 15. DELIVERABLES AND DEFINITION OF DONE

Produce/update documents in AURA:

- `docs/aoraton/INTEGRATION_AUDIT.md` (verified deltas)
- `docs/aoraton/LIVE_ARCHITECTURE.md`
- `docs/aoraton/PAIRING_PROTOCOL.md`
- `docs/aoraton/E2EE_COMPATIBILITY.md`
- `docs/aoraton/AUTH_RLS_REVIEW.md`
- `docs/aoraton/TEST_ENV_SETUP.md`
- `docs/aoraton/SECURE_STORAGE_AND_REVOCATION.md`
- `docs/aoraton/FIRMWARE_RESOURCE_BUDGET.md`
- `docs/aoraton/INTEROPERABILITY_TEST_REPORT.md`
- `docs/aoraton/PROGRESS.md`

Definition of done for **real messaging in simulator**:

1. A real, authenticated test account has been linked using a verified mechanism.
2. Real authorized test rooms are listed, with no fabricated production rooms.
3. Room keys are provisioned securely, and E2EE v2 is browser-compatible.
4. A browser message appears decrypted in AURA; an AURA message decrypts in browser.
5. Sends require preview and explicit confirmation and use real idempotency rules.
6. Incoming new messages update unread counts and trigger working notifications.
7. Reconnection backfills missed messages; errors do not become fake successes.
8. Authorization, revocation, corrupted ciphertext and negative tests pass.
9. Touch UI and all prior AURA functions still work.
10. The environment is demonstrably **TEST**, not production.

Definition of done for **live production** is separate: explicit authorization for any necessary Aoraton backend changes, verified account pairing in the live service, key-transfer security review, successful interoperability, revocation, and ESP32 hardware tests before declaring live device support.

Final report must state: exact files changed, Aoraton files read, source/migrations consulted, architecture/security decisions, tested auth mode, actual supported rooms, E2EE tests, network/notification behavior, commands to launch simulator, build/test results, screenshots, memory measurements if obtained, pending browser/backend patches, and blockers. Mark each feature **MOCK / TEST VERIFIED / LIVE VERIFIED / HARDWARE UNTESTED / BLOCKED** accurately.

---

## FINAL EXECUTION DIRECTIVE

**Proceed to implement in the existing AURA repository now.** Start by reading both projects and audits, then build the first real browser ↔ AURA encrypted text-messaging milestone in an isolated Supabase test environment. Preserve the existing application design, animations, audio and AI services. Don't restart from zero. Don't replace cryptography with plaintext. Don't invent Supabase device grants, room ownership or access to keys. Don't modify or deploy the original Aoraton production app without explicit approval. Don't stop after writing a plan; implement, run, test, fix errors and report verified outcomes. When a missing Aoraton-side change is necessary, produce a concrete, reviewable patch proposal and continue all AURA-side work that can be done safely.

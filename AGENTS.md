# AURA repository guidance

Read `docs/ARCHITECTURE.md`, `docs/SECURITY_PRIVACY.md`, the ADRs, and `docs/VERIFICATION.md` before changing code.

Non-negotiable rules:

1. Preserve simulator/embedded parity: business state and LVGL screens live under `shared/`; platform code stays in `simulator/`, `bridge/`, or `firmware/`.
2. LVGL is owned by one UI thread. Background work communicates through bounded events and never mutates LVGL objects directly.
3. Keep all visible copy in `shared/localization`. Greek is the default, UTF-8 is mandatory, and English remains a supported fallback.
4. Demo data must say `DEMO`. Do not invent live phone numbers, weather, sources, messages, or hardware results.
5. Treat external/model output as untrusted display text. No side effect executes without typed validation and explicit user confirmation.
6. The bridge listens on `127.0.0.1`, uses a bearer token, invokes `codex` with an argument array and `shell: false`, and never logs prompts or credentials by default.
7. Record every meaningful build/test result in `docs/VERIFICATION.md` as PASSED, FAILED, NOT RUN, or NEEDS HARDWARE. Never convert an unrun check into a claim.
8. Run `.\scripts\test.ps1` after relevant changes. Run `.\scripts\build-firmware.ps1` only in an activated ESP-IDF 5.5.5 environment.
9. Do not commit generated build output, managed ESP-IDF components, credentials, personal data, or local runtime logs.


# Aoraton v11 implementation decisions

Updated: 2026-10-21

## Decisions

1. `incognitochat` remains read-only. Its GPL-3.0-only source was inspected for behavior; no source was copied into AURA. The AURA codec is a clean-room implementation of observed wire behavior with synthetic vectors.
2. Account authorization and room-key possession remain separate. A room may be authorized but `key_available=false`; AURA then renders a locked state and will not display a preview, open history, or send.
3. LVGL remains single-threaded. `RealAoratonService` owns bounded UI state while a future platform worker communicates only through `AoratonMailbox` (16 commands, 64 events). The worker cannot mutate LVGL.
4. A confirmed send is `Queued`, then `Submitted`, then `Acknowledged`; no network response is represented as `Sent`. `Failed` and `Uncertain` are distinct.
5. Windows E2EE uses CNG (`bcrypt`); firmware uses the ESP-IDF mbedTLS APIs. Wire framing and UTF-8 validation live in `shared/`.
6. Windows secrets use current-user DPAPI with atomic replacement. No token or room key is placed in UI snapshots, the generic event log, Codex, or screenshots.
7. Pairing must use a device-owned anonymous Supabase identity plus explicit, per-room grants and browser-to-device encrypted room-key transfer. It must not copy the browser refresh token and must never place room keys in QR data.
8. Real transport is fail-closed until an isolated backend can prove its schema/RLS and pairing grant contract. Mock remains clearly marked; no production URL or credential is embedded.
9. Simulator mode is explicit through `AURA_AORATON_MODE`. TEST/LIVE never fall back to mock; while the backend contract is unavailable, the native adapter converts commands to typed failure events and never manufactures rooms, acknowledgements or notifications.

## Hard blockers

- The inspected Aoraton checkout contains no committed core SQL migrations or RLS policies. Documentation fragments cannot prove the deployed schema.
- Its checked-in/runtime configuration identifies only a remote Supabase project. It was not contacted because it may be production.
- Supabase CLI and Docker are unavailable locally, and no isolated project credentials/schema export were supplied.
- Aoraton has no existing Link AURA browser action, pairing relay, device grant tables/RPCs, or revocation endpoint.

Therefore codec parity, real-service state, DPAPI storage and LVGL pairing states are implemented and tested, while authenticated REST/Realtime, browser approval and end-to-end live messages remain `BLOCKED`, not silently simulated.

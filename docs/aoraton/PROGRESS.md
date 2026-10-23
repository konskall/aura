# Aoraton v11 progress

Updated: 2026-10-21

| Capability | Status | Evidence / blocker |
|---|---|---|
| Read-only source audit | TEST VERIFIED | Auth, join RPC, room queries, message pagination/send/retry, Realtime and E2EE source inspected; checkout unchanged except its pre-existing untracked marketing PDF. |
| Clean-room E2EE v2 on Windows | TEST VERIFIED | CNG known-answer, negative and CSPRNG tests pass. |
| Browser-compatible oracle | TEST VERIFIED | Node WebCrypto matches the CNG wire in both directions. |
| Real shared service state | TEST VERIFIED | Pairing/access/locked rooms/confirmation/delivery/dedupe/unread/revocation and bounded mailbox tests pass. |
| Windows secure vault | TEST VERIFIED | DPAPI round-trip/rotation/no-plaintext/corruption/erase tests pass. |
| Simulator production-service composition | TEST VERIFIED | Explicit `AURA_AORATON_MODE`; TEST selects `RealAoratonService`, CSPRNG ids, bounded mailbox and an honest fail-closed adapter. No implicit mock downgrade. |
| Pairing touch route | TEST VERIFIED | Native SDL/LVGL pointer smoke opens Pair Device and renders a typed absent-backend failure without fake success. |
| Pairing UI | TEST VERIFIED (state/UI) | Native 480×480 not-paired, QR and SAS captures; public synthetic TEST QR only. |
| Pairing relay/ECDH/grants | BLOCKED | Requires reviewed Aoraton browser/backend patch and isolated deployment. |
| Authenticated REST rooms/messages | BLOCKED | No isolated schema/RLS/credentials. No request sent to remote configured project. |
| Realtime receive/backfill | BLOCKED | Same backend blocker; no fake live claim. |
| Real browser→AURA→browser message | BLOCKED | Codec passes; live account/room test cannot run safely yet. |
| ESP32 crypto source | HARDWARE UNTESTED | mbedTLS adapter added; ESP-IDF 5.5.5 environment absent. |
| ESP32 secure vault/transport | BLOCKED | Encrypted NVS and device grant backend not implemented. |
| Existing AURA features | TEST VERIFIED | Final `scripts/test.ps1` passed in 168.7 s: 12/12 CTest, bridge 8/8, controls 2/2, 210-view stress and 109/109 pointer checks at both scales plus motion/weather evidence. |

## v11.1 corrections (AURA-side, backend-independent)

- Unacknowledged sends stay newest and are never evicted by incoming rows; late acknowledgements reconcile (unit-tested).
- Conversation bubbles size to their text and show the delivery stage (queued, submitted, stored, failed, uncertain; `DEMO` in mock).
- Room list scrolls through every authorized room; paired TEST/LIVE devices get a device page with **Αποσύνδεση συσκευής** (`unpair`).
- Compose shows a byte counter for the active limit (512 mock, 1500 real) and explains rejected drafts.
- Real-service notifications appear in Notifications and open their room.
- Pairing failure is no longer described as expiry; an absent TEST backend is named explicitly.

## v11 implementation files

- Shared contracts/state/crypto: `shared/core/include/aura/aoraton.hpp`, `aoraton_crypto.hpp`, `aoraton_real.hpp`; `shared/core/src/aoraton.cpp`, `aoraton_crypto.cpp`, `aoraton_real.cpp`.
- Shared UI/copy: `shared/ui/include/aura/aura_ui.hpp`, `shared/ui/src/aura_ui.cpp`, `shared/localization/include/aura/strings.hpp`, `shared/localization/src/strings.cpp`.
- Windows: `simulator/app/include/aura/windows_aoraton_crypto.hpp`, `windows_aoraton_vault.hpp`, `fail_closed_aoraton_transport.hpp`; matching sources plus `simulator/app/src/main.cpp` and `simulator/config/lv_conf.h`.
- Firmware: `firmware/components/aura_aoraton/CMakeLists.txt`, `include/aura/esp32_aoraton_crypto.hpp`, `src/esp32_aoraton_crypto.cpp`, `firmware/components/aura_shared/CMakeLists.txt`, `firmware/main/CMakeLists.txt`, and `firmware/sdkconfig.defaults`.
- Tests/fixtures: `tests/unit/aoraton_crypto_test.cpp`, `aoraton_real_service_test.cpp`, `aoraton_vault_test.cpp`, `fail_closed_aoraton_transport_test.cpp`, `tests/interop/aoraton_webcrypto_oracle.mjs`, `tests/fixtures/aoraton_e2ee_v2_vectors.json`, and `CMakeLists.txt`.
- Documentation: the v11 files under `docs/aoraton/`, plus `docs/ARCHITECTURE.md`, `docs/SECURITY_PRIVACY.md` and `docs/VERIFICATION.md`.

## Next authorized action

Provide an isolated Supabase project plus the authoritative schema/RLS migrations, or authorize a separate Aoraton branch for the proposed pairing migration/UI. Do not provide production service-role credentials.

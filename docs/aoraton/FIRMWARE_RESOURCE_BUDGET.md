# Aoraton firmware resource budget

Updated: 2026-10-21

Status: `NEEDS HARDWARE`. Values below are limits/design targets, not measurements.

| Resource | Implemented limit / target | Verification |
|---|---:|---|
| UI messages | 100 records | Host unit test/state code |
| History page | 50 rows | Audited web behavior; transport pending |
| Authorized rooms | 64 | Host state code |
| Commands / events | 16 / 64 | Host unit test |
| Plaintext | 1500 UTF-8 bytes | CNG/WebCrypto tests |
| Wire envelope | 2048 ASCII chars | CNG/WebCrypto tests |
| Room key | 32 bytes | CNG/WebCrypto tests |
| AES-GCM IV/tag | 12 / 16 bytes | CNG/WebCrypto tests |
| Realtime reconnect | bounded backoff + jitter target | Not implemented |

Required hardware measurements: binary/IRAM/DRAM delta, free heap and largest block before/after TLS, REST and WebSocket, PSRAM fragmentation after 100-message pagination, UI/audio/weather concurrency, watchdog behavior, Wi-Fi reconnect storms, awake/screen-off current, and deep-sleep notification limitations.

Continuous Realtime may be replaced by accurately labeled adaptive polling only after these measurements. Deep sleep cannot promise instant notification while Wi-Fi is suspended.

# Provisional ESP32-S3 memory budget

These are design ceilings, not hardware measurements.

| Area | Provisional ceiling | Notes |
|---|---:|---|
| LVGL partial draw buffers | 90 KiB | two 480 × 48 RGB565 buffers if double-buffered |
| Generated fonts (v12) | 256 KiB flash ceiling | Native objects total 227,712 bytes: Plex 22/26/28/32 full range, Light 72/96 numeric subsets, wordmark; firmware linker map pending |
| Icon assets (v13) | 448 KiB flash ceiling | 100 A8 sources at 64×64 for 104 roles, 416,032 bytes; scaled at draw time (v12: 87 sources, 361,952 bytes, 384 KiB ceiling) |
| BLE phone link (v13) | measure at first firmware build | NimBLE peripheral with a GATT client for ANCS; flash and RAM to be read from the linker map and heap logs |
| Voice recording (v13) | 480 KiB PSRAM | at most 15 s of 16 kHz mono PCM in `VoiceRecorder`, plus a 2 s capture queue |
| Mercury orb | 93 KiB RAM | one 176×176 RGB565+A8 frame inside `AuraUi`; the halo ring redraws with Mercury frames (15 FPS) |
| Audio source buffer | 512 KiB | maximum 256K signed-16 samples; should allocate from PSRAM when enabled |
| Audio transfer chunk | 1 KiB | fixed worker buffer; codec/I²S owns DMA-capable buffers |
| Aoraton mock cache | 64 × 512-byte bodies | roughly 33 KiB plus model overhead |
| Network/TLS/Realtime | 256 KiB target | must be measured before selecting a production adapter |
| Crypto working memory | 64 KiB target | AES-GCM/HKDF operations, excluding TLS |
| Task stacks | 6 KiB audio; others TBD | high-water marks require hardware |

No permanent 480 × 480 RGB565 framebuffer is assumed. Simultaneous LVGL, TLS, Aoraton crypto, microphone capture and playback is not yet proven. Firmware heap, PSRAM, task-stack and underrun measurements remain mandatory on the board.

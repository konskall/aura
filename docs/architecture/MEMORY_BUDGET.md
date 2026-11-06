# Provisional ESP32-S3 memory budget

These are design ceilings, not hardware measurements.

| Area | Provisional ceiling | Notes |
|---|---:|---|
| LVGL partial draw buffers | 188 KiB PSRAM | two 480 × 100 RGB565 buffers (96,000 bytes each), double-buffered by the LVGL adapter (`aura_board_display_start`, `DEVICE_DRAW_ROWS`). LVGL draws one band at a time and the header glass can blur only what its band holds, so a band holds the whole capsule; the BSP's own 50-row bands (94 KiB) cut it with a visible seam |
| Generated fonts (v12) | 256 KiB flash ceiling | Native objects total 227,712 bytes: Plex 22/26/28/32 full range, Light 72/96 numeric subsets, wordmark; firmware linker map pending |
| Icon assets (v13) | 448 KiB flash ceiling | 100 A8 sources at 64×64 for 104 roles, 416,032 bytes; scaled at draw time (v12: 87 sources, 361,952 bytes, 384 KiB ceiling) |
| Full-colour brand assets | 115 KiB flash payload | 117,248 static ARGB8888 bytes: Aura 76,288, Aoraton 36,864, Google 4,096; no permanent decoded logo buffer, final linker placement pending firmware build |
| BLE phone link (v13) | measure at first firmware build | NimBLE peripheral with a GATT client for ANCS; flash and RAM to be read from the linker map and heap logs |
| Voice recording (v13) | 480 KiB PSRAM | at most 15 s of 16 kHz mono PCM in `VoiceRecorder`, plus a 2 s capture queue |
| LVGL heap (widgets and the layers they draw through) | 512 KiB peak | through the C library since 2026-11-05 (`CONFIG_LV_USE_CLIB_MALLOC`): widgets stay in internal RAM and blocks above 16 KiB go to PSRAM. LVGL's own fixed 64 KiB pool was too small for a layer of 28-427 KiB, and LVGL skips a widget whose layer it cannot allocate. The pointer harness measures the peak in the simulator, which draws the whole screen at once (an upper bound for the device's 100-row bands): 460,344 bytes, against this budget (`lvgl.device_memory`). No animation draws through a transform layer any more (`lvgl.transform_layers`); the one static half-turn left is the other side of face-to-face translation |
| Halo Synapse | 381 KiB PSRAM, 736 KiB on the device | one 480×252 frame of 8-bit light (362,880 bytes; packed in place to RGB565 on the device), 16 KiB of path points, a 5.6 KiB background-tint grid and a 4.5 KiB vignette table, all inside the presence object, which the heap places in PSRAM (larger than 16 KiB). On the device a second frame lets the worker on core 1 draw the next frame while LVGL shows the last (`FrameWorker`, 4 KiB stack); the simulator draws on its UI thread with one frame. Pacing leaves idle time of max(50 ms - the drawing, twice the drawing) after each frame, so drawing takes at most a third of the time on any device, and a full frame slower than 100 ms switches to the reduced frame (70 % of the time on the PC) until one is faster than 40 ms |
| Aoraton account worker | 16 KiB stack, internal | the shared worker on its own thread (it writes NVS, so its stack is internal); TLS allocates from PSRAM; a photo or voice file of up to 4 MiB and its decoded copy live in PSRAM after a heap check; thumbnails of at most 216 px (91 KiB each, six per room delivered, at most twelve kept: about 1.1 MiB); a voice message plays as 16 kHz mono of at most two minutes (3.7 MiB) |
| Aoraton realtime task | 8 KiB stack, internal | `esp_transport` TLS + WebSocket, frames of at most 64 KiB |
| Voice decoding | 24 KiB stack, PSRAM | a short thread per message for `esp_audio_codec` (about 20 KiB of decoder stack); each decoded frame is reduced to 16 kHz mono as it arrives, so no full-rate copy is ever held |
| Cloud assistant worker | 12 KiB stack, internal | one thread runs the Groq and Azure requests in order (TLS handshakes, mbedTLS allocating from PSRAM); a spoken answer arrives as MP3 of at most 2 MiB and is decoded to PCM in PSRAM |
| Audio source buffer | 512 KiB | maximum 256K signed-16 samples; should allocate from PSRAM when enabled |
| Audio transfer chunk | 1 KiB | fixed worker buffer; codec/I²S owns DMA-capable buffers |
| Aoraton mock cache | 64 × 512-byte bodies | roughly 33 KiB plus model overhead |
| Network/TLS/Realtime | 256 KiB target | must be measured before selecting a production adapter |
| Crypto working memory | 64 KiB target | AES-GCM/HKDF operations, excluding TLS |
| Task stacks | 6 KiB audio; others TBD | high-water marks require hardware |

No permanent 480 × 480 RGB565 framebuffer is assumed. Simultaneous LVGL, TLS, Aoraton crypto, microphone capture and playback is not yet proven. Firmware heap, PSRAM, task-stack and underrun measurements remain mandatory on the board.

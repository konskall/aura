# Performance and memory budget

| Area | Target / bound | Current status |
|---|---:|---|
| Desktop tap feedback | <100 ms | formal pointer-to-photon measurement pending |
| Simulator UI loop | informational only | final diagnostics capture: 179 Hz loop, 0.11 ms average handler, 7.82 ms max; not display FPS |
| Rapid navigation | stable allocator endpoint | 100 transitions: 20,464 → 20,272 bytes LVGL used, 1% final fragmentation |
| Active animation | 30 fps device goal / 60 fps desktop option | device unverified |
| Firmware LVGL buffers | two partial RGB565 buffers | implemented in BSP composition; device unverified |
| Assistant prompt | ≤4,000 UTF-8 bytes | enforced by bridge |
| Assistant output | ≤12,000 UTF-8 bytes | enforced by bridge |
| Active assistant turns | 1 | enforced |
| Provider timeout | 45 s default | enforced by bridge |
| Aoraton body | ≤512 bytes | enforced by shared mock service |
| Aoraton retained messages | ≤64 | enforced by shared mock service |
| Firmware PCM/WAV input | ≤256K signed-16 samples / 512 KiB PCM | enforced by audio backend |
| Firmware codec write | 512 samples per chunk | implemented; hardware verification pending |
| Greek UI fonts | ≤150 KiB flash provisional ceiling | 79,168 bytes across native 14/18/22 px objects; firmware map pending |

Host FPS and memory are host-only values. Any ESP32 heap, PSRAM, battery, codec or frame-time result remains an estimate until measured on the board.

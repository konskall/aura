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
| Greek UI fonts | ≤150 KiB flash provisional ceiling | Άλως's Plex set links 227,712 B, above this provisional ceiling (set before v12's larger sizes); the owner has not revised it |
| Appearance fonts | firmware map | Μάρμαρο 272,839 B, Σέλας 235,716 B, Γραφίτης 224,162 B |
| Appearance icon families | firmware map | Phosphor 412,400 B, Solar 408,276 B, Material Symbols 416,524 B |
| Firmware app partition | ≥15% free | 23% free after the four appearances (app `0x501010` of `0x680000`) |
| Static internal RAM (DIRAM) | informational | 94,965 B free after the four appearances (`idf.py size`) |
| Presence buffers | one at a time, heap (PSRAM on the device) | Mercury and Μαργαριτάρι 92,928 B; Σέλας ribbons 259,200 B; Αστερισμός 209,088 B; a change of look holds the old one until the new screen is drawn |
| Light fields and glows | static internal RAM | two 32 × 32 A8 discs (1 KB each) and the pearl's 768 B body shades |
| Presence frame | 66 ms per frame (about 15 fps) | device unverified: per-pixel drawing of 176², 432 × 200 or 264² pixels plus the LVGL blend |

Host FPS and memory are host-only values. Any ESP32 heap, PSRAM, battery, codec or frame-time result remains an estimate until measured on the board.

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
| Firmware app partition | ≥15% free | 16% free on 2026-11-05 (app `0x571e20` of `0x680000`), after the glass header, the Σύναψη port, the device's cloud assistant and the board drivers; was 23% after the four appearances. Close to the floor: the next large addition needs a decision (smaller fonts or icons, or a larger partition) |
| Static internal RAM (DIRAM) | informational | 94,965 B free after the four appearances (`idf.py size`) |
| Presence buffers | one at a time, heap (PSRAM on the device) | Mercury and Μαργαριτάρι 92,928 B; Σέλας ribbons 259,200 B; Αστερισμός 209,088 B; a change of look holds the old one until the new screen is drawn |
| Light fields and glows | static internal RAM | two 32 × 32 A8 discs (1 KB each) and the pearl's 768 B body shades |
| Presence frame | 66 ms per frame (about 15 fps) | device unverified: per-pixel drawing of 176², 432 × 200 or 264² pixels plus the LVGL blend |
| Σύναψη drawing | at most a third of the time, on any device | 2026-11-05: idle time after a frame is max(50 ms - the drawing, twice the drawing), with no frame-rate floor (`paced_wait`; a 150 ms frame waits 300 ms). On the device the frame is drawn on core 1 (`FrameWorker`), so the UI thread never waits for it. A full frame slower than 100 ms switches to the reduced frame, about 70 % of the time on the PC with nearly the same light, until a reduced frame is faster than 40 ms. PC: 6.1 ms full, 4.5 ms reduced. Device unverified (estimate 150–300 ms full) |
| Main loop and processor | follow the screen | 2026-11-05: the loop runs every 33 ms while the screen is lit, 100 ms for the always-on clock and 250 ms when it is dark (`host_loop_interval_ms`). Dynamic frequency scaling is on: 240 MHz while lit (a held `ESP_PM_CPU_FREQ_MAX` lock), 80 MHz otherwise, no automatic light sleep yet. Device unverified |
| Data refresh during a touch | waits for the finger | 2026-11-05: a screen rebuild that new data asks for waits until the finger lifts and the list stops (`ui.refresh_waits_for_drag`) |
| First-boot numbers | printed on the serial console | 2026-11-05: `metrics t=… frame avg/max … loop avg/max … synapse … internal … psram …` every 10 s for two minutes, then every minute (`format_runtime_metrics`) |

Host FPS and memory are host-only values. Any ESP32 heap, PSRAM, battery, codec or frame-time result remains an estimate until measured on the board.

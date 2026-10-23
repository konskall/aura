# AURA v10.2 Mercury animation validation

Date: 2026-10-19. Renderer: original procedural LVGL-compatible RGB565+A8 component, 176×176, fixed 92,928-byte buffer, nominal 66 ms frame interval. Evidence spans 2,450 ms with eight time-separated native captures per state.

| State | First→last hero delta | Mercury rate | Handler avg / max | LVGL used | Source-edge bright pixels |
|---|---:|---:|---:|---:|---:|
| Home | 34,048 px | 14.69 Hz | 0.491 / 3.684 ms | 20,808 B | 0 in all 8 frames |
| Listening | 34,052 px | 14.69 Hz | 0.491 / 4.406 ms | 20,856 B | 0 in all 8 frames |
| Thinking | 33,980 px | 14.69 Hz | 0.485 / 3.866 ms | 19,848 B | 0 in all 8 frames |
| Speaking | 16,964 px | 14.69 Hz | 0.652 / 4.681 ms | 24,472 B | 0 in all 8 frames |

Every adjacent pair changes at least 15,650 hero pixels. All 32 frames are distinct. Pairwise first-frame differences between different states range from 19,369 to 34,734 pixels, proving that state identity is carried by the Mercury material itself rather than only by labels, meters or halos.

- Home: slow balanced silver/lilac flow.
- Listening: faster cyan-biased field coupled to the explicitly labelled simulated desktop input amplitude.
- Thinking: higher-contour violet/rose processing field shown only for the active assistant request.
- Speaking: rose/violet material coupled to the real Windows PCM envelope; pause/mute/stop remain service callbacks.

The transparent deformation margin was increased after the first validator found six bright edge pixels in one Listening frame. The final 32-frame run reports zero bright pixels on every source boundary. Contact sheet and GIFs: `artifacts/v10_2/AURA_V10_2_MERCURY_MOTION.png` and `artifacts/v10_2/motion/`.

These are Windows Release measurements, not ESP32-S3 FPS, PSRAM, battery or thermal claims. Firmware performance remains **NEEDS HARDWARE**.

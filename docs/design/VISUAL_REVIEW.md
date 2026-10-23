# Visual review record

## Pass 1

Twenty native captures were assembled into a contact sheet and inspected at full resolution. The new presence, content-first answer, Tools navigation and borderless utility layouts were coherent, but the review rejected four details:

- Aoraton compose still used LVGL's default Latin keyboard.
- The first Settings viewport exposed a clipped reduced-motion row.
- An unsupported diagnostic/offline glyph rendered as a square.
- Back affordances could disappear after particular re-render sequences.

The keyboard received a Greek map with Latin-symbol fallback, Settings spacing was corrected, unsupported copy was replaced/localized, deprecated object-flag APIs were removed, and Back was promoted above screen/action layers.

## Pass 2 and final gallery

The complete gallery was regenerated and inspected as one product family and at full resolution for Home, Listening, Speaking, long Answer, Settings, Audio, Diagnostics and all Aoraton compose/confirm states. Two additional lifecycle captures were added for ambient/sleep and low battery, producing 22 final 480 × 480 PNGs.

The final pass found no clipped headings/actions, footer remnants, notification badge misownership, missing Greek keyboard glyphs, unsupported square glyphs or unreachable reviewed screen. Long answer content intentionally extends below the reader viewport and exposes a visible scrollbar.

## Measured host evidence

- Diagnostics capture: 179 Hz simulator UI-loop cadence, 0.11 ms average `lv_timer_handler` duration and 7.82 ms maximum during that short capture. This is not display FPS and not an ESP32 measurement.
- Navigation stress: 100 immediate shared-LVGL view transitions completed; LVGL allocator usage changed from 20,464 to 20,272 bytes and reported 1% fragmentation at the endpoint.
- Generated Greek font object text sizes: 18,688 bytes (14 px), 26,080 bytes (18 px), 34,400 bytes (22 px), 79,168 bytes total in the native build. Firmware linker-map size remains unmeasured.

Hardware frame time, heap, PSRAM, power and touch latency remain `NEEDS HARDWARE`.

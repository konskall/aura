# AURA v8 animation architecture

`MotionModel` is the shared bounded state engine. `AuraUi::tick` advances it at a 33 ms target interval; the main LVGL thread alone mutates objects. Enter/leave occurs with screen render, so hidden scenes have no running LVGL timer or orphan animation. Reduced-motion mode freezes spatial phase while retaining a calm state indication.

All four heroes are procedural LVGL scenes, not screen-shaped images:

- Home: breathing inner/outer energy, six rotating/scaling ribbons, three counter-rotating arcs and eight orbit particles.
- Listening: 15 amplitude-reactive bars and three width/opacity sound ribbons over bounded halos. Desktop input is deterministic and labelled `DEMO`.
- Thinking: four independent counter-rotating arcs with changing arc spans plus eight particles on layered orbits.
- Speaking: PCM-envelope-driven core/ribbons, counter-rotating arcs and a nine-bar meter. Paused/muted/stopped/error states come from AudioService.

Dynamic bounds are limited to the central hero region. Status and utility controls remain stable. Rendering allocates objects only on screen entry; frames only alter sizes, positions, rotation, opacity and arc values.

## Measured Windows budget

Eight frames per scene were captured across 2,450 ms. Measured update rate was 25.92–29.00 Hz. Average `lv_timer_handler` cost was 1.13–2.91 ms; observed maxima were 8.74–45.90 ms. LVGL allocator use at sequence end was 22,808–26,864 bytes with 1% fragmentation. Exact logs are under `artifacts/v8/performance/`.

The simulator SDL framebuffer is 480×480; a full RGB565 buffer would be about 450 KiB. Firmware remains configured for two partial draw buffers. ESP32-S3 FPS, CPU, DMA, PSRAM bandwidth, transferred pixels and power have not been measured. Device tuning must preserve the shared state model and may reduce update cadence/quality only after hardware measurement.

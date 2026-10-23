# AURA v4 redesign progress

## Completed

- Read the full visual-lock directive and inspected the 1536×1024 board plus all 18 individual panels.
- Preserved a fresh 22-state pre-v4 simulator baseline in `artifacts/screenshots/before/` and `AURA_V4_BEFORE.png`.
- Added a reusable 468 px circular shell, radial safe area, bezel, status chord and glass surface primitives.
- Added distinct project-local RGB565 visuals for Home, Listening, Thinking, Speaking, fictional map, audio artwork and camera demo preview.
- Implemented and made navigable all 18 reference views in the shared LVGL codebase.
- Preserved Codex provider switching, assistant state synchronization, AudioService callbacks/PCM meter, Aoraton room/thread/compose/confirm, Greek keyboard, diagnostics and long-answer reader.
- Added deterministic capture mode, an 18-screen capture pipeline, a full contact sheet, 18 full-size comparison pairs and one combined comparison sheet.
- Expanded Greek fonts to 14/18/22/28/36 px and kept English fallback copy.
- Added automated artifact validation and expanded UI stress to all 18 primary views.

## Honest limits

- Firmware was not compiled because an activated ESP-IDF 5.5.5 environment is unavailable.
- Display, touch, PMIC, ESP32-S3 frame rate, flash link size, PSRAM behavior, ES8311 output and power draw need the physical board.
- Navigation/weather/search/calendar/camera/extra voices are providers or optional hardware, so their simulator screens are marked DEMO/setup/disabled rather than pretending to be live.

## Next device priority

When the board arrives: compile with ESP-IDF 5.5.5, measure signature-state frame rate and dirty regions, validate the RGB565 asset path and flash budget, then tune the adaptive asset tier before adding more decorative frames.

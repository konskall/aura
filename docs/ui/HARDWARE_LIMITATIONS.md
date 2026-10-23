# AURA v6 hardware limitations

The Windows simulator is the verified acceptance environment. The physical Waveshare ESP32-S3-Touch-AMOLED-2.16 board is not available.

## Verified on Windows

- Shared LVGL 9.6 screen tree, routes, real SDL mouse path and 1×/2× coordinate mapping.
- Host clock, explicit simulated battery/connectivity, deterministic input amplitude and developer controls.
- SDL2 PCM tone/WAV playback, envelope-driven visual, mute, pause/resume, stop and volume.
- Mock Aoraton state/confirmation boundaries and local Codex loopback bridge tests.
- Motion lifecycle, approximate host update cadence, screenshot and frame-sequence generation.

## Not verified on physical hardware

- CO5300 display refresh/dirty-region cost, touch calibration, rotation and edge accuracy.
- ESP32-S3 frame rate, task scheduling, flash placement, PSRAM bandwidth and thermal/power behavior.
- AXP2101 battery percentage/charging telemetry and RTC/NTP validity.
- ES8311 speaker output, microphone capture, real PCM envelope latency and amplifier behavior.
- GPIO46 amplifier control beyond the BSP-owned source contract.
- Wake word, SD card, IMU and any optional external camera/location hardware.

Firmware must show unavailable status until validated adapters supply measurements. Desktop `SIM` values must never be carried forward as device readings.

## Next board procedure

1. Activate the pinned ESP-IDF 5.5.5 environment and run `scripts/build-firmware.ps1`.
2. Verify display/touch with the real 48 px targets, including corners and 2×2 tiles.
3. Measure Home/Listening/Thinking/Speaking update cadence and dirty rectangles under network/audio load.
4. Inspect linked flash/PSRAM placement for the 1.58 MB RGB565 payload and font objects.
5. Validate AXP2101 power values and only then mark their origin `LIVE`.
6. Validate ES8311 playback/capture and GPIO46 behavior using `docs/development/HARDWARE_VALIDATION.md`.
7. Record each result as PASSED, FAILED or NEEDS HARDWARE in `docs/VERIFICATION.md`.


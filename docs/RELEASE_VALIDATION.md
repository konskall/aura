# AURA v8 release validation

## Reproducible Windows commands

```powershell
.\scripts\build-simulator.ps1
.\scripts\test.ps1
.\scripts\capture-screenshots.ps1
.\scripts\capture-motion-evidence.ps1
.\scripts\dev-simulator.ps1 -Provider codex
```

## Verified

- Native CMake/Ninja build and five C++ tests.
- Bridge security/provider tests and simulator-controls validation tests.
- Native simulator smoke and 180-transition LVGL allocator stress.
- 18 actual 480×480 screenshots.
- Four eight-frame/2,450 ms hero sequences with hero-region pixel analysis.
- 80/80 real SDL pointer checks at 1× and 80/80 at 2×, covering 40 action IDs, scroll, outside rejection, runtime setting persistence and rapid repeat.
- Windows SDL PCM/WAV state, pause/resume, stop, mute, volume and reactive envelope.
- Bounded Aoraton DEMO rooms/chat/compose/explicit-confirm/cancel flow.

## Not verified

- ESP-IDF firmware compile: `idf.py`/activated ESP-IDF 5.5.5 environment is unavailable.
- Physical display/touch calibration, ESP32-S3 frame rate/heap/PSRAM/DMA/power, AXP2101 telemetry, microphone capture, ES8311 output, speaker presence and GPIO46 electrical behavior: NEEDS HARDWARE.
- Production Aoraton transport/E2EE interoperability and Greek TTS: intentionally disabled/not run.

The authoritative detailed ledger is `docs/VERIFICATION.md`. A build result is not used as a substitute for a runtime, pointer, animation or hardware claim.

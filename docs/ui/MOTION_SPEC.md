# AURA v6 motion specification

> Historical v6 baseline. The current Mercury renderer and measurements are documented in [MERCURY_IMPLEMENTATION.md](MERCURY_IMPLEMENTATION.md).

Motion is produced by `MotionModel` plus LVGL object updates on the single UI thread. No background task owns an LVGL pointer and no full-screen video or animation frame atlas is decoded.

## State behavior

| State | Motion | Input | Lifecycle |
|---|---|---|---|
| Idle/Home | Slow scale/luminance breathing and two opposing halo phases | Monotonic elapsed time | Starts on Home, leaves on route change |
| Listening | Fifteen cyan/violet bars change height over a bounded visual | Simulator synthetic amplitude, explicitly `DEMO`; future real capture may supply amplitude | Stops when recording ends/cancels |
| Thinking | Vortex luminance/scale, four orbiting particles and rotating bounded arc | Elapsed time; no fake percent | Cancel is responsive; objects are released on state exit |
| Speaking | Nine-bar meter and bounded image pulse | Actual `AudioDiagnostics::envelope` from PCM | Stops on playback stop/complete/error |
| Text answer | Content-first reader; no speaking claim | Provider text | No audio animation |
| Error/offline | Static persistent state | Service failure | No costly perpetual effect |

The runtime requests updates every 33 ms. Current Windows evidence measured approximately 26–28 updates/s including capture overhead; this is a host observation, not ESP32 frame-rate evidence. Only the visual objects change, leaving status text stable.

## Reduced motion and resource safety

Reduced motion freezes scale/rotation at a stable frame while leaving state, text and controls functional. Capture mode freezes motion only for deterministic screenshots; animation evidence runs with capture mode disabled. `render()` calls `MotionModel::leave()`, clears old objects, resets all stored pointers, builds the new view and enters only the matching motion mode. UI stress exercises repeated cleanup.

## Evidence

- `artifacts/v6/AURA_V6_MOTION_FRAMES.png`: 4 states × 8 real frames.
- `artifacts/v6/motion/*.gif`: looping review artifacts.
- `artifacts/v6/evidence-summary.json`: frame counts, changed-pixel counts and pointer coverage.

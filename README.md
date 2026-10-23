# AURA

AURA is a Greek-first AI companion for the Waveshare ESP32-S3-Touch-AMOLED-2.16. It uses one shared C++17 application core and LVGL UI across a native Windows simulator and ESP-IDF firmware.

## Current milestone

The Phase 1 vertical slice now includes:

- Production v8 LVGL redesign with a living procedural AURA presence, distinct animated AI states, content-first answers and a compact Tools launcher instead of the legacy footer.
- Native 480 × 480 LVGL/SDL2 simulator with mouse and keyboard input.
- Home, assistant, demo cards, notifications, settings, diagnostics, audio and Aoraton messaging screens.
- Real Windows SDL2 PCM/WAV playback with pause, stop, volume, mute, progress and reactive envelope.
- Fictional local Aoraton `DEMO` rooms, messages, unread state, reconnect/error scenarios and explicit send confirmation.
- Greek on-screen Aoraton keyboard, 18-view native 480 × 480 production gallery and measured rapid-navigation stress check.
- Live Open-Meteo weather with manual city search/default persistence, cached offline recovery, Greek on-screen input and animated condition states; no GPS capability is implied.
- Deterministic mock assistant plus a loopback-only bridge to the authenticated Codex CLI.
- ESP32 ES8311 audio implementation through the official Waveshare BSP; compile and physical playback remain unverified until ESP-IDF and hardware are available.

## Windows quick start

First-time setup and normal demo mode:

```powershell
.\scripts\setup-windows.ps1
.\scripts\dev-simulator.ps1
```

To connect the Assistant screen to the locally authenticated Codex CLI:

```powershell
codex login status
.\scripts\dev-simulator.ps1 -Provider codex
.\scripts\setup-voice.ps1   # optional: local Greek speech recognition (about 200 MB)
```

The setup is project-local. It installs pinned portable build tools under the ignored `.tools` directory and does not change the machine-wide `PATH`. The development command opens the simulator and its loopback control panel; closing the simulator tears down the helper processes it started.

Run all checks available on this computer:

```powershell
.\scripts\test.ps1
```

See [docs/SIMULATOR.md](docs/SIMULATOR.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/WEATHER_ARCHITECTURE.md](docs/WEATHER_ARCHITECTURE.md), [docs/audio/AUDIO_ARCHITECTURE.md](docs/audio/AUDIO_ARCHITECTURE.md), [docs/aoraton/INTEGRATION_AUDIT.md](docs/aoraton/INTEGRATION_AUDIT.md), and [docs/VERIFICATION.md](docs/VERIFICATION.md).

## Important boundaries

- The Codex CLI bridge is desktop development tooling only. It does not run on the ESP32 and does not grant production API rights.
- Aoraton is a local mock in AURA. There is no production Supabase connection, real room pairing or E2EE implementation yet.
- Every fictional room/message and simulated delivery is marked `DEMO`.
- Greek TTS is not enabled yet. The audio engine currently proves actual tone and PCM/WAV output.
- Speaker presence, GPIO46 behavior, touch accuracy, microphones, battery life, device FPS and firmware audio are not claimed before physical-board validation.

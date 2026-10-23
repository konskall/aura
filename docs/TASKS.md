# Execution checklist

## Completed

- [x] Bootstrap a pinned project-local Windows compiler/CMake/Ninja toolchain.
- [x] Build a native 480 × 480 LVGL/SDL2 simulator from shared UI sources.
- [x] Implement Greek-first screens, deterministic assistant mock and loopback Codex CLI bridge.
- [x] Verify a real authenticated Greek Codex turn in the LVGL window.
- [x] Implement shared audio state/service contracts and native tests.
- [x] Implement actual Windows SDL2 tone and PCM/WAV playback with pause/stop/volume/mute/progress/envelope.
- [x] Implement the Audio screen and capture it from the real LVGL renderer.
- [x] Audit the Aoraton repository read-only and document protocol/security/licensing constraints.
- [x] Implement bounded fictional Aoraton rooms, chat, compose, unread, reconnect/failure scenarios and mandatory confirmation.
- [x] Add a regression test that prevents stale control-event replay.
- [x] Implement an ESP32 ES8311 backend that delegates codec/amplifier ownership to the official BSP.

- [x] v13: shared companion models, screens, bridge speech, simulator runtime and evidence; firmware `aura_companion` adapters written.

## Pending gates

- [ ] Choose the cloud AI provider and build the gateway; enable device voice/translation/spoken replies through it.
- [ ] Build the AURA Link Android app and the device-side GATT server (`docs/phone/ANDROID_COMPANION.md`).
- [ ] Train a wake-word model for «Αύρα» (ESP-SR WakeNet or microWakeWord) and measure its battery cost.
- [ ] Decide the OTA hosting and signing (secure boot or signed images) before the first field update.

- [ ] Install/activate ESP-IDF 5.5.5 and compile the firmware; resolve only evidence-backed BSP/API drift.
- [ ] On board arrival, execute display/touch/PMIC/SD/RTC/IMU/audio/heap/frame-time validation.
- [ ] Confirm safe GPIO46 mute/boot behavior and run rapid-cycle/long-play audio tests.
- [ ] Add bounded streaming audio and optional Greek TTS without bypassing PCM diagnostics.
- [ ] Build and test an independent Aoraton adapter against an isolated test backend and known-answer crypto vectors.
- [ ] Decide licensing and production authorization before any live Aoraton connectivity.
- [ ] Add screenshot pixel-diff baselines with an explicit human review/update command.

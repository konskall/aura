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
- [x] Add persisted continuous spoken follow-ups and conservative speech-over-TTS barge-in to the Windows/shared voice pipeline.

## Pending gates

- [x] The device's own assistant (the owner's choice, 2026-11-05; ADR 0009): Groq listens and answers, Azure speaks, keys sent from the PC into encrypted NVS (`scripts/send-device-keys.ps1`). Spoken questions and replies are wired; confirming the model's actions on the device is still to do.
- [ ] Confirming the model's proposed actions on the device.
- [ ] Build the AURA Link Android app and the device-side GATT server (`docs/phone/ANDROID_COMPANION.md`).
- [ ] Train and license a Greek wake-word model for «Αύρα» (ESP-SR WakeNet, openWakeWord or microWakeWord); measure false accepts/rejects and battery cost before enabling the existing capability.
- [x] Signed updates in code (2026-11-05): the manifest carries the owner's ECDSA P-256 signature (`scripts/update-signing.mjs`); production adds Secure Boot V2.
- [ ] Generate the owner's update signing key, put its public key in `sdkconfig.defaults`, and decide where updates are hosted.

- [x] Install/activate ESP-IDF 5.5.5 and compile the firmware; resolve only evidence-backed BSP/API drift (2026-10-24: clean build; see VERIFICATION).
- [ ] Flash the board and validate display, touch, audio, Wi‑Fi, IMU, BLE and microSD on hardware.
- [ ] On board arrival, execute display/touch/PMIC/SD/RTC/IMU/audio/heap/frame-time validation. The firmware prints frame, loop, Synapse and memory numbers every 10 s for the first two minutes (`metrics ...` on the serial console); the AXP2101 and PCF85063 drivers are written but unverified.
- [ ] Measure power on the board, then decide on automatic light sleep (dynamic frequency scaling is on: 240 MHz with the screen lit, 80 MHz dark).
- [ ] Confirm safe GPIO46 mute/boot behavior and run rapid-cycle/long-play audio tests.
- [ ] Add bounded streaming audio and optional Greek TTS without bypassing PCM diagnostics.
- [ ] Build and test an independent Aoraton adapter against an isolated test backend and known-answer crypto vectors.
- [x] Aoraton on the device (ADR 0010, 2026-11-06): sign-in through the PC, encrypted NVS, hybrid sync, photos and voice messages (play and send), built and tested on the PC.
- [x] Sign-in from the phone, without a PC (ADR 0010, 2026-11-07): the QR, the two pages, the project's values built in. Tested on the PC against a fake Supabase.
- [x] The phone's return through aoraton.com (ADR 0010, 2026-11-07): Supabase refuses a local return address, so the static page `/aura-phone/` (incognitochat 53d50b9) hands the code to the device. No Supabase setting.
- [x] Sign-in from the phone, live (2026-11-11): `run.bat device` and an iPhone, through aoraton.com. The `http://192.168.*.*/**` Redirect URL is no longer used and can go.
- [ ] Run Aoraton on the board live (`docs/VERIFICATION.md`: NEEDS HARDWARE).
- [x] Internet radio on the device (2026-11-06): the stream, continuous audio output and the station search, on the shared `radio_pump`. Hear it on the board: NEEDS HARDWARE.
- [ ] Radio by voice on the device ("βάλε Σφαίρα"): its assistant does not yet handle local commands.
- [ ] Decide on pairing a device without a PC: it needs changes to the aoraton.com server, which this repository only reads.
- [x] Decide the firmware app partition (2026-11-06, the owner): two 7 MB app slots and 1.8 MB `storage` (everything the owner keeps lives on the microSD card); `aura_firmware_config_tests` checks both tables.
- [ ] Decide licensing and production authorization before any live Aoraton connectivity.
- [ ] Add screenshot pixel-diff baselines with an explicit human review/update command.

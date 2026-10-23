# AURA v13 "Companion"

Status: **IMPLEMENTED IN THE SIMULATOR** (`scripts/test.ps1` green, see `docs/VERIFICATION.md`) · firmware adapters written, **not compiled** · physical board untested.

The owner asked for every proposal from the v12 review: a real assistant (follow-ups, device knowledge, confirmed actions, voice), everyday tools (timers, alarms, reminders, voice notes, morning briefing, conversation translation) and board features (raise to wake, always-on clock, phone notifications, OTA, wake word). Decisions taken with the owner on 2026-10-22:

- **AI provider:** Codex through the local bridge for now; a cloud API and a gateway come later. Nothing in this release contacts a paid API.
- **Phone:** both platforms. The shared model, screens and the iPhone (ANCS) link are built; Android gets a written protocol for a companion app (`docs/phone/ANDROID_COMPANION.md`), not the app.
- **Speech on the PC:** local and free. whisper.cpp v1.9.2 with the multilingual `small-q5_1` model (`scripts/setup-voice.ps1`, SHA-256 pinned, under `.tools/`), and the installed Windows voices (OneCore "Stefanos" el-GR, SAPI "Zira" en-US).

## Principles

1. **The model proposes, the owner decides.** Answers may end with one `ACTION: {json}` line. The device parses it strictly (`parse_assistant_answer`: fixed types, bounded fields, last line only) and shows a confirmation card. Nothing runs without a tap. A message goes through the existing Aoraton confirmation screen.
2. **Device facts are data.** Time, battery, Wi‑Fi, the loaded forecast, timers, alarms and reminders travel with each question as a quoted block; the provider is told to ignore instructions inside it. Notes and phone notifications are added **only** when the question is about them (`question_mentions_notes/phone`).
3. **Memory is short and local.** Up to four exchanges, 600 bytes each, kept in RAM and forgotten after five quiet minutes or on «Νέο θέμα». Never written to disk or logs.
4. **Truthful capabilities.** `CompanionCapabilities` says what this platform can do now (microphone, recognition, Greek voice, IMU, phone link, wake word). The UI disables or explains the rest. DEMO stand-ins say DEMO.
5. **Same models on both platforms.** All logic lives in `shared/core` and is unit-tested; the simulator and the firmware only move bytes.

## Architecture

| Layer | Pieces |
|---|---|
| Shared core | `text_util` (UTF-8 safe text, Greek folding, flat JSON, civil time), `assistant_companion` (memory, device context, action parser), `clock_services`, `notes`, `briefing`, `translation`, `display_power` (IMU gestures + screen power), `phone_link` (+ ANCS codec), `ota_update` (manifest, versions, install rule), `voice` (recorder, VAD, WAV) |
| Shared UI | `aura_ui_companion.inl` (screens, included inside `AuraUi::Impl`) and `aura_ui_companion_api.inl` (apply_*), 19 new views, 13 new icon roles, `CompanionStrings` (el/en) |
| Bridge | `turn-prompt.mjs` (validation and instructions for chat/translate), `speech.mjs` (whisper.cpp, Windows voices), `/v1/speech/transcribe`, `/v1/speech/synthesize`, speech health |
| Simulator | `CompanionRuntime` (models, callbacks, persistence in `simulator/runtime/companion/`), `BridgeSpeechService`, `SdlVoiceCapture` (PC microphone, open only while recording), AURA LAB "Companion" controls |
| Firmware | `aura_companion`: `Esp32Motion` (QMI8658), `Esp32PhoneLink` (NimBLE ANCS client), `Esp32Ota` (HTTPS manifest + streamed image, SHA-256, rollback), `Esp32Microphone` (ES7210), `Esp32CompanionStore` (microSD), `Esp32Companion` glue |

## Features

- **Conversation:** follow-ups use the memory; the Answer screen offers «Νέο θέμα» (clears the memory; «Νέα συζήτηση» did not fit the half-width button) while memory is active and a speaker button when a Greek voice exists.
- **Voice:** tap the orb; the PC microphone records 16 kHz mono with an energy VAD (stops 1.2 s after speech, 15 s max, 6 s without speech → «Δεν σε άκουσα»). The halo follows the real level. Recognition → Codex → the spoken answer through the existing PCM path (Speaking screen).
- **Weather questions** without a city are answered from the loaded forecast once one exists; city changes still open the Weather screens.
- **Clock app:** timers (nine one-tap presets), alarms (hour/minute rollers, once or daily), reminders (text, then a time choice). Full-screen alert with pulsing ring and a repeating chime; «Τέλος», «Αναβολή 5 λεπτά» or «+1 λεπτό». Alarms missed for over an hour are not rung late; overdue reminders are.
- **Notes:** typed or voice notes (WAV plus transcript), detail view with playback, two-tap delete. Stored locally (simulator runtime folder; device microSD).
- **Today:** greeting by time of day, date, weather, next alarm, today's reminders, running timers, unread messages and phone notifications; tap a row to open it; read aloud when a voice exists. Opens by itself on the first wake each morning (05:00–11:59).
- **Translation:** the device lies between two people; the top half is drawn upside down for the other person with their own microphone. Greek with English, German, French, Italian or Spanish; translations are spoken when a voice for that language exists (el and en on this PC).
- **Display power:** active → dimmed (30 s) → always-on clock (or off). The always-on clock is dim and moves a few pixels each minute against burn-in. Since 2026-10-23 it is an ambient screen, approved by the owner, showing:
  - the weather: icon, temperature, city, and the day's high and low, labelled DEMO or cached when it is;
  - a faint halo in the colour of the sky behind the clock;
  - how many new messages there are and from which room, never their text;
  - the next alarm.

  It redraws at once when notifications or the weather change, and is otherwise never animated. Raise to wake and turn-over-to-mute come from the QMI8658 through `ImuGestureDetector`; the first touch on a dark screen only wakes it.
- **Phone:** notifications list, detail and dismiss, incoming-call screen (answer/decline act on the phone). iPhone via ANCS on the device; DEMO injection in the simulator.
- **Software update:** HTTPS manifest (`version`, `url`, `size`, `sha256`, `notes`), install only with Wi‑Fi and ≥30 % battery or charging, explicit confirmation, progress, verification, restart. The simulator source is DEMO and installs nothing.
- **Wake word «Αύρα»:** shown as unavailable until a trained model exists (ESP-SR WakeNet custom word or a microWakeWord model). Continuous listening also costs battery on the 1000 mAh cell, so the side button stays the primary trigger.

## Device mapping (Waveshare docs, BSP 2.0.1 header)

QMI8658 on the shared I2C bus (GPIO14/15, INT1 17, INT2 21), address 0x6B or 0x6A by SA0 (both are probed with WHO_AM_I 0x05). GPIO18 is the user button (10 k pull-up, low when pressed): a press wakes or sleeps the screen, holding it records a voice note. ES7210 microphones through `bsp_audio_codec_microphone_init()`, microSD through `bsp_sdcard_mount()`, CO5300 brightness through `bsp_display_brightness_set()`. A speaker is not assumed; the ES8311 amplifier is on GPIO46.

## Not in this release

- A cloud AI gateway (provider, key custody, cost): on the device the assistant, recognition, spoken replies and translation stay unavailable until it exists.
- The Android companion app itself.
- A trained wake-word model; signed images / secure boot for OTA (the manifest hash and HTTPS are the current guard).
- RTC alarms that wake the board from deep sleep (no deep-sleep design yet).

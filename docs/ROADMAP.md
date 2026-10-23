# Roadmap

- Completed foundation: reproducible Windows toolchain, shared core/UI, native LVGL simulator, safe local Codex CLI bridge and official BSP firmware scaffold.
- Completed Phase 1 simulator audio: real SDL2 PCM/WAV output, controls, progress, diagnostics and reactive UI.
- Completed Phase 1 Aoraton UX mock: read-only source audit, rooms, chat, compose, unread, failure/reconnect scenarios and explicit send confirmation.
- Completed v10 simulator UI: Space Grotesk/IBM Plex typography, semantic licensed icons, four-state Mercury presence, 18-screen evidence and real SDL/LVGL pointer regression at 1×/2×.
- Completed v13 "Companion" in the simulator: follow-up memory, device facts, confirmed ACTION proposals, real voice on the PC (whisper.cpp + Windows voices), timers/alarms/reminders, notes and voice notes, Today briefing, two-sided translation, display power with always-on clock and IMU gestures, phone notifications (iPhone ANCS on the device), OTA flow; firmware adapters written, not compiled.
- Next assistant step: choose the cloud AI provider and build the gateway (key custody, cost limits), then enable device voice, translation and spoken replies through it.
- Next audio step: compile under ESP-IDF 5.5.5, validate ES8311/BSP and GPIO46 behavior on the board, then add streaming/TTS only through the common PCM path.
- Next UI step: run the shared v10 code on the physical 480×480 board and measure frame time, PSRAM/heap, partial redraw, touch edges, battery and simultaneous audio/Wi-Fi load.
- Next messaging step: independent protocol adapter in a separate test Supabase project, known-answer E2EE vectors and reviewed key storage. No production connection before those gates pass.
- Later: screenshot regression thresholds, gesture polish, hardware display/touch/PMIC/RTC/IMU/SD bring-up, measured microphone/VAD work and a supported production AI gateway.

Detailed sequencing and exit criteria are in [development/ROADMAP.md](development/ROADMAP.md).

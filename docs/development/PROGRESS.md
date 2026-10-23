# AURA progress

## Weather integration v10.3 — simulator verified

- Added the bounded shared Weather model, WMO mapping and Greek/English assistant weather intents without introducing GPS or silent default-city changes.
- Added live Open-Meteo geocoding/forecast over fixed-host WinHTTP, typed JSON validation, cancellation generations, separate persisted location/cache files and explicit stale/offline behavior.
- Rebuilt the 480×480 LVGL Weather and city-selection routes with current metrics, hourly/daily cards, Greek keyboard, Lucide condition/metric assets and real clear/rain/snow/wind animation.
- Wired Weather into Quick Actions, Settings, controls and assistant routing; all user-facing copy remains localized and deterministic captures are visibly `DEMO`.
- Added `aura_weather_tests` and v10.3 screenshot/motion/pointer evidence to the full test runner. Native pointer coverage passes 106/106 at both 1× and 2×; a live UTF-8 Αθήνα request passed.
- ESP-IDF HTTP/NVS compile and physical board validation remain external gates and are not claimed.

## Unified corrective UI/UX v10.2 — simulator verified

- Audited the actual v10 LVGL output against the unified v10.2 directive and preserved a restorable 371-entry source archive before editing.
- Consolidated square-display safe rectangles and rebuilt the shared status component with grid/flex ownership, semantic Wi-Fi/Bluetooth assets, host-centered time and one battery/charging cluster.
- Removed the Home tagline, replaced the custom microphone drawing with the registered icon inside the ask pill, retained one full-surface callback and preserved all 18 routes.
- Increased Mercury deformation margin and introduced visibly different Idle/Listening/Thinking/Speaking material fields without increasing the fixed 92,928-byte buffer.
- Added a v10.2 evidence pipeline covering 18 screens, three typography pages, 32 motion frames, 16 status combinations and pointer coverage at 1×/2×.
- Final Release `scripts/test.ps1` and live Local Codex CLI verification passed. ESP-IDF compile was not run because the required 5.5.5 environment is unavailable; device validation still needs hardware.

## Final Mercury/Lordicon/typography v10 — simulator verified

- Replaced the production typography stack with generated 4-bpp Space Grotesk branding and IBM Plex Sans Greek/English UI roles; added reproducible pinned-source generation, runtime glyph tests and a three-page 480×480 proof.
- Added a complete 74-role semantic icon registry backed by 71 Lucide ISC A8 assets. Lordicon candidates and the legal fallback decision are documented without scraping or mislabelling assets.
- Implemented one 176×176 `AuraMercuryView` for Idle, Listening, Thinking and PCM-reactive Speaking, with 92,928 bytes of fixed RGB565+A8 storage and regional invalidation.
- Rebuilt Offline/Error icon wells, preserved all 18 routes and retained Audio, Aoraton confirmation, provider, simulator and firmware boundaries.
- Captured 18 production screens, three typography pages and 32 time-separated Mercury frames from the native LVGL simulator.
- Automated SDL/LVGL pointer coverage now passes 80/80 checks at both 1× and 2× scale.
- Added v10 evidence generation to `scripts/test.ps1` and a native asset-registry test to CTest.
- Firmware compile and physical display/touch/audio/power profiling remain explicit external gates.

## Premium UI/UX redesign v3 — implemented and inspected

- Central semantic theme/primitives and Noto Sans Greek 14/18/22 typography.
- Organic layered AURA presence with distinct Idle, Wake, Listening, Thinking and PCM-reactive Speaking geometry.
- Content-first short/long Answer reader, explicit Back/Ask-again and calm Offline/Error recovery.
- Legacy four-button footer removed; Home now exposes one Tools launcher and notification-owned recent activity.
- Cards, Notifications, Settings, Diagnostics, Audio and complete Aoraton native flows redesigned on the shared LVGL path.
- Greek Aoraton on-screen keyboard with physical keyboard support retained.
- Simulator-only discrete `DEV` status, expanded design review controls and measured host-loop diagnostics.
- 18 before baselines, 22 after PNG captures, contact sheet and two recorded visual review passes.
- 100-transition UI stress integrated into `scripts/test.ps1`.

## Preserved functionality

- Loopback Codex CLI provider switching/cancellation and bridge security boundaries.
- SDL2 PCM/WAV playback and audio diagnostics.
- ESP32 ES8311/BSP/PA_CTRL implementation and single LVGL-thread ownership.
- Bounded Aoraton mock, unread/reconnect/invalid-payload behavior and mandatory send confirmation.

## Pending gates

- ESP-IDF 5.5.5 compile is not runnable on this machine.
- Physical display/touch/frame-time/heap/PSRAM/power/audio validation requires the board.
- Production Aoraton transport/E2EE and Greek TTS remain intentionally unimplemented.

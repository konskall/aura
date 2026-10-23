# AURA v6 functional recovery report

## Repaired regressions

| Regression | Cause | Recovery | Verification |
|---|---|---|---|
| Icon area did not click while label did | Default-clickable decorative icon well sat above parent tile | Decorative children are non-clickable; semantic parent owns one handler | 4 coordinates × 4 Quick Action tiles at 1× and 2× |
| Home primary action could become dead after systemic reset change | Custom `lv_button` also passed through non-interactive reset | Explicit semantic opt-in after reset | Four Home CTA coordinates |
| Fake/misrouted launcher entries | Visual concept screens were treated as implementations | Launcher now exposes Assistant, Aoraton mock, Audio and Notifications only | Route report and screenshots |
| Circular watch frame on square panel | v4 copied the concept silhouette into the physical viewport | Full 480×480 square background/status/content regions | 18-screen contact sheet |
| Static poster-style AI states | One RGB565 bitmap per state; only opacity/small spinner changed | Shared time/amplitude/PCM-driven motion model plus LVGL primitives | 4 × 8 frame sequences and changed-pixel check |
| Thinking had no cancel | Static state composition omitted recovery | 220×50 cancel action remains active during motion | Pointer test during animation |
| Speaking implied audio from artwork | State used speaking visual when PCM played but provided little content distinction | Speaking occurs only while Audio state is Playing/Paused; text-only responses use reader | Audio/state integration tests |
| Frozen/unexplained status values | UI owned a capture constant and default snapshot values looked measured | `DeviceStatusModel` carries validity/origin; host clock is live, desktop battery/network are `SIM`, firmware defaults unavailable | Unit tests and captures |
| Settings routed to unrelated placeholders | Bluetooth/Display/Location rows targeted Voice/Battery/Map | Working provider, motion, Audio, Status and Diagnostics rows only | Settings pointer routes |
| Fake audio transport | Previous/next both invoked the same bundled WAV | Explicit Test tone and DEMO WAV actions; real pause/stop/mute/volume | SDL audio pointer flow |
| Notification list included fake meeting/weather | Static concept fixtures were styled as enabled rows | One disclosed Aoraton mock item or empty state, with working open/clear | State/count assertions |
| Aoraton callback could rebuild during its LVGL event | Service callback immediately reapplied UI snapshot | Event changes service state; next UI loop applies snapshot | Compose/confirm/cancel pointer flow + stress |
| On-screen keyboard disappeared | LVGL keyboard retained bottom alignment while positional offsets moved it off-canvas | Explicit top-left alignment; dark item styling | Compose screenshot + key taps |
| Rapid taps could hit a newly revealed control | Immediate route replacement had no action guard | 75 ms semantic-action debounce without blocking slider adjustment | Rapid double-tap report |

## Feature parity

Preserved and verified: assistant controller, mock provider, local Codex bridge, Greek answer reader, Windows PCM/WAV audio, runtime provider switch, notifications, diagnostics, status injection and Aoraton mock room/compose/explicit-confirm workflow.

Removed from enabled product navigation because no implementation exists: Search, weather, navigation/GPS, calendar, camera capture, notes, reminders, translation, calculator, Bluetooth headset audio and alternate voice/TTS selection. This is functional honesty rather than a loss of working capability; the pre-v6 audit found only static graphics or unrelated destinations.

The detailed before/current/fix/test table is in `FEATURE_INVENTORY.md`; the enabled action contract is in `ROUTE_ACTION_MATRIX.md`.

## Exact v6 source/documentation changes

- `CMakeLists.txt`
- `firmware/components/aura_shared/CMakeLists.txt`
- `firmware/main/app_main.cpp`
- `shared/core/include/aura/assistant_state.hpp`
- `shared/core/include/aura/device_status.hpp`
- `shared/core/include/aura/motion_model.hpp`
- `shared/core/include/aura/navigation_stack.hpp`
- `shared/core/src/assistant_state.cpp`
- `shared/core/src/device_status.cpp`
- `shared/core/src/motion_model.cpp`
- `shared/localization/include/aura/visual_strings.hpp`
- `shared/localization/src/visual_strings.cpp`
- `shared/ui/include/aura/aura_theme.hpp`
- `shared/ui/include/aura/aura_ui.hpp`
- `shared/ui/src/aura_theme.cpp`
- `shared/ui/src/aura_ui.cpp`
- `simulator/app/include/aura/windows_audio_backend.hpp`
- `simulator/app/src/main.cpp`
- `simulator/app/src/windows_audio_backend.cpp`
- `simulator/controls/public/index.html`
- `simulator/controls/server.mjs`
- `simulator/controls/tests/server.test.mjs`
- `tests/unit/assistant_state_test.cpp`
- `tests/unit/ui_recovery_models_test.cpp`
- `scripts/capture-screenshots.ps1`
- `scripts/capture-motion-evidence.ps1`
- `scripts/make-v6-evidence.py`
- `scripts/test.ps1`
- `docs/VERIFICATION.md`
- `docs/ui/FEATURE_INVENTORY.md`
- `docs/ui/ROUTE_ACTION_MATRIX.md`
- `docs/ui/DESIGN_SYSTEM.md`
- `docs/ui/MOTION_SPEC.md`
- `docs/ui/TOUCH_HIT_TESTS.md`
- `docs/ui/VISUAL_QA.md`
- `docs/ui/REGRESSION_REPORT.md`
- `docs/ui/HARDWARE_LIMITATIONS.md`

Generated evidence lives under `artifacts/v6/`; the reversible pre-change archive is under `artifacts/recovery/`.

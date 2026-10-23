# AURA v12 "Halo" redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> Execution for this pass: native (the owner asked for the proposal to be built directly, without a review round).

**Goal:** Rebuild every shared LVGL screen at watch-class sizes with the "Halo" visual language while keeping the
UI/state/callback contracts, simulator/firmware parity and every verification gate.

**Architecture:** Same `AuraUi::Impl` render-per-view model. New tokens, fonts (Plex 22/26/28/32 + Light 72/96
subsets) and 64 px icon sources feed a small component layer (header, row, pill, chip, round button, halo ring)
that every `render_*` uses. Core gains a date in `DeviceStatusSnapshot`; tests and evidence validators move to the
new geometry.

**Tech Stack:** C++17 shared core, LVGL 9.6, SDL2 simulator, lv_font_conv 1.5.3, Lucide via resvg, Python/Pillow
evidence.

**Spec:** `docs/design/AURA_V12_HALO_DESIGN.md`

## Global Constraints

- Shared code compiles under ESP-IDF flags (`gnu++2b -Wall -Wextra -Werror -fno-exceptions -fno-rtti`);
  `scripts/check-firmware-flags.ps1` stays green.
- All visible copy in `shared/localization` (Greek default, English fallback, UTF-8); DEMO data says DEMO.
- One LVGL thread; no background task touches LVGL; `apply_*` stay sequence-first.
- Firmware is not compiled in this pass; `docs/VERIFICATION.md` records it as NOT RUN.
- No commits.

## Review Focus

- Long untrusted strings (32-byte SSIDs, room names, provider answers) in larger fonts: titles/rows must clip with
  an ellipsis, never overlap the time or a trailing control.
- Clock unknown (firmware before SNTP) and date unknown: Home must show the wordmark, never `--:--` boxes.
- No `on_ask` (firmware today): the orb must not look tappable and must say why.
- Bright halo pixels inside the orb's image border (Mercury evidence rule) at any listening/speaking level.
- Pointer coverage at 2× scale: every new target keeps its hit area at the new coordinates.

---

### Task 1: Fonts

**Files:** `tools/fonts/build-fonts.ps1`, `tools/fonts/verify-font-sources.py`, `shared/assets/fonts/*.c`,
`CMakeLists.txt`, `firmware/components/aura_shared/CMakeLists.txt`, `shared/ui/include/aura/aura_theme.hpp`,
`shared/ui/src/aura_theme.cpp`, `tests/unit/ui_assets_test.cpp`

**Produces:** `font_caption()` (Regular 22), `font_body()` (Regular 26), `font_headline()` (Medium 28),
`font_title()` (SemiBold 32), `font_display()` (Light 72 subset), `font_clock()` (Light 96 subset), `font_brand()`.

- [x] Add `IBMPlexSans-Light.ttf` (pinned commit) and the six jobs; subsets get `.fallback = &aura_font_plex_semibold_32`.
- [x] Remove the 14/17/18/20/24/40/64 px and Inter objects from both CMake lists and the script.
- [x] Test: the 33 required codepoints resolve in caption/body/headline/title; display covers `0-9 % + - . / : ° C −`
      and space; clock covers `0-9 : -` and space; all Strings/VisualStrings resolve in `font_caption()`.
- [x] Run `cmake --build build/native --target aura_ui_assets_tests` and the test: PASS.

### Task 2: Icons at 64 px

**Files:** `tools/icons/generate-icons.mjs`, `shared/assets/icons/*`, `shared/ui/include/aura/aura_icons.hpp`,
`shared/ui/src/aura_icons.cpp`, `tests/unit/ui_assets_test.cpp`

**Produces:** roles `ChevronLeft` (`chevron-left`), `Check` (`check`), `Plus` (`plus`); 64 × 64 A8 sources;
`icon_view` scales by `size * LV_SCALE_NONE / 64`.

- [x] Regenerate; test expects 64 × 64 sources and the three new roles.

### Task 3: Date in device status

**Files:** `shared/core/include/aura/device_status.hpp`, `shared/core/src/device_status.cpp`,
`shared/localization/*`, `simulator/app/src/main.cpp`, `firmware/main/app_main.cpp`, device status unit test.

**Produces:** `DeviceStatusModel::set_date(int year, int month, int day, int weekday, StatusOrigin)` and
`set_date_unavailable()`; snapshot fields `date_valid, year, month, day, weekday` (weekday 0 = Sunday);
`VisualStrings::weekday_names[7]`, `month_names_genitive[12]`, `date_format` ("%s %d %s").

- [x] Test: setting the same date twice does not bump `sequence`; invalid month/day/weekday is rejected.
- [x] Simulator sets it from `localtime_s` each second; captures pin Thursday 22 October 2026; firmware sets it
      from `localtime_r` after SNTP.

### Task 4: Theme and components

**Files:** `shared/ui/include/aura/aura_theme.hpp`, `shared/ui/src/aura_theme.cpp`, `shared/ui/src/aura_keyboard.cpp`,
`shared/ui/src/aura_ui.cpp`

- [x] Tokens per spec; `make_button` → 80 px pill, no shadow; `style_glass` → flat platter (no shadow).
- [x] In `Impl`: `make_header(title, action)`, `row(list, y, title, subtitle, icon, accent)`, `chip(...)`,
      `round_button(...)` (tinted, no border), `create_halo(...)`, toast and input restyle.
- [x] Keyboard: full width, 28 px legends, 14 px key radius, platter keys.

### Task 5: Screens

**Files:** `shared/ui/src/aura_ui.cpp`, `shared/localization/*`

- [x] Home, Apps, Listening/Thinking/Speaking (halo), Answer, Failure, Notifications, Settings, Wi-Fi ×3, Audio,
      Status, Diagnostics, Aoraton ×5, Weather ×2, Unavailable, Typography proof — geometry per spec.
- [x] Status chrome: Home variant (Wi-Fi left, battery right) and inner variant (time right, exception icons).
- [x] Gestures: Home left/up → Apps, down → Notifications.

### Task 6: Tests and evidence

**Files:** `simulator/app/src/main.cpp`, `scripts/make-v10_2-evidence.py`, `scripts/make-v10_3_1-weather-evidence.py`,
`tests/unit/ui_assets_test.cpp`

- [x] Captures clear restored weather first (deterministic evidence); Home capture uses labelled DEMO weather.
- [x] Pointer routes re-targeted to the new geometry (same actions and assertions, same or more checks).
- [x] `MERCURY_BOX`/`MERCURY_SOURCE_BOX`, `WEATHER_SCENE_BOX`, the weather viewport size and the stress list
      follow the new layout.
- [x] Run `.\scripts\test.ps1`: exit 0; inspect the 24-screen sheet, typography proof and status matrix.

### Task 7: Documentation

- [x] `docs/UI_DESIGN_SYSTEM.md`, `docs/UI_FONT_AND_ICON_REGISTRY.md`, `docs/architecture/MEMORY_BUDGET.md`,
      `docs/SIMULATOR.md`, `docs/VERIFICATION.md` (v12 rows: PASSED/FAILED/NOT RUN/NEEDS HARDWARE as observed).

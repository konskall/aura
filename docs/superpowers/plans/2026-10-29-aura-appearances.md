# AURA Appearances Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** four appearances selectable in Settings on the device:

- Άλως (today's look);
- Μάρμαρο;
- Σέλας;
- Γραφίτης.

Each one carries its own palette, fonts, icons, component styles, six signature layouts and assistant presence.

**Architecture:**

- A core `Appearance` enum is persisted inside `DisplaySettings` (`display.txt`).
- A UI registry swaps runtime colour tokens, the font set, the icon family and the component metrics. It is driven by `AuraUi::set_appearance`.
- Signature screens dispatch on the active appearance.
- The assistant presence sits behind a `PresenceView` interface. Mercury is the first implementation.

**Tech stack:**

- C++17, LVGL 9.6, the SDL2 simulator and ESP-IDF 5.5.5;
- `lv_font_conv@1.5.3`, plus a pinned `fontTools` for static instances;
- resvg through the existing icon pipeline;
- pinned `@iconify-json` sets.

**Spec:** `docs/superpowers/specs/2026-10-29-aura-appearances-design.md`. The 1:1 references and `presence-reference.js` are in `docs/design/themes/`.

## Global Constraints

- **Shared code:** LVGL screens and models live in `shared/`. LVGL calls happen only on the UI thread (AGENTS.md rules 1–2).
- **Copy:** all visible copy lives in `shared/localization`, Greek first, English as the fallback (rule 3). New strings go in `CompanionStrings` (`s.x =` style).
- **Sizes:** text ≥ 22 px, controls ≥ 64 px, primary actions ≥ 80 px high, in every appearance and both languages.
- **Default:** `Appearance::Halo`. An unknown or missing persisted id means Halo.
- **Άλως pixel identity:** captures must match the pre-refactor baseline, except the `settings` scenario.
- **Firmware app size:** ≤ 85% of the `0x680000` partition (≥ 15% free), measured from `firmware/build/aura_firmware.map`.
- **Presences:** one exists at a time, at 15 fps (66 ms). Its buffer is allocated on attach and freed on detach. Reduced Motion freezes it on a stable frame.
- **Contrast:**
  - `text` ≥ 7:1 on `canvas` and `surface`;
  - `text_secondary` and `text_tertiary` ≥ 4.5:1 on `canvas`;
  - `on_primary` ≥ 3:1 on `blue` and `primary_end`.
- **Licences:** fonts are OFL. Phosphor is MIT. Material Symbols is Apache 2.0. Solar is CC BY 4.0, with attribution in `shared/assets/THIRD_PARTY.md` and the README.
- **Phase gate:** `.\scripts\test.ps1` passes at the end of every phase. From Phase 2 on, `idf.py build` must pass with no warnings, from an activated ESP-IDF 5.5.5 PowerShell (rule 8).
- **Records:** every meaningful build or test result is recorded in `docs/VERIFICATION.md` as PASSED, FAILED, NOT RUN or NEEDS HARDWARE (rule 7).
- **Commits:** none without the owner's word. Each phase ends at a checkpoint that asks.

## Review Focus

1. **Switching appearance while the assistant is listening or speaking.** The view and state stay as they are, the new presence continues at the current level, and there is no stale LVGL pointer. *Tested in Task 4 (`appearance.switch_while_listening`).*
2. **A `display.txt` from an older build (no `appearance=`) or with an unknown id (`appearance=neon`).** The result is Halo, and every other display setting is kept. *Tested in Task 1.*
3. **Long dynamic text in the signature layouts.** The cases are a long city, a long timer label, a long reminder and English copy. Text ends in an ellipsis, never overflows or overlaps, and targets keep ≥ 64 px. *Tested in each signature task with `Αγιος Νικόλαος Λασιθίου` and a 60-character timer label, in both languages.*
4. **Always-on and dim round trips in a non-Halo appearance.** The always-on screen uses the active palette and fonts, and waking restores the same view in the same appearance. *Tested in Task 4 (`appearance.aod_roundtrip`).*
5. **Factory reset.** It returns to Halo with no leftover tokens. *Tested in Task 5 (`appearance.factory_reset`).*

---

## File Structure

| File | Responsibility |
|---|---|
| `shared/core/include/aura/appearance.hpp`, `shared/core/src/appearance.cpp` *(new)* | `Appearance` enum, ids, parsing (no LVGL) |
| `shared/core/include/aura/display_power.hpp`, `src/display_power.cpp` | `DisplaySettings::appearance` + `appearance=` line |
| `shared/ui/include/aura/aura_appearance.hpp`, `shared/ui/src/aura_appearance.cpp` *(new)* | palettes, font sets, metrics, active appearance, contrast helper |
| `shared/ui/include/aura/aura_theme.hpp`, `src/aura_theme.cpp` | runtime tokens; font accessors through the active set; component helpers through the metrics |
| `shared/ui/include/aura/aura_icons.hpp`, `src/aura_icons.cpp` | per-appearance icon resolution and fallbacks |
| `shared/ui/include/aura/aura_presence.hpp`, `src/aura_presence.cpp` *(new)* | `PresenceView`, `make_presence(Appearance)` |
| `shared/ui/include/aura/aura_mercury_view.hpp`, `src/aura_mercury_view.cpp` | Mercury implements `PresenceView` |
| `shared/ui/src/aura_ui_appearance.inl` *(new)* | `render_appearance_settings()` (2 × 2 cards), included in `AuraUi::Impl` |
| `shared/ui/src/aura_ui_marble.inl`, `aura_ui_aurora.inl`, `aura_ui_graphite.inl` *(new, phases 3–5)* | the six signature layouts per appearance |
| `shared/ui/src/aura_pearl_view.cpp`, `aura_aurora_view.cpp`, `aura_stars_view.cpp` (+ headers) *(new, phases 3–5)* | presences |
| `shared/ui/include/aura/aura_ui.hpp`, `src/aura_ui.cpp`, `src/aura_ui_companion*.inl` | `UiView::AppearanceSettings`, `set_appearance`, Settings row, dispatch |
| `shared/localization/include/aura/companion_strings.hpp`, `src/companion_strings.cpp` | appearance copy |
| `simulator/app/src/main.cpp`, `companion_runtime.cpp` | `--appearance` flag, restore before first render, harness checks, capture scenarios |
| `firmware/components/aura_companion/src/esp32_companion.cpp` | restore before first render |
| `CMakeLists.txt`, `firmware/components/aura_shared/CMakeLists.txt` | new sources and tests (`check-firmware-flags.ps1` globs `shared/**/src`) |
| `tools/fonts/build-fonts.ps1`, `tools/icons/generate-icons.mjs` | new fonts and icon families (pinned) |
| `scripts/compare-captures.py` *(new)* | the Άλως pixel-identity gate |
| `tests/unit/appearance_test.cpp` *(new)*, `tests/unit/ui_assets_test.cpp` | core ids; registry, fonts, icons, contrast |

## Interfaces (shared by all tasks)

```cpp
// aura/appearance.hpp (core)
enum class Appearance : std::uint8_t { Halo, Marble, Aurora, Graphite };
inline constexpr std::array<Appearance, 4> kAppearances{Appearance::Halo, Appearance::Marble,
                                                        Appearance::Aurora, Appearance::Graphite};
const char* appearance_id(Appearance appearance) noexcept;      // "halo" "marble" "aurora" "graphite"
Appearance appearance_from_id(std::string_view id) noexcept;   // anything else -> Halo

// aura/aura_appearance.hpp (ui)
struct Palette { std::uint32_t canvas, surface, surface_high, surface_glass, separator, danger_surface,
                 text, text_secondary, text_tertiary, blue, primary_end, on_primary, indigo, ice, lilac,
                 magenta, mint, amber, pink, coral; };
struct FontSet { const lv_font_t *caption, *body, *headline, *title, *display, *clock, *hero; };
enum class RowStyle : std::uint8_t { Platter, Glass, Hairline };
enum class WellStyle : std::uint8_t { TintedCircle, HairlineCircle, Chip, Squircle };
struct Metrics { RowStyle rows; WellStyle wells; bool gradient_primary; bool outline_secondary;
                 int platter_radius; int button_radius; int hero_center_y; };
const Palette& palette_for(Appearance) noexcept;
const FontSet& fonts_for(Appearance) noexcept;
const Metrics& metrics_for(Appearance) noexcept;
void set_active_appearance(Appearance) noexcept;   // copies the palette into tokens::
Appearance active_appearance() noexcept;
const Metrics& active_metrics() noexcept;
// aura/aura_theme.hpp gains: const lv_font_t* font_hero();  // fonts.hero, or fonts.clock when null
double contrast_ratio(std::uint32_t foreground, std::uint32_t background) noexcept;  // WCAG 2.x

// aura/aura_icons.hpp
const lv_image_dsc_t* icon_asset(IconRole role) noexcept;                    // active appearance
const lv_image_dsc_t* icon_asset(IconRole role, Appearance appearance) noexcept;
bool icon_is_fallback(IconRole role, Appearance appearance) noexcept;         // true = Lucide stand-in

// aura/aura_presence.hpp
enum class PresenceState : std::uint8_t { Idle, Listening, Thinking, Speaking };
class PresenceView {
public:
    virtual ~PresenceView() = default;
    virtual lv_obj_t* attach(lv_obj_t* parent, int center_x, int center_y) = 0;
    virtual void detach() noexcept = 0;
    virtual void set_state(PresenceState state) noexcept = 0;
    virtual void set_level(float level) noexcept = 0;
    virtual void set_reduced_motion(bool reduced) noexcept = 0;
    virtual bool tick(std::uint32_t elapsed_ms) = 0;       // true when a frame was drawn
    virtual std::uint64_t frame_count() const noexcept = 0;
    virtual std::size_t buffer_bytes() const noexcept = 0;
};
std::unique_ptr<PresenceView> make_presence(Appearance appearance);

// AuraUi
void set_appearance(Appearance appearance);  // before or after create(); re-renders when it changes
Appearance appearance() const noexcept;
// UiView::AppearanceSettings is added right after UiView::DisplaySettings.
```

Copy (`CompanionStrings`) has five keys:

| Key | Greek | English |
|---|---|---|
| `appearance_title` | «Εμφάνιση» | "Appearance" |
| `appearance_halo` | «Άλως» | "Halo" |
| `appearance_marble` | «Μάρμαρο» | "Marble" |
| `appearance_aurora` | «Σέλας» | "Aurora" |
| `appearance_graphite` | «Γραφίτης» | "Graphite" |

---

## Phase 1: Infrastructure (Άλως only looks different by one Settings row)

### Task 0: Άλως baseline captures

**Files:**
- Create: `scripts/compare-captures.py`

**Interfaces:**
- Produces: `python scripts/compare-captures.py <baseline_dir> <candidate_dir> [--allow name ...]`.
  - It prints one line per differing scenario with its changed-pixel count.
  - It exits 1 on any difference not in `--allow`.

- [ ] **Step 1: Write the script and capture.** Build the simulator as it is now. Run `aura_simulator.exe --capture <s> --screenshot <dir>\<s>.bmp` for every scenario literal in `simulator/app/src/main.cpp`, into two folders: `scratch\baseline-a` and `scratch\baseline-b`.
- [ ] **Step 2: Check determinism.** Run `python scripts/compare-captures.py baseline-a baseline-b`.
  - Expected: exit 0.
  - If any scenario differs between identical runs, list it in the script's `NON_DETERMINISTIC` set with a one-line reason. It is then excluded from the gate.

### Task 1: Core appearance and persistence

**Files:**
- Create: `shared/core/include/aura/appearance.hpp`, `shared/core/src/appearance.cpp`, `tests/unit/appearance_test.cpp`
- Modify: `shared/core/include/aura/display_power.hpp`, `shared/core/src/display_power.cpp`, `CMakeLists.txt` (source and the `aura_appearance_tests` target), `firmware/components/aura_shared/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests.** In `appearance_test.cpp`:
  - `appearance_ids_round_trip`: for every `kAppearances` entry, `appearance_from_id(appearance_id(a)) == a`.
  - `unknown_id_is_halo`: `appearance_from_id("neon")`, `""` and `"HALO "` all give `Halo`.
  - `display_settings_keep_appearance`: `parse_display_settings(serialize_display_settings(s))` with `s.appearance = Graphite` gives back `Graphite`. The text contains `"appearance=graphite\n"`.
  - `old_display_file_is_halo`: parsing the old text `"aura-display 1\nalways_on=0\n"` gives `Halo` with `always_on == false`. `"appearance=neon"` gives `Halo` and keeps the other keys.
- [ ] **Step 2: Watch it fail.** Build: `cmake --build build\native --target aura_appearance_tests`. Expected: a compile error, because `aura/appearance.hpp` is missing.
- [ ] **Step 3: Implement.** Add the enum and functions from "Interfaces". Add `Appearance appearance{Appearance::Halo};` as the last field of `DisplaySettings`. Serialise it as the last line, `appearance=<id>`. When parsing, use `appearance_from_id` for that key.
- [ ] **Step 4: Run and confirm green.** Run `ctest --test-dir build\native -R "aura_appearance_tests|aura_companion_models_tests"`. Expected: both pass.

### Task 2: Registry and runtime tokens (four palettes; fonts and icons fall back to Plex and Lucide)

**Files:**
- Create: `shared/ui/include/aura/aura_appearance.hpp`, `shared/ui/src/aura_appearance.cpp`
- Modify: `shared/ui/include/aura/aura_theme.hpp`, `shared/ui/src/aura_theme.cpp`, `tests/unit/ui_assets_test.cpp`, both CMake lists

- [ ] **Step 1: Write the failing tests** in `ui_assets_test.cpp`:
  - `halo_palette_is_v12`: every `palette_for(Halo)` field equals the current constant. `primary_end == blue` and `on_primary == 0xFFFFFF`.
  - `spec_palettes`: each palette equals the spec table. Check one representative per row, for example Marble `text == 0xF2EDE4`, Aurora `primary_end == 0xC35CF0` and Graphite `text_tertiary == 0x737985`.
  - `palettes_meet_contrast`: the Global Constraints contrast rules hold for every appearance.
  - `activation_updates_tokens`: `set_active_appearance(Marble)` makes `tokens::text == 0xF2EDE4` and `tokens::on_primary == 0x0B0A09`. `set_active_appearance(Halo)` restores `0xF5F8FC`.
  - `font_accessors_follow_active_set`: `font_body() == fonts_for(active_appearance()).body` for every appearance. In Phase 1 every set is the Plex set. `font_hero()` equals `clock` when `hero` is null.
- [ ] **Step 2: Watch it fail** with a compile error.
- [ ] **Step 3: Implement.**
  - The tokens' `constexpr std::uint32_t` colour names become `inline std::uint32_t`. Add `on_primary` and `primary_end`. Keep the geometry `constexpr`.
  - Fix each constant-expression use that no longer compiles by making that local non-`constexpr`.
  - Fill the palettes from the spec table.
  - Make `Metrics` for Halo match today: `Platter`, `TintedCircle`, flat primary, filled secondary, radius 28, pill buttons and `hero_center_y = 260`.
  - Give the other three Halo metrics until their phase.
  - `make_button` uses `tokens::on_primary` for the Primary label colour (white in Halo).
- [ ] **Step 4: Run the tests and the Άλως gate.** Run `ctest -R aura_ui_assets_tests`, then the captures. Run `python scripts/compare-captures.py baseline-a after-task2`. Expected: exit 0.

### Task 3: PresenceView with Mercury behind it

**Files:**
- Create: `shared/ui/include/aura/aura_presence.hpp`, `shared/ui/src/aura_presence.cpp`
- Modify: `aura_mercury_view.hpp/.cpp`, `aura_ui.cpp` (Impl member `std::unique_ptr<ui::PresenceView> presence` replaces the `AuraMercuryView` member), both CMake lists, `ui_assets_test.cpp`

- [ ] **Step 1: Write the failing test** `presence_factory`.
  - `make_presence(a)` is non-null for every appearance.
  - After `attach(screen, 240, 260)` with ticks of 66 ms, `frame_count()` rises for Idle, Listening, Thinking and Speaking.
  - With `set_reduced_motion(true)` it does not rise.
  - `detach()` twice is safe.
  - Halo's `buffer_bytes() == 92928`.
- [ ] **Step 2: Watch it fail.**
- [ ] **Step 3: Implement.**
  - Mercury implements the interface. `attach` takes the centre (top-left = centre − 88), and `MercuryState` maps 1:1 to `PresenceState`.
  - `make_presence` returns Mercury for every appearance until phases 3–5.
  - The public `mercury_frame_count()` and `mercury_buffer_bytes()` report through `presence`.
- [ ] **Step 4: Confirm green.** Run `ctest -R aura_ui_assets_tests` and the Άλως gate. Both must pass.

### Task 4: `set_appearance`, Settings row and the Appearance screen

**Files:**
- Create: `shared/ui/src/aura_ui_appearance.inl`
- Modify: `aura_ui.hpp` (enum value, API), `aura_ui.cpp`, `aura_ui_companion.inl`, `aura_ui_companion_api.inl` (`apply_display`), `companion_strings.hpp/.cpp`, `simulator/app/src/main.cpp`, `docs/ui/ROUTE_ACTION_MATRIX.md`, `docs/ui/FEATURE_AND_ROUTE_PARITY.md`

**Behaviour:**

- `set_appearance(a)`:
  - stores `a` and calls `set_active_appearance(a)`;
  - if created and `a` changed, rebuilds the presence through `make_presence(a)`, keeping its state and level;
  - calls `render(current_view)`.
- **Settings row:** «Εμφάνιση» comes right after «Γλώσσα», with the active name as its subtitle and `IconRole::Display`. It opens `UiView::AppearanceSettings`.
- **The Appearance screen:**
  - The header shows `appearance_title`.
  - Four cards, 210 × 172, sit at (24,100), (246,100), (24,284) and (246,284). Each card is drawn from its own appearance:
    - `palette_for(a).surface` background with `metrics_for(a).platter_radius`;
    - the name in `fonts_for(a).title`;
    - Home, Assistant and Weather icons at 32 px from `icon_asset(r, a)`, tinted `palette_for(a).text`;
    - four 16 px swatches: `surface`, `text`, `blue`, `indigo`.
  - The selected card shows a 2 px `blue` border and a Check icon.
  - A tap calls `set_appearance(a)` and then `callbacks.on_display_settings(settings with appearance = a)`.
- **`apply_display`:** when the snapshot's `settings.appearance` differs from the previous snapshot's, it calls `set_appearance`. A snapshot that merely repeats an older appearance never reverts a local choice.
- **Back and swipe-back** from AppearanceSettings return to Settings.

- [ ] **Step 1: Write the failing harness checks** in `main.cpp`, next to `settings.display`:
  - `appearance.settings_row`: `find_text("Εμφάνιση")` and `find_text("Άλως")` on Settings. A tap opens `AppearanceSettings`.
  - `appearance.cards`: all four names are whole on the screen, in Greek and in English.
  - `appearance.pick`: a tap on «Μάρμαρο» gives `ui.appearance() == Marble`, the `on_display_settings` capture carries `Marble`, the view stays the same, the card shows its border, and `tokens::text == 0xF2EDE4`. The check then restores Halo.
  - `appearance.switch_while_listening` (Review Focus 1): in `Assistant`/Listening, `set_appearance(Graphite)` keeps the view and Listening state, and `mercury_frame_count()` keeps rising over 10 ticks.
  - `appearance.aod_roundtrip` (Review Focus 4): in Marble, with the AlwaysOn snapshot and then the Active one, the appearance is still Marble and the view is restored.
  - `appearance.sweep`: for each appearance, the stress list (plus `AppearanceSettings`) is shown ten times. LVGL memory after is within the existing stress bound.
  - Add `UiView::AppearanceSettings` to the stress view list and to the gesture-back list in `aura_ui.cpp` (the block holding `UiView::DisplaySettings`).
- [ ] **Step 2: Watch it fail** with a compile error on `UiView::AppearanceSettings` and `set_appearance`.
- [ ] **Step 3: Implement** the behaviour above, the five strings and the route-document rows.
- [ ] **Step 4: Confirm green.** Run the pointer harness at 1× and 2×. All checks pass, including the 217 existing ones. Then run the Άλως gate with `--allow settings`.

### Task 5: Restore before the first frame and `--appearance`

**Files:**
- Modify: `simulator/app/src/companion_runtime.cpp`, `simulator/app/src/main.cpp`, `firmware/components/aura_companion/src/esp32_companion.cpp`

- [ ] **Step 1: Write the failing harness checks:**
  - `appearance.restore`: a runtime started on a store whose `display.txt` holds `appearance=aurora` calls `ui.set_appearance(Aurora)` before `boot()`. The first rendered frame already has `tokens::text == 0xF3F5FF`.
  - `appearance.factory_reset` (Review Focus 5): from Marble, a factory reset gives Halo, `tokens::text == 0xF5F8FC`, and a `display.txt` that holds `appearance=halo` once saved.
- [ ] **Step 2: Watch them fail.**
- [ ] **Step 3: Implement.**
  - **Simulator:** the runtime and the firmware read the restored `DisplaySettings` and call `ui.set_appearance(settings.appearance)` before `create()` / `boot()`.
  - **`--appearance <id>`** in `main.cpp` overrides it for captures, and composes with `--capture`.
  - **Firmware:** the same call follows `display.txt` in `Esp32Companion::start()`.
- [ ] **Step 4: Confirm green.** Run the harness and the firmware build; both must pass.

### Task 6: Phase 1 gate

- [ ] Run `.\scripts\test.ps1`. Expected: exit 0.
- [ ] Run the Άλως gate with `--allow settings`. Expected: exit 0.
- [ ] Run `idf.py build` in activated ESP-IDF. Expected: no warnings.
- [ ] Add a VERIFICATION row: "Appearances phase 1".
- [ ] **Checkpoint:** report to the owner and ask about a commit.

## Phase 2: Assets

### Task 7: Fonts for Μάρμαρο, Σέλας, Γραφίτης

**Files:**
- Modify: `tools/fonts/build-fonts.ps1`, `aura_appearance.cpp` (font sets), `ui_assets_test.cpp`, both CMake lists
- Create: generated `shared/assets/fonts/aura_font_*.c` and `shared/assets/licenses/{NOTO_SERIF_DISPLAY,COMMISSIONER,MANROPE,ROBOTO_FLEX}_OFL.txt`

**Values from the spec:**

| Appearance | Role | Font, weight and axes |
|---|---|---|
| Marble | caption | Commissioner Regular 22 |
| Marble | body | Commissioner Regular 26 |
| Marble | headline | Commissioner Medium 28 |
| Marble | title | Noto Serif Display Regular 32 |
| Marble | display (numerals) | Noto Serif Display wght 300, 84 |
| Marble | clock (numerals) | Noto Serif Display wght 200, 128 |
| Aurora | caption | Manrope 500, 22 |
| Aurora | body | Manrope 500, 26 |
| Aurora | headline | Manrope 600, 28 |
| Aurora | title | Manrope 700, 32 |
| Aurora | display (numerals) | Manrope 300, 80 |
| Aurora | clock (numerals) | Manrope 200, 116 |
| Graphite | caption | Roboto Flex 500 wdth 100, 22 |
| Graphite | body | Roboto Flex 400, 26 |
| Graphite | headline | Roboto Flex 600 wdth 88, 28 |
| Graphite | title | Roboto Flex 600, 32 |
| Graphite | display (numerals) | Roboto Flex 300 wdth 46, 64 |
| Graphite | clock (numerals) | Roboto Flex 300 wdth 46, 116 |
| Graphite | hero (numerals) | Roboto Flex 300 wdth 46, 204 |

Text roles use `$uiRange`. Numeral roles use `0x20,0x25,0x2B,0x2D-0x3A,0x43,0xB0,0x2212`, with the title font as fallback. Object names follow `aura_font_<family>_<weight>_<size>`, with `_cond` for wdth < 100.

- [ ] **Step 1: Write the failing tests.**
  - `appearance_fonts_cover_copy`: `expect_complete_table` checks the glyphs of every string table against the caption, body, headline and title fonts of every appearance.
  - `appearance_numerals`: every display, clock and hero font has the glyphs for `"0123456789:°%−+-./C "`.
- [ ] **Step 2: Watch them fail.** Every non-Halo set is still Plex, so the identity checks in the test fail: the Marble title must not be Plex.
- [ ] **Step 3: Implement.**
  - Pinned google/fonts commit for the four variable sources.
  - A `.tools\fonts-venv` with a pinned `fonttools`, whose version is recorded in the script.
  - Run `fontTools.varLib.instancer` with explicit axis values, then `lv_font_conv@1.5.3 --bpp 4` as today.
  - Fill `fonts_for`.
- [ ] **Step 4: Confirm green.** `ctest -R aura_ui_assets_tests` passes. Run the Άλως gate.

### Task 8: Icon families

**Files:**
- Modify: `tools/icons/generate-icons.mjs`, `tools/icons/build-icons.ps1`, `aura_icons.hpp/.cpp`, `ui_assets_test.cpp`, both CMake lists, `shared/assets/THIRD_PARTY.md`, `docs/ui/ICON_REGISTRY_AND_LICENSES.md`
- Create: generated `shared/assets/icons/aura_icon_assets_{phosphor,solar,material}.cpp` and `.hpp`, plus their `manifest-*.json`

**Values:**

| Family | Package | Appearance | Style |
|---|---|---|---|
| Phosphor | `@iconify-json/ph` | Marble | `-light` |
| Solar | `@iconify-json/solar` | Aurora | `-bold-duotone`; `-linear` for Back, Forward, Chevron, ChevronLeft and Close |
| Material Symbols | `@iconify-json/material-symbols` | Graphite | `-rounded`, filled |

All three packages are pinned in `.tools/icon-build/package.json`. Every family covers every `IconRole` through an explicit map. The known fallback is Solar Languages, which uses `dialog-2`. Unmapped roles fall back to Lucide and are listed in the manifest. Symbols are named `aura_icon_<family>_<name>`, at 64 px A8, as today.

- [ ] **Step 1: Write the failing test** `appearance_icons`. For every appearance and every role, `icon_asset(r, a)` is A8 and 64 × 64. For Marble, Aurora and Graphite it differs from Lucide unless `icon_is_fallback(r, a)` is true. At most 8 fallbacks are allowed per family.
- [ ] **Step 2: Watch it fail.**
- [ ] **Step 3: Implement** the generation and the per-family resolution tables, indexed by `IconRole`.
- [ ] **Step 4: Confirm green.** The test passes. Run the Άλως gate.

### Task 9: Budget

- [ ] Run `idf.py build`. Read the font, icon and total app sizes from the map.
  - Expected: no warnings, and the app ≤ 85% of `0x680000`.
  - If it is above that, apply the spec's mitigations in order, then rebuild.
- [ ] Add a VERIFICATION row, "Appearances phase 2 assets", with the measured bytes.
- [ ] Run `.\scripts\test.ps1` and confirm exit 0.
- [ ] **Checkpoint.**

## Phase 3: Μάρμαρο

### Task 10: Marble component kit

**Values:** `Metrics{Hairline, HairlineCircle, gradient_primary=false, outline_secondary=true, platter_radius=24, button_radius=LV_RADIUS_CIRCLE, hero_center_y=278}`.

The component rules are these:

- **Rows:** no fill, with a 1 px `separator` line at the bottom.
- **Primary buttons:** ivory with dark text.
- **Secondary buttons:** a 1.5 px `0x4A443D` outline.
- **Icon wells:** an 82 px hairline circle, filled with `canvas`.
- **Headers and titles:** `font_title()` (the serif).

- [ ] **Step 1: Failing harness check `marble.kit`.** In Marble:
  - a Settings row has `bg_opa == LV_OPA_TRANSP` and a bottom border of 1 px in `separator`;
  - the Primary label colour equals `on_primary`;
  - a Secondary button has a border width ≥ 1;
  - the header title's font equals `fonts_for(Marble).title`.
- [ ] **Step 2: Implement** in `list_row`, `settings_row`, `make_button`/`pill`, the round buttons, `icon_view` wells and `make_header`, reading `active_metrics()`.
- [ ] **Step 3: Confirm green.** Run the harness and the Άλως gate. Capture a contact sheet of the generic screens in Marble (Wi-Fi, notes, translate, a keyboard form, failure, system) and review it.

### Task 11: Marble signature layouts

**Files:**
- Create: `shared/ui/src/aura_ui_marble.inl`, with `render_home_marble()`, `render_answer_marble()`, `render_launcher_marble()`, `render_weather_marble()`, `render_clock_item_marble()` and `render_briefing_marble()`
- Modify: the six render entry points dispatch on `active_appearance()`

The reference is `docs/design/themes/marble-*.png`. The spec rules apply:

- Apps button top right, at least 64 px, with the unread dot.
- The presence centre is `(240, metrics.hero_center_y)` on Home and in the state screens.
- Single-line labels use `line()`.
- The data comes from the real models.

- [ ] **Step 1: Failing harness checks `marble.home`, `.answer`, `.launcher`, `.weather`, `.timer`, `.briefing`.** Run each in Greek and English. Each check:
  - finds the key labels whole: clock, date, greeting, titles, condition, the timer label, «Ρώτα ξανά» and «Νέο θέμα»;
  - confirms every clickable child is ≥ 64 × 64.

  Specific checks:
  - **Home:** Apps opens the Launcher; a tap on the presence starts Listening.
  - **Review Focus 3:** with the city `Αγιος Νικόλαος Λασιθίου` and a 60-character timer label, the labels end in an ellipsis and stay inside their boxes (`lv_obj_get_coords` within the screen).
- [ ] **Step 2: Implement** the six layouts.
- [ ] **Step 3: Confirm green.** Run the harness at 1× and 2× and the Άλως gate.

### Task 12: Μαργαριτάρι presence

**Files:**
- Create: `shared/ui/include/aura/aura_pearl_view.hpp`, `shared/ui/src/aura_pearl_view.cpp`
- Modify: `make_presence` (Marble returns Pearl)

**Values:**

- **Sphere:** a radius-80 sphere rendered per pixel into a 176 × 176 RGB565 + A8 buffer (92,928 B), at 66 ms per frame.
- **Rings, ripples and the thinking comet:** LVGL arcs owned by the view, radius up to 205.
- **Behaviour:** follows `pearlDraw` in `presence-reference.js`.
- **State blend:** an exponential ease with a 200 ms time constant.

- [ ] **Step 1: Failing test `pearl_presence`.**
  - `buffer_bytes() == 92928`.
  - Frames advance in every state.
  - Over 10 Listening ticks, the outer-ring radius at level 1.0 is greater than at level 0.0.
  - Reduced Motion freezes the frame count.
  - `detach()` frees the arcs, and a second `attach` works.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Confirm green.** The unit test passes, and `appearance.switch_while_listening` passes in Marble.

### Task 13: Marble review and gate

- [ ] Make the contact sheets: the six signature screens, Appearance and the six generic screens, in Greek and English. Review them against the references and fix what is found.
- [ ] Run `.\scripts\test.ps1` and `idf.py build`. Add a VERIFICATION row.
- [ ] **Checkpoint.**

## Phase 4: Σέλας (same task shape as Phase 3; only the values differ)

### Task 14: Aurora kit

**Values:** `Metrics{Glass, Chip, gradient_primary=true, outline_secondary=false, platter_radius=28, button_radius=LV_RADIUS_CIRCLE, hero_center_y=234}`.

- **Glass:** a vertical `bg_grad` from `0x1A1F34` to `0x0D1020`, with a 1 px border at `0xAAB4FF` and 11% opacity.
- **Primary buttons:** a horizontal gradient from `blue` to `primary_end`.
- **Chips:** 48 px rounded squares with the accent at 14%.
- **Light fields:** one generated 128 × 128 A8 soft blob, scaled and recoloured. It is used only on static regions.

The check is `aurora.kit`: a row's `bg_grad_dir == LV_GRAD_DIR_VER`, and the Primary's `bg_grad_color == primary_end`.

### Task 15: Aurora signature layouts

The file is `aura_ui_aurora.inl`. The references are `docs/design/themes/aurora-*.png`. The checks are `aurora.home` through `.briefing`, with the same assertions as Task 11. The timer ring is 36 segmented arcs, each coloured from teal through blue and violet to rose.

### Task 16: Σέλας presence

The files are `aura_aurora_view.hpp/.cpp`.

- **Rendering:** three ribbons in a 432 × 200 RGB565 + A8 band (259,200 B), following `Aurora.draw`. While waving, glow is drawn per column. The thinking figure-8 uses stamped points, with additive blending.
- **Test** `aurora_presence`:
  - `buffer_bytes() == 259200`;
  - the ribbon amplitude at a Listening level of 1.0 is greater than at 0.0;
  - the thinking blend reaches the loop form after 1 s;
  - Reduced Motion freezes it;
  - re-attach works.

### Task 17: Aurora review and gate

The same as Task 13. Then the **checkpoint**.

## Phase 5: Γραφίτης (same task shape)

### Task 18: Graphite kit

**Values:** `Metrics{Platter, Squircle, gradient_primary=false, outline_secondary=false, platter_radius=24, button_radius=22, hero_center_y=260}`.

- **Primary buttons:** white with black text.
- **Squircles:** 52 px with the accent at 15%.
- **Numeral sizes:** the display role is 64 condensed, the clock role 116 and the hero role 204.

The check is `graphite.kit`.

### Task 19: Graphite signature layouts

The file is `aura_ui_graphite.inl`. The references are `docs/design/themes/graphite-*.png`.

- **Home:** the hero clock and a 2 × 2 grid of live tiles. The «Ρώτα την Αύρα» tile opens Listening.
- **Answer:** the structured weather card appears only when the turn was answered from the loaded forecast (`AssistantExtras` or the weather context the device built). Otherwise the answer is text only.
- **Weather:** the gauges are an `lv_arc` humidity ring, a compass of `lv_line` lines and segmented air-quality bars.
- **Checks:** `graphite.home` through `.briefing`.

### Task 20: Αστερισμός presence

The files are `aura_stars_view.hpp/.cpp`.

- **Rendering:** 300 Fibonacci points rotated, projected and stamped into a 280 × 280 RGB565 + A8 buffer (235,200 B), following `Stars.draw`.
- **Test** `stars_presence`:
  - `buffer_bytes() == 235200`;
  - the point displacement at a Listening level of 1.0 is greater than at 0.0;
  - the thinking band moves;
  - Reduced Motion freezes it;
  - re-attach works.

### Task 21: Graphite review and gate

The same as Task 13. Then the **checkpoint**.

## Phase 6: Close-out

### Task 22: Documents and the final gate

- [ ] **Update the documents:**
  - README: Settings → Εμφάνιση, the four looks, a screenshot row and the credits, including the Solar CC BY attribution.
  - The spec's status line.
  - `docs/UI_FONT_AND_ICON_REGISTRY.md`, `docs/ui/ICON_REGISTRY_AND_LICENSES.md`, `docs/PERFORMANCE_BUDGET.md` (fonts, icons and presence buffers) and `docs/HARDWARE.md` if anything there changed.
- [ ] **Run the final gate:**
  - `.\scripts\test.ps1` exits 0;
  - `idf.py build` has no warnings;
  - VERIFICATION rows for the full suite, the firmware build and the measured app size;
  - a NEEDS HARDWARE row for presence frame time, flush cost and how the AMOLED looks.
- [ ] **Checkpoint:** ask the owner about committing, merging and pushing.

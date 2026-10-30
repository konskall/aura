# AURA appearances ("Εμφανίσεις"): design

Status: **APPROVED (2026-10-29) · IMPLEMENTED in phases 1–5** · branch `aura-themes` (from `a902f24`; commits `8775e57`, `372c895`, `256d8d9`) · merged into `main` and pushed on 2026-10-30 (`da9a0e5`); nothing has run on the device yet (results in `docs/VERIFICATION.md`, the decisions taken during the build at the end of the plan).
References: 1:1 mockups in `docs/design/themes/*.png` (480 × 480) and the motion reference `docs/design/themes/presence-reference.js`.

## Intent

On 2026-10-29 the owner reviewed three UI proposals and the assistant-motion concepts on a local server. They liked all three and asked for:

- all three to be built;
- the current look to be kept;
- all four to be selectable from Settings on the device.

The goal is a professional, premium assistant whose owner can choose the character of the whole device.

## Goals

1. **Four appearances.** The device has four appearances, chosen in **Ρυθμίσεις → Εμφάνιση**:

   | Id | Greek name | English name | Character | Assistant presence |
   |---|---|---|---|---|
   | `halo` | Άλως | Halo | today's v12/v13 UI, unchanged | Mercury (today's) |
   | `marble` | Μάρμαρο | Marble | quiet luxury: serif numerals, ivory, hairlines | Μαργαριτάρι (pearl) |
   | `aurora` | Σέλας | Aurora | living light: glass surfaces, light fields, duotone icons | Σέλας (light ribbons) |
   | `graphite` | Γραφίτης | Graphite | precision instrument: condensed numerals, live tiles, gauges | Αστερισμός (dot sphere) |

2. **Instant, persistent switching.** A choice applies immediately, survives a restart and behaves the same in the simulator and the firmware.

3. **What an appearance defines:**
   - the colour tokens;
   - the fonts for every type role;
   - the icon family;
   - the component styles (buttons, rows, chips, wells, headers, toggles, sliders, inputs, keyboard);
   - its own layout on six **signature screens**;
   - its own assistant presence.

4. **Signature screens.** Each new appearance has its own layout, following the mockups, on these screens:
   - Home (`Home`);
   - the assistant (`Assistant`: the answer layout, plus the presence in listening, thinking and speaking);
   - Apps (`Launcher`);
   - Weather (`Weather`);
   - a running timer (`ClockItem`);
   - Today (`Briefing`).

5. **Every other view works in every appearance.** The other 48 views keep their structure and take the appearance through the shared components.

6. **The v12 ergonomics hold in all four.** These are text ≥ 22 px, controls ≥ 64 px and primary actions ≥ 80 px high, in Greek and in English.

## Non-goals

- Light themes, custom accent colours, and per-user font or icon mixing.
- Bespoke layouts for views other than the six signature screens.
- The «Περίγραμμα» (edge glow) concept. It was proposed but not chosen.
- Any change to services, state machines, privacy rules, DEMO labelling, the bridge or the firmware drivers.

## Anti-goals (failure even if it "works")

- A label cut off or a touch target under 64 px in any appearance or language.
- Άλως looking different after the refactor.
  - The only allowed change is the new Settings row.
- Assets that push the firmware app past 85% of its partition.
- A theme switch that leaks LVGL memory or leaves stale styles or a stale presence.
- Light effects that redraw large areas every frame on the ESP32-S3.

## Constraints

- **Rendering:** LVGL 9.6, with every LVGL call on the UI thread. Shared code lives in `shared/`, and the simulator and firmware stay in parity (AGENTS.md rules 1–2).
- **Copy:** all copy goes in `shared/localization`, Greek first and English as the fallback (rule 3).
- **Flash and RAM:** the measured map from 2026-10-29 has icons at **412,400 B** (Lucide, 64 px A8) and the seven Plex fonts at **225,903 B**. The app is `0x301f40` (about 3.0 MiB) of the `0x680000` partition, so 54% is free. New assets must leave **≥ 15% free**. If they would not, apply the mitigations in "Asset budget" in order.
- **Presence cost:** one presence object exists at a time. Its frame buffer is allocated on attach and freed on detach, and lives in PSRAM on the device. It refreshes at 15 fps (66 ms), like Mercury. Device frame time is **NEEDS HARDWARE**.
- **Licences:**
  - Fonts: Noto Serif Display, Commissioner, Manrope and Roboto Flex are all OFL.
  - Icons: Phosphor is MIT, Material Symbols is Apache 2.0, and Solar is CC BY 4.0, which needs attribution in `shared/assets/THIRD_PARTY.md` and in the README credits.
- **Commits:** none without the owner's word.

## Decisions

### The owner's (2026-10-29)

- Build all three new appearances, keep the current one, and make all four selectable in Settings on the device.
- Each appearance brings its matching presence: Άλως→Mercury, Μάρμαρο→Μαργαριτάρι, Σέλας→Σέλας, Γραφίτης→Αστερισμός.
- Commit the second-review fixes first (`a902f24` on `aura-review-2`) and build on the new branch `aura-themes`.

### The designer's calls (the owner may override any of them)

1. **Name of the current look.** It is called **«Άλως» (Halo)**, its v12 design name. "Τρέχουσα" would be ambiguous once another appearance is active.
2. **Default.** Άλως stays the default, so nothing changes until the owner picks.
3. **Where the setting lives.** It is a new row **«Εμφάνιση»** in Settings, right after «Γλώσσα», showing the active name. It opens a new view, `AppearanceSettings`.
   - The view is a 2 × 2 grid of specimen cards.
   - Each card is drawn live from its appearance: the name in the appearance's own title font, three icons from its family in its accent, and four colour swatches.
   - Tapping a card applies it at once. The whole UI, including this screen, re-renders in the new appearance, and the selected card shows a check.
   - No pre-rendered thumbnails are used, so the cards cost no flash and never go stale.
4. **The always-on screen and Boot** follow the active palette and fonts, with today's layout.
5. **Home always keeps a visible Apps button**, at least 64 px, with the notification dot. The mockups relied on the swipe alone, which fails the "visible control for every gesture" rule. It goes in the top-right corner in all three new Home layouts, and the status glyphs move beside it.
6. **Assistant state screens** (listening, thinking, speaking) keep today's structure:
   - the state title at the top;
   - the presence centred on the appearance's hero centre;
   - the transcript or caption under it;
   - the controls at the bottom.

   The appearance supplies the presence, fonts, colours and buttons. The answer screen uses the signature layout.

   Today's continuity rule still holds: tapping the presence on Home starts listening without the presence moving. To keep it, each appearance defines one hero centre, and its Home and its state screens both use it. The Home mockups may shift by up to about 25 px to fit. Γραφίτης has no large presence on Home; its «Ρώτα την Αύρα» tile opens the listening screen, where the sphere appears.

## Architecture

### Appearance registry (new: `aura/aura_appearance.hpp` / `.cpp`)

```cpp
enum class Appearance : std::uint8_t { Halo, Marble, Aurora, Graphite };
const char* appearance_id(Appearance) noexcept;          // "halo", "marble", "aurora", "graphite"
Appearance appearance_from_id(std::string_view) noexcept; // unknown -> Halo
void set_active_appearance(Appearance) noexcept;          // UI thread only
Appearance active_appearance() noexcept;
```

`AppearanceSettings` draws all four cards at once, so the registry also offers lookups for any appearance, not just the active one:

- `fonts_for(Appearance)`;
- `icon_asset(IconRole, Appearance)`;
- `palette_for(Appearance)`.

`set_active_appearance` loads three things:

- **Palette:** the existing `tokens::` colour names become runtime values. This is described under "Tokens".
- **Font set:** the existing `font_caption()…font_brand()` accessors return the active set. A new `font_hero()` is added.
- **Icon family:** `icon_asset(role)` resolves through it.

Component helpers read the theme metrics: `style_platter`, `make_button`, `make_tag`, the header and back button, rows, toggles, sliders, inputs and the keyboard. The metrics are these:

| Metric | What it controls |
|---|---|
| row style | platter, glass or hairline list |
| primary button | fill, gradient or ivory with dark text |
| radii | |
| icon well | tinted circle, hairline circle, chip or squircle |
| title font role | |

### Tokens: runtime values with the existing names

The `constexpr` colours in `aura_theme.hpp` become `inline` variables. `set_active_appearance` reassigns them.

This keeps the roughly 560 call sites unchanged. Any constant-expression use that stops compiling is fixed where it stands.

Two new semantic tokens are added:

- `on_primary`: text on a primary button.
- `primary_end`: the second stop of a gradient primary; it equals `blue` when the primary is flat.

The hue names keep their current meaning across appearances:

| Token | Meaning today | Άλως | Μάρμαρο | Σέλας | Γραφίτης |
|---|---|---|---|---|---|
| canvas | background | 000000 | 000000 | 000000 | 000000 |
| surface | platter / tile | 121822 | 110F0D | 11152A (glass: 1A1F34→0D1020) | 141519 |
| surface_high | pressed / raised | 1B2330 | 1A1714 | 1C2236 | 1D1F24 |
| surface_glass | selected | 222C3B | 24201B | 252C46 | 26292F |
| separator | hairlines, tracks | 2B3546 | 2B2723 | 2A3150 | 2B2E34 |
| danger_surface | danger button | 3A141B | 2A1512 | 3A1430 | 3A141B |
| text | primary text | F5F8FC | F2EDE4 | F3F5FF | FFFFFF |
| text_secondary | | A9B5C7 | B7AEA1 | A9B1CC | A3A8B2 |
| text_tertiary | | 748197 | 877D71 | 6F7797 | 737985 |
| blue | primary / interactive | 178BFF | F2EDE4 | 3A7BFF | FFFFFF |
| primary_end *(new)* | gradient end | =blue | =blue | C35CF0 | =blue |
| on_primary *(new)* | text on primary | FFFFFF | 0B0A09 | FFFFFF | 000000 |
| indigo | secondary accent | 4C52FF | DFC08C | 7B5CFF | A98CFF |
| ice | listening | 25D6FF | 7FE3EC | 38E1C9 | 5CC8FF |
| lilac | thinking | 8A45FF | B39CFF | 9D6BFF | A98CFF |
| magenta | speaking | DE58FB | F0A9C0 | FF6FB1 | FF5D7D |
| mint | success | 35ECC3 | A8C9A4 | 43D9A3 | 3DDC97 |
| amber | warning, time | FFCB3F | E9B872 | FFC266 | FFB547 |
| pink | alerts | FF579E | E58A7B | FF6FB1 | FF5D7D |
| coral | danger | FF6B78 | E58A7B | FF6B78 | FF5D7D |

The contrast rules are enforced by a unit test:

- `text` ≥ 7:1 on canvas and surface;
- `text_secondary` and `text_tertiary` ≥ 4.5:1 on canvas;
- `on_primary` ≥ 3:1 on both primary stops, since buttons use ≥ 28 px text.

### Type roles

Text roles cover the full UI range used today:

`0x20-0xFF, 0x300-0x36F, 0x370-0x3FF, 0x2010-0x2026, 0x20AC, 0x2122, 0x2190-0x2193, 0x2212`.

Numeral roles use the numeral subset, `0x20,0x25,0x2B,0x2D-0x3A,0x43,0xB0,0x2212`.

| Role | Άλως (today) | Μάρμαρο | Σέλας | Γραφίτης |
|---|---|---|---|---|
| caption 22 | Plex Regular | Commissioner Regular | Manrope Medium | Roboto Flex Medium |
| body 26 | Plex Regular | Commissioner Regular | Manrope Medium | Roboto Flex Regular |
| headline 28 | Plex Medium | Commissioner Medium | Manrope SemiBold | Roboto Flex SemiBold, wdth 88 |
| title 32 | Plex SemiBold | Noto Serif Display Regular | Manrope Bold | Roboto Flex SemiBold |
| display (numerals) | Plex Light 72 | Noto Serif Display Light 84 | Manrope Light 80 | Roboto Flex Light wdth 46, 64 |
| clock (numerals) | Plex Light 96 | Noto Serif Display ExtraLight 128 | Manrope ExtraLight 116 | Roboto Flex Light wdth 46, 116 |
| hero *(new)* (numerals) | = clock | = clock | = clock | Roboto Flex Light wdth 46, 204 (Home clock) |
| brand | Space Grotesk 26 | unchanged | unchanged | unchanged |

`tools/fonts/build-fonts.ps1` does the following:

1. It downloads the variable sources from pinned commits of the google/fonts repository.
2. It makes static instances with a pinned `fontTools` `varLib.instancer` run in a `.tools` venv.
3. It converts them with the pinned `lv_font_conv@1.5.3` at 4 bpp, as today.

`TypographyProof` shows the active set.

### Icon families

`tools/icons/generate-icons.mjs` gains three families from pinned `@iconify-json` packages:

- Phosphor Light, for Μάρμαρο;
- Solar Bold Duotone, with Solar Linear for chevrons and close, for Σέλας;
- Material Symbols Rounded filled, for Γραφίτης.

Each family has an explicit role map for every `IconRole`. They are rendered by the same resvg pipeline into 64 px A8, so a duotone icon stays one tint at two opacities.

A role with no faithful glyph in a family falls back to Lucide. The fallback is listed in `manifest.json` and asserted by the asset test. One known case is Solar's "translation", which is a broadcast glyph, so `dialog-2` is used.

### Presence views

A small interface, `PresenceView`, has these members:

- `attach(parent, rect)` and `detach()`;
- `set_state(Idle|Listening|Thinking|Speaking)`;
- `set_level(float)`;
- `set_reduced_motion(bool)`;
- `tick(ms)`.

Mercury is adapted to it. States blend over about 250 ms, so a change of state morphs instead of cutting, as the reference does. Reduced Motion freezes on a stable frame. The behaviour of each presence is defined by `presence-reference.js`.

| Presence | How it renders | Buffer (PSRAM) |
|---|---|---|
| Mercury | unchanged | 92,928 B (176², RGB565 + A8) |
| Μαργαριτάρι | sphere shaded per pixel (body, nacre, state tint, inner lobes while thinking, specular). Rings, ripples and the thinking comet are LVGL arcs around it. | 92,928 B |
| Σέλας | three ribbons drawn into a 432 × 200 band: a glow per column while waving, stamped points for the thinking figure-8, additive blending | 259,200 B |
| Αστερισμός | 300 Fibonacci points rotated, projected and stamped as anti-aliased dots, with depth setting size and opacity | 235,200 B (280²) |

The presence takes its level from today's sources: microphone amplitude while listening, and `AudioDiagnostics::envelope` while speaking.

Σέλας's light fields work as follows:

- Each field is one shared 128 × 128 A8 soft blob (16 KB), scaled and tinted.
- Fields are only used on static regions of the six signature screens.
- A field is never placed under an animated area. Glow around a presence is drawn by the presence itself.

## Asset budget

These are estimates from the measured Lucide and Plex sizes:

| Item | Per appearance | Three new |
|---|---|---|
| Icons, 64 px A8, about 105 unique glyphs | ≈ 410 KB | ≈ 1.23 MB |
| Text fonts (4 roles, full range) | ≈ 200 KB | ≈ 0.60 MB |
| Numeral fonts (digit subsets, up to 3) | ≈ 30–90 KB | ≈ 0.20 MB |
| Light-field blob | 16 KB (Σέλας only) | 0.02 MB |
| **Total** | | **≈ 2.05 MB** → app ≈ 5.2 MiB, about 21% of the partition free |

Mitigations if the measured app would leave less than 15% free, applied in this order:

1. 56 px icon sources, which save about 23% of icon bytes.
2. RLE-compressed fonts, after checking LVGL's compressed-font support and glyph-cache cost.
3. Lucide fallbacks for rarely used roles.

The phase-2 build records the real numbers in `docs/VERIFICATION.md`.

## Signature layouts (from the mockups)

The mockups in `docs/design/themes/` are the reference for the composition, hierarchy, sizes and spacing. The shared data and copy come from the real models and localization, not from the mockups. Every single-line dynamic label uses the existing ellipsis helper.

| Screen | Μάρμαρο | Σέλας | Γραφίτης |
|---|---|---|---|
| Home | serif clock (clock role), date, pearl (tap = talk), hint, two complications (weather, next alarm), Apps button top right | clock top left (display role), presence centred with its glow, hint, two glass tiles (weather, next alarm), Apps button top right | hero clock left, day and date right, 2 × 2 live tiles (weather, alarm, messages, «Ρώτα την Αύρα»), Apps button top right |
| Answer | small pearl in the header, the question as a serif quote, answer text, ivory «Ρώτα ξανά» and outline «Νέο θέμα» | question as a right-aligned glass bubble, answer text, gradient «Ρώτα ξανά» and a round speaker button | question line, structured card when the answer carries known weather facts (otherwise text only), white «Ρώτα ξανά» and graphite «Νέο θέμα» |
| Apps | 3 × 3 hairline circles with labels, gold unread dot | 3 × 3 glass tiles with a tinted glow and a count badge | 2-column live tiles (name and live status), colour only for apps with something happening |
| Weather | serif temperature, condition and range, hourly row between hairlines, 2 × 2 metric list | temperature over a sky light field, glass hourly strip, three glass metric tiles | condensed temperature, range bar, hourly bar chart, three gauges (humidity ring, wind compass, air quality bar) |
| Timer (`ClockItem`) | watch dial: 60 ticks, hairline track, champagne arc, serif digits, label, end time, three round controls | gradient ring (segmented arcs), digits, label, glass controls | instrument dial: ticks, amber arc, condensed digits, label, end time, three squircle controls |
| Today (`Briefing`) | serif greeting and date, hairline rows | greeting, glass rows with tinted chips | timeline: times on the left, tiles on the right, namedays in the subtitle |

Behaviour stays as it is today:

- the same callbacks, gestures, empty and DEMO states and accessibility names;
- the structured weather card in Γραφίτης is fed only by data the device already holds;
- nothing is parsed out of model text.

## Testing and acceptance

Each item fails first where it is new (TDD).

1. **Unit tests** in a new `aura_appearance_tests`:
   - Every appearance has a non-null font for every role.
   - Text fonts have a glyph for every character of a Greek and English sample, including ΐ, ΰ and accented vowels. Numeral fonts cover their subset.
   - Every `IconRole` resolves to a 64 × 64 A8 asset in every family, or a declared Lucide fallback.
   - The palettes pass the contrast rules.
   - `appearance_from_id` and `appearance_id` round-trip, and an unknown id gives Halo.
2. **Pointer harness:**
   - The Settings row shows the active name.
   - Each card applies its appearance, fires `on_appearance_changed`, keeps the view and shows the check.
   - The choice persists across a simulated restart.
   - For each new appearance, every view renders, and LVGL memory stays within today's stress bound.
   - On the six signature screens, in both languages, the key labels are whole (`find_text`) and every target is at least 64 px.
   - The existing 217 checks keep running on Άλως at 1× and 2×.
3. **Visual:**
   - Per-appearance contact sheets of the six signature screens, Settings → Εμφάνιση and six common screens (Wi-Fi list, notes, translate, a keyboard form, failure, system), in Greek and English. They are reviewed before each appearance is called done.
   - Άλως captures before and after the refactor are compared pixel by pixel. The only allowed difference is the new Settings row.
4. **Firmware:** `idf.py build` passes with no warnings in ESP-IDF 5.5.5. The app size and the percentage free are recorded.
5. **NEEDS HARDWARE:** presence frame time at 15 fps on the ESP32-S3, the flush cost of light fields and dials, and how the new palettes look on the AMOLED.

## Phases

The plan breaks these down. Each phase ends with `scripts/test.ps1` green, and from phase 2 on with a clean firmware build.

1. **Infrastructure:**
   - the registry, runtime tokens and component metrics;
   - `set_appearance` / `on_appearance_changed`;
   - persistence (`appearance.txt` beside `language.txt`, in the simulator and the firmware);
   - the Settings row and `AppearanceSettings`;
   - the `PresenceView` interface with Mercury behind it.

   Only Άλως is populated, and the pixel-identical gate applies.
2. **Assets:** fonts and icon families through the pinned pipelines, licences, budget measurement and the asset tests.
3. **Μάρμαρο:** palette, component kit, the six signature layouts, and the Μαργαριτάρι presence.
4. **Σέλας:** the same, with the light fields and the Σέλας presence.
5. **Γραφίτης:** the same, with the Αστερισμός presence.
6. **Close-out:** README and design-doc status, `docs/ui/ICON_REGISTRY_AND_LICENSES.md`, `UI_FONT_AND_ICON_REGISTRY.md`, `THIRD_PARTY.md`, `VERIFICATION.md`, and the full suite plus the firmware build.

## Left to the builder

- Exact pixel positions beyond the mockups, provided they keep the minimum sizes.
- The per-family role maps.
- The internal maths of each presence, following the reference.
- How signature layouts are split into files. A reasonable split is one `.inl` per appearance, included in `AuraUi::Impl` like today's companion views.
- The order of tests inside a phase.

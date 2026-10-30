# AURA v12 font and icon registry

Status: **SIMULATOR VERIFIED** · **FIRMWARE LINK NOT RUN** · **PHYSICAL DISPLAY UNTESTED**

## Typography actually compiled

| UI role | Compiled LVGL object | Family / weight / size | Coverage |
|---|---|---|---|
| Clock | `aura_font_plex_light_96` | IBM Plex Sans Light 96 px | digits, `:`, `-`, space |
| Display | `aura_font_plex_light_72` | IBM Plex Sans Light 72 px | digits, `% + - . / : ° C −`, space |
| Title | `aura_font_plex_semibold_32` | IBM Plex Sans SemiBold 32 px | full UI range |
| Headline | `aura_font_plex_medium_28` | IBM Plex Sans Medium 28 px | full UI range |
| Body | `aura_font_plex_regular_26` | IBM Plex Sans Regular 26 px | full UI range |
| Caption | `aura_font_plex_regular_22` | IBM Plex Sans Regular 22 px | full UI range |
| Wordmark | `aura_font_space_medium_26` | Space Grotesk Medium 26 px | `A R U`, space |

The full UI range is `0x20-0xFF, 0x300-0x36F, 0x370-0x3FF, 0x2010-0x2026, 0x20AC, 0x2122, 0x2190-0x2193, 0x2212`.
The two Light subsets fall back to the title font, so an unexpected character is drawn smaller rather than as an
empty box. Linked font data is 227,712 bytes (the retired 14/17/18/20/24/40/64 px set was 280,416 bytes).

`aura_ui_assets_tests` checks 33 required Latin, Greek, combining-accent, punctuation and currency codepoints in every
full-range font, every character the numeric formats print in the two subsets, the subset fallback, and that every
Strings/VisualStrings field in both languages resolves in the caption font. The Inter A/B proof font is retired.

## Semantic icons

All 104 roles resolve through `IconRole` to 100 tintable 64×64 A8 assets (416,032 bytes), drawn at 22–80 px. v13 added Languages, Notebook, Sunrise, Smartphone, Phone, PhoneOff, Download, BellRing, Moon, Hand, Flip, Ear and Swap (Lucide `languages`, `notebook-pen`, `sunrise`, `smartphone`, `phone`, `phone-off`, `download`, `bell-ring`, `moon`, `hand`, `flip-vertical`, `ear`, `arrow-left-right`). Active assets are **Lucide under ISC**, not Lordicon. No eligible licensed Lordicon export is present, so the lawful Lucide fallback and Lordicon candidate list remain recorded in `shared/assets/icons/manifest.json` and `docs/ui/ICON_REGISTRY_AND_LICENSES.md`.

Key corrected mappings:

| Role | Active source | Placement/action |
|---|---|---|
| Microphone | Lucide `mic` | Centered in the Home ask pill; pill parent owns the callback. |
| Bluetooth | Lucide `bluetooth` | Left header cluster; hidden when unavailable/disabled. |
| Charging | Lucide `battery-charging` | Conditional member of the right battery cluster. |
| Back / Home | Lucide `arrow-left` / `home` | Centered inside full 48×48 circular targets. |
| Settings | Lucide `settings` | Quick Actions bottom target and System row. |
| Offline | Lucide `wifi-off` | Dedicated balanced Offline symbol. |
| Error | Lucide `triangle-alert` | Dedicated Warning/Error symbol. |
| Sun / cloud variants | Lucide `sun`, `cloud`, `cloud-sun` | Current/hourly/daily weather conditions. |
| Rain / drizzle / snow / storm | Lucide weather assets | Condition-specific, tintable weather animation centers. |
| Wind / droplet / thermometer / clock | Lucide metric assets | Weather metrics and forecast context. |

The source audit finds no plain `"BLE"` label or legacy custom microphone renderer in `shared/`, `simulator/`, or `tests/`.

## Appearances (Settings → Εμφάνιση)

Άλως is the set above. The three new looks bring their own text and numeral fonts, generated from the variable
sources pinned to google/fonts commit `b68b528a75da7d9b73f3928899c07a23083a6633` (fontTools 4.65.0 instances,
`lv_font_conv` 1.5.3 at 4 bpp, see `tools/fonts/build-fonts.ps1`). Each face keeps only the code points it has and
falls back to the Plex font of the same role; numeral fonts fall back to their look's title font.

| Role | Μάρμαρο | Σέλας | Γραφίτης |
|---|---|---|---|
| Caption 22 | Commissioner Regular | Manrope Medium | Roboto Flex Medium |
| Body 26 | Commissioner Regular | Manrope Medium | Roboto Flex Regular |
| Headline 28 | Commissioner Medium | Manrope SemiBold | Roboto Flex SemiBold, condensed |
| Title 32 | Noto Serif Display Regular | Manrope Bold | Roboto Flex SemiBold |
| Display | Noto Serif Display Light 72 | Manrope Light 72 | Roboto Flex Light condensed 64 |
| Clock | Noto Serif Display ExtraLight 96 | Manrope ExtraLight 96 | Roboto Flex Light condensed 96 |
| Numerals small / large | Noto Serif Display Light 84 / ExtraLight 128 | Manrope Light 80 / ExtraLight 116 | Roboto Flex Light condensed 64 / 116 |
| Hero clock | — | — | Roboto Flex Light condensed 204 |

Icons: every `IconRole` resolves to a 64 px A8 asset in each look's family (`icon_asset(role)` follows the active
look): Lucide (Άλως), Phosphor Light (Μάρμαρο), Solar Bold Duotone (Σέλας) and Material Symbols Rounded
(Γραφίτης). Sources, versions and licences are in `docs/ui/ICON_REGISTRY_AND_LICENSES.md` and
`shared/assets/THIRD_PARTY.md`.

Measured flash (firmware map, phase 2): Μάρμαρο fonts 272,839 B and Phosphor icons 412,400 B; Σέλας fonts
235,716 B and Solar icons 408,276 B; Γραφίτης fonts 224,162 B and Material icons 416,524 B.

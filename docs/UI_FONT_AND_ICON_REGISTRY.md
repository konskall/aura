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

# AURA v10 production design system

This is the implemented v10 system for the shared LVGL application. The 480×480 Windows simulator and ESP32-S3 firmware compile the same screen and component code from `shared/ui`.

## Canvas and grid

- Fixed logical canvas: 480×480, square, edge to edge.
- Status bar: y=0–47. It is visually independent from animated content.
- App header: approximately y=48–104 with 48×48 Back/Home targets and a protected title width.
- Main content: x=16–464, normally y=104–468.
- Standard inset: 16 px; radii: 12/20/28 px; minimum enabled touch target: 48×48 px.
- Long answers and conversations scroll inside bounded containers. No bright visual layer sits beneath reading text.

## Color tokens

The source of truth is `shared/ui/include/aura/aura_theme.hpp`.

| Role | Value |
|---|---|
| Canvas / blue canvas | `#000105` / `#030712` |
| Surface / elevated / glass | `#0B1424` / `#111D30` / `#182337` |
| Separator | `#2A3951` |
| Text / secondary / tertiary | `#F8FBFF` / `#BACBE3` / `#8295B0` |
| Ice / blue / indigo | `#25D6FF` / `#178BFF` / `#4C52FF` |
| Lilac / magenta | `#8A45FF` / `#DE58FB` |
| Success / warning / error | `#35ECC3` / `#FFCB3F` / `#FF6B78` |

Blue/cyan communicates primary action and connectivity, violet identifies AURA intelligence, mint identifies healthy/active audio, amber identifies DEMO or caution, and coral is reserved for stop/error/destructive actions.

## Typography

The `AuraFonts` registry is exposed through semantic functions rather than screen-local font choices.

| Token | Family/weight | Size | Use |
|---|---|---:|---|
| `font_meta` | IBM Plex Sans Regular | 14 | provenance, supporting data |
| `font_status` | IBM Plex Sans Medium | 14 | status bar, compact labels |
| `font_action` | IBM Plex Sans Medium | 17 | buttons and controls |
| `font_body` | IBM Plex Sans Regular | 18 | answers, chat, descriptions |
| `font_heading` | IBM Plex Sans SemiBold | 20 | section headings |
| `font_title` | IBM Plex Sans SemiBold | 24 | screen title |
| `font_display` | IBM Plex Sans Medium | 40 | large device values |
| `font_brand` | Space Grotesk Medium | 26 | AURA wordmark only |

Greek is the default locale and English remains supported. Details and proof are in `TYPOGRAPHY_SPEC_AND_GREEK_GLYPH_TESTS.md`.

## Shared components

- `StatusBar`: source-aware Wi-Fi/Bluetooth/time/battery; no cellular/SIM fiction. Desktop mode is identified as `DEV`.
- `AppHeader`: consistent Back/title/Home geometry.
- `PrimaryAction`, `IconButton`, `ActionTile`, `ListRow`: one clickable parent; icon, label, border and empty interior route through that owner.
- `AuraIcon`: semantic `IconRole` resolved to tintable 48×48 A8 assets.
- `AuraMercuryView`: the same 176×176 liquid-metal presence in Idle, Listening, Thinking and Speaking.
- `ErrorState` / `OfflineState`: dedicated warning/Wi-Fi-off icon, concise explanation and implemented Retry/Home actions.

`ui::reset()` makes generic objects non-clickable and non-scrollable. Interactive components explicitly opt in, which prevents decorative halos and child labels from intercepting input.

## Motion

Content transitions use 120 ms micro, 220 ms content and 300 ms sheet timings. Mercury targets a 66 ms interval (about 15 FPS) and invalidates only its image region. Static text and the status bar remain pixel-stable. Reduced Motion freezes optional animation without disabling controls or state transitions.

## Screen language

Home and the three active assistant states share one centered presence at the same size and coordinate. Listening uses cyan emphasis and a DEMO provenance line when input is simulated. Thinking uses violet emphasis without fake progress. Speaking uses PCM-envelope-driven activity and real audio controls. Answer, Settings, Audio, Aoraton, Offline/Error, Battery and Diagnostics reuse the same glass surfaces, spacing, type roles and semantic icons.

The 18-screen production sheet is `artifacts/v10/AURA_V10_PRODUCTION_SCREENS.png`.

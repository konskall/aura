# AURA v6 square-display design system

> Historical v6 baseline. The implemented authoritative system is [AURA_V10_DESIGN_SYSTEM.md](AURA_V10_DESIGN_SYSTEM.md).

## Canvas and regions

- Native canvas: 480×480, square, OLED black/navy background. There is no watch bezel, circular viewport, corner mask or full-screen border.
- Status bar: y=0–43, pixel-stable and independent from animated content. It reports source-aware clock, connection and power values.
- App header: y=52–100 with 48×48 Back and Home controls. Titles have a 320 px safe width.
- Content: normally x=16–464 and y=104–468. Primary actions are at least 52 px high.

## Tokens

The C++ source of truth is `shared/ui/include/aura/aura_theme.hpp`.

| Role | Value |
|---|---|
| Canvas | `#000105` / `#030712` |
| Surface | `#0B1424` |
| Elevated surface | `#111D30` |
| Separator | `#2A3951` |
| Foreground | `#F8FBFF` |
| Secondary | `#BACBE3` |
| Cyan / blue | `#25D6FF` / `#178BFF` |
| Violet / magenta | `#8A45FF` / `#DE58FB` |
| Success / warning / error | `#35ECC3` / `#FFCB3F` / `#FF6B78` |
| Touch minimum | 48 px |
| Content transition | 220 ms |
| Motion update target | approximately 30 Hz |

Greek fonts are compiled at 14, 18, 22, 28 and 36 px. English remains the fallback. Bright artwork never carries unprotected long text.

## Components

- `StatusBar`: non-interactive live/simulated/unavailable values.
- `AppHeader`: Back, title, Home with stable hit targets.
- `PrimaryAction` / `IconButton`: one semantic LVGL owner; child icon and label are non-clickable.
- `ActionTile`: 212×132 glass tile. Icon, icon edge, label and gap all hit the same parent.
- `ListRow` / `MessageRow`: informational by default; clickability is opt-in only when a handler exists.
- `AIVisual`: bounded image layer plus native LVGL halo/bars/orbit/meter motion.
- `LoadingState`, `ErrorState`, `UnavailableState`: truthful status and an implemented recovery route.

`ui::reset()` makes generic objects non-clickable and non-scrollable. Semantic controls explicitly opt into clickability. This prevents decorative wells, halos, images and containers from intercepting pointer input.

## Enabled product surface

Home, assistant states/answer, Quick Actions, Notifications, Settings, Audio, Aoraton mock, Device Status and Diagnostics share the same component system. Search, weather, map/GPS, calendar, camera, generic applications and alternate voices are absent from product navigation; a direct simulator request shows a non-interactive unavailable state.

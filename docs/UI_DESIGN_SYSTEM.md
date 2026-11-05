# AURA v12 "Halo" UI design system

The production surface is a 2.16" square 480×480 AMOLED (~38.8 mm, ~12.4 px/mm). Sizes therefore follow
watch-class legibility, not phone-UI pixel habits: text is at least 22 px, controls at least 64 px and primary
actions 80 px high. The full rationale and screen map are in [design/AURA_V12_HALO_DESIGN.md](design/AURA_V12_HALO_DESIGN.md).

Tokens live in `shared/ui/include/aura/aura_theme.hpp`. The canvas is true black (`#000000`); content sits on
borderless graphite platters (`#121822`, pressed `#1B2330`, selected `#222C3B` with a 2 px accent hairline). There are
no drop shadows. The one blur is the header's glass, which frosts what scrolls under it. Icon wells and round buttons tint their accent at 22 %. The cyan/violet/magenta accents
are reserved for the assistant's states.

| Component | Geometry |
|---|---|
| Header | 64 px round back button (long press: Home) at (16,16), 32 px title from x=96, time or one 64 px action at the right, all on a glass capsule (8,8, 464×80): the look's surfaces at 69 % fading to 53 %, a 1 px light edge at 16 %, an 18 px backdrop blur. Lists scroll under it; a tap anywhere in the 96 px band stops at it |
| Row | 432×88 platter at x=24 on a 96 px pitch: 52 px icon well, 28 px title, 22 px subtitle, chevron/switch/badge |
| Pill button | 80 px high; bottom action row at y=380 (one 432 px or two 208 px pills) |
| Chip | 64 px pill with icon, 28 px value and 22 px detail (Home weather glance) |
| Apps | 3×2 grid of 104 px tinted circles with 22 px labels; the whole 144×164 cell is the target |
| Presence | Halo's Synapse: the approved browser presentation at 1:1 in a 480 × 252 frame centred at (240,246), behind every label and control; centred reactor, eight symmetric kinetic paths, wavefronts and resonance; same position on Home and in every assistant state |
| Keyboard | full width, 4 rows of 55–68 px keys, 28 px legends, pop-overs |

Status is shown by exception. Home carries Wi-Fi (also the shortcut to Wi-Fi settings), Bluetooth, the simulator-only
`DEV` tag, charging and battery in a 40 px band above the 96 px clock. Every other screen shows only the time in its
header, plus Wi-Fi-off, low battery or charging when they apply; a header action replaces the time.

Gestures: on Home, swipe up or left opens the apps and swipe down opens notifications; elsewhere swipe right goes
back on list and detail screens (not on screens with sliders, strips or a keyboard).

The same C++/LVGL hierarchy is compiled for simulator and firmware. Simulator controls and evidence tooling are
external development surfaces, not alternate product UI.

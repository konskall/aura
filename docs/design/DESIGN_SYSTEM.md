# AURA design system — Quiet Intelligence

The visual direction is calm intelligence, tactile precision and living light. OLED black is the canvas; matte blue-black surfaces establish depth; ice blue is the primary interactive light; lilac is reserved for cognition/Aoraton; mint communicates ready/secure; amber and coral communicate warning/error.

## Tokens

Tokens and reusable styling functions live in `shared/ui/include/aura/aura_theme.hpp` and `shared/ui/src/aura_theme.cpp`.

| Role | Value |
|---|---|
| Canvas | `#05070B` |
| Surface / elevated | `#101722` / `#172131` |
| Separator | `#293548` |
| Primary / secondary / tertiary text | `#F5F7FA` / `#A8B5C7` / `#78879B` |
| Identity ice / lilac | `#72D7FF` / `#AB9BFF` |
| Success / warning / error | `#5EDBBB` / `#F1BD73` / `#F18D94` |

Spacing follows a 4 px base and 8 px rhythm with 20 px edge insets. Frequent controls are at least 46 px high. Radii are 12/18/26 px. Matte fills and spacing provide hierarchy; outlines are reserved for focus and exceptional states.

## Type

Noto Sans Regular subsets provide complete Greek/Latin UI coverage at 14, 18 and 22 px. Montserrat 28 is used only for the Latin `AURA` wordmark. The 22 px subset is the screen-heading/primary-emphasis step; 18 px is body/action; 14 px is metadata. Important content never uses tertiary color at 14 px.

## Core primitives

- `SystemStatus`: time, privacy/listening state, connectivity and battery in a quiet 42 px band.
- `AIPresence`: layered organic forms with state-specific geometry and bounded local animation.
- `ScreenHeader`: explicit Back plus heading; no mystery gestures are required.
- `PrimaryAction`, `SecondaryAction`, `QuietAction`, `DangerAction`: shared press transform and semantic styling.
- `Surface` and `ListRow`: borderless matte grouping with optional accent markers.
- `Tag`: sparse use for provenance/security (`DEMO`, `E2EE`), never global decoration.
- `MessageBubble`, `SettingRow`, `ErrorState`, `TextReader`, audio meters and confirmation surfaces share the same tokens.

## Accessibility and interaction

State is communicated through copy and layout, not color alone. Reduced motion stops deformation and animated light while keeping state compositions distinct. Scrollbars are subtle but visible. User-facing fixture data is labeled `DEMO`; ordinary screens do not expose adapter/control-panel terminology. Unsupported controls are not shown as active.

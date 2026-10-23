# AURA v4 design system

## Circular application shell

- Physical framebuffer: 480×480, always captured raw as a square.
- Dial: 468×468 at (6,6), OLED black fill, 2 px low-luminance bezel and a restrained blue shadow.
- Status chord: y≈16–38; Wi-Fi left, time centered, battery right.
- Primary content safe area: x≈28–452 and y≈52–452. Dense rows use 398–414 px widths.
- Full-bleed map/media is clipped by a native LVGL rounded container; the outer square remains black.

## Surfaces and type

`style_glass()` supplies a dark vertical navy gradient, one-pixel low-opacity border, 18–28 px radius and accent-colored selective bloom. It is shared by lists, cards, pills and selected controls.

Noto Sans is generated at 14, 18, 22, 28 and 36 px with Modern Greek, Latin punctuation and technical characters. LVGL's bundled Font Awesome glyphs provide the consistent icon family; no Unicode emoji are shipped as icons.

## Signature presence

The four AI states use distinct RGB565 assets plus native widgets:

- Home: slow opacity breathing over a glasslike spherical render.
- Listening: horizontal waveform sculpture; native completion mic action.
- Thinking: hollow particle vortex; native rotating progress arc.
- Speaking: orb plus energy ribbon; native PCM envelope meter and stop action.

Capture mode freezes time and pauses animation to eliminate partial-frame nondeterminism. Runtime mode animates without mutating LVGL from background threads. Reduced motion suppresses decorative updates.

## Interaction

- Pressed controls shrink subtly through the shared button style.
- Selected controls gain an accent border and narrow bloom.
- Primary actions are at least 48 px; hero controls are 56–88 px.
- Back controls are always visible on secondary views. Home uses one voice pill plus one compact launcher affordance.
- No provider, message send, GPS, camera or audio side effect is inferred from decoration. Existing typed callbacks remain the only execution path.

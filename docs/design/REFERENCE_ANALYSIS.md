# AURA v4 reference analysis

The authoritative asset `design/reference/aura_ui_target.png` was opened directly at its native 1536×1024 resolution on 2026-10-18. It is a six-column by three-row board. Each column is 256 px wide; the three analysis bands are approximately y=0–340, y=341–681 and y=682–1023. The preserved panel crops are in `design/reference/panels/`.

## Visual DNA observed

- A near-black circular dial sits inside a black square frame. A thin steel-blue ring defines the device edge without making the square corners visible.
- Status information uses the upper circular chord: compact connectivity at left, centered time, battery at right.
- The first four panels are not variants of one icon. Home is a translucent sphere, Listening a horizontal membrane/wave, Thinking a hollow particle vortex, and Speaking a sphere crossed by a flowing ribbon.
- Controls use dark navy glass wells, one-pixel cool borders, small colored icon wells, restrained shadow and selective neon bloom. Bright outlines are reserved for the primary action.
- Content-heavy panels remain within the central chord. Rows are roughly 80–88% of the dial width and 13–17% of its height.
- The palette is cyan → royal blue → ultraviolet → magenta on black, with mint for success, amber for weather/warnings and pink for messaging.

## Board geometry

| Row | Board y | Panels | Common dial bounds inside each 256 px cell |
|---|---:|---|---|
| 1 | 0–340 | 01–06 | x≈9–247, y≈4–280 |
| 2 | 341–681 | 07–12 | x≈9–247, y≈344–624 |
| 3 | 682–1023 | 13–18 | x≈9–247, y≈686–958 |

The LVGL adaptation uses a 468 px dial centered in the physical 480×480 square framebuffer, leaving 6 px black corners. Interactive content uses x≈28–452, with tighter central chords for rows and labels.

## Reference-extracted colors

Quantization of a downsampled reference board confirmed that the dominant field is effectively black (`#000000`, `#000204`, `#05080D`) with deep slate/navy surfaces (`#0B1014`, `#101F39`, `#11131D`) and blue-gray text/material tones (`#4C5573`, `#838FA0`). The implementation adds the visually sampled highlight family used by the hero artwork:

| Role | Token |
|---|---|
| OLED | `#000105` |
| Deep navy | `#030712` |
| Glass row | `#0B1424` / `#111D30` / `#182337` |
| Text | `#F8FBFF` / `#BACBE3` / `#8295B0` |
| Cyan / blue | `#25D6FF` / `#178BFF` |
| Indigo / violet / magenta | `#4C52FF` / `#8A45FF` / `#DE58FB` |
| Mint / amber / pink | `#35ECC3` / `#FFCB3F` / `#FF579E` |

## Panel observations

1. Home: orb dominates the upper two-thirds; identity is inside the orb; one mic pill anchors the lower chord.
2. Listening: luminous horizontal waveform with a bright folded center; label and large circular microphone below.
3. Thinking: hollow particle spiral with dark center; compact status and spinner below.
4. Speaking: ribbon crosses a floating glass orb; equalizer and stop control form the lower stack.
5. Quick Actions: symmetrical 2×2 circular icon wells with labels and large breathing room.
6. Notifications: three dense glass rows with source color, title, subtitle and relative time.
7. Aoraton: identity/status header, three message/room rows and a wide compose pill.
8. Navigation: map fills the dial, route and marker are brightest, instruction card floats near the top.
9. Search: large rounded search field over three category rows.
10. Weather: large amber sun and temperature, atmospheric blue-violet lower glow, three metrics.
11. Settings: six compact dark rows with colored round icons and chevrons.
12. Audio: square artwork, title/source, three playback controls and thin volume/progress line.
13. Tools: 2×2 raised rounded tiles with saturated utility icons.
14. Calendar: compact month grid, blue selected date and one lower event card.
15. Camera: photographic preview fills most of the dial; shutter controls sit over the image.
16. Offline: one dignified violet disabled symbol with centered explanatory copy.
17. Battery: large mint circular gauge, percentage, charging state and four quick controls.
18. Voice: four stacked selectable voice rows with violet accents and language/provider cues.

The reference contains conceptual or unsupported features. The implementation labels all fixture data `DEMO`, treats the map as fictional, disables camera capture, does not claim onboard GPS, and does not advertise unsupported Bluetooth audio.

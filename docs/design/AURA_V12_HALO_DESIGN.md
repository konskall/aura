# AURA v12 "Halo" UI redesign

Status: **IMPLEMENTED IN THE SIMULATOR** (`scripts/test.ps1` green, see `docs/VERIFICATION.md`) · firmware compile not run · physical display untested.
Plan: `docs/superpowers/plans/2026-10-22-aura-v12-halo-redesign.md`.
The owner asked for a radical, modern and professional UI for an always-with-me AI companion and delegated the
design decisions (no interview). Everything below is the designer's call unless a constraint from `AGENTS.md`
is quoted.

## Why a redesign

The panel is a 2.16" square AMOLED: 480 × 480 px on a ~38.8 mm side, ~12.4 px/mm (~314 PPI). The v11 UI used
14–18 px text (≈1.1–1.5 mm em) and 48 px targets (≈3.9 mm). That is half of watch/phone legibility (body ≈2.5 mm
em) and below the ~6–7 mm comfortable touch target. The desktop simulator shows the panel ~3.4× larger than the
device, which hid the problem. v11 also chopped every screen into navy glass cards with soft shadows: dense,
generic and expensive to render on an ESP32-S3.

## Principles

1. **One second glance.** Each screen has one job; the most important item is the biggest thing on it.
2. **Watch-class ergonomics.** Minimum text 22 px; reading text 26 px; primary targets 80–104 px; no control
   under 64 px. Lists show three to four rows, not seven.
3. **Black is the canvas.** True black (`#000000`) AMOLED. Content sits on borderless graphite platters. No drop
   shadows and no blur (also the cheapest path for LVGL on the ESP32-S3).
4. **One living element.** The Mercury orb and its halo ring are AURA's presence and the only decoration. The
   state colours are reserved for it: cyan listening, violet thinking, magenta speaking.
5. **Status by exception.** Time is always visible. Wi-Fi/battery appear in full on Home; elsewhere only when
   they need attention (offline, low battery, charging).

## Tokens

| Token | Value | Use |
|---|---|---|
| `canvas` | `#000000` | every background |
| `surface` | `#121822` | platters: rows, cards, chips, keys |
| `surface_high` | `#1B2330` | pressed / raised platter |
| `surface_glass` | `#222C3B` | selected platter |
| `separator` | `#2B3546` | hairlines, inactive ring track |
| `text` | `#F5F8FC` | primary text |
| `text_secondary` | `#A9B5C7` | secondary text |
| `text_tertiary` | `#748197` | captions, hints |
| `ice` / `blue` / `indigo` / `lilac` / `magenta` | unchanged | orb states and interaction |
| `mint` / `amber` / `coral` / `pink` | unchanged | success / warning / danger / alerts |

Radii: platter 28, chip/pill = height/2, keys 14. Spacing: 8 px grid, 24 px side gutter (content width 432).

## Type scale (IBM Plex Sans; Space Grotesk for the wordmark only)

| Role | Font object | Size | Coverage |
|---|---|---|---|
| Clock | `aura_font_plex_light_96` | Light 96 | digits, `:`, `-`, space |
| Display | `aura_font_plex_light_72` | Light 72 | digits, `% + - . / : ° C −`, space |
| Title | `aura_font_plex_semibold_32` | SemiBold 32 | full UI range |
| Headline | `aura_font_plex_medium_28` | Medium 28 | full UI range (row titles, buttons, keys) |
| Body | `aura_font_plex_regular_26` | Regular 26 | full UI range (reading text) |
| Caption | `aura_font_plex_regular_22` | Regular 22 | full UI range (meta, hints) |
| Wordmark | `aura_font_space_medium_26` | Space Grotesk 26 | `A R U` |

Full UI range is unchanged: `0x20-0xFF, 0x300-0x36F, 0x370-0x3FF, 0x2010-0x2026, 0x20AC, 0x2122, 0x2190-0x2193, 0x2212`.
Subset fonts fall back to the title font. The 14/17/18/20/24/40/64 px objects and the Inter A/B font are retired.

## Components

- **Header** (inner screens, 0–96 px): 64 px round back button at (16,16) with a chevron; title (32 px) from
  x=96; time (26 px, secondary) right-aligned at the top right; long press on Back returns Home. An optional 64 px action button replaces
  the time on screens that need one (Wi-Fi rescan, Weather refresh, compose).
- **Row**: 432 × 88, radius 28, platter. Icon 30 px inside a 52 px tinted circle (accent at 22 %), title 28 px,
  optional subtitle 22 px, trailing chevron/switch/badge. Pitch 96 px.
- **Button**: pill, 80 px high. Primary = `blue` fill + white text; Secondary = platter; Danger = `#3A141B` with
  coral text. Labels use the 28 px headline font.
- **Round button**: 88 px transport controls, 64 px header controls; tinted circle, no border, no shadow.
- **Chip**: 64 px pill, icon + 24 px label; used for Home glances and weather city presets.
- **Toast**: top-centred pill, 26 px text, accent hairline; hidden in capture mode.
- **Input**: 72 px high, radius 24, 26 px text, 2 px accent border when focused.
- **Keyboard**: full width (480), 4 rows of ≥64 px keys, 28 px legends, pop-overs on.

## Halo (assistant presence)

The 176 px Mercury orb is drawn at the same place on Home and in every assistant state (centre 240,260), so a
tap on the orb turns it into the listening state without a jump. A 3–10 px ring of radius 96 surrounds it:
dim lilac when idle, cyan following microphone level when listening, a rotating violet segment while thinking,
magenta following the audio envelope while speaking. The ring stays below luminance 145 inside the orb's image
bounds (the Mercury evidence rule) and is updated together with Mercury frames (15 FPS), not at 30 FPS.

## Screens

- **Home**: Wi-Fi glyph (left) and battery (right) at the top; 96 px clock and a 24 px date; the orb (tap =
  talk) with a one-line hint; two 64 px chips: weather glance (icon + temperature + city, or "Καιρός") and
  "Εφαρμογές" (dot when there are notifications). Swipe left/up → apps, swipe down → notifications.
- **Apps**: 3 × 2 grid of 104 px tinted circles with 24 px labels: Συνομιλία, Καιρός, Μηνύματα, Ήχος,
  Ειδοποιήσεις, Ρυθμίσεις. The whole cell (circle + label) is one target.
- **Listening / Thinking / Speaking**: 32 px state title and a caption at the top; orb + halo; controls at the
  bottom (two 80 px pills; one pill; three 88 px round buttons).
- **Answer**: header; the question as a 22 px secondary quote; the answer at 26 px in a scroller; a primary
  "Ρώτα ξανά" pill.
- **Lists** (Settings, Wi-Fi, Notifications, Aoraton rooms, weather results): header + rows.
- **Forms** (Wi-Fi password, city search, compose): header; 72 px input; keyboard. The keyboard's OK key and a
  header action submit.
- **Weather**: header with refresh; scroller with the city line, a 72 px light temperature, condition, high/low,
  the animated scene, metric strip, hazards, hourly strip, detail card and daily strip at the larger sizes.
- **Status**: 220 px battery ring with the 72 px percentage, rows for source and connectivity, mute toggle.
- **Failure** (offline/error/unavailable): 132 px ring icon, 32 px title, 26 px explanation, error code caption,
  two 80 px buttons.

## Non-goals

No new product features beyond navigation gestures and the Home glance; no change to state machines, services,
privacy rules, demo labelling, or the bridge. Firmware is not compiled in this pass (owner request).

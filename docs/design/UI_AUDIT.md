# AURA UI audit — v3 redesign baseline

Audit date: 2026-10-18. The actual shared LVGL implementation, simulator state injection, localization, fonts and all 18 available 480 × 480 captures were inspected. Baselines are preserved under `docs/design/screenshots/before/`.

## Before-state findings

- Home is dominated by a cyan/violet `A` inside rotating arcs. It reads as a logo, not an attentive presence, and consumes space without making the primary interaction obvious.
- Listening, thinking and answer share the same orb, placement and large bordered conversation card. State changes amount mostly to caption changes.
- A permanent four-button footer occupies 58 vertical pixels on every screen. Its detached notification badge visually belongs to Settings rather than Notifications.
- A centered `DEMO` provider pill competes with product identity. Developer/provider context appears globally instead of only where it is useful.
- Cards, notifications, settings, diagnostics, audio and messaging rely on the same outline-card treatment. Hierarchy comes from border color rather than typography, density and spatial rhythm.
- The answer reader has only about 130 useful vertical pixels; long Greek responses are forced into a rigid panel and contextual actions are absent.
- Settings are compressed into one panel. Brightness appears interactive without a platform callback, while diagnostics are over-promoted.
- Greek body text is readable at 18 px, but there is no Greek heading step between body and Latin-only Montserrat display sizes.
- Touch targets are generally adequate, but the Home audio/Aoraton actions are only 38 px tall and several screens have large unused black regions above cramped content.
- Aoraton boundaries are safe and visibly `DEMO`, but bubbles are visually flat and the thread is contained in another large outlined box.

## Redesign priorities

1. Replace the monogram/orbits with a layered, organic, stateful `AIPresence` built from bounded LVGL objects.
2. Remove persistent footer navigation; introduce one Tools launcher, screen headers and explicit Back actions.
3. Give Idle, Wake, Listening, Thinking, Speaking, Answer, Offline and Error distinct compositions.
4. Establish central semantic color, type, spacing, radius, touch and motion tokens.
5. Make answer reading content-first and scrollable; use matte surfaces and separators instead of ubiquitous outlines.
6. Redesign utility screens with one component language while preserving every existing callback and security boundary.
7. Add honest host-loop metrics, genuine PNG galleries, a contact sheet and two full visual review passes.

## Architecture constraints preserved

All product state and widgets remain in `shared/`; SDL, Codex, Windows audio and capture remain in `simulator/`; codec/amplifier code remains in `firmware/`. LVGL remains single-thread owned. No Aoraton production connection or external side effect is introduced.

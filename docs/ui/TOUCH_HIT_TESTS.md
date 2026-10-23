# AURA v6 touch and hit-test verification

## Root cause and repair

The reported label-only behavior was reproduced from the LVGL object tree. `action_tile()` attached its event handler to the parent button, but the round icon well was a default-clickable `lv_obj` above it. Taps on the well terminated at a child with no handler; label taps reached the parent because labels were not clickable.

The systemic repair is:

1. Generic/decorative objects created through `ui::reset()` are non-clickable and non-scrollable.
2. Buttons, rows and tiles explicitly opt into clickability only when they own a real semantic action.
3. Icons, labels, images, halos, tags and switches used as visual children cannot steal input.
4. One 75 ms action guard prevents a rapid second release from firing a newly revealed control at the same coordinate.
5. Service callbacks mutate service/state only. The next UI-loop application renders changes, avoiding deletion of the event target inside its callback.

## Real input path

`aura_simulator --interaction-test` pushes `SDL_MOUSEMOTION`, `SDL_MOUSEBUTTONDOWN` and `SDL_MOUSEBUTTONUP` into SDL. LVGL 9.6's own SDL event timer maps the coordinates through the installed mouse indev and performs the real object hit test. No UI handler is called directly.

The same suite runs at native 1× and at LVGL SDL window zoom 2×. The driver divides desktop coordinates by the configured zoom, proving the rendered and pointer coordinate systems stay aligned.

## Verified matrix

| Area | Evidence |
|---|---|
| Four Quick Action tiles | Icon center, icon edge, label center and icon/label gap for each tile |
| Home conversation action | Icon center, edge, label center and gap |
| Navigation | Back and Home destinations |
| Settings | Provider, reduced motion, Audio, Status, Diagnostics and rapid double tap |
| Notifications | Open mock notification and clear count |
| Audio | Test tone, DEMO WAV, play/pause, stop, mute and real SDL slider drag |
| Aoraton mock | Room, compose, on-screen keyboard, preview, confirm and cancel |
| Assistant | Finish listening, cancel listening/thinking, ask again, retry failure/restore and Home |
| Unavailable feature | Center tap remains inactive and executes no callback |

Latest machine-readable evidence:

- `artifacts/v6/interaction/pointer-1x.json`: 52/52 passed.
- `artifacts/v6/interaction/pointer-2x.json`: 52/52 passed.

The report contains each action ID, tested point, boolean result and observed route/state. Pointer acceptance is part of `scripts/test.ps1`.


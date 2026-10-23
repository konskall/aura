# AURA v8 UI interaction matrix

Machine-readable results are `artifacts/v8/interaction/pointer-1x.json` and `pointer-2x.json`. Both contain 80/80 passing checks over 40 action IDs. Events are injected as SDL mouse/touch input and traverse the real LVGL indev/hit-test route. Direct route calls are used only to reset test preconditions.

| Surface | Actions | Visible target / coverage | Result |
|---|---|---|---|
| Home | ask, launcher | CTA 344×58: icon center, edge, label, gap, outside rejection; launcher 58×58 | PASSED 1×/2× |
| Quick Actions | Assistant, Aoraton, Audio, Notifications, Settings | Four 212×132 composite tiles: icon center, visible edge, label center, gap; settings 48×48 | PASSED 1×/2× |
| Shared navigation | Back, Home | 48×48 controls and stack assertions | PASSED 1×/2× |
| Listening/Thinking/Answer | finish, cancel, ask again | 50–56 px high buttons; cancel during active animation | PASSED 1×/2× |
| Speaking | pause/resume, mute, stop | Three 56×56 controls tied to real audio state | PASSED 1×/2× |
| Notifications | open row, clear | 398×82 composite row at icon/edge/label/gap; clear 190×52 | PASSED 1×/2× |
| Settings | provider, reduced motion, Audio, Status, Diagnostics | Every composite row at icon/edge/label/gap; rapid repeat and leave/reopen persistence | PASSED 1×/2× |
| Audio | tone, WAV, play/pause, stop, mute, volume | Two 194×54 buttons, three 64×64 controls and actual slider drag | PASSED 1×/2× |
| Aoraton | room, compose, prepare, confirm, cancel | Room row at four locations; keyboard/compose/confirm journey | PASSED 1×/2× |
| Scroll | long answer, long chat | Actual SDL press/motion/release; measured LVGL scroll displacement; no compose activation | PASSED 1×/2× |
| Recovery/status | retry, home, mute | Offline failure/restoration, error/home and audio mute | PASSED 1×/2× |
| Unavailable | search review | Center tap produces no route or callback | PASSED 1×/2× |

The semantic action debounce rejects duplicate route execution within 75 ms; the harness includes rapid double-tap coverage. Decorative halos, labels, icons and overlays are non-clickable.

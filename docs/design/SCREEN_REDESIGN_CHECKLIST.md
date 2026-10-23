# Screen redesign checklist

All listed evidence is from the native shared LVGL application at 480 × 480.

| View | Implemented | PNG capture | Review pass 1 | Review pass 2 | Interaction/state check |
|---|---|---|---|---|---|
| Home / Idle / ambient / low battery | yes | yes | passed with fixes | passed | tap callback, Sleep/Wake and battery state |
| Wake | yes | yes | passed | passed | real assistant transition |
| Listening | yes | yes | fixed Back visibility | passed | cancel and finish-listening callbacks |
| Thinking | yes | yes | passed | passed | cancel and network interruption |
| Speaking / PCM | yes | yes | fixed Back visibility | passed | actual SDL PCM envelope, mute and stop |
| Answer short / long | yes | yes | passed | passed | scrollable reader and ask-again |
| Cards | yes | yes | passed | passed | safe `DEMO` provenance; no fake actions |
| Notifications | yes | yes | passed | passed | unread ownership and Aoraton navigation |
| Offline / Error | yes | yes | fixed unsupported glyph | passed | retry and Tools paths |
| Tools / Settings / Diagnostics | yes | yes | fixed clipped Settings row | passed | provider, reduced motion, audio/diagnostics routes |
| Audio | yes | yes | passed | passed | tone/WAV, pause, stop, mute and volume |
| Aoraton rooms/thread | yes | yes | passed | passed | selection/unread/back/compose |
| Aoraton compose/confirm | yes | yes | added Greek keyboard and stable Back | passed | prepare/cancel/explicit confirm |

Regression suite status and blockers are recorded in `docs/VERIFICATION.md`.

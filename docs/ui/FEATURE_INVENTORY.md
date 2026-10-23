# AURA v6 feature inventory

Audit date: 2026-10-18. This inventory is based on the shared C++/LVGL implementation, simulator adapters, core tests, v3/v4 screenshots and the v6 confirmed-defect report. The repository has no commits and all project files are untracked, so the reversible G0 recovery point is `artifacts/recovery/v6-prechange-source-20261018.zip` (SHA-256 `A6702B3FB8FA64C5D8555791943973252492180D9498406D71054C809AD103E8`).

`REAL` means a concrete implementation exists. `SIMULATED` means a deterministic local fixture/service exists and must be disclosed. `NOT AVAILABLE` means no implementation was found. Hardware behavior remains unverified.

| Feature | Was available | Exists now | Trigger | Actual result before v6 | Defect | v6 fix | Test |
|---|---|---|---|---|---|---|---|
| Assistant state machine | Yes | REAL | Home conversation action / control event | Idle → listening → thinking → response/offline/error | Thinking had no visible cancel; simulated microphone status was ambiguous | Keep controller as source of truth, add truthful labels and cancel path | Core state tests + pointer flow |
| Local Codex CLI | Yes | REAL, simulator only | Provider setting + prompt | Bounded loopback provider invokes authenticated CLI | Must remain PC-only and visible as live provider | Preserve bridge boundary and live-state copy | Bridge 8-test suite + live health/smoke |
| Mock provider | Yes | SIMULATED | Provider setting | Deterministic Greek response | DEMO disclosure was inconsistent | Contextual DEMO label only | Simulator flow |
| Answer reader | Yes | REAL | Provider result | Scrollable short/long Greek text | v4 hero artwork could dominate response states | Content-first square reader, no decorative overlay | Short/long capture + scroll route |
| Home orb | Visual only | Partial | Home | One static RGB565 image with opacity modulation | Static poster-like asset, circular bezel and limited motion | Layered breathing scale/halo/parallax motion | Multi-frame pixel-difference evidence |
| Listening visual | Visual only | Partial | Begin recording | Static waveform image | No real motion or amplitude binding; capture absent | Dynamic bars/rings driven by clearly labelled synthetic amplitude in simulator | Multi-frame sequence + interaction flow |
| Thinking visual | Visual only | Partial | Turn started | Static vortex plus rotating small arc | Main artwork did not move and cancel was missing | Rotating/layered bounded motion plus working cancel | Motion sequence + tap during motion |
| Speaking visual | Yes | Partial | Response while PCM plays | Static ribbon plus PCM-reactive small meter | Decoration obscured useful answer context | Bounded PCM-reactive visual and clear stop; text-only response remains reader | Audio sequence + stop test |
| Quick Actions | Yes | Partial | Home launcher button | Four tiles; Search/Navigation/Applications routed to static/fake screens | Icon well intercepted taps; fake destinations | Four verified tiles only: Assistant, Aoraton mock, Audio, Notifications | Four-point SDL taps per tile |
| SDL pointer mapping | Yes | REAL | Mouse/touch | LVGL SDL driver maps 480×480 coordinates | Decorative child objects captured input; scaled mode was not tested | Non-interactive children, one semantic owner, normal and 2× zoom tests | SDL event injection report |
| Notifications | Yes | Partial/SIMULATED | Inject notification | Count changed, but screen showed unrelated meeting/weather fixtures | No dismiss action; fake rows looked enabled | Show disclosed mock notification, open Aoraton, clear/dismiss count | Pointer open/dismiss tests |
| Aoraton | Yes | SIMULATED | Quick Action | Mock rooms/thread/prepare/confirm work | Safe core was preserved; third decorative status row could intercept | Keep only functional room and explicit-confirm routes; label MOCK/DEMO | Core tests + pointer compose/confirm flow |
| Windows audio | Yes | REAL | Audio screen | SDL PCM tone/WAV, pause, stop, volume and mute work | Prev/next icons both invoked the same WAV and pretended to be transport | Explicit Test tone / Demo WAV actions; real pause/stop/mute/slider only | Audio integration flow |
| Firmware audio | Source exists | UNVERIFIED ON HARDWARE | Firmware adapter | ES8311/BSP path implemented | No board verification | Preserve source and label hardware status honestly | Firmware build when IDF exists; hardware checklist |
| Settings | Yes | Partial | Quick Action/header | Provider row worked; Bluetooth/Display/Location routes were unrelated placeholders | Misrouting and fake enabled controls | Provider, reduced motion, Audio, Diagnostics and Status only | Pointer routes + runtime persistence |
| Diagnostics | Yes | REAL host metrics | Settings | Shows LVGL loop timing and device caveats | No animation update metric | Add motion update rate/lifecycle counters | Capture + metric assertion |
| Clock | Yes | REAL host time in simulator | Status bar | Host time updated except deterministic captures | Firmware validity and source were not explicit | Device status model with LIVE/SIM/UNAVAILABLE origins | Unit test + timed frame evidence |
| Battery/connectivity | Yes | SIMULATED in desktop | Status bar/control panel | Values updated but initial 78% looked measured | Unlabelled simulation | Status model labels simulated values `SIM`; unknown remains unavailable | Unit + control-event/capture evidence |
| Search | No working query | NOT AVAILABLE | Former launcher tile | Static category rows, no query/result selection | Fake feature | Remove from enabled launcher; retain only developer unavailable view if directly requested | Disabled/no-route assertion |
| Weather | No provider | NOT AVAILABLE | Former notification/launcher route | Fixed Athens 22°C fixture | Fake live-looking data | Remove enabled route; no weather claim | Disabled/no-route assertion |
| Navigation/maps/GPS | No provider/hardware | NOT AVAILABLE | Former launcher tile | Fictional pasted map and route | Fake navigation | Remove enabled route and map screen from product paths | Disabled/no-route assertion |
| Tools/notes/translate/calculator | No implementations | NOT AVAILABLE | Former Applications tile | Icons routed to unrelated Calendar/Voice/Battery screens | Misleading dead/fake actions | Remove from enabled product UI | Route coverage report |
| Calendar | No provider | NOT AVAILABLE | Former fake tool/notification | Static April 2025 fixture | Fake functionality | Hide from product routes | Route coverage report |
| Camera | No camera | NOT AVAILABLE | Former utility carousel | Disabled shutter over demo photo | Still consumed product navigation | Hide from product routes; developer unavailable state only | Disabled/no-route assertion |
| Voice/TTS choices | No TTS provider | NOT AVAILABLE | Former settings/tools route | Static voice rows | Fake selectable language/voice | Hide choices; retain real audio diagnostics only | Route coverage report |
| Offline/error recovery | Yes | Partial | Provider/network failure | Retry reset state; no Home action | Retry could imply service recovery without checking connectivity | Retry respects current connection status; explicit Home/reset action | Offline failure/restore flow |

## G0 baseline findings

- The display/input backend is LVGL 9.6.0 with SDL2 2.32.10. The simulator has a fixed logical 480×480 display and the SDL driver divides pointer coordinates by its window zoom.
- LVGL is owned by the main thread. Provider work, audio callback and control events publish state back to the UI loop.
- The confirmed tile bug is structural: `make_dot()` creates a default-clickable child over the icon area while the callback is attached only to its parent tile. Labels do not intercept, which explains label-only success.
- The v4 UI creates a 468 px circular `dial`, circular clipping and a bezel on the square panel. This conflicts with v6 and wastes/crops the physical canvas.
- All v4 signature visuals are single RGB565 images. Only opacity, a small arc and the speaking meter change; this is not sufficient motion evidence.
- Current product routes expose seven non-features or misleading routes. They are not counted as existing functionality.


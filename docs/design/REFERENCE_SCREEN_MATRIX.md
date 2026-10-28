# AURA v4 reference screen matrix

| # | Reference position | Composition and controls | Application/backend state | Implemented LVGL view | Edge cases / honesty |
|---:|---|---|---|---|---|
| 01 | r1 c1 | Layered orb, AURA identity, one mic pill | Idle / wake tap | `Home` | Real time/battery; capture fixture is deterministic |
| 02 | r1 c2 | Horizontal neon membrane, privacy line, mic action | Listening / finish | `Assistant: Listening` | Simulator explicitly says voice input is inactive |
| 03 | r1 c3 | Hollow particle vortex, status, spinner | Transcribing / thinking | `Assistant: Thinking` | No fabricated throughput or provider metrics |
| 04 | r1 c4 | Ribbon-orb, PCM meter, stop | Responding + AudioService | `Assistant: Speaking` | Speaking layout only appears for real PLAYING/PAUSED state |
| 05 | r1 c5 | Circular 2×2 launcher | Voice, search, navigation, apps | `Launcher` | Tiles open working views or honest setup screens |
| 06 | r1 c6 | Three glass notification rows | Notification fixtures | `Notifications` | All fixture sources/times say DEMO |
| 07 | r2 c1 | Aoraton room list, status, compose | Mock adapter / room selection | `AoratonRooms` | Room semantics come from real adapter metadata; E2EE boundary unchanged |
| 08 | r2 c2 | Full map, route, marker, setup | Optional location provider | `Navigation` | Fictional DEMO map; no GPS/ETA claim |
| 09 | r2 c3 | Search pill and category rows | Provider-dependent | `Cards` / Search | Provider setup is explicit; no live result fabrication |
| 10 | r2 c4 | Amber sun, 22°C fixture, three metrics | Weather provider placeholder | `Weather` | Every value is marked DEMO |
| 11 | r2 c5 | Six colored settings rows | Provider/audio/navigation/system | `Settings` | Bluetooth row does not claim headphones/LE Audio |
| 12 | r2 c6 | Artwork, player controls, progress, volume | Windows/ESP audio service | `Audio` | Center control drives actual PCM state; WAV/tone callbacks preserved |
| 13 | r3 c1 | Four utility tiles | Tools carousel | `Tools` | Incomplete tools route to existing honest views |
| 14 | r3 c2 | Month grid, selected date, event | Local fixture | `Calendar` | Month/event explicitly marked DEMO |
| 15 | r3 c3 | Demo preview and disabled shutter | Optional camera contract | `Camera` | States that the board has no camera; shutter disabled |
| 16 | r3 c4 | Violet offline symbol, retry | Provider unavailable | `Assistant: Offline` | Retry dispatches the real recovery event |
| 17 | r3 c5 | Mint gauge, charge label, quick controls | Battery snapshot | `Battery` | Runtime estimate omitted; simulator value marked DEMO |
| 18 | r3 c6 | Athina/Nestoras rows, selected profile, preview | Validated TTS profile and provider capability | `VoiceSettings` | Preview is disabled when speech output is unavailable; neural service needs Internet |

Additional retained views: long-answer reader, Aoraton conversation, Greek compose keyboard, explicit send confirmation, diagnostics, error recovery and audio-provider states.

Navigation is explicit through click targets and Back/Home controls. The remaining utility carousel also supports left/right swipe: Tools → Calendar → Camera → Battery → Voice.

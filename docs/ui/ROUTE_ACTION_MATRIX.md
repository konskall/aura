# AURA v6 route and action contract

This is the product action source of truth. Enabled actions must appear in the machine-readable SDL pointer report. Simulator developer controls may enter diagnostic/unavailable views directly, but that does not make them product routes.

| Screen | Visible control | Action ID | Class | Destination / handler | Dependency | Success | Failure | Test |
|---|---|---|---|---|---|---|---|---|
| Home | Conversation card | `home.ask` | EXECUTE | `on_ask` → Assistant/Listening | Assistant controller | Listening UI with truthful source | Error/offline state | Four-point SDL tap |
| Home | Quick Actions | `home.launcher` | NAVIGATE | Launcher, pushes Home | UI navigation stack | Launcher | N/A | Center/edge/label/gap |
| Launcher | Assistant | `launcher.assistant` | EXECUTE | `on_ask` | Assistant controller | Listening | Error/offline | Four-point SDL tap |
| Launcher | Aoraton | `launcher.aoraton` | NAVIGATE | Aoraton rooms | Mock Aoraton service | DEMO rooms | Disconnected state | Four-point SDL tap |
| Launcher | Audio | `launcher.audio` | NAVIGATE | Audio | Audio service | Diagnostics/controls | Audio unavailable state | Four-point SDL tap |
| Launcher | Notifications | `launcher.notifications` | NAVIGATE | Notifications | Assistant notification count | Mock list/empty state | Empty state | Four-point SDL tap |
| Launcher | Settings | `launcher.settings` | NAVIGATE | Settings | Runtime settings | Settings | N/A | Pointer tap |
| All secondary screens | Back | `nav.back` | NAVIGATE | Pop navigation stack | Route stack | Previous view | Home fallback | Pointer tap |
| All secondary screens | Home | `nav.home` | NAVIGATE | Clear stack → Home | Route stack | Home | N/A | Pointer tap |
| Notifications | Mock message row | `notifications.open` | NAVIGATE | Aoraton rooms | Mock Aoraton | Rooms and unread handling | Disconnected state | Four-point SDL tap |
| Notifications | Clear | `notifications.clear` | EXECUTE | `NotificationsCleared` | Assistant controller | Count becomes zero, empty state | N/A | Pointer + state assertion |
| Settings | Appearance row (after Language) | `appearance.settings_row` | NAVIGATE | AppearanceSettings, Back returns to Settings | Shared UI | Four cards; the active look is checked | N/A | Pointer route |
| AppearanceSettings | Look card (Άλως, Μάρμαρο, Σέλας, Γραφίτης) | `appearance.pick` | TOGGLE | `set_appearance` + `on_display_settings` (stored in display.txt) | Display settings store | Whole UI re-renders in the look at once | N/A | Pointer + token assertion |
| Settings | Provider row | `settings.provider` | TOGGLE | Mock ↔ local Codex | Desktop provider adapter | Provider switches | Live request later surfaces bridge error | Pointer + state assertion |
| Settings | Reduced motion | `settings.motion` | TOGGLE | UI motion controller | Shared UI | Persists for runtime | N/A | Pointer + motion counter assertion |
| Settings | Audio | `settings.audio` | NAVIGATE | Audio | Audio service | Audio screen | Unavailable diagnostic | Pointer route |
| Settings | Status | `settings.status` | NAVIGATE | Battery/connectivity status | Device status model | Truthful values/origins | Unknown state | Pointer route |
| Settings | Diagnostics | `settings.diagnostics` | NAVIGATE | Diagnostics | Host metrics | Metrics | Device fields remain unverified | Pointer route |
| Audio | Test tone | `audio.tone` | EXECUTE | `play_test_tone` | Audio service | PCM playback | Error code displayed | Pointer + audio state |
| Audio | Demo WAV | `audio.wav` | EXECUTE | `play_bundled_wav` | Simulator audio | WAV playback | Error code displayed | Pointer + audio state |
| Audio | Play/pause | `audio.play_pause` | EXECUTE | play test / pause / resume | Audio service | State changes once | Error displayed | Rapid-tap test |
| Audio | Stop | `audio.stop` | EXECUTE | stop | Audio service | Ready state | N/A | Pointer + state |
| Audio | Mute | `audio.mute` | TOGGLE | set muted | Audio service | Persistent runtime flag | N/A | Pointer + state |
| Audio | Volume | `audio.volume` | ADJUST | set volume | Audio service | Bounded 0–100% | N/A | Real SDL drag |
| Aoraton rooms | Room row | `aoraton.room.*` | NAVIGATE | Select room → Conversation | Mock service | Thread; unread cleared | Disconnected state | Four-point row tap |
| Aoraton rooms | New message | `aoraton.compose` | NAVIGATE | Compose selected/default room | Mock service | Composer | Disabled if no room | Pointer route |
| Aoraton conversation | Compose | `aoraton.compose` | NAVIGATE | Compose | Mock service | Composer | Disabled if unavailable | Pointer route |
| Aoraton compose | Preview | `aoraton.prepare` | EXECUTE | Prepare exact room/text | Mock service | Confirm screen | Validation stays on compose | Keyboard/pointer flow |
| Aoraton confirm | Confirm | `aoraton.confirm` | EXECUTE | Explicit confirm send | Mock service | Conversation/delivery state | Failure remains visible | Pointer flow |
| Aoraton confirm | Cancel | `aoraton.cancel` | EXECUTE | Cancel pending | Mock service | Conversation | N/A | Pointer flow |
| Listening | Finish | `assistant.finish_listening` | EXECUTE | End recording | Assistant controller | Transcribing/processing | Invalid transition ignored | Pointer flow |
| Listening/Thinking | Cancel | `assistant.cancel` | EXECUTE | Cancel provider/state | Provider/controller | Home/Idle | N/A | Tap during animation |
| Answer | Ask again | `assistant.ask_again` | EXECUTE | New listening state | Assistant controller | Listening | Offline/error | Pointer flow |
| Answer | Home | `nav.home` | NAVIGATE | Reset/return Home | Controller/route stack | Home | N/A | Pointer flow |
| Offline/Error | Retry | `assistant.retry` | EXECUTE | Retry only when connectivity permits | Controller/status | Idle/Home | Remains Offline with reason | Failure/restore test |
| Offline/Error | Home | `nav.home` | NAVIGATE | Reset and clear stack | Controller/route stack | Home | N/A | Pointer flow |
| Status | Mute | `status.mute` | TOGGLE | Real audio mute | Audio service | Flag changes | Audio unavailable shown | Pointer + state |

## Intentionally disabled or hidden

Search, weather, navigation/GPS, generic applications, notes, reminders, translation, calculator, calendar, camera capture, Bluetooth headset audio and alternate voice/TTS choices have no verified implementation. They are absent from enabled product navigation. The former `Not available` review routes for search, navigation and camera were removed on 2026-10-29.

Decorative artwork, icons, labels, halos, cards and status text are `NON_INTERACTIVE`; they must not carry `LV_OBJ_FLAG_CLICKABLE` or intercept their semantic parent.

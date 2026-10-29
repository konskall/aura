# AURA v10 feature and route parity

Baseline audited: 2026-10-18, before v10 layout changes. Runtime UI is shared C++/LVGL 9.6.0 under `shared/ui`; Windows and ESP-IDF platform adapters remain separate. The repository has no committed Git baseline, so the reversible checkpoint is `artifacts/recovery/v10-prechange-source-20261018.zip` (SHA-256 `B21977B0A81087DA8B14083BEB89B15AF283714FA8439EA0658ECCF677735585`).

The supplied seed and v8 directives were reviewed as history. No v9 or v9.1 directive file exists in the repository, implementation kit, or Downloads folder; v10 is therefore applied over the inspected source rather than inferred missing requirements.

## Runtime services that must survive v10

| Capability | Current reality | Boundary / proof |
|---|---|---|
| Assistant state machine | LIVE state logic with deterministic mock provider | `shared/core`; CTest and simulator scenarios |
| Local Codex provider | LIVE on Windows only when the authenticated CLI bridge is selected | Loopback bearer-token bridge; never firmware |
| Audio | LIVE SDL PCM/WAV on Windows; ESP32 ES8311 backend source present | Audio tests plus simulator pointer paths; hardware remains unverified |
| Aoraton | Functional local MOCK/DEMO rooms, chat, compose, confirmation and deterministic result | No production send; explicit confirmation remains mandatory |
| Device status | Host clock plus typed Wi-Fi/BLE/battery status with LIVE/SIMULATED/UNKNOWN origin | No cellular provider or carrier model |
| Notifications | Local DEMO notification with clear/open action and dismiss | Does not claim a production push service |
| Diagnostics | Measured host loop/handler values plus clearly unmeasured device estimates | Host values are not firmware benchmarks |

## Routes and controls

| Route / evidence screen | Entry and enabled controls | Callback / destination | Mode | v10 preservation requirement |
|---|---|---|---|---|
| Home (01) | Ask field; Tools/menu | `on_ask`; Launcher | LIVE/MOCK provider boundary | Preserve assistant start and menu |
| Assistant: Listening (02) | Finish; Cancel | `on_end_listening`; `on_cancel` | Simulated input is labelled DEMO | Preserve state transition and cancellation |
| Assistant: Thinking (03) | Cancel request | `on_cancel` | LIVE or deterministic MOCK | Preserve asynchronous cancellation |
| Assistant: Speaking (04) | Pause/resume; mute; stop | Audio service callbacks | LIVE SDL audio when playing | Preserve envelope-driven state |
| Assistant: Answer (05–06) | New question; scroll long answer | `on_ask`; LVGL scroll | LIVE/MOCK text | Preserve readable, scrollable untrusted text |
| Launcher / Quick actions (07) | Assistant; Aoraton; Audio; Notifications; Settings | Typed `UiView` navigation or `on_ask` | Mixed, truthfully labelled | Whole tile remains one hit target |
| Launcher edit mode | Pencil → pick an app → pick its new place; ✓/Back/swipe ends | `on_launcher_app_move(from, to)`; shared `LauncherLayoutModel` | Persisted on simulator and device | Edit mode never survives leaving the grid |
| Notifications (08) | Open message; clear | AoratonRooms; `on_notifications_clear` | DEMO | Preserve dismiss and destination |
| Settings (09) | Appearance (four looks, 2026-10-29); provider toggle; reduced motion; Audio; Device status; Diagnostics | Typed callbacks/routes | LIVE setting state | Preserve leave/reopen state |
| Audio (10) | Tone; DEMO WAV; pause/resume; stop; mute; volume | Audio service callbacks | LIVE Windows backend | Preserve actual PCM/WAV control |
| Aoraton rooms (11) | Room rows; compose | `on_aoraton_room`; AoratonCompose | MOCK/DEMO | Preserve unread and offline/error cases |
| Aoraton chat (12) | Scroll thread; compose | LVGL scroll; AoratonCompose | MOCK/DEMO | Never forward message text to AI |
| Aoraton compose (13) | Textarea/Greek keyboard; preview | `on_aoraton_prepare`; Confirm route | MOCK/DEMO | Validate draft; no send here |
| Aoraton confirm (14) | Cancel; confirm | `on_aoraton_cancel`; `on_aoraton_confirm` | MOCK/DEMO | Explicit confirmation remains required |
| Offline (15) | Retry; Home | `on_retry`; `on_home` | Truthful connection state | Retry must re-evaluate state |
| Error (16) | Retry; Home | `on_retry`; `on_home` | Real typed error state | Preserve useful recovery path |
| Battery/status (17) | Mute action plus Back/Home | Audio callback; navigation | Origin-labelled data | Never present simulation as measurement |
| Diagnostics (18) | Back/Home; readable measured data | Navigation | LIVE host / unmeasured device | Preserve metric provenance |

## Additional enum routes

The unreachable `Tools` placeholder was removed on 2026-10-28 and the `Cards` (search), `Navigation` and `Camera` placeholders, which only showed "not available", on 2026-10-29 (enum, rendering, captures and control-panel routes), and `Calendar` is the working Organizer. Weather is backed by its validated provider model. `VoiceSettings` is now a working localized screen with persisted Athina/Nestoras profiles and a capability-gated preview callback; the optional host neural adapter and the offline Windows path both reuse the real PCM audio service.

## Event ownership invariants

- LVGL objects are created and mutated only by the UI thread.
- Entire button/tile/row parents own click callbacks; child icon and text objects are non-clickable.
- A 75 ms UI debounce prevents duplicated event delivery.
- Long answer and conversation containers explicitly own scrolling and pointer drag.
- External assistant and Aoraton text is display-only; side effects retain typed callbacks and explicit confirmation.

## Baseline evidence

- Before screens: `artifacts/v10/before/AURA_V10_BEFORE_SCREENS.png`
- Before motion: `artifacts/v10/before/AURA_V10_BEFORE_MOTION.png`
- Supplied critique/reference files: `AURA_Final_v10_Implementation_Kit/references/`

## v10 implementation result

- The 18 production routes remain present and use the same controller/service callbacks audited above.
- Space Grotesk branding and IBM Plex Sans Greek/English UI fonts are shared by simulator and firmware; the Inter comparison is simulator-only.
- All visible production icons resolve through 74 semantic roles. Lucide ISC is the active lawful fallback because no eligible Lordicon exports were supplied.
- Home, Listening, Thinking and Speaking use the same `AuraMercuryView`; input/audio coupling does not alter navigation ownership.
- Offline/Error retain real Retry/Home callbacks and now use dedicated semantic images.
- `TypographyProof` is a simulator-only developer capture route. It is not exposed in product navigation and does not increase the 18-screen product surface.
- Automated SDL/LVGL pointer evidence passes 80/80 cases at 1× and 80/80 at 2×. The 180-transition stress path verifies teardown/re-entry and bounded LVGL allocation.

Final evidence is indexed by `artifacts/v10/evidence-summary.json` and `docs/ui/QA_V10.md`.

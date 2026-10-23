# AURA v10.3 UI audit and feature parity

## Weather addition

v10.3 adds a functional Weather tile to Quick Actions, a default-city route in Settings and weather-specific assistant routing. The ready screen displays live current conditions, humidity, wind, apparent temperature, hourly and daily forecasts. Loading, no-city, Offline, Error, search and result states are separate, localized layouts rather than one fake card. City selection is manual and confirmed; no GPS capability is implied.

All weather visuals live in the shared LVGL layer. Windows network/persistence code remains in `simulator/app`; the UI thread alone owns LVGL. Deterministic evidence is marked `DEMO`, while the live Open-Meteo capture identifies its source. Existing assistant, audio, Aoraton, Settings, simulator and firmware paths remain present.

Evidence and detailed gates are recorded in `WEATHER_QA.md` and `WEATHER_ARCHITECTURE.md`.

Audit date: 2026-10-19 (Europe/Athens). The repository has no committed Git baseline, so the non-destructive recovery point is `artifacts/recovery/v10_2-prechange-source-20261019.zip` (371 entries, 25,587,178 bytes, SHA-256 `483C7C3912D64C34283E9D651A20EA4E7293109153C1464BDAE89D9DF9EEB6A6`). No destructive Git operation was used and the separate `incognitochat` project was not modified.

## Observed v10 baseline defects and disposition

| Defect observed in source/capture | Cause | v10.2 result |
|---|---|---|
| Main Home subtitle was still visible | `render_home()` still rendered `home_subtitle` | Removed from the localization schema and all shared UI source. No replacement tagline was added. |
| Bluetooth was plain `BLE` text | Status fragments were manually positioned labels | Shared grid/flex header now uses the registered Lucide Bluetooth asset. |
| Charging icon floated near the center | Icon had an absolute x-coordinate independent of the battery | Conditional charging icon now participates in the right flex cluster, directly between percentage and battery. |
| Normal battery fill was blue | Status update selected blue for non-charging state | Normal and charging fill are green; only non-charging critical state becomes coral. |
| Home microphone appeared low/protruding | Hand-built shapes inherited 12 px pill padding | Replaced by the semantic Microphone asset inside a centered 42×42 well; the whole 344×58 pill owns one handler. |
| Mercury glow touched the header and states looked too similar | Absolute hero geometry plus one shared color response | Shared safe rectangles move the source below the header; state-specific cyan/violet/rose optical fields are visibly distinct. |

## Core screens plus Weather routes

| # | Screen | Runtime status | Preserved action/service |
|---:|---|---|---|
| 01 | Home | LIVE UI | Full ask pill starts assistant recording; menu opens Quick Actions. |
| 02 | Listening | SIMULATED input on desktop | Finish and Cancel; synthetic input is labelled `DEMO`. |
| 03 | Thinking | LIVE assistant state | Cancel request; only shown while the turn is active. |
| 04 | Speaking | LIVE Windows PCM when playing | Pause/resume, mute and stop; Mercury level follows the audio envelope. |
| 05 | Answer | LIVE/MOCK provider boundary | Read answer and start a new question. |
| 06 | Long Answer | LIVE/MOCK provider boundary | Wrapped, touch-scrollable answer surface. |
| 07 | Quick Actions | MIXED, truth-labelled | Assistant, live Weather, Aoraton mock, Windows audio, notifications and Settings routes. |
| 08 | Notifications | DEMO | Open test notification or clear it. |
| 09 | Settings | LIVE runtime settings | Provider, reduced motion, Weather city, Audio, Device Status and Diagnostics. |
| 10 | Audio | LIVE on Windows | PCM tone/WAV, pause, stop, mute and volume. |
| 11 | Aoraton Rooms | MOCK/DEMO | Select room or compose; no production transport claim. |
| 12 | Aoraton Chat | MOCK/DEMO | Scroll thread and compose. |
| 13 | Compose | MOCK/DEMO | Validated local draft and preview only. |
| 14 | Confirm | MOCK/DEMO | Explicit cancel/confirm remains mandatory. |
| 15 | Offline | REAL typed error state | Retry re-evaluates connectivity; Home exits safely. |
| 16 | Error | REAL typed error state | Retry and Home remain functional. |
| 17 | Battery | SIMULATED on desktop | Origin-labelled power/connectivity state and real mute callback. |
| 18 | Diagnostics | LIVE host / unmeasured device | Host timing and explicit hardware-pending fields. |
| 19 | Weather | LIVE provider / DEMO capture | Current/hourly/daily data, refresh, cached-offline recovery and change city. |
| 20 | Weather Location | LIVE geocoding / DEMO capture | Typed/preset search and explicit result confirmation. |

No assistant, Codex bridge, Windows audio, firmware source, settings, notifications or Aoraton mock capability was removed. Unsupported enum routes remain unavailable rather than appearing as decorative enabled features.

Evidence: `artifacts/v10_2/AURA_V10_2_BEFORE_AFTER.png`, `artifacts/v10_2/AURA_V10_2_PRODUCTION_SCREENS.png`, and `artifacts/v10_2/evidence-summary.json`.

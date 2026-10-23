# Weather architecture v10.3.2

Status: **Windows simulator verified** · **firmware source implemented, compile not run** · **physical device untested**

## Data flow

```text
Quick Action / Settings / assistant intent
                 |
                 v
       shared WeatherModel
       (bounded snapshot/state)
                 |
       typed search/fetch request
                 v
 simulator OpenMeteoWeatherService ---- HTTPS ---- Open-Meteo fallback
                 |
                 +---- HTTP loopback / HTTPS ---- unchanged tlesdetector
                         /api/weather
                         /api/opportunities
                 |
       future polled by UI thread
                 v
       WeatherModel -> AuraUi::apply_weather -> LVGL
                 |
       confirmed city only
                 v
       WeatherStore (location + last valid cache)
```

`shared/core/weather.*` owns platform-independent validation, WMO condition mapping, state transitions and Greek/English weather-intent analysis. `shared/ui` owns both LVGL weather routes and consumes only `WeatherSnapshot`. It performs no network or file I/O.

The Windows adapter in `simulator/app` uses WinHTTP, finite timeouts, a 512 KiB response ceiling and a depth-bounded JSON parser. With `AURA_TLESDETECTOR_URL` configured, it consumes the existing `tlesdetector` `/api/weather` route and then optionally merges `/api/opportunities` alerts, MTG-LI lightning and Xweather CG polarity. No route, source file or environment file is changed in the `tlesdetector` repository. If the server is absent or its weather payload is invalid, AURA falls back to the fixed Open-Meteo HTTPS hosts. HTTP is accepted only for `127.0.0.1`/`localhost` simulator development; remote URLs must be HTTPS.

Work occurs in futures; only the simulator main loop applies completed snapshots to LVGL. Cancellation uses a monotonically increasing generation, so an obsolete response is ignored. Both external payloads are treated as untrusted and are parsed into bounded shared types before display or persistence.

## Location and consent

There is no GPS claim. A city can be entered with the Greek on-screen keyboard, selected from a preset, opened from Settings, or requested through a weather-specific assistant intent. Geocoding returns a bounded five-item selection list. A location becomes the default only after the user taps a concrete result. An assistant question about another city fetches that city for the current result without silently replacing the saved default.

## State and cache policy

The finite states are `Unconfigured`, `Loading`, `Ready`, `Searching`, `SearchResults`, `Offline` and `Error`. Current, hourly and daily data are bounded to 4/4 entries in the model. The 480×480 UI exposes all bounded entries through a clipped vertical content scroller and separate horizontal metric/hour/day strips; the header remains fixed.

The Windows store writes two versioned, typed files below `%LOCALAPPDATA%\AURA\weather-v1` by default. `AURA_WEATHER_STORE` can redirect tests. Location and cache writes are atomic. Every field is length/range checked on load. Cached data is explicitly marked stale/offline; `DEMO` capture data is never persisted and is visibly labelled.

## Provider fields

The enhanced path retains current pressure, gusts, dew point, visibility, UV, cloud, precipitation and European AQI alongside the existing temperature, humidity and wind fields. The opportunities merge retains bounded alert counts/severity, the nearest selected observed storm, age, bearing and MTG-LI flash activity, plus positive CG count/current when Xweather enrichment exists. Missing opportunity data never invalidates an otherwise valid forecast.

The fallback Open-Meteo request keeps current temperature, apparent temperature, relative humidity, WMO weather code, daylight flag and 10 m wind, plus hourly temperature/code/precipitation probability and daily min/max/code/maximum precipitation probability, using `timezone=auto`. Exact WMO codes and bounded 0–100% rain probabilities are retained in the shared snapshot and version-compatible cache so the UI can distinguish intensity without parsing provider text. Geocoding requests Greek display fields. Provider text remains untrusted and passes through the bounded `WeatherLocation` validator before display or persistence.

## Premium UI and motion

The ready route uses a fixed device header, 64 px IBM Plex numeric temperature, condition/day-night hero scene, horizontally scrollable metric and forecast cards, and a tappable forecast detail card. Clear, cloudy, rain, storm, snow, fog, wind and night render from bounded LVGL objects allocated during route construction. The 33 ms UI-thread tick only changes positions, opacity, scale and rotation; it performs no allocation or provider work. Reduced-motion and deterministic capture modes stop weather motion. The weather renderer is independent from the Mercury assistant renderer.

## Firmware boundary

The shared model, snapshots, localization, icons and LVGL routes compile from the same source tree for simulator and firmware. `firmware/components/aura_weather` contains an asynchronous ESP-IDF HTTPS adapter for the same two existing routes. Only the server URL and confirmed city are provisioned in NVS; Open-Meteo, Xweather and other upstream keys stay in the `tlesdetector` server and are never compiled into or provisioned to the ESP32. Responses have a 96 KiB ceiling and are converted to bounded `WeatherFetchResult` values on a worker task. `poll()` is consumed by the firmware composition/UI task, so the network task never touches LVGL. Network access comes from `firmware/components/aura_network`, a Wi-Fi station joined from the on-screen Wi-Fi settings; the SSID/password are stored in NVS namespace `aura_wifi` (`ssid`, optional `pass`) only after a successful join. Requests are refused with truthful `NETWORK_UNAVAILABLE` or `TLESDETECTOR_NOT_CONFIGURED` states instead of being attempted without a link; the saved city is fetched when the station first obtains an IP address.

The firmware source has not been compiled in this shell because an activated ESP-IDF 5.5.5 environment is unavailable. Wi-Fi/TLS, heap/PSRAM use and physical-screen behavior therefore remain unverified hardware work, not inferred results.

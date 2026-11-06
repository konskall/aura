# Architecture

## Goal

AURA uses one portable application core and one LVGL UI on two targets: Windows/SDL2 now and the Waveshare ESP32-S3-Touch-AMOLED-2.16 later.

```text
platform drivers → adapters → services → bounded state snapshots → LVGL UI
                           ↑                              |
                           +--------- typed intents ------+
```

## Layers

- `shared/core`: dependency-free C++17 assistant, audio/radio, Greek calendar, fuel, Aoraton and weather state/services, bounded data models and action safety.
- `shared/ui`: actual LVGL 9.6 screens and widgets. It consumes snapshots and emits intents; it performs no network, file or device I/O.
- `shared/localization`: compiled UTF-8 resources. Greek is the default and English remains available.
- `simulator/app`: SDL2 display/input, Windows audio/radio, opt-in Windows ANCS, event-log adapter, provider workers, WinHTTP weather/fuel and persistence adapters, screenshot capture and host metrics.
- `simulator/controls`: loopback development panel that injects versioned events without pretending to be product UI.
- `bridge`: loopback-only Node service that mediates non-interactive Codex CLI calls, enables Codex web search only for typed explicit-search turns, can opt into Groq through a process-environment secret for development, offers a bounded optional neural-TTS subprocess, and serves a strictly validated local cache of the official EU fuel bulletin.
- `firmware`: ESP-IDF composition root and board/audio adapters built on the official managed Waveshare BSP.

More detailed diagrams and platform boundaries are in [architecture/SYSTEM_OVERVIEW.md](architecture/SYSTEM_OVERVIEW.md) and [architecture/PLATFORM_ABSTRACTION.md](architecture/PLATFORM_ABSTRACTION.md).

## Thread ownership

The simulator main thread is the only LVGL caller. Provider, weather, fuel, radio and ANCS work runs outside it and publishes bounded results that are polled by the main loop. SDL's audio callback owns audio consumption and publishes only atomic/mutex-protected diagnostics. Control, provider, weather, fuel, radio, phone and message state is applied to LVGL from the main loop.

Firmware follows the same rule: its fixed-size audio worker writes codec samples without touching LVGL. UI snapshots are applied only while the display lock is held. LVGL callbacks run in the BSP's LVGL task under that lock, so the `app_main` loop also mutates the shared models (assistant, Aoraton, weather, device status) only while holding it. No background task stores or mutates LVGL object pointers.

The simulator never destroys an unfinished `std::async` future on the UI thread (that would block until the request ends); superseded requests are retired and released once ready, and provider tasks never capture the provider object.

Shared sources must compile under the ESP-IDF 5.5 defaults (`-std=gnu++2b -Wall -Wextra -Werror`); `scripts/check-firmware-flags.ps1` enforces this on Windows. Avoid `u8"…"` literals (they are `char8_t` in C++20) and combine LVGL part/state flags through `lv_style_selector_t`.

## State and confirmation

Every snapshot carries a monotonically increasing `sequence`; `AuraUi::apply_*` compares it before copying anything, so a reset must continue the sequence rather than restart it (`WeatherModel::clear_location`, `MockAoratonService::reset_mock`).

`WifiModel` (shared) validates join requests (SSID 1–32 bytes, WPA/WPA2/WPA3-personal passphrase 8–63 printable ASCII or a 64-digit PSK; WEP/enterprise unsupported), normalizes untrusted scan results (display-safe UTF-8, no bidi overrides, deduplicated, bounded to 12) and never holds a password. The simulator adapter scans the host adapter read-only; the firmware `Esp32Network` scans, joins, saves credentials to NVS only after an IP is obtained (driver flash storage disabled), falls back to the saved network on failure, reconnects with exponential back-off and starts SNTP (Europe/Athens).

`AssistantState` explicitly models the assistant lifecycle and rejects stale generations after cancellation. `AudioStateMachine` models readiness, buffering, playback, pause, stop and failure. `MockAoratonService` remains an explicit DEMO adapter. `RealAoratonService` models account pairing separately from per-room key possession and exchanges typed work with a platform worker through a bounded mailbox; the platform worker cannot mutate LVGL. `WeatherModel` owns bounded location/search/current/hourly/daily state, a validated 15/30/60/180-minute refresh preference and a valid cache during offline recovery. Platform composition roots persist that preference (`.settings` in the simulator, NVS on ESP32) and schedule refresh work without putting I/O in the shared UI; see [WEATHER_ARCHITECTURE.md](WEATHER_ARCHITECTURE.md) and [aoraton/LIVE_ARCHITECTURE.md](aoraton/LIVE_ARCHITECTURE.md).

Assistant token streaming follows the same ownership rule. A provider worker writes untrusted deltas into the bounded `AssistantStreamMailbox`; the owner thread consumes the latest cumulative snapshot and alone dispatches `PartialResult`. `AssistantStreamDecoder` validates the NDJSON wire format shared by the simulator and the future ESP-IDF gateway. A completed first sentence may enter the bounded speech queue early, while final text remains authoritative for memory and action confirmation. See [ADR 0003](ADR/0003-streaming-ai-gateway-and-device-secrets.md).

Speech profiles and their persisted selection live in `shared/core`; provider names do not. The simulator bridge maps the validated Athina/Nestoras profile to a language-specific neural voice, returns bounded MP3, and `BridgeSpeechService` decodes it through the common `minimp3` path before the SDL backend receives PCM. The bridge can use the official Azure Speech REST API or the optional host-only Edge Python adapter; both reuse the same shared UI, profile and decoder path. Firmware will consume the same profile through a future authenticated gateway and never receives an upstream provider key. See [ADR 0004](ADR/0004-neural-speech-simulator-adapter.md) and [ADR 0005](ADR/0005-official-azure-speech-simulator-provider.md).

System power actions remain typed platform intents. Restart, power-off and factory reset first enter a shared LVGL confirmation scene; only an explicit confirmation invokes the platform callback. Their procedural AI core, boot progress and low-power wake affordance contain no platform I/O and are animated only by the UI thread.

`RadioModel` and the common CC0 `minimp3` decoder keep station/search/favourite/playback state portable; platform adapters own HTTP and PCM output. One stream's work is shared too (`radio_pump`): it splits off the ICY titles, decodes the MP3 and feeds the platform's output between two water marks. `WindowsRadioService` and the device's `Esp32RadioService` (raw `esp_transport`, Shoutcast's "ICY 200 OK", up to three redirects) both use it. minimp3 keeps its 16 KB per-frame scratch on the heap (`MINIMP3_EXTERNAL_SCRATCH`), so decoding fits the device's small task stacks; the PC suite and the firmware build both check this. `greek_calendar` computes Orthodox Easter, holidays and the bounded offline nameday table without network access. `FuelPriceModel` accepts only the exact Greece/European Commission schema; the simulator fetches it from the authenticated loopback bridge, while the device transport remains a future cloud-gateway adapter. The Windows ANCS client reuses the shared packet codec, keeps notification text in memory, and publishes a bounded 64-event queue; pairing and OS bond removal stay visible Windows actions. See [phone/IPHONE_WINDOWS_ANCS.md](phone/IPHONE_WINDOWS_ANCS.md).

Provider and future network text is untrusted display data. Typed cards are created by trusted code. Assistant side effects and Aoraton sends cannot execute directly from generated text: the user must see the destination and exact content and explicitly confirm.

## Aoraton account realtime

Aoraton account realtime uses the optional shared `IAoratonRealtime` boundary. The Windows WinHTTP adapter owns its socket and bounded invalidation state; the existing account worker performs immediate REST reconciliation and alone publishes mailbox events. JWT/room changes replace the subscription generation. A separately configurable periodic check (off, or 10/30/60/300 seconds) supplements realtime; token renewal and connection recovery remain independent of that timer. Preferences use shared typed validation and local platform storage. UI realtime state reflects acknowledged subscriptions, never merely an open socket. See [ADR 0007](ADR/0007-aoraton-client-realtime.md).

## Message time formatting

Message timestamps stay unchanged in the service model for ordering and deduplication. The shared formatter parses explicit ISO UTC offsets into absolute instants and calls the platform `on_local_time` converter for the message's date. Windows uses its dynamic timezone database; firmware uses the already configured local TZ rules. Both bubble metadata and the full-message reader use the same formatter. If conversion is unavailable, the localized UI labels the UTC fallback explicitly.

## Local photos

`PhotoModel` owns the catalog, selection, slideshow and ambient-photo preference in shared code. `PhotoWorker` reads, decodes and performs confirmed exact-name deletion only on a platform worker through bounded mailboxes; it never calls LVGL. The Windows adapter uses a local `photos` directory beside the companion stores, rejects redirected directories/files and reads Unicode filenames through Windows handles. Firmware uses `/sdcard/aura/photos` on its existing storage task. The separate authenticated transfer worker can add/delete files; the owner thread reconciles completed revisions. Accepted settings are flushed before normal shutdown, and reset preserves the photo directory. Deleting the selected ambient file clears both its cached image and persisted preference.

The catalog is limited to 128 files (up to 2,048 directory entries examined), and the UI renders eight file rows per page. JPG/JPEG/PNG files are bounded to 4 MiB. Baseline JPEGs up to 40 megapixels decode in MCU blocks with integer downscaling into at most 480x480 RGB565. PNG and progressive JPEG expansion is capped at 1,048,576 pixels and uses a separate 4 MiB allocation budget; actual free memory can impose a smaller limit. Large input/output buffers allocate without exceptions and fail visibly when unavailable. The same pinned TJpgDec/stb_image code runs on both platforms. EXIF orientations 1-8 are applied locally; alpha is composited over black.

One decoded image is displayed and at most one selected ambient image is retained independently while browsing. LVGL source/header caches are invalidated on the UI thread before source replacement. A slideshow runs only in the viewer, holds the display awake, and stops when another view opens. Ambient uses a static dimmed image, the existing clock and pixel shifting; it does no continuous SD reads. Photo names/pixels never enter assistant or bridge requests. Owner-initiated local uploads use the isolated temporary service below.

## Local file transfer

The shared `TransferSession` owns bounded, mutex-protected session state and exports snapshots; only the UI thread renders them. Enable creates a ten-minute memory-only PIN/bearer lease. Native Winsock and ESP-IDF HTTP adapters stream validated owner uploads through `TransferProtocol` into photos/files staging adapters. Neither the protocol nor platform workers call LVGL. Authenticated lists are bounded to 128 ordinary files per cursor page; exact-name deletion requires explicit confirmation. There is no download endpoint. Atomic non-replacing commits notify the photo catalog through a revision consumed by the owner thread. Uploads leave a visible viewer undisturbed; external photo deletion returns it to the refreshed gallery and reconciles the ambient preference. Windows uses an exclusive staging lock across app instances, a bounded graceful drain for rejected request bodies, and a separate explicit browser action. The localized browser prepares larger photos at 480x480 without changing the original. The AI bridge keeps its loopback-only listener. See [ADR 0008](ADR/0008-local-file-transfer.md).

## Memory policy

The simulator renders at 480 × 480. Firmware uses partial LVGL draw buffers rather than assuming a permanent full RGB565 framebuffer. Text, history, diagnostics, Aoraton messages and audio input are bounded. Codec writes use fixed 512-sample chunks; see [architecture/MEMORY_BUDGET.md](architecture/MEMORY_BUDGET.md).

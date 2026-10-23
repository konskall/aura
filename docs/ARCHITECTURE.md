# Architecture

## Goal

AURA uses one portable application core and one LVGL UI on two targets: Windows/SDL2 now and the Waveshare ESP32-S3-Touch-AMOLED-2.16 later.

```text
platform drivers → adapters → services → bounded state snapshots → LVGL UI
                           ↑                              |
                           +--------- typed intents ------+
```

## Layers

- `shared/core`: dependency-free C++17 assistant, audio, Aoraton and weather state/services, bounded data models and action safety.
- `shared/ui`: actual LVGL 9.6 screens and widgets. It consumes snapshots and emits intents; it performs no network, file or device I/O.
- `shared/localization`: compiled UTF-8 resources. Greek is the default and English remains available.
- `simulator/app`: SDL2 display/input, Windows audio, event-log adapter, provider workers, Open-Meteo/WinHTTP weather and persistence adapters, screenshot capture and host metrics.
- `simulator/controls`: loopback development panel that injects versioned events without pretending to be product UI.
- `bridge`: loopback-only Node service that mediates non-interactive Codex CLI calls.
- `firmware`: ESP-IDF composition root and board/audio adapters built on the official managed Waveshare BSP.

More detailed diagrams and platform boundaries are in [architecture/SYSTEM_OVERVIEW.md](architecture/SYSTEM_OVERVIEW.md) and [architecture/PLATFORM_ABSTRACTION.md](architecture/PLATFORM_ABSTRACTION.md).

## Thread ownership

The simulator main thread is the only LVGL caller. Provider and weather requests run outside it and publish bounded results that are polled by the main loop. SDL's audio callback owns audio consumption and publishes only atomic/mutex-protected diagnostics. Control, provider, weather, audio and mock-message state is applied to LVGL from the main loop.

Firmware follows the same rule: its fixed-size audio worker writes codec samples without touching LVGL. UI snapshots are applied only while the display lock is held. LVGL callbacks run in the BSP's LVGL task under that lock, so the `app_main` loop also mutates the shared models (assistant, Aoraton, weather, device status) only while holding it. No background task stores or mutates LVGL object pointers.

The simulator never destroys an unfinished `std::async` future on the UI thread (that would block until the request ends); superseded requests are retired and released once ready, and provider tasks never capture the provider object.

Shared sources must compile under the ESP-IDF 5.5 defaults (`-std=gnu++2b -Wall -Wextra -Werror`); `scripts/check-firmware-flags.ps1` enforces this on Windows. Avoid `u8"…"` literals (they are `char8_t` in C++20) and combine LVGL part/state flags through `lv_style_selector_t`.

## State and confirmation

Every snapshot carries a monotonically increasing `sequence`; `AuraUi::apply_*` compares it before copying anything, so a reset must continue the sequence rather than restart it (`WeatherModel::clear_location`, `MockAoratonService::reset_mock`).

`WifiModel` (shared) validates join requests (SSID 1–32 bytes, WPA/WPA2/WPA3-personal passphrase 8–63 printable ASCII or a 64-digit PSK; WEP/enterprise unsupported), normalizes untrusted scan results (display-safe UTF-8, no bidi overrides, deduplicated, bounded to 12) and never holds a password. The simulator adapter scans the host adapter read-only; the firmware `Esp32Network` scans, joins, saves credentials to NVS only after an IP is obtained (driver flash storage disabled), falls back to the saved network on failure, reconnects with exponential back-off and starts SNTP (Europe/Athens).

`AssistantState` explicitly models the assistant lifecycle and rejects stale generations after cancellation. `AudioStateMachine` models readiness, buffering, playback, pause, stop and failure. `MockAoratonService` remains an explicit DEMO adapter. `RealAoratonService` models account pairing separately from per-room key possession and exchanges typed work with a platform worker through a bounded mailbox; the platform worker cannot mutate LVGL. `WeatherModel` owns bounded location/search/current/hourly/daily state and preserves a valid cache during offline recovery; see [WEATHER_ARCHITECTURE.md](WEATHER_ARCHITECTURE.md) and [aoraton/LIVE_ARCHITECTURE.md](aoraton/LIVE_ARCHITECTURE.md).

Provider and future network text is untrusted display data. Typed cards are created by trusted code. Assistant side effects and Aoraton sends cannot execute directly from generated text: the user must see the destination and exact content and explicitly confirm.

## Memory policy

The simulator renders at 480 × 480. Firmware uses partial LVGL draw buffers rather than assuming a permanent full RGB565 framebuffer. Text, history, diagnostics, Aoraton messages and audio input are bounded. Codec writes use fixed 512-sample chunks; see [architecture/MEMORY_BUDGET.md](architecture/MEMORY_BUDGET.md).

#include "aura/assistant_state.hpp"
#include "aura/aoraton.hpp"
#include "aura/aura_ui.hpp"
#include "aura_board_profile.h"
#include "esp32_audio_backend.hpp"
#include "esp32_companion.hpp"
#include "esp32_network.hpp"
#include "esp32_weather_backend.hpp"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>

namespace {
constexpr const char* kTag = "aura";

}

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "AURA boot: Waveshare ESP32-S3-Touch-AMOLED-2.16 profile %s",
             aura_board::profile_name);
    ESP_LOGW(kTag, "Hardware behavior is unverified until physical board bring-up");

    // NVS holds Wi-Fi credentials, the weather city, and an optional enhanced
    // tlesdetector endpoint. Core weather uses Open-Meteo directly when absent.
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS layout unreadable (%s); erasing", esp_err_to_name(nvs));
        nvs_flash_erase();
        nvs = nvs_flash_init();
    }
    if (nvs != ESP_OK) ESP_LOGE(kTag, "NVS unavailable: %s", esp_err_to_name(nvs));

    lv_display_t* display = bsp_display_start();
    if (display == nullptr) {
        ESP_LOGE(kTag, "BSP display initialization failed");
        return;
    }

    static aura::AssistantController controller;
    static aura::DeviceStatusModel device_status;
    static aura::MockAoratonService aoraton;
    static aura::WeatherModel weather;
    static aura::firmware::Esp32AudioBackend audio;
    static aura::firmware::Esp32WeatherBackend weather_backend;
    static aura::firmware::Esp32Network network;
    static aura::WifiModel wifi;
    static aura::AuraUi ui;
    static aura::firmware::Esp32Companion companion(audio, device_status);
    const bool audio_ready = audio.initialize();
    const bool weather_backend_ready = weather_backend.initialize();
    if (weather_backend_ready) weather.set_refresh_interval(weather_backend.refresh_interval());
    const bool network_started = network.start();
    ESP_LOGI(kTag, "Audio backend initialization: %s", audio_ready ? "ready" : "unavailable");
    ESP_LOGI(kTag, "Weather backend initialization: %s",
             weather_backend_ready && weather_backend.configured() ? "ready" : "unavailable");
    ESP_LOGI(kTag, "Network: %s", network_started ? "Wi-Fi radio started" : "radio unavailable");
    companion.start();
    if (network_started) {
        wifi.set_radio_available(true);
        wifi.set_saved_network(network.saved_ssid());
    } else {
        wifi.set_radio_available(false, "RADIO_UNAVAILABLE");
        device_status.set_connection(false, aura::StatusOrigin::Live, 0);
    }
    // The clock stays "unknown" until SNTP sets real time after a connection.
    device_status.set_clock_unavailable();

    // Starts a fetch or records why it cannot run; never claims data it lacks.
    const auto request_weather = [](const aura::WeatherLocation& location) {
        if (!weather_backend.configured()) {
            weather.fetch_failed("WEATHER_BACKEND_UNAVAILABLE", false);
        } else if (!network.connected()) {
            weather.fetch_failed("NETWORK_UNAVAILABLE", true);
        } else if (!weather_backend.request(location)) {
            weather.fetch_failed("WEATHER_BUSY", false);
        }
    };
    // The saved city is fetched once the station first obtains an IP address.
    bool boot_weather_pending = false;
    std::uint32_t weather_refresh_ms = 0;
    if (weather_backend_ready && weather_backend.configured()) {
        if (const auto location = weather_backend.saved_location()) {
            weather.restore_location(*location);
            boot_weather_pending = true;
        }
    }

    // LVGL callbacks run in the BSP's LVGL task while it holds the display lock;
    // the loop below touches the same models only under that lock as well.
    if (bsp_display_lock(0)) {
        aura::AuraUiCallbacks callbacks;
        callbacks.on_home = [] { controller.dispatch({aura::EventType::Reset, {}, 0}); };
        callbacks.on_notifications_clear = [] { controller.dispatch({aura::EventType::NotificationsCleared, {}, 0}); };
        callbacks.on_audio_test = [] { audio.play_test_tone(); };
        callbacks.on_audio_wav = [] { audio.play_wav("/sdcard/aura/test.wav"); };
        callbacks.on_audio_pause_resume = [] {
            if (audio.diagnostics().state == aura::AudioState::Paused) audio.resume();
            else audio.pause();
        };
        callbacks.on_audio_stop = [] { audio.stop(); };
        callbacks.on_audio_muted = [](bool muted) { audio.set_muted(muted); };
        callbacks.on_audio_volume = [](float volume) { audio.set_volume(volume); };
        callbacks.on_aoraton_room = [](const std::string& room_id) { return aoraton.select_room(room_id); };
        callbacks.on_aoraton_prepare = [](const std::string& room_id, const std::string& text) {
            return aoraton.prepare_send(room_id, text);
        };
        callbacks.on_aoraton_confirm = [] { return aoraton.confirm_send(); };
        callbacks.on_aoraton_cancel = [] { aoraton.cancel_send(); };
        callbacks.on_weather_open = [request_weather, &weather_refresh_ms] {
            if (!weather_backend.configured() || weather.snapshot().state != aura::WeatherState::Unconfigured) return;
            if (const auto location = weather_backend.saved_location()) {
                weather.begin_fetch(*location, true);
                weather_refresh_ms = 0;
                request_weather(*location);
                ui.apply_weather(weather.snapshot());
            }
        };
        callbacks.on_weather_refresh = [request_weather, &weather_refresh_ms] {
            const auto& snapshot = weather.snapshot();
            if (!weather_backend.configured()) {
                weather.fetch_failed("WEATHER_BACKEND_UNAVAILABLE", false);
            } else if (snapshot.has_data || snapshot.has_saved_location) {
                const auto location = snapshot.has_data ? snapshot.data.location : snapshot.saved_location;
                weather.begin_fetch(location, false);
                weather_refresh_ms = 0;
                request_weather(location);
            }
            ui.apply_weather(weather.snapshot());
        };
        callbacks.on_weather_refresh_interval = [&weather_refresh_ms](aura::WeatherRefreshInterval interval) {
            if (!weather.set_refresh_interval(interval)) return;
            weather_refresh_ms = 0;
            weather_backend.save_refresh_interval(interval);
            ui.apply_weather(weather.snapshot());
        };
        callbacks.on_weather_search = [](const std::string& query) {
            if (!weather.begin_search(query)) return;
            if (!network.connected()) {
                weather.search_failed("NETWORK_UNAVAILABLE", true);
            } else if (!weather_backend.request_search(weather.snapshot().search_query, companion.language())) {
                weather.search_failed("WEATHER_BUSY", false);
            }
            ui.apply_weather(weather.snapshot());
        };
        callbacks.on_weather_select_result = [request_weather, &weather_refresh_ms](std::size_t index) {
            const auto& snapshot = weather.snapshot();
            if (index >= snapshot.suggestion_count) return;
            const auto location = snapshot.suggestions[index];
            weather.begin_fetch(location, true);
            weather_refresh_ms = 0;
            weather_backend.save_location(location);
            request_weather(location);
            ui.apply_weather(weather.snapshot());
        };
        // Wi-Fi: requests are validated by the shared model, then handed to the
        // station; results come back through Esp32Network::poll() below.
        callbacks.on_wifi_open = [] {
            if (!wifi.snapshot().scanning && wifi.begin_scan()) network.request_scan();
        };
        callbacks.on_wifi_scan = [] {
            if (wifi.begin_scan()) network.request_scan();
        };
        callbacks.on_wifi_connect = [](const std::string& ssid, aura::WifiSecurity security,
                                       const std::string& password) {
            if (!wifi.begin_connect(ssid, security, password)) return false;
            if (!network.request_connect(ssid, security, password)) {
                wifi.connect_failed("CONNECT_FAILED");
                return false;
            }
            return true;
        };
        callbacks.on_wifi_cancel = [] {
            wifi.cancel_connect();
            network.cancel_connect();
        };
        callbacks.on_wifi_forget = [] {
            network.forget();
            wifi.forget_saved_network();
        };
        companion.install(callbacks);
        ui.set_language(companion.language());
        ui.set_appearance(companion.appearance());
        ui.create(display, std::move(callbacks));
        companion.apply(ui);
        ui.apply(controller.snapshot());
        ui.apply_status(device_status.snapshot());
        ui.apply_audio(audio.diagnostics());
        ui.apply_aoraton(aoraton.snapshot());
        ui.apply_weather(weather.snapshot());
        ui.apply_wifi(wifi.snapshot());
        bsp_display_unlock();
    }

    TickType_t previous = xTaskGetTickCount();
    std::uint32_t unapplied_ms = 0;
    std::uint32_t status_ms = 1000;
    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        unapplied_ms += static_cast<std::uint32_t>((now - previous) * portTICK_PERIOD_MS);
        previous = now;
        if (bsp_display_lock(pdMS_TO_TICKS(25))) {
            const std::uint32_t elapsed_ms = std::min<std::uint32_t>(unapplied_ms, 1000);
            unapplied_ms = 0;
            audio.tick(elapsed_ms);
            aoraton.tick(elapsed_ms);
            companion.poll(elapsed_ms);
            aura::WeatherFetchResult weather_result;
            if (weather_backend.poll(weather_result)) {
                if (weather_result.ok) weather.fetch_succeeded(weather_result.data);
                else weather.fetch_failed(weather_result.error_code, weather_result.offline);
            }
            aura::WeatherGeocodingResult geocoding_result;
            if (weather_backend.poll_search(geocoding_result)) {
                if (geocoding_result.ok) weather.search_succeeded(geocoding_result.locations);
                else weather.search_failed(geocoding_result.error_code, geocoding_result.offline);
            }
            aura::firmware::WifiEvent wifi_event;
            while (network.poll(wifi_event)) {
                switch (wifi_event.kind) {
                    case aura::firmware::WifiEvent::Kind::ScanDone:
                        wifi.scan_succeeded(wifi_event.networks, aura::WifiOrigin::Live);
                        break;
                    case aura::firmware::WifiEvent::Kind::ScanFailed:
                        wifi.scan_failed(wifi_event.error_code);
                        break;
                    case aura::firmware::WifiEvent::Kind::ConnectSucceeded:
                        wifi.connect_succeeded(wifi_event.ssid, wifi_event.ip_address, aura::WifiOrigin::Live);
                        break;
                    case aura::firmware::WifiEvent::Kind::ConnectFailed:
                        wifi.connect_failed(wifi_event.error_code);
                        break;
                    case aura::firmware::WifiEvent::Kind::LinkChanged:
                        wifi.set_link(wifi_event.connected, wifi_event.ssid, wifi_event.rssi_dbm,
                                      wifi_event.ip_address, aura::WifiOrigin::Live);
                        break;
                }
            }
            status_ms += elapsed_ms;
            weather_refresh_ms = std::min<std::uint32_t>(weather_refresh_ms + elapsed_ms, 24U * 60U * 60U * 1000U);
            if (status_ms >= 1000) {
                status_ms = 0;
                if (network_started) {
                    const bool online = network.connected();
                    device_status.set_connection(online, aura::StatusOrigin::Live, network.signal_level());
                    if (controller.snapshot().wifi_connected != online) {
                        controller.dispatch({aura::EventType::WifiChanged, {}, online ? 1 : 0});
                    }
                    if (online && boot_weather_pending) {
                        boot_weather_pending = false;
                        const auto location = weather.snapshot().saved_location;
                        weather.begin_fetch(location, true);
                        weather_refresh_ms = 0;
                        request_weather(location);
                    }
                    const auto& weather_snapshot = weather.snapshot();
                    const auto refresh_due_ms = aura::weather_refresh_interval_minutes(
                        weather_snapshot.refresh_interval) * 60U * 1000U;
                    if (online && weather_refresh_ms >= refresh_due_ms && weather_snapshot.has_saved_location &&
                        weather_snapshot.state != aura::WeatherState::Loading &&
                        weather_snapshot.state != aura::WeatherState::Searching) {
                        weather_refresh_ms = 0;
                        weather.begin_fetch(weather_snapshot.saved_location, false);
                        request_weather(weather_snapshot.saved_location);
                    }
                }
                if (network.time_valid()) {
                    const std::time_t now_time = std::time(nullptr);
                    std::tm local_time{};
                    localtime_r(&now_time, &local_time);
                    device_status.set_clock(local_time.tm_hour, local_time.tm_min, aura::StatusOrigin::Live);
                    device_status.set_date(local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
                                           local_time.tm_wday, aura::StatusOrigin::Live);
                    companion.set_clock(local_time);
                }
            }
            ui.tick(elapsed_ms);
            ui.apply(controller.snapshot());
            ui.apply_status(device_status.snapshot());
            ui.apply_audio(audio.diagnostics());
            ui.apply_aoraton(aoraton.snapshot());
            ui.apply_weather(weather.snapshot());
            ui.apply_wifi(wifi.snapshot());
            companion.apply(ui);
            companion.confirm_image_after(static_cast<std::uint32_t>(esp_timer_get_time() / 1000));
            bsp_display_unlock();
        }
        // Confirmed power actions can block, restart or enter deep sleep. They
        // must never execute while the single LVGL/BSP display lock is held.
        companion.run_deferred_system_action();
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

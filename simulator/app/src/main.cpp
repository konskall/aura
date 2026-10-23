#include "aura/assistant_state.hpp"
#include "aura/aoraton.hpp"
#include "aura/aoraton_real.hpp"
#include "aura/aura_ui.hpp"
#include "aura/bridge_speech_service.hpp"
#include "aura/companion_runtime.hpp"
#include "aura/control_events.hpp"
#include "aura/device_status.hpp"
#include "aura/fail_closed_aoraton_transport.hpp"
#include "aura/local_codex_provider.hpp"
#include "aura/mock_provider.hpp"
#include "aura/open_meteo_weather_service.hpp"
#include "aura/provider.hpp"
#include "aura/screenshot.hpp"
#include "aura/strings.hpp"
#include "aura/weather.hpp"
#include "aura/weather_store.hpp"
#include "aura/wifi.hpp"
#include "aura/windows_audio_backend.hpp"
#include "aura/windows_wifi_service.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <future>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#endif

#include "SDL.h"
#include "lvgl.h"

namespace {
std::vector<std::string> utf8_command_line_arguments(int argc, char** argv) {
#ifdef _WIN32
    int wide_count = 0;
    auto** wide_arguments = CommandLineToArgvW(GetCommandLineW(), &wide_count);
    if (wide_arguments) {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(wide_count));
        for (int index = 0; index < wide_count; ++index) {
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_arguments[index], -1,
                                                 nullptr, 0, nullptr, nullptr);
            if (size <= 0) {
                arguments.clear();
                break;
            }
            std::string value(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_arguments[index], -1,
                                value.data(), size, nullptr, nullptr);
            value.pop_back();
            arguments.push_back(std::move(value));
        }
        LocalFree(wide_arguments);
        if (!arguments.empty()) return arguments;
    }
#endif
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) arguments.emplace_back(argv[index]);
    return arguments;
}

const char* environment(const char* name, const char* fallback = "") {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

int integer(const std::string& value, int fallback = 0) {
    try { return std::stoi(value); } catch (...) { return fallback; }
}

std::string aoraton_client_message_id() {
    std::array<std::uint8_t, 16> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
#else
    return {};
#endif
    // RFC 4122 variant/version bits make diagnostics recognizable while the
    // randomness still comes from the operating-system CSPRNG.
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "aura-";
    result.reserve(41);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) result.push_back('-');
        result.push_back(hex[bytes[index] >> 4]);
        result.push_back(hex[bytes[index] & 0x0fU]);
    }
    return result;
}

aura::WeatherLocation demo_weather_location(const std::string& name = "Αθήνα") {
    if (name == "Σπάρτη") return {name, "Πελοπόννησος", "Ελλάδα", "GR", "Europe/Athens", 37.0733, 22.4297};
    if (name == "Θεσσαλονίκη") return {name, "Κεντρική Μακεδονία", "Ελλάδα", "GR", "Europe/Athens", 40.6401, 22.9444};
    if (name == "Πάτρα") return {name, "Δυτική Ελλάδα", "Ελλάδα", "GR", "Europe/Athens", 38.2466, 21.7346};
    return {name, "Αττική", "Ελλάδα", "GR", "Europe/Athens", 37.9838, 23.7275};
}

aura::WeatherData demo_weather_data(aura::WeatherCondition condition = aura::WeatherCondition::PartlyCloudy,
                                     const std::string& city = "Αθήνα") {
    aura::WeatherData data;
    data.location = demo_weather_location(city);
    data.temperature_c = condition == aura::WeatherCondition::Rain ? 17.0F : 22.0F;
    data.apparent_temperature_c = condition == aura::WeatherCondition::Rain ? 16.0F : 24.0F;
    data.wind_kmh = condition == aura::WeatherCondition::Windy ? 46.0F : 12.0F;
    data.humidity_percent = condition == aura::WeatherCondition::Rain ? 82 : 45;
    data.daily_minimum_c = condition == aura::WeatherCondition::Rain ? 14.0F : 17.0F;
    data.daily_maximum_c = condition == aura::WeatherCondition::Rain ? 19.0F : 25.0F;
    data.condition = condition;
    const auto demo_code = [](aura::WeatherCondition value) {
        switch (value) {
            case aura::WeatherCondition::Clear: return 0;
            case aura::WeatherCondition::PartlyCloudy: return 2;
            case aura::WeatherCondition::Cloudy: return 3;
            case aura::WeatherCondition::Fog: return 45;
            case aura::WeatherCondition::Drizzle: return 53;
            case aura::WeatherCondition::Rain: return 65;
            case aura::WeatherCondition::Snow: return 75;
            case aura::WeatherCondition::Thunderstorm: return 95;
            case aura::WeatherCondition::Windy: return 3;
            case aura::WeatherCondition::Unknown: return -1;
        }
        return -1;
    };
    data.weather_code = demo_code(condition);
    data.rain_probability_percent = condition == aura::WeatherCondition::Thunderstorm ? 92 :
        (condition == aura::WeatherCondition::Rain ? 84 :
         (condition == aura::WeatherCondition::Drizzle ? 55 : 8));
    data.observed_at = "2026-10-19T18:24";
    const aura::WeatherCondition later[] = {condition, aura::WeatherCondition::PartlyCloudy,
        aura::WeatherCondition::Cloudy, aura::WeatherCondition::Clear};
    const float temperatures[] = {data.temperature_c, data.temperature_c - 1.0F,
                                  data.temperature_c - 2.0F, data.temperature_c - 3.0F};
    const char* times[] = {"2026-10-19T18:00", "2026-10-19T21:00",
                           "2026-10-20T00:00", "2026-10-20T03:00"};
    for (std::size_t index = 0; index < data.hourly.size(); ++index) {
        data.hourly[index] = {times[index], temperatures[index], later[index], demo_code(later[index]),
            later[index] == aura::WeatherCondition::Rain ? 82 :
            (later[index] == aura::WeatherCondition::Thunderstorm ? 92 : 12)};
    }
    data.hourly_count = data.hourly.size();
    const char* dates[] = {"2026-10-19", "2026-10-20", "2026-10-21", "2026-10-22"};
    const aura::WeatherCondition days[] = {condition, aura::WeatherCondition::Rain,
        aura::WeatherCondition::Cloudy, aura::WeatherCondition::Clear};
    for (std::size_t index = 0; index < data.daily.size(); ++index) {
        data.daily[index] = {dates[index], data.daily_minimum_c - static_cast<float>(index),
            data.daily_maximum_c - static_cast<float>(index), days[index], demo_code(days[index]),
            days[index] == aura::WeatherCondition::Rain ? 82 : 14};
    }
    data.daily_count = data.daily.size();
    data.origin = aura::WeatherDataOrigin::Demo;
    return data;
}

// Deterministic runs (captures, pointer tests, stress) never scan the host:
// they use these clearly labelled DEMO networks.
std::vector<aura::WifiNetwork> demo_wifi_networks() {
    return {
        {"AURA Lab · DEMO", -48, aura::WifiSecurity::WpaPersonal},
        {"Καφέ Αθηνά · DEMO", -63, aura::WifiSecurity::Open},
        {"Γραφείο 5G · DEMO", -71, aura::WifiSecurity::Wpa3Personal},
        {"Corp Secure · DEMO", -77, aura::WifiSecurity::Enterprise},
        {"Guest · DEMO", -84, aura::WifiSecurity::WpaPersonal},
    };
}

aura::WeatherData demo_tlesdetector_weather_data() {
    auto data = demo_weather_data(aura::WeatherCondition::Thunderstorm);
    data.provider = aura::WeatherProvider::TlesDetector;
    data.temperature_c = 19.0F;
    data.apparent_temperature_c = 18.0F;
    data.humidity_percent = 78;
    data.wind_kmh = 24.0F;
    data.metrics.pressure_hpa = 1007.0F;
    data.metrics.set(aura::WeatherMetric::Pressure);
    data.metrics.wind_gust_kmh = 46.0F;
    data.metrics.set(aura::WeatherMetric::WindGust);
    data.metrics.visibility_km = 8.4F;
    data.metrics.set(aura::WeatherMetric::Visibility);
    data.metrics.uv_index = 0.0F;
    data.metrics.set(aura::WeatherMetric::UvIndex);
    data.metrics.european_aqi = 41;
    data.metrics.set(aura::WeatherMetric::AirQuality);
    data.hazards.alerts_available = true;
    data.hazards.active_alert_count = 3;
    data.hazards.nearby_alert_count = 1;
    data.hazards.highest_alert = aura::WeatherAlertLevel::Severe;
    data.hazards.lightning_available = true;
    data.hazards.lightning_observed = true;
    data.hazards.nearest_lightning_km = 84.0F;
    data.hazards.lightning_bearing_deg = 215;
    data.hazards.lightning_age_minutes = 4;
    data.hazards.max_flashes_per_5_min = 9;
    data.hazards.xweather_available = true;
    data.hazards.positive_cg_count = 3;
    data.hazards.max_positive_ka = 91;
    return data;
}
}

int main(int argc, char** argv) {
    bool smoke = false;
    bool ui_stress = false;
    bool aoraton_pairing_smoke = false;
    std::string screenshot_path;
    std::string capture_scenario;
    std::string interaction_report;
    std::string motion_scenario;
    std::string motion_directory;
    std::string weather_live_city;
    bool capture_weather_forecast = false;
    int capture_weather_scroll_y = 340;
    float window_scale = 1.0F;
    const auto arguments = utf8_command_line_arguments(argc, argv);
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (argument == "--smoke") smoke = true;
        if (argument == "--ui-stress") ui_stress = true;
        if (argument == "--aoraton-pairing-smoke") aoraton_pairing_smoke = true;
        if (argument == "--screenshot" && index + 1 < arguments.size()) screenshot_path = arguments[++index];
        if (argument == "--capture" && index + 1 < arguments.size()) capture_scenario = arguments[++index];
        if (argument == "--interaction-test" && index + 1 < arguments.size()) interaction_report = arguments[++index];
        if (argument == "--motion-sequence" && index + 1 < arguments.size()) motion_scenario = arguments[++index];
        if (argument == "--motion-dir" && index + 1 < arguments.size()) motion_directory = arguments[++index];
        if (argument == "--weather-live-city" && index + 1 < arguments.size()) weather_live_city = arguments[++index];
        if (argument == "--scale" && index + 1 < arguments.size()) {
            try { window_scale = std::clamp(std::stof(arguments[++index]), 1.0F, 3.0F); }
            catch (...) { window_scale = 1.0F; }
        }
    }

    SDL_SetMainReady();
    lv_init();
    lv_display_t* display = lv_sdl_window_create(480, 480);
    if (!display) {
        std::cerr << "AURA: failed to create SDL display\n";
        return 2;
    }
    lv_sdl_window_set_title(display, "AURA • 480×480 LVGL Simulator");
    lv_sdl_window_set_resizeable(display, false);
    lv_sdl_window_set_zoom(display, window_scale);
    auto* mouse = lv_sdl_mouse_create();
    lv_indev_set_display(mouse, display);
    auto* keyboard = lv_sdl_keyboard_create();
    lv_indev_set_display(keyboard, display);
    auto* keyboard_group = lv_group_create();
    lv_group_set_default(keyboard_group);
    lv_indev_set_group(keyboard, keyboard_group);

    aura::AssistantController controller;
    aura::DeviceStatusModel device_status;
    const auto& text = aura::l10n::el_gr();
    const std::string aoraton_mode_name = environment("AURA_AORATON_MODE", "mock");
    std::unique_ptr<aura::AoratonMailbox> aoraton_mailbox;
    std::unique_ptr<aura::IAoratonService> aoraton_service;
    std::unique_ptr<aura::simulator::FailClosedAoratonTransport> aoraton_fail_closed_transport;
    if (aoraton_mode_name == "mock") {
        aoraton_service = std::make_unique<aura::MockAoratonService>();
    } else if (aoraton_mode_name == "test" || aoraton_mode_name == "live") {
        const auto runtime_mode = aoraton_mode_name == "test" ?
            aura::AoratonRuntimeMode::IntegrationTest : aura::AoratonRuntimeMode::Live;
        aoraton_mailbox = std::make_unique<aura::AoratonMailbox>();
        aoraton_service = std::make_unique<aura::RealAoratonService>(
            *aoraton_mailbox, runtime_mode, aoraton_client_message_id);
        aoraton_fail_closed_transport =
            std::make_unique<aura::simulator::FailClosedAoratonTransport>(*aoraton_mailbox);
        std::cerr << "AURA Aoraton " << (runtime_mode == aura::AoratonRuntimeMode::IntegrationTest ? "TEST" : "LIVE")
                  << ": transport disabled until a reviewed isolated pairing backend is configured\n";
    } else {
        std::cerr << "AURA: invalid AURA_AORATON_MODE='" << aoraton_mode_name
                  << "' (expected mock, test, or live); refusing implicit fallback\n";
        return 2;
    }
    aura::IAoratonService& aoraton = *aoraton_service;
    std::optional<aura::AoratonSnapshot> aoraton_capture_override;
    aura::simulator::WindowsAudioBackend audio;
    audio.initialize();
    aura::AuraUi ui;
    aura::WeatherModel weather_model;
    aura::simulator::OpenMeteoWeatherService weather_service;
    const std::string weather_store_override = environment("AURA_WEATHER_STORE");
    aura::simulator::WeatherStore weather_store(weather_store_override.empty() ?
        aura::simulator::WeatherStore::default_base_path() : std::filesystem::path(weather_store_override));
    if (const auto cached = weather_store.load_cache()) weather_model.restore_cache(*cached);
    if (const auto saved = weather_store.load_location()) weather_model.restore_location(*saved);
    std::optional<std::future<aura::WeatherGeocodingResult>> pending_weather_search;
    std::optional<std::future<aura::WeatherFetchResult>> pending_weather_fetch;
    bool weather_auto_fetch_first{false};
    bool weather_fetch_persist{false};
    bool weather_live_finished{false};
    bool weather_live_succeeded{false};
    int weather_live_exit_code{0};
    std::chrono::steady_clock::time_point weather_live_finished_at{};
    std::unique_ptr<aura::IAssistantProvider> provider;
    bool live_provider = std::string(environment("AURA_PROVIDER", "mock")) == "codex";
    std::future<aura::AssistantReply> pending_reply;
    // Superseded async work. Destroying an unfinished std::async future blocks
    // until it completes, so replaced requests are parked here and released
    // once ready instead of freezing the UI thread.
    std::vector<std::future<aura::AssistantReply>> retired_replies;
    std::vector<std::future<aura::WeatherGeocodingResult>> retired_weather_searches;
    std::vector<std::future<aura::WeatherFetchResult>> retired_weather_fetches;
    const auto prune_ready = [](auto& futures) {
        futures.erase(std::remove_if(futures.begin(), futures.end(), [](auto& future) {
            return future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        }), futures.end());
    };
    bool pending = false;
    std::uint64_t pending_generation = 0;
    float manual_input_amplitude = -1.0F;
    struct {
        int ask{0};
        int translate_mine{0};
        int translate_theirs{0};
        int finish_listening{0};
        int cancel{0};
        int retry{0};
        int home{0};
        int provider{0};
        int audio_tone{0};
        int audio_wav{0};
        int audio_pause{0};
        int audio_stop{0};
        int audio_mute{0};
        int notifications_clear{0};
        int aoraton_room{0};
        int aoraton_prepare{0};
        int aoraton_confirm{0};
        int aoraton_cancel{0};
        int weather_open{0};
        int weather_refresh{0};
        int weather_search{0};
        int weather_select{0};
        int wifi_open{0};
        int wifi_scan{0};
        int wifi_connect{0};
        int wifi_forget{0};
    } interaction_counts;

    std::function<void()> provider_changed_hook;
    auto select_provider = [&](bool live) {
        if (provider) provider->cancel();
        if (pending) {
            // The replaced provider's reply can no longer be delivered; end the
            // turn explicitly instead of leaving Thinking without a request.
            pending = false;
            controller.dispatch({aura::EventType::Cancel});
        }
        if (pending_reply.valid()) retired_replies.push_back(std::move(pending_reply));
        live_provider = live;
        if (live) {
            provider = std::make_unique<aura::simulator::LocalCodexProvider>(
                environment("AURA_BRIDGE_TOKEN"),
                static_cast<std::uint16_t>(integer(environment("AURA_BRIDGE_PORT", "4778"), 4778)));
        } else {
            provider = std::make_unique<aura::MockProvider>();
        }
        if (provider_changed_hook) provider_changed_hook();
    };

    auto apply = [&] { ui.apply(controller.snapshot()); };
    auto apply_weather = [&] { ui.apply_weather(weather_model.snapshot()); };
    auto same_weather_location = [](const aura::WeatherLocation& left, const aura::WeatherLocation& right) {
        return left.name == right.name && std::fabs(left.latitude - right.latitude) < 0.0001 &&
               std::fabs(left.longitude - right.longitude) < 0.0001;
    };
    std::function<void(aura::WeatherLocation, bool)> start_weather_fetch;
    std::function<void(std::string, bool)> start_weather_search;
    start_weather_fetch = [&](aura::WeatherLocation location, bool save_as_default) {
        if (!weather_model.begin_fetch(location, save_as_default)) return;
        if (save_as_default && interaction_report.empty()) weather_store.save_location(location);
        weather_fetch_persist = save_as_default ||
            (weather_model.snapshot().has_saved_location &&
             same_weather_location(weather_model.snapshot().saved_location, location));
        apply_weather();
        if (!interaction_report.empty()) {
            auto data = demo_weather_data(aura::WeatherCondition::PartlyCloudy, location.name);
            data.location = std::move(location);
            weather_model.fetch_succeeded(data);
            apply_weather();
            return;
        }
        if (pending_weather_fetch) retired_weather_fetches.push_back(std::move(*pending_weather_fetch));
        pending_weather_fetch.emplace(weather_service.fetch(std::move(location)));
    };
    start_weather_search = [&](std::string query, bool auto_fetch_first) {
        if (!weather_model.begin_search(query)) return;
        weather_auto_fetch_first = auto_fetch_first;
        apply_weather();
        if (!interaction_report.empty()) {
            std::vector<aura::WeatherLocation> results{
                demo_weather_location(query.empty() ? "Αθήνα" : query),
                demo_weather_location("Αθήνα"), demo_weather_location("Σπάρτη")};
            weather_model.search_succeeded(results);
            apply_weather();
            if (auto_fetch_first) {
                weather_auto_fetch_first = false;
                ui.show(aura::UiView::Weather);
                start_weather_fetch(results.front(), false);
            }
            return;
        }
        if (pending_weather_search) retired_weather_searches.push_back(std::move(*pending_weather_search));
        pending_weather_search.emplace(weather_service.search(std::move(query)));
    };
    auto open_weather = [&] {
        const auto& snapshot = weather_model.snapshot();
        if (snapshot.has_saved_location) start_weather_fetch(snapshot.saved_location, false);
        else apply_weather();
    };
    auto poll_weather = [&] {
        if (pending_weather_search &&
            pending_weather_search->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto result = pending_weather_search->get();
            pending_weather_search.reset();
            if (result.ok) {
                weather_model.search_succeeded(result.locations);
                apply_weather();
                if (weather_auto_fetch_first && !result.locations.empty()) {
                    weather_auto_fetch_first = false;
                    ui.show(aura::UiView::Weather);
                    start_weather_fetch(result.locations.front(), false);
                }
            } else if (result.error_code != "CANCELLED") {
                if (!weather_live_city.empty()) {
                    std::cerr << "AURA live weather geocoding error=" << result.error_code << '\n';
                }
                weather_model.search_failed(result.error_code, result.offline);
                apply_weather();
                if (!weather_live_city.empty()) {
                    weather_live_finished = true;
                    weather_live_succeeded = false;
                    weather_live_finished_at = std::chrono::steady_clock::now();
                }
            }
        }
        if (pending_weather_fetch &&
            pending_weather_fetch->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto result = pending_weather_fetch->get();
            pending_weather_fetch.reset();
            if (result.ok) {
                weather_model.fetch_succeeded(result.data);
                if (weather_fetch_persist && interaction_report.empty()) weather_store.save_cache(result.data);
                weather_live_succeeded = true;
            } else if (result.error_code != "CANCELLED") {
                if (!weather_live_city.empty()) {
                    std::cerr << "AURA live weather forecast error=" << result.error_code << '\n';
                }
                weather_model.fetch_failed(result.error_code, result.offline);
                weather_live_succeeded = false;
            }
            apply_weather();
            if (!weather_live_city.empty()) {
                weather_live_finished = true;
                weather_live_finished_at = std::chrono::steady_clock::now();
            }
        }
    };
    // Wi-Fi: real read-only scans of the host adapter in interactive runs,
    // labelled DEMO networks in deterministic runs. A simulator "connect" is a
    // labelled stand-in; the password is never stored or checked here.
    const bool deterministic_run = smoke || ui_stress || !capture_scenario.empty() ||
        !interaction_report.empty() || !motion_scenario.empty() || !weather_live_city.empty() ||
        aoraton_pairing_smoke;
    aura::WifiModel wifi_model;
    aura::simulator::WindowsWifiService wifi_service;
    aura::simulator::WifiHostScan wifi_host;
    std::optional<std::future<aura::simulator::WifiHostScan>> pending_wifi_scan;
    std::vector<std::future<aura::simulator::WifiHostScan>> retired_wifi_scans;
    std::chrono::steady_clock::time_point wifi_last_scan{};
    std::optional<std::chrono::steady_clock::time_point> wifi_connect_ready_at;
    std::string wifi_connect_ssid;
    bool wifi_fail_next = false;
    struct PreviousLink {
        bool connected{false};
        std::string ssid;
        int rssi_dbm{-100};
        aura::WifiOrigin origin{aura::WifiOrigin::Host};
    } wifi_previous_link;
    auto apply_wifi = [&] { ui.apply_wifi(wifi_model.snapshot()); };
    // The status bar, the assistant and Aoraton follow the simulated device link.
    auto sync_device_link = [&] {
        const auto& wifi = wifi_model.snapshot();
        if (wifi.link == aura::WifiLinkState::Connecting || wifi.link == aura::WifiLinkState::Unavailable) return;
        const bool online = wifi.link == aura::WifiLinkState::Connected;
        device_status.set_connection(online,
            wifi.link_origin == aura::WifiOrigin::Host ? aura::StatusOrigin::Live : aura::StatusOrigin::Simulated,
            online ? aura::wifi_signal_bars(wifi.connected_rssi_dbm) : 0);
        if (controller.snapshot().wifi_connected != online) {
            controller.dispatch({aura::EventType::WifiChanged, {}, online ? 1 : 0});
        }
        aoraton.set_network_available(online);
    };
    auto restore_previous_link = [&] {
        if (wifi_previous_link.connected) {
            wifi_model.set_link(true, wifi_previous_link.ssid, wifi_previous_link.rssi_dbm, {},
                                wifi_previous_link.origin);
        } else if (wifi_host.host_connected) {
            wifi_model.set_link(true, wifi_host.host_ssid, wifi_host.host_rssi_dbm, {}, aura::WifiOrigin::Host);
        }
    };
    auto seed_demo_wifi = [&] {
        wifi_model.set_radio_available(true);
        wifi_model.scan_succeeded(demo_wifi_networks(), aura::WifiOrigin::Demo);
        wifi_model.set_link(true, "AURA Lab · DEMO", -48, {}, aura::WifiOrigin::Demo);
        wifi_model.set_saved_network("AURA Lab · DEMO");
    };
    auto start_wifi_scan = [&] {
        if (deterministic_run) {
            if (!wifi_model.begin_scan()) return;
            wifi_model.scan_succeeded(demo_wifi_networks(), aura::WifiOrigin::Demo);
            apply_wifi();
            return;
        }
        if (!wifi_model.begin_scan()) return;
        apply_wifi();
        if (pending_wifi_scan) retired_wifi_scans.push_back(std::move(*pending_wifi_scan));
        pending_wifi_scan.emplace(wifi_service.scan());
        wifi_last_scan = std::chrono::steady_clock::now();
    };
    auto poll_wifi = [&](std::chrono::steady_clock::time_point now) {
        if (pending_wifi_scan &&
            pending_wifi_scan->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto result = pending_wifi_scan->get();
            pending_wifi_scan.reset();
            if (!result.adapter_present) {
                wifi_model.set_radio_available(false, result.scan.error_code.empty() ? "NO_ADAPTER" : result.scan.error_code);
            } else {
                wifi_host = result;
                if (result.scan.ok) wifi_model.scan_succeeded(result.scan.networks, aura::WifiOrigin::Live);
                else wifi_model.scan_failed(result.scan.error_code);
                // A simulated association stays in place; otherwise mirror the host.
                if (wifi_model.snapshot().link_origin == aura::WifiOrigin::Host ||
                    wifi_model.snapshot().link == aura::WifiLinkState::Disconnected) {
                    if (result.host_connected) {
                        wifi_model.set_link(true, result.host_ssid, result.host_rssi_dbm, {}, aura::WifiOrigin::Host);
                    } else if (wifi_model.snapshot().link_origin == aura::WifiOrigin::Host) {
                        wifi_model.set_link(false, {}, -100, {}, aura::WifiOrigin::Host);
                    }
                }
                sync_device_link();
            }
            apply_wifi();
        }
        if (wifi_connect_ready_at && now >= *wifi_connect_ready_at) {
            wifi_connect_ready_at.reset();
            if (wifi_fail_next) {
                wifi_fail_next = false;
                wifi_model.connect_failed("WRONG_PASSWORD");
                restore_previous_link();
            } else {
                wifi_model.connect_succeeded(wifi_connect_ssid, {}, aura::WifiOrigin::Simulated);
            }
            sync_device_link();
            apply_wifi();
        }
    };

    std::function<void()> cancel_voice_hook;
    auto cancel = [&] {
        if (provider) provider->cancel();
        if (cancel_voice_hook) cancel_voice_hook();
        pending = false;
        controller.dispatch({aura::EventType::Cancel});
    };
    std::function<aura::TurnRequest(std::string)> turn_request_hook;
    auto start_turn = [&](std::string prompt) {
        const auto weather_intent = aura::analyze_weather_intent(prompt);
        const bool answer_from_forecast = weather_intent.kind == aura::WeatherIntentKind::ShowSaved &&
                                          weather_model.snapshot().has_data;
        if (weather_intent.kind != aura::WeatherIntentKind::None && !answer_from_forecast) {
            // A weather request is answered by the Weather screen; close the
            // voice turn so the loop below does not re-submit a transcript.
            if (aura::is_turn_active(controller.snapshot().state)) controller.dispatch({aura::EventType::Cancel});
            if (weather_intent.kind == aura::WeatherIntentKind::ShowSaved) {
                ui.show(aura::UiView::Weather);
                open_weather();
            } else if (weather_intent.kind == aura::WeatherIntentKind::QueryCity) {
                ui.show(aura::UiView::WeatherLocation);
                start_weather_search(weather_intent.city_query, true);
            } else {
                ui.show(aura::UiView::WeatherLocation);
                if (!weather_intent.city_query.empty()) {
                    start_weather_search(weather_intent.city_query, false);
                }
            }
            return;
        }
        if (pending) {
            controller.dispatch({aura::EventType::Fail, "BUSY"});
            apply();
            return;
        }
        if (prompt.empty()) prompt = text.capture_short_prompt;
        controller.dispatch({aura::EventType::TurnStarted, prompt});
        apply();
        pending_generation = controller.snapshot().turn_generation;
        if (pending_reply.valid()) retired_replies.push_back(std::move(pending_reply));
        pending_reply = provider->start_turn(turn_request_hook ? turn_request_hook(std::move(prompt)) :
                                                                 aura::TurnRequest{std::move(prompt), {}, {}, {}, {}, {}});
        pending = true;
    };

    aura::simulator::CompanionContext companion_context;
    companion_context.controller = &controller;
    companion_context.ui = &ui;
    companion_context.audio = &audio;
    companion_context.aoraton = &aoraton;
    companion_context.weather = &weather_model;
    companion_context.status = &device_status;
    companion_context.start_turn = [&start_turn](std::string prompt) { start_turn(std::move(prompt)); };
    companion_context.make_translator = [&]() -> std::unique_ptr<aura::IAssistantProvider> {
        if (live_provider) {
            return std::make_unique<aura::simulator::LocalCodexProvider>(environment("AURA_BRIDGE_TOKEN"),
                static_cast<std::uint16_t>(integer(environment("AURA_BRIDGE_PORT", "4778"), 4778)));
        }
        return std::make_unique<aura::MockProvider>(deterministic_run ? 0U : 600U);
    };
    companion_context.deterministic = deterministic_run;
    companion_context.store_dir = environment("AURA_COMPANION_STORE", "simulator/runtime/companion");
    aura::simulator::CompanionRuntime companion(std::move(companion_context));
    turn_request_hook = [&companion](std::string prompt) { return companion.turn_request(std::move(prompt)); };
    provider_changed_hook = [&companion] { companion.provider_changed(); };
    cancel_voice_hook = [&companion] { companion.cancel_voice(); };

    aura::AuraUiCallbacks ui_callbacks;
    ui_callbacks.on_ask = [&] {
        ++interaction_counts.ask;
        // A new question interrupts a spoken answer instead of talking over it.
        const auto audio_state = audio.diagnostics().state;
        if (audio_state == aura::AudioState::Playing || audio_state == aura::AudioState::Paused) audio.stop();
        controller.dispatch({aura::EventType::WakeDetected});
        controller.dispatch({aura::EventType::BeginRecording});
        // The PC microphone when speech recognition runs; otherwise the labelled synthetic input.
        companion.begin_listening();
    };
    ui_callbacks.on_end_listening = [&] {
        ++interaction_counts.finish_listening;
        if (companion.capturing()) companion.end_listening();
        else controller.dispatch({aura::EventType::EndRecording});
    };
    ui_callbacks.on_cancel = [&] { ++interaction_counts.cancel; cancel(); };
    ui_callbacks.on_retry = [&] {
        ++interaction_counts.retry;
        if (!controller.snapshot().wifi_connected) {
            controller.dispatch({aura::EventType::ProviderUnavailable, "NETWORK_UNAVAILABLE"});
            return;
        }
        // "Try again" asks the same question again rather than going home.
        const std::string prompt = controller.snapshot().user_text;
        controller.dispatch({aura::EventType::Retry});
        if (!prompt.empty()) start_turn(prompt);
    };
    ui_callbacks.on_home = [&] { ++interaction_counts.home; controller.dispatch({aura::EventType::Reset}); };
    ui_callbacks.on_notifications_clear = [&] {
        ++interaction_counts.notifications_clear;
        controller.dispatch({aura::EventType::NotificationsCleared});
    };
    ui_callbacks.on_provider_changed = [&](bool live) {
        ++interaction_counts.provider;
        select_provider(live);
    };
    ui_callbacks.on_audio_test = [&] {
        ++interaction_counts.audio_tone;
        audio.play_test_tone();
    };
    ui_callbacks.on_audio_wav = [&] {
        ++interaction_counts.audio_wav;
        audio.play_bundled_wav();
    };
    ui_callbacks.on_audio_pause_resume = [&] {
        ++interaction_counts.audio_pause;
        if (audio.diagnostics().state == aura::AudioState::Paused) audio.resume();
        else audio.pause();
    };
    ui_callbacks.on_audio_stop = [&] { ++interaction_counts.audio_stop; audio.stop(); };
    ui_callbacks.on_audio_muted = [&](bool muted) { ++interaction_counts.audio_mute; audio.set_muted(muted); };
    ui_callbacks.on_audio_volume = [&](float volume) { audio.set_volume(volume); };
    ui_callbacks.on_aoraton_room = [&](const std::string& room_id) {
        ++interaction_counts.aoraton_room;
        return aoraton.select_room(room_id);
    };
    ui_callbacks.on_aoraton_prepare = [&](const std::string& room_id, const std::string& text_value) {
        ++interaction_counts.aoraton_prepare;
        return aoraton.prepare_send(room_id, text_value);
    };
    ui_callbacks.on_aoraton_confirm = [&] {
        ++interaction_counts.aoraton_confirm;
        return aoraton.confirm_send();
    };
    ui_callbacks.on_aoraton_cancel = [&] {
        ++interaction_counts.aoraton_cancel;
        aoraton.cancel_send();
    };
    ui_callbacks.on_aoraton_pair_start = [&] { return aoraton.begin_pairing(); };
    ui_callbacks.on_aoraton_pair_confirm_sas = [&] { return aoraton.confirm_pairing_sas(); };
    ui_callbacks.on_aoraton_pair_cancel = [&] { aoraton.cancel_pairing(); };
    ui_callbacks.on_aoraton_unpair = [&] { aoraton.unpair(); };
    ui_callbacks.on_weather_open = [&] {
        ++interaction_counts.weather_open;
        open_weather();
    };
    ui_callbacks.on_weather_refresh = [&] {
        ++interaction_counts.weather_refresh;
        const auto& snapshot = weather_model.snapshot();
        if (snapshot.has_data) start_weather_fetch(snapshot.data.location, false);
        else if (snapshot.has_saved_location) start_weather_fetch(snapshot.saved_location, false);
        else apply_weather();
    };
    ui_callbacks.on_weather_search = [&](const std::string& query) {
        ++interaction_counts.weather_search;
        start_weather_search(query, false);
    };
    ui_callbacks.on_weather_select_result = [&](std::size_t index) {
        ++interaction_counts.weather_select;
        const auto& snapshot = weather_model.snapshot();
        if (index < snapshot.suggestion_count) start_weather_fetch(snapshot.suggestions[index], true);
    };
    ui_callbacks.on_wifi_open = [&] {
        ++interaction_counts.wifi_open;
        const auto& wifi = wifi_model.snapshot();
        const bool stale = !wifi.scanned ||
            std::chrono::steady_clock::now() - wifi_last_scan > std::chrono::seconds(15);
        if (!wifi.scanning && stale) start_wifi_scan();
    };
    ui_callbacks.on_wifi_scan = [&] {
        ++interaction_counts.wifi_scan;
        start_wifi_scan();
    };
    ui_callbacks.on_wifi_connect = [&](const std::string& ssid, aura::WifiSecurity security,
                                       const std::string& password) {
        ++interaction_counts.wifi_connect;
        const auto& wifi = wifi_model.snapshot();
        wifi_previous_link = {wifi.link == aura::WifiLinkState::Connected, wifi.connected_ssid,
                              wifi.connected_rssi_dbm, wifi.link_origin};
        // Typed validation happens in the shared model; the password itself is
        // neither copied nor kept by the simulator.
        if (!wifi_model.begin_connect(ssid, security, password)) {
            apply_wifi();
            return false;
        }
        wifi_connect_ssid = ssid;
        wifi_connect_ready_at = std::chrono::steady_clock::now() +
            (deterministic_run ? std::chrono::milliseconds(0) : std::chrono::milliseconds(1400));
        apply_wifi();
        return true;
    };
    ui_callbacks.on_wifi_cancel = [&] {
        wifi_connect_ready_at.reset();
        wifi_model.cancel_connect();
        restore_previous_link();
        sync_device_link();
        apply_wifi();
    };
    ui_callbacks.on_wifi_forget = [&] {
        ++interaction_counts.wifi_forget;
        const bool simulated = wifi_model.snapshot().link_origin != aura::WifiOrigin::Host;
        wifi_model.forget_saved_network();
        // The simulator itself stays online through the host connection.
        if (simulated && wifi_host.host_connected) {
            wifi_model.set_link(true, wifi_host.host_ssid, wifi_host.host_rssi_dbm, {}, aura::WifiOrigin::Host);
        }
        sync_device_link();
        apply_wifi();
    };
    companion.install(ui_callbacks);
    {
        auto translate_record = ui_callbacks.on_translate_record;
        ui_callbacks.on_translate_record = [&interaction_counts, translate_record](aura::TranslateSide side) {
            ++(side == aura::TranslateSide::Mine ? interaction_counts.translate_mine : interaction_counts.translate_theirs);
            return translate_record(side);
        };
    }
    ui.create(display, std::move(ui_callbacks));
    ui.set_developer_mode(true);
    select_provider(live_provider);
    ui.set_provider_live(live_provider);
    std::time_t initial_time = std::time(nullptr);
    std::tm initial_local{};
    localtime_s(&initial_local, &initial_time);
    device_status.set_clock(initial_local.tm_hour, initial_local.tm_min, aura::StatusOrigin::Live);
    device_status.set_date(initial_local.tm_year + 1900, initial_local.tm_mon + 1, initial_local.tm_mday,
                           initial_local.tm_wday, aura::StatusOrigin::Live);
    {
        const std::string token = environment("AURA_BRIDGE_TOKEN");
        std::shared_ptr<aura::simulator::BridgeSpeechService> speech;
        if (!deterministic_run && !token.empty()) {
            speech = std::make_shared<aura::simulator::BridgeSpeechService>(token,
                static_cast<std::uint16_t>(integer(environment("AURA_BRIDGE_PORT", "4778"), 4778)));
        }
        companion.start(std::move(speech));
        aura::LocalDateTime local{initial_local.tm_year + 1900, initial_local.tm_mon + 1, initial_local.tm_mday,
                                  initial_local.tm_hour, initial_local.tm_min, initial_local.tm_sec,
                                  initial_local.tm_wday};
        companion.set_clock(local);
        companion.apply();
    }
    device_status.set_power(78, false, aura::StatusOrigin::Simulated);
    device_status.set_connection(true, aura::StatusOrigin::Simulated, 3);
    device_status.set_bluetooth(false, aura::StatusOrigin::Simulated);
    // The desktop build shows one labelled sample notification; a device
    // starts with none.
    controller.dispatch({aura::EventType::NotificationReceived, "DEMO"});
    if (deterministic_run) {
        seed_demo_wifi();
    } else {
        wifi_host = aura::simulator::WindowsWifiService::probe();
        if (wifi_host.adapter_present) {
            wifi_model.set_radio_available(true);
            if (wifi_host.host_connected) {
                wifi_model.set_link(true, wifi_host.host_ssid, wifi_host.host_rssi_dbm, {}, aura::WifiOrigin::Host);
                sync_device_link();
            }
            start_wifi_scan();
        } else {
            wifi_model.set_radio_available(false, wifi_host.scan.error_code.empty() ? "NO_ADAPTER"
                                                                                   : wifi_host.scan.error_code);
        }
    }
    apply();
    ui.apply_status(device_status.snapshot());
    ui.apply_audio(audio.diagnostics());
    ui.apply_aoraton(aoraton.snapshot());
    ui.apply_weather(weather_model.snapshot());
    ui.apply_wifi(wifi_model.snapshot());

    if (!weather_live_city.empty()) {
        ui.show(aura::UiView::WeatherLocation);
        start_weather_search(weather_live_city, true);
    }

    if (!capture_scenario.empty()) {
        ui.set_capture_mode(true);
        controller.dispatch({aura::EventType::BatteryChanged, {}, 92});
        device_status.set_clock(18, 24, aura::StatusOrigin::Simulated);
        device_status.set_date(2026, 10, 22, 4, aura::StatusOrigin::Simulated);
        device_status.set_power(92, false, aura::StatusOrigin::Simulated);
        // Evidence never shows this desktop's saved city or cached forecast;
        // Home captures use labelled DEMO weather instead.
        weather_model.clear_location();
        if (capture_scenario == "home" || capture_scenario == "ambient" || capture_scenario == "low-battery" ||
            capture_scenario == "briefing") {
            weather_model.fetch_succeeded(demo_weather_data());
        }
        apply_weather();
        device_status.set_connection(true, aura::StatusOrigin::Simulated, 3);
        device_status.set_bluetooth(false, aura::StatusOrigin::Simulated);
        ui.apply_status(device_status.snapshot());
        apply();
        if (capture_scenario.rfind("status-", 0) == 0 && capture_scenario.size() == 11) {
            const std::string bits = capture_scenario.substr(7);
            if (bits.find_first_not_of("01") != std::string::npos) {
                std::cerr << "AURA: invalid status matrix scenario: " << capture_scenario << '\n';
                return 5;
            }
            const bool wifi = bits[0] == '1';
            const bool bluetooth = bits[1] == '1';
            const bool charging = bits[2] == '1';
            const bool developer = bits[3] == '1';
            const int battery_percent = wifi ? (bluetooth ? 92 : 68) : (bluetooth ? 35 : 12);
            ui.set_developer_mode(developer);
            device_status.set_connection(wifi, aura::StatusOrigin::Simulated, wifi ? 3 : 0);
            device_status.set_bluetooth(bluetooth, aura::StatusOrigin::Simulated);
            device_status.set_power(battery_percent, charging, aura::StatusOrigin::Simulated);
            ui.apply_status(device_status.snapshot());
            ui.show(aura::UiView::Home);
        }
        else if (capture_scenario == "home") ui.show(aura::UiView::Home);
        else if (capture_scenario == "ambient") {
            controller.dispatch({aura::EventType::Sleep});
            apply();
        }
        else if (capture_scenario == "low-battery") {
            controller.dispatch({aura::EventType::BatteryChanged, {}, 12});
            apply();
            ui.show(aura::UiView::Home);
        }
        else if (capture_scenario == "launcher" || capture_scenario == "quick-actions") ui.show(aura::UiView::Launcher);
        else if (capture_scenario == "wake") {
            controller.dispatch({aura::EventType::WakeDetected});
            apply();
        }
        else if (capture_scenario == "listening") {
            controller.dispatch({aura::EventType::WakeDetected});
            controller.dispatch({aura::EventType::BeginRecording});
            apply();
        } else if (capture_scenario == "thinking") {
            controller.dispatch({aura::EventType::TurnStarted, text.capture_thinking_prompt});
            apply();
        } else if (capture_scenario == "answer") {
            controller.dispatch({aura::EventType::TurnStarted, text.capture_short_prompt});
            controller.dispatch({aura::EventType::FinalResult, text.capture_short_answer});
            apply();
        } else if (capture_scenario == "answer-long") {
            controller.dispatch({aura::EventType::TurnStarted, text.capture_long_prompt});
            controller.dispatch({aura::EventType::FinalResult, text.capture_long_answer});
            apply();
        } else if (capture_scenario == "speaking") {
            controller.dispatch({aura::EventType::TurnStarted, text.capture_speaking_prompt});
            controller.dispatch({aura::EventType::FinalResult, text.capture_speaking_answer});
            apply();
            audio.play_test_tone(1600);
            ui.apply_audio(audio.diagnostics());
        } else if (capture_scenario == "cards" || capture_scenario == "search") ui.show(aura::UiView::Cards);
        else if (capture_scenario == "settings") ui.show(aura::UiView::Settings);
        else if (capture_scenario == "diagnostics") ui.show(aura::UiView::Diagnostics);
        else if (capture_scenario == "notifications") ui.show(aura::UiView::Notifications);
        else if (capture_scenario == "offline") {
            controller.dispatch({aura::EventType::ProviderUnavailable, "PROVIDER_UNAVAILABLE"});
            apply();
        } else if (capture_scenario == "error") {
            controller.dispatch({aura::EventType::Fail, "DEMO_FAILURE"});
            apply();
        } else if (capture_scenario == "audio") {
            ui.show(aura::UiView::Audio);
            audio.play_test_tone(1400);
            ui.apply_audio(audio.diagnostics());
        } else if (capture_scenario == "audio-wav") {
            ui.show(aura::UiView::Audio);
            if (!audio.play_bundled_wav()) return 7;
            ui.apply_audio(audio.diagnostics());
        } else if (capture_scenario == "aoraton") {
            ui.show(aura::UiView::AoratonRooms);
        } else if (capture_scenario == "aoraton-unpaired") {
            aura::AoratonSnapshot snapshot;
            snapshot.mock_mode = false;
            snapshot.runtime_mode = aura::AoratonRuntimeMode::IntegrationTest;
            snapshot.pairing = aura::AoratonPairingState::NotPaired;
            snapshot.sequence = aoraton.snapshot().sequence + 1000;
            aoraton_capture_override = snapshot;
            ui.apply_aoraton(snapshot);
            ui.show(aura::UiView::AoratonRooms);
        } else if (capture_scenario == "aoraton-pair") {
            aura::AoratonSnapshot snapshot;
            snapshot.mock_mode = false;
            snapshot.runtime_mode = aura::AoratonRuntimeMode::IntegrationTest;
            snapshot.pairing = aura::AoratonPairingState::WaitingForBrowser;
            snapshot.pairing_qr_payload = "https://pair.aura.invalid/#v=1&id=TEST-DEMO&pk=PUBLIC-TEST-ONLY";
            snapshot.pairing_code = "TEST-4821";
            snapshot.pairing_seconds_remaining = 90;
            snapshot.sequence = aoraton.snapshot().sequence + 1000;
            aoraton_capture_override = snapshot;
            ui.apply_aoraton(snapshot);
            ui.show(aura::UiView::AoratonPair);
        } else if (capture_scenario == "aoraton-sas") {
            aura::AoratonSnapshot snapshot;
            snapshot.mock_mode = false;
            snapshot.runtime_mode = aura::AoratonRuntimeMode::IntegrationTest;
            snapshot.pairing = aura::AoratonPairingState::AwaitingSasConfirmation;
            snapshot.pairing_sas = "204 681";
            snapshot.pairing_seconds_remaining = 60;
            snapshot.sequence = aoraton.snapshot().sequence + 1000;
            aoraton_capture_override = snapshot;
            ui.apply_aoraton(snapshot);
            ui.show(aura::UiView::AoratonPair);
        } else if (capture_scenario == "aoraton-chat") {
            aoraton.select_room("demo-family");
            ui.apply_aoraton(aoraton.snapshot());
            ui.show(aura::UiView::AoratonConversation);
        } else if (capture_scenario == "aoraton-confirm") {
            aoraton.select_room("demo-family");
            aoraton.prepare_send("demo-family", text.capture_message);
            ui.apply_aoraton(aoraton.snapshot());
            ui.show(aura::UiView::AoratonConfirm);
        } else if (capture_scenario == "aoraton-compose") {
            aoraton.select_room("demo-family");
            ui.apply_aoraton(aoraton.snapshot());
            ui.show(aura::UiView::AoratonCompose);
        } else if (capture_scenario == "navigation") {
            ui.show(aura::UiView::Navigation);
        } else if (capture_scenario == "weather-sunny" || capture_scenario == "weather-cloudy" ||
                   capture_scenario == "weather-rainy" || capture_scenario == "weather-stormy" ||
                   capture_scenario == "weather-snowy" || capture_scenario == "weather-night" ||
                   capture_scenario == "weather-forecast" || capture_scenario == "weather-gateway" ||
                   capture_scenario == "weather-gateway-details") {
            aura::WeatherCondition condition = aura::WeatherCondition::Clear;
            if (capture_scenario == "weather-cloudy") condition = aura::WeatherCondition::Cloudy;
            else if (capture_scenario == "weather-rainy") condition = aura::WeatherCondition::Rain;
            else if (capture_scenario == "weather-stormy") condition = aura::WeatherCondition::Thunderstorm;
            else if (capture_scenario == "weather-snowy") condition = aura::WeatherCondition::Snow;
            auto data = (capture_scenario == "weather-gateway" || capture_scenario == "weather-gateway-details") ?
                demo_tlesdetector_weather_data() : demo_weather_data(condition);
            if (capture_scenario == "weather-night") data.is_day = false;
            weather_model.clear_location();
            weather_model.fetch_succeeded(data);
            apply_weather();
            ui.show(aura::UiView::Weather);
            capture_weather_forecast = capture_scenario == "weather-forecast" ||
                capture_scenario == "weather-gateway-details";
            if (capture_scenario == "weather-gateway-details") capture_weather_scroll_y = 270;
        } else if (capture_scenario == "weather" || capture_scenario == "weather-success" ||
                   capture_scenario == "weather-from-quick-actions") {
            weather_model.clear_location();
            weather_model.fetch_succeeded(demo_weather_data());
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-loading") {
            weather_model.clear_location();
            weather_model.restore_location(demo_weather_location("Σπάρτη"));
            weather_model.begin_fetch(demo_weather_location("Σπάρτη"), false);
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-no-city") {
            weather_model.clear_location();
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-offline") {
            weather_model.clear_location();
            weather_model.restore_location(demo_weather_location());
            weather_model.begin_fetch(demo_weather_location(), false);
            weather_model.fetch_failed("NETWORK_UNAVAILABLE", true);
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-error") {
            weather_model.clear_location();
            weather_model.restore_location(demo_weather_location());
            weather_model.begin_fetch(demo_weather_location(), false);
            weather_model.fetch_failed("WEATHER_PROVIDER_ERROR", false);
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-location") {
            weather_model.clear_location();
            apply_weather();
            ui.show(aura::UiView::WeatherLocation);
        } else if (capture_scenario == "weather-results") {
            weather_model.clear_location();
            weather_model.begin_search("Σπάρτη");
            weather_model.search_succeeded({demo_weather_location("Σπάρτη"),
                demo_weather_location("Αθήνα"), demo_weather_location("Πάτρα")});
            apply_weather();
            ui.show(aura::UiView::WeatherLocation);
        } else if (capture_scenario == "wifi") {
            ui.show(aura::UiView::Wifi);
        } else if (capture_scenario == "wifi-password") {
            ui.open_wifi_network("Γραφείο 5G · DEMO", aura::WifiSecurity::Wpa3Personal);
        } else if (capture_scenario == "wifi-open") {
            ui.open_wifi_network("Καφέ Αθηνά · DEMO", aura::WifiSecurity::Open);
        } else if (capture_scenario == "wifi-connecting" || capture_scenario == "wifi-failed") {
            wifi_model.begin_connect("Γραφείο 5G · DEMO", aura::WifiSecurity::Wpa3Personal, "demo-only-passphrase");
            if (capture_scenario == "wifi-failed") wifi_model.connect_failed("WRONG_PASSWORD");
            ui.apply_wifi(wifi_model.snapshot());
            ui.show(aura::UiView::Wifi);
        } else if (capture_scenario == "wifi-details") {
            ui.show(aura::UiView::WifiDetails);
        } else if (capture_scenario == "wifi-unavailable") {
            wifi_model.set_radio_available(false, "NO_ADAPTER");
            ui.apply_wifi(wifi_model.snapshot());
            ui.show(aura::UiView::Wifi);
        } else if (capture_scenario == "tools") {
            ui.show(aura::UiView::Tools);
        } else if (capture_scenario == "calendar") {
            ui.show(aura::UiView::Calendar);
        } else if (capture_scenario == "camera") {
            ui.show(aura::UiView::Camera);
        } else if (capture_scenario == "battery") {
            controller.dispatch({aura::EventType::ChargingChanged, {}, 1});
            device_status.set_power(92, true, aura::StatusOrigin::Simulated);
            ui.apply_status(device_status.snapshot());
            apply();
            ui.show(aura::UiView::Battery);
        } else if (capture_scenario == "voice") {
            ui.show(aura::UiView::VoiceSettings);
        } else if (capture_scenario == "typography-proof-1") {
            ui.show_typography_proof(0);
        } else if (capture_scenario == "typography-proof-2") {
            ui.show_typography_proof(1);
        } else if (capture_scenario == "typography-proof-3") {
            ui.show_typography_proof(2);
        } else if (companion.capture(capture_scenario)) {
            apply();
        } else {
            std::cerr << "AURA: unknown capture scenario: " << capture_scenario << '\n';
            return 5;
        }
    }

    if (capture_weather_forecast) {
        lv_obj_update_layout(ui.root());
        lv_obj_t* weather_scroller = nullptr;
        int greatest_bottom = 0;
        bool exact_weather_scroller_found = false;
        std::function<void(lv_obj_t*)> find_weather_scroller = [&](lv_obj_t* object) {
            if (lv_obj_is_scrollable(object)) {
                const int bottom = lv_obj_get_scroll_bottom(object);
                const bool exact_weather_viewport = lv_obj_get_width(object) == 480 &&
                    lv_obj_get_height(object) == 384;
                if (exact_weather_viewport) {
                    greatest_bottom = bottom;
                    weather_scroller = object;
                    exact_weather_scroller_found = true;
                } else if (!exact_weather_scroller_found && bottom > greatest_bottom) {
                    greatest_bottom = bottom;
                    weather_scroller = object;
                }
            }
            const std::uint32_t child_count = lv_obj_get_child_count(object);
            for (std::uint32_t index = 0; index < child_count; ++index) {
                find_weather_scroller(lv_obj_get_child(object, static_cast<int32_t>(index)));
            }
        };
        find_weather_scroller(ui.root());
        if (weather_scroller) lv_obj_scroll_to_y(weather_scroller, capture_weather_scroll_y, LV_ANIM_OFF);
    }

    if (!motion_scenario.empty()) {
        if (motion_directory.empty()) {
            std::cerr << "AURA: --motion-dir is required with --motion-sequence\n";
            return 8;
        }
        std::filesystem::create_directories(motion_directory);
        controller.dispatch({aura::EventType::Reset});
        if (motion_scenario == "home") {
            apply();
            ui.show(aura::UiView::Home);
        } else if (motion_scenario == "listening") {
            controller.dispatch({aura::EventType::WakeDetected});
            controller.dispatch({aura::EventType::BeginRecording});
            apply();
            ui.set_input_amplitude(0.45F, true);
        } else if (motion_scenario == "thinking") {
            controller.dispatch({aura::EventType::TurnStarted, text.capture_thinking_prompt});
            apply();
        } else if (motion_scenario == "speaking") {
            controller.dispatch({aura::EventType::TurnStarted, text.capture_speaking_prompt});
            controller.dispatch({aura::EventType::FinalResult, text.capture_speaking_answer});
            apply();
            audio.play_test_tone(5000);
            ui.apply_audio(audio.diagnostics());
        } else if (motion_scenario == "weather-clear" || motion_scenario == "weather-cloudy" ||
                   motion_scenario == "weather-rain" || motion_scenario == "weather-storm" ||
                   motion_scenario == "weather-snow" || motion_scenario == "weather-night" ||
                   motion_scenario == "weather-wind") {
            aura::WeatherCondition condition = aura::WeatherCondition::Clear;
            if (motion_scenario == "weather-cloudy") condition = aura::WeatherCondition::Cloudy;
            else if (motion_scenario == "weather-rain") condition = aura::WeatherCondition::Rain;
            else if (motion_scenario == "weather-storm") condition = aura::WeatherCondition::Thunderstorm;
            else if (motion_scenario == "weather-snow") condition = aura::WeatherCondition::Snow;
            else if (motion_scenario == "weather-wind") condition = aura::WeatherCondition::Windy;
            auto data = demo_weather_data(condition);
            if (motion_scenario == "weather-night") data.is_day = false;
            weather_model.clear_location();
            weather_model.fetch_succeeded(data);
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else {
            std::cerr << "AURA: unknown motion scenario: " << motion_scenario << '\n';
            return 9;
        }
    }

    if (ui_stress) {
        lv_mem_monitor_t memory_before{};
        lv_mem_monitor(&memory_before);
        aoraton.select_room("demo-family");
        ui.apply_aoraton(aoraton_capture_override ? *aoraton_capture_override : aoraton.snapshot());
        const aura::UiView views[] = {
            aura::UiView::Home, aura::UiView::Launcher, aura::UiView::Cards,
            aura::UiView::Notifications, aura::UiView::Settings, aura::UiView::Audio,
            aura::UiView::AoratonRooms, aura::UiView::AoratonPair, aura::UiView::AoratonConversation,
            aura::UiView::AoratonCompose, aura::UiView::AoratonConfirm, aura::UiView::Navigation,
            aura::UiView::Weather, aura::UiView::WeatherLocation, aura::UiView::Tools, aura::UiView::Calendar,
            aura::UiView::Camera, aura::UiView::Battery, aura::UiView::VoiceSettings,
            aura::UiView::Diagnostics, aura::UiView::Wifi, aura::UiView::WifiPassword,
            aura::UiView::WifiDetails, aura::UiView::Clock, aura::UiView::ClockNew, aura::UiView::TimerNew,
            aura::UiView::AlarmNew, aura::UiView::ReminderNew, aura::UiView::Notes, aura::UiView::NoteNew,
            aura::UiView::Briefing, aura::UiView::Translate, aura::UiView::PhoneLink,
            aura::UiView::DisplaySettings, aura::UiView::Update, aura::UiView::Home,
        };
        for (int pass = 0; pass < 10; ++pass) {
            for (const auto view : views) ui.show(view);
        }
        ui.show(aura::UiView::Home);
        lv_mem_monitor_t memory_after{};
        lv_mem_monitor(&memory_after);
        const auto used_before = memory_before.total_size - memory_before.free_size;
        const auto used_after = memory_after.total_size - memory_after.free_size;
        std::cout << "AURA UI stress: " << (std::size(views) * 10)
                  << " shared-LVGL view transitions completed; LVGL used "
                  << used_before << " -> " << used_after << " bytes; fragmentation "
                  << static_cast<int>(memory_after.frag_pct) << "%\n";
    }

    if (aoraton_pairing_smoke) {
        if (aoraton.snapshot().runtime_mode != aura::AoratonRuntimeMode::IntegrationTest ||
            !aoraton_fail_closed_transport) {
            std::cerr << "AURA: --aoraton-pairing-smoke requires AURA_AORATON_MODE=test\n";
            return 13;
        }
        ui.apply_aoraton(aoraton.snapshot());
        ui.show(aura::UiView::AoratonRooms);
        auto pump_pairing = [&](int iterations) {
            for (int iteration = 0; iteration < iterations; ++iteration) {
                aoraton_fail_closed_transport->poll();
                aoraton.tick(8);
                ui.apply_aoraton(aoraton.snapshot());
                ui.tick(8);
                lv_timer_handler();
                SDL_Delay(6);
            }
        };
        auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(display));
        auto* window = renderer ? SDL_RenderGetWindow(renderer) : nullptr;
        const Uint32 window_id = window ? SDL_GetWindowID(window) : 0;
        auto push_pairing_pointer = [&](Uint32 type) {
            SDL_Event event{};
            event.type = type;
            if (type == SDL_MOUSEMOTION) {
                event.motion.windowID = window_id;
                event.motion.x = static_cast<int>(240.0F * window_scale);
                event.motion.y = static_cast<int>(408.0F * window_scale);
            } else {
                event.button.windowID = window_id;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
                event.button.x = static_cast<int>(240.0F * window_scale);
                event.button.y = static_cast<int>(408.0F * window_scale);
            }
            SDL_PushEvent(&event);
        };
        pump_pairing(5);
        push_pairing_pointer(SDL_MOUSEMOTION);
        push_pairing_pointer(SDL_MOUSEBUTTONDOWN);
        pump_pairing(4);
        push_pairing_pointer(SDL_MOUSEBUTTONUP);
        pump_pairing(12);
        const bool passed = ui.view() == aura::UiView::AoratonPair &&
            aoraton.snapshot().pairing == aura::AoratonPairingState::Error &&
            aoraton.snapshot().last_error == "PAIRING_TEST_BACKEND_NOT_CONFIGURED";
        std::cout << "AURA Aoraton pairing pointer smoke: " << (passed ? "PASS" : "FAIL") << '\n';
        if (!passed) return 14;
    }

    if (!interaction_report.empty()) {
        struct InteractionResult {
            std::string action;
            std::string point;
            bool passed;
            std::string observed;
        };
        std::vector<InteractionResult> results;
        auto record = [&](std::string action, std::string point, bool passed, std::string observed) {
            results.push_back({std::move(action), std::move(point), passed, std::move(observed)});
        };
        auto pump = [&](int iterations = 9) {
            for (int i = 0; i < iterations; ++i) {
                audio.tick(8);
                if (aoraton_fail_closed_transport) aoraton_fail_closed_transport->poll();
                aoraton.tick(8);
                poll_wifi(std::chrono::steady_clock::now());
                apply();
                ui.apply_audio(audio.diagnostics());
                ui.apply_aoraton(aoraton.snapshot());
                ui.apply_status(device_status.snapshot());
                ui.apply_weather(weather_model.snapshot());
                ui.apply_wifi(wifi_model.snapshot());
                companion.poll(8);
                companion.apply();
                if (controller.snapshot().state == aura::AssistantState::Listening) {
                    ui.set_input_amplitude(0.48F + static_cast<float>(i % 3) * 0.12F, true);
                }
                ui.tick(8);
                lv_timer_handler();
                SDL_Delay(6);
            }
        };
        auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(display));
        auto* window = renderer ? SDL_RenderGetWindow(renderer) : nullptr;
        const Uint32 window_id = window ? SDL_GetWindowID(window) : 0;
        // The scripted taps carry their own mouse id; a real mouse moving over the
        // test window (likely at 2x) must not mix with them, so it is filtered out.
        constexpr Uint32 kScriptedMouse = 0x5AFE;
        SDL_SetEventFilter([](void*, SDL_Event* event) -> int {
            switch (event->type) {
                case SDL_MOUSEMOTION: return event->motion.which == kScriptedMouse ? 1 : 0;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP: return event->button.which == kScriptedMouse ? 1 : 0;
                case SDL_MOUSEWHEEL: return 0;
                default: return 1;
            }
        }, nullptr);
        auto push_motion = [&](int x, int y, Uint32 state = 0) {
            SDL_Event event{};
            event.type = SDL_MOUSEMOTION;
            event.motion.which = kScriptedMouse;
            event.motion.windowID = window_id;
            event.motion.x = static_cast<int>(static_cast<float>(x) * window_scale);
            event.motion.y = static_cast<int>(static_cast<float>(y) * window_scale);
            event.motion.state = state;
            SDL_PushEvent(&event);
        };
        auto push_button = [&](Uint32 type, int x, int y) {
            SDL_Event event{};
            event.type = type;
            event.button.which = kScriptedMouse;
            event.button.windowID = window_id;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
            event.button.x = static_cast<int>(static_cast<float>(x) * window_scale);
            event.button.y = static_cast<int>(static_cast<float>(y) * window_scale);
            SDL_PushEvent(&event);
        };
        auto tap = [&](int x, int y) {
            push_motion(x, y);
            push_button(SDL_MOUSEBUTTONDOWN, x, y);
            pump(5);
            push_button(SDL_MOUSEBUTTONUP, x, y);
            pump(8);
        };
        auto long_press = [&](int x, int y) {
            push_motion(x, y);
            push_button(SDL_MOUSEBUTTONDOWN, x, y);
            pump(70);
            push_button(SDL_MOUSEBUTTONUP, x, y);
            pump(8);
        };
        auto rapid_double_tap = [&](int x, int y) {
            push_motion(x, y);
            push_button(SDL_MOUSEBUTTONDOWN, x, y); pump(2);
            push_button(SDL_MOUSEBUTTONUP, x, y); pump(2);
            push_button(SDL_MOUSEBUTTONDOWN, x, y); pump(2);
            push_button(SDL_MOUSEBUTTONUP, x, y); pump(8);
        };
        lv_obj_t* last_drag_scroll_object = nullptr;
        int last_drag_scroll_y = 0;
        int last_drag_scroll_x = 0;
        auto drag = [&](int x1, int y1, int x2, int y2) {
            last_drag_scroll_object = nullptr;
            last_drag_scroll_y = 0;
            last_drag_scroll_x = 0;
            push_motion(x1, y1);
            pump(3);
            push_button(SDL_MOUSEBUTTONDOWN, x1, y1);
            pump(5);
            for (int step = 1; step <= 12; ++step) {
                const int x = x1 + (x2 - x1) * step / 12;
                const int y = y1 + (y2 - y1) * step / 12;
                push_motion(x, y, SDL_BUTTON_LMASK);
                pump(3);
                last_drag_scroll_object = lv_indev_get_scroll_obj(mouse);
                if (last_drag_scroll_object) {
                    last_drag_scroll_y = lv_obj_get_scroll_y(last_drag_scroll_object);
                    last_drag_scroll_x = lv_obj_get_scroll_x(last_drag_scroll_object);
                }
            }
            push_button(SDL_MOUSEBUTTONUP, x2, y2);
            pump(3);
        };
        // A finger swipe: movement on every input read, as LVGL's gesture
        // detector requires (a pause between reads restarts its distance).
        auto swipe = [&](int x1, int y1, int x2, int y2) {
            push_motion(x1, y1);
            pump(2);
            push_button(SDL_MOUSEBUTTONDOWN, x1, y1);
            pump(2);
            for (int step = 1; step <= 10; ++step) {
                push_motion(x1 + (x2 - x1) * step / 10, y1 + (y2 - y1) * step / 10, SDL_BUTTON_LMASK);
                pump(1);
            }
            push_button(SDL_MOUSEBUTTONUP, x2, y2);
            pump(6);
        };
        // Every check starts from a fresh render: re-showing the same view would
        // keep its scroll position, so one lost tap could cascade.
        auto reset_to = [&](aura::UiView view) {
            controller.dispatch({aura::EventType::Reset});
            apply();
            if (ui.view() == view && view != aura::UiView::Home) ui.show(aura::UiView::Home);
            ui.show(view);
            pump(4);
        };
        std::function<lv_obj_t*(lv_obj_t*)> find_scrollable = [&](lv_obj_t* root) -> lv_obj_t* {
            if (lv_obj_is_scrollable(root) && lv_obj_get_scroll_bottom(root) > 0) return root;
            const std::uint32_t children = lv_obj_get_child_count(root);
            for (std::uint32_t index = 0; index < children; ++index) {
                if (auto* found = find_scrollable(lv_obj_get_child(root, static_cast<int32_t>(index)))) return found;
            }
            return nullptr;
        };
        std::function<lv_obj_t*(lv_obj_t*, int, int)> find_sized_clickable =
            [&](lv_obj_t* root, int width, int height) -> lv_obj_t* {
                if (lv_obj_is_clickable(root) && lv_obj_get_width(root) == width &&
                    lv_obj_get_height(root) == height) return root;
                const std::uint32_t children = lv_obj_get_child_count(root);
                for (std::uint32_t index = 0; index < children; ++index) {
                    if (auto* found = find_sized_clickable(
                            lv_obj_get_child(root, static_cast<int32_t>(index)), width, height)) return found;
                }
                return nullptr;
            };

        struct Point { const char* name; int x; int y; };
        // v12 rows: 432x88 platters at x=24; the icon well is centred at x=66
        // and the title starts at x=108.
        auto row_points = [](int y) {
            return std::array<Point, 4>{{
                {"icon-center", 66, y}, {"visible-edge", 32, y},
                {"label-center", 220, y}, {"icon-label-gap", 100, y}
            }};
        };
        // Apps grid: 3x2 cells of 144x164 from (24,104); 104 px circles centred
        // at y=164/336 with their labels 74 px below the circle centre.
        auto app_points = [](int x, int y) {
            return std::array<Point, 4>{{
                {"icon-center", x, y}, {"icon-edge", x - 46, y},
                {"label-center", x, y + 74}, {"icon-label-gap", x, y + 56}
            }};
        };
        const auto left_top = app_points(96, 164);
        const auto right_top = app_points(240, 164);
        const auto left_middle = app_points(384, 164);
        const auto right_middle = app_points(96, 336);
        const auto left_bottom = app_points(240, 336);
        const auto right_bottom = app_points(384, 336);

        auto test_launcher_route = [&](const char* action, const Point* points, aura::UiView expected, bool assistant) {
            for (int index = 0; index < 4; ++index) {
                reset_to(aura::UiView::Launcher);
                const int before = interaction_counts.ask;
                tap(points[index].x, points[index].y);
                const bool passed = assistant ?
                    (interaction_counts.ask == before + 1 && ui.view() == aura::UiView::Assistant &&
                     controller.snapshot().state == aura::AssistantState::Listening) :
                    (ui.view() == expected && interaction_counts.ask == before);
                record(action, points[index].name, passed,
                       std::string("view=") + std::to_string(static_cast<int>(ui.view())));
            }
        };
        test_launcher_route("launcher.assistant", left_top.data(), aura::UiView::Assistant, true);
        for (int index = 0; index < 4; ++index) {
            weather_model.clear_location();
            apply_weather();
            reset_to(aura::UiView::Launcher);
            const int before = interaction_counts.weather_open;
            tap(right_top[index].x, right_top[index].y);
            record("launcher.weather", right_top[index].name,
                   interaction_counts.weather_open == before + 1 && ui.view() == aura::UiView::Weather &&
                   weather_model.snapshot().state == aura::WeatherState::Unconfigured,
                   aura::weather_state_name(weather_model.snapshot().state));
        }
        test_launcher_route("launcher.aoraton", left_middle.data(), aura::UiView::AoratonRooms, false);
        test_launcher_route("launcher.clock", right_middle.data(), aura::UiView::Clock, false);
        test_launcher_route("launcher.notes", left_bottom.data(), aura::UiView::Notes, false);
        test_launcher_route("launcher.translate", right_bottom.data(), aura::UiView::Translate, false);

        // The 200 px halo around the orb (centre 240,260) is the talk button.
        const Point home_ask[]{{"orb-center",240,260},{"halo-edge",152,260},{"orb-top",240,176},{"orb-bottom",240,344}};
        for (const auto& point : home_ask) {
            reset_to(aura::UiView::Home);
            const int before = interaction_counts.ask;
            tap(point.x, point.y);
            record("home.ask", point.name,
                   interaction_counts.ask == before + 1 && controller.snapshot().state == aura::AssistantState::Listening,
                   aura::state_name(controller.snapshot().state));
        }
        reset_to(aura::UiView::Home);
        tap(424, 436);
        record("home.launcher", "icon-center", ui.view() == aura::UiView::Launcher,
               std::to_string(static_cast<int>(ui.view())));
        lv_obj_update_layout(ui.root());
        if (auto* grid = find_scrollable(ui.root())) lv_obj_scroll_to_y(grid, 148, LV_ANIM_OFF);
        pump(3);
        tap(384, 360);
        record("launcher.settings", "icon-center", ui.view() == aura::UiView::Settings,
               std::to_string(static_cast<int>(ui.view())));
        reset_to(aura::UiView::Home);
        swipe(240, 400, 240, 150);
        record("home.swipe_up", "apps", ui.view() == aura::UiView::Launcher,
               std::to_string(static_cast<int>(ui.view())));
        reset_to(aura::UiView::Home);
        swipe(240, 140, 240, 400);
        record("home.swipe_down", "notifications", ui.view() == aura::UiView::Notifications,
               std::to_string(static_cast<int>(ui.view())));
        reset_to(aura::UiView::Launcher);
        tap(96, 336);
        swipe(60, 300, 420, 300);
        record("nav.swipe_back", "clock-screen", ui.view() == aura::UiView::Launcher,
               std::to_string(static_cast<int>(ui.view())));

        // Settings is a scrolling list: rows are 88 px high on a 96 px pitch
        // starting at y=96 (Wi-Fi, provider, motion, weather, sound, status, system).
        auto settings_row_y = [](int index, int scroll) { return 96 + index * 96 + 44 - scroll; };
        auto scroll_view_to = [&](int y) {
            lv_obj_update_layout(ui.root());
            if (auto* scroller = find_scrollable(ui.root())) lv_obj_scroll_to_y(scroller, y, LV_ANIM_OFF);
            pump(3);
        };
        reset_to(aura::UiView::Settings);
        const int rapid_provider_before = interaction_counts.provider;
        rapid_double_tap(220, settings_row_y(1, 0));
        record("settings.provider", "rapid-double-tap",
               interaction_counts.provider == rapid_provider_before + 1,
               std::to_string(interaction_counts.provider - rapid_provider_before));

        controller.dispatch({aura::EventType::Reset});
        controller.dispatch({aura::EventType::WakeDetected});
        controller.dispatch({aura::EventType::BeginRecording});
        apply(); pump(4);
        const int finish_before = interaction_counts.finish_listening;
        tap(128, 420);
        record("assistant.finish_listening", "button-center",
               interaction_counts.finish_listening == finish_before + 1 &&
               controller.snapshot().state == aura::AssistantState::Transcribing,
               aura::state_name(controller.snapshot().state));
        controller.dispatch({aura::EventType::Reset});
        controller.dispatch({aura::EventType::WakeDetected});
        controller.dispatch({aura::EventType::BeginRecording});
        apply(); pump(4);
        const int listening_cancel_before = interaction_counts.cancel;
        tap(352, 420);
        record("assistant.cancel", "listening-button",
               interaction_counts.cancel == listening_cancel_before + 1 &&
               controller.snapshot().state == aura::AssistantState::Idle,
               aura::state_name(controller.snapshot().state));

        controller.dispatch({aura::EventType::TurnStarted, text.capture_short_prompt});
        controller.dispatch({aura::EventType::FinalResult, text.capture_short_answer});
        apply(); pump(4);
        const int ask_again_before = interaction_counts.ask;
        tap(240, 420);
        record("assistant.ask_again", "button-center",
               interaction_counts.ask == ask_again_before + 1 &&
               controller.snapshot().state == aura::AssistantState::Listening,
               aura::state_name(controller.snapshot().state));

        reset_to(aura::UiView::Launcher);
        tap(96, 336);
        tap(48, 48);
        record("nav.back", "center", ui.view() == aura::UiView::Launcher,
               std::to_string(static_cast<int>(ui.view())));
        tap(96, 336);
        // Long-pressing Back goes straight home.
        long_press(48, 48);
        record("nav.home", "back-long-press", ui.view() == aura::UiView::Home,
               std::to_string(static_cast<int>(ui.view())));

        for (const auto& point : row_points(settings_row_y(1, 0))) {
            reset_to(aura::UiView::Settings);
            const int provider_before = interaction_counts.provider;
            const bool live_before = live_provider;
            tap(point.x, point.y);
            record("settings.provider", point.name,
                   interaction_counts.provider == provider_before + 1 && live_provider != live_before,
                   live_provider ? "codex" : "mock");
        }
        for (const auto& point : row_points(settings_row_y(2, 0))) {
            reset_to(aura::UiView::Settings);
            const bool reduced_before = ui.reduced_motion();
            tap(point.x, point.y);
            record("settings.motion", point.name, ui.reduced_motion() != reduced_before,
                   ui.reduced_motion() ? "reduced" : "full");
        }
        // The last three rows are reached by scrolling the list to its end (296 px).
        const struct { const char* action; int index; int scroll; aura::UiView expected; } setting_routes[] = {
            {"settings.weather", 3, 0, aura::UiView::WeatherLocation},
            {"settings.audio", 4, 296, aura::UiView::Audio},
            {"settings.status", 5, 296, aura::UiView::Battery},
            {"settings.diagnostics", 6, 296, aura::UiView::Diagnostics},
        };
        for (const auto& route : setting_routes) {
            for (const auto& point : row_points(settings_row_y(route.index, route.scroll))) {
                reset_to(aura::UiView::Settings);
                if (route.scroll > 0) scroll_view_to(route.scroll);
                tap(point.x, point.y);
                record(route.action, point.name, ui.view() == route.expected,
                       std::to_string(static_cast<int>(ui.view())));
            }
        }
        for (const auto& point : row_points(settings_row_y(0, 0))) {
            reset_to(aura::UiView::Settings);
            const int wifi_open_before = interaction_counts.wifi_open;
            tap(point.x, point.y);
            record("settings.wifi", point.name,
                   ui.view() == aura::UiView::Wifi && interaction_counts.wifi_open == wifi_open_before + 1,
                   std::to_string(static_cast<int>(ui.view())));
        }

        // Wi-Fi (DEMO networks): status-bar shortcut, open/unsupported/secured
        // networks, local password validation, simulated join and forget.
        reset_to(aura::UiView::Home);
        tap(34, 20);
        record("status.wifi_shortcut", "icon-center", ui.view() == aura::UiView::Wifi,
               std::to_string(static_cast<int>(ui.view())));
        // One scroller below the header: the status card, then network rows from
        // y=236 on a 96 px pitch (connected network excluded): Καφέ Αθηνά (open),
        // Γραφείο 5G (WPA3), Corp Secure (enterprise), Guest.
        reset_to(aura::UiView::Wifi);
        tap(240, 280);
        record("wifi.open_network", "row-center", ui.view() == aura::UiView::WifiPassword,
               std::to_string(static_cast<int>(ui.view())));
        reset_to(aura::UiView::Wifi);
        scroll_view_to(96);
        tap(240, 376);
        record("wifi.unsupported_network", "row-center",
               ui.view() == aura::UiView::Wifi && wifi_model.snapshot().link == aura::WifiLinkState::Connected,
               aura::wifi_link_state_name(wifi_model.snapshot().link));
        reset_to(aura::UiView::Wifi);
        // Re-showing the same view keeps its scroll position; start at the top.
        scroll_view_to(0);
        tap(240, 376);
        record("wifi.secured_network", "row-center", ui.view() == aura::UiView::WifiPassword,
               std::to_string(static_cast<int>(ui.view())));
        // The header's check button submits; an empty password is refused locally.
        const int wifi_connect_before = interaction_counts.wifi_connect;
        tap(432, 48);
        record("wifi.password_validation", "empty-password",
               ui.view() == aura::UiView::WifiPassword && interaction_counts.wifi_connect == wifi_connect_before,
               std::to_string(interaction_counts.wifi_connect - wifi_connect_before));
        // Type "qwertyui" on the full-width Latin password layout (row 1, y=243).
        for (int key = 0; key < 8; ++key) tap(25 + key * 477 / 10, 243);
        tap(432, 48);
        pump(4);
        const auto& joined = wifi_model.snapshot();
        record("wifi.connect", "typed-password",
               interaction_counts.wifi_connect == wifi_connect_before + 1 && ui.view() == aura::UiView::Wifi &&
               joined.link == aura::WifiLinkState::Connected && joined.connected_ssid == "Γραφείο 5G · DEMO" &&
               joined.link_origin == aura::WifiOrigin::Simulated,
               aura::wifi_link_state_name(joined.link));
        tap(240, 140);
        record("wifi.details", "status-card", ui.view() == aura::UiView::WifiDetails,
               std::to_string(static_cast<int>(ui.view())));
        const int wifi_forget_before = interaction_counts.wifi_forget;
        tap(240, 420);
        const bool armed_only = interaction_counts.wifi_forget == wifi_forget_before &&
                                ui.view() == aura::UiView::WifiDetails;
        tap(240, 420);
        record("wifi.forget", "two-step-confirm",
               armed_only && interaction_counts.wifi_forget == wifi_forget_before + 1 &&
               wifi_model.snapshot().saved_ssid.empty() && ui.view() == aura::UiView::Wifi,
               wifi_model.snapshot().saved_ssid.empty() ? "forgotten" : "kept");
        // Later checks expect the online DEMO link.
        seed_demo_wifi();
        sync_device_link();
        apply();
        ui.apply_wifi(wifi_model.snapshot());

        reset_to(aura::UiView::Settings);
        const bool persisted_motion_before = ui.reduced_motion();
        tap(184, settings_row_y(2, 0));
        ui.show(aura::UiView::Home);
        ui.show(aura::UiView::Settings);
        pump(3);
        record("settings.motion_persistence", "leave-reopen",
               ui.reduced_motion() != persisted_motion_before,
               ui.reduced_motion() ? "reduced persisted" : "full persisted");

        weather_model.clear_location();
        apply_weather();
        reset_to(aura::UiView::Weather);
        tap(240, 420);
        record("weather.choose_city", "empty-state-cta",
               ui.view() == aura::UiView::WeatherLocation,
               std::to_string(static_cast<int>(ui.view())));
        // Presets are a 2x2 grid of 208x80 tiles from (24,232); Athens is first.
        const int weather_search_before = interaction_counts.weather_search;
        tap(128, 272);
        record("weather.city_preset", "athens-button",
               interaction_counts.weather_search == weather_search_before + 1 &&
               weather_model.snapshot().state == aura::WeatherState::SearchResults,
               aura::weather_state_name(weather_model.snapshot().state));
        const int weather_select_before = interaction_counts.weather_select;
        tap(240, 176);
        record("weather.select_result", "first-result-row",
               interaction_counts.weather_select == weather_select_before + 1 &&
               ui.view() == aura::UiView::Weather &&
               weather_model.snapshot().state == aura::WeatherState::Ready &&
               weather_model.snapshot().has_saved_location,
               aura::weather_state_name(weather_model.snapshot().state));
        const int weather_refresh_before = interaction_counts.weather_refresh;
        tap(432, 48);
        record("weather.refresh", "icon-center",
               interaction_counts.weather_refresh == weather_refresh_before + 1 &&
               weather_model.snapshot().state == aura::WeatherState::Ready,
               aura::weather_state_name(weather_model.snapshot().state));
        tap(120, 120);
        record("weather.change_city", "city-pill",
               ui.view() == aura::UiView::WeatherLocation,
               std::to_string(static_cast<int>(ui.view())));

        weather_model.clear_location();
        weather_model.fetch_succeeded(demo_weather_data(aura::WeatherCondition::Rain));
        apply_weather();
        ui.show(aura::UiView::Weather); pump(4);
        drag(238, 426, 238, 174);
        record("weather.vertical_scroll", "forecast-content",
               last_drag_scroll_object != nullptr && last_drag_scroll_y > 100,
               std::to_string(last_drag_scroll_y));
        // Stop elastic/inertial travel before targeting the hourly strip. The drag
        // above is the gesture assertion; this deterministic position isolates the
        // following tap and horizontal-swipe assertions from host timing variance.
        if (last_drag_scroll_object) {
            lv_anim_delete(last_drag_scroll_object, nullptr);
            lv_obj_scroll_to_y(last_drag_scroll_object, 252, LV_ANIM_OFF);
            pump(12);
        }
        lv_obj_update_layout(ui.root());
        auto* first_hour_card = find_sized_clickable(ui.root(), 120, 164);
        lv_area_t hour_bounds{};
        if (first_hour_card) lv_obj_get_coords(first_hour_card, &hour_bounds);
        const auto forecast_before = ui.weather_forecast_interaction_count();
        tap(first_hour_card ? (hour_bounds.x1 + hour_bounds.x2) / 2 : 72,
            first_hour_card ? (hour_bounds.y1 + hour_bounds.y2) / 2 : 272);
        record("weather.hour_detail", "first-hour-card",
               first_hour_card && ui.weather_forecast_interaction_count() == forecast_before + 1 &&
               ui.view() == aura::UiView::Weather,
               std::to_string(ui.weather_forecast_interaction_count() - forecast_before));
        lv_area_t strip_bounds{};
        auto* hourly_strip = first_hour_card ? lv_obj_get_parent(first_hour_card) : nullptr;
        if (hourly_strip) lv_obj_get_coords(hourly_strip, &strip_bounds);
        const int strip_y = hourly_strip ? (strip_bounds.y1 + strip_bounds.y2) / 2 : 272;
        drag(hourly_strip ? std::min<int>(strip_bounds.x2 - 24, 390) : 375, strip_y,
             hourly_strip ? std::max<int>(strip_bounds.x1 + 24, 90) : 105, strip_y);
        record("weather.hourly_swipe", "horizontal-strip",
               hourly_strip && last_drag_scroll_object != nullptr && last_drag_scroll_x > 20,
               std::to_string(last_drag_scroll_x));

        weather_model.clear_location();
        apply_weather();
        reset_to(aura::UiView::WeatherLocation);
        // The search field opens the keyboard (y=184..480, 4 rows of ~68 px):
        // row 1 y=222 (θ), row 2 y=295 (α, η), row 3 y=369 (ν).
        tap(240, 140);
        tap(25, 295);  // Α (the empty field starts with a capital)
        tap(312, 222); // θ
        tap(264, 295); // η
        tap(336, 369); // ν
        tap(25, 295);  // α
        const int typed_search_before = interaction_counts.weather_search;
        tap(432, 48);
        record("weather.city_search", "typed-query-button",
               interaction_counts.weather_search == typed_search_before + 1 &&
               weather_model.snapshot().state == aura::WeatherState::SearchResults,
               aura::weather_state_name(weather_model.snapshot().state));

        weather_model.clear_location();
        weather_model.restore_location(demo_weather_location());
        weather_model.fetch_succeeded(demo_weather_data());
        apply_weather();
        reset_to(aura::UiView::Launcher);
        tap(240, 164);
        tap(48, 48);
        record("weather.back", "header-button",
               ui.view() == aura::UiView::Launcher,
               std::to_string(static_cast<int>(ui.view())));

        ui.show(aura::UiView::Weather);
        pump(3);
        long_press(48, 48);
        record("weather.home", "back-long-press",
               ui.view() == aura::UiView::Home,
               std::to_string(static_cast<int>(ui.view())));

        reset_to(aura::UiView::Settings);
        tap(184, settings_row_y(3, 0));
        tap(48, 48);
        record("weather_location.back", "header-button",
               ui.view() == aura::UiView::Settings,
               std::to_string(static_cast<int>(ui.view())));

        ui.show(aura::UiView::WeatherLocation);
        pump(3);
        long_press(48, 48);
        record("weather_location.home", "back-long-press",
               ui.view() == aura::UiView::Home,
               std::to_string(static_cast<int>(ui.view())));

        weather_model.clear_location();
        weather_model.restore_location(demo_weather_location());
        weather_model.begin_fetch(demo_weather_location(), false);
        weather_model.fetch_failed("NETWORK_UNAVAILABLE", true);
        apply_weather();
        ui.show(aura::UiView::Weather); pump(3);
        const int weather_retry_offline = interaction_counts.weather_refresh;
        tap(240, 420);
        record("weather.retry", "offline-cta",
               interaction_counts.weather_refresh == weather_retry_offline + 1 &&
               weather_model.snapshot().state == aura::WeatherState::Ready,
               aura::weather_state_name(weather_model.snapshot().state));

        weather_model.clear_location();
        weather_model.restore_location(demo_weather_location());
        weather_model.begin_fetch(demo_weather_location(), false);
        weather_model.fetch_failed("WEATHER_PROVIDER_ERROR", false);
        apply_weather();
        ui.show(aura::UiView::Weather); pump(3);
        const int weather_retry_error = interaction_counts.weather_refresh;
        tap(240, 420);
        record("weather.retry", "error-cta",
               interaction_counts.weather_refresh == weather_retry_error + 1 &&
               weather_model.snapshot().state == aura::WeatherState::Ready,
               aura::weather_state_name(weather_model.snapshot().state));

        weather_model.clear_location();
        apply_weather();
        start_turn("Πες μου τον καιρό στη Σπάρτη");
        pump(3);
        record("assistant.weather_query", "explicit-city",
               ui.view() == aura::UiView::Weather &&
               weather_model.snapshot().state == aura::WeatherState::Ready &&
               !weather_model.snapshot().has_saved_location,
               aura::weather_state_name(weather_model.snapshot().state));

        weather_model.clear_location();
        apply_weather();
        start_turn("Άλλαξε πόλη σε Αθήνα");
        pump(3);
        record("assistant.weather_change_city", "confirmation-required",
               ui.view() == aura::UiView::WeatherLocation &&
               weather_model.snapshot().state == aura::WeatherState::SearchResults &&
               !weather_model.snapshot().has_saved_location,
               aura::weather_state_name(weather_model.snapshot().state));

        for (const auto& point : row_points(140)) {
            controller.dispatch({aura::EventType::NotificationsCleared});
            controller.dispatch({aura::EventType::NotificationReceived, "DEMO"});
            apply(); ui.show(aura::UiView::Notifications); pump(3);
            tap(point.x, point.y);
            record("notifications.open", point.name, ui.view() == aura::UiView::AoratonRooms,
                   std::to_string(static_cast<int>(ui.view())));
        }
        controller.dispatch({aura::EventType::NotificationReceived, "DEMO"});
        apply(); ui.show(aura::UiView::Notifications); pump(3);
        const int clear_before = interaction_counts.notifications_clear;
        tap(240, 420);
        record("notifications.clear", "button-center",
               interaction_counts.notifications_clear == clear_before + 1 && controller.snapshot().notification_count == 0,
               std::to_string(controller.snapshot().notification_count));

        reset_to(aura::UiView::Audio);
        const int tone_before = interaction_counts.audio_tone;
        tap(128, 144);
        record("audio.tone", "button-center", interaction_counts.audio_tone == tone_before + 1,
               aura::audio_state_name(audio.diagnostics().state));
        const int wav_before = interaction_counts.audio_wav;
        tap(352, 144);
        record("audio.wav", "button-center", interaction_counts.audio_wav == wav_before + 1,
               aura::audio_state_name(audio.diagnostics().state));
        const int pause_before = interaction_counts.audio_pause;
        tap(124, 388);
        record("audio.play_pause", "button-center", interaction_counts.audio_pause == pause_before + 1,
               aura::audio_state_name(audio.diagnostics().state));
        const int stop_before = interaction_counts.audio_stop;
        tap(240, 388);
        record("audio.stop", "button-center", interaction_counts.audio_stop == stop_before + 1,
               aura::audio_state_name(audio.diagnostics().state));
        const bool muted_before = audio.diagnostics().muted;
        tap(356, 388);
        record("audio.mute", "button-center", audio.diagnostics().muted != muted_before,
               audio.diagnostics().muted ? "muted" : "unmuted");
        drag(120, 452, 400, 452);
        record("audio.volume", "slider-drag", audio.diagnostics().volume > 0.75F,
               std::to_string(audio.diagnostics().volume));

        controller.dispatch({aura::EventType::Reset});
        controller.dispatch({aura::EventType::TurnStarted, text.capture_speaking_prompt});
        controller.dispatch({aura::EventType::FinalResult, text.capture_speaking_answer});
        audio.play_test_tone(2600);
        apply(); ui.apply_audio(audio.diagnostics()); pump(4);
        const int speaking_pause_before = interaction_counts.audio_pause;
        tap(124, 416);
        record("speaking.pause", "icon-center",
               interaction_counts.audio_pause == speaking_pause_before + 1 &&
               audio.diagnostics().state == aura::AudioState::Paused,
               aura::audio_state_name(audio.diagnostics().state));
        const bool speaking_muted_before = audio.diagnostics().muted;
        tap(240, 416);
        record("speaking.mute", "icon-center",
               audio.diagnostics().muted != speaking_muted_before,
               audio.diagnostics().muted ? "muted" : "unmuted");
        const int speaking_stop_before = interaction_counts.audio_stop;
        tap(356, 416);
        record("speaking.stop", "icon-center",
               interaction_counts.audio_stop == speaking_stop_before + 1 &&
               audio.diagnostics().state != aura::AudioState::Playing &&
               audio.diagnostics().state != aura::AudioState::Paused,
               aura::audio_state_name(audio.diagnostics().state));

        // Room rows start below the security caption (first row centre y=180).
        for (const auto& point : row_points(180)) {
            aoraton.reset_mock();
            ui.apply_aoraton(aoraton.snapshot());
            reset_to(aura::UiView::AoratonRooms);
            const int room_before = interaction_counts.aoraton_room;
            tap(point.x, point.y);
            record("aoraton.room", point.name,
                   interaction_counts.aoraton_room == room_before + 1 && ui.view() == aura::UiView::AoratonConversation,
                   std::to_string(static_cast<int>(ui.view())));
        }
        aoraton.reset_mock();
        ui.apply_aoraton(aoraton.snapshot());
        reset_to(aura::UiView::AoratonRooms);
        tap(180, 180);
        // The conversation's reply bar opens the composer.
        tap(240, 424);
        record("aoraton.compose", "reply-bar", ui.view() == aura::UiView::AoratonCompose,
               std::to_string(static_cast<int>(ui.view())));
        if (ui.view() == aura::UiView::AoratonCompose) {
            // Keyboard y=208..480: Greek row 2 at y=310.
            tap(25, 310);  // Α
            tap(73, 310);  // σ
            const int prepare_before = interaction_counts.aoraton_prepare;
            tap(432, 48);
            record("aoraton.prepare", "header-send",
                   interaction_counts.aoraton_prepare == prepare_before + 1 && ui.view() == aura::UiView::AoratonConfirm,
                   std::to_string(static_cast<int>(ui.view())));
            if (ui.view() == aura::UiView::AoratonConfirm) {
                const int confirm_before = interaction_counts.aoraton_confirm;
                tap(328, 420);
                record("aoraton.confirm", "button-center",
                       interaction_counts.aoraton_confirm == confirm_before + 1 && ui.view() == aura::UiView::AoratonConversation,
                       std::to_string(static_cast<int>(ui.view())));
            }
        }
        aoraton.select_room("demo-family");
        aoraton.prepare_send("demo-family", text.capture_message);
        ui.apply_aoraton(aoraton.snapshot());
        ui.show(aura::UiView::AoratonConfirm);
        pump(4);
        const int message_cancel_before = interaction_counts.aoraton_cancel;
        tap(104, 420);
        record("aoraton.cancel", "button-center",
               interaction_counts.aoraton_cancel == message_cancel_before + 1 &&
               ui.view() == aura::UiView::AoratonConversation,
               std::to_string(static_cast<int>(ui.view())));

        controller.dispatch({aura::EventType::Reset});
        controller.dispatch({aura::EventType::TurnStarted, text.capture_thinking_prompt});
        apply(); pump(5);
        tap(240, 420);
        record("assistant.cancel", "during-animation",
               controller.snapshot().state == aura::AssistantState::Idle && ui.view() == aura::UiView::Home,
               aura::state_name(controller.snapshot().state));

        controller.dispatch({aura::EventType::Reset});
        controller.dispatch({aura::EventType::TurnStarted, text.capture_long_prompt});
        controller.dispatch({aura::EventType::FinalResult, text.capture_long_answer});
        apply(); pump(5);
        auto* long_reader = find_scrollable(ui.root());
        const int long_scroll_before = long_reader ? lv_obj_get_scroll_y(long_reader) : 0;
        drag(240, 350, 240, 332);
        pump(4);
        const int long_scroll_after = long_reader ? lv_obj_get_scroll_y(long_reader) : 0;
        record("answer.long_scroll", "swipe-up",
               ui.view() == aura::UiView::Assistant && long_reader &&
               last_drag_scroll_y > long_scroll_before,
               std::string(long_reader ? "found " : "missing ") +
               std::to_string(long_scroll_before) + "->" + std::to_string(long_scroll_after) +
               " bottom=" + std::to_string(long_reader ? lv_obj_get_scroll_bottom(long_reader) : -1) +
               " active=" + (last_drag_scroll_object ? "yes" : "no") +
               " during=" + std::to_string(last_drag_scroll_y));

        aoraton.select_room("demo-family");
        for (int index = 0; index < 7; ++index) {
            aoraton.inject_incoming("demo-family", std::string("DEMO scroll message ") + std::to_string(index + 1));
        }
        ui.apply_aoraton(aoraton.snapshot());
        ui.show(aura::UiView::AoratonConversation);
        pump(4);
        const int prepare_before_scroll = interaction_counts.aoraton_prepare;
        // Opened at its newest message, the thread has history above, not below.
        std::function<lv_obj_t*(lv_obj_t*)> find_scrolled = [&](lv_obj_t* root) -> lv_obj_t* {
            if (lv_obj_is_scrollable(root) && lv_obj_get_scroll_top(root) > 0) return root;
            const std::uint32_t children = lv_obj_get_child_count(root);
            for (std::uint32_t index = 0; index < children; ++index) {
                if (auto* found = find_scrolled(lv_obj_get_child(root, static_cast<int32_t>(index)))) return found;
            }
            return nullptr;
        };
        auto* chat_thread = find_scrolled(ui.root());
        const int chat_scroll_before = chat_thread ? lv_obj_get_scroll_y(chat_thread) : 0;
        // The conversation opens at its newest message; swiping down reveals history.
        drag(240, 180, 240, 340);
        pump(4);
        const int chat_scroll_after = chat_thread ? lv_obj_get_scroll_y(chat_thread) : 0;
        record("aoraton.chat_scroll", "swipe-down-to-history",
               ui.view() == aura::UiView::AoratonConversation &&
               interaction_counts.aoraton_prepare == prepare_before_scroll && chat_thread &&
               chat_scroll_before > 0 && chat_scroll_after < chat_scroll_before,
               std::string(chat_thread ? "found " : "missing ") +
               std::to_string(chat_scroll_before) + "->" + std::to_string(chat_scroll_after) +
               " bottom=" + std::to_string(chat_thread ? lv_obj_get_scroll_bottom(chat_thread) : -1) +
               " active=" + (last_drag_scroll_object ? "yes" : "no") +
               " during=" + std::to_string(last_drag_scroll_y));

        reset_to(aura::UiView::Home);
        const int outside_before = interaction_counts.ask;
        tap(380, 380);
        record("home.ask", "outside-target",
               interaction_counts.ask == outside_before && ui.view() == aura::UiView::Home,
               "inactive outside target");

        controller.dispatch({aura::EventType::WifiChanged, {}, 0});
        controller.dispatch({aura::EventType::TurnStarted, text.capture_short_prompt});
        controller.dispatch({aura::EventType::ProviderUnavailable, "NETWORK_UNAVAILABLE"});
        apply(); pump(3);
        const int retry_offline_before = interaction_counts.retry;
        tap(128, 420);
        record("assistant.retry", "offline-failure",
               interaction_counts.retry == retry_offline_before + 1 && controller.snapshot().state == aura::AssistantState::Offline,
               aura::state_name(controller.snapshot().state));
        controller.dispatch({aura::EventType::WifiChanged, {}, 1});
        apply(); pump(3);
        const int retry_restored_before = interaction_counts.retry;
        tap(128, 420);
        // Retry re-asks the last question once the network is back.
        record("assistant.retry", "restored",
               interaction_counts.retry == retry_restored_before + 1 &&
               controller.snapshot().state == aura::AssistantState::Thinking,
               aura::state_name(controller.snapshot().state));
        controller.dispatch({aura::EventType::ProviderUnavailable, "DEMO_FAILURE"});
        apply(); pump(3);
        const int home_before = interaction_counts.home;
        tap(352, 420);
        record("assistant.home", "offline-button",
               interaction_counts.home == home_before + 1 && ui.view() == aura::UiView::Home &&
               controller.snapshot().state == aura::AssistantState::Idle,
               aura::state_name(controller.snapshot().state));

        reset_to(aura::UiView::Battery);
        const bool status_muted_before = audio.diagnostics().muted;
        tap(240, 420);
        record("status.mute", "button-center", audio.diagnostics().muted != status_muted_before,
               audio.diagnostics().muted ? "muted" : "unmuted");

        reset_to(aura::UiView::Cards);
        const auto view_before_disabled = ui.view();
        const int total_callbacks_before = interaction_counts.ask + interaction_counts.provider + interaction_counts.audio_tone +
            interaction_counts.audio_wav + interaction_counts.audio_pause + interaction_counts.audio_stop + interaction_counts.audio_mute;
        tap(240, 240);
        const int total_callbacks_after = interaction_counts.ask + interaction_counts.provider + interaction_counts.audio_tone +
            interaction_counts.audio_wav + interaction_counts.audio_pause + interaction_counts.audio_stop + interaction_counts.audio_mute;
        record("unavailable.search", "screen-center", ui.view() == view_before_disabled && total_callbacks_before == total_callbacks_after,
               "inactive");

        // ---- v13 "Companion": real SDL taps on the new screens ----------------
        auto view_text = [&] { return std::to_string(static_cast<int>(ui.view())); };
        auto& clock_model = companion.clock();
        // Typing goes straight into the focused text area; the keyboard itself
        // is covered by the Wi-Fi and compose checks above.
        auto type_text = [&](const char* value) {
            if (auto* area = lv_group_get_focused(lv_group_get_default())) lv_textarea_set_text(area, value);
            pump(3);
        };
        auto clear_timers = [&] {
            while (!clock_model.snapshot().timers.empty()) clock_model.cancel_timer(clock_model.snapshot().timers.front().id);
            while (clock_model.snapshot().alert) clock_model.dismiss_alert();
            pump(2);
        };

        // Launcher: the third row sits below the fold.
        const Point third_row[]{{"today", 96, 360}, {"notifications", 240, 360}, {"settings", 384, 360}};
        const aura::UiView third_row_views[]{aura::UiView::Briefing, aura::UiView::Notifications, aura::UiView::Settings};
        for (int index = 0; index < 3; ++index) {
            reset_to(aura::UiView::Launcher);
            scroll_view_to(148);
            tap(third_row[index].x, third_row[index].y);
            record("launcher.row3", third_row[index].name, ui.view() == third_row_views[index], view_text());
        }

        reset_to(aura::UiView::Home);
        tap(240, 132);
        record("home.today", "date-line", ui.view() == aura::UiView::Briefing, view_text());

        clear_timers();
        clock_model.start_timer(300, "DEMO");
        reset_to(aura::UiView::Home);
        tap(196, 436);
        record("home.timer_chip", "chip-center", ui.view() == aura::UiView::Clock, view_text());

        clear_timers();
        reset_to(aura::UiView::Clock);
        tap(432, 48);
        const bool clock_chooser = ui.view() == aura::UiView::ClockNew;
        tap(220, 140);
        const bool timer_presets = ui.view() == aura::UiView::TimerNew;
        tap(92, 144);
        record("clock.timer_preset", "first-chip",
               clock_chooser && timer_presets && clock_model.snapshot().timers.size() == 1 &&
               clock_model.snapshot().timers.front().duration_s == 60 && ui.view() == aura::UiView::Clock, view_text());

        reset_to(aura::UiView::AlarmNew);
        const auto alarms_before = clock_model.snapshot().alarms.size();
        tap(148, 239);
        pump(40);
        tap(240, 420);
        bool eight_oclock = false;
        for (const auto& alarm : clock_model.snapshot().alarms) eight_oclock = eight_oclock || (alarm.hour == 8 && alarm.minute == 0);
        record("clock.alarm_roller_save", "lower-row-then-save",
               clock_model.snapshot().alarms.size() == alarms_before + 1 && eight_oclock && ui.view() == aura::UiView::Clock,
               view_text());

        reset_to(aura::UiView::ClockNew);
        tap(220, 332);
        const bool reminder_form = ui.view() == aura::UiView::ReminderNew;
        type_text("Πότισμα · DEMO");
        tap(432, 48);
        const auto reminders_before = clock_model.snapshot().reminders.size();
        tap(220, 140);
        record("clock.reminder", "text-then-first-time",
               reminder_form && clock_model.snapshot().reminders.size() == reminders_before + 1 &&
               ui.view() == aura::UiView::Clock, view_text());

        clear_timers();
        clock_model.start_timer(120, "DEMO");
        reset_to(aura::UiView::Clock);
        tap(220, 140);
        const bool timer_item = ui.view() == aura::UiView::ClockItem;
        tap(128, 420);
        const bool timer_paused = !clock_model.snapshot().timers.empty() && clock_model.snapshot().timers.front().paused;
        tap(352, 420);
        record("clock.timer_item", "pause-then-cancel",
               timer_item && timer_paused && clock_model.snapshot().timers.empty() && ui.view() == aura::UiView::Clock,
               view_text());

        clear_timers();
        reset_to(aura::UiView::Home);
        clock_model.start_timer(1, "DEMO");
        clock_model.tick(1200);
        pump(4);
        const bool timer_ringing = ui.view() == aura::UiView::ClockAlert;
        tap(128, 420);
        record("clock.alert_stop", "stop-button",
               timer_ringing && !clock_model.snapshot().alert && ui.view() == aura::UiView::Home, view_text());

        clock_model.start_timer(1, "DEMO");
        clock_model.tick(1200);
        pump(4);
        tap(352, 420);
        record("clock.alert_plus_minute", "snooze-button",
               !clock_model.snapshot().alert && clock_model.snapshot().timers.size() == 1 &&
               clock_model.snapshot().timers.front().duration_s == 60, view_text());
        clear_timers();

        companion.notes().add_text("Πρώτη σημείωση · DEMO", 0);
        reset_to(aura::UiView::Notes);
        tap(220, 140);
        const bool note_detail = ui.view() == aura::UiView::NoteDetail;
        const auto notes_before = companion.notes().snapshot().notes.size();
        tap(432, 48);
        const bool note_armed_only = companion.notes().snapshot().notes.size() == notes_before;
        tap(432, 48);
        record("notes.delete", "header-twice",
               note_detail && note_armed_only && companion.notes().snapshot().notes.size() + 1 == notes_before &&
               ui.view() == aura::UiView::Notes, view_text());

        reset_to(aura::UiView::Notes);
        tap(352, 420);
        const bool note_form = ui.view() == aura::UiView::NoteNew;
        type_text("Γραπτή σημείωση · DEMO");
        const auto typed_before = companion.notes().snapshot().notes.size();
        tap(432, 48);
        record("notes.typed", "save-header",
               note_form && companion.notes().snapshot().notes.size() == typed_before + 1 &&
               ui.view() == aura::UiView::Notes, view_text());

        reset_to(aura::UiView::Notes);
        tap(128, 420);
        record("notes.voice_without_mic", "disabled-button", ui.view() == aura::UiView::Notes, view_text());

        auto propose = [&](const char* reply) {
            controller.dispatch({aura::EventType::Reset});
            controller.dispatch({aura::EventType::TurnStarted, "Βάλε χρονόμετρο · DEMO"});
            companion.deliver_reply(reply);
            apply();
            ui.show(aura::UiView::Assistant);
            pump(4);
        };
        clear_timers();
        propose("DEMO • Βάζω χρονόμετρο.\nACTION: {\"type\":\"timer\",\"seconds\":300,\"label\":\"DEMO\"}");
        const bool card_shown = controller.snapshot().state == aura::AssistantState::ActionConfirmation;
        tap(128, 420);
        record("assistant.action_confirm", "confirm-button",
               card_shown && clock_model.snapshot().timers.size() == 1 &&
               controller.snapshot().state == aura::AssistantState::Responding, aura::state_name(controller.snapshot().state));
        clear_timers();
        propose("DEMO • Βάζω χρονόμετρο.\nACTION: {\"type\":\"timer\",\"seconds\":300,\"label\":\"DEMO\"}");
        tap(352, 420);
        record("assistant.action_decline", "decline-button",
               clock_model.snapshot().timers.empty() && controller.snapshot().state == aura::AssistantState::Responding,
               aura::state_name(controller.snapshot().state));
        controller.dispatch({aura::EventType::Reset});
        apply();

        companion.memory().record("Τι ώρα είναι;", "DEMO • 18:24.", companion.now_ms());
        controller.dispatch({aura::EventType::TurnStarted, "Και αύριο; · DEMO"});
        companion.deliver_reply("DEMO • Συνεχίζω.");
        apply();
        ui.show(aura::UiView::Assistant);
        pump(4);
        const auto ask_before_new = interaction_counts.ask;
        tap(352, 420);
        record("assistant.new_conversation", "secondary-button",
               companion.memory().size() == 0 && interaction_counts.ask == ask_before_new + 1,
               aura::state_name(controller.snapshot().state));
        controller.dispatch({aura::EventType::Reset});
        apply();

        reset_to(aura::UiView::Briefing);
        tap(220, 180);
        record("today.row", "first-row", ui.view() != aura::UiView::Briefing, view_text());

        aura::CompanionCapabilities voice_capabilities;
        voice_capabilities.microphone = true;
        voice_capabilities.speech_recognition = true;
        voice_capabilities.imu = true;
        voice_capabilities.phone_link = true;
        companion.set_capabilities(voice_capabilities);
        reset_to(aura::UiView::Translate);
        const int mine_before = interaction_counts.translate_mine;
        const int theirs_before = interaction_counts.translate_theirs;
        tap(412, 404);
        // The other person's microphone is drawn rotated 180 degrees.
        tap(68, 76);
        record("translate.microphones", "mine-and-rotated-theirs",
               interaction_counts.translate_mine == mine_before + 1 &&
               interaction_counts.translate_theirs == theirs_before + 1, view_text());
        const auto language_before = companion.translation().snapshot().their_language;
        tap(240, 240);
        const bool language_changed = companion.translation().snapshot().their_language != language_before;
        tap(48, 240);
        record("translate.language_back", "pill-then-back",
               language_changed && ui.view() != aura::UiView::Translate, view_text());
        aura::CompanionCapabilities plain_capabilities;
        plain_capabilities.imu = true;
        plain_capabilities.phone_link = true;
        companion.set_capabilities(plain_capabilities);

        reset_to(aura::UiView::Home);
        companion.display().sleep_now(companion.now_ms());
        pump(3);
        const bool always_on = ui.view() == aura::UiView::AlwaysOn;
        tap(240, 240);
        record("display.wake", "first-touch",
               always_on && ui.view() == aura::UiView::Home &&
               companion.display().snapshot().mode == aura::DisplayMode::Active, view_text());

        reset_to(aura::UiView::Home);
        companion.handle_event("phone_call", "DEMO");
        pump(4);
        const bool call_screen = ui.view() == aura::UiView::PhoneCall;
        tap(144, 408);
        record("phone.call_decline", "round-button",
               call_screen && !companion.phone().snapshot().incoming_call && ui.view() == aura::UiView::Home, view_text());
        companion.handle_event("phone_call", "DEMO");
        pump(4);
        tap(336, 408);
        record("phone.call_accept", "round-button",
               !companion.phone().snapshot().incoming_call && ui.view() == aura::UiView::Home, view_text());

        companion.handle_event("phone_clear", {});
        companion.handle_event("phone_notification", "WhatsApp|DEMO|Γεια σου");
        reset_to(aura::UiView::Notifications);
        const int rows_before_phone = (controller.snapshot().notification_count > 0 ? 1 : 0) +
            static_cast<int>(std::min<std::size_t>(aoraton.snapshot().notifications.size(), 6));
        tap(220, 96 + rows_before_phone * 96 + 44);
        const bool phone_detail = ui.view() == aura::UiView::PhoneNotification;
        tap(240, 420);
        record("phone.notification_dismiss", "row-then-button",
               phone_detail && companion.phone().snapshot().notifications.empty() &&
               ui.view() == aura::UiView::Notifications, view_text());

        companion.phone().set_link(aura::PhoneLinkState::NotPaired, aura::PhonePlatform::Unknown, {});
        reset_to(aura::UiView::PhoneLink);
        tap(240, 420);
        const bool pairing = companion.phone().snapshot().link == aura::PhoneLinkState::Pairing;
        for (int wait = 0; wait < 60 && companion.phone().snapshot().link != aura::PhoneLinkState::Connected; ++wait) {
            pump(2);
            SDL_Delay(25);
        }
        record("phone.pair_demo", "pair-button",
               pairing && companion.phone().snapshot().link == aura::PhoneLinkState::Connected, view_text());

        reset_to(aura::UiView::DisplaySettings);
        const bool always_on_before = companion.display().snapshot().settings.always_on;
        tap(220, 140);
        record("display.always_on_row", "row-center",
               companion.display().snapshot().settings.always_on != always_on_before, view_text());

        companion.ota().reset();
        reset_to(aura::UiView::Update);
        tap(240, 420);
        const bool checking = companion.ota().snapshot().state == aura::UpdateState::Checking;
        for (int wait = 0; wait < 60 && companion.ota().snapshot().state != aura::UpdateState::Available; ++wait) {
            pump(2);
            SDL_Delay(25);
        }
        tap(240, 420);
        const bool asked = companion.ota().snapshot().state == aura::UpdateState::Available;
        tap(128, 420);
        record("update.check_install", "check-install-confirm",
               checking && asked && companion.ota().snapshot().state == aura::UpdateState::Downloading, view_text());

        reset_to(aura::UiView::Settings);
        scroll_view_to(680);
        tap(220, settings_row_y(7, 680));
        record("settings.display", "row", ui.view() == aura::UiView::DisplaySettings, view_text());
        reset_to(aura::UiView::Settings);
        scroll_view_to(680);
        tap(220, settings_row_y(8, 680));
        record("settings.phone", "row", ui.view() == aura::UiView::PhoneLink, view_text());
        reset_to(aura::UiView::Settings);
        scroll_view_to(680);
        tap(220, settings_row_y(9, 680));
        record("settings.wake_word", "info-row", ui.view() == aura::UiView::Settings, view_text());
        reset_to(aura::UiView::Settings);
        scroll_view_to(680);
        tap(220, settings_row_y(10, 680));
        record("settings.update", "row", ui.view() == aura::UiView::Update, view_text());

        const std::size_t passed = static_cast<std::size_t>(std::count_if(results.begin(), results.end(),
            [](const InteractionResult& result) { return result.passed; }));
        std::filesystem::create_directories(std::filesystem::path(interaction_report).parent_path());
        std::ofstream report(interaction_report, std::ios::binary | std::ios::trunc);
        report << "{\n  \"schema\": \"aura.pointer-coverage.v1\",\n  \"scale\": " << window_scale
               << ",\n  \"total\": " << results.size() << ",\n  \"passed\": " << passed << ",\n  \"results\": [\n";
        for (std::size_t index = 0; index < results.size(); ++index) {
            const auto& result = results[index];
            report << "    {\"action\":\"" << result.action << "\",\"point\":\"" << result.point
                   << "\",\"passed\":" << (result.passed ? "true" : "false")
                   << ",\"observed\":\"" << result.observed << "\"}"
                   << (index + 1 == results.size() ? "\n" : ",\n");
        }
        report << "  ]\n}\n";
        report.close();
        std::cout << "AURA pointer coverage: " << passed << "/" << results.size()
                  << " passed at " << window_scale << "x; report=" << interaction_report << '\n';
        if (provider) provider->cancel();
        audio.shutdown();
        if (lv_is_initialized()) lv_deinit();
        return passed == results.size() ? 0 : 10;
    }

    const std::string event_path = environment("AURA_CONTROL_EVENTS", "simulator/runtime/control-events.log");
    aura::simulator::ControlEventReader events(event_path);

    std::cout << "AURA simulator started: 480x480, provider=" << provider->name() << '\n';
    const auto started = std::chrono::steady_clock::now();
    auto previous = started;
    bool running = true;
    bool escape_down = false;
    bool f12_down = false;
    bool smoke_started = false;
    bool smoke_shot = false;
    std::chrono::steady_clock::time_point smoke_result_ready{};
    auto metrics_started = started;
    std::uint64_t handler_microseconds = 0;
    std::uint64_t handler_max_microseconds = 0;
    std::uint32_t handler_iterations = 0;
    std::uint64_t total_handler_microseconds = 0;
    std::uint64_t total_handler_max_microseconds = 0;
    std::uint64_t total_handler_iterations = 0;
    long long last_clock_second = -1;
    int motion_frame = 0;
    long long next_motion_frame_ms = 300;

    while (running && lv_is_initialized()) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - previous).count());
        previous = now;

        for (const auto& event : events.poll()) {
            if (event.type == "wake_detected") controller.dispatch({aura::EventType::WakeDetected});
            else if (event.type == "reset") {
                controller.dispatch({aura::EventType::Reset});
                ui.show(aura::UiView::Home);
            }
            else if (event.type == "begin_recording") controller.dispatch({aura::EventType::BeginRecording});
            else if (event.type == "end_recording") controller.dispatch({aura::EventType::EndRecording});
            else if (event.type == "prompt") start_turn(event.payload);
            else if (event.type == "partial") controller.dispatch({aura::EventType::PartialResult, event.payload});
            else if (event.type == "final") controller.dispatch({aura::EventType::FinalResult, event.payload});
            else if (event.type == "provider_unavailable") controller.dispatch({aura::EventType::ProviderUnavailable, "PROVIDER_UNAVAILABLE"});
            else if (event.type == "error") controller.dispatch({aura::EventType::Fail, event.payload});
            else if (event.type == "notification") controller.dispatch({aura::EventType::NotificationReceived, event.payload});
            else if (event.type == "wifi") {
                const bool online = integer(event.payload) != 0;
                controller.dispatch({aura::EventType::WifiChanged, {}, online ? 1 : 0});
                aoraton.set_network_available(online);
                device_status.set_connection(online, aura::StatusOrigin::Simulated, online ? 3 : 0);
                // Keep the Wi-Fi screen in step with the injected link state.
                if (!online) {
                    wifi_model.set_link(false, {}, -100, {}, aura::WifiOrigin::Simulated);
                } else if (wifi_host.host_connected) {
                    wifi_model.set_link(true, wifi_host.host_ssid, wifi_host.host_rssi_dbm, {}, aura::WifiOrigin::Host);
                } else if (deterministic_run) {
                    wifi_model.set_link(true, "AURA Lab · DEMO", -48, {}, aura::WifiOrigin::Demo);
                }
            }
            else if (event.type == "wifi_fail_next") wifi_fail_next = true;
            else if (event.type == "bluetooth") {
                device_status.set_bluetooth(integer(event.payload) != 0, aura::StatusOrigin::Simulated);
            }
            else if (event.type == "battery") {
                const int percent = integer(event.payload);
                controller.dispatch({aura::EventType::BatteryChanged, {}, percent});
                device_status.set_power(percent, device_status.snapshot().charging, aura::StatusOrigin::Simulated);
            }
            else if (event.type == "battery_unknown") {
                device_status.set_power_unavailable();
            }
            else if (event.type == "charging") {
                const bool charging = integer(event.payload) != 0;
                controller.dispatch({aura::EventType::ChargingChanged, {}, charging ? 1 : 0});
                device_status.set_power(device_status.snapshot().battery_percent, charging, aura::StatusOrigin::Simulated);
            }
            else if (event.type == "provider") {
                select_provider(event.payload == "codex");
                ui.set_provider_live(live_provider);
            }
            else if (event.type == "review_long_answer") {
                controller.dispatch({aura::EventType::Reset});
                controller.dispatch({aura::EventType::TurnStarted, text.capture_long_prompt});
                controller.dispatch({aura::EventType::FinalResult, text.capture_long_answer});
            }
            else if (event.type == "review_speaking") {
                controller.dispatch({aura::EventType::Reset});
                controller.dispatch({aura::EventType::TurnStarted, text.capture_speaking_prompt});
                controller.dispatch({aura::EventType::FinalResult, text.capture_speaking_answer});
                audio.play_test_tone(2400);
            }
            else if (event.type == "ui_view") {
                if (event.payload == "home") ui.show(aura::UiView::Home);
                else if (event.payload == "launcher") ui.show(aura::UiView::Launcher);
                else if (event.payload == "cards") ui.show(aura::UiView::Cards);
                else if (event.payload == "notifications") ui.show(aura::UiView::Notifications);
                else if (event.payload == "settings") ui.show(aura::UiView::Settings);
                else if (event.payload == "diagnostics") ui.show(aura::UiView::Diagnostics);
                else if (event.payload == "audio") ui.show(aura::UiView::Audio);
                else if (event.payload == "aoraton") ui.show(aura::UiView::AoratonRooms);
                else if (event.payload == "navigation") ui.show(aura::UiView::Navigation);
                else if (event.payload == "weather") {
                    ui.show(aura::UiView::Weather);
                    open_weather();
                }
                else if (event.payload == "weather-location") ui.show(aura::UiView::WeatherLocation);
                else if (event.payload == "tools") ui.show(aura::UiView::Tools);
                else if (event.payload == "calendar") ui.show(aura::UiView::Calendar);
                else if (event.payload == "camera") ui.show(aura::UiView::Camera);
                else if (event.payload == "battery") ui.show(aura::UiView::Battery);
                else if (event.payload == "voice") ui.show(aura::UiView::VoiceSettings);
            }
            else if (event.type == "screen" && event.payload == "off") controller.dispatch({aura::EventType::Sleep});
            else if (event.type == "screen" && event.payload == "on") controller.dispatch({aura::EventType::WakeScreen});
            else if (event.type == "audio_test" || event.type == "speaker") audio.play_test_tone();
            else if (event.type == "audio_wav") audio.play_bundled_wav();
            else if (event.type == "audio_pause") audio.pause();
            else if (event.type == "audio_resume") audio.resume();
            else if (event.type == "audio_stop") audio.stop();
            else if (event.type == "audio_complete") audio.simulate_complete();
            else if (event.type == "audio_fail") audio.simulate_failure(event.payload.empty() ? "DEMO_AUDIO_FAILURE" : event.payload.c_str());
            else if (event.type == "audio_mute") audio.set_muted(integer(event.payload) != 0);
            else if (event.type == "audio_volume") audio.set_volume(static_cast<float>(integer(event.payload, 55)) / 100.0F);
            else if (event.type == "input_amplitude") {
                manual_input_amplitude = static_cast<float>(std::clamp(integer(event.payload), 0, 100)) / 100.0F;
            }
            else if (event.type == "weather_city") {
                ui.show(aura::UiView::WeatherLocation);
                start_weather_search(event.payload, false);
            }
            else if (event.type == "weather_refresh") open_weather();
            else if (event.type == "aoraton_incoming" || event.type == "aoraton_unread") {
                const std::string text_value = event.payload.empty() ? text.incoming_demo_message : event.payload;
                aoraton.inject_incoming("demo-lab", text_value);
                controller.dispatch({aura::EventType::NotificationReceived, "AORATON_MESSAGE"});
            }
            else if (event.type == "aoraton_disconnect") aoraton.set_network_available(false);
            else if (event.type == "aoraton_reconnect") aoraton.set_network_available(true);
            else if (event.type == "aoraton_invalid") aoraton.reject_invalid_payload("INVALID_AUTHENTICATED_PAYLOAD");
            else if (event.type == "aoraton_pairing_failure") aoraton.reject_invalid_payload("PAIRING_REJECTED");
            else if (event.type == "aoraton_reset") aoraton.reset_mock();
            else if (event.type == "aoraton_prepare") {
                const std::string room_id = aoraton.snapshot().selected_room_id.empty() ?
                    "demo-family" : aoraton.snapshot().selected_room_id;
                aoraton.prepare_send(room_id, event.payload);
            }
            else if (event.type == "aoraton_confirm") aoraton.confirm_send();
            else if (event.type == "aoraton_sound") audio.play_test_tone(180);
            else if (event.type == "cancel") cancel();
            else companion.handle_event(event.type, event.payload);
            apply();
            ui.apply_status(device_status.snapshot());
            ui.apply_audio(audio.diagnostics());
            ui.apply_aoraton(aoraton.snapshot());
            ui.apply_weather(weather_model.snapshot());
            ui.apply_wifi(wifi_model.snapshot());
        }

        poll_weather();
        poll_wifi(now);
        prune_ready(retired_replies);
        prune_ready(retired_weather_searches);
        prune_ready(retired_weather_fetches);
        prune_ready(retired_wifi_scans);

        if (!pending && controller.snapshot().state == aura::AssistantState::Transcribing &&
            !companion.handles_transcribing()) {
            // The desktop has no speech recognizer: the Listening screen shows a
            // labelled synthetic input, whose transcript is the fixed sample
            // prompt. Without this the assistant would wait in Thinking forever.
            start_turn(text.capture_short_prompt);
        }

        if (pending && pending_reply.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            const auto reply = pending_reply.get();
            pending = false;
            if (pending_generation == controller.snapshot().turn_generation) {
                if (reply.ok) companion.deliver_reply(reply.text);
                else if (reply.error_code != "CANCELLED") {
                    const bool offline = reply.error_code == "BRIDGE_UNAVAILABLE" ||
                                         reply.error_code == "CLI_NOT_INSTALLED" ||
                                         reply.error_code == "NOT_LOGGED_IN";
                    controller.dispatch({offline ? aura::EventType::ProviderUnavailable : aura::EventType::Fail,
                                         reply.error_code});
                }
                apply();
                if (smoke) smoke_result_ready = now;
            }
        }

        const auto runtime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
        if (smoke && !smoke_started && runtime_ms > 100) {
            smoke_started = true;
            start_turn("Πες μου για τον Ταΰγετο");
        }
        const bool smoke_transition_complete = smoke_result_ready.time_since_epoch().count() != 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - smoke_result_ready).count() >= 320;
        if (smoke && !smoke_shot && smoke_transition_complete && !pending) {
            smoke_shot = true;
            lv_timer_handler();
            SDL_Delay(16);
            lv_timer_handler();
            if (!screenshot_path.empty() && !aura::simulator::save_display_bmp(display, screenshot_path)) {
                std::cerr << "AURA: screenshot failed\n";
                return 3;
            }
            running = false;
        }
        if (smoke && runtime_ms > 60000) {
            std::cerr << "AURA: smoke provider timeout\n";
            return 4;
        }
        if (aoraton_pairing_smoke && runtime_ms > 400) {
            lv_timer_handler();
            SDL_Delay(16);
            lv_timer_handler();
            if (!screenshot_path.empty() && !aura::simulator::save_display_bmp(display, screenshot_path)) {
                std::cerr << "AURA: Aoraton pairing smoke screenshot failed\n";
                return 15;
            }
            running = false;
        }
        if (!capture_scenario.empty() && runtime_ms > 800) {
            if (screenshot_path.empty() || !aura::simulator::save_display_bmp(display, screenshot_path)) {
                std::cerr << "AURA: capture failed\n";
                return 6;
            }
            running = false;
        }
        if (!weather_live_city.empty() && weather_live_finished &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - weather_live_finished_at).count() >= 500) {
            if (!screenshot_path.empty() && !aura::simulator::save_display_bmp(display, screenshot_path)) {
                std::cerr << "AURA: live weather screenshot failed\n";
                weather_live_exit_code = 13;
            } else if (!weather_live_succeeded) {
                weather_live_exit_code = 12;
            }
            const bool tlesdetector = weather_live_succeeded && weather_model.snapshot().has_data &&
                weather_model.snapshot().data.provider == aura::WeatherProvider::TlesDetector;
            std::cout << "AURA live weather: provider=" << (tlesdetector ? "tlesdetector" : "Open-Meteo")
                      << ", result="
                      << (weather_live_succeeded ? "ready" : "failed") << '\n';
            running = false;
        }
        if (!weather_live_city.empty() && runtime_ms > 30000 && !weather_live_finished) {
            std::cerr << "AURA: live weather timeout\n";
            return 12;
        }

        audio.tick(std::min<std::uint32_t>(elapsed, 100));
        companion.poll(capture_scenario.empty() ? std::min<std::uint32_t>(elapsed, 100) : 0U);
        if (aoraton_fail_closed_transport) aoraton_fail_closed_transport->poll();
        aoraton.tick(std::min<std::uint32_t>(elapsed, 100));
        const long long clock_second = runtime_ms / 1000;
        if (capture_scenario.empty() && clock_second != last_clock_second) {
            last_clock_second = clock_second;
            std::time_t now_time = std::time(nullptr);
            std::tm local_time{};
            localtime_s(&local_time, &now_time);
            device_status.set_clock(local_time.tm_hour, local_time.tm_min, aura::StatusOrigin::Live);
            device_status.set_date(local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
                                   local_time.tm_wday, aura::StatusOrigin::Live);
            companion.set_clock({local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
                                 local_time.tm_hour, local_time.tm_min, local_time.tm_sec, local_time.tm_wday});
        }
        apply();
        ui.apply_audio(audio.diagnostics());
        ui.apply_aoraton(aoraton_capture_override ? *aoraton_capture_override : aoraton.snapshot());
        ui.apply_status(device_status.snapshot());
        ui.apply_weather(weather_model.snapshot());
        ui.apply_wifi(wifi_model.snapshot());
        companion.apply();
        if (companion.capturing()) {
            // The runtime feeds the real microphone level.
        } else if (controller.snapshot().state == aura::AssistantState::Listening) {
            const float synthetic = manual_input_amplitude >= 0.0F ? manual_input_amplitude :
                0.18F + 0.62F * (0.5F + 0.5F * std::sin(static_cast<float>(runtime_ms) / 115.0F));
            ui.set_input_amplitude(synthetic, true);
        } else {
            ui.set_input_amplitude(0.0F, false);
        }
        ui.tick(std::min<std::uint32_t>(elapsed, 100));
        const auto handler_started = std::chrono::steady_clock::now();
        lv_timer_handler();
        const auto handler_finished = std::chrono::steady_clock::now();
        const auto handler_elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(handler_finished - handler_started).count());
        handler_microseconds += handler_elapsed;
        handler_max_microseconds = std::max(handler_max_microseconds, handler_elapsed);
        ++handler_iterations;
        total_handler_microseconds += handler_elapsed;
        total_handler_max_microseconds = std::max(total_handler_max_microseconds, handler_elapsed);
        ++total_handler_iterations;
        if (!motion_scenario.empty() && runtime_ms >= next_motion_frame_ms && motion_frame < 8) {
            const auto path = std::filesystem::path(motion_directory) /
                (motion_scenario + "-" + std::to_string(motion_frame) + ".bmp");
            if (!aura::simulator::save_display_bmp(display, path.string())) {
                std::cerr << "AURA: motion frame capture failed: " << path.string() << '\n';
                return 11;
            }
            ++motion_frame;
            next_motion_frame_ms += 350;
            if (motion_frame == 8) running = false;
        }
        const auto metrics_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            handler_finished - metrics_started).count();
        if (metrics_elapsed >= 500 && handler_iterations > 0) {
            ui.set_host_metrics(
                static_cast<float>(handler_iterations) * 1000.0F / static_cast<float>(metrics_elapsed),
                static_cast<float>(handler_microseconds) / static_cast<float>(handler_iterations) / 1000.0F,
                static_cast<float>(handler_max_microseconds) / 1000.0F);
            metrics_started = handler_finished;
            handler_microseconds = 0;
            handler_max_microseconds = 0;
            handler_iterations = 0;
        }
        if (!lv_display_get_next(nullptr)) running = false;

        const auto* keys = SDL_GetKeyboardState(nullptr);
        const bool escape = keys[SDL_SCANCODE_ESCAPE] != 0;
        if (escape && !escape_down && aura::is_turn_active(controller.snapshot().state)) cancel();
        escape_down = escape;
        const bool f12 = keys[SDL_SCANCODE_F12] != 0;
        if (f12 && !f12_down) {
            const std::string path = screenshot_path.empty() ?
                "simulator/screenshots/aura-latest.bmp" : screenshot_path;
            std::cout << (aura::simulator::save_display_bmp(display, path) ? "Screenshot: " : "Screenshot failed: ")
                      << path << '\n';
        }
        f12_down = f12;
        if (ui_stress && runtime_ms > 250) running = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (provider) provider->cancel();
    audio.shutdown();
    if (!motion_scenario.empty()) {
        lv_mem_monitor_t motion_memory{};
        lv_mem_monitor(&motion_memory);
        const auto motion_used = motion_memory.total_size - motion_memory.free_size;
        const float average_handler_ms = total_handler_iterations == 0 ? 0.0F :
            static_cast<float>(total_handler_microseconds) /
            static_cast<float>(total_handler_iterations) / 1000.0F;
        const auto mercury_frames = ui.mercury_frame_count();
        const auto evidence_span_ms = motion_frame > 1 ? (motion_frame - 1) * 350 : 0;
        const float mercury_hz = evidence_span_ms == 0 || mercury_frames <= 1 ? 0.0F :
            static_cast<float>(mercury_frames - 1) * 1000.0F / static_cast<float>(evidence_span_ms);
        std::cout << "AURA motion evidence: scenario=" << motion_scenario
                  << ", frames=" << motion_frame
                  << ", updates=" << ui.motion_update_count()
                  << ", span_ms=" << evidence_span_ms
                  << ", measured_update_hz=" << ui.motion_update_hz()
                  << ", weather_updates=" << ui.weather_motion_update_count()
                  << ", weather_update_hz=" << ui.weather_motion_update_hz()
                  << ", mercury_frames=" << mercury_frames
                  << ", mercury_estimated_hz=" << mercury_hz
                  << ", mercury_buffer_bytes=" << ui.mercury_buffer_bytes()
                  << ", handler_avg_ms=" << average_handler_ms
                  << ", handler_max_ms=" << static_cast<float>(total_handler_max_microseconds) / 1000.0F
                  << ", lvgl_used_bytes=" << motion_used
                  << ", lvgl_fragmentation=" << static_cast<int>(motion_memory.frag_pct) << "%\n";
    }
    if (lv_is_initialized()) lv_deinit();
    std::cout << "AURA simulator stopped cleanly\n";
    return weather_live_exit_code;
}

#include "aura/assistant_state.hpp"
#include "aura/assistant_stream.hpp"
#include "aura/aoraton.hpp"
#include "aura/aoraton_real.hpp"
#include "aura/aura_appearance.hpp"
#include "aura/aura_theme.hpp"
#include "aura/aura_ui.hpp"
#include "aura/bridge_speech_service.hpp"
#include "aura/companion_runtime.hpp"
#include "aura/companion_strings.hpp"
#include "aura/control_events.hpp"
#include "aura/device_status.hpp"
#include "aura/aoraton_account_transport.hpp"
#include "aura/fail_closed_aoraton_transport.hpp"
#include "aura/local_codex_provider.hpp"
#include "aura/mock_provider.hpp"
#include "aura/open_meteo_weather_service.hpp"
#include "aura/provider.hpp"
#include "aura/radio.hpp"
#include "aura/screenshot.hpp"
#include "aura/strings.hpp"
#include "aura/visual_strings.hpp"
#include "aura/weather.hpp"
#include "aura/weather_store.hpp"
#include "aura/wifi.hpp"
#include "aura/greek_calendar.hpp"
#include "aura/offline_commands.hpp"
#include "aura/windows_audio_backend.hpp"
#include "aura/windows_ancs_service.hpp"
#include "aura/windows_fuel_service.hpp"
#include "aura/windows_radio_service.hpp"
#include "aura/windows_wifi_service.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <future>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
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
constexpr std::size_t kMaximumRetiredRequests = 4;

template <typename T>
void prune_finished(std::vector<std::future<T>>& futures) {
    futures.erase(std::remove_if(futures.begin(), futures.end(), [](std::future<T>& future) {
        return !future.valid() || future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    }), futures.end());
}

template <typename T>
bool retire_future(std::future<T>& active, std::vector<std::future<T>>& retired) {
    prune_finished(retired);
    if (!active.valid()) return true;
    if (active.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        active = {};
        return true;
    }
    if (retired.size() >= kMaximumRetiredRequests) return false;
    retired.push_back(std::move(active));
    return true;
}

template <typename T>
bool retire_future(std::optional<std::future<T>>& active, std::vector<std::future<T>>& retired) {
    prune_finished(retired);
    if (!active || !active->valid()) {
        active.reset();
        return true;
    }
    if (active->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        active.reset();
        return true;
    }
    if (retired.size() >= kMaximumRetiredRequests) return false;
    retired.push_back(std::move(*active));
    active.reset();
    return true;
}

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

aura::WeatherLocation demo_weather_location(
    const std::string& requested_name = {},
    aura::InterfaceLanguage language = aura::InterfaceLanguage::Greek) {
    const bool english = language == aura::InterfaceLanguage::English;
    const std::string name = requested_name.empty() ? (english ? "Athens" : "Αθήνα") : requested_name;
    if (name == "Σπάρτη" || name == "Sparta") {
        return {english ? "Sparta" : "Σπάρτη", english ? "Peloponnese" : "Πελοπόννησος",
                english ? "Greece" : "Ελλάδα", "GR", "Europe/Athens", 37.0733, 22.4297};
    }
    if (name == "Θεσσαλονίκη" || name == "Thessaloniki") {
        return {english ? "Thessaloniki" : "Θεσσαλονίκη",
                english ? "Central Macedonia" : "Κεντρική Μακεδονία", english ? "Greece" : "Ελλάδα",
                "GR", "Europe/Athens", 40.6401, 22.9444};
    }
    if (name == "Πάτρα" || name == "Patras") {
        return {english ? "Patras" : "Πάτρα", english ? "Western Greece" : "Δυτική Ελλάδα",
                english ? "Greece" : "Ελλάδα", "GR", "Europe/Athens", 38.2466, 21.7346};
    }
    return {english ? "Athens" : "Αθήνα", english ? "Attica" : "Αττική",
            english ? "Greece" : "Ελλάδα", "GR", "Europe/Athens", 37.9838, 23.7275};
}

aura::WeatherData demo_weather_data(aura::WeatherCondition condition = aura::WeatherCondition::PartlyCloudy,
                                     const std::string& city = {},
                                     aura::InterfaceLanguage language = aura::InterfaceLanguage::Greek,
                                     bool is_day = true) {
    aura::WeatherData data;
    data.location = demo_weather_location(city, language);
    data.is_day = is_day;
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
            (later[index] == aura::WeatherCondition::Thunderstorm ? 92 : 12), is_day && index == 0};
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

aura::WeatherData demo_tlesdetector_weather_data(
    aura::InterfaceLanguage language = aura::InterfaceLanguage::Greek) {
    auto data = demo_weather_data(aura::WeatherCondition::Thunderstorm, {}, language);
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

std::string read_text_store(const std::filesystem::path& path) {
    constexpr std::uintmax_t kMaxBytes = 128U * 1024U;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaxBytes) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool write_text_store(const std::filesystem::path& path, const std::string& data) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
#else
    std::filesystem::rename(temporary, path, error);
    if (error) std::filesystem::remove(temporary, error);
    if (error) return false;
#endif
    return true;
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
    std::optional<aura::InterfaceLanguage> language_override;
    std::optional<aura::Appearance> appearance_override;
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
        if (argument == "--language" && index + 1 < arguments.size()) {
            const auto value = arguments[++index];
            if (value != "el" && value != "en") {
                std::cerr << "AURA: --language expects el or en\n";
                return 2;
            }
            language_override = aura::parse_interface_language(value);
        }
        if (argument == "--appearance" && index + 1 < arguments.size()) {
            const auto value = arguments[++index];
            const auto look = aura::appearance_from_id(value);
            if (value != aura::appearance_id(look)) {
                std::cerr << "AURA: --appearance expects halo, marble, aurora or graphite\n";
                return 2;
            }
            appearance_override = look;
        }
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
    const std::string aoraton_mode_name = environment("AURA_AORATON_MODE", "mock");
    std::unique_ptr<aura::AoratonMailbox> aoraton_mailbox;
    std::unique_ptr<aura::IAoratonService> aoraton_service;
    std::unique_ptr<aura::simulator::FailClosedAoratonTransport> aoraton_fail_closed_transport;
    std::unique_ptr<aura::simulator::AoratonAccountTransport> aoraton_account_transport;
    aura::WindowsAoratonAccountCrypto aoraton_id_crypto;
    if (aoraton_mode_name == "mock") {
        aoraton_service = std::make_unique<aura::MockAoratonService>();
    } else if (aoraton_mode_name == "test" || aoraton_mode_name == "live") {
        const auto runtime_mode = aoraton_mode_name == "test" ?
            aura::AoratonRuntimeMode::IntegrationTest : aura::AoratonRuntimeMode::Live;
        aoraton_mailbox = std::make_unique<aura::AoratonMailbox>();
        aura::simulator::AoratonTransportOptions aoraton_options;
        std::string aoraton_config_error;
        // The owner's own aoraton.com account (docs/design/AURA_AORATON_ACCOUNT_LOGIN_DESIGN.md);
        // capture and interaction runs never sign in, so they keep the fail-closed adapter.
        const bool account_configured = aura::simulator::load_aoraton_transport_options(aoraton_options,
                                                                                        aoraton_config_error);
        if (account_configured) {
            aoraton_service = std::make_unique<aura::RealAoratonService>(
                *aoraton_mailbox, runtime_mode, [&aoraton_id_crypto] {
                    return aura::aoraton_new_client_message_id(aoraton_id_crypto);
                });
            aoraton_account_transport = std::make_unique<aura::simulator::AoratonAccountTransport>(
                *aoraton_mailbox, std::move(aoraton_options));
            std::cerr << "AURA Aoraton " << (runtime_mode == aura::AoratonRuntimeMode::IntegrationTest ? "TEST" : "LIVE")
                      << ": account sign-in through the PC browser\n";
            if (aoraton_account_transport->inco_enabled()) {
                std::cerr << "AURA inco: ON - this device answers its own @inco messages through inco-ai (Gemini)\n";
            }
        } else {
            aoraton_service = std::make_unique<aura::RealAoratonService>(
                *aoraton_mailbox, runtime_mode, aoraton_client_message_id);
            aoraton_fail_closed_transport =
                std::make_unique<aura::simulator::FailClosedAoratonTransport>(*aoraton_mailbox);
            std::cerr << "AURA Aoraton " << (runtime_mode == aura::AoratonRuntimeMode::IntegrationTest ? "TEST" : "LIVE")
                      << ": transport disabled (" << aoraton_config_error << "; run scripts\\setup-aoraton.ps1)\n";
        }
    } else {
        std::cerr << "AURA: invalid AURA_AORATON_MODE='" << aoraton_mode_name
                  << "' (expected mock, test, or live); refusing implicit fallback\n";
        return 2;
    }
    aura::IAoratonService& aoraton = *aoraton_service;
    std::optional<aura::AoratonSnapshot> aoraton_capture_override;
    aura::simulator::WindowsAudioBackend audio;
    audio.initialize();
    aura::simulator::WindowsRadioService radio_service(audio);
    aura::RadioModel radio_model;
    aura::LauncherLayoutModel launcher_layout_model;
    aura::simulator::WindowsFuelService fuel_service(
        environment("AURA_BRIDGE_TOKEN"),
        static_cast<std::uint16_t>(integer(environment("AURA_BRIDGE_PORT", "4778"), 4778)));
    aura::FuelPriceModel fuel_model;
    const std::string companion_store_dir = environment("AURA_COMPANION_STORE", "simulator/runtime/companion");
    const std::filesystem::path radio_store_path = std::filesystem::path(companion_store_dir) / "radio.txt";
    const std::filesystem::path launcher_layout_store_path =
        std::filesystem::path(companion_store_dir) / "app-layout.txt";
    aura::AuraUi ui;
    const auto text = [&ui]() -> const aura::l10n::Strings& {
        return ui.language() == aura::InterfaceLanguage::English ? aura::l10n::en_us() : aura::l10n::el_gr();
    };
    aura::WeatherModel weather_model;
    aura::simulator::OpenMeteoWeatherService weather_service;
    const std::string weather_store_override = environment("AURA_WEATHER_STORE");
    aura::simulator::WeatherStore weather_store(weather_store_override.empty() ?
        aura::simulator::WeatherStore::default_base_path() : std::filesystem::path(weather_store_override));
    if (const auto cached = weather_store.load_cache()) weather_model.restore_cache(*cached);
    if (const auto saved = weather_store.load_location()) weather_model.restore_location(*saved);
    weather_model.set_refresh_interval(weather_store.load_refresh_interval());
    std::optional<std::future<aura::WeatherGeocodingResult>> pending_weather_search;
    std::optional<std::future<aura::WeatherFetchResult>> pending_weather_fetch;
    std::optional<std::chrono::steady_clock::time_point> next_weather_refresh_at;
    std::optional<std::future<aura::RadioSearchResult>> pending_radio_search;
    std::optional<std::future<aura::simulator::FuelFetchResult>> pending_fuel_fetch;
    bool weather_auto_fetch_first{false};
    bool weather_fetch_persist{false};
    bool weather_live_finished{false};
    bool weather_live_succeeded{false};
    int weather_live_exit_code{0};
    std::chrono::steady_clock::time_point weather_live_finished_at{};
    std::unique_ptr<aura::IAssistantProvider> provider;
    bool live_provider = std::string(environment("AURA_PROVIDER", "mock")) == "codex";
    std::future<aura::AssistantReply> pending_reply;
    std::shared_ptr<aura::AssistantStreamMailbox> pending_stream;
    std::uint64_t pending_stream_sequence = 0;
    // Superseded async work. Destroying an unfinished std::async future blocks
    // until it completes, so replaced requests are parked here and released
    // once ready instead of freezing the UI thread.
    std::vector<std::future<aura::AssistantReply>> retired_replies;
    std::vector<std::future<aura::WeatherGeocodingResult>> retired_weather_searches;
    std::vector<std::future<aura::WeatherFetchResult>> retired_weather_fetches;
    std::vector<std::future<aura::RadioSearchResult>> retired_radio_searches;
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
        int weather_interval{0};
        int weather_search{0};
        int weather_select{0};
        int radio_play{0};
        int radio_stop{0};
        int radio_search{0};
        int radio_favorite{0};
        int radio_move{0};
        int launcher_move{0};
        int wifi_open{0};
        int wifi_scan{0};
        int wifi_connect{0};
        int wifi_forget{0};
        int system_action{0};
        int sound_preview{0};
        int voice_preview{0};
        int aoraton_unlock{0};
        int aoraton_unlock_bytes{0};
        int aoraton_paste{0};
        int aoraton_sign_in{0};
        int aoraton_sign_out{0};
    } interaction_counts;

    std::function<void()> provider_changed_hook;
    auto select_provider = [&](bool live) {
        if (!retire_future(pending_reply, retired_replies)) return;
        if (provider) provider->cancel();
        if (pending) {
            // The replaced provider's reply can no longer be delivered; end the
            // turn explicitly instead of leaving Thinking without a request.
            pending = false;
            controller.dispatch({aura::EventType::Cancel});
        }
        live_provider = live;
        if (live) {
            provider = std::make_unique<aura::simulator::LocalCodexProvider>(
                environment("AURA_BRIDGE_TOKEN"),
                static_cast<std::uint16_t>(integer(environment("AURA_BRIDGE_PORT", "4778"), 4778)),
                std::string(environment("AURA_BRIDGE_AI_PROVIDER", "codex")) == "groq" ?
                    "Groq via local gateway" : "Local Codex CLI");
        } else {
            provider = std::make_unique<aura::MockProvider>();
        }
        if (provider_changed_hook) provider_changed_hook();
    };

    auto apply = [&] { ui.apply(controller.snapshot()); };
    auto apply_weather = [&] {
        const auto& weather = weather_model.snapshot();
        ui.apply_weather(weather);
        // inco may ask where the owner is: the saved weather city answers.
        if (aoraton_account_transport) {
            aoraton_account_transport->set_location_city(weather.has_saved_location ? weather.saved_location.name
                                                                                    : std::string{});
        }
    };
    auto same_weather_location = [](const aura::WeatherLocation& left, const aura::WeatherLocation& right) {
        return left.name == right.name && std::fabs(left.latitude - right.latitude) < 0.0001 &&
               std::fabs(left.longitude - right.longitude) < 0.0001;
    };
    std::function<void(aura::WeatherLocation, bool)> start_weather_fetch;
    std::function<void(std::string, bool)> start_weather_search;
    start_weather_fetch = [&](aura::WeatherLocation location, bool save_as_default) {
        if (!retire_future(pending_weather_fetch, retired_weather_fetches)) {
            weather_model.fetch_failed("WEATHER_BUSY", false);
            apply_weather();
            return;
        }
        if (!weather_model.begin_fetch(location, save_as_default)) return;
        next_weather_refresh_at = std::chrono::steady_clock::now() + std::chrono::minutes(
            aura::weather_refresh_interval_minutes(weather_model.snapshot().refresh_interval));
        if (save_as_default && interaction_report.empty()) weather_store.save_location(location);
        weather_fetch_persist = save_as_default ||
            (weather_model.snapshot().has_saved_location &&
             same_weather_location(weather_model.snapshot().saved_location, location));
        apply_weather();
        if (!interaction_report.empty()) {
            auto data = demo_weather_data(aura::WeatherCondition::PartlyCloudy, location.name, ui.language());
            data.location = std::move(location);
            weather_model.fetch_succeeded(data);
            apply_weather();
            return;
        }
        pending_weather_fetch.emplace(weather_service.fetch(std::move(location)));
    };
    start_weather_search = [&](std::string query, bool auto_fetch_first) {
        if (!retire_future(pending_weather_search, retired_weather_searches)) {
            weather_model.search_failed("WEATHER_BUSY", false);
            apply_weather();
            return;
        }
        if (!weather_model.begin_search(query)) return;
        weather_auto_fetch_first = auto_fetch_first;
        apply_weather();
        if (!interaction_report.empty()) {
            const bool english = ui.language() == aura::InterfaceLanguage::English;
            std::vector<aura::WeatherLocation> results{
                demo_weather_location(query, ui.language()),
                demo_weather_location({}, ui.language()),
                demo_weather_location(english ? "Sparta" : "Σπάρτη", ui.language())};
            weather_model.search_succeeded(results);
            apply_weather();
            if (auto_fetch_first) {
                weather_auto_fetch_first = false;
                ui.show(aura::UiView::Weather);
                start_weather_fetch(results.front(), false);
            }
            return;
        }
        pending_weather_search.emplace(weather_service.search(std::move(query),
            aura::interface_language_code(ui.language())));
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
    aura::simulator::WindowsAncsService ancs_service;
    const bool ancs_enabled = !deterministic_run && std::string(environment("AURA_ANCS_ENABLED")) == "1";
    if (!deterministic_run) {
        const auto saved_radio = read_text_store(radio_store_path);
        if (!saved_radio.empty() && !radio_model.restore_favorites(saved_radio)) {
            std::cerr << "AURA radio: ignored invalid favourites store\n";
        }
        const auto saved_layout = read_text_store(launcher_layout_store_path);
        if (!saved_layout.empty() && !launcher_layout_model.restore(saved_layout)) {
            std::cerr << "AURA launcher: ignored invalid app layout store\n";
        }
    }
    // Deterministic runs never sign in to the owner's account.
    if (aoraton_account_transport && !deterministic_run) aoraton_account_transport->start();
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
        if (!retire_future(pending_wifi_scan, retired_wifi_scans)) {
            wifi_model.scan_failed("SCAN_BUSY");
            apply_wifi();
            return;
        }
        if (!wifi_model.begin_scan()) return;
        apply_wifi();
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

    std::uint64_t radio_generation{0};
    auto apply_radio = [&] { ui.apply_radio(radio_model.snapshot()); };
    auto save_radio = [&] {
        if (!deterministic_run && !write_text_store(radio_store_path, radio_model.serialize_favorites())) {
            std::cerr << "AURA radio: could not save favourites\n";
        }
    };
    auto apply_launcher_layout = [&] { ui.apply_launcher_layout(launcher_layout_model.snapshot()); };
    auto save_launcher_layout = [&] {
        if (!deterministic_run &&
            !write_text_store(launcher_layout_store_path, launcher_layout_model.serialize())) {
            std::cerr << "AURA launcher: could not save app layout\n";
        }
    };
    auto play_radio = [&](const aura::RadioStation& selected) {
        const aura::RadioStation station = selected;
        radio_model.begin_connect(station);
        if (deterministic_run) {
            radio_generation = 0;
            radio_model.set_playing();
            radio_model.set_title(ui.language() == aura::InterfaceLanguage::English ?
                                      aura::l10n::companion_en_us().radio_demo_title :
                                      aura::l10n::companion_el_gr().radio_demo_title);
        } else {
            radio_generation = radio_service.play(station);
        }
        apply_radio();
    };
    auto stop_radio = [&] {
        radio_service.stop();
        radio_generation = radio_service.generation();
        radio_model.stop();
        apply_radio();
    };
    auto search_radio = [&](std::string query) {
        if (!retire_future(pending_radio_search, retired_radio_searches)) {
            radio_model.begin_search(query);
            radio_model.search_finished({}, "RADIO_BUSY");
            apply_radio();
            return;
        }
        radio_model.begin_search(query);
        apply_radio();
        if (deterministic_run) {
            auto results = aura::greek_default_stations();
            if (results.size() > 4) results.resize(4);
            // DEMO first: a long station name is cut short in the list.
            for (auto& station : results) station.name = std::string(text().demo) + " · " + station.name;
            radio_model.search_finished(std::move(results), {});
            apply_radio();
            return;
        }
        pending_radio_search.emplace(radio_service.search(std::move(query)));
    };
    auto poll_radio = [&] {
        aura::simulator::RadioWorkerEvent event;
        bool changed = false;
        while (radio_service.poll(event)) {
            if (event.generation != radio_generation) continue;
            if (event.type == aura::simulator::RadioWorkerEventType::Playing) radio_model.set_playing();
            else if (event.type == aura::simulator::RadioWorkerEventType::Title) radio_model.set_title(event.text);
            else if (event.type == aura::simulator::RadioWorkerEventType::Reconnecting) {
                // The stream dropped and is being joined again: show "Σύνδεση…".
                radio_model.begin_connect(radio_model.snapshot().station);
            }
            else radio_model.fail(event.text);
            changed = true;
        }
        if (pending_radio_search &&
            pending_radio_search->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto result = pending_radio_search->get();
            pending_radio_search.reset();
            radio_model.search_finished(std::move(result.stations), result.ok ? std::string{} : result.error_code);
            changed = true;
        }
        if (changed) apply_radio();
    };

    auto apply_fuel = [&] { ui.apply_fuel(fuel_model.snapshot()); };
    auto refresh_fuel = [&] {
        if (!fuel_service.configured() || pending_fuel_fetch || !fuel_model.begin_fetch()) return;
        apply_fuel();
        pending_fuel_fetch.emplace(fuel_service.fetch());
    };
    auto poll_fuel = [&] {
        if (!pending_fuel_fetch ||
            pending_fuel_fetch->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        auto result = pending_fuel_fetch->get();
        pending_fuel_fetch.reset();
        if (result.ok) {
            const auto& status = device_status.snapshot();
            if (status.date_valid) {
                result.data.stale = aura::fuel_prices_stale(result.data, status.year, status.month, status.day);
            }
            fuel_model.fetch_succeeded(std::move(result.data));
        } else {
            fuel_model.fetch_failed(std::move(result.error_code));
        }
        apply_fuel();
    };
    if (!deterministic_run) refresh_fuel();
    // The prices refresh on their own (sooner while missing or stale); the
    // bridge re-reads its cache, which dev-simulator.ps1 renews weekly.
    std::uint32_t fuel_refresh_elapsed_ms = 0;
    auto tick_fuel = [&](std::uint32_t elapsed_ms) {
        if (deterministic_run) return;
        fuel_refresh_elapsed_ms += elapsed_ms;
        if (fuel_refresh_elapsed_ms < aura::fuel_refresh_interval_ms(fuel_model.snapshot())) return;
        fuel_refresh_elapsed_ms = 0;
        refresh_fuel();
    };

    std::function<void()> cancel_voice_hook;
    auto cancel = [&] {
        if (provider) provider->cancel();
        if (cancel_voice_hook) cancel_voice_hook();
        pending = false;
        controller.dispatch({aura::EventType::Cancel});
    };
    std::function<aura::TurnRequest(std::string)> turn_request_hook;
    std::function<bool(const std::string&)> offline_command_hook;
    std::function<void(const std::string&)> local_answer_hook;
    std::function<void()> end_voice_turn_hook;
    auto start_turn = [&](std::string prompt) {
        auto calendar_intent = aura::analyze_greek_calendar_intent(prompt);
        // "Πότε γιορτάζει ο Γιώργος;" is answered here only for a listed name;
        // the assistant answers about anybody else.
        std::optional<aura::NamedayMatch> nameday_match;
        if (calendar_intent == aura::GreekCalendarIntent::NamedayOf) {
            const auto& status = device_status.snapshot();
            if (status.date_valid) nameday_match = aura::greek_nameday_of(prompt, status.year);
            if (!nameday_match) calendar_intent = aura::GreekCalendarIntent::None;
        }
        // Timers, reminders and settings come first: "βάλε υπενθύμιση ... ραδιόφωνο"
        // is a reminder, not a radio request.
        const bool offline_command = aura::parse_offline_command(prompt).kind != aura::OfflineCommandKind::None;
        const auto radio_intent = offline_command ? aura::RadioIntent{} : aura::analyze_radio_intent(prompt);
        const auto* spoken_station = radio_intent.kind == aura::RadioIntentKind::PlayStation ?
            radio_model.find_spoken(prompt) : nullptr;
        if (radio_intent.kind == aura::RadioIntentKind::Stop || spoken_station ||
            (radio_intent.explicit_radio && radio_intent.kind != aura::RadioIntentKind::None)) {
            if (aura::is_turn_active(controller.snapshot().state)) controller.dispatch({aura::EventType::Cancel});
            if (end_voice_turn_hook) end_voice_turn_hook();
            ui.show(aura::UiView::Radio);
            // "Βάλε ραδιόφωνο" with no station named plays the last one again
            // (or the first favourite); a station already on keeps playing.
            const auto* resume = radio_intent.kind == aura::RadioIntentKind::PlayStation && !spoken_station &&
                                         radio_model.snapshot().state == aura::RadioState::Idle ?
                radio_model.station_to_resume() : nullptr;
            if (radio_intent.kind == aura::RadioIntentKind::Stop) stop_radio();
            else if (spoken_station) play_radio(*spoken_station);
            else if (resume) play_radio(*resume);
            else apply_radio();
            return;
        }
        const auto weather_intent = aura::analyze_weather_intent(prompt);
        const bool answer_from_forecast = weather_intent.kind == aura::WeatherIntentKind::ShowSaved &&
                                          weather_model.snapshot().has_data;
        if (weather_intent.kind != aura::WeatherIntentKind::None && !answer_from_forecast) {
            // A weather request is answered by the Weather screen; close the
            // voice turn so the loop below does not re-submit a transcript.
            if (aura::is_turn_active(controller.snapshot().state)) controller.dispatch({aura::EventType::Cancel});
            if (end_voice_turn_hook) end_voice_turn_hook();
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
        const auto previous = controller.snapshot();
        if (pending) {
            // A repeated send while the same question is being answered waits for that answer.
            if (pending_generation == previous.turn_generation) return;
            // The reply belongs to a turn that already ended (an error or a reset); it is never shown.
            if (provider) provider->cancel();
            if (!retire_future(pending_reply, retired_replies)) {
                pending = false;
                controller.dispatch({aura::EventType::Fail, "PROVIDER_BUSY"});
                apply();
                return;
            }
            pending = false;
        }
        // A shown answer, confirmation card, error or listening screen closes first, so the
        // new question is the one on screen and in the conversation memory.
        if (previous.state != aura::AssistantState::Idle && previous.state != aura::AssistantState::Transcribing) {
            if (cancel_voice_hook) cancel_voice_hook();
            const auto audio_state = audio.diagnostics().state;
            if (audio_state == aura::AudioState::Playing || audio_state == aura::AudioState::Paused) audio.stop();
            controller.dispatch({aura::EventType::Reset});
        }
        if (prompt.empty()) prompt = text().capture_short_prompt;
        const bool started = controller.dispatch({aura::EventType::TurnStarted, prompt});
        apply();
        if (!started) return;
        if (offline_command_hook && offline_command_hook(prompt)) {
            apply();
            return;
        }
        if (calendar_intent != aura::GreekCalendarIntent::None) {
            const bool english = ui.language() == aura::InterfaceLanguage::English;
            const auto& strings = english ? aura::l10n::companion_en_us() : aura::l10n::companion_el_gr();
            const auto& status = device_status.snapshot();
            std::string answer = strings.calendar_date_unavailable;
            char formatted[384];
            if (nameday_match) {
                std::string days;
                const auto& dates = nameday_match->dates;
                for (std::size_t index = 0; index < dates.size(); ++index) {
                    if (index > 0) days += index + 1 == dates.size() ? strings.calendar_and : ", ";
                    days += std::to_string(dates[index].day) + '/' + std::to_string(dates[index].month);
                }
                std::snprintf(formatted, sizeof(formatted), strings.calendar_nameday_of_answer,
                              nameday_match->name.c_str(), days.c_str());
                answer = formatted;
            } else if (status.date_valid && calendar_intent == aura::GreekCalendarIntent::Namedays) {
                const auto calendar = aura::greek_calendar_day(status.year, status.month, status.day);
                if (calendar.namedays.empty()) {
                    answer = strings.briefing_no_namedays;
                } else {
                    std::snprintf(formatted, sizeof(formatted), strings.briefing_namedays_format,
                                  aura::join_namedays(calendar.namedays).c_str());
                    answer = formatted;
                }
            } else if (status.date_valid) {
                const auto next = aura::next_greek_holiday(status.year, status.month, status.day);
                if (next && next->days_until == 0) {
                    const auto* holiday = english ?
                        aura::greek_holiday_name_en(next->kind) : aura::greek_holiday_name_el(next->kind);
                    std::snprintf(formatted, sizeof(formatted), strings.calendar_holiday_today_answer, holiday);
                    answer = formatted;
                } else if (next) {
                    const auto* holiday = english ?
                        aura::greek_holiday_name_en(next->kind) : aura::greek_holiday_name_el(next->kind);
                    std::snprintf(formatted, sizeof(formatted), strings.calendar_next_holiday_answer,
                                  holiday, next->date.day, next->date.month);
                    answer = formatted;
                }
            }
            if (local_answer_hook) local_answer_hook(answer);
            else controller.dispatch({aura::EventType::FinalResult, answer});
            apply();
            return;
        }
        pending_generation = controller.snapshot().turn_generation;
        if (!retire_future(pending_reply, retired_replies)) {
            controller.dispatch({aura::EventType::Fail, "PROVIDER_BUSY"});
            apply();
            return;
        }
        aura::TurnRequest request;
        if (turn_request_hook) request = turn_request_hook(std::move(prompt));
        else request.prompt = std::move(prompt);
        pending_stream = std::make_shared<aura::AssistantStreamMailbox>();
        pending_stream_sequence = 0;
        request.stream = pending_stream;
        pending_reply = provider->start_turn(std::move(request));
        pending = true;
    };

    aura::simulator::CompanionContext companion_context;
    companion_context.controller = &controller;
    companion_context.ui = &ui;
    companion_context.audio = &audio;
    companion_context.aoraton = &aoraton;
    companion_context.weather = &weather_model;
    companion_context.fuel = &fuel_model;
    companion_context.status = &device_status;
    companion_context.start_turn = [&start_turn](std::string prompt) { start_turn(std::move(prompt)); };
    companion_context.cancel_turn = [&] {
        if (provider) provider->cancel();
        pending = false;
    };
    companion_context.on_voice_active = [&radio_service](bool active) { radio_service.set_hold(active); };
    companion_context.assistant_provider_name = [&]() {
        return provider ? std::string(provider->name()) : std::string{};
    };
    companion_context.web_search_available = [&]() {
        return live_provider && std::string(environment("AURA_BRIDGE_AI_PROVIDER", "codex")) == "codex";
    };
    if (ancs_enabled) {
        companion_context.on_phone_action = [&ancs_service](std::uint32_t uid, bool positive) {
            ancs_service.perform_action(uid, positive);
        };
        companion_context.on_phone_pair = [&ancs_service] {
            ancs_service.start();
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open", L"ms-settings:bluetooth", nullptr, nullptr, SW_SHOWNORMAL);
#endif
        };
        companion_context.on_phone_forget = [&ancs_service] {
            // Stop the private in-process transport immediately. Windows owns
            // the bond, so let the user explicitly remove it in system UI.
            ancs_service.stop();
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open", L"ms-settings:bluetooth", nullptr, nullptr, SW_SHOWNORMAL);
#endif
        };
    }
    companion_context.make_translator = [&]() -> std::unique_ptr<aura::IAssistantProvider> {
        if (live_provider) {
            return std::make_unique<aura::simulator::LocalCodexProvider>(environment("AURA_BRIDGE_TOKEN"),
                static_cast<std::uint16_t>(integer(environment("AURA_BRIDGE_PORT", "4778"), 4778)));
        }
        return std::make_unique<aura::MockProvider>(deterministic_run ? 0U : 600U);
    };
    companion_context.deterministic = deterministic_run;
    companion_context.store_dir = companion_store_dir;
    aura::simulator::CompanionRuntime companion(std::move(companion_context));
    if (language_override) companion.set_language(*language_override);
    ui.set_language(companion.language());
    ui.set_appearance(appearance_override ? *appearance_override : companion.appearance());
    if (ancs_enabled) ancs_service.start();
    auto poll_ancs = [&] {
        if (!ancs_enabled) return;
        aura::simulator::WindowsAncsEvent event;
        while (ancs_service.poll(event)) {
            if (event.kind == aura::simulator::WindowsAncsEvent::Kind::Link) {
                companion.phone().set_link(event.link,
                    event.link == aura::PhoneLinkState::Connected ? aura::PhonePlatform::Iphone :
                                                                   aura::PhonePlatform::Unknown,
                    event.link == aura::PhoneLinkState::Connected ? "iPhone" : "");
            } else if (event.kind == aura::simulator::WindowsAncsEvent::Kind::Remove) {
                companion.phone().remove(event.uid);
            } else {
                auto notification = std::move(event.notification);
                if (companion.clock().snapshot().clock_valid) {
                    notification.minute = companion.clock().snapshot().now_minute;
                }
                notification.demo = false;
                companion.phone().upsert(std::move(notification));
            }
        }
    };
    turn_request_hook = [&companion](std::string prompt) { return companion.turn_request(std::move(prompt)); };
    offline_command_hook = [&companion](const std::string& prompt) {
        return companion.handle_offline_command(prompt);
    };
    local_answer_hook = [&companion](const std::string& answer) { companion.answer_locally(answer); };
    end_voice_turn_hook = [&companion] { companion.end_voice_turn(); };
    provider_changed_hook = [&companion] { companion.provider_changed(); };
    cancel_voice_hook = [&companion] { companion.cancel_voice(); };

    aura::AuraUiCallbacks ui_callbacks;
    ui_callbacks.on_ask = [&] {
        ++interaction_counts.ask;
        // A new question interrupts a spoken answer instead of talking over it.
        if (aura::is_turn_active(controller.snapshot().state)) cancel();
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
    ui_callbacks.on_aoraton_pair_start = [&] {
        ++interaction_counts.aoraton_sign_in;
        return aoraton.begin_pairing();
    };
    ui_callbacks.on_aoraton_pair_confirm_sas = [&] { return aoraton.confirm_pairing_sas(); };
    ui_callbacks.on_aoraton_pair_cancel = [&] { aoraton.cancel_pairing(); };
    ui_callbacks.on_aoraton_unpair = [&] {
        ++interaction_counts.aoraton_sign_out;
        aoraton.unpair();
    };
    ui_callbacks.on_aoraton_unlock = [&](const std::string& passphrase) {
        ++interaction_counts.aoraton_unlock;
        interaction_counts.aoraton_unlock_bytes = static_cast<int>(passphrase.size());
        return aoraton.unlock_keys(passphrase);
    };
    // The invite link (it carries a room key) comes from the PC clipboard, never
    // through the control panel's event log, and is wiped after use.
    ui_callbacks.on_aoraton_paste_invite = [&] {
        ++interaction_counts.aoraton_paste;
        char* clipboard = SDL_GetClipboardText();
        std::string invite = clipboard ? clipboard : "";
        if (clipboard) {
            for (char* cursor = clipboard; *cursor; ++cursor) *cursor = 0;
            SDL_free(clipboard);
        }
        const bool queued = !invite.empty() && aoraton.add_invite_key(invite);
        volatile char* bytes = invite.empty() ? nullptr : &invite[0];
        for (std::size_t index = 0; index < invite.size(); ++index) bytes[index] = 0;
        return queued;
    };
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
    ui_callbacks.on_weather_refresh_interval = [&](aura::WeatherRefreshInterval interval) {
        ++interaction_counts.weather_interval;
        if (!weather_model.set_refresh_interval(interval)) return;
        if (!deterministic_run && !weather_store.save_refresh_interval(interval)) {
            std::cerr << "AURA weather: could not save refresh interval\n";
        }
        next_weather_refresh_at = std::chrono::steady_clock::now() +
            std::chrono::minutes(aura::weather_refresh_interval_minutes(interval));
        apply_weather();
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
    ui_callbacks.on_radio_play = [&](const aura::RadioStation& station) {
        ++interaction_counts.radio_play;
        play_radio(station);
    };
    ui_callbacks.on_radio_stop = [&] {
        ++interaction_counts.radio_stop;
        stop_radio();
    };
    ui_callbacks.on_radio_search = [&](const std::string& query) {
        ++interaction_counts.radio_search;
        search_radio(query);
    };
    ui_callbacks.on_radio_favorite = [&](const aura::RadioStation& station) {
        ++interaction_counts.radio_favorite;
        if (radio_model.toggle_favorite(station)) save_radio();
        apply_radio();
    };
    ui_callbacks.on_radio_move = [&](std::size_t index, int direction) {
        ++interaction_counts.radio_move;
        if (radio_model.move_favorite(index, direction)) save_radio();
        apply_radio();
    };
    ui_callbacks.on_launcher_app_move = [&](std::size_t from, std::size_t to) {
        ++interaction_counts.launcher_move;
        if (launcher_layout_model.move(from, to)) save_launcher_layout();
        apply_launcher_layout();
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
    // Settings > System. The simulator restarts in place: the window stays,
    // the state a real restart would lose is dropped, and the boot screen shows.
    ui_callbacks.on_system_action = [&](aura::SystemAction action) {
        ++interaction_counts.system_action;
        if (action == aura::SystemAction::PowerOff) {
            companion.set_powered_off(true);
            ui.power_off();
            return;
        }
        if (action == aura::SystemAction::FactoryReset) {
            // Captures and tests never touch the files of a real run.
            if (!deterministic_run) {
                weather_store.clear_location();
                weather_store.clear_settings();
            }
            stop_radio();
            radio_model = aura::RadioModel{};
            launcher_layout_model.reset();
            if (!deterministic_run) {
                std::error_code radio_error;
                std::filesystem::remove(radio_store_path, radio_error);
                std::error_code layout_error;
                std::filesystem::remove(launcher_layout_store_path, layout_error);
            }
            apply_radio();
            apply_launcher_layout();
            weather_model.clear_location();
            weather_model.set_refresh_interval(aura::kDefaultWeatherRefreshInterval);
            next_weather_refresh_at.reset();
            apply_weather();
            wifi_model.forget_saved_network();
            if (wifi_host.host_connected) {
                wifi_model.set_link(true, wifi_host.host_ssid, wifi_host.host_rssi_dbm, {}, aura::WifiOrigin::Host);
            }
            sync_device_link();
            apply_wifi();
            aoraton.unpair();
            ui.reset_preferences();
            companion.factory_reset();
        } else {
            companion.soft_restart();
        }
        ui.boot();
    };
    companion.install(ui_callbacks);
    {
        auto preview = ui_callbacks.on_sound_preview;
        ui_callbacks.on_sound_preview = [&interaction_counts, preview] {
            ++interaction_counts.sound_preview;
            preview();
        };
    }
    {
        auto preview = ui_callbacks.on_voice_preview;
        ui_callbacks.on_voice_preview = [&interaction_counts, preview](aura::AssistantVoice voice) {
            ++interaction_counts.voice_preview;
            preview(voice);
        };
    }
    {
        auto translate_record = ui_callbacks.on_translate_record;
        ui_callbacks.on_translate_record = [&interaction_counts, translate_record](aura::TranslateSide side) {
            ++(side == aura::TranslateSide::Mine ? interaction_counts.translate_mine : interaction_counts.translate_theirs);
            return translate_record(side);
        };
    }
    ui.create(display, std::move(ui_callbacks));
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
    // Captures and tests start with one labelled sample notification; a normal
    // run, like the device, starts with none.
    if (deterministic_run) controller.dispatch({aura::EventType::NotificationReceived, "DEMO"});
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
    ui.apply_radio(radio_model.snapshot());
    ui.apply_launcher_layout(launcher_layout_model.snapshot());
    ui.apply_fuel(fuel_model.snapshot());

    if (!deterministic_run && weather_model.snapshot().has_saved_location) {
        start_weather_fetch(weather_model.snapshot().saved_location, false);
    }

    if (!weather_live_city.empty()) {
        ui.show(aura::UiView::WeatherLocation);
        start_weather_search(weather_live_city, true);
    }

    if (!capture_scenario.empty() || !motion_scenario.empty()) {
        // Evidence is never touched: a real cursor over the capture window must
        // not hover, press or select anything.
        SDL_SetEventFilter([](void*, SDL_Event* event) -> int {
            switch (event->type) {
                case SDL_MOUSEMOTION:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                case SDL_MOUSEWHEEL:
                case SDL_FINGERDOWN:
                case SDL_FINGERUP:
                case SDL_FINGERMOTION: return 0;
                default: return 1;
            }
        }, nullptr);
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
            capture_scenario == "briefing" || capture_scenario == "always-on") {
            weather_model.fetch_succeeded(demo_weather_data(
                aura::WeatherCondition::PartlyCloudy, {}, ui.language()));
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
            // The ambient screen is the always-on clock (the Home orb is "home").
            companion.capture("always-on");
            apply();
        }
        else if (capture_scenario == "low-battery") {
            controller.dispatch({aura::EventType::BatteryChanged, {}, 12});
            device_status.set_power(12, false, aura::StatusOrigin::Simulated);
            ui.apply_status(device_status.snapshot());
            apply();
            ui.show(aura::UiView::Home);
        }
        else if (capture_scenario == "launcher" || capture_scenario == "quick-actions") ui.show(aura::UiView::Launcher);
        else if (capture_scenario == "timer") {
            // The end time follows the captured status clock (18:24), not this desktop's.
            companion.set_clock({2026, 10, 22, 18, 24, 0, 4});
            companion.apply();
            std::uint32_t timer_id = 0;
            companion.clock().start_timer(600, "DEMO", &timer_id);
            companion.clock().tick(138000);
            ui.open_clock_item(aura::ClockAlertKind::Timer, timer_id);
        }
        else if (capture_scenario == "launcher-reorder") ui.edit_launcher_layout();
        else if (capture_scenario == "wake") {
            controller.dispatch({aura::EventType::WakeDetected});
            apply();
        }
        else if (capture_scenario == "listening") {
            controller.dispatch({aura::EventType::WakeDetected});
            controller.dispatch({aura::EventType::BeginRecording});
            apply();
        } else if (capture_scenario == "thinking") {
            controller.dispatch({aura::EventType::TurnStarted, text().capture_thinking_prompt});
            apply();
        } else if (capture_scenario == "answer") {
            controller.dispatch({aura::EventType::TurnStarted, text().capture_short_prompt});
            controller.dispatch({aura::EventType::FinalResult, text().capture_short_answer});
            apply();
        } else if (capture_scenario == "answer-long") {
            controller.dispatch({aura::EventType::TurnStarted, text().capture_long_prompt});
            controller.dispatch({aura::EventType::FinalResult, text().capture_long_answer});
            apply();
        } else if (capture_scenario == "speaking") {
            controller.dispatch({aura::EventType::TurnStarted, text().capture_speaking_prompt});
            controller.dispatch({aura::EventType::FinalResult, text().capture_speaking_answer});
            apply();
            audio.play_test_tone(1600);
            ui.apply_audio(audio.diagnostics());
        } else if (capture_scenario == "settings") ui.show(aura::UiView::Settings);
        else if (capture_scenario == "language") ui.show(aura::UiView::LanguageSettings);
        else if (capture_scenario == "appearance") ui.show(aura::UiView::AppearanceSettings);
        else if (capture_scenario == "diagnostics") ui.show(aura::UiView::Diagnostics);
        else if (capture_scenario == "system") ui.show(aura::UiView::System);
        else if (capture_scenario == "system-restart") ui.show_system_confirmation(aura::SystemAction::Restart);
        else if (capture_scenario == "system-power-off") ui.show_system_confirmation(aura::SystemAction::PowerOff);
        else if (capture_scenario == "system-factory-reset") {
            ui.show_system_confirmation(aura::SystemAction::FactoryReset);
        }
        else if (capture_scenario == "power-off") ui.power_off();
        else if (capture_scenario == "boot") ui.boot();
        else if (capture_scenario == "weather-settings") ui.show(aura::UiView::WeatherSettings);
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
        } else if (capture_scenario == "radio") {
            const auto stations = aura::greek_default_stations();
            if (!stations.empty()) play_radio(stations.front());
            ui.show(aura::UiView::Radio);
        } else if (capture_scenario == "radio-search") {
            search_radio("rock");
            ui.show(aura::UiView::RadioSearch);
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
        } else if (capture_scenario.rfind("aoraton-account", 0) == 0) {
            // aoraton.com account screens with a DEMO label (no real account),
            // in the capture's language.
            const bool english_capture = ui.language() == aura::InterfaceLanguage::English;
            aura::AoratonSnapshot snapshot;
            snapshot.mock_mode = false;
            snapshot.runtime_mode = aura::AoratonRuntimeMode::Live;
            snapshot.pairing = aura::AoratonPairingState::Paired;
            snapshot.connection = aura::AoratonConnectionState::Connected;
            snapshot.account_label = english_capture ? "DEMO account" : "DEMO λογαριασμός";
            snapshot.key_state = aura::AoratonKeyState::Ready;
            snapshot.key_count = 3;
            aura::UiView view = aura::UiView::AoratonPair;
            if (capture_scenario == "aoraton-account-signin") {
                snapshot.pairing = aura::AoratonPairingState::NotPaired;
                snapshot.connection = aura::AoratonConnectionState::Disconnected;
                snapshot.account_label.clear();
                view = aura::UiView::AoratonRooms;
            } else if (capture_scenario == "aoraton-account-waiting") {
                snapshot.pairing = aura::AoratonPairingState::WaitingForBrowser;
                snapshot.pairing_seconds_remaining = 287;
            } else if (capture_scenario == "aoraton-account-timeout") {
                snapshot.pairing = aura::AoratonPairingState::Error;
                snapshot.last_error = "SIGN_IN_TIMEOUT";
            } else if (capture_scenario == "aoraton-account-reauth") {
                snapshot.pairing = aura::AoratonPairingState::ReauthorizationRequired;
                view = aura::UiView::AoratonRooms;
            } else if (capture_scenario == "aoraton-account-unlock") {
                snapshot.key_state = aura::AoratonKeyState::WrongPassphrase;
                view = aura::UiView::AoratonUnlock;
            } else if (capture_scenario == "aoraton-account-rooms") {
                aura::AoratonRoom open;
                open.id = "demo-open";
                open.display_name = english_capture ? "DEMO Family" : "DEMO Οικογένεια";
                open.preview = english_capture ? "DEMO See you tonight" : "DEMO Τα λέμε το βράδυ";
                open.unread = 2;
                aura::AoratonRoom locked;
                locked.id = "demo-locked";
                locked.display_name = english_capture ? "DEMO Work" : "DEMO Δουλειά";
                locked.key_available = false;
                snapshot.rooms = {open, locked};
                view = aura::UiView::AoratonRooms;
            } else if (capture_scenario == "aoraton-account-invite") {
                snapshot.key_state = aura::AoratonKeyState::NotMember;
            }
            snapshot.sequence = aoraton.snapshot().sequence + 1000;
            aoraton_capture_override = snapshot;
            ui.apply_aoraton(snapshot);
            ui.show(view);
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
            aoraton.prepare_send("demo-family", text().capture_message);
            ui.apply_aoraton(aoraton.snapshot());
            ui.show(aura::UiView::AoratonConfirm);
        } else if (capture_scenario == "aoraton-compose") {
            aoraton.select_room("demo-family");
            ui.apply_aoraton(aoraton.snapshot());
            ui.show(aura::UiView::AoratonCompose);
        } else if (capture_scenario == "weather-sunny" || capture_scenario == "weather-cloudy" ||
                   capture_scenario == "weather-rainy" || capture_scenario == "weather-stormy" ||
                   capture_scenario == "weather-snowy" || capture_scenario == "weather-night" ||
                   capture_scenario == "weather-night-cloudy" ||
                   capture_scenario == "weather-forecast" || capture_scenario == "weather-gateway" ||
                   capture_scenario == "weather-gateway-details") {
            aura::WeatherCondition condition = aura::WeatherCondition::Clear;
            if (capture_scenario == "weather-cloudy") condition = aura::WeatherCondition::Cloudy;
            else if (capture_scenario == "weather-rainy") condition = aura::WeatherCondition::Rain;
            else if (capture_scenario == "weather-stormy") condition = aura::WeatherCondition::Thunderstorm;
            else if (capture_scenario == "weather-snowy") condition = aura::WeatherCondition::Snow;
            else if (capture_scenario == "weather-night-cloudy") condition = aura::WeatherCondition::PartlyCloudy;
            const bool night = capture_scenario == "weather-night" ||
                               capture_scenario == "weather-night-cloudy";
            auto data = (capture_scenario == "weather-gateway" || capture_scenario == "weather-gateway-details") ?
                demo_tlesdetector_weather_data(ui.language()) :
                demo_weather_data(condition, {}, ui.language(), !night);
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
            weather_model.fetch_succeeded(demo_weather_data(
                aura::WeatherCondition::PartlyCloudy, {}, ui.language()));
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-loading") {
            weather_model.clear_location();
            const char* city = ui.language() == aura::InterfaceLanguage::English ? "Sparta" : "Σπάρτη";
            weather_model.restore_location(demo_weather_location(city, ui.language()));
            weather_model.begin_fetch(demo_weather_location(city, ui.language()), false);
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-no-city") {
            weather_model.clear_location();
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-offline") {
            weather_model.clear_location();
            weather_model.restore_location(demo_weather_location({}, ui.language()));
            weather_model.begin_fetch(demo_weather_location({}, ui.language()), false);
            weather_model.fetch_failed("NETWORK_UNAVAILABLE", true);
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-error") {
            weather_model.clear_location();
            weather_model.restore_location(demo_weather_location({}, ui.language()));
            weather_model.begin_fetch(demo_weather_location({}, ui.language()), false);
            weather_model.fetch_failed("WEATHER_PROVIDER_ERROR", false);
            apply_weather();
            ui.show(aura::UiView::Weather);
        } else if (capture_scenario == "weather-location") {
            weather_model.clear_location();
            apply_weather();
            ui.show(aura::UiView::WeatherLocation);
        } else if (capture_scenario == "weather-results") {
            weather_model.clear_location();
            const bool english = ui.language() == aura::InterfaceLanguage::English;
            weather_model.begin_search(english ? "Sparta" : "Σπάρτη");
            weather_model.search_succeeded({
                demo_weather_location(english ? "Sparta" : "Σπάρτη", ui.language()),
                demo_weather_location(english ? "Athens" : "Αθήνα", ui.language()),
                demo_weather_location(english ? "Patras" : "Πάτρα", ui.language())});
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
        } else if (capture_scenario == "calendar") {
            companion.capture("calendar");
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
        } else if (capture_scenario == "briefing-calendar") {
            device_status.set_date(2026, 12, 6, 0, aura::StatusOrigin::Simulated);
            ui.apply_status(device_status.snapshot());
            companion.capture("briefing");
            ui.show(aura::UiView::Briefing);
        } else if (capture_scenario == "briefing-fuel") {
            // Captures run without the bridge: labelled DEMO prices stand in for
            // the European Commission bulletin.
            aura::FuelPriceData demo_fuel;
            demo_fuel.period_date = "2026-10-19";
            demo_fuel.source = "European Commission Weekly Oil Bulletin";
            demo_fuel.source_url = "https://energy.ec.europa.eu/data-and-analysis/weekly-oil-bulletin_en";
            demo_fuel.unleaded95_millieur_per_litre = 1912;
            demo_fuel.diesel_millieur_per_litre = 1634;
            demo_fuel.demo = true;
            fuel_model.begin_fetch();
            fuel_model.fetch_succeeded(std::move(demo_fuel));
            apply_fuel();
            companion.capture("briefing");
            ui.show(aura::UiView::Briefing);
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
            controller.dispatch({aura::EventType::TurnStarted, text().capture_thinking_prompt});
            apply();
        } else if (motion_scenario == "speaking") {
            controller.dispatch({aura::EventType::TurnStarted, text().capture_speaking_prompt});
            controller.dispatch({aura::EventType::FinalResult, text().capture_speaking_answer});
            apply();
            audio.play_test_tone(5000);
            ui.apply_audio(audio.diagnostics());
        } else if (motion_scenario == "system-restart") {
            apply();
            ui.show_system_confirmation(aura::SystemAction::Restart);
        } else if (motion_scenario == "weather-clear" || motion_scenario == "weather-cloudy" ||
                   motion_scenario == "weather-rain" || motion_scenario == "weather-storm" ||
                   motion_scenario == "weather-snow" || motion_scenario == "weather-night" ||
                   motion_scenario == "weather-night-cloudy" ||
                   motion_scenario == "weather-wind") {
            aura::WeatherCondition condition = aura::WeatherCondition::Clear;
            if (motion_scenario == "weather-cloudy") condition = aura::WeatherCondition::Cloudy;
            else if (motion_scenario == "weather-rain") condition = aura::WeatherCondition::Rain;
            else if (motion_scenario == "weather-storm") condition = aura::WeatherCondition::Thunderstorm;
            else if (motion_scenario == "weather-snow") condition = aura::WeatherCondition::Snow;
            else if (motion_scenario == "weather-wind") condition = aura::WeatherCondition::Windy;
            else if (motion_scenario == "weather-night-cloudy") condition = aura::WeatherCondition::PartlyCloudy;
            const bool night = motion_scenario == "weather-night" ||
                               motion_scenario == "weather-night-cloudy";
            auto data = demo_weather_data(condition, {}, ui.language(), !night);
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
            aura::UiView::Home, aura::UiView::Launcher,
            aura::UiView::Notifications, aura::UiView::Settings, aura::UiView::Audio,
            aura::UiView::AoratonRooms, aura::UiView::AoratonPair, aura::UiView::AoratonConversation,
            aura::UiView::AoratonCompose, aura::UiView::AoratonConfirm,
            aura::UiView::Weather, aura::UiView::WeatherSettings, aura::UiView::WeatherLocation,
            aura::UiView::Calendar,
            aura::UiView::Battery, aura::UiView::VoiceSettings,
            aura::UiView::Diagnostics, aura::UiView::Wifi, aura::UiView::WifiPassword,
            aura::UiView::WifiDetails, aura::UiView::Clock, aura::UiView::ClockNew, aura::UiView::TimerNew,
            aura::UiView::AlarmNew, aura::UiView::ReminderNew, aura::UiView::Notes, aura::UiView::NoteNew,
            aura::UiView::Briefing, aura::UiView::Translate, aura::UiView::PhoneLink,
            aura::UiView::DisplaySettings, aura::UiView::AppearanceSettings, aura::UiView::Update,
            aura::UiView::Radio, aura::UiView::RadioSearch, aura::UiView::Home,
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
            // Observed text may quote anything (a raw reply): the report stays valid JSON.
            std::string escaped;
            for (const char ch : observed) {
                if (ch == '"' || ch == '\\') escaped += '\\';
                if (static_cast<unsigned char>(ch) >= 0x20) escaped += ch;
            }
            results.push_back({std::move(action), std::move(point), passed, std::move(escaped)});
        };
        auto pump = [&](int iterations = 9) {
            for (int i = 0; i < iterations; ++i) {
                audio.tick(8);
                if (aoraton_fail_closed_transport) aoraton_fail_closed_transport->poll();
                aoraton.tick(8);
                poll_wifi(std::chrono::steady_clock::now());
                poll_radio();
                poll_fuel();
                apply();
                ui.apply_audio(audio.diagnostics());
                ui.apply_aoraton(aoraton_capture_override ? *aoraton_capture_override : aoraton.snapshot());
                ui.apply_status(device_status.snapshot());
                ui.apply_weather(weather_model.snapshot());
                ui.apply_wifi(wifi_model.snapshot());
                ui.apply_radio(radio_model.snapshot());
                ui.apply_fuel(fuel_model.snapshot());
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
        // A finger swipe. LVGL's gesture detector restarts its distance on any
        // input read that sees no movement, and a read can land before SDL has
        // delivered the step; so each step moves past the 50 px gesture limit
        // and any read that sees one step is enough.
        auto swipe = [&](int x1, int y1, int x2, int y2) {
            push_motion(x1, y1);
            pump(2);
            push_button(SDL_MOUSEBUTTONDOWN, x1, y1);
            pump(2);
            const int steps = std::max(2, std::max(std::abs(x2 - x1), std::abs(y2 - y1)) / 64);
            for (int step = 1; step <= steps; ++step) {
                push_motion(x1 + (x2 - x1) * step / steps, y1 + (y2 - y1) * step / steps, SDL_BUTTON_LMASK);
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
            // Home at the root, then `view`: Back returns Home, never to a
            // screen left over from an earlier check.
            ui.show_from_home(view);
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
        if (auto* grid = find_scrollable(ui.root())) lv_obj_scroll_to_y(grid, 680, LV_ANIM_OFF);
        pump(3);
        tap(96, 360);
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
        // starting at y=96 (Wi-Fi, language, provider, motion, weather, sound, status, system).
        auto settings_row_y = [](int index, int scroll) { return 96 + index * 96 + 44 - scroll; };
        auto scroll_view_to = [&](int y) {
            lv_obj_update_layout(ui.root());
            if (auto* scroller = find_scrollable(ui.root())) lv_obj_scroll_to_y(scroller, y, LV_ANIM_OFF);
            pump(3);
        };
        reset_to(aura::UiView::Settings);
        const int rapid_provider_before = interaction_counts.provider;
        rapid_double_tap(220, settings_row_y(3, 0));
        record("settings.provider", "rapid-double-tap",
               interaction_counts.provider == rapid_provider_before + 1,
               std::to_string(interaction_counts.provider - rapid_provider_before));

        reset_to(aura::UiView::Settings);
        tap(220, settings_row_y(1, 0));
        const bool language_page = ui.view() == aura::UiView::LanguageSettings;
        tap(220, 236);
        const bool english_selected = ui.language() == aura::InterfaceLanguage::English;
        tap(48, 48);
        tap(220, settings_row_y(1, 0));
        tap(220, 140);
        const bool greek_restored = ui.language() == aura::InterfaceLanguage::Greek;
        tap(48, 48);
        record("settings.language", "english-then-greek", language_page && english_selected && greek_restored,
               english_selected ? "English copy and locale applied" : "English locale not applied");

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

        controller.dispatch({aura::EventType::TurnStarted, text().capture_short_prompt});
        controller.dispatch({aura::EventType::FinalResult, text().capture_short_answer});
        apply(); pump(4);
        const int ask_again_before = interaction_counts.ask;
        tap(240, 420);
        record("assistant.ask_again", "button-center",
               interaction_counts.ask == ask_again_before + 1 &&
               controller.snapshot().state == aura::AssistantState::Listening,
               aura::state_name(controller.snapshot().state));

        // Typed questions (the control panel's prompt): one sent over the listening screen
        // replaces it, and a repeat while it is being answered waits instead of failing BUSY.
        start_turn(text().capture_long_prompt);
        start_turn(text().capture_long_prompt);
        record("assistant.prompt_repeat", "while-thinking",
               pending && controller.snapshot().state == aura::AssistantState::Thinking &&
               controller.snapshot().user_text == text().capture_long_prompt,
               aura::state_name(controller.snapshot().state));
        cancel();
        controller.dispatch({aura::EventType::TurnStarted, text().capture_short_prompt});
        controller.dispatch({aura::EventType::FinalResult, text().capture_short_answer});
        apply(); pump(4);
        start_turn(text().capture_long_prompt);
        record("assistant.prompt_after_answer", "replaces-question",
               controller.snapshot().state == aura::AssistantState::Thinking &&
               controller.snapshot().user_text == text().capture_long_prompt,
               aura::state_name(controller.snapshot().state));
        cancel();
        apply(); pump(4);

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

        for (const auto& point : row_points(settings_row_y(3, 0))) {
            reset_to(aura::UiView::Settings);
            const int provider_before = interaction_counts.provider;
            const bool live_before = live_provider;
            tap(point.x, point.y);
            record("settings.provider", point.name,
                   interaction_counts.provider == provider_before + 1 && live_provider != live_before,
                   live_provider ? "codex" : "mock");
        }
        for (const auto& point : row_points(settings_row_y(4, 96))) {
            reset_to(aura::UiView::Settings);
            scroll_view_to(96);
            const bool reduced_before = ui.reduced_motion();
            tap(point.x, point.y);
            record("settings.motion", point.name, ui.reduced_motion() != reduced_before,
                   ui.reduced_motion() ? "reduced" : "full");
        }
        {
            // Every check starts from Home: after reset_to, Back never reaches a
            // screen left over from an earlier check.
            reset_to(aura::UiView::Settings);
            tap(240, settings_row_y(1, 0));
            const bool language_opened = ui.view() == aura::UiView::LanguageSettings;
            reset_to(aura::UiView::Clock);
            tap(48, 48);
            record("harness.reset_to", "back-returns-home", language_opened && ui.view() == aura::UiView::Home,
                   std::to_string(static_cast<int>(ui.view())));
        }
        // Lower rows are reached by scrolling the list while preserving 80 px targets.
        const struct { const char* action; int index; int scroll; aura::UiView expected; } setting_routes[] = {
            {"settings.weather", 5, 296, aura::UiView::WeatherSettings},
            {"settings.audio", 6, 392, aura::UiView::Audio},
            {"settings.status", 7, 392, aura::UiView::Battery},
            {"settings.system", 8, 488, aura::UiView::System},
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
        reset_to(aura::UiView::WeatherSettings);
        // Leave the UI debounce window used by the preceding System-row tap.
        pump(4);
        const int weather_interval_before = interaction_counts.weather_interval;
        tap(129, 262);
        record("weather.settings_interval", "15-minute-tile",
               interaction_counts.weather_interval == weather_interval_before + 1 &&
               weather_model.snapshot().refresh_interval == aura::WeatherRefreshInterval::Minutes15,
               std::to_string(aura::weather_refresh_interval_minutes(weather_model.snapshot().refresh_interval)));
        pump(8);
        tap(220, 136);
        record("weather.settings_location", "location-row", ui.view() == aura::UiView::WeatherLocation,
               std::to_string(static_cast<int>(ui.view())));
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
        scroll_view_to(96);
        tap(184, settings_row_y(4, 96));
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
        scroll_view_to(296);
        tap(184, settings_row_y(5, 296));
        tap(48, 48);
        record("weather_settings.back", "header-button",
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

        // Sound: the volume slider (y 211), then switch rows on a 96 px pitch.
        // Exercise the enabled paths explicitly. Normal simulator capability
        // discovery may correctly disable these controls when no speech service
        // is running, which is a different state from a broken touch target.
        aura::CompanionCapabilities voice_test_capabilities{};
        voice_test_capabilities.microphone = true;
        voice_test_capabilities.speech_recognition = true;
        voice_test_capabilities.speech_output = true;
        ui.set_capabilities(voice_test_capabilities);
        reset_to(aura::UiView::Audio);
        drag(60, 211, 430, 211);
        record("audio.volume", "slider-drag", audio.diagnostics().volume > 0.75F,
               std::to_string(audio.diagnostics().volume));
        reset_to(aura::UiView::Audio);
        const bool muted_before = audio.diagnostics().muted;
        tap(220, 290);
        record("audio.mute", "row-center", audio.diagnostics().muted != muted_before,
               audio.diagnostics().muted ? "muted" : "unmuted");
        tap(220, 290);
        reset_to(aura::UiView::Audio);
        const bool chime_before = companion.sound().message_chime;
        tap(220, 386);
        record("audio.chime", "row-center", companion.sound().message_chime != chime_before,
               companion.sound().message_chime ? "on" : "off");
        tap(220, 386);
        reset_to(aura::UiView::Audio);
        scroll_view_to(150);
        const bool spoken_before = companion.sound().spoken_replies;
        tap(220, 332);
        record("audio.spoken", "row-center", companion.sound().spoken_replies != spoken_before,
               companion.sound().spoken_replies ? "on" : "off");
        tap(220, 332);
        reset_to(aura::UiView::Audio);
        scroll_view_to(246);
        const bool continuous_before = companion.sound().continuous_conversation;
        tap(220, 332);
        record("audio.continuous", "row-center",
               companion.sound().continuous_conversation != continuous_before,
               companion.sound().continuous_conversation ? "on" : "off");
        tap(220, 332);
        reset_to(aura::UiView::Audio);
        scroll_view_to(342);
        const bool barge_before = companion.sound().barge_in;
        tap(220, 332);
        record("audio.barge_in", "row-center", companion.sound().barge_in != barge_before,
               companion.sound().barge_in ? "on" : "off");
        tap(220, 332);
        reset_to(aura::UiView::Audio);
        scroll_view_to(452);
        const int preview_before = interaction_counts.sound_preview;
        tap(240, 408);
        record("audio.preview", "button-center", interaction_counts.sound_preview == preview_before + 1,
               aura::audio_state_name(audio.diagnostics().state));

        reset_to(aura::UiView::VoiceSettings);
        tap(220, 140);
        const bool athina_selected = companion.sound().assistant_voice == aura::AssistantVoice::Athina;
        const int voice_preview_before = interaction_counts.voice_preview;
        tap(240, 336);
        const bool preview_requested = interaction_counts.voice_preview == voice_preview_before + 1;
        tap(220, 236);
        record("voice.select_preview", "rows-and-button",
               athina_selected && preview_requested &&
                   companion.sound().assistant_voice == aura::AssistantVoice::Nestoras,
               std::to_string(interaction_counts.voice_preview - voice_preview_before));

        // Diagnostics keeps the speaker checks: a test tone and the demo WAV.
        reset_to(aura::UiView::Diagnostics);
        lv_obj_update_layout(ui.root());
        if (auto* tone = find_sized_clickable(ui.root(), (432 - 40 - 12) / 2, 80)) {
            lv_obj_scroll_to_view_recursive(tone, LV_ANIM_OFF);
            pump(3);
            lv_area_t area{};
            lv_obj_get_coords(tone, &area);
            const int tone_before = interaction_counts.audio_tone;
            tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
            record("diagnostics.tone", "button-center", interaction_counts.audio_tone == tone_before + 1,
                   aura::audio_state_name(audio.diagnostics().state));
            lv_obj_get_coords(lv_obj_get_child(lv_obj_get_parent(tone), 1), &area);
            const int wav_before = interaction_counts.audio_wav;
            tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
            record("diagnostics.wav", "button-center", interaction_counts.audio_wav == wav_before + 1,
                   aura::audio_state_name(audio.diagnostics().state));
        } else {
            record("diagnostics.tone", "button-center", false, "not found");
        }
        audio.stop();

        // System: software, diagnostics, then power actions that ask first.
        // Rows at list y 0 and 96; after the "Power" caption, restart 240,
        // power off 336, factory reset 432.
        auto wait_for_home = [&] {
            for (int round = 0; round < 40 && ui.view() != aura::UiView::Home; ++round) pump(9);
        };
        reset_to(aura::UiView::System);
        tap(220, settings_row_y(0, 0));
        record("system.update", "row-center", ui.view() == aura::UiView::Update,
               std::to_string(static_cast<int>(ui.view())));
        reset_to(aura::UiView::System);
        tap(220, settings_row_y(1, 0));
        record("system.diagnostics", "row-center", ui.view() == aura::UiView::Diagnostics,
               std::to_string(static_cast<int>(ui.view())));
        reset_to(aura::UiView::System);
        tap(220, 380);
        const bool restart_asks = ui.view() == aura::UiView::SystemConfirm;
        const int restart_before = interaction_counts.system_action;
        tap(352, 420);
        const bool booting = ui.view() == aura::UiView::Boot;
        wait_for_home();
        record("system.restart", "confirm",
               restart_asks && booting && interaction_counts.system_action == restart_before + 1 &&
               ui.view() == aura::UiView::Home, std::to_string(static_cast<int>(ui.view())));
        // The orbit check below needs full motion; earlier checks may leave reduced motion on.
        if (ui.reduced_motion()) {
            reset_to(aura::UiView::Settings);
            scroll_view_to(96);
            tap(240, settings_row_y(4, 96));
        }
        reset_to(aura::UiView::System);
        scroll_view_to(120);
        tap(220, 356);
        const bool power_asks = ui.view() == aura::UiView::SystemConfirm;
        tap(352, 420);
        const bool off = ui.view() == aura::UiView::PowerOff;
        {
            // The power-off orb is the small "quiet" one: its 6 px dot circles at
            // radius 56 around (240, 220), inside its rings, not at radius 72.
            // Enough frames for the 33 ms animation step to move it.
            pump(12);
            std::function<lv_obj_t*(lv_obj_t*)> find_dot = [&](lv_obj_t* object) -> lv_obj_t* {
                if (lv_obj_get_width(object) == 6 && lv_obj_get_height(object) == 6) return object;
                for (std::uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
                    if (auto* found = find_dot(lv_obj_get_child(object, static_cast<int32_t>(index)))) return found;
                }
                return nullptr;
            };
            lv_obj_update_layout(ui.root());
            auto* dot = find_dot(ui.root());
            double radius = 0.0;
            if (dot) {
                const double dx = lv_obj_get_x(dot) + 3 - 240.0;
                const double dy = lv_obj_get_y(dot) + 3 - 220.0;
                radius = std::sqrt(dx * dx + dy * dy);
            }
            record("system.power_off_orbit", "inside-quiet-rings", dot && std::fabs(radius - 56.0) <= 2.0,
                   std::to_string(radius));
        }
        tap(240, 240);
        const bool tap_ignored = ui.view() == aura::UiView::PowerOff;
        long_press(240, 240);
        const bool powering_on = ui.view() == aura::UiView::Boot;
        wait_for_home();
        record("system.power", "off-then-hold",
               power_asks && off && tap_ignored && powering_on && ui.view() == aura::UiView::Home,
               std::to_string(static_cast<int>(ui.view())));
        reset_to(aura::UiView::System);
        scroll_view_to(120);
        tap(220, 452);
        const bool reset_asks = ui.view() == aura::UiView::SystemConfirm;
        const int reset_before = interaction_counts.system_action;
        tap(128, 420);
        record("system.reset_cancel", "cancel",
               reset_asks && ui.view() == aura::UiView::System && interaction_counts.system_action == reset_before,
               std::to_string(static_cast<int>(ui.view())));

        // Back returns to a list where it was left.
        reset_to(aura::UiView::Settings);
        scroll_view_to(488);
        tap(220, settings_row_y(8, 488));
        const bool opened_system = ui.view() == aura::UiView::System;
        tap(56, 56);
        lv_obj_update_layout(ui.root());
        auto* settings_list = find_scrollable(ui.root());
        const int kept = settings_list ? static_cast<int>(lv_obj_get_scroll_y(settings_list)) : -1;
        record("nav.back_keeps_scroll", "header-back",
               opened_system && ui.view() == aura::UiView::Settings && kept == 488, std::to_string(kept));

        controller.dispatch({aura::EventType::Reset});
        controller.dispatch({aura::EventType::TurnStarted, text().capture_speaking_prompt});
        controller.dispatch({aura::EventType::FinalResult, text().capture_speaking_answer});
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
        aoraton.prepare_send("demo-family", text().capture_message);
        ui.apply_aoraton(aoraton.snapshot());
        ui.show(aura::UiView::AoratonConfirm);
        pump(4);
        const int message_cancel_before = interaction_counts.aoraton_cancel;
        tap(104, 420);
        record("aoraton.cancel", "button-center",
               interaction_counts.aoraton_cancel == message_cancel_before + 1 &&
               ui.view() == aura::UiView::AoratonConversation,
               std::to_string(static_cast<int>(ui.view())));

        // aoraton.com account screens, shown through a live-mode snapshot (DEMO label).
        std::uint64_t account_serial = 0;
        auto account_snapshot = [&](aura::AoratonPairingState pairing) {
            aura::AoratonSnapshot snapshot;
            snapshot.mock_mode = false;
            snapshot.runtime_mode = aura::AoratonRuntimeMode::Live;
            snapshot.pairing = pairing;
            snapshot.connection = pairing == aura::AoratonPairingState::Paired ?
                aura::AoratonConnectionState::Connected : aura::AoratonConnectionState::Disconnected;
            if (pairing == aura::AoratonPairingState::Paired) snapshot.account_label = "DEMO λογαριασμός";
            snapshot.key_state = aura::AoratonKeyState::Ready;
            snapshot.key_count = 2;
            snapshot.sequence = aoraton.snapshot().sequence + 5000 + (++account_serial);
            aoraton_capture_override = snapshot;
            ui.apply_aoraton(snapshot);
        };
        account_snapshot(aura::AoratonPairingState::NotPaired);
        reset_to(aura::UiView::AoratonRooms);
        const int sign_in_before = interaction_counts.aoraton_sign_in;
        tap(240, 420);
        record("aoraton.sign_in", "button-center", interaction_counts.aoraton_sign_in == sign_in_before + 1,
               std::to_string(interaction_counts.aoraton_sign_in - sign_in_before));
        account_snapshot(aura::AoratonPairingState::Paired);
        reset_to(aura::UiView::AoratonPair);
        const int paste_before = interaction_counts.aoraton_paste;
        tap(240, 332);
        record("aoraton.paste_invite", "row-center",
               interaction_counts.aoraton_paste == paste_before + 1 && ui.view() == aura::UiView::AoratonPair,
               std::to_string(interaction_counts.aoraton_paste - paste_before));
        tap(240, 236);
        record("aoraton.keys_row", "row-center", ui.view() == aura::UiView::AoratonUnlock,
               std::to_string(static_cast<int>(ui.view())));
        const int unlock_before = interaction_counts.aoraton_unlock;
        tap(432, 48);
        const bool empty_refused = interaction_counts.aoraton_unlock == unlock_before;
        for (int key = 0; key < 8; ++key) tap(25 + key * 477 / 10, 243);
        tap(432, 48);
        record("aoraton.unlock_submit", "typed-passphrase",
               empty_refused && interaction_counts.aoraton_unlock == unlock_before + 1 &&
               interaction_counts.aoraton_unlock_bytes == 8,
               std::to_string(interaction_counts.aoraton_unlock_bytes));
        // The vault answers while the passphrase screen is open: back to the account.
        const bool typing_passphrase = ui.view() == aura::UiView::AoratonUnlock;
        for (const auto key_state : {aura::AoratonKeyState::Unlocking, aura::AoratonKeyState::Ready}) {
            auto vault = *aoraton_capture_override;
            vault.key_state = key_state;
            vault.key_count = 3;
            vault.sequence = aoraton.snapshot().sequence + 5000 + (++account_serial);
            aoraton_capture_override = vault;
            ui.apply_aoraton(vault);
            pump(2);
        }
        record("aoraton.unlock_done", "vault-answer",
               typing_passphrase && ui.view() == aura::UiView::AoratonPair,
               std::to_string(static_cast<int>(ui.view())));
        reset_to(aura::UiView::AoratonPair);
        const int sign_out_before = interaction_counts.aoraton_sign_out;
        tap(240, 432);
        record("aoraton.sign_out", "button-center",
               interaction_counts.aoraton_sign_out == sign_out_before + 1 && ui.view() == aura::UiView::AoratonRooms,
               std::to_string(interaction_counts.aoraton_sign_out - sign_out_before));
        aoraton_capture_override.reset();
        ui.apply_aoraton(aoraton.snapshot());

        controller.dispatch({aura::EventType::Reset});
        controller.dispatch({aura::EventType::TurnStarted, text().capture_thinking_prompt});
        apply(); pump(5);
        tap(240, 420);
        record("assistant.cancel", "during-animation",
               controller.snapshot().state == aura::AssistantState::Idle && ui.view() == aura::UiView::Launcher,
               aura::state_name(controller.snapshot().state));

        controller.dispatch({aura::EventType::Reset});
        controller.dispatch({aura::EventType::TurnStarted, text().capture_long_prompt});
        controller.dispatch({aura::EventType::FinalResult, text().capture_long_answer});
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
        controller.dispatch({aura::EventType::TurnStarted, text().capture_short_prompt});
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
        scroll_view_to(220);
        tap(240, 420);
        record("status.mute", "button-center", audio.diagnostics().muted != status_muted_before,
               audio.diagnostics().muted ? "muted" : "unmuted");

        // ---- v13 "Companion": real SDL taps on the new screens ----------------
        auto view_text = [&] { return std::to_string(static_cast<int>(ui.view())); };
        // The label with this exact text anywhere under object, if any.
        std::function<lv_obj_t*(lv_obj_t*, const char*)> find_text = [&](lv_obj_t* object,
                                                                          const char* text) -> lv_obj_t* {
            if (lv_obj_check_type(object, &lv_label_class) && std::strcmp(lv_label_get_text(object), text) == 0) {
                return object;
            }
            for (std::uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
                if (auto* found = find_text(lv_obj_get_child(object, static_cast<int32_t>(index)), text)) return found;
            }
            return nullptr;
        };
        auto shows_text = [&](const char* text) { return find_text(ui.root(), text) != nullptr; };
        // Copy in either language.
        auto shows_copy = [&](const char* aura::l10n::CompanionStrings::*field) {
            return shows_text(aura::l10n::companion_el_gr().*field) || shows_text(aura::l10n::companion_en_us().*field);
        };
        // A visible, non-transparent object above the screens.
        auto veil_visible = [] {
            for (std::uint32_t index = 0; index < lv_obj_get_child_count(lv_layer_top()); ++index) {
                auto* child = lv_obj_get_child(lv_layer_top(), static_cast<int32_t>(index));
                if (!lv_obj_is_hidden(child) && lv_obj_get_style_bg_opa(child, LV_PART_MAIN) > 0) {
                    return true;
                }
            }
            return false;
        };
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

        reset_to(aura::UiView::Launcher);
        tap(96, 188);
        const bool conversation_started = ui.view() == aura::UiView::Assistant;
        tap(352, 420);
        record("conversation.cancel", "returns-to-apps",
               conversation_started && ui.view() == aura::UiView::Launcher &&
                   controller.snapshot().state == aura::AssistantState::Idle,
               view_text());

        // Launcher: row three contains Today, Radio and Notifications.
        const Point third_row[]{{"today", 96, 360}, {"radio", 240, 360}, {"notifications", 384, 360}};
        const aura::UiView third_row_views[]{aura::UiView::Briefing, aura::UiView::Radio,
                                             aura::UiView::Notifications};
        for (int index = 0; index < 3; ++index) {
            reset_to(aura::UiView::Launcher);
            scroll_view_to(148);
            tap(third_row[index].x, third_row[index].y);
            record("launcher.row3", third_row[index].name, ui.view() == third_row_views[index], view_text());
        }
        reset_to(aura::UiView::Launcher);
        scroll_view_to(680);
        tap(96, 360);
        record("launcher.row4", "settings", ui.view() == aura::UiView::Settings, view_text());

        // Launcher edit mode: Edit, pick an app, pick its new place. The order
        // is stored at once, and the grid opens apps again when editing ends.
        launcher_layout_model.reset();
        apply_launcher_layout();
        reset_to(aura::UiView::Launcher);
        const int reorder_moves_before = interaction_counts.launcher_move;
        const int reorder_asks_before = interaction_counts.ask;
        tap(432, 48);
        tap(96, 164);
        const bool reorder_selected = ui.view() == aura::UiView::Launcher &&
                                      interaction_counts.ask == reorder_asks_before;
        tap(384, 336);
        const auto& reordered = launcher_layout_model.snapshot().order;
        record("launcher.reorder", "select-then-place",
               reorder_selected && interaction_counts.launcher_move == reorder_moves_before + 1 &&
                   reordered[5] == aura::LauncherApp::Conversation &&
                   reordered[0] == aura::LauncherApp::Weather && ui.view() == aura::UiView::Launcher,
               std::to_string(interaction_counts.launcher_move - reorder_moves_before));
        tap(432, 48);
        const int reorder_weather_before = interaction_counts.weather_open;
        tap(96, 164);
        record("launcher.reorder", "done-then-open",
               ui.view() == aura::UiView::Weather && interaction_counts.weather_open == reorder_weather_before + 1,
               view_text());
        // Leaving by a swipe also ends editing. The grid is reached the way the
        // owner reaches it (Home, then Apps), so Back has a real Home below it.
        reset_to(aura::UiView::Home);
        tap(424, 436);
        const bool reorder_on_grid = ui.view() == aura::UiView::Launcher;
        tap(432, 48);
        swipe(60, 300, 420, 300);
        const bool reorder_left = reorder_on_grid && ui.view() == aura::UiView::Home;
        tap(424, 436);
        const int reorder_weather_again = interaction_counts.weather_open;
        tap(96, 164);
        record("launcher.reorder", "swipe-ends-edit",
               reorder_left && ui.view() == aura::UiView::Weather &&
                   interaction_counts.weather_open == reorder_weather_again + 1,
               view_text());
        launcher_layout_model.reset();
        apply_launcher_layout();

        reset_to(aura::UiView::Launcher);
        scroll_view_to(680);
        tap(240, 360);
        record("launcher.row4", "organizer", ui.view() == aura::UiView::Calendar, view_text());
        tap(432, 48);
        record("organizer.new", "plus", ui.view() == aura::UiView::OrganizerNew, view_text());
        companion.organizer().clear();
        companion.organizer().add_task("DEMO task", 0);
        companion.apply();
        ui.open_organizer(aura::OrganizerKind::Task);
        pump(3);
        tap(180, 184);
        record("organizer.task", "toggle",
               companion.organizer().snapshot().tasks.size() == 1 &&
                   companion.organizer().snapshot().tasks.front().completed,
               companion.organizer().snapshot().tasks.empty() ? "missing" : "completed");
        tap(424, 184);
        record("organizer.task", "delete", companion.organizer().snapshot().tasks.empty(),
               std::to_string(companion.organizer().snapshot().tasks.size()));

        // A new song title must not rebuild the search keyboard while the owner types.
        {
            play_radio(radio_model.snapshot().favorites.front());
            reset_to(aura::UiView::Radio);
            tap(432, 48);
            std::function<lv_obj_t*(lv_obj_t*)> find_textarea = [&](lv_obj_t* object) -> lv_obj_t* {
                if (lv_obj_check_type(object, &lv_textarea_class)) return object;
                for (std::uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
                    if (auto* found = find_textarea(lv_obj_get_child(object, static_cast<int32_t>(index)))) return found;
                }
                return nullptr;
            };
            // A marker on the object itself: a rebuilt keyboard is a new object without it.
            static int marker = 0;
            auto* area_before = find_textarea(ui.root());
            if (area_before) lv_obj_set_user_data(area_before, &marker);
            radio_model.set_title("DEMO · Νέο τραγούδι");
            apply_radio();
            pump(2);
            auto* area_after = find_textarea(ui.root());
            record("radio.search_typing", "title-update-keeps-keyboard",
                   ui.view() == aura::UiView::RadioSearch && area_after &&
                       lv_obj_get_user_data(area_after) == &marker,
                   view_text());
            stop_radio();
        }

        radio_model = aura::RadioModel{};
        apply_radio();
        reset_to(aura::UiView::Radio);
        const auto first_station_before_move = radio_model.snapshot().favorites.front().id;
        tap(240, 180);
        const int radio_move_before = interaction_counts.radio_move;
        tap(422, 280);
        record("radio.reorder", "first-station-down",
               interaction_counts.radio_move == radio_move_before + 1 &&
                   radio_model.snapshot().favorites[1].id == first_station_before_move,
               std::to_string(interaction_counts.radio_move - radio_move_before));
        tap(240, 180);
        const int radio_play_before = interaction_counts.radio_play;
        tap(220, 272);
        record("radio.play", "first-favourite",
               interaction_counts.radio_play == radio_play_before + 1 &&
               radio_model.snapshot().state == aura::RadioState::Playing,
               aura::radio_state_name(radio_model.snapshot().state));
        const int radio_favourite_before = interaction_counts.radio_favorite;
        tap(352, 252);
        record("radio.favorite", "button-center",
               interaction_counts.radio_favorite == radio_favourite_before + 1,
               std::to_string(interaction_counts.radio_favorite - radio_favourite_before));
        const int radio_stop_before = interaction_counts.radio_stop;
        tap(128, 252);
        record("radio.stop", "button-center",
               interaction_counts.radio_stop == radio_stop_before + 1 &&
               radio_model.snapshot().state == aura::RadioState::Idle,
               aura::radio_state_name(radio_model.snapshot().state));
        {
            // "Put the radio on" with no station plays the last one again.
            const auto* resume = radio_model.station_to_resume();
            const std::string resume_id = resume ? resume->id : std::string{};
            start_turn("Βάλε ραδιόφωνο");
            pump(2);
            record("radio.resume_spoken", "last-station",
                   !resume_id.empty() && ui.view() == aura::UiView::Radio &&
                       radio_model.snapshot().state == aura::RadioState::Playing &&
                       radio_model.snapshot().station.id == resume_id,
                   aura::radio_state_name(radio_model.snapshot().state));
            stop_radio();
        }

        reset_to(aura::UiView::Radio);
        tap(432, 48);
        const bool radio_search_opened = ui.view() == aura::UiView::RadioSearch;
        type_text("rock");
        const int radio_search_before = interaction_counts.radio_search;
        tap(432, 48);
        record("radio.search", "typed-query-button",
               radio_search_opened && interaction_counts.radio_search == radio_search_before + 1 &&
               !radio_model.snapshot().results.empty(),
               std::to_string(radio_model.snapshot().results.size()));
        {
            // Radio, its search results and the conversation history swipe back
            // like every other list (a swipe over a result never plays it);
            // while typing, the keyboard keeps the swipe.
            const int plays_before_swipe = interaction_counts.radio_play;
            swipe(60, 300, 420, 300);
            const bool results_back = ui.view() == aura::UiView::Radio &&
                                      interaction_counts.radio_play == plays_before_swipe;
            if (radio_model.snapshot().state != aura::RadioState::Idle) stop_radio();
            reset_to(aura::UiView::Radio);
            tap(432, 48);
            swipe(60, 136, 420, 136);
            const bool typing_kept = ui.view() == aura::UiView::RadioSearch;
            reset_to(aura::UiView::Radio);
            swipe(60, 300, 420, 300);
            const bool radio_back = ui.view() == aura::UiView::Home;
            reset_to(aura::UiView::ConversationHistory);
            swipe(60, 300, 420, 300);
            const bool history_back = ui.view() == aura::UiView::Home;
            record("nav.swipe_back_lists", "radio-search-history",
                   results_back && typing_kept && radio_back && history_back,
                   std::to_string(results_back) + std::to_string(typing_kept) + std::to_string(radio_back) +
                       std::to_string(history_back));
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
        type_text("DEMO · Πότισμα");
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
        type_text("DEMO · Γραπτή σημείωση");
        const auto typed_before = companion.notes().snapshot().notes.size();
        tap(432, 48);
        record("notes.typed", "save-header",
               note_form && companion.notes().snapshot().notes.size() == typed_before + 1 &&
               ui.view() == aura::UiView::Notes, view_text());

        reset_to(aura::UiView::Notes);
        tap(128, 420);
        record("notes.voice_without_mic", "disabled-button", ui.view() == aura::UiView::Notes, view_text());
        {
            // A voice note's page has a whole title ("Ηχογράφηση", not the bare
            // adjective "Φωνητική") that fits the header uncut.
            std::uint32_t voice_id = 0;
            companion.notes().add_voice("note-demo.wav", 4000, 0, &voice_id);
            reset_to(aura::UiView::Notes);
            tap(220, 140);
            record("notes.voice_header", "voice-note-title",
                   ui.view() == aura::UiView::NoteDetail &&
                       shows_copy(&aura::l10n::CompanionStrings::voice_note_header),
                   view_text());
            companion.notes().remove(voice_id);
        }

        {
            // Every kept note is reachable from the list, the oldest too.
            std::vector<std::uint32_t> filled;
            std::uint32_t id = 0;
            for (int index = 0; companion.notes().add_text("Σημείωση " + std::to_string(index) + " · DEMO", 0, &id) ==
                                aura::NoteResult::Ok; ++index) {
                filled.push_back(id);
            }
            reset_to(aura::UiView::Notes);
            // One row per note, then the list's end spacer.
            auto* list = find_scrollable(ui.root());
            const std::uint32_t rows = list ? lv_obj_get_child_count(list) - 1 : 0;
            record("notes.all_listed", "one-row-each",
                   companion.notes().snapshot().notes.size() == aura::NotesModel::kMaxNotes &&
                       rows == aura::NotesModel::kMaxNotes,
                   std::to_string(rows));
            for (const auto filled_id : filled) companion.notes().remove(filled_id);
        }

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
        // "In 1 minute", not "in 1 minutes".
        propose("DEMO • Θα σου θυμίσω.\nACTION: {\"type\":\"reminder\",\"text\":\"DEMO\",\"in_minutes\":1}");
        record("assistant.reminder_one_minute", "singular",
               controller.snapshot().state == aura::AssistantState::ActionConfirmation &&
                   shows_copy(&aura::l10n::CompanionStrings::action_in_one_minute),
               aura::state_name(controller.snapshot().state));
        tap(352, 420);
        // An action that is not valid says so: never silence, never the raw line.
        propose("ACTION: {\"type\":\"timer\",\"seconds\":-5}");
        record("assistant.action_refused", "only-invalid-action",
               controller.snapshot().state == aura::AssistantState::Responding && !companion.extras().pending_action &&
                   shows_copy(&aura::l10n::CompanionStrings::action_refused) &&
                   controller.snapshot().answer_text.find("ACTION") == std::string::npos,
               controller.snapshot().answer_text);
        controller.dispatch({aura::EventType::Reset});
        apply();

        // A streamed answer shows as it arrives, and each delta updates the
        // same label instead of rebuilding the screen.
        {
            controller.dispatch({aura::EventType::TurnStarted, "Πες μου κάτι · DEMO"});
            apply();
            ui.show(aura::UiView::Assistant);
            aura::AssistantStreamSnapshot update;
            update.text = "DEMO • Πρώτη πρόταση.";
            companion.deliver_stream_update(update);
            apply();
            pump(2);
            int marker = 0;
            auto* first = find_text(ui.root(), "DEMO • Πρώτη πρόταση.");
            if (first) lv_obj_set_user_data(first, &marker);
            update.text = "DEMO • Πρώτη πρόταση. Δεύτερη.";
            companion.deliver_stream_update(update);
            apply();
            pump(2);
            auto* second = find_text(ui.root(), "DEMO • Πρώτη πρόταση. Δεύτερη.");
            record("assistant.partial_text", "shown-in-place",
                   first && second && lv_obj_get_user_data(second) == &marker,
                   std::to_string(first != nullptr) + std::to_string(second != nullptr));
            controller.dispatch({aura::EventType::Reset});
            apply();
        }

        reset_to(aura::UiView::Launcher);
        tap(96, 188);
        const bool history_opened = ui.view() == aura::UiView::ConversationHistory;
        tap(128, 420);
        const bool history_ask_started = ui.view() == aura::UiView::Assistant;
        tap(352, 420);
        record("conversation.history", "open-ask-cancel",
               history_opened && history_ask_started && ui.view() == aura::UiView::Launcher,
               view_text());

        start_turn("Βάλε ένταση 35");
        pump(4);
        const bool offline_volume_card = controller.snapshot().state == aura::AssistantState::ActionConfirmation &&
            companion.extras().pending_action &&
            companion.extras().pending_action->type == aura::AssistantActionType::Volume;
        tap(128, 420);
        record("offline.volume", "confirm-button",
               offline_volume_card && std::fabs(audio.diagnostics().volume - 0.35F) < 0.01F,
               aura::state_name(controller.snapshot().state));
        // "Louder" without a number is one step up from the volume heard now.
        start_turn("Δυνάμωσε την ένταση");
        pump(4);
        const bool step_card = controller.snapshot().state == aura::AssistantState::ActionConfirmation &&
            companion.extras().pending_action && companion.extras().pending_action->relative &&
            shows_text("+10%");
        tap(128, 420);
        record("offline.volume_step", "louder-confirm",
               step_card && std::fabs(audio.diagnostics().volume - 0.45F) < 0.01F,
               std::to_string(audio.diagnostics().volume));

        companion.organizer().clear();
        start_turn("Αγόρασε ψωμί");
        pump(4);
        const bool offline_shopping_card = controller.snapshot().state == aura::AssistantState::ActionConfirmation &&
            companion.extras().pending_action &&
            companion.extras().pending_action->type == aura::AssistantActionType::Shopping;
        tap(128, 420);
        record("offline.shopping", "confirm-button",
               offline_shopping_card && companion.organizer().snapshot().shopping.size() == 1,
               std::to_string(companion.organizer().snapshot().shopping.size()));

        controller.dispatch({aura::EventType::Reset});
        start_turn("Τι ώρα είναι;");
        pump(4);
        record("offline.time", "local-reply",
               controller.snapshot().state == aura::AssistantState::Responding &&
                   !controller.snapshot().answer_text.empty() && !companion.extras().pending_action,
               controller.snapshot().answer_text);
        controller.dispatch({aura::EventType::Reset});
        apply();

        // A local calendar answer joins the conversation like any other answer
        // (and is spoken on a voice turn through the same companion path).
        start_turn("Ποιος γιορτάζει σήμερα;");
        pump(4);
        {
            const auto& history = companion.extras().conversation_history;
            record("local.calendar_history", "local-reply",
                   controller.snapshot().state == aura::AssistantState::Responding && !history.empty() &&
                       history.back().question == "Ποιος γιορτάζει σήμερα;" &&
                       history.back().answer == controller.snapshot().answer_text,
                   std::to_string(history.size()));
        }
        controller.dispatch({aura::EventType::Reset});
        apply();
        start_turn("Πότε γιορτάζει ο Γιώργος;");
        pump(4);
        {
            // A time never wraps at its colon ("19:" then "30"): ':' is not a line break.
            const lv_font_t* font = lv_font_get_default();
            lv_point_t one_line{};
            lv_text_get_size(&one_line, "aaaaaaaa 19:", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_point_t wrapped{};
            lv_text_get_size(&wrapped, "aaaaaaaa 19:30", font, 0, 0, one_line.x + 1, LV_TEXT_FLAG_NONE);
            record("text.time_wrap", "no-break-at-colon", wrapped.x < one_line.x, std::to_string(wrapped.x));
        }
        record("local.nameday_of", "local-reply",
               controller.snapshot().state == aura::AssistantState::Responding &&
                   controller.snapshot().answer_text.find("23/4") != std::string::npos,
               controller.snapshot().answer_text);
        controller.dispatch({aura::EventType::Reset});
        apply();

        // A proposed message opens the Aoraton confirmation; the provider path
        // then applies the reply, which must not cover that screen.
        {
            const std::string room = aoraton.snapshot().rooms.empty() ? std::string{} :
                aoraton.snapshot().rooms.front().display_name;
            controller.dispatch({aura::EventType::TurnStarted, "Στείλε μήνυμα · DEMO"});
            companion.deliver_reply("DEMO • Θα σταλεί.\nACTION: {\"type\":\"message\",\"room\":\"" + room +
                                    "\",\"text\":\"DEMO\"}");
            apply();
            pump(4);
            const bool confirm_shown = ui.view() == aura::UiView::AoratonConfirm &&
                                       aoraton.snapshot().awaiting_confirmation;
            aoraton.cancel_send();
            record("assistant.message_action", "provider-reply", confirm_shown, view_text());
            controller.dispatch({aura::EventType::Reset});
            apply();
        }

        companion.memory().record("Τι ώρα είναι;", "DEMO • 18:24.", companion.now_ms());
        controller.dispatch({aura::EventType::TurnStarted, "Και αύριο; · DEMO"});
        companion.deliver_reply("DEMO • Συνεχίζω.");
        apply();
        ui.show(aura::UiView::Assistant);
        pump(4);
        const auto ask_before_new = interaction_counts.ask;
        tap(352, 420);
        const bool new_topic_history_open = ui.view() == aura::UiView::ConversationHistory;
        tap(352, 420);
        record("assistant.new_conversation", "secondary-button",
               new_topic_history_open && companion.memory().size() == 0 &&
                   interaction_counts.ask == ask_before_new + 1,
               aura::state_name(controller.snapshot().state));
        controller.dispatch({aura::EventType::Reset});
        apply();

        // Keep the calendar-dependent row order deterministic. 26 October has
        // a nameday row while 25 October does not, which used to move the alarm
        // below this physical pointer target depending on the day of the run.
        device_status.set_clock(18, 24, aura::StatusOrigin::Simulated);
        device_status.set_date(2026, 10, 25, 0, aura::StatusOrigin::Simulated);
        reset_to(aura::UiView::Briefing);
        // The holiday is informative; the first actionable row is the alarm
        // created earlier through the real alarm form.
        lv_obj_update_layout(ui.root());
        auto* today_action = find_sized_clickable(ui.root(), 432, 88);
        lv_area_t today_action_bounds{};
        if (today_action) {
            lv_obj_scroll_to_view_recursive(today_action, LV_ANIM_OFF);
            pump(3);
            lv_obj_get_coords(today_action, &today_action_bounds);
        }
        tap(today_action ? (today_action_bounds.x1 + today_action_bounds.x2) / 2 : 220,
            today_action ? (today_action_bounds.y1 + today_action_bounds.y2) / 2 : 276);
        record("today.row", "first-actionable-row",
               today_action && ui.view() == aura::UiView::Clock, view_text());

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
        scroll_view_to(776);
        tap(220, settings_row_y(9, 776));
        record("settings.display", "row", ui.view() == aura::UiView::DisplaySettings, view_text());
        reset_to(aura::UiView::Settings);
        scroll_view_to(776);
        tap(220, settings_row_y(10, 776));
        record("settings.phone", "row", ui.view() == aura::UiView::PhoneLink, view_text());
        reset_to(aura::UiView::Settings);
        scroll_view_to(776);
        tap(220, settings_row_y(11, 776));
        record("settings.wake_word", "info-row", ui.view() == aura::UiView::Settings, view_text());

        {
            // Settings > Appearance: four looks, chosen on the device
            // (docs/superpowers/specs/2026-10-29-aura-appearances-design.md).
            const auto& el = aura::l10n::companion_el_gr();
            const auto& en = aura::l10n::companion_en_us();
            auto names_whole = [&](const aura::l10n::CompanionStrings& strings) {
                return find_text(ui.root(), strings.appearance_halo) != nullptr &&
                       find_text(ui.root(), strings.appearance_marble) != nullptr &&
                       find_text(ui.root(), strings.appearance_aurora) != nullptr &&
                       find_text(ui.root(), strings.appearance_graphite) != nullptr;
            };
            reset_to(aura::UiView::Settings);
            const bool row_named = find_text(ui.root(), el.appearance_title) != nullptr &&
                                   find_text(ui.root(), el.appearance_halo) != nullptr;
            tap(220, settings_row_y(2, 0));
            record("appearance.settings_row", "row",
                   row_named && ui.view() == aura::UiView::AppearanceSettings, view_text());

            const bool greek_cards = names_whole(el);
            ui.set_language(aura::InterfaceLanguage::English);
            const bool english_cards = names_whole(en);
            ui.set_language(aura::InterfaceLanguage::Greek);
            record("appearance.cards", "four-names", greek_cards && english_cards,
                   std::to_string(greek_cards) + std::to_string(english_cards));

            tap(351, 186);  // the Marble card, top right
            pump(3);
            // The selected card's mark leaves its name whole («Μάρμαρο» in its serif).
            const bool picked_names = names_whole(el);
            record("appearance.pick", "marble-card",
                   ui.appearance() == aura::Appearance::Marble &&
                   ui.view() == aura::UiView::AppearanceSettings &&
                   companion.display().snapshot().settings.appearance == aura::Appearance::Marble &&
                   aura::ui::tokens::text == 0xF2EDE4 && picked_names,
                   aura::appearance_id(ui.appearance()) + std::string("/names=") + std::to_string(picked_names));

            // Dimming to the always-on clock and waking keep the look and the view.
            auto display_snapshot = companion.display().snapshot();
            display_snapshot.mode = aura::DisplayMode::AlwaysOn;
            display_snapshot.sequence += 1000;
            ui.apply_display(display_snapshot);
            const bool dark_kept = ui.appearance() == aura::Appearance::Marble &&
                                   ui.view() == aura::UiView::AlwaysOn;
            display_snapshot.mode = aura::DisplayMode::Active;
            display_snapshot.sequence += 1;
            ui.apply_display(display_snapshot);
            pump(2);
            record("appearance.aod_roundtrip", "always-on",
                   dark_kept && ui.appearance() == aura::Appearance::Marble &&
                   ui.view() == aura::UiView::AppearanceSettings,
                   view_text());

            // A change of look while listening keeps the state and the moving presence.
            reset_to(aura::UiView::Home);
            controller.dispatch({aura::EventType::Reset});
            controller.dispatch({aura::EventType::WakeDetected});
            controller.dispatch({aura::EventType::BeginRecording});
            apply();
            pump(2);
            const bool listening = controller.snapshot().state == aura::AssistantState::Listening;
            ui.set_appearance(aura::Appearance::Graphite);
            const auto frames_before = ui.mercury_frame_count();
            for (int step = 0; step < 10; ++step) {
                ui.tick(66);
                pump(1);
            }
            record("appearance.switch_while_listening", "graphite",
                   listening && ui.view() == aura::UiView::Assistant &&
                   controller.snapshot().state == aura::AssistantState::Listening &&
                   ui.mercury_frame_count() > frames_before,
                   std::to_string(listening) + "/" + std::to_string(static_cast<int>(ui.view())) + "/" +
                       aura::state_name(controller.snapshot().state) + "/" +
                       std::to_string(ui.mercury_frame_count() - frames_before));
            // The same into Μάρμαρο: its pearl takes over and keeps moving.
            ui.set_appearance(aura::Appearance::Marble);
            const auto pearl_frames_before = ui.mercury_frame_count();
            for (int step = 0; step < 10; ++step) {
                ui.tick(66);
                pump(1);
            }
            record("appearance.switch_while_listening", "marble",
                   ui.view() == aura::UiView::Assistant &&
                   controller.snapshot().state == aura::AssistantState::Listening &&
                   ui.mercury_frame_count() > pearl_frames_before,
                   aura::state_name(controller.snapshot().state) + std::string("/") +
                       std::to_string(ui.mercury_frame_count() - pearl_frames_before));
            controller.dispatch({aura::EventType::Reset});
            apply();
            pump(2);

            // Each look's component kit on a real screen: a Settings row, its icon
            // well and the header's Back button.
            {
                bool kit_ok = true;
                std::string kit_detail;
                for (const auto look : {aura::Appearance::Marble, aura::Appearance::Aurora,
                                        aura::Appearance::Graphite}) {
                    ui.set_appearance(look);
                    reset_to(aura::UiView::Settings);
                    const auto& metrics = aura::ui::metrics_for(look);
                    auto* label = find_text(ui.root(), aura::l10n::el_gr().wifi_title);
                    auto* row = label ? lv_obj_get_parent(label) : nullptr;
                    auto* well = row ? lv_obj_get_child(row, 0) : nullptr;
                    lv_point_t back_point{48, 48};
                    auto* back = lv_indev_search_obj(ui.root(), &back_point);
                    bool well_ok = false;
                    bool back_ok = false;
                    if (well && back) {
                        const auto well_radius = lv_obj_get_style_radius(well, LV_PART_MAIN);
                        if (metrics.wells == aura::ui::WellStyle::HairlineCircle) {
                            well_ok = lv_obj_get_style_bg_opa(well, LV_PART_MAIN) == LV_OPA_TRANSP &&
                                      lv_obj_get_style_border_width(well, LV_PART_MAIN) >= 1;
                            back_ok = lv_obj_get_style_bg_opa(back, LV_PART_MAIN) == LV_OPA_TRANSP &&
                                      lv_obj_get_style_border_width(back, LV_PART_MAIN) >= 1;
                        } else {
                            well_ok = well_radius < 30 && lv_obj_get_style_bg_opa(well, LV_PART_MAIN) > 0;
                            back_ok = metrics.rows == aura::ui::RowStyle::Glass
                                          ? lv_obj_get_style_bg_grad_dir(back, LV_PART_MAIN) == LV_GRAD_DIR_VER
                                          : lv_obj_get_style_radius(back, LV_PART_MAIN) == 20;
                        }
                    }
                    kit_ok = kit_ok && well_ok && back_ok;
                    kit_detail += std::string(aura::appearance_id(look)) + ":" + std::to_string(well_ok) +
                                  std::to_string(back_ok) + " ";
                }
                ui.set_appearance(aura::Appearance::Halo);
                record("appearance.kit", "settings-row-well-back", kit_ok, kit_detail);
            }

            // Signature screens keep every cut label inside its box and every
            // control at least 64 px (labels made tappable keep their own margin).
            std::function<bool(lv_obj_t*)> labels_inside = [&](lv_obj_t* object) {
                bool inside = true;
                const auto count = lv_obj_get_child_count(object);
                for (std::uint32_t index = 0; index < count; ++index) {
                    auto* child = lv_obj_get_child(object, static_cast<std::int32_t>(index));
                    if (lv_obj_is_hidden(child)) continue;
                    if (lv_obj_check_type(child, &lv_label_class) &&
                        lv_label_get_long_mode(child) == LV_LABEL_LONG_DOT) {
                        lv_area_t own{};
                        lv_area_t parent{};
                        lv_obj_get_coords(child, &own);
                        lv_obj_get_coords(object, &parent);
                        inside = inside && own.x1 >= parent.x1 - 1 && own.x2 <= parent.x2 + 1 && own.x1 >= 0 &&
                                 own.x2 <= 480;
                    }
                    inside = labels_inside(child) && inside;
                }
                return inside;
            };
            std::function<int(lv_obj_t*)> smallest_target = [&](lv_obj_t* object) {
                int smallest = 9999;
                const auto count = lv_obj_get_child_count(object);
                for (std::uint32_t index = 0; index < count; ++index) {
                    auto* child = lv_obj_get_child(object, static_cast<std::int32_t>(index));
                    if (lv_obj_is_hidden(child)) continue;
                    if (lv_obj_is_clickable(child) && !lv_obj_check_type(child, &lv_label_class)) {
                        smallest = std::min(smallest, std::min(static_cast<int>(lv_obj_get_width(child)),
                                                               static_cast<int>(lv_obj_get_height(child))));
                    }
                    smallest = std::min(smallest, smallest_target(child));
                }
                return smallest;
            };
            std::function<lv_obj_t*(lv_obj_t*, aura::UiView)> find_target = [&](lv_obj_t* object, aura::UiView view) {
                const auto count = lv_obj_get_child_count(object);
                for (std::uint32_t index = 0; index < count; ++index) {
                    auto* child = lv_obj_get_child(object, static_cast<std::int32_t>(index));
                    if (lv_obj_is_clickable(child) &&
                        lv_obj_get_user_data(child) == reinterpret_cast<void*>(static_cast<std::intptr_t>(view))) {
                        return child;
                    }
                    if (auto* found = find_target(child, view)) return found;
                }
                return static_cast<lv_obj_t*>(nullptr);
            };
            std::function<bool(lv_obj_t*, const lv_font_t*)> uses_font = [&](lv_obj_t* object, const lv_font_t* font) {
                const auto count = lv_obj_get_child_count(object);
                for (std::uint32_t index = 0; index < count; ++index) {
                    auto* child = lv_obj_get_child(object, static_cast<std::int32_t>(index));
                    if (lv_obj_check_type(child, &lv_label_class) &&
                        lv_obj_get_style_text_font(child, LV_PART_MAIN) == font) {
                        return true;
                    }
                    if (uses_font(child, font)) return true;
                }
                return false;
            };
            // The view's own content (the status bar is shared chrome, not a layout).
            auto view_content = [&]() {
                const auto count = lv_obj_get_child_count(ui.root());
                for (std::uint32_t index = 0; index < count; ++index) {
                    auto* child = lv_obj_get_child(ui.root(), static_cast<std::int32_t>(index));
                    if (!lv_obj_is_hidden(child) && lv_obj_get_height(child) == 480 && lv_obj_get_child_count(child) > 0) {
                        return child;
                    }
                }
                return ui.root();
            };
            auto tap_center = [&](lv_obj_t* object) {
                if (!object) return;
                lv_area_t area{};
                lv_obj_get_coords(object, &area);
                tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
            };

            {
                // Μάρμαρο: Home. Serif numerals, the next alarm beside the weather,
                // the presence (tap = talk) and Apps, in both languages.
                ui.set_appearance(aura::Appearance::Marble);
                const auto& marble_fonts = aura::ui::fonts_for(aura::Appearance::Marble);
                std::uint32_t alarm_id = 0;
                companion.clock().add_alarm(7, 30, true, "", &alarm_id);
                reset_to(aura::UiView::Home);
                pump(3);
                const bool serif_clock = uses_font(ui.root(), marble_fonts.numerals_large);
                const bool alarm_shown = find_text(ui.root(), "07:30") != nullptr;
                const bool greek_hint = find_text(ui.root(), aura::l10n::el_gr().home_hint) != nullptr;
                ui.set_language(aura::InterfaceLanguage::English);
                pump(2);
                const bool english_hint = find_text(ui.root(), aura::l10n::en_us().home_hint) != nullptr;
                ui.set_language(aura::InterfaceLanguage::Greek);
                pump(2);
                const int ask_before = interaction_counts.ask;
                tap(240, aura::ui::metrics_for(aura::Appearance::Marble).hero_center_y);
                const bool talks = interaction_counts.ask == ask_before + 1;
                controller.dispatch({aura::EventType::Reset});
                apply();
                reset_to(aura::UiView::Home);
                tap_center(find_target(ui.root(), aura::UiView::Launcher));
                const bool apps = ui.view() == aura::UiView::Launcher;
                record("marble.home", "layout",
                       serif_clock && alarm_shown && greek_hint && english_hint && talks && apps,
                       std::to_string(serif_clock) + std::to_string(alarm_shown) + std::to_string(greek_hint) +
                           std::to_string(english_hint) + std::to_string(talks) + std::to_string(apps));

                // A long timer label stays inside its box; every control stays 64 px.
                std::uint32_t timer_id = 0;
                companion.clock().start_timer(600, "Ζυμαρικά για τέσσερα άτομα με σάλτσα ντομάτας και βασιλικό",
                                              &timer_id);
                reset_to(aura::UiView::Home);
                pump(3);
                const bool inside = labels_inside(ui.root());
                const int smallest = smallest_target(view_content());
                record("marble.home_fit", "long-timer-label", inside && smallest >= 64,
                       std::to_string(inside) + "/" + std::to_string(smallest));
                companion.clock().cancel_timer(timer_id);
                companion.clock().remove_alarm(alarm_id);
                pump(2);

                const auto font_of = [](lv_obj_t* object) {
                    return object ? lv_obj_get_style_text_font(object, LV_PART_MAIN) : nullptr;
                };
                // Wholly on the first screen, below the header.
                const auto on_screen = [](lv_obj_t* object) {
                    if (!object) return false;
                    lv_area_t area{};
                    lv_obj_get_coords(object, &area);
                    return area.y1 >= 96 && area.y2 <= 480;
                };
                std::function<lv_obj_t*(lv_obj_t*, const lv_obj_class_t*)> find_type =
                    [&](lv_obj_t* object, const lv_obj_class_t* type) -> lv_obj_t* {
                    const auto count = lv_obj_get_child_count(object);
                    for (std::uint32_t index = 0; index < count; ++index) {
                        auto* child = lv_obj_get_child(object, static_cast<std::int32_t>(index));
                        if (lv_obj_check_type(child, type)) return child;
                        if (auto* found = find_type(child, type)) return found;
                    }
                    return nullptr;
                };
                const auto fits = [&] { return labels_inside(view_content()) && smallest_target(view_content()) >= 64; };
                const aura::InterfaceLanguage languages[] = {aura::InterfaceLanguage::Greek,
                                                             aura::InterfaceLanguage::English};

                // Answer: a small pearl replaces the title, the question is a serif
                // quote in the reader's language, and today's actions stay.
                bool answer_ok = true;
                std::string answer_detail;
                for (const auto language : languages) {
                    ui.set_language(language);
                    const bool english = language == aura::InterfaceLanguage::English;
                    const auto& strings = english ? aura::l10n::en_us() : aura::l10n::el_gr();
                    const auto& extra = english ? aura::l10n::companion_en_us() : aura::l10n::companion_el_gr();
                    controller.dispatch({aura::EventType::Reset});
                    controller.dispatch({aura::EventType::TurnStarted, strings.capture_short_prompt});
                    controller.dispatch({aura::EventType::FinalResult, strings.capture_short_answer});
                    apply();
                    pump(4);
                    char quoted[256];
                    std::snprintf(quoted, sizeof(quoted), extra.answer_quote_format, strings.capture_short_prompt);
                    const bool serif = font_of(find_text(view_content(), quoted)) == marble_fonts.title;
                    const bool no_title = find_text(view_content(), strings.answer_title) == nullptr;
                    const bool ask = find_text(view_content(), strings.ask_again) != nullptr;
                    const bool fit = fits();
                    answer_ok = answer_ok && serif && no_title && ask && fit;
                    answer_detail += std::to_string(serif) + std::to_string(no_title) + std::to_string(ask) +
                                     std::to_string(fit) + " ";
                }
                controller.dispatch({aura::EventType::Reset});
                apply();
                record("marble.answer", "quote-and-actions", answer_ok, answer_detail);

                // Apps: three rows of hairline circles fit the screen, and unread
                // notifications show a gold dot instead of a count.
                const int notifications_before = controller.snapshot().notification_count;
                controller.dispatch({aura::EventType::NotificationReceived});
                apply();
                bool launcher_ok = true;
                std::string launcher_detail;
                for (const auto language : languages) {
                    ui.set_language(language);
                    const auto& visual = language == aura::InterfaceLanguage::English ? aura::l10n::visual_en_us() :
                                                                                        aura::l10n::visual_el_gr();
                    reset_to(aura::UiView::Launcher);
                    pump(2);
                    auto* notifications = find_target(view_content(), aura::UiView::Notifications);
                    auto* clock_cell = find_target(view_content(), aura::UiView::Clock);
                    const bool third_row = on_screen(notifications) && find_text(notifications, visual.notifications);
                    auto* well = clock_cell ? lv_obj_get_child(clock_cell, 0) : nullptr;
                    const bool hairline = well && lv_obj_get_style_border_width(well, LV_PART_MAIN) >= 1 &&
                                          lv_obj_get_style_bg_opa(well, LV_PART_MAIN) == LV_OPA_TRANSP;
                    bool dot = false;
                    for (std::uint32_t index = 0; notifications && index < lv_obj_get_child_count(notifications);
                         ++index) {
                        auto* child = lv_obj_get_child(notifications, static_cast<std::int32_t>(index));
                        dot = dot || (lv_obj_get_width(child) == 14 && lv_obj_get_height(child) == 14);
                    }
                    const bool fit = fits();
                    tap_center(clock_cell);
                    const bool opens = ui.view() == aura::UiView::Clock;
                    launcher_ok = launcher_ok && third_row && hairline && dot && fit && opens;
                    launcher_detail += std::to_string(third_row) + std::to_string(hairline) + std::to_string(dot) +
                                       std::to_string(fit) + std::to_string(opens) + " ";
                }
                if (notifications_before == 0) controller.dispatch({aura::EventType::NotificationsCleared});
                apply();
                record("marble.launcher", "three-rows-hairline-dot", launcher_ok, launcher_detail);

                // Weather: the city is the title (tap = choose a city); the serif
                // temperature, the hourly row and the metric list fill the first
                // screen; DEMO stays visible.
                bool weather_ok = true;
                std::string weather_detail;
                for (const auto language : languages) {
                    ui.set_language(language);
                    const bool english = language == aura::InterfaceLanguage::English;
                    const auto& visual = english ? aura::l10n::visual_en_us() : aura::l10n::visual_el_gr();
                    weather_model.restore_location(demo_weather_location({}, language));
                    weather_model.fetch_succeeded(demo_weather_data(aura::WeatherCondition::PartlyCloudy, {}, language));
                    apply_weather();
                    reset_to(aura::UiView::Weather);
                    pump(3);
                    auto* city = find_text(view_content(), english ? "Athens" : "Αθήνα");
                    const bool serif_city = font_of(city) == marble_fonts.title;
                    const bool serif_degrees = font_of(find_text(view_content(), "22°")) == marble_fonts.numerals_large;
                    const bool first_screen = on_screen(find_text(view_content(), "21:00")) &&
                                              on_screen(find_text(view_content(), visual.humidity)) &&
                                              on_screen(find_text(view_content(), visual.wind));
                    const bool demo = find_text(view_content(), visual.demo_badge) != nullptr;
                    const bool fit = fits();
                    tap_center(city);
                    const bool picker = ui.view() == aura::UiView::WeatherLocation;
                    weather_ok = weather_ok && serif_city && serif_degrees && first_screen && demo && fit && picker;
                    weather_detail += std::to_string(serif_city) + std::to_string(serif_degrees) +
                                      std::to_string(first_screen) + std::to_string(demo) + std::to_string(fit) +
                                      std::to_string(picker) + " ";
                }
                // A long city name ends in an ellipsis inside its box.
                ui.set_language(aura::InterfaceLanguage::Greek);
                auto long_city = demo_weather_data();
                long_city.location.name = "Αγιος Νικόλαος Λασιθίου";
                weather_model.restore_location(long_city.location);
                weather_model.fetch_succeeded(long_city);
                apply_weather();
                reset_to(aura::UiView::Weather);
                pump(3);
                const bool long_city_cut = find_text(view_content(), long_city.location.name.c_str()) == nullptr &&
                                           fits();
                weather_ok = weather_ok && long_city_cut;
                weather_detail += "long=" + std::to_string(long_city_cut);
                weather_model.restore_location(demo_weather_location());
                weather_model.fetch_succeeded(demo_weather_data());
                apply_weather();
                record("marble.weather", "hero-hourly-metrics", weather_ok, weather_detail);

                // Timer: the watch dial (60 ticks and an arc that shrinks), serif
                // digits, the label and the end time; pause and cancel still work.
                bool timer_ok = true;
                std::string timer_detail;
                for (const auto language : languages) {
                    ui.set_language(language);
                    const auto& extra = language == aura::InterfaceLanguage::English ?
                        aura::l10n::companion_en_us() : aura::l10n::companion_el_gr();
                    clear_timers();
                    clock_model.start_timer(600, "Ζυμαρικά");
                    reset_to(aura::UiView::Clock);
                    tap(220, 140);
                    pump(2);
                    const bool item = ui.view() == aura::UiView::ClockItem;
                    auto* scale = find_type(view_content(), &lv_scale_class);
                    const bool ticks = scale && lv_scale_get_total_tick_count(scale) == 60;
                    // The end time is set when the screen opens (ten minutes from now);
                    // tick() below runs the timer without moving the wall clock.
                    const auto& clock_now = clock_model.snapshot();
                    const auto end = aura::from_minute_index(clock_now.now_minute + (600 + 30) / 60);
                    char ends[48];
                    std::snprintf(ends, sizeof(ends), extra.timer_ends_format, end.hour, end.minute);
                    const bool end_shown = !clock_now.clock_valid || find_text(view_content(), ends) != nullptr;
                    const bool label_shown = find_text(view_content(), "Ζυμαρικά") != nullptr;
                    auto* arc = find_type(view_content(), &lv_arc_class);
                    const int full = arc ? lv_arc_get_value(arc) : -1;
                    clock_model.tick(150000);
                    pump(3);
                    auto* arc_later = find_type(view_content(), &lv_arc_class);
                    const int later = arc_later ? lv_arc_get_value(arc_later) : -1;
                    // The timer also runs in real time between pumps (a few ms).
                    const bool shrinks = full >= 990 && later <= full - 250 && later >= full - 260;
                    const bool serif_digits = font_of(find_text(view_content(), "07:30")) == marble_fonts.numerals_small;
                    const bool fit = fits();
                    timer_ok = timer_ok && item && ticks && shrinks && serif_digits && end_shown && label_shown && fit;
                    timer_detail += std::to_string(item) + std::to_string(ticks) + std::to_string(full) + "/" +
                                    std::to_string(later) + std::to_string(serif_digits) + std::to_string(end_shown) +
                                    std::to_string(label_shown) + std::to_string(fit) + " ";
                }
                ui.set_language(aura::InterfaceLanguage::Greek);
                clear_timers();
                clock_model.start_timer(600, "Ζυμαρικά για τέσσερα άτομα με σάλτσα ντομάτας και βασιλικό");
                reset_to(aura::UiView::Clock);
                tap(220, 140);
                pump(2);
                const bool long_label_inside = fits();
                tap_center(find_sized_clickable(view_content(), 80, 80));
                const bool timer_paused = !clock_model.snapshot().timers.empty() &&
                                          clock_model.snapshot().timers.front().paused;
                tap_center(find_sized_clickable(view_content(), 72, 72));
                const bool timer_cancelled = clock_model.snapshot().timers.empty() &&
                                             ui.view() == aura::UiView::Clock;
                timer_ok = timer_ok && long_label_inside && timer_paused && timer_cancelled;
                timer_detail += "long=" + std::to_string(long_label_inside) + std::to_string(timer_paused) +
                                std::to_string(timer_cancelled);
                clear_timers();
                record("marble.timer", "dial-digits-controls", timer_ok, timer_detail);

                // Today: the serif greeting and the date over hairline rows.
                bool briefing_ok = true;
                std::string briefing_detail;
                for (const auto language : languages) {
                    ui.set_language(language);
                    reset_to(aura::UiView::Briefing);
                    pump(2);
                    const bool serif = uses_font(view_content(), marble_fonts.title);
                    const bool fit = fits();
                    briefing_ok = briefing_ok && serif && fit;
                    briefing_detail += std::to_string(serif) + std::to_string(fit) + " ";
                }
                ui.set_language(aura::InterfaceLanguage::Greek);
                record("marble.briefing", "greeting-rows", briefing_ok, briefing_detail);
                ui.set_appearance(aura::Appearance::Halo);
            }

            // Every view in every look, ten times over: no crash and no LVGL growth.
            const aura::UiView sweep_views[] = {
                aura::UiView::Home, aura::UiView::Launcher, aura::UiView::Notifications,
                aura::UiView::Settings, aura::UiView::AppearanceSettings, aura::UiView::Audio,
                aura::UiView::AoratonRooms, aura::UiView::AoratonConversation, aura::UiView::AoratonCompose,
                aura::UiView::Weather, aura::UiView::WeatherSettings, aura::UiView::Calendar,
                aura::UiView::Battery, aura::UiView::VoiceSettings, aura::UiView::Diagnostics,
                aura::UiView::Wifi, aura::UiView::Clock, aura::UiView::ClockNew, aura::UiView::TimerNew,
                aura::UiView::AlarmNew, aura::UiView::ReminderNew, aura::UiView::Notes, aura::UiView::NoteNew,
                aura::UiView::Briefing, aura::UiView::Translate, aura::UiView::PhoneLink,
                aura::UiView::DisplaySettings, aura::UiView::Update, aura::UiView::Radio,
                aura::UiView::RadioSearch, aura::UiView::System,
            };
            bool sweep_ok = true;
            std::string sweep_detail;
            for (const auto appearance : aura::kAppearances) {
                ui.set_appearance(appearance);
                ui.show(aura::UiView::Home);
                pump(2);
                lv_mem_monitor_t before{};
                lv_mem_monitor(&before);
                for (int pass = 0; pass < 10; ++pass) {
                    for (const auto view : sweep_views) ui.show(view);
                }
                ui.show(aura::UiView::Home);
                pump(2);
                lv_mem_monitor_t after{};
                lv_mem_monitor(&after);
                const long grew = static_cast<long>(after.total_size - after.free_size) -
                                  static_cast<long>(before.total_size - before.free_size);
                if (grew > 8192) sweep_ok = false;
                sweep_detail += std::string(aura::appearance_id(appearance)) + ":" + std::to_string(grew) + " ";
            }
            record("appearance.sweep", "all-views", sweep_ok, sweep_detail);

            // Back to Halo through the card, so the stored setting follows.
            reset_to(aura::UiView::AppearanceSettings);
            tap(129, 186);  // the Halo card, top left
            pump(3);
            record("appearance.restore_halo", "halo-card",
                   ui.appearance() == aura::Appearance::Halo &&
                   companion.display().snapshot().settings.appearance == aura::Appearance::Halo &&
                   aura::ui::tokens::text == 0xF5F8FC,
                   aura::appearance_id(ui.appearance()));
        }

        {
            // Weather copy: a singular count, no "-0°", metric titles that fit in
            // both languages and a toast in the interface language.
            auto weather_check = weather_model.snapshot();
            weather_check.state = aura::WeatherState::Ready;
            weather_check.has_data = true;
            weather_check.data = demo_tlesdetector_weather_data();
            weather_check.data.temperature_c = -0.4F;
            weather_check.sequence += 1000;
            reset_to(aura::UiView::Weather);
            ui.apply_weather(weather_check);
            const auto& visual_el = aura::l10n::visual_el_gr();
            const auto& visual_en = aura::l10n::visual_en_us();
            const bool singular = shows_text(visual_el.weather_nearby_alert_one);
            const bool no_negative_zero = shows_text("0°C") && !shows_text("-0°C");
            // Every metric title cut short (with its full width), or empty.
            auto title_cut = [&](const aura::l10n::VisualStrings& strings) -> std::string {
                std::string cut;
                for (const char* title : {strings.humidity, strings.wind, strings.feels_like, strings.weather_rain,
                                          strings.weather_pressure, strings.weather_gusts, strings.weather_visibility,
                                          strings.weather_uv, strings.weather_air_quality}) {
                    auto* label = find_text(ui.root(), title);
                    if (!label) {
                        // A dotted label no longer holds its full text.
                        lv_point_t full{};
                        lv_text_get_size(&full, title, aura::ui::font_caption(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                        cut += std::to_string(full.x) + ";";
                        continue;
                    }
                    lv_obj_update_layout(label);
                    lv_point_t size{};
                    lv_text_get_size(&size, title, lv_obj_get_style_text_font(label, LV_PART_MAIN),
                                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX,
                                     LV_TEXT_FLAG_NONE);
                    if (size.x > lv_obj_get_content_width(label)) {
                        cut += std::to_string(size.x) + ">" + std::to_string(lv_obj_get_content_width(label)) + ";";
                    }
                }
                return cut;
            };
            // The widest demo values stay whole beside their icon.
            std::string values_cut;
            for (const char* value : {"1007 hPa", "46 km/h", "24 km/h", "41 AQI", "8.4 km"}) {
                if (!find_text(ui.root(), value)) values_cut += std::string(value) + ";";
            }
            const auto greek_cut = title_cut(visual_el);
            ui.set_language(aura::InterfaceLanguage::English);
            const auto english_cut = title_cut(visual_en);
            const bool greek_fit = greek_cut.empty();
            const bool english_fit = english_cut.empty();
            reset_to(aura::UiView::WeatherLocation);
            auto offline = weather_check;
            offline.state = aura::WeatherState::Offline;
            offline.sequence += 1;
            ui.apply_weather(offline);
            const bool english_toast = find_text(lv_screen_active(), visual_en.weather_offline_title) != nullptr;
            ui.set_language(aura::InterfaceLanguage::Greek);
            auto restored = weather_model.snapshot();
            restored.sequence = offline.sequence + 1;
            ui.apply_weather(restored);
            record("weather.copy", "singular-zero-titles-toast",
                   singular && no_negative_zero && greek_fit && english_fit && english_toast && values_cut.empty(),
                   std::to_string(singular) + std::to_string(no_negative_zero) + std::to_string(english_toast) +
                       " " + greek_cut + " " + english_cut + " " + values_cut);
        }

        {
            // The compose counter (characters left) reads whole beside the DEMO
            // tag in both languages.
            aoraton.select_room("demo-family");
            ui.apply_aoraton(aoraton.snapshot());
            reset_to(aura::UiView::AoratonCompose);
            auto counter_whole = [&](const aura::l10n::CompanionStrings& strings) {
                auto* area = lv_group_get_focused(lv_group_get_default());
                const char* typed = area && lv_obj_check_type(area, &lv_textarea_class) ? lv_textarea_get_text(area) : "";
                char expected[64];
                std::snprintf(expected, sizeof(expected), strings.compose_left_format,
                              static_cast<unsigned>(aura::aoraton_characters_left(typed ? typed : "",
                                                                                  aoraton.snapshot().max_message_bytes)));
                return find_text(ui.root(), expected) != nullptr;
            };
            const bool greek_counter = counter_whole(aura::l10n::companion_el_gr());
            ui.set_language(aura::InterfaceLanguage::English);
            const bool english_counter = counter_whole(aura::l10n::companion_en_us());
            ui.set_language(aura::InterfaceLanguage::Greek);
            record("aoraton.compose_counter", "whole-both-languages", greek_counter && english_counter,
                   std::to_string(greek_counter) + std::to_string(english_counter));
        }
        {
            // The note recorder says how long it listens and when it stops, uncut
            // in both languages (a dictated note runs to 30 s, past short pauses).
            reset_to(aura::UiView::NoteRecord);
            const bool greek_whole = find_text(ui.root(), aura::l10n::companion_el_gr().recording_detail) != nullptr;
            ui.set_language(aura::InterfaceLanguage::English);
            const bool english_whole = find_text(ui.root(), aura::l10n::companion_en_us().recording_detail) != nullptr;
            ui.set_language(aura::InterfaceLanguage::Greek);
            const std::string greek_copy = aura::l10n::companion_el_gr().recording_detail;
            record("notes.record_copy", "limits-uncut",
                   greek_whole && english_whole && greek_copy.find("30") != std::string::npos,
                   std::to_string(greek_whole) + std::to_string(english_whole));
        }
        {
            // A sign-in that timed out tells the owner what to do; the server
            // setup hint (Supabase redirect URLs) is for developer mode only.
            auto timed_out = aoraton.snapshot();
            timed_out.pairing = aura::AoratonPairingState::Error;
            timed_out.last_error = "SIGN_IN_TIMEOUT";
            timed_out.sequence += 1000;
            reset_to(aura::UiView::AoratonPair);
            ui.apply_aoraton(timed_out);
            ui.show(aura::UiView::AoratonPair);
            const bool owner_copy = shows_copy(&aura::l10n::CompanionStrings::aoraton_timeout_hint) &&
                                    !shows_copy(&aura::l10n::CompanionStrings::aoraton_timeout_detail);
            ui.set_developer_mode(true);
            ui.show(aura::UiView::AoratonPair);
            const bool developer_copy = shows_copy(&aura::l10n::CompanionStrings::aoraton_timeout_detail);
            ui.set_developer_mode(false);
            auto restored = aoraton.snapshot();
            restored.sequence = timed_out.sequence + 1;
            ui.apply_aoraton(restored);
            record("aoraton.timeout_copy", "owner-or-developer", owner_copy && developer_copy,
                   std::to_string(owner_copy) + std::to_string(developer_copy));

            // The Status screen names the demo assistant without English copy
            // (an earlier check switched to the live provider).
            const bool was_live = live_provider;
            select_provider(false);
            pump(2);
            reset_to(aura::UiView::Battery);
            const std::string assistant_name = companion.extras().assistant_provider;
            record("status.demo_provider", "no-english-name",
                   !live_provider && !assistant_name.empty() && shows_text(assistant_name.c_str()) &&
                       !shows_text("Demo Mode"),
                   assistant_name);
            if (was_live) select_provider(true);
        }

        // The simulator has no panel to dim, so a veil stands in for it; the
        // device dims its backlight and must not get a veil on top of that.
        aura::CompanionCapabilities panel_capabilities{};
        panel_capabilities.panel_brightness = true;
        panel_capabilities.power_button = true;
        reset_to(aura::UiView::Home);
        auto dimmed = companion.display().snapshot();
        dimmed.mode = aura::DisplayMode::Dimmed;
        dimmed.brightness_percent = 30;
        dimmed.sequence += 1000;
        ui.apply_display(dimmed);
        const bool simulator_veil = veil_visible();
        ui.set_capabilities(panel_capabilities);
        const bool veil_after_capability = veil_visible();
        dimmed.sequence += 1;
        ui.apply_display(dimmed);
        const bool device_veil = veil_visible();
        record("display.dim_veil", "simulator-only", simulator_veil && !veil_after_capability && !device_veil,
               std::to_string(simulator_veil) + std::to_string(veil_after_capability) + std::to_string(device_veil));
        auto awake = companion.display().snapshot();
        awake.sequence = dimmed.sequence + 1;
        ui.apply_display(awake);

        // Power off says how to come back: the side button on the device (only
        // it wakes from deep sleep), a long press in the simulator. The row is
        // found by its name, so the list's scroll cannot send the tap elsewhere.
        auto open_power_off = [&] {
            reset_to(aura::UiView::System);
            auto* name = find_text(ui.root(), aura::l10n::companion_el_gr().system_power_off);
            if (!name) name = find_text(ui.root(), aura::l10n::companion_en_us().system_power_off);
            if (!name) return;
            lv_obj_scroll_to_view_recursive(name, LV_ANIM_OFF);
            pump(3);
            lv_area_t area{};
            lv_obj_get_coords(name, &area);
            tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        };
        open_power_off();
        const auto device_view = static_cast<int>(ui.view());
        const bool device_copy = ui.view() == aura::UiView::SystemConfirm &&
                                 shows_copy(&aura::l10n::CompanionStrings::system_power_off_detail_button) &&
                                 !shows_copy(&aura::l10n::CompanionStrings::system_power_off_detail);
        companion.set_capabilities(plain_capabilities);
        pump(2);
        open_power_off();
        const auto simulator_view = static_cast<int>(ui.view());
        const bool simulator_copy = ui.view() == aura::UiView::SystemConfirm &&
                                    shows_copy(&aura::l10n::CompanionStrings::system_power_off_detail) &&
                                    !shows_copy(&aura::l10n::CompanionStrings::system_power_off_detail_button);
        record("system.power_off_copy", "button-or-long-press", device_copy && simulator_copy,
               std::to_string(device_copy) + std::to_string(simulator_copy) + " views " +
                   std::to_string(device_view) + "/" + std::to_string(simulator_view));

        {
            // The always-on clock lights as little of the AMOLED as it can: its
            // sky halo stays within the clock block, not the whole screen.
            auto settings = companion.display().snapshot().settings;
            const bool always_on_before = settings.always_on;
            settings.always_on = true;
            companion.display().set_settings(settings);
            reset_to(aura::UiView::Home);
            companion.display().sleep_now(companion.now_ms());
            pump(3);
            const bool asleep = ui.view() == aura::UiView::AlwaysOn;
            int widest = 0;
            std::function<void(lv_obj_t*)> measure = [&](lv_obj_t* object) {
                if (!lv_obj_is_hidden(object) && lv_obj_get_style_bg_opa(object, LV_PART_MAIN) > 0 &&
                    lv_color_brightness(lv_obj_get_style_bg_color(object, LV_PART_MAIN)) > 0) {
                    widest = std::max(widest, static_cast<int>(lv_obj_get_width(object)));
                }
                for (std::uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
                    measure(lv_obj_get_child(object, static_cast<int32_t>(index)));
                }
            };
            lv_obj_update_layout(ui.root());
            measure(ui.root());
            tap(240, 240);
            pump(3);
            settings.always_on = always_on_before;
            companion.display().set_settings(settings);
            record("display.aod_glow", "lit-area", asleep && widest > 0 && widest <= 280, std::to_string(widest));
        }
        {
            // Demo data leads with DEMO: a chip or row that cuts its text short
            // ("Ζυμαρικά · D…") must still say it is DEMO. Runs last: the capture
            // seeds change the clock and the stores.
            companion.capture("home-timer");
            companion.capture("calendar");
            companion.capture("notes");
            pump(2);
            std::string unmarked;
            auto check_label = [&](const std::string& label) {
                if (label.find("DEMO") != std::string::npos && label.rfind("DEMO", 0) != 0) unmarked += label + ";";
            };
            for (const auto& timer : companion.clock().snapshot().timers) check_label(timer.label);
            for (const auto& alarm : companion.clock().snapshot().alarms) check_label(alarm.label);
            for (const auto& reminder : companion.clock().snapshot().reminders) check_label(reminder.text);
            for (const auto& event : companion.organizer().snapshot().events) check_label(event.title);
            for (const auto& task : companion.organizer().snapshot().tasks) check_label(task.text);
            for (const auto& item : companion.organizer().snapshot().shopping) check_label(item.text);
            for (const auto& note : companion.notes().snapshot().notes) check_label(note.text);
            for (const auto& station : radio_model.snapshot().results) check_label(station.name);
            record("capture.demo_first", "seed-labels", unmarked.empty(), unmarked);

            // The owner's side of a translation speaks Greek in either interface
            // language (every pair is Greek with another language).
            companion.set_language(aura::InterfaceLanguage::English);
            companion.capture("translate");
            bool greek_mine = false;
            for (const auto& entry : companion.translation().snapshot().entries) {
                if (entry.side != aura::TranslateSide::Mine) continue;
                greek_mine = std::any_of(entry.source.begin(), entry.source.end(),
                                         [](char ch) { return static_cast<unsigned char>(ch) >= 0x80; });
            }
            companion.set_language(aura::InterfaceLanguage::Greek);
            record("capture.translate_sides", "greek-mine-in-english", greek_mine, view_text());
        }

        {
            // The stored look is read from display.txt before the first frame.
            const auto store = std::filesystem::temp_directory_path() / "aura-appearance-restore-check";
            std::filesystem::create_directories(store);
            {
                std::ofstream file(store / "display.txt", std::ios::binary | std::ios::trunc);
                file << "aura-display 1\nalways_on=1\nappearance=aurora\n";
            }
            const auto stored = aura::simulator::read_stored_appearance(store.string());
            const auto missing = aura::simulator::read_stored_appearance((store / "missing").string());
            std::error_code removed;
            std::filesystem::remove_all(store, removed);
            record("appearance.restore", "display.txt",
                   stored == aura::Appearance::Aurora && missing == aura::Appearance::Halo,
                   aura::appearance_id(stored));

            // Last of all: a factory reset returns to Halo (it clears other state too).
            reset_to(aura::UiView::AppearanceSettings);
            tap(351, 186);  // the Marble card
            pump(3);
            const bool marble = ui.appearance() == aura::Appearance::Marble;
            reset_to(aura::UiView::System);
            scroll_view_to(120);
            tap(220, 452);
            const bool asks = ui.view() == aura::UiView::SystemConfirm;
            tap(352, 420);
            wait_for_home();
            record("appearance.factory_reset", "confirm",
                   marble && asks && ui.appearance() == aura::Appearance::Halo &&
                   companion.display().snapshot().settings.appearance == aura::Appearance::Halo &&
                   aura::ui::tokens::text == 0xF5F8FC,
                   aura::appearance_id(ui.appearance()));
        }

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
        radio_service.stop();
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
    bool smoke_stream_observed = false;
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
                select_provider(event.payload == "codex" || event.payload == "groq");
                ui.set_provider_live(live_provider);
            }
            else if (event.type == "language") {
                if (event.payload == "el" || event.payload == "en") {
                    const auto language = aura::parse_interface_language(event.payload);
                    companion.set_language(language);
                    ui.set_language(language);
                }
            }
            else if (event.type == "review_long_answer") {
                controller.dispatch({aura::EventType::Reset});
                controller.dispatch({aura::EventType::TurnStarted, text().capture_long_prompt});
                controller.dispatch({aura::EventType::FinalResult, text().capture_long_answer});
            }
            else if (event.type == "review_speaking") {
                controller.dispatch({aura::EventType::Reset});
                controller.dispatch({aura::EventType::TurnStarted, text().capture_speaking_prompt});
                controller.dispatch({aura::EventType::FinalResult, text().capture_speaking_answer});
                audio.play_test_tone(2400);
            }
            else if (event.type == "ui_view") {
                if (event.payload == "home") ui.show(aura::UiView::Home);
                else if (event.payload == "launcher") ui.show(aura::UiView::Launcher);
                else if (event.payload == "assistant") ui.show(aura::UiView::Assistant);
                else if (event.payload == "notifications") ui.show(aura::UiView::Notifications);
                else if (event.payload == "settings") ui.show(aura::UiView::Settings);
                else if (event.payload == "language") ui.show(aura::UiView::LanguageSettings);
                else if (event.payload == "diagnostics") ui.show(aura::UiView::Diagnostics);
                else if (event.payload == "system") ui.show(aura::UiView::System);
                else if (event.payload == "audio") ui.show(aura::UiView::Audio);
                else if (event.payload == "radio") ui.show(aura::UiView::Radio);
                else if (event.payload == "radio-search") ui.show(aura::UiView::RadioSearch);
                else if (event.payload == "aoraton") ui.show(aura::UiView::AoratonRooms);
                else if (event.payload == "weather") {
                    ui.show(aura::UiView::Weather);
                    open_weather();
                }
                else if (event.payload == "weather-settings") ui.show(aura::UiView::WeatherSettings);
                else if (event.payload == "weather-location") ui.show(aura::UiView::WeatherLocation);
                else if (event.payload == "calendar") ui.show(aura::UiView::Calendar);
                else if (event.payload == "battery") ui.show(aura::UiView::Battery);
                else if (event.payload == "voice") ui.show(aura::UiView::VoiceSettings);
                else if (event.payload == "wifi") ui.show(aura::UiView::Wifi);
                else if (event.payload == "clock") ui.show(aura::UiView::Clock);
                else if (event.payload == "notes") ui.show(aura::UiView::Notes);
                else if (event.payload == "briefing") ui.show(aura::UiView::Briefing);
                else if (event.payload == "translate") ui.show(aura::UiView::Translate);
                else if (event.payload == "phone") ui.show(aura::UiView::PhoneLink);
                else if (event.payload == "display") ui.show(aura::UiView::DisplaySettings);
                else if (event.payload == "update") ui.show(aura::UiView::Update);
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
            else if (event.type == "voice_preview") {
                if (event.payload == "athina") companion.preview_voice(aura::AssistantVoice::Athina);
                else if (event.payload == "nestoras") companion.preview_voice(aura::AssistantVoice::Nestoras);
            }
            else if (event.type == "radio_play") {
                const auto* station = event.payload.empty() ?
                    (radio_model.snapshot().favorites.empty() ? nullptr : &radio_model.snapshot().favorites.front()) :
                    radio_model.find_spoken(event.payload);
                if (station) play_radio(*station);
            }
            else if (event.type == "radio_stop") stop_radio();
            else if (event.type == "radio_search") search_radio(event.payload);
            else if (event.type == "fuel_refresh") refresh_fuel();
            else if (event.type == "input_amplitude") {
                manual_input_amplitude = static_cast<float>(std::clamp(integer(event.payload), 0, 100)) / 100.0F;
            }
            else if (event.type == "weather_city") {
                ui.show(aura::UiView::WeatherLocation);
                start_weather_search(event.payload, false);
            }
            else if (event.type == "weather_refresh") open_weather();
            else if (event.type == "aoraton_incoming" || event.type == "aoraton_unread") {
                const std::string text_value = event.payload.empty() ? text().incoming_demo_message : event.payload;
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
            ui.apply_radio(radio_model.snapshot());
            ui.apply_fuel(fuel_model.snapshot());
        }

        poll_weather();
        if (!deterministic_run && next_weather_refresh_at && now >= *next_weather_refresh_at &&
            !pending_weather_fetch && !pending_weather_search && weather_model.snapshot().has_saved_location) {
            if (device_status.snapshot().online) {
                start_weather_fetch(weather_model.snapshot().saved_location, false);
            } else {
                // Do not spin while offline; retry quietly after a short bounded delay.
                next_weather_refresh_at = now + std::chrono::minutes(5);
            }
        }
        poll_wifi(now);
        poll_radio();
        poll_fuel();
        tick_fuel(elapsed);
        poll_ancs();
        prune_finished(retired_replies);
        prune_finished(retired_weather_searches);
        prune_finished(retired_weather_fetches);
        prune_finished(retired_wifi_scans);
        prune_finished(retired_radio_searches);

        if (pending && pending_stream && pending_generation == controller.snapshot().turn_generation) {
            aura::AssistantStreamSnapshot update;
            if (pending_stream->take_after(pending_stream_sequence, update)) {
                pending_stream_sequence = update.sequence;
                companion.deliver_stream_update(update);
                if (smoke && !update.text.empty() && controller.snapshot().state == aura::AssistantState::Responding) {
                    smoke_stream_observed = true;
                }
                apply();
            }
        }

        if (!pending && controller.snapshot().state == aura::AssistantState::Transcribing &&
            !companion.handles_transcribing()) {
            // The desktop has no speech recognizer: the Listening screen shows a
            // labelled synthetic input, whose transcript is the fixed sample
            // prompt. Without this the assistant would wait in Thinking forever.
            start_turn(text().capture_short_prompt);
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
            if (!smoke_stream_observed) {
                std::cerr << "AURA: smoke did not observe a streamed partial reply\n";
                return 11;
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
        ui.apply_radio(radio_model.snapshot());
        ui.apply_fuel(fuel_model.snapshot());
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
    radio_service.stop();
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

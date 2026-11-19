#include "aura/assistant_actions.hpp"
#include "aura/assistant_companion.hpp"
#include "aura/sounds.hpp"
#include "aura/assistant_state.hpp"
#include "aura/aoraton.hpp"
#include "aura/aoraton_device_link.hpp"
#include "aura/aoraton_real.hpp"
#include "aura/display_power.hpp"
#include "aura/esp32_aoraton_account.hpp"
#include "aura/aura_frame_worker.hpp"
#include "aura/aura_mercury_view.hpp"
#include "aura/aura_theme.hpp"
#include "aura/aura_ui.hpp"
#include "aura/cloud_assistant.hpp"
#include "aura/companion_strings.hpp"
#include "aura/offline_commands.hpp"
#include "aura/voice_turn.hpp"
#include "aura/runtime_metrics.hpp"
#include "aoraton_build_config.h"
#include "aura_board_chips.hpp"
#include "aura_board_profile.h"
#include "aura_display.h"
#include "esp32_audio_backend.hpp"
#include "esp32_cloud_keys.hpp"
#include "esp32_https_client.hpp"
#include "esp32_companion.hpp"
#include "esp32_fuel_backend.hpp"
#include "esp32_network.hpp"
#include "esp32_radio_service.hpp"
#include "esp32_weather_backend.hpp"

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_pthread.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <future>
#include <memory>
#include <sys/time.h>
#include <string>
#include <utility>

namespace {
constexpr const char* kTag = "aura";

// Synapse draws its frames on a worker, so the UI never waits for one
// (start_frame_worker). The first boot on a board must report what the simulator cannot: frame and
// loop times, Synapse's cost and the free memory, on the serial console every
// 10 s for two minutes and every minute after. Touched under the display lock
// only (LVGL's render events fire in its task while it holds the lock).
aura::RuntimeMetrics metrics;
std::int64_t render_started_us = 0;

// Per frame: the time spent waiting for the panel's transfers, and the pixels
// sent, to tell drawing from sending and a small change from a full screen.
std::int64_t flush_wait_started_us = 0;
std::uint32_t frame_wait_us = 0;
std::uint64_t frame_pixels = 0;

void on_render_start(lv_event_t*) {
    render_started_us = esp_timer_get_time();
    frame_wait_us = 0;
    frame_pixels = 0;
}

void on_flush_start(lv_event_t* event) {
    if (const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event))) {
        frame_pixels += lv_area_get_size(area);
    }
}

void on_flush_wait_start(lv_event_t*) { flush_wait_started_us = esp_timer_get_time(); }

void on_flush_wait_finish(lv_event_t*) {
    if (flush_wait_started_us > 0) {
        frame_wait_us += static_cast<std::uint32_t>(esp_timer_get_time() - flush_wait_started_us);
    }
}

void on_render_ready(lv_event_t*) {
    if (render_started_us > 0) metrics.frame.add(static_cast<std::uint32_t>(esp_timer_get_time() - render_started_us));
    metrics.flush_wait.add(frame_wait_us);
    metrics.flushed_pixels += frame_pixels;
}

void report_metrics(aura::AuraUi& ui, std::uint64_t& presence_frames, std::uint32_t window_ms,
                    const aura::l10n::CompanionStrings& strings) {
    metrics.uptime_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    const std::uint64_t frames = ui.mercury_frame_count();
    metrics.presence_fps = window_ms == 0 || frames < presence_frames ? 0.0 :
        static_cast<double>(frames - presence_frames) * 1000.0 / static_cast<double>(window_ms);
    presence_frames = frames;
    metrics.presence_frame_ms = ui.mercury_frame_ms();
    metrics.presence_reduced = ui.mercury_reduced();
    constexpr std::uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    metrics.internal_free = heap_caps_get_free_size(internal);
    metrics.internal_min_free = heap_caps_get_minimum_free_size(internal);
    metrics.internal_largest = heap_caps_get_largest_free_block(internal);
    metrics.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    metrics.psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    metrics.psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(kTag, "metrics %s", aura::format_runtime_metrics(metrics).c_str());
    // The UI loop's stack: the least ever left free (CONFIG_ESP_MAIN_TASK_STACK_SIZE).
    const auto stack_free = static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr));
    // The LVGL task's (16 KiB, aura_display.c), for the same reason.
    static TaskHandle_t lvgl_task = xTaskGetHandle("lvgl");
    const auto lvgl_free = lvgl_task ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(lvgl_task)) : 0U;
    ESP_LOGI(kTag, "stack main_min_free=%u lvgl_min_free=%u", stack_free, lvgl_free);
    // The same numbers on Ρυθμίσεις → Σύστημα → Διαγνωστικά.
    char text[256];
    std::snprintf(text, sizeof(text), strings.diagnostics_device_format,
                  static_cast<double>(metrics.frame.mean_us()) / 1000.0,
                  static_cast<double>(metrics.frame.max_us()) / 1000.0,
                  static_cast<double>(metrics.loop.mean_us()) / 1000.0, metrics.presence_fps,
                  static_cast<unsigned>(metrics.internal_free / 1024U),
                  static_cast<unsigned>(metrics.psram_free / 1024U), stack_free);
    ui.set_device_metrics(text);
    metrics.frame.reset();
    metrics.flush_wait.reset();
    metrics.flushed_pixels = 0;
    metrics.loop.reset();
}

// The processor runs at 240 MHz only while the screen is lit (a held lock) and
// at 80 MHz otherwise; drivers hold their own locks while they transfer.
esp_pm_lock_handle_t start_power_management() {
    const esp_pm_config_t config = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = false};
    if (esp_pm_configure(&config) != ESP_OK) {
        ESP_LOGW(kTag, "Dynamic frequency scaling unavailable; staying at full speed");
        return nullptr;
    }
    esp_pm_lock_handle_t lock = nullptr;
    if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "aura_screen", &lock) != ESP_OK) return nullptr;
    esp_pm_lock_acquire(lock);
    return lock;
}

// The cloud requests block for seconds, each on a worker thread with room for
// a TLS handshake on its stack: one for Groq (listening and answers), one for
// Azure's voice, which is asked for while the answer is still being written.
std::unique_ptr<aura::CloudWorker> start_cloud_worker(const char* name) {
    esp_pthread_cfg_t worker = esp_pthread_get_default_config();
    worker.thread_name = name;
    worker.stack_size = 12288;
    worker.prio = 4;
    // Groq and Azure over HTTPS only, never the flash: the stack lives in PSRAM
    // (internal RAM ran out on the first flash, 2026-11-14).
    worker.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    esp_pthread_set_cfg(&worker);
    auto started = std::make_unique<aura::CloudWorker>();
    const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&defaults);
    return started;
}

// The device's own assistant (the owner's choice, 2026-11-05): Groq listens and
// answers, Azure speaks, straight from the device, once the owner's keys are in
// NVS (sent from the PC: scripts/send-device-keys.ps1). Restarted with new keys.
struct CloudAssistant {
    std::unique_ptr<aura::CloudAssistantProvider> provider;
    std::unique_ptr<aura::CloudSpeechService> speech;
    std::unique_ptr<aura::VoiceAssistantTurn> turn;
};

// How long a spoken answer keeps the owner waiting, on the serial console once
// the voice has begun and the answer is whole (numbers only).
class TurnTimes {
public:
    void follow(const aura::AssistantSnapshot& turn, bool sounding, std::int64_t now_ms) {
        const bool answering =
            turn.state == aura::AssistantState::Thinking || turn.state == aura::AssistantState::Responding;
        if (!answering) {
            asked_ms_ = -1;
            return;
        }
        if (asked_ms_ < 0) {
            asked_ms_ = now_ms;
            words_ms_ = voice_ms_ = whole_ms_ = -1;
            reported_ = false;
        }
        if (words_ms_ < 0 && turn.state == aura::AssistantState::Responding) words_ms_ = now_ms;
        if (whole_ms_ < 0 && !turn.answer_text.empty()) whole_ms_ = now_ms;
        if (voice_ms_ < 0 && sounding) voice_ms_ = now_ms;
        if (reported_ || words_ms_ < 0 || voice_ms_ < 0 || whole_ms_ < 0) return;
        reported_ = true;
        ESP_LOGI(kTag, "turn: after the question, first words %lld ms, voice %lld ms, whole answer %lld ms",
                 static_cast<long long>(words_ms_ - asked_ms_), static_cast<long long>(voice_ms_ - asked_ms_),
                 static_cast<long long>(whole_ms_ - asked_ms_));
    }

private:
    std::int64_t asked_ms_{-1};
    std::int64_t words_ms_{-1};
    std::int64_t voice_ms_{-1};
    std::int64_t whole_ms_{-1};
    bool reported_{false};
};

const aura::l10n::CompanionStrings& companion_strings(aura::InterfaceLanguage language) {
    return language == aura::InterfaceLanguage::English ? aura::l10n::companion_en_us() :
                                                          aura::l10n::companion_el_gr();
}

// Synapse draws its frames on the first core, beside this loop (the same
// priority: they take turns every millisecond) and below Wi-Fi, Bluetooth and
// the audio. The second core belongs to LVGL, which spent 55-70 ms showing
// each of Synapse's frames (measured on the board, 2026-11-14): sharing that
// core, Synapse waited for LVGL and LVGL for Synapse.
void start_frame_worker() {
    esp_pthread_cfg_t worker = esp_pthread_get_default_config();
    worker.thread_name = "synapse";
    worker.pin_to_core = 0;
    worker.prio = 1;
    worker.stack_size = 4096;
    esp_pthread_set_cfg(&worker);
    aura::ui::FrameWorker::start_shared();
    const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&defaults);
}

// Half of each of Synapse's frames is drawn on the second core, below LVGL:
// with Synapse's rows going to the panel straight, LVGL draws a few rows a
// frame there and leaves the core idle most of the time (2026-11-15).
void start_frame_helper() {
    esp_pthread_cfg_t helper = esp_pthread_get_default_config();
    helper.thread_name = "synapse2";
    helper.pin_to_core = 1;
    helper.prio = 1;
    helper.stack_size = 4096;
    esp_pthread_set_cfg(&helper);
    aura::ui::FrameWorker::start_helper();
    const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&defaults);
}

// Synapse's middle rows go to the panel straight (aura::ui::SynapseOutput):
// LVGL then draws only the band's top and bottom rows of each frame, not all
// of it in PSRAM the frame was drawn in. The worker's two strips are internal
// RAM, which the view gathers its light in and the panel's DMA reads; the
// helper's two are PSRAM, its light kept in the data cache and copied to the
// panel's DMA as LVGL's own bands are (the internal RAM has no room for four).
// Sent under LVGL's lock.
class PanelSynapseOutput final : public aura::ui::SynapseOutput {
public:
    bool start(std::size_t bytes) {
        for (std::size_t index = 0; index < strips_.size(); ++index) {
            const std::uint32_t caps = index < 2 ? (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) : MALLOC_CAP_SPIRAM;
            strips_[index] = heap_caps_malloc(bytes, caps);
            if (strips_[index] == nullptr) {
                for (auto& taken : strips_) {
                    heap_caps_free(taken);
                    taken = nullptr;
                }
                return false;
            }
        }
        bytes_ = bytes;
        return true;
    }
    void* strip(int index, std::size_t bytes) override {
        return bytes <= bytes_ ? strips_[static_cast<std::size_t>(index & 3)] : nullptr;
    }
    bool lock(std::uint32_t timeout_ms) override {
        return aura_board_display_lock(static_cast<std::int32_t>(timeout_ms));
    }
    void unlock() override { aura_board_display_unlock(); }
    bool send(int left, int top, int columns, int rows, const void* pixels) override {
        return aura_board_display_draw(left, top, left + columns, top + rows, pixels);
    }

private:
    std::array<void*, 4> strips_{};
    std::size_t bytes_{};
};
PanelSynapseOutput synapse_panel;

// After the services have started, so their start-up peaks keep the internal
// RAM they had (64 KiB free, 38 KiB at the least, on the board, 2026-11-14).
// Synapse's worker wakes this loop when it has drawn a frame: the loop shows
// it at once and begins the next once its rest is over. Waiting out its 33 ms
// passes, the loop showed 6.9 frames a second of 85 ms ones (2026-11-15).
void wake_loop(void* task) { xTaskNotifyGive(static_cast<TaskHandle_t>(task)); }

void start_synapse_output() {
    // A strip's light, six rows of 8-bit blue, green and red; finished, the
    // same memory holds the panel's RGB565 rows.
    constexpr std::size_t bytes = static_cast<std::size_t>(aura::ui::AuraMercuryView::width) *
                                  aura::ui::AuraMercuryView::strip_rows * 3U;
    if (synapse_panel.start(bytes)) {
        aura::ui::set_synapse_output(&synapse_panel);
    } else {
        ESP_LOGW(kTag, "Synapse goes through LVGL: no internal RAM for its strips");
    }
}

// The project's public values from config/aoraton.local.json at build time
// (firmware/main/CMakeLists.txt); false when the build had none.
bool built_in_aoraton_config(aura::AoratonLinkConfig& config) {
    config.base_url = AURA_AORATON_BUILT_IN_URL;
    config.anon_key = AURA_AORATON_BUILT_IN_ANON_KEY;
    config.inco_enabled = AURA_AORATON_BUILT_IN_INCO;
    return aura::aoraton_link_config_valid(config);
}

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

    // As bsp_display_start(), but LVGL draws bands tall enough for the header's
    // glass, and the panel stays dark until AURA's own first screen is drawn.
    lv_display_t* display = aura_board_display_start(aura::ui::tokens::DEVICE_DRAW_ROWS);
    if (display == nullptr) {
        ESP_LOGE(kTag, "BSP display initialization failed");
        return;
    }
    // The brand's start-up screen is the first thing the panel shows (it showed
    // two seconds of white before, the owner, 2026-11-14); its bar follows the
    // services below as they start, and ui.create() fills it.
    static aura::AuraUi ui;
    if (aura_board_display_lock(-1)) {
        ui.show_startup(display);
        aura_board_display_unlock();
    }
    aura_board_display_reveal(100);
    const auto boot_step = [](int percent) {
        if (aura_board_display_lock(100)) {
            ui.set_boot_progress(percent);
            aura_board_display_unlock();
        }
    };
    boot_step(8);

    // The board's clock keeps UTC across restarts, so the time is known before
    // Wi-Fi; SNTP corrects it once connected (the clock is written after each sync).
    static aura::firmware::Pcf85063Clock board_clock;
    static aura::firmware::Axp2101Power board_power;
    const bool board_clock_ready = board_clock.start();
    if (board_clock_ready) {
        if (const auto utc = board_clock.read_utc()) {
            const timeval now{static_cast<time_t>(*utc), 0};
            settimeofday(&now, nullptr);
            ESP_LOGI(kTag, "Time from the board clock");
        } else {
            ESP_LOGW(kTag, "The board clock lost its time; waiting for the network clock");
        }
    }
    const bool board_power_ready = board_power.start();
    ESP_LOGI(kTag, "Board clock %s, power manager %s", board_clock_ready ? "ready" : "unavailable",
             board_power_ready ? "ready" : "unavailable");
    boot_step(18);

    static aura::AssistantController controller;
    static aura::DeviceStatusModel device_status;
    // The owner's own aoraton.com account (ADR 0010). The device cannot show
    // Google's page: the owner's PC signs it in over File transfer's session
    // (scripts/aoraton-device-sign-in.mjs); the session and the room keys stay
    // here, in encrypted NVS. Until the PC has sent the project settings the
    // account stays signed out, never DEMO.
    static aura::AoratonMailbox aoraton_mailbox;
    static aura::firmware::Esp32AoratonAccountCrypto aoraton_ids;
    static aura::RealAoratonService aoraton(aoraton_mailbox, aura::AoratonRuntimeMode::Live,
                                            [] { return aura::aoraton_new_client_message_id(aoraton_ids); });
    static std::atomic<bool> aoraton_config_changed{false};
    // The PC's settings are stored: the PC is told so at once, even while the
    // account restarts with them (a sign-in it asks for then waits).
    static std::atomic<bool> aoraton_configured{false};
    static aura::AoratonDeviceLink aoraton_link([](const aura::AoratonLinkConfig& config) {
        // The helper sends the settings on every run: only a change restarts the account.
        aura::AoratonLinkConfig stored;
        if (aura::firmware::esp32_aoraton_load_config(stored) && stored.base_url == config.base_url &&
            stored.anon_key == config.anon_key && stored.inco_enabled == config.inco_enabled) {
            aoraton_configured = true;
            return true;
        }
        if (!aura::firmware::esp32_aoraton_save_config(config)) return false;
        aoraton_configured = true;
        aoraton_config_changed = true;
        return true;
    });
    static std::unique_ptr<aura::firmware::Esp32AoratonAccount> aoraton_account;
    // The owner tapped sign-in: File transfer, once ready, shows the phone's QR.
    static bool aoraton_phone_wanted = false;
    static aura::WeatherModel weather;
    static aura::firmware::Esp32AudioBackend audio;
    static aura::firmware::Esp32WeatherBackend weather_backend;
    static aura::FuelPriceModel fuel;
    static aura::firmware::Esp32FuelBackend fuel_backend;
    static aura::firmware::Esp32Network network;
    static aura::WifiModel wifi;
    static aura::firmware::Esp32Companion companion(audio, device_status);
    // The internet radio (the owner's request, 2026-11-06): stations stream
    // through the audio backend's continuous channel; favourites on the card.
    static aura::RadioModel radio_model;
    static std::unique_ptr<aura::firmware::Esp32RadioService> radio;
    static std::uint64_t radio_generation = 0;
    static bool radio_held = false;
    // A station plays or stops from the Ράδιο app or from the owner's voice.
    static const auto play_radio = [](const aura::RadioStation& selected) {
        const aura::RadioStation station = selected;  // `selected` may be the model's own
        radio_model.begin_connect(station);
        radio_generation = radio->play(station);
        ui.apply_radio(radio_model.snapshot());
    };
    static const auto stop_radio = [] {
        radio->stop();
        radio_generation = radio->generation();
        radio_model.stop();
        ui.apply_radio(radio_model.snapshot());
    };
    static aura::firmware::Esp32HttpsClient cloud_https;
    static aura::firmware::Esp32HttpsClient voice_https;
    static aura::firmware::Esp32CloudKeyStore cloud_keys;
    static std::unique_ptr<aura::CloudWorker> cloud_worker = start_cloud_worker("aura_cloud");
    static std::unique_ptr<aura::CloudWorker> voice_worker = start_cloud_worker("aura_voice");
    static CloudAssistant assistant;
    static TurnTimes turn_times;
    static aura::InterfaceLanguage assistant_language = aura::InterfaceLanguage::Greek;
    // Ήχος → «Ακρόαση φωνής»: the preview phrase on its way from Azure.
    static std::future<aura::SpeechResult> voice_preview;
    companion.set_cloud_key_sink(&cloud_keys);
    companion.set_aoraton_link(&aoraton_link);
    const bool audio_ready = audio.initialize();
    boot_step(30);
    const bool weather_backend_ready = weather_backend.initialize();
    if (weather_backend_ready) weather.set_refresh_interval(weather_backend.refresh_interval());
    boot_step(38);
    const bool network_started = network.start();
    boot_step(50);
    ESP_LOGI(kTag, "Audio backend initialization: %s", audio_ready ? "ready" : "unavailable");
    ESP_LOGI(kTag, "Weather backend initialization: %s",
             weather_backend_ready && weather_backend.configured() ? "ready" : "unavailable");
    ESP_LOGI(kTag, "Network: %s", network_started ? "Wi-Fi radio started" : "radio unavailable");
    companion.set_power_key(board_power_ready ? &board_power : nullptr);
    companion.start();
    boot_step(68);
    if (audio_ready) radio = std::make_unique<aura::firmware::Esp32RadioService>(audio);
    if (const std::string saved = companion.load_text("radio.txt");
        !saved.empty() && !radio_model.restore_favorites(saved)) {
        ESP_LOGW(kTag, "Radio: the saved favourites were not readable");
    }
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
    // The week's fuel prices: half a minute after the start, then as the model says.
    fuel_backend.initialize();
    std::uint32_t fuel_refresh_ms = aura::fuel_refresh_interval_ms(fuel.snapshot()) - 30000U;
    // The saved city is fetched once the station first obtains an IP address.
    bool boot_weather_pending = false;
    std::uint32_t weather_refresh_ms = 0;
    if (weather_backend_ready && weather_backend.configured()) {
        if (const auto location = weather_backend.saved_location()) {
            weather.restore_location(*location);
            boot_weather_pending = true;
        }
    }

    start_frame_worker();
    aura::ui::FrameWorker::shared()->set_finished(wake_loop, xTaskGetCurrentTaskHandle());
    start_frame_helper();
    start_synapse_output();
    boot_step(76);

    // Listening for a question: from the talk target, after a spoken answer
    // (continuous conversation) and when the owner speaks over one (barge-in).
    static const auto start_listening = [](bool automatic) {
        if (!assistant.turn) {
            controller.dispatch({aura::EventType::ProviderUnavailable, "PROVIDER_NOT_CONFIGURED", 0});
            return;
        }
        // begin_question keeps the barge-in monitor's microphone open (no
        // ES7210 settling; after a barge-in, the words that interrupted).
        assistant.turn->begin_listening(automatic);
        if (!companion.begin_question()) {
            assistant.turn->cancel();
            controller.dispatch({aura::EventType::Fail, "MICROPHONE_UNAVAILABLE", 0});
        }
    };

    // LVGL callbacks run in the BSP's LVGL task while it holds the display lock;
    // the loop below touches the same models only under that lock as well.
    if (aura_board_display_lock(-1)) {
        // A fading screen is drawn whole, through a layer, for each step of the
        // fade: 150-190 ms a frame on the board, ten frames to open the menu
        // (measured 2026-11-18). Here a new screen is simply there.
        ui.set_view_fade(false);
        aura::AuraUiCallbacks callbacks;
        callbacks.on_local_time = [network_started](std::int64_t timestamp) -> std::optional<aura::LocalDateTime> {
            if (!network_started) return std::nullopt;
            const auto when = static_cast<std::time_t>(timestamp);
            if (static_cast<std::int64_t>(when) != timestamp) return std::nullopt;
            std::tm local{};
            if (!localtime_r(&when, &local)) return std::nullopt;
            return aura::LocalDateTime{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                                      local.tm_hour, local.tm_min, local.tm_sec, local.tm_wday};
        };
        callbacks.on_home = [] { controller.dispatch({aura::EventType::Reset, {}, 0}); };
        callbacks.on_notifications_clear = [] { controller.dispatch({aura::EventType::NotificationsCleared, {}, 0}); };
        callbacks.on_audio_test = [] { audio.play_test_tone(); };
        callbacks.on_audio_wav = [] { audio.play_wav("/sdcard/aura/test.wav"); };
        callbacks.on_audio_pause_resume = [] {
            if (audio.diagnostics().state == aura::AudioState::Paused) audio.resume();
            else audio.pause();
        };
        callbacks.on_audio_stop = [] {
            // The owner stopped the voice: no follow-up listening after it.
            if (assistant.turn) assistant.turn->stop_speaking();
            audio.stop();
        };
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
        companion.install(callbacks, &aoraton);
        callbacks.on_aoraton_pair_cancel = [] { aoraton.cancel_pairing(); };
        // A new topic: the assistant forgets the last questions, the history stays.
        callbacks.on_conversation_new = [] {
            if (assistant.turn) assistant.turn->forget_conversation();
        };
        callbacks.on_conversation_clear = [] {
            companion.clear_conversation();
            if (assistant.turn) assistant.turn->forget_conversation();
        };
        callbacks.on_aoraton_phone_sign_in = [] { aoraton_phone_wanted = true; };
        callbacks.on_aoraton_unpair = [] { aoraton.unpair(); };
        callbacks.on_aoraton_unlock = [](const std::string& passphrase) { return aoraton.unlock_keys(passphrase); };
        callbacks.on_aoraton_open_attachment = [](const std::string& id) { return aoraton.open_attachment(id); };
        callbacks.on_aoraton_close_attachment = [] { aoraton.close_attachment(); };
        // A voice message: the companion records it like a note; the WAV goes
        // to the service, which waits for the confirmation.
        static std::string voice_message_room;
        callbacks.on_aoraton_voice_start = [](const std::string& room_id) {
            voice_message_room = room_id;
            return companion.begin_voice_message();
        };
        callbacks.on_aoraton_voice_stop = [] { companion.end_voice_message(); };
        callbacks.on_aoraton_voice_cancel = [] { companion.cancel_voice_message(); };
        companion.set_message_sink([](std::vector<std::uint8_t> wav) {
            aoraton.prepare_voice(voice_message_room, std::move(wav));
        });
        // The radio (no output: the app stays greyed out, as before).
        if (radio) {
            callbacks.on_radio_play = play_radio;
            callbacks.on_radio_stop = stop_radio;
            callbacks.on_radio_search = [](const std::string& query) {
                radio_model.begin_search(query);
                if (!radio->search(query)) radio_model.search_finished({}, "RADIO_BUSY");
                ui.apply_radio(radio_model.snapshot());
            };
            callbacks.on_radio_favorite = [](const aura::RadioStation& station) {
                if (radio_model.toggle_favorite(station) &&
                    !companion.save_text("radio.txt", radio_model.serialize_favorites())) {
                    ESP_LOGW(kTag, "Radio: could not save the favourites");
                }
                ui.apply_radio(radio_model.snapshot());
            };
            callbacks.on_radio_move = [](std::size_t index, int direction) {
                if (radio_model.move_favorite(index, direction) &&
                    !companion.save_text("radio.txt", radio_model.serialize_favorites())) {
                    ESP_LOGW(kTag, "Radio: could not save the favourites");
                }
                ui.apply_radio(radio_model.snapshot());
            };
        }
        // The assistant's spoken question: the companion records it with the
        // microphone it shares with notes; the turn transcribes, asks and speaks.
        callbacks.on_ask = [] { start_listening(false); };
        // The read-aloud button of Σήμερα and of an answer.
        callbacks.on_speak = [](const std::string& text, const std::string&) {
            if (assistant.turn) assistant.turn->read_aloud(text);
        };
        callbacks.on_speak_stop = [] {
            if (assistant.turn) assistant.turn->stop_speaking();
            audio.stop();
        };
        callbacks.on_end_listening = [] { companion.end_question(); };
        // A proposed action (a timer, an alarm, a note...) runs only once the
        // owner confirms it, as in the simulator.
        callbacks.on_action_confirm = [] {
            const bool done = companion.confirm_action();
            controller.dispatch({aura::EventType::ConfirmAction, {}, 0});
            const auto& strings = companion_strings(companion.language());
            ui.show_message(done ? strings.action_done : strings.action_failed, !done);
        };
        callbacks.on_action_decline = [] {
            companion.decline_action();
            controller.dispatch({aura::EventType::DeclineAction, {}, 0});
        };
        callbacks.on_cancel = [] {
            companion.cancel_question();
            if (assistant.turn) assistant.turn->cancel();
            audio.stop();
            controller.dispatch({aura::EventType::Cancel, {}, 0});
        };
        callbacks.on_retry = [] {
            const std::string prompt = controller.snapshot().user_text;
            controller.dispatch({aura::EventType::Retry, {}, 0});
            if (assistant.turn && !prompt.empty()) assistant.turn->ask(prompt);
        };
        // Ήχος → «Ακρόαση φωνής»: the preview phrase in the voice it was pressed
        // for, spoken by Azure like an answer (the simulator's preview_voice). The
        // loop plays it once it arrives.
        callbacks.on_voice_preview = [](aura::AssistantVoice voice) {
            const char* language = aura::interface_language_code(companion.language());
            if (!assistant.speech || !assistant.speech->tts_available(language)) return;
            voice_preview = assistant.speech->synthesize(companion_strings(companion.language()).voice_preview_phrase,
                                                         language, aura::assistant_voice_id(voice));
        };
        companion.set_question_sink([](std::vector<std::uint8_t> wav, bool heard) {
            if (assistant.turn) assistant.turn->finish_listening(std::move(wav), heard);
        });
        ui.set_language(companion.language());
        ui.set_appearance(companion.appearance());
        ui.set_aoraton_sign_in_relay(true);
        ui.create(display, std::move(callbacks));
        // Διαγνωστικά shows the board's measurements, never the simulator's.
        ui.set_device_metrics(companion_strings(companion.language()).diagnostics_device_waiting);
        companion.apply(ui);
        ui.apply(controller.snapshot());
        ui.apply_status(device_status.snapshot());
        ui.apply_audio(audio.diagnostics());
        ui.apply_aoraton(aoraton.snapshot());
        ui.apply_weather(weather.snapshot());
        ui.apply_wifi(wifi.snapshot());
        ui.apply_radio(radio_model.snapshot());
        lv_display_add_event_cb(display, on_render_start, LV_EVENT_RENDER_START, nullptr);
        lv_display_add_event_cb(display, on_render_ready, LV_EVENT_RENDER_READY, nullptr);
        lv_display_add_event_cb(display, on_flush_start, LV_EVENT_FLUSH_START, nullptr);
        lv_display_add_event_cb(display, on_flush_wait_start, LV_EVENT_FLUSH_WAIT_START, nullptr);
        lv_display_add_event_cb(display, on_flush_wait_finish, LV_EVENT_FLUSH_WAIT_FINISH, nullptr);
        ESP_LOGI(kTag, "UI ready %lu ms after boot", static_cast<unsigned long>(esp_timer_get_time() / 1000));
        aura_board_display_unlock();
    }

    // Starts, or restarts with new keys, the assistant. Under the display lock.
    const auto start_assistant = [] {
        companion.set_speech_service(nullptr);
        if (assistant.turn) assistant.turn->cancel();
        assistant.turn.reset();
        assistant.speech.reset();
        assistant.provider.reset();
        auto keys = cloud_keys.load();
        if (keys.groq_api_key.empty() || !cloud_worker || !voice_worker) {
            companion.set_speech_available(false, false);
            ESP_LOGI(kTag, "Assistant: no cloud keys yet (send them from the PC over the file transfer session)");
            return;
        }
        assistant.provider = std::make_unique<aura::CloudAssistantProvider>(cloud_https, *cloud_worker,
                                                                            keys.groq_api_key);
        assistant.speech = std::make_unique<aura::CloudSpeechService>(cloud_https, *cloud_worker, voice_https,
                                                                      *voice_worker, keys);
        aura::wipe_cloud_keys(keys);
        assistant.turn = std::make_unique<aura::VoiceAssistantTurn>(controller, *assistant.speech,
                                                                    *assistant.provider, audio);
        assistant_language = companion.language();
        const auto& strings = companion_strings(assistant_language);
        assistant.turn->set_language(assistant_language);
        assistant.turn->set_answer_copy(strings.action_question, strings.action_refused, strings.empty_answer);
        assistant.turn->set_action_handler([](const aura::AssistantAction& action) { companion.propose_action(action); });
        // A message by voice: Aoraton's own confirmation, for the room named.
        assistant.turn->set_message_handler([](const aura::AssistantAction& action) {
            const auto* room = aura::assistant_message_room(aoraton.snapshot().rooms, action.room);
            if (room && aoraton.prepare_send(room->id, action.text)) {
                ui.apply_aoraton(aoraton.snapshot());
                ui.show(aura::UiView::AoratonConfirm);
            } else {
                ui.show_message(companion_strings(companion.language()).action_failed, true);
            }
        });
        assistant.turn->set_listen_again([] { start_listening(true); });
        assistant.turn->set_turn_recorded([](const std::string& question, const std::string& answer) {
            companion.record_conversation(question, answer);
        });
        companion.set_speech_service(assistant.speech.get());
        // "Βάλε Σφαίρα", "σταμάτα το ραδιόφωνο": the radio does it, decided as
        // in the simulator (radio_request); the assistant is not asked.
        assistant.turn->set_local_request([](const std::string& question) {
            if (radio) {
                const auto request = aura::radio_request(question, radio_model);
                if (request.kind != aura::RadioRequestKind::None) {
                    controller.dispatch({aura::EventType::Cancel, {}, 0});
                    ui.show(aura::UiView::Radio);
                    if (request.kind == aura::RadioRequestKind::Stop) stop_radio();
                    else if (request.kind == aura::RadioRequestKind::Play) play_radio(*request.station);
                    else ui.apply_radio(radio_model.snapshot());
                    return true;
                }
            }
            // The owner's own words for the device, matched here and never by
            // the model, are done at once (the owner, 2026-11-18). What the
            // model proposes still waits for a tap. The time and the battery
            // are answered from the device's facts.
            const auto command = aura::parse_offline_command(question);
            if (command.kind == aura::OfflineCommandKind::None || command.kind == aura::OfflineCommandKind::ShowTime ||
                command.kind == aura::OfflineCommandKind::ShowBattery) {
                return false;
            }
            controller.dispatch({aura::EventType::Cancel, {}, 0});
            if (command.kind == aura::OfflineCommandKind::Restart) {
                companion.system_action(aura::SystemAction::Restart);
            } else if (command.kind == aura::OfflineCommandKind::PowerOff) {
                companion.system_action(aura::SystemAction::PowerOff);
            } else if (const char* app = aura::offline_command_app(command.kind)) {
                ui.open_app(app);
            } else if (command.has_action) {
                const bool done = companion.perform_action(command.action);
                const auto& strings = companion_strings(companion.language());
                ui.show_message(done ? strings.action_done : strings.action_failed, !done);
            }
            return true;
        });
        assistant.turn->set_context([](const std::string& question) {
            aura::DeviceContextInput input;
            input.status = &device_status.snapshot();
            input.weather = &weather.snapshot();
            input.fuel = &fuel.snapshot();
            input.clock = &companion.clock();
            input.notes = &companion.notes();
            input.organizer = &companion.organizer();
            input.phone = &companion.phone();
            input.question = question;
            return aura::build_device_context(input);
        });
        companion.set_speech_available(
            true, assistant.speech->tts_available(aura::interface_language_code(assistant_language)));
        ESP_LOGI(kTag, "Assistant: Groq answers%s", assistant.speech->tts_available("el") ? ", Azure speaks" : "");
    };
    std::uint32_t assistant_keys = cloud_keys.generation();
    if (aura_board_display_lock(-1)) {
        start_assistant();
        aura_board_display_unlock();
    }

    // (Re)starts the account with the project settings the PC sent last or,
    // without them, the ones built in from config/aoraton.local.json, so the
    // owner's phone alone can sign it in (ADR 0010).
    const auto start_aoraton = [] {
        aoraton_account.reset();
        aura::AoratonLinkConfig config;
        if (!aura::firmware::esp32_aoraton_load_config(config) && !built_in_aoraton_config(config)) return;
        aoraton_configured = true;
        auto account = std::make_unique<aura::firmware::Esp32AoratonAccount>(aoraton_mailbox, config, aoraton_link);
        if (account->start()) aoraton_account = std::move(account);
    };
    start_aoraton();
    ESP_LOGI(kTag, "Aoraton: %s", aoraton_account ? "account ready" : "waiting for the project's settings");
    bool external_power = false;
    std::string aoraton_city;
    std::shared_ptr<const aura::PcmClip> voice_message_playing;

    esp_pm_lock_handle_t full_speed = start_power_management();
    std::uint32_t metrics_ms = 0;
    std::uint64_t presence_frames = 0;
    std::uint32_t clock_written_syncs = 0;
    std::uint32_t power_ms = 5000;
    std::uint32_t storage_ms = 50000;  // the first look 10 s after boot
    bool at_full_speed = full_speed != nullptr;
    TickType_t previous = xTaskGetTickCount();
    TickType_t ticked = previous;  // the UI's last tick
    std::uint32_t unapplied_ms = 0;
    std::uint32_t status_ms = 1000;
    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        unapplied_ms += static_cast<std::uint32_t>((now - previous) * portTICK_PERIOD_MS);
        previous = now;
        // LVGL's frames take 50-150 ms on the board (the first flash,
        // 2026-11-14): waiting only 25 ms skipped most of the loop's work while
        // the screen moved, and each miss logged an error.
        if (aura_board_display_lock(200)) {
            const std::int64_t pass_started_us = esp_timer_get_time();
            const std::uint32_t elapsed_ms = std::min<std::uint32_t>(unapplied_ms, 1000);
            unapplied_ms = 0;
            audio.tick(elapsed_ms);
            aoraton.tick(elapsed_ms);
            // Aoraton: settings and requests from the PC helper, then the
            // hybrid sync (realtime while lit or on USB, a 5-minute check
            // otherwise; the owner's choice).
            aura::aoraton_link_pump(aoraton_link, aoraton, aoraton_configured, aoraton_account != nullptr);
            // The phone's way: File transfer shows the QR while a phone sign-in is on.
            ui.set_aoraton_phone_sign_in(aura::aoraton_phone_pump(
                aoraton_link, companion.transfer_snapshot(), aoraton_phone_wanted,
                [](std::array<std::uint8_t, 16>& secret) {
                    esp_fill_random(secret.data(), secret.size());
                    return true;
                }));
            // The radio: its worker's events and a finished search; silent while
            // the microphone records.
            if (radio) {
                bool radio_changed = false;
                aura::RadioWorkerEvent event;
                while (radio->poll(event)) {
                    if (event.generation != radio_generation) continue;
                    if (event.type == aura::RadioWorkerEventType::Playing) radio_model.set_playing();
                    else if (event.type == aura::RadioWorkerEventType::Title) radio_model.set_title(event.text);
                    else if (event.type == aura::RadioWorkerEventType::Reconnecting)
                        radio_model.begin_connect(radio_model.snapshot().station);
                    else radio_model.fail(event.text);
                    radio_changed = true;
                }
                aura::RadioSearchResult found;
                if (radio->take_search_result(found)) {
                    radio_model.search_finished(std::move(found.stations),
                                                found.ok ? std::string{} : found.error_code);
                    radio_changed = true;
                }
                if (radio_changed) ui.apply_radio(radio_model.snapshot());
                if (const bool hold = companion.microphone_active(); hold != radio_held) {
                    radio_held = hold;
                    radio->set_hold(hold);
                }
            }
            aoraton.set_power_saving(!aura::realtime_sync_allowed(companion.display_mode(), external_power));
            // A decoded voice message plays once; stopping or leaving it stops the
            // sound, and the end of the sound closes it.
            if (const auto& clip = aoraton.snapshot().voice_clip; clip != voice_message_playing) {
                if (clip) audio.play_pcm(*clip);
                else if (voice_message_playing) audio.stop();
                voice_message_playing = clip;
            } else if (voice_message_playing && audio.diagnostics().state != aura::AudioState::Playing &&
                       audio.diagnostics().state != aura::AudioState::Buffering) {
                voice_message_playing.reset();
                aoraton.close_attachment();
            }
            if (aoraton_account && weather.snapshot().has_saved_location &&
                weather.snapshot().saved_location.name != aoraton_city) {
                aoraton_city = weather.snapshot().saved_location.name;
                aoraton_account->set_location_city(aoraton_city);
            }
            companion.poll(elapsed_ms);
            // New keys from the PC start the assistant again.
            if (cloud_keys.generation() != assistant_keys) {
                assistant_keys = cloud_keys.generation();
                start_assistant();
            }
            if (assistant.turn) {
                if (companion.language() != assistant_language) {
                    assistant_language = companion.language();
                    const auto& strings = companion_strings(assistant_language);
                    assistant.turn->set_language(assistant_language);
                    assistant.turn->set_answer_copy(strings.action_question, strings.action_refused,
                                                    strings.empty_answer);
                    companion.set_speech_available(
                        true, assistant.speech->tts_available(aura::interface_language_code(assistant_language)));
                }
                // Ήχος: spoken answers on or off, and the chosen voice.
                assistant.turn->set_spoken_replies(companion.sound().spoken_replies);
                assistant.turn->set_voice(aura::assistant_voice_id(companion.sound().assistant_voice));
                assistant.turn->set_follow_up(companion.sound().continuous_conversation);
                assistant.turn->poll();
                companion.set_speaking(assistant.turn->speaking());
                turn_times.follow(controller.snapshot(),
                                  assistant.turn->speaking() && audio.diagnostics().state == aura::AudioState::Playing,
                                  esp_timer_get_time() / 1000);
                if (voice_preview.valid() &&
                    voice_preview.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    const auto spoken = voice_preview.get();
                    if (spoken.ok) audio.play_pcm(spoken.clip);
                }
                // Barge-in: the microphone listens while the answer is spoken (not
                // while it is paused, so a paused answer does not keep it open). It
                // opens once the voice is sounding: opening beside the speaker, it kept
                // the voice waiting 120 ms.
                const auto sounding = audio.diagnostics().state;
                companion.set_barge_monitor(assistant.turn->speaking() && companion.sound().barge_in &&
                                            sounding != aura::AudioState::Paused &&
                                            (companion.barge_monitoring() || sounding == aura::AudioState::Playing));
                if (companion.take_barge_in()) {
                    assistant.turn->stop_speaking();
                    audio.stop();
                    start_listening(true);
                }
            }
            aura::WeatherFetchResult weather_result;
            if (weather_backend.poll(weather_result)) {
                if (weather_result.ok) {
                    ESP_LOGI(kTag, "weather: code %d, %s, %.0f degrees", weather_result.data.weather_code,
                             weather_result.data.is_day ? "day" : "night",
                             static_cast<double>(weather_result.data.temperature_c));
                    weather.fetch_succeeded(weather_result.data);
                } else {
                    ESP_LOGW(kTag, "weather: %s", weather_result.error_code.c_str());
                    weather.fetch_failed(weather_result.error_code, weather_result.offline);
                }
            }
            std::optional<aura::FuelPriceData> fuel_prices;
            if (fuel_backend.poll(fuel_prices)) {
                if (fuel_prices) {
                    const auto& today = device_status.snapshot();
                    if (today.date_valid) {
                        fuel_prices->stale = aura::fuel_prices_stale(*fuel_prices, today.year, today.month, today.day);
                    }
                    ESP_LOGI(kTag, "fuel: week of %s, unleaded %d, diesel %d", fuel_prices->period_date.c_str(),
                             fuel_prices->unleaded95_millieur_per_litre, fuel_prices->diesel_millieur_per_litre);
                    fuel.fetch_succeeded(std::move(*fuel_prices));
                } else if (fuel.snapshot().state == aura::FuelPriceState::Ready) {
                    // The prices known stay, marked as old, and are asked for again sooner.
                    auto kept = fuel.snapshot().data;
                    kept.stale = true;
                    fuel.fetch_succeeded(std::move(kept));
                } else {
                    fuel.fetch_failed("FUEL_DATA_UNAVAILABLE");
                }
                ui.apply_fuel(fuel.snapshot());
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
            power_ms += elapsed_ms;
            storage_ms += elapsed_ms;
            // The microSD card's space (Κατάσταση συσκευής): a minute apart, since
            // a large FAT32 card can take a moment to count its free space.
            if (storage_ms >= 60000) {
                storage_ms = 0;
                aura::StorageStatus card;
                card.known = true;
                card.origin = aura::StatusOrigin::Live;
                card.present = companion.card_mounted() && companion.card_space(card.total_bytes, card.free_bytes);
                device_status.set_storage(card);
            }
            // The battery, every five seconds (the OTA rule and the status bar use it).
            if (board_power_ready && power_ms >= 5000) {
                power_ms = 0;
                const auto reading = board_power.read();
                external_power = reading && reading->external_power;
                if (reading && reading->percent) {
                    device_status.set_power(*reading->percent, reading->charging, aura::StatusOrigin::Live);
                } else {
                    device_status.set_power_unavailable();
                }
                // What else the power manager measures (Κατάσταση συσκευής).
                aura::BatteryDetails details;
                if (reading && reading->battery_present) {
                    details.known = true;
                    details.battery_mv = reading->battery_mv;
                    details.usb_mv = reading->external_power ? reading->vbus_mv : 0;
                    details.system_mv = reading->system_mv;
                    details.chip_temperature_known = true;
                    details.chip_celsius = reading->chip_celsius;
                    details.stage = reading->external_power ? reading->stage : aura::ChargeStage::Stopped;
                    details.rated_mah = aura_board::battery_rated_mah;
                    details.origin = aura::StatusOrigin::Live;
                }
                device_status.set_battery_details(details);
            }
            // A new network time goes to the board clock, in UTC.
            if (board_clock_ready && network.time_sync_count() != clock_written_syncs) {
                clock_written_syncs = network.time_sync_count();
                if (!board_clock.write_utc(static_cast<std::int64_t>(std::time(nullptr)))) {
                    ESP_LOGW(kTag, "Could not write the board clock");
                }
            }
            weather_refresh_ms = std::min<std::uint32_t>(weather_refresh_ms + elapsed_ms, 24U * 60U * 60U * 1000U);
            fuel_refresh_ms = std::min<std::uint32_t>(fuel_refresh_ms + elapsed_ms, 24U * 60U * 60U * 1000U);
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
                    if (online && fuel_refresh_ms >= aura::fuel_refresh_interval_ms(fuel.snapshot()) &&
                        fuel_backend.request()) {
                        fuel_refresh_ms = 0;
                    }
                }
                if (network.time_valid()) {
                    const std::time_t now_time = std::time(nullptr);
                    std::tm local_time{};
                    localtime_r(&now_time, &local_time);
                    device_status.set_clock(local_time.tm_hour, local_time.tm_min, aura::StatusOrigin::Live);
                    device_status.set_date(local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
                                           local_time.tm_wday, aura::StatusOrigin::Live);
                    companion.set_clock(local_time, now_time);
                }
            }
            // The fingers on the glass: two of them zoom an open photo.
            {
                std::int32_t finger_x[2]{};
                std::int32_t finger_y[2]{};
                const int fingers = aura_board_touch_points(finger_x, finger_y);
                aura::TouchPoint points[2];
                for (int finger = 0; finger < fingers; ++finger) {
                    points[finger] = {static_cast<int>(finger_x[finger]), static_cast<int>(finger_y[finger])};
                }
                ui.set_touch_points(points, static_cast<std::size_t>(fingers));
            }
            ticked = xTaskGetTickCount();
            ui.tick(elapsed_ms);
            ui.apply(controller.snapshot());
            ui.apply_status(device_status.snapshot());
            ui.apply_audio(audio.diagnostics());
            ui.apply_aoraton(aoraton.snapshot());
            ui.apply_weather(weather.snapshot());
            ui.apply_wifi(wifi.snapshot());
            companion.apply(ui);
            companion.confirm_image_after(static_cast<std::uint32_t>(esp_timer_get_time() / 1000));
            metrics.loop.add(static_cast<std::uint32_t>(esp_timer_get_time() - pass_started_us));
            metrics_ms += elapsed_ms;
            const std::uint32_t report_every_ms = esp_timer_get_time() < 120LL * 1000 * 1000 ? 10000U : 60000U;
            if (metrics_ms >= report_every_ms) {
                report_metrics(ui, presence_frames, metrics_ms, companion_strings(companion.language()));
                metrics_ms = 0;
            }
            aura_board_display_unlock();
        }
        // Confirmed power actions can block, restart or enter deep sleep. They
        // must never execute while the single LVGL/BSP display lock is held.
        companion.run_deferred_system_action();
        // New aoraton settings from the PC restart the account, outside the
        // display lock: stopping waits for a request in flight.
        if (aoraton_config_changed.exchange(false)) start_aoraton();
        // A dark screen lets the loop slow down and the processor drop its clock.
        const aura::DisplayMode mode = companion.display_mode();
        if (full_speed && aura::needs_full_speed(mode) != at_full_speed) {
            at_full_speed = !at_full_speed;
            if (at_full_speed) esp_pm_lock_acquire(full_speed);
            else esp_pm_lock_release(full_speed);
        }
        // While the screen is lit, no longer than Synapse's rest before its
        // next frame (the rest began at the UI's tick: the pass since counts),
        // and woken as soon as its worker has drawn one.
        const std::uint32_t interval = aura::host_loop_interval_ms(mode);
        std::uint32_t sleep_ms = interval;
        if (aura::needs_full_speed(mode)) {
            const std::uint32_t due = ui.next_tick_ms();
            const auto spent = static_cast<std::uint32_t>((xTaskGetTickCount() - ticked) * portTICK_PERIOD_MS);
            sleep_ms = std::min(interval, due > spent ? due - spent : 0U);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep_ms));
    }
}

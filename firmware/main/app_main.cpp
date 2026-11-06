#include "aura/assistant_companion.hpp"
#include "aura/assistant_state.hpp"
#include "aura/aoraton.hpp"
#include "aura/aoraton_device_link.hpp"
#include "aura/aoraton_real.hpp"
#include "aura/display_power.hpp"
#include "aura/esp32_aoraton_account.hpp"
#include "aura/aura_frame_worker.hpp"
#include "aura/aura_theme.hpp"
#include "aura/aura_ui.hpp"
#include "aura/cloud_assistant.hpp"
#include "aura/companion_strings.hpp"
#include "aura/voice_turn.hpp"
#include "aura/runtime_metrics.hpp"
#include "aura_board_chips.hpp"
#include "aura_board_profile.h"
#include "aura_display.h"
#include "esp32_audio_backend.hpp"
#include "esp32_cloud_keys.hpp"
#include "esp32_https_client.hpp"
#include "esp32_companion.hpp"
#include "esp32_network.hpp"
#include "esp32_weather_backend.hpp"

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_pthread.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <memory>
#include <sys/time.h>
#include <string>
#include <utility>

namespace {
constexpr const char* kTag = "aura";

// Synapse draws its frames on the second core, below LVGL's priority, so the UI
// never waits for one (the LVGL task floats; Wi-Fi, BLE and this task use core 0).
// The first boot on a board must report what the simulator cannot: frame and
// loop times, Synapse's cost and the free memory, on the serial console every
// 10 s for two minutes and every minute after. Touched under the display lock
// only (LVGL's render events fire in its task while it holds the lock).
aura::RuntimeMetrics metrics;
std::int64_t render_started_us = 0;

void on_render_start(lv_event_t*) { render_started_us = esp_timer_get_time(); }

void on_render_ready(lv_event_t*) {
    if (render_started_us > 0) metrics.frame.add(static_cast<std::uint32_t>(esp_timer_get_time() - render_started_us));
}

void report_metrics(const aura::AuraUi& ui, std::uint64_t& presence_frames, std::uint32_t window_ms) {
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
    metrics.frame.reset();
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

// The cloud requests (Groq, Azure) block for seconds on one worker thread with
// room for a TLS handshake on its stack.
std::unique_ptr<aura::CloudWorker> start_cloud_worker() {
    esp_pthread_cfg_t worker = esp_pthread_get_default_config();
    worker.thread_name = "aura_cloud";
    worker.stack_size = 12288;
    worker.prio = 4;
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

const aura::l10n::CompanionStrings& companion_strings(aura::InterfaceLanguage language) {
    return language == aura::InterfaceLanguage::English ? aura::l10n::companion_en_us() :
                                                          aura::l10n::companion_el_gr();
}

void start_frame_worker() {
    esp_pthread_cfg_t worker = esp_pthread_get_default_config();
    worker.thread_name = "synapse";
    worker.pin_to_core = 1;
    worker.prio = 2;
    worker.stack_size = 4096;
    esp_pthread_set_cfg(&worker);
    aura::ui::FrameWorker::start_shared();
    const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&defaults);
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

    // As bsp_display_start(), but LVGL draws bands tall enough for the header's glass.
    lv_display_t* display = aura_board_display_start(aura::ui::tokens::DEVICE_DRAW_ROWS);
    if (display == nullptr) {
        ESP_LOGE(kTag, "BSP display initialization failed");
        return;
    }

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
    static aura::AoratonDeviceLink aoraton_link([](const aura::AoratonLinkConfig& config) {
        // The helper sends the settings on every run: only a change restarts the account.
        aura::AoratonLinkConfig stored;
        if (aura::firmware::esp32_aoraton_load_config(stored) && stored.base_url == config.base_url &&
            stored.anon_key == config.anon_key && stored.inco_enabled == config.inco_enabled)
            return true;
        if (!aura::firmware::esp32_aoraton_save_config(config)) return false;
        aoraton_config_changed = true;
        return true;
    });
    static std::unique_ptr<aura::firmware::Esp32AoratonAccount> aoraton_account;
    static aura::WeatherModel weather;
    static aura::firmware::Esp32AudioBackend audio;
    static aura::firmware::Esp32WeatherBackend weather_backend;
    static aura::firmware::Esp32Network network;
    static aura::WifiModel wifi;
    static aura::AuraUi ui;
    static aura::firmware::Esp32Companion companion(audio, device_status);
    static aura::firmware::Esp32HttpsClient cloud_https;
    static aura::firmware::Esp32CloudKeyStore cloud_keys;
    static std::unique_ptr<aura::CloudWorker> cloud_worker = start_cloud_worker();
    static CloudAssistant assistant;
    static aura::InterfaceLanguage assistant_language = aura::InterfaceLanguage::Greek;
    companion.set_cloud_key_sink(&cloud_keys);
    companion.set_aoraton_link(&aoraton_link);
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

    start_frame_worker();

    // LVGL callbacks run in the BSP's LVGL task while it holds the display lock;
    // the loop below touches the same models only under that lock as well.
    if (bsp_display_lock(0)) {
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
        companion.install(callbacks, &aoraton);
        callbacks.on_aoraton_pair_cancel = [] { aoraton.cancel_pairing(); };
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
        // The assistant's spoken question: the companion records it with the
        // microphone it shares with notes; the turn transcribes, asks and speaks.
        callbacks.on_ask = [] {
            if (!assistant.turn) {
                controller.dispatch({aura::EventType::ProviderUnavailable, "PROVIDER_NOT_CONFIGURED", 0});
                return;
            }
            assistant.turn->begin_listening();
            if (!companion.begin_question()) {
                assistant.turn->cancel();
                controller.dispatch({aura::EventType::Fail, "MICROPHONE_UNAVAILABLE", 0});
            }
        };
        callbacks.on_end_listening = [] { companion.end_question(); };
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
        companion.set_question_sink([](std::vector<std::uint8_t> wav) {
            if (assistant.turn) assistant.turn->finish_listening(std::move(wav));
        });
        ui.set_language(companion.language());
        ui.set_appearance(companion.appearance());
        ui.set_aoraton_sign_in_relay(true);
        ui.create(display, std::move(callbacks));
        companion.apply(ui);
        ui.apply(controller.snapshot());
        ui.apply_status(device_status.snapshot());
        ui.apply_audio(audio.diagnostics());
        ui.apply_aoraton(aoraton.snapshot());
        ui.apply_weather(weather.snapshot());
        ui.apply_wifi(wifi.snapshot());
        lv_display_add_event_cb(display, on_render_start, LV_EVENT_RENDER_START, nullptr);
        lv_display_add_event_cb(display, on_render_ready, LV_EVENT_RENDER_READY, nullptr);
        ESP_LOGI(kTag, "UI ready %lu ms after boot", static_cast<unsigned long>(esp_timer_get_time() / 1000));
        bsp_display_unlock();
    }

    // Starts, or restarts with new keys, the assistant. Under the display lock.
    const auto start_assistant = [] {
        if (assistant.turn) assistant.turn->cancel();
        assistant.turn.reset();
        assistant.speech.reset();
        assistant.provider.reset();
        auto keys = cloud_keys.load();
        if (keys.groq_api_key.empty() || !cloud_worker) {
            companion.set_speech_available(false, false);
            ESP_LOGI(kTag, "Assistant: no cloud keys yet (send them from the PC over the file transfer session)");
            return;
        }
        assistant.provider = std::make_unique<aura::CloudAssistantProvider>(cloud_https, *cloud_worker,
                                                                            keys.groq_api_key);
        assistant.speech = std::make_unique<aura::CloudSpeechService>(cloud_https, *cloud_worker, keys);
        aura::wipe_cloud_keys(keys);
        assistant.turn = std::make_unique<aura::VoiceAssistantTurn>(controller, *assistant.speech,
                                                                    *assistant.provider, audio);
        assistant_language = companion.language();
        const auto& strings = companion_strings(assistant_language);
        assistant.turn->set_language(assistant_language);
        assistant.turn->set_answer_copy(strings.action_question, strings.action_refused, strings.empty_answer);
        assistant.turn->set_context([](const std::string& question) {
            aura::DeviceContextInput input;
            input.status = &device_status.snapshot();
            input.weather = &weather.snapshot();
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
    if (bsp_display_lock(0)) {
        start_assistant();
        bsp_display_unlock();
    }

    // (Re)starts the account with the project settings the PC sent last.
    const auto start_aoraton = [] {
        aoraton_account.reset();
        aura::AoratonLinkConfig config;
        if (!aura::firmware::esp32_aoraton_load_config(config)) return;
        auto account = std::make_unique<aura::firmware::Esp32AoratonAccount>(aoraton_mailbox, config, aoraton_link);
        if (account->start()) aoraton_account = std::move(account);
    };
    start_aoraton();
    ESP_LOGI(kTag, "Aoraton: %s", aoraton_account ? "account ready" : "waiting for the PC's settings");
    bool external_power = false;
    std::string aoraton_city;
    std::shared_ptr<const aura::PcmClip> voice_message_playing;

    esp_pm_lock_handle_t full_speed = start_power_management();
    std::uint32_t metrics_ms = 0;
    std::uint64_t presence_frames = 0;
    std::uint32_t clock_written_syncs = 0;
    std::uint32_t power_ms = 5000;
    bool at_full_speed = full_speed != nullptr;
    TickType_t previous = xTaskGetTickCount();
    std::uint32_t unapplied_ms = 0;
    std::uint32_t status_ms = 1000;
    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        unapplied_ms += static_cast<std::uint32_t>((now - previous) * portTICK_PERIOD_MS);
        previous = now;
        if (bsp_display_lock(pdMS_TO_TICKS(25))) {
            const std::int64_t pass_started_us = esp_timer_get_time();
            const std::uint32_t elapsed_ms = std::min<std::uint32_t>(unapplied_ms, 1000);
            unapplied_ms = 0;
            audio.tick(elapsed_ms);
            aoraton.tick(elapsed_ms);
            // Aoraton: settings and requests from the PC helper, then the
            // hybrid sync (realtime while lit or on USB, a 5-minute check
            // otherwise; the owner's choice).
            if (aoraton_link.take_sign_in_request()) aoraton.begin_pairing();
            std::string invite;
            while (aoraton_link.take_invite(invite)) {
                aoraton.add_invite_key(invite);
                std::fill(invite.begin(), invite.end(), '\0');
                invite.clear();
            }
            aoraton_link.publish(aoraton_account != nullptr, aoraton.snapshot().pairing, aoraton.snapshot().last_error);
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
                assistant.turn->poll();
            }
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
            power_ms += elapsed_ms;
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
            }
            // A new network time goes to the board clock, in UTC.
            if (board_clock_ready && network.time_sync_count() != clock_written_syncs) {
                clock_written_syncs = network.time_sync_count();
                if (!board_clock.write_utc(static_cast<std::int64_t>(std::time(nullptr)))) {
                    ESP_LOGW(kTag, "Could not write the board clock");
                }
            }
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
                    companion.set_clock(local_time, now_time);
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
            metrics.loop.add(static_cast<std::uint32_t>(esp_timer_get_time() - pass_started_us));
            metrics_ms += elapsed_ms;
            const std::uint32_t report_every_ms = esp_timer_get_time() < 120LL * 1000 * 1000 ? 10000U : 60000U;
            if (metrics_ms >= report_every_ms) {
                report_metrics(ui, presence_frames, metrics_ms);
                metrics_ms = 0;
            }
            bsp_display_unlock();
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
        vTaskDelay(pdMS_TO_TICKS(aura::host_loop_interval_ms(mode)));
    }
}

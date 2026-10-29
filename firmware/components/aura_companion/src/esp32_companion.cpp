#include "esp32_companion.hpp"

#include "aura/audio_service.hpp"
#include "aura/text_util.hpp"
#include "aura_board_profile.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include <cstdio>
#include <new>
#include <string>
#include <utility>

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura-companion";
constexpr std::uint64_t kLongPressMs = 600;

}  // namespace

Esp32Companion::Esp32Companion(IAudioService& audio, DeviceStatusModel& status)
    : audio_(audio), status_(status), ota_("0.0.0") {}

std::uint64_t Esp32Companion::now_ms() const {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

std::int64_t Esp32Companion::now_minute() const {
    return clock_.snapshot().clock_valid ? clock_.snapshot().now_minute : 0;
}

void Esp32Companion::start() {
    const bool storage_mounted = store_.mount();
    if (const auto data = store_.read("clock.txt"); !data.empty()) clock_.restore(data);
    if (const auto data = store_.read("notes.txt"); !data.empty()) notes_.restore(data);
    if (const auto data = store_.read("organizer.txt"); !data.empty()) organizer_.restore(data);
    if (const auto data = store_.read("sound.txt"); !data.empty()) sound_ = parse_sound_settings(data);
    if (const auto data = store_.read("display.txt"); !data.empty()) {
        display_.set_settings(parse_display_settings(data));
    }
    if (const auto data = store_.read("language.txt"); data.rfind("en", 0) == 0) {
        language_ = InterfaceLanguage::English;
    }
    if (const auto data = store_.read("app-layout.txt"); !data.empty() && !launcher_layout_.restore(data)) {
        ESP_LOGW(kTag, "ignored an invalid app layout store");
    }
    audio_.set_volume(sound_.volume);
    audio_.set_muted(sound_.muted);
    saved_clock_ = clock_.snapshot().sequence;
    saved_clock_text_ = clock_.serialize();
    saved_notes_ = notes_.snapshot().sequence;
    saved_notes_text_ = notes_.serialize();
    saved_organizer_ = organizer_.snapshot().sequence;
    saved_organizer_text_ = organizer_.serialize();
    saved_sound_text_ = serialize_sound_settings(sound_);
    saved_display_text_ = serialize_display_settings(display_.snapshot().settings);
    saved_language_text_ = std::string(interface_language_code(language_)) + "\n";
    saved_layout_text_ = launcher_layout_.serialize();

    if (storage_mounted) {
        storage_jobs_ = xQueueCreate(8, sizeof(StoreJob*));
        storage_results_ = xQueueCreate(8, sizeof(StoreJob*));
        if (!storage_jobs_ || !storage_results_ ||
            xTaskCreate(storage_task_entry, "aura_store", 6144, this, 3, &storage_task_handle_) != pdPASS) {
            ESP_LOGE(kTag, "storage worker unavailable; persistent writes and voice notes disabled");
            if (storage_jobs_) vQueueDelete(storage_jobs_);
            if (storage_results_) vQueueDelete(storage_results_);
            storage_jobs_ = nullptr;
            storage_results_ = nullptr;
            storage_task_handle_ = nullptr;
        }
    }

    updater_.initialize();
    ota_ = OtaUpdateModel(updater_.running_version());
    ota_.set_source(updater_.configured(), false);

    capabilities_.imu = motion_.start();
    display_.set_imu_available(capabilities_.imu);
    capabilities_.phone_link = phone_link_.start();
    phone_.set_link(capabilities_.phone_link ? PhoneLinkState::NotPaired : PhoneLinkState::Unavailable,
                    PhonePlatform::Unknown, {});
    capabilities_.microphone = storage_task_handle_ && microphone_.initialize();
    // Recognition, spoken replies and the wake word wait for the AI gateway
    // and a trained "Aura" model.
    capabilities_.speech_recognition = false;
    capabilities_.speech_output = false;
    capabilities_.wake_word = false;
    // The panel dims itself (no veil from the UI), and the side button is the
    // only way back from power off.
    capabilities_.panel_brightness = true;
    capabilities_.power_button = true;
    capabilities_dirty_ = true;

    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << aura_board::Pins::user_button;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&button);
}

void Esp32Companion::install(AuraUiCallbacks& callbacks) {
    callbacks.on_language_changed = [this](InterfaceLanguage language) {
        language_ = language;
    };
    callbacks.on_launcher_app_move = [this](std::size_t from, std::size_t to) {
        launcher_layout_.move(from, to);
    };
    callbacks.on_timer_start = [this](std::uint32_t seconds, const std::string& label) {
        return clock_.start_timer(seconds, label) == ClockResult::Ok;
    };
    callbacks.on_timer_pause = [this](std::uint32_t id, bool paused) { clock_.pause_timer(id, paused); };
    callbacks.on_timer_cancel = [this](std::uint32_t id) { clock_.cancel_timer(id); };
    callbacks.on_alarm_add = [this](int hour, int minute, bool daily) {
        return clock_.add_alarm(hour, minute, daily, {}) == ClockResult::Ok;
    };
    callbacks.on_alarm_enable = [this](std::uint32_t id, bool enabled) { clock_.set_alarm_enabled(id, enabled); };
    callbacks.on_alarm_remove = [this](std::uint32_t id) { clock_.remove_alarm(id); };
    callbacks.on_reminder_add = [this](std::int64_t due, const std::string& text) {
        return clock_.add_reminder_at(due, text) == ClockResult::Ok;
    };
    callbacks.on_reminder_remove = [this](std::uint32_t id) { clock_.remove_reminder(id); };
    callbacks.on_alert_dismiss = [this] {
        clock_.dismiss_alert();
        audio_.stop();
    };
    callbacks.on_alert_snooze = [this](int minutes) {
        clock_.snooze_alert(minutes);
        audio_.stop();
    };
    callbacks.on_note_add = [this](const std::string& text) {
        return notes_.add_text(text, now_minute()) == NoteResult::Ok;
    };
    callbacks.on_note_record = [this] {
        begin_note();
        return recording_note_;
    };
    callbacks.on_note_record_stop = [this] { finish_note(); };
    callbacks.on_note_record_cancel = [this] {
        microphone_.stop();
        recorder_.cancel();
        recording_note_ = false;
        voice_.state = VoiceCaptureState::Idle;
        ++voice_.sequence;
    };
    callbacks.on_note_play = [this](std::uint32_t id) {
        if (const auto* note = notes_.find(id); note && !note->audio_file.empty()) {
            audio_.play_wav(store_.note_audio_path(note->audio_file));
        }
    };
    callbacks.on_note_delete = [this](std::uint32_t id) {
        std::string file;
        if (notes_.remove(id, &file) == NoteResult::Ok && !file.empty()) {
            auto* job = new (std::nothrow) StoreJob;
            if (job) {
                job->kind = StoreJobKind::DeleteVoice;
                job->name = std::move(file);
                enqueue(job);
            }
        }
    };
    callbacks.on_organizer_event_add = [this](const std::string& title, std::int64_t start,
                                               std::uint32_t duration) {
        organizer_.prune_events_before(now_minute());
        return organizer_.add_event(title, start, duration) == OrganizerResult::Ok;
    };
    callbacks.on_organizer_task_add = [this](const std::string& text) {
        return organizer_.add_task(text, now_minute()) == OrganizerResult::Ok;
    };
    callbacks.on_organizer_shopping_add = [this](const std::string& text) {
        return organizer_.add_shopping(text) == OrganizerResult::Ok;
    };
    callbacks.on_organizer_toggle = [this](OrganizerKind kind, std::uint32_t id) {
        organizer_.toggle(kind, id);
    };
    callbacks.on_organizer_remove = [this](OrganizerKind kind, std::uint32_t id) {
        organizer_.remove(kind, id);
    };
    callbacks.on_display_settings = [this](const DisplaySettings& settings) { display_.set_settings(settings); };
    callbacks.on_sound_settings = [this](const SoundSettings& settings) {
        sound_ = settings;
        audio_.set_volume(sound_.volume);
        audio_.set_muted(sound_.muted);
    };
    callbacks.on_sound_preview = [this] {
        if (!sound_.muted) audio_.play_pcm(make_message_chime());
    };
    callbacks.on_wake = [this] { display_.user_activity(now_ms()); };
    callbacks.on_phone_call = [this](std::uint32_t uid, bool accept) {
        phone_link_.perform_action(uid, accept);
        phone_.remove(uid);
    };
    callbacks.on_phone_dismiss = [this](std::uint32_t uid) {
        phone_link_.perform_action(uid, false);
        phone_.remove(uid);
    };
    callbacks.on_phone_pair = [this] { phone_link_.set_pairable(true); };
    callbacks.on_phone_forget = [this] { phone_link_.forget(); };
    callbacks.on_update_check = [this] {
        if (ota_.begin_check() && !updater_.request_check()) ota_.check_failed("OTA_BUSY");
    };
    callbacks.on_update_install = [this] {
        const auto& status = status_.snapshot();
        if (ota_.can_install(status.connection_known && status.online, status.battery_percent, status.power_known,
                             status.charging) != UpdateBlock::None || !ota_.snapshot().available) {
            return;
        }
        const auto manifest = *ota_.snapshot().available;
        if (ota_.begin_download() && !updater_.request_install(manifest)) ota_.fail("OTA_BUSY");
    };
    callbacks.on_update_restart = [this] { updater_.restart(); };
    callbacks.on_system_action = [this](SystemAction action) {
        pending_system_action_.store(static_cast<int>(action));
    };
}

void Esp32Companion::begin_note() {
    if (recording_note_ || voice_.state != VoiceCaptureState::Idle ||
        !capabilities_.microphone || !microphone_.start()) return;
    audio_.stop();
    // A note is dictated: it may pause to think and may last half a minute.
    recorder_.start(VoiceRecorder::Mode::Dictation);
    recording_note_ = true;
    voice_.state = VoiceCaptureState::Recording;
    voice_.purpose = VoicePurpose::Note;
    voice_.error_code.clear();
    ++voice_.sequence;
}

void Esp32Companion::finish_note() {
    if (!recording_note_) return;
    if (!microphone_.stop()) {
        recorder_.cancel();
        recording_note_ = false;
        voice_.state = VoiceCaptureState::Idle;
        voice_.error_code = "MICROPHONE_STOP_TIMEOUT";
        ++voice_.sequence;
        return;
    }
    samples_.clear();
    microphone_.drain(samples_);
    if (!samples_.empty()) recorder_.append(samples_.data(), samples_.size());
    const auto elapsed = recorder_.elapsed_ms();
    auto wav = recorder_.finish_wav();
    recording_note_ = false;
    // The random part keeps the name unique when the clock is unknown or the
    // counter restarted with the device: a new note never overwrites an old one.
    const auto name = voice_note_file_name(now_minute(), ++note_counter_, esp_random());
    auto* job = new (std::nothrow) StoreJob;
    if (!job) {
        voice_.error_code = "NOTE_NOT_SAVED";
        voice_.state = VoiceCaptureState::Idle;
        ++voice_.sequence;
        return;
    }
    job->kind = StoreJobKind::Voice;
    job->name = name;
    job->audio = std::move(wav);
    job->duration_ms = elapsed;
    job->minute = now_minute();
    if (!enqueue(job)) {
        voice_.error_code = "STORAGE_BUSY";
        voice_.state = VoiceCaptureState::Idle;
        ++voice_.sequence;
        return;
    }
    voice_.state = VoiceCaptureState::Processing;
    ++voice_.sequence;
}

void Esp32Companion::poll(std::uint32_t elapsed_ms) {
    const auto now = now_ms();
    poll_storage_results();
    clock_.tick(elapsed_ms);

    imu_samples_.clear();
    motion_.drain(imu_samples_);
    for (const auto& sample : imu_samples_) {
        const auto gesture = imu_.push(sample);
        if (gesture != ImuGesture::None) display_.gesture(gesture, now);
    }

    PhoneEvent phone_event;
    while (phone_link_.poll(phone_event)) {
        switch (phone_event.kind) {
            case PhoneEvent::Kind::Link: phone_.set_link(phone_event.link, phone_event.platform, {}); break;
            case PhoneEvent::Kind::Upsert:
                phone_event.notification.minute = now_minute();
                phone_.upsert(std::move(phone_event.notification));
                break;
            case PhoneEvent::Kind::Remove: phone_.remove(phone_event.uid); break;
        }
    }

    OtaEvent ota_event;
    while (updater_.poll(ota_event)) {
        switch (ota_event.kind) {
            case OtaEvent::Kind::CheckDone: ota_.check_finished(ota_event.manifest); break;
            case OtaEvent::Kind::CheckFailed: ota_.check_failed(ota_event.error_code); break;
            case OtaEvent::Kind::Progress: ota_.set_progress(ota_event.progress); break;
            case OtaEvent::Kind::Verifying: ota_.begin_verify(); break;
            case OtaEvent::Kind::Ready: ota_.ready_to_restart(); break;
            case OtaEvent::Kind::Failed: ota_.fail(ota_event.error_code); break;
        }
    }

    if (recording_note_) {
        samples_.clear();
        microphone_.drain(samples_);
        if (!samples_.empty()) recorder_.append(samples_.data(), samples_.size());
        voice_.level = recorder_.level();
        voice_.elapsed_ms = recorder_.elapsed_ms();
        ++voice_.sequence;
        if (recorder_.should_stop()) finish_note();
    }

    // The side button: a press wakes or sleeps the screen; holding it records a note.
    const bool down = gpio_get_level(static_cast<gpio_num_t>(aura_board::Pins::user_button)) == 0;
    if (down && !button_down_) button_since_ms_ = now;
    if (down && !recording_note_ && now - button_since_ms_ >= kLongPressMs && button_since_ms_ != 0) {
        display_.user_activity(now);
        begin_note();
        button_since_ms_ = 0;
    }
    if (!down && button_down_) {
        if (recording_note_) {
            finish_note();
        } else if (button_since_ms_ != 0) {
            if (display_.snapshot().mode == DisplayMode::Active) display_.sleep_now(now);
            else display_.user_activity(now);
        }
    }
    button_down_ = down;

    const bool busy = clock_.snapshot().alert || recording_note_ || phone_.snapshot().incoming_call ||
                      audio_.diagnostics().state == AudioState::Playing ||
                      ota_.snapshot().state == UpdateState::Downloading;
    display_.hold_awake(busy, now);
    display_.tick(now);
    const int brightness = display_.snapshot().brightness_percent;
    if (brightness != applied_brightness_) {
        applied_brightness_ = brightness;
        bsp_display_brightness_set(brightness);
    }
    // The alert rings until it is handled, a little louder each pass and
    // through mute; face down silences it.
    const auto audio = audio_.diagnostics();
    if (const auto* pass = alarm_ringer_.next(clock_.snapshot().alert.has_value(),
                                              display_.snapshot().muted_by_flip,
                                              audio.state == AudioState::Playing, now)) {
        audio_.play_alert(*pass);
    }
    if (display_.snapshot().muted_by_flip && audio.state == AudioState::Playing) audio_.stop();
    // A soft chime for a phone notification that was not there before.
    if (phone_.snapshot().sequence != chime_phone_sequence_) {
        chime_phone_sequence_ = phone_.snapshot().sequence;
        std::vector<std::string> ids;
        for (const auto& item : phone_.snapshot().notifications) ids.push_back("p" + std::to_string(item.uid));
        const bool quiet = display_.snapshot().muted_by_flip || audio.muted || audio.state == AudioState::Playing ||
                           clock_.snapshot().alert || recording_note_;
        if (message_chime_.observe(std::move(ids), now, quiet || !sound_.message_chime)) {
            audio_.play_pcm(make_message_chime());
        }
    }
    save();
}

bool Esp32Companion::enqueue(StoreJob* job) {
    if (!job) return false;
    if (!storage_accepting_.load() || !storage_jobs_ || xQueueSend(storage_jobs_, &job, 0) != pdTRUE) {
        ESP_LOGW(kTag, "storage queue busy; operation deferred");
        delete job;
        return false;
    }
    return true;
}

void Esp32Companion::storage_task_entry(void* context) {
    static_cast<Esp32Companion*>(context)->storage_task();
}

void Esp32Companion::storage_task() {
    for (;;) {
        StoreJob* job = nullptr;
        if (xQueueReceive(storage_jobs_, &job, portMAX_DELAY) != pdTRUE || !job) continue;
        storage_busy_ = true;
        switch (job->kind) {
            case StoreJobKind::Clock:
            case StoreJobKind::Notes:
            case StoreJobKind::Organizer:
            case StoreJobKind::Sound:
            case StoreJobKind::Display:
            case StoreJobKind::Language:
            case StoreJobKind::Layout:
                job->success = store_.write(job->name.c_str(), job->text);
                break;
            case StoreJobKind::Voice:
                job->success = store_.write_note_audio(job->name, job->audio);
                job->audio.clear();
                break;
            case StoreJobKind::DeleteVoice:
                store_.remove_note_audio(job->name);
                job->success = true;
                break;
        }
        storage_busy_ = false;
        if (xQueueSend(storage_results_, &job, portMAX_DELAY) != pdTRUE) delete job;
    }
}

void Esp32Companion::poll_storage_results() {
    if (!storage_results_) return;
    StoreJob* job = nullptr;
    while (xQueueReceive(storage_results_, &job, 0) == pdTRUE) {
        if (!job) continue;
        switch (job->kind) {
            case StoreJobKind::Clock:
                clock_save_pending_ = false;
                if (job->success) {
                    saved_clock_ = job->sequence;
                    saved_clock_text_ = job->text;
                }
                break;
            case StoreJobKind::Notes:
                notes_save_pending_ = false;
                if (job->success) {
                    saved_notes_ = job->sequence;
                    saved_notes_text_ = job->text;
                }
                break;
            case StoreJobKind::Organizer:
                organizer_save_pending_ = false;
                if (job->success) {
                    saved_organizer_ = job->sequence;
                    saved_organizer_text_ = job->text;
                }
                break;
            // A failed write is tried again on the next poll.
            case StoreJobKind::Sound:
                sound_save_pending_ = false;
                if (job->success) saved_sound_text_ = job->text;
                else sound_pending_ = true;
                break;
            case StoreJobKind::Display:
                display_save_pending_ = false;
                if (job->success) saved_display_text_ = job->text;
                else saved_display_sequence_ = 0;
                break;
            case StoreJobKind::Language:
                language_save_pending_ = false;
                if (job->success) saved_language_text_ = job->text;
                break;
            case StoreJobKind::Layout:
                layout_save_pending_ = false;
                if (job->success) saved_layout_text_ = job->text;
                else saved_layout_sequence_ = 0;
                break;
            case StoreJobKind::Voice: {
                std::uint32_t id = 0;
                if (job->success && notes_.add_voice(job->name, job->duration_ms, job->minute, &id) == NoteResult::Ok) {
                    // No recognizer on the device yet: retain the audio and mark
                    // only the optional transcript as unavailable.
                    notes_.set_transcript_failed(id);
                    voice_.error_code.clear();
                } else {
                    voice_.error_code = job->success ? "NOTES_FULL" : "NOTE_NOT_SAVED";
                    if (job->success) {
                        auto* cleanup = new (std::nothrow) StoreJob;
                        if (cleanup) {
                            cleanup->kind = StoreJobKind::DeleteVoice;
                            cleanup->name = job->name;
                            enqueue(cleanup);
                        }
                    }
                }
                voice_.state = VoiceCaptureState::Idle;
                ++voice_.sequence;
                break;
            }
            case StoreJobKind::DeleteVoice: break;
        }
        delete job;
        job = nullptr;
    }
}

// Running timers bump the clock sequence every second. Serialization remains
// on the owner thread, while all blocking FAT writes run on the bounded worker.
void Esp32Companion::save() {
    if (!storage_jobs_) return;
    const auto queue_text = [this](StoreJobKind kind, const char* name, std::string data,
                                   std::uint64_t sequence, bool& pending) {
        auto* job = new (std::nothrow) StoreJob;
        if (!job) return;
        job->kind = kind;
        job->name = name;
        job->text = std::move(data);
        job->sequence = sequence;
        if (enqueue(job)) pending = true;
    };

    if (!clock_save_pending_ && clock_.snapshot().sequence != saved_clock_) {
        const auto sequence = clock_.snapshot().sequence;
        auto data = clock_.serialize();
        if (data == saved_clock_text_) saved_clock_ = sequence;
        else queue_text(StoreJobKind::Clock, "clock.txt", std::move(data), sequence, clock_save_pending_);
    }
    if (!notes_save_pending_ && notes_.snapshot().sequence != saved_notes_) {
        const auto sequence = notes_.snapshot().sequence;
        auto data = notes_.serialize();
        if (data == saved_notes_text_) saved_notes_ = sequence;
        else queue_text(StoreJobKind::Notes, "notes.txt", std::move(data), sequence, notes_save_pending_);
    }
    if (!organizer_save_pending_ && organizer_.snapshot().sequence != saved_organizer_) {
        const auto sequence = organizer_.snapshot().sequence;
        auto data = organizer_.serialize();
        if (data == saved_organizer_text_) saved_organizer_ = sequence;
        else queue_text(StoreJobKind::Organizer, "organizer.txt", std::move(data), sequence,
                        organizer_save_pending_);
    }
    // The volume slider and the mute switch change only the audio service:
    // what is stored follows it. Values are compared every poll; text is
    // built only after a change, not for every frame.
    sound_ = current_sound_settings(sound_, audio_.diagnostics());
    if (sound_ != seen_sound_) {
        seen_sound_ = sound_;
        sound_pending_ = true;
    }
    if (sound_pending_ && !sound_save_pending_) {
        auto sound_text = serialize_sound_settings(sound_);
        if (sound_text == saved_sound_text_) {
            sound_pending_ = false;
        } else if (sound_settle_.settled(sound_text, now_ms())) {
            queue_text(StoreJobKind::Sound, "sound.txt", std::move(sound_text), 0, sound_save_pending_);
            sound_pending_ = !sound_save_pending_;
        }
    }
    if (!display_save_pending_ && display_.snapshot().sequence != saved_display_sequence_) {
        saved_display_sequence_ = display_.snapshot().sequence;
        auto display_text = serialize_display_settings(display_.snapshot().settings);
        if (display_text != saved_display_text_) {
            queue_text(StoreJobKind::Display, "display.txt", std::move(display_text), 0, display_save_pending_);
            if (!display_save_pending_) saved_display_sequence_ = 0;
        }
    }
    if (!language_save_pending_ && saved_language_text_.compare(0, 2, interface_language_code(language_)) != 0) {
        queue_text(StoreJobKind::Language, "language.txt",
                   std::string(interface_language_code(language_)) + "\n", 0, language_save_pending_);
    }
    if (!layout_save_pending_ && launcher_layout_.snapshot().sequence != saved_layout_sequence_) {
        saved_layout_sequence_ = launcher_layout_.snapshot().sequence;
        auto layout_text = launcher_layout_.serialize();
        if (layout_text != saved_layout_text_) {
            queue_text(StoreJobKind::Layout, "app-layout.txt", std::move(layout_text), 0, layout_save_pending_);
            if (!layout_save_pending_) saved_layout_sequence_ = 0;
        }
    }
}

void Esp32Companion::apply(AuraUi& ui) {
    if (capabilities_dirty_) {
        capabilities_dirty_ = false;
        ui.set_capabilities(capabilities_);
    }
    ui.apply_display(display_.snapshot());
    ui.apply_clock(clock_.snapshot());
    ui.apply_notes(notes_.snapshot());
    ui.apply_organizer(organizer_.snapshot());
    ui.apply_voice(voice_);
    ui.apply_phone(phone_.snapshot());
    ui.apply_update(ota_.snapshot());
    ui.apply_sound(sound_);
    ui.apply_launcher_layout(launcher_layout_.snapshot());
}

void Esp32Companion::set_clock(const std::tm& local) {
    LocalDateTime now;
    now.year = local.tm_year + 1900;
    now.month = local.tm_mon + 1;
    now.day = local.tm_mday;
    now.hour = local.tm_hour;
    now.minute = local.tm_min;
    now.second = local.tm_sec;
    now.weekday = local.tm_wday;
    clock_.set_clock(now);
    display_.set_minute(minute_index(now));
}

void Esp32Companion::confirm_image_after(std::uint32_t uptime_ms) {
    if (image_confirmed_ || uptime_ms < 30000) return;
    image_confirmed_ = true;
    Esp32Ota::confirm_running_image();
}

void Esp32Companion::run_deferred_system_action() {
    const int raw = pending_system_action_.exchange(-1);
    if (raw < 0) return;
    const auto action = static_cast<SystemAction>(raw);
    storage_accepting_ = false;
    bool storage_flushed = true;
    if (storage_jobs_) {
        storage_flushed = false;
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (uxQueueMessagesWaiting(storage_jobs_) == 0 && !storage_busy_.load()) {
                storage_flushed = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (!storage_flushed) ESP_LOGW(kTag, "system action continuing after storage flush timeout");
    microphone_.stop();
    audio_.stop();

    if (action == SystemAction::FactoryReset) {
        store_.factory_reset();
        const auto erased = nvs_flash_erase();
        if (erased != ESP_OK) ESP_LOGE(kTag, "factory reset NVS erase failed: %s", esp_err_to_name(erased));
        esp_restart();
    }
    if (action == SystemAction::PowerOff) {
        bsp_display_brightness_set(0);
        // The side button only: a touch in a pocket or a bag must not switch
        // AURA on (the screen says to press the button).
        const std::uint64_t wake_mask = 1ULL << aura_board::Pins::user_button;
        const auto wake = esp_sleep_enable_ext1_wakeup_io(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);
        if (wake != ESP_OK) {
            ESP_LOGE(kTag, "deep-sleep wake configuration failed: %s", esp_err_to_name(wake));
            storage_accepting_ = true;
            return;
        }
        ESP_LOGI(kTag, "entering deep sleep; the side button wakes AURA");
        esp_deep_sleep_start();
    }
    // Restart and PowerOn both perform a real clean boot.
    esp_restart();
}

}  // namespace aura::firmware

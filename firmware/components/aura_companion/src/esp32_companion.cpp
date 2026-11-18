#include "esp32_companion.hpp"

#include "aura/assistant_actions.hpp"
#include "aura/audio_service.hpp"
#include "aura/text_util.hpp"
#include "aura_board_profile.h"
#include "aura_display.h"

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>
#include <string>
#include <utility>

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura-companion";
// The volume keys: 10 % a press, again every 200 ms after 500 ms held.
constexpr float kVolumeStep = 0.1F;
constexpr std::uint64_t kVolumeRepeatDelayMs = 500;
constexpr std::uint64_t kVolumeRepeatMs = 200;
// The PMU is asked for the PWR key's presses this often.
constexpr std::uint64_t kPowerKeyPollMs = 100;

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
    transfer_.initialize();
    const bool storage_mounted = store_.mount();
    if (!storage_mounted) {
        PhotoResult unavailable; unavailable.error = PhotoError::Storage;
        photo_mailbox_.publish(std::move(unavailable));
    }
    parse_aoraton_sync_settings(store_.read("aoraton-sync.txt"), aoraton_sync_);
    saved_aoraton_sync_text_ = serialize_aoraton_sync_settings(aoraton_sync_);
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
            // The card only (FAT on SDMMC), never the flash: the stack lives in PSRAM.
            xTaskCreateWithCaps(storage_task_entry, "aura_store", 16384, this, 3, &storage_task_handle_,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
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
    // Questions and voice messages are recorded in memory, so the assistant
    // listens without a card; only voice notes need the card (begin_note).
    capabilities_.microphone = microphone_.initialize();
    // Recognition and spoken replies start with the cloud keys
    // (set_speech_available); the wake word waits for a trained «Αύρα» model.
    capabilities_.speech_recognition = false;
    capabilities_.speech_output = false;
    capabilities_.wake_word = false;
    // The panel dims itself (no veil from the UI), and the side button is the
    // only way back from power off.
    capabilities_.panel_brightness = true;
    capabilities_.power_button = true;
    capabilities_dirty_ = true;

    gpio_config_t button{};
    // The right key (GPIO18) and the left one (BOOT, GPIO0): the volume.
    button.pin_bit_mask = (1ULL << aura_board::Pins::user_button) | (1ULL << aura_board::Pins::boot_button);
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&button);
}

void Esp32Companion::install(AuraUiCallbacks& callbacks, IAoratonService* aoraton) {
    callbacks.on_transfer_start = [this] { return transfer_.start(); };
    callbacks.on_transfer_stop = [this] { transfer_.stop(); };
    callbacks.on_photos_refresh = [this] { if (storage_jobs_ && storage_accepting_) photos_.refresh(); };
    callbacks.on_photo_select = [this](std::size_t index) { return storage_jobs_ && storage_accepting_ && photos_.select(index); };
    callbacks.on_photo_advance = [this](int direction) { photos_.advance(direction); };
    callbacks.on_photo_play = [this](bool playing) { photos_.set_playing(playing); };
    callbacks.on_photo_interval = [this](unsigned seconds) { return storage_jobs_ && storage_accepting_ && photos_.set_interval(seconds); };
    callbacks.on_photo_ambient = [this](bool enabled) { return storage_jobs_ && storage_accepting_ && photos_.set_ambient(enabled); };
    callbacks.on_photo_delete = [this](const std::string& name) { return storage_jobs_ && storage_accepting_ && photos_.erase(name); };
    aoraton_ = aoraton;
    if (aoraton_) aoraton_->set_sync_settings(aoraton_sync_);
    callbacks.on_aoraton_sync_changed = [this](const AoratonSyncSettings& settings) {
        if (!aoraton_ || !storage_jobs_ || !aoraton_sync_settings_valid(settings) || !aoraton_->set_sync_settings(settings)) return false;
        aoraton_sync_ = settings;
        return true;
    };
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
    callbacks.on_system_action = [this](SystemAction action) { system_action(action); };
}

bool Esp32Companion::begin_question() {
    // The barge-in monitor's microphone, still open (the owner spoke over the
    // answer, or it ended and the conversation goes on), is kept: no restart,
    // so none of the ES7210's settling, and after a barge-in the words that
    // interrupted the answer begin the question.
    const bool warm = microphone_.running() && (barge_monitoring_ || barge_triggered_);
    const bool interrupted = warm && barge_triggered_;
    barge_monitoring_ = false;
    barge_triggered_ = false;
    if (recording_note_ || recording_question_ || voice_.state != VoiceCaptureState::Idle ||
        !capabilities_.speech_recognition) {
        if (warm) microphone_.stop();
        return false;
    }
    if (!warm) {
        set_barge_monitor(false);
        if (!microphone_.start()) return false;
    }
    audio_.stop();
    // A new question drops an action that was never confirmed.
    decline_action();
    // A question ends by itself when the speaker stops (VoiceRecorder's turn mode).
    recorder_.start(VoiceRecorder::Mode::Turn);
    question_level_ = {};
    if (warm) {
        microphone_.set_gain(Esp32Microphone::kVoiceGainDb);
        microphone_.discard();  // the answer's last moments
        if (interrupted && barge_words_count_ > 0) {
            // Oldest first, and heard 6 dB lower while the device spoke (kMonitorGainDb).
            std::vector<std::int16_t> words(barge_words_count_);
            const std::size_t first = barge_words_next_ + barge_words_.size() - barge_words_count_;
            for (std::size_t index = 0; index < words.size(); ++index) {
                const int sample = barge_words_[(first + index) % barge_words_.size()];
                words[index] = static_cast<std::int16_t>(std::clamp(sample * 2, -32768, 32767));
            }
            recorder_.append(words.data(), words.size());
        }
        ESP_LOGI(kTag, "question: from the open microphone%s", interrupted ? ", after the owner's words" : "");
    }
    barge_words_next_ = barge_words_count_ = 0;
    recording_question_ = true;
    voice_.state = VoiceCaptureState::Recording;
    voice_.purpose = VoicePurpose::Assistant;
    voice_.error_code.clear();
    ++voice_.sequence;
    return true;
}

void Esp32Companion::end_question() {
    if (!recording_question_) return;
    recording_question_ = false;
    const bool stopped = microphone_.stop();
    samples_.clear();
    microphone_.drain(samples_);
    if (!samples_.empty()) recorder_.append(samples_.data(), samples_.size());
    const bool heard = recorder_.speech_detected();
    const auto recorded_ms = recorder_.elapsed_ms();
    auto wav = recorder_.finish_wav();
    const auto& level = question_level_;
    ESP_LOGI(kTag, "question: %u ms recorded, speech %s; input peak %d, rms %u, %u%% zero, %u samples",
             static_cast<unsigned>(recorded_ms), heard ? "heard" : "not heard", static_cast<int>(level.peak),
             static_cast<unsigned>(level.samples == 0 ? 0.0 : std::sqrt(level.energy / level.samples)),
             static_cast<unsigned>(level.samples == 0 ? 0U : level.zeros * 100U / level.samples),
             static_cast<unsigned>(level.samples));
    char tenths[20 * 7 + 1]{};
    std::size_t used = 0;
    for (const auto tenth : level.tenths) {
        used += static_cast<std::size_t>(std::snprintf(tenths + used, sizeof(tenths) - used, " %u", tenth));
        if (used >= sizeof(tenths)) break;
    }
    ESP_LOGI(kTag, "question: rms by tenths of a second:%s", tenths);
    voice_.state = VoiceCaptureState::Idle;
    voice_.level = 0.0F;
    ++voice_.sequence;
    if (!stopped) {
        voice_.error_code = "MICROPHONE_STOP_TIMEOUT";
        return;
    }
    if (question_sink_) question_sink_(std::move(wav), heard);
}

void Esp32Companion::cancel_question() {
    if (!recording_question_) return;
    recording_question_ = false;
    microphone_.stop();
    recorder_.cancel();
    voice_.state = VoiceCaptureState::Idle;
    voice_.level = 0.0F;
    ++voice_.sequence;
}

bool Esp32Companion::begin_voice_message() {
    set_barge_monitor(false);
    if (recording_note_ || recording_question_ || recording_message_ || voice_.state != VoiceCaptureState::Idle ||
        !capabilities_.microphone || !microphone_.start()) {
        return false;
    }
    audio_.stop();
    recorder_.start(VoiceRecorder::Mode::Dictation);
    recording_message_ = true;
    voice_.state = VoiceCaptureState::Recording;
    voice_.purpose = VoicePurpose::Message;
    voice_.elapsed_ms = 0;
    voice_.error_code.clear();
    ++voice_.sequence;
    return true;
}

void Esp32Companion::end_voice_message() {
    if (!recording_message_) return;
    recording_message_ = false;
    const bool stopped = microphone_.stop();
    samples_.clear();
    microphone_.drain(samples_);
    if (!samples_.empty()) recorder_.append(samples_.data(), samples_.size());
    auto wav = recorder_.finish_wav();
    voice_.state = VoiceCaptureState::Idle;
    voice_.level = 0.0F;
    ++voice_.sequence;
    if (!stopped) {
        voice_.error_code = "MICROPHONE_STOP_TIMEOUT";
        return;
    }
    if (message_sink_) message_sink_(std::move(wav));
}

void Esp32Companion::cancel_voice_message() {
    if (!recording_message_) return;
    recording_message_ = false;
    microphone_.stop();
    recorder_.cancel();
    voice_.state = VoiceCaptureState::Idle;
    voice_.level = 0.0F;
    ++voice_.sequence;
}

void Esp32Companion::set_speech_available(bool recognition, bool output) {
    const bool listens = recognition && capabilities_.microphone;
    if (capabilities_.speech_recognition == listens && capabilities_.speech_output == output) return;
    capabilities_.speech_recognition = listens;
    capabilities_.speech_output = output;
    capabilities_dirty_ = true;
    // The device's own assistant (app_main): Groq hears and answers, Azure speaks.
    extras_.assistant_provider = recognition ? "Groq" : "";
    extras_.speech_recognition_provider = listens ? "Groq Whisper" : "";
    extras_.speech_output_provider = output ? "Azure" : "";
    ++extras_.sequence;
    extras_dirty_ = true;
}

void Esp32Companion::set_barge_monitor(bool on) {
    if (on == barge_monitoring_) return;
    if (!on) {
        barge_monitoring_ = false;
        microphone_.stop();
        samples_.clear();
        if (barge_gate_) {
            ESP_LOGI(kTag, "barge-in monitor off: the answer's echo %.1f dB down; the loudest sound over it %.1fx "
                     "the echo left (%.1fx interrupts)",
                     static_cast<double>(barge_gate_->cancelled_db()), static_cast<double>(barge_gate_->loudest()),
                     static_cast<double>(EchoReferenceGate::kOwnerMargin));
        }
        return;
    }
    if (!capabilities_.microphone || recording_note_ || recording_question_ || recording_message_ ||
        voice_.state != VoiceCaptureState::Idle || barge_triggered_ || !microphone_.start()) {
        return;
    }
    microphone_.set_gain(Esp32Microphone::kMonitorGainDb);
    if (!barge_gate_) barge_gate_ = std::make_unique<EchoReferenceGate>();
    barge_gate_->reset();
    barge_words_next_ = barge_words_count_ = 0;
    samples_.clear();
    barge_monitoring_ = true;
}

bool Esp32Companion::take_barge_in() noexcept {
    const bool heard = barge_heard_;
    barge_heard_ = false;
    return heard;
}

void Esp32Companion::propose_action(const AssistantAction& action) {
    extras_.pending_action = action;
    ++extras_.sequence;
    extras_dirty_ = true;
}

bool Esp32Companion::confirm_action() {
    if (!extras_.pending_action) return false;
    const auto action = *extras_.pending_action;
    decline_action();
    return perform_action(action);
}

bool Esp32Companion::perform_action(const AssistantAction& action) {
    switch (action.type) {
        case AssistantActionType::OpenApp: return active_ui_ && active_ui_->open_app(action.app);
        case AssistantActionType::Restart: system_action(SystemAction::Restart); return true;
        case AssistantActionType::PowerOff: system_action(SystemAction::PowerOff); return true;
        default: break;
    }
    // The changed models are stored by poll(), as after a tap.
    return execute_assistant_action(action, {clock_, notes_, organizer_, display_, audio_, sound_, now_minute()});
}

void Esp32Companion::system_action(SystemAction action) {
    // Its screen at once, as the simulator shows it: switched off for a
    // switch-off; for a restart, reset or format its screen with a bar,
    // held until the board restarts (run_deferred_system_action).
    if (active_ui_) {
        if (action == SystemAction::PowerOff) active_ui_->power_off();
        else active_ui_->show_system_progress(action);
    }
    system_percent_ = 0;
    pending_system_action_.store(static_cast<int>(action));
}

void Esp32Companion::decline_action() {
    if (!extras_.pending_action) return;
    extras_.pending_action.reset();
    ++extras_.sequence;
    extras_dirty_ = true;
}

void Esp32Companion::begin_note() {
    set_barge_monitor(false);
    if (recording_note_ || recording_question_ || voice_.state != VoiceCaptureState::Idle ||
        !capabilities_.microphone || !storage_jobs_ || !microphone_.start()) return;
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
    const bool heard = recorder_.speech_detected();
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
    job->transcribe = heard && speech_ && capabilities_.speech_recognition;
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
    transfer_.poll();
    const auto transfer_state = transfer_.session().snapshot();
    if (transfer_state.photos_revision != transfer_photo_revision_) {
        transfer_photo_revision_ = transfer_state.photos_revision; transfer_photos_pending_ = true;
    }
    if (transfer_state.removals_revision != transfer_removal_revision_) {
        transfer_removal_revision_ = transfer_state.removals_revision; transfer_removals_pending_ = true;
    }
    if (transfer_photos_pending_ && active_ui_ && (active_ui_->view() != UiView::PhotoViewer || transfer_removals_pending_) && !photos_.snapshot().scanning) {
        const bool refreshed = transfer_removals_pending_ ? photos_.external_removal() : photos_.refresh();
        if (refreshed) {
            if (transfer_removals_pending_ && (active_ui_->view() == UiView::PhotoViewer || active_ui_->view() == UiView::PhotoSettings || active_ui_->view() == UiView::PhotoDelete)) active_ui_->show(UiView::Photos);
            transfer_photos_pending_ = transfer_removals_pending_ = false;
        }
    }
    photos_.poll(elapsed_ms, photos_.snapshot().playing);
    const auto now = now_ms();
    poll_storage_results();
    note_transcriptions_.poll(notes_);
    // Speech over a spoken answer: the speaker's own signal (ES7210 MIC3) is
    // taken out of the microphone, and the owner's voice is what is left.
    if (barge_monitoring_ && barge_gate_) {
        samples_.clear();
        reference_.clear();
        microphone_.drain(samples_, reference_);
        if (!samples_.empty()) {
            const bool spoke = barge_gate_->observe(samples_.data(), reference_.data(), samples_.size());
            // The last second, in a ring: moving 32 KB at every pass emptied the
            // processor's data cache, and the animation's frames took a third
            // longer while the device spoke (measured on the board, 2026-11-18).
            const auto& left = barge_gate_->residual();
            constexpr std::size_t kWordsKept = VoiceRecorder::kSampleRate;
            if (barge_words_.size() != kWordsKept) barge_words_.assign(kWordsKept, 0);
            for (const auto sample : left) {
                barge_words_[barge_words_next_] = sample;
                barge_words_next_ = (barge_words_next_ + 1U) % kWordsKept;
            }
            barge_words_count_ = std::min(barge_words_count_ + left.size(), kWordsKept);
            if (spoke) {
                ESP_LOGI(kTag, "barge-in: the owner spoke over the answer (its echo %.1f dB down)",
                         static_cast<double>(barge_gate_->cancelled_db()));
                barge_monitoring_ = false;  // the microphone stays open for the question
                barge_triggered_ = true;
                barge_heard_ = true;
            }
        }
    }
    // A microphone left open by a barge-in nobody took up is closed.
    if (barge_triggered_ && !barge_heard_ && !recording_question_) {
        barge_triggered_ = false;
        microphone_.stop();
    }
    clock_.tick(elapsed_ms);

    imu_samples_.clear();
    motion_.drain(imu_samples_);
    // The screen turns with the device unless the owner locked it.
    const bool rotation_locked = display_.snapshot().settings.rotation_locked;
    if (rotation_locked != orientation_.locked()) orientation_.set_locked(rotation_locked);
    for (const auto& sample : imu_samples_) {
        const auto gesture = imu_.push(sample);
        if (gesture != ImuGesture::None) display_.gesture(gesture, now);
        orientation_.push(sample);
    }
    const int rotation = static_cast<int>(orientation_.rotation());
    if (rotation != applied_rotation_) {
        aura_board_display_set_rotation(rotation);
        applied_rotation_ = rotation;
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

    if (recording_note_ || recording_question_ || recording_message_) {
        samples_.clear();
        microphone_.drain(samples_);
        if (recording_question_) {
            auto& level = question_level_;
            constexpr std::uint32_t kTenth = VoiceRecorder::kSampleRate / 10U;
            for (const std::int16_t sample : samples_) {
                const std::int32_t magnitude = sample < 0 ? -static_cast<std::int32_t>(sample) : sample;
                level.peak = std::max(level.peak, magnitude);
                level.energy += static_cast<double>(sample) * sample;
                if (sample == 0) ++level.zeros;
                const std::uint32_t tenth = level.samples / kTenth;
                if (tenth < level.tenths.size()) {
                    level.tenth_energy += static_cast<double>(sample) * sample;
                    if (level.samples % kTenth == kTenth - 1U) {
                        level.tenths[tenth] = static_cast<std::uint16_t>(
                            std::min(65535.0, std::sqrt(level.tenth_energy / kTenth)));
                        level.tenth_energy = 0.0;
                    }
                }
                ++level.samples;
            }
        }
        if (!samples_.empty()) recorder_.append(samples_.data(), samples_.size());
        voice_.level = recorder_.level();
        voice_.elapsed_ms = recorder_.elapsed_ms();
        ++voice_.sequence;
        if (recorder_.should_stop()) {
            if (recording_note_) finish_note();
            else if (recording_message_) end_voice_message();
            else end_question();
        }
    }

    // The three keys (the owner's request on the first flash, 2026-11-14). The
    // left (BOOT) turns the volume down and the right (GPIO18) up, again while
    // held. The centre one is the PMU's PWR key: a press wakes or sleeps the
    // screen, a hold of 1.5 s switches AURA off, and held for 1 s while off
    // the PMU switches it on.
    poll_volume_key(volume_down_key_, gpio_get_level(static_cast<gpio_num_t>(aura_board::Pins::boot_button)) == 0,
                    -1, now);
    poll_volume_key(volume_up_key_, gpio_get_level(static_cast<gpio_num_t>(aura_board::Pins::user_button)) == 0,
                    1, now);
    if (power_ != nullptr && now - power_key_polled_ms_ >= kPowerKeyPollMs) {
        power_key_polled_ms_ = now;
        const auto presses = power_->read_power_key();
        if (presses.long_press) {
            if (active_ui_) active_ui_->power_off();
            pending_system_action_.store(static_cast<int>(SystemAction::PowerOff));
        } else if (presses.short_press) {
            if (display_.snapshot().mode == DisplayMode::Active) display_.sleep_now(now);
            else display_.user_activity(now);
        }
    }

    // Any touch resets the screen timeout, as in the simulator: LVGL's inactive
    // time falls back whenever the panel is touched. Without it the screen dimmed
    // while in use (the first flash, 2026-11-14). Under the display lock.
    const std::uint32_t inactive = lv_display_get_inactive_time(nullptr);
    if (inactive < last_inactive_ms_) display_.user_activity(now);
    last_inactive_ms_ = inactive;

    const bool busy = clock_.snapshot().alert || recording_note_ || recording_question_ || recording_message_ ||
                      phone_.snapshot().incoming_call ||
                      audio_.diagnostics().state == AudioState::Playing ||
                      ota_.snapshot().state == UpdateState::Downloading || photos_.snapshot().playing ||
                      transfer_state.state == TransferState::Receiving ||
                      (active_ui_ && active_ui_->view() == UiView::FileTransfer && (transfer_state.state == TransferState::Ready || transfer_state.state == TransferState::Starting));
    display_.hold_awake(busy, now);
    display_.tick(now);
    const int brightness = display_.snapshot().brightness_percent;
    if (brightness != applied_brightness_) {
        applied_brightness_ = brightness;
        aura_board_display_set_brightness(brightness);
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

void Esp32Companion::poll_volume_key(VolumeKey& key, bool down, int direction, std::uint64_t now) {
    if (!down) {
        key.down = false;
        return;
    }
    if (key.down && now < key.next_ms) return;
    key.next_ms = now + (key.down ? kVolumeRepeatMs : kVolumeRepeatDelayMs);
    key.down = true;
    display_.user_activity(now);
    const auto audio = audio_.diagnostics();
    const float volume = std::clamp(audio.volume + kVolumeStep * static_cast<float>(direction), 0.0F, 1.0F);
    audio_.set_volume(volume);
    if (direction > 0 && audio.muted) audio_.set_muted(false);
    if (active_ui_) active_ui_->show_volume(static_cast<int>(volume * 100.0F + 0.5F));
}

void Esp32Companion::storage_task_entry(void* context) {
    static_cast<Esp32Companion*>(context)->storage_task();
}

void Esp32Companion::storage_task() {
    photo_worker_.initialize();
    for (;;) {
        storage_busy_ = true;
        photo_worker_.step();
        storage_busy_ = false;
        StoreJob* job = nullptr;
        if (xQueueReceive(storage_jobs_, &job, pdMS_TO_TICKS(25)) != pdTRUE || !job) continue;
        storage_busy_ = true;
        switch (job->kind) {
            case StoreJobKind::Clock:
            case StoreJobKind::Notes:
            case StoreJobKind::Organizer:
            case StoreJobKind::Sound:
            case StoreJobKind::Display:
            case StoreJobKind::Language:
            case StoreJobKind::Layout:
            case StoreJobKind::AoratonSync:
                job->success = store_.write(job->name.c_str(), job->text);
                break;
            case StoreJobKind::Voice:
                job->success = store_.write_note_audio(job->name, job->audio);
                // A note to transcribe keeps its WAV for the UI thread.
                if (!job->transcribe || !job->success) job->audio.clear();
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
            case StoreJobKind::AoratonSync:
                aoraton_sync_save_pending_ = false;
                if (job->success) saved_aoraton_sync_text_ = job->text;
                break;
            case StoreJobKind::Voice: {
                std::uint32_t id = 0;
                if (job->success && notes_.add_voice(job->name, job->duration_ms, job->minute, &id) == NoteResult::Ok) {
                    // Its words come from the assistant's speech service (Groq),
                    // beside any question; without keys the note stays audio only.
                    if (job->transcribe && speech_ && !job->audio.empty()) {
                        note_transcriptions_.start(
                            id, speech_->transcribe(std::move(job->audio), interface_language_code(language_)),
                            notes_);
                    } else {
                        notes_.set_transcript_failed(id);
                    }
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

    if (!aoraton_sync_save_pending_) {
        auto data = serialize_aoraton_sync_settings(aoraton_sync_);
        if (data != saved_aoraton_sync_text_)
            queue_text(StoreJobKind::AoratonSync, "aoraton-sync.txt", std::move(data), 0, aoraton_sync_save_pending_);
    }
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
    active_ui_ = &ui;
    if (capabilities_dirty_) {
        capabilities_dirty_ = false;
        ui.set_capabilities(capabilities_);
    }
    if (extras_dirty_) {
        extras_dirty_ = false;
        ui.apply_assistant_extras(extras_);
    }
    ui.apply_display(display_.snapshot());
    ui.apply_photos(photos_.snapshot());
    ui.apply_transfer(transfer_.session().snapshot());
    ui.apply_clock(clock_.snapshot());
    ui.apply_notes(notes_.snapshot());
    ui.apply_organizer(organizer_.snapshot());
    ui.apply_voice(voice_);
    ui.apply_phone(phone_.snapshot());
    ui.apply_update(ota_.snapshot());
    ui.apply_sound(sound_);
    ui.apply_launcher_layout(launcher_layout_.snapshot());
}

void Esp32Companion::set_clock(const std::tm& local, std::time_t utc) {
    LocalDateTime now;
    now.year = local.tm_year + 1900;
    now.month = local.tm_mon + 1;
    now.day = local.tm_mday;
    now.hour = local.tm_hour;
    now.minute = local.tm_min;
    now.second = local.tm_sec;
    now.weekday = local.tm_wday;
    clock_.set_clock(now, static_cast<std::int64_t>(utc) / 60);
    display_.set_minute(minute_index(now));
}

void Esp32Companion::confirm_image_after(std::uint32_t uptime_ms) {
    if (image_confirmed_ || uptime_ms < 30000) return;
    image_confirmed_ = true;
    Esp32Ota::confirm_running_image();
}

void Esp32Companion::system_progress(int percent) {
    system_percent_ = percent;
    if (active_ui_ && aura_board_display_lock(200)) {
        active_ui_->set_boot_progress(percent);
        aura_board_display_unlock();
    }
}

void Esp32Companion::hold_system_screen(int percent, std::uint32_t ms) {
    constexpr std::uint32_t kStepMs = 40;
    const int from = system_percent_;
    const int steps = std::max(1, static_cast<int>(ms / kStepMs));
    for (int step = 1; step <= steps; ++step) {
        if (active_ui_ && aura_board_display_lock(200)) {
            active_ui_->tick(kStepMs);  // the core keeps turning
            active_ui_->set_boot_progress(from + (percent - from) * step / steps);
            aura_board_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(kStepMs));
    }
    system_percent_ = percent;
}

// A confirmed action's screen stays at least this long, its bar gliding to full;
// the panel then keeps that last frame through the restart until the next start
// resets it (the owner wanted the simulator's screens on the board, 2026-11-14).
constexpr std::uint32_t kSystemScreenMs = 1400;

void Esp32Companion::run_deferred_system_action() {
    const int raw = pending_system_action_.exchange(-1);
    if (raw < 0) return;
    const std::uint64_t shown_at = now_ms();
    transfer_.shutdown();
    const auto action = static_cast<SystemAction>(raw);
    const bool switching_off = action == SystemAction::PowerOff;
    if (!switching_off) system_progress(15);
    storage_accepting_ = false;
    bool storage_flushed = true;
    if (storage_jobs_) {
        storage_flushed = false;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            if (uxQueueMessagesWaiting(storage_jobs_) == 0 && !photo_mailbox_.pending() && !storage_busy_.load()) {
                storage_flushed = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (!storage_flushed) ESP_LOGW(kTag, "system action continuing after storage flush timeout");
    microphone_.stop();
    audio_.stop();
    if (!switching_off) system_progress(40);
    // The rest of the screen's time, the bar gliding to full, then the last
    // frame's way to the panel; the restart follows.
    const auto finish_screen = [this, shown_at] {
        const auto shown = now_ms() - shown_at;
        hold_system_screen(100, shown + 300 >= kSystemScreenMs ? 300U : static_cast<std::uint32_t>(kSystemScreenMs - shown));
        vTaskDelay(pdMS_TO_TICKS(120));
    };

    if (action == SystemAction::FormatCard) {
        // Everything on the card goes; NVS (Wi-Fi, keys, the account) stays.
        // The restart loads the empty stores.
        if (!store_.format()) ESP_LOGE(kTag, "the card was not formatted");
        finish_screen();
        esp_restart();
    }
    if (action == SystemAction::FactoryReset) {
        store_.factory_reset();
        system_progress(65);
        const auto erased = nvs_flash_erase();
        if (erased != ESP_OK) ESP_LOGE(kTag, "factory reset NVS erase failed: %s", esp_err_to_name(erased));
        finish_screen();
        esp_restart();
    }
    if (switching_off) {
        // The switched-off screen (power_off()) a moment, then dimming to dark.
        const int from = applied_brightness_ > 0 ? applied_brightness_ : 100;
        constexpr int kSteps = 24;  // 50 ms each: 0.4 s as it is, 0.8 s dimming
        constexpr int kSteady = 8;
        for (int step = 1; step <= kSteps; ++step) {
            if (aura_board_display_lock(200)) {
                if (active_ui_) active_ui_->tick(50);
                if (step > kSteady) aura_board_display_set_brightness(from * (kSteps - step) / (kSteps - kSteady));
                aura_board_display_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        // Lit again by the next poll should AURA stay on (no PMU, no deep sleep).
        applied_brightness_ = 0;
        // The PMU switches the board off; held for 1 s, the PWR key switches it
        // on again. Should it not (no PMU), deep sleep, woken by the right key.
        if (power_ != nullptr && power_->power_off()) {
            ESP_LOGI(kTag, "switched off; hold the PWR key to switch on");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
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
    finish_screen();
    esp_restart();
}

}  // namespace aura::firmware

#include "esp32_companion.hpp"

#include "aura/audio_service.hpp"
#include "aura/text_util.hpp"
#include "aura_board_profile.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <cstdio>
#include <string>

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
    store_.mount();
    if (const auto data = store_.read("clock.txt"); !data.empty()) clock_.restore(data);
    if (const auto data = store_.read("notes.txt"); !data.empty()) notes_.restore(data);
    saved_clock_ = clock_.snapshot().sequence;
    saved_clock_text_ = clock_.serialize();
    saved_notes_ = notes_.snapshot().sequence;

    updater_.initialize();
    ota_ = OtaUpdateModel(updater_.running_version());
    ota_.set_source(updater_.configured(), false);

    capabilities_.imu = motion_.start();
    display_.set_imu_available(capabilities_.imu);
    capabilities_.phone_link = phone_link_.start();
    phone_.set_link(capabilities_.phone_link ? PhoneLinkState::NotPaired : PhoneLinkState::Unavailable,
                    PhonePlatform::Unknown, {});
    capabilities_.microphone = microphone_.initialize() && store_.mounted();
    // Recognition, spoken replies and the wake word wait for the AI gateway
    // and a trained "Aura" model.
    capabilities_.speech_recognition = false;
    capabilities_.speech_output = false;
    capabilities_.wake_word = false;
    capabilities_dirty_ = true;

    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << aura_board::Pins::user_button;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&button);
}

void Esp32Companion::install(AuraUiCallbacks& callbacks) {
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
        if (notes_.remove(id, &file) == NoteResult::Ok && !file.empty()) store_.remove_note_audio(file);
    };
    callbacks.on_display_settings = [this](const DisplaySettings& settings) { display_.set_settings(settings); };
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
}

void Esp32Companion::begin_note() {
    if (recording_note_ || !capabilities_.microphone || !microphone_.start()) return;
    audio_.stop();
    recorder_.start();
    recording_note_ = true;
    voice_.state = VoiceCaptureState::Recording;
    voice_.purpose = VoicePurpose::Note;
    voice_.error_code.clear();
    ++voice_.sequence;
}

void Esp32Companion::finish_note() {
    if (!recording_note_) return;
    microphone_.stop();
    samples_.clear();
    microphone_.drain(samples_);
    if (!samples_.empty()) recorder_.append(samples_.data(), samples_.size());
    const auto elapsed = recorder_.elapsed_ms();
    const auto wav = recorder_.finish_wav();
    recording_note_ = false;
    char name[48];
    std::snprintf(name, sizeof(name), "note-%lld-%u.wav", static_cast<long long>(now_minute()), ++note_counter_);
    std::uint32_t id = 0;
    if (store_.write_note_audio(name, wav) && notes_.add_voice(name, elapsed, now_minute(), &id) == NoteResult::Ok) {
        // No recogniser on the device yet: the note keeps its audio only.
        notes_.set_transcript_failed(id);
    } else {
        voice_.error_code = "NOTE_NOT_SAVED";
    }
    voice_.state = VoiceCaptureState::Idle;
    ++voice_.sequence;
}

void Esp32Companion::poll(std::uint32_t elapsed_ms) {
    const auto now = now_ms();
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
    // The alert rings until it is handled, a little louder each pass.
    const auto audio = audio_.diagnostics();
    if (!clock_.snapshot().alert) alert_since_ms_ = 0;
    else if (alert_since_ms_ == 0) alert_since_ms_ = now;
    if (clock_.snapshot().alert && !display_.snapshot().muted_by_flip && now >= chime_next_ms_ &&
        audio.state != AudioState::Playing) {
        audio_.play_pcm(make_alarm_melody(alarm_loudness(now - alert_since_ms_)));
        chime_next_ms_ = now + kAlarmCycleMs;
    }
    if (display_.snapshot().muted_by_flip && audio.state == AudioState::Playing) audio_.stop();
    // A soft chime for a phone notification that was not there before.
    if (phone_.snapshot().sequence != chime_phone_sequence_) {
        chime_phone_sequence_ = phone_.snapshot().sequence;
        std::vector<std::string> ids;
        for (const auto& item : phone_.snapshot().notifications) ids.push_back("p" + std::to_string(item.uid));
        const bool quiet = display_.snapshot().muted_by_flip || audio.muted || audio.state == AudioState::Playing ||
                           clock_.snapshot().alert || recording_note_;
        if (message_chime_.observe(std::move(ids), now, quiet)) audio_.play_pcm(make_message_chime());
    }
    save();
}

// Running timers bump the clock sequence every second; the card is written
// only when the stored alarms and reminders actually change.
void Esp32Companion::save() {
    if (clock_.snapshot().sequence != saved_clock_) {
        saved_clock_ = clock_.snapshot().sequence;
        auto data = clock_.serialize();
        if (data != saved_clock_text_ && store_.write("clock.txt", data)) saved_clock_text_ = std::move(data);
    }
    if (notes_.snapshot().sequence != saved_notes_) {
        saved_notes_ = notes_.snapshot().sequence;
        store_.write("notes.txt", notes_.serialize());
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
    ui.apply_voice(voice_);
    ui.apply_phone(phone_.snapshot());
    ui.apply_update(ota_.snapshot());
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

}  // namespace aura::firmware

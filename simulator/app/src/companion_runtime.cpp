#include "aura/companion_runtime.hpp"

#include "aura/briefing.hpp"
#include "aura/companion_strings.hpp"
#include "aura/text_util.hpp"

#include "SDL.h"
#include "lvgl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace aura::simulator {
namespace {

template <typename T>
bool ready(const std::optional<std::future<T>>& slot) {
    return slot && slot->valid() && slot->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

// A replaced request must not block the UI thread in ~future; park it.
template <typename T>
void retire(std::optional<std::future<T>>& slot, std::vector<std::future<T>>& parked) {
    if (slot && slot->valid()) parked.push_back(std::move(*slot));
    slot.reset();
}

template <typename T>
void prune(std::vector<std::future<T>>& parked) {
    parked.erase(std::remove_if(parked.begin(), parked.end(), [](std::future<T>& value) {
        return !value.valid() || value.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    }), parked.end());
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream data;
    data << input.rdbuf();
    return data.str();
}

void write_file(const std::filesystem::path& path, const std::string& data) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    std::filesystem::rename(temporary, path, error);
}

UpdateManifest demo_manifest() {
    UpdateManifest manifest;
    manifest.version = "13.1.0";
    manifest.url = "https://updates.invalid/aura-13.1.0.bin";
    manifest.size_bytes = 3355443;
    manifest.sha256 = std::string(64, '0');
    manifest.notes = "DEMO · Γρηγορότερη αναγνώριση ομιλίας και νέες φωνές.";
    return manifest;
}

const char* source_language(TranslateSide side, const TranslationModel& model) {
    return model.source_language(side).c_str();
}

}  // namespace

CompanionRuntime::CompanionRuntime(CompanionContext context) : ctx_(std::move(context)) {
    capabilities_.imu = true;          // simulated through the control panel
    capabilities_.phone_link = true;   // DEMO transport through the control panel
    phone_.set_link(PhoneLinkState::NotPaired, PhonePlatform::Unknown, {});
    ota_.set_source(true, true);
}

CompanionRuntime::~CompanionRuntime() { capture_.stop(); }

std::uint64_t CompanionRuntime::now_ms() const noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_).count());
}

std::int64_t CompanionRuntime::now_minute() const {
    return clock_.snapshot().clock_valid ? clock_.snapshot().now_minute : 0;
}

void CompanionRuntime::start(std::shared_ptr<BridgeSpeechService> speech) {
    speech_ = std::move(speech);
    translator_ = ctx_.make_translator ? ctx_.make_translator() : nullptr;
    if (!ctx_.deterministic) {
        capabilities_.microphone = SDL_GetNumAudioDevices(1) > 0;
        load_stores();
    }
    capabilities_dirty_ = true;
    next_health_ms_ = 0;
}

void CompanionRuntime::provider_changed() {
    if (ctx_.make_translator) translator_ = ctx_.make_translator();
}

void CompanionRuntime::set_capabilities(const CompanionCapabilities& capabilities) {
    capabilities_ = capabilities;
    capabilities_dirty_ = true;
}

void CompanionRuntime::refresh_capabilities() {
    if (!speech_) return;
    capabilities_.speech_recognition = speech_->stt_available();
    capabilities_.speech_output = speech_->tts_available("el");
    capabilities_dirty_ = true;
}

void CompanionRuntime::install(AuraUiCallbacks& callbacks) {
    callbacks.on_action_confirm = [this] {
        if (!extras_.pending_action) return;
        const auto action = *extras_.pending_action;
        const bool done = execute(action);
        extras_.pending_action.reset();
        bump_extras();
        ctx_.controller->dispatch({EventType::ConfirmAction});
        const auto& strings = l10n::companion_el_gr();
        ctx_.ui->show_message(done ? strings.action_done : strings.action_failed, !done);
    };
    callbacks.on_action_decline = [this] {
        extras_.pending_action.reset();
        bump_extras();
        ctx_.controller->dispatch({EventType::DeclineAction});
    };
    callbacks.on_conversation_clear = [this] {
        memory_.clear();
        extras_.memory_turns = 0;
        bump_extras();
    };
    callbacks.on_speak = [this](const std::string& text, const std::string& language) { speak(text, language); };
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
        ctx_.audio->stop();
    };
    callbacks.on_alert_snooze = [this](int minutes) {
        clock_.snooze_alert(minutes);
        ctx_.audio->stop();
    };
    callbacks.on_note_add = [this](const std::string& text) {
        return notes_.add_text(text, now_minute()) == NoteResult::Ok;
    };
    callbacks.on_note_record = [this] { return begin_voice(VoiceJob::Note, TranslateSide::Mine); };
    callbacks.on_note_record_stop = [this] { if (job_ == VoiceJob::Note) finish_voice(); };
    callbacks.on_note_record_cancel = [this] { if (job_ == VoiceJob::Note) cancel_voice(); };
    callbacks.on_note_play = [this](std::uint32_t id) {
        if (const auto* note = notes_.find(id); note && !note->audio_file.empty()) {
            ctx_.audio->play_wav((std::filesystem::path(ctx_.store_dir) / "notes" / note->audio_file).string());
        }
    };
    callbacks.on_note_delete = [this](std::uint32_t id) {
        std::string file;
        if (notes_.remove(id, &file) == NoteResult::Ok && !file.empty()) {
            std::error_code error;
            std::filesystem::remove(std::filesystem::path(ctx_.store_dir) / "notes" / file, error);
        }
    };
    callbacks.on_translate_record = [this](TranslateSide side) { return begin_voice(VoiceJob::Translate, side); };
    callbacks.on_translate_stop = [this] { if (job_ == VoiceJob::Translate) finish_voice(); };
    callbacks.on_translate_language = [this](const std::string& code) { translation_.set_their_language(code); };
    callbacks.on_briefing_open = [this] {
        const auto& status = ctx_.status->snapshot();
        if (status.date_valid) last_briefing_day_ = days_from_civil(status.year, status.month, status.day);
    };
    callbacks.on_display_settings = [this](const DisplaySettings& settings) { display_.set_settings(settings); };
    callbacks.on_wake = [this] { display_.user_activity(now_ms()); };
    callbacks.on_phone_call = [this](std::uint32_t uid, bool accept) {
        phone_.remove(uid);
        ctx_.ui->show_message(accept ? l10n::companion_el_gr().call_on_phone : l10n::companion_el_gr().decline_call,
                              false);
    };
    callbacks.on_phone_dismiss = [this](std::uint32_t uid) { phone_.remove(uid); };
    callbacks.on_phone_pair = [this] {
        phone_.set_link(PhoneLinkState::Pairing, PhonePlatform::Unknown, {});
        pair_ready_ms_ = now_ms() + 1200;
    };
    callbacks.on_phone_forget = [this] {
        pair_ready_ms_ = 0;
        phone_.set_link(PhoneLinkState::NotPaired, PhonePlatform::Unknown, {});
    };
    callbacks.on_update_check = [this] {
        if (ota_.begin_check()) update_check_ms_ = now_ms() + 900;
    };
    callbacks.on_update_install = [this] {
        const auto& status = ctx_.status->snapshot();
        if (ota_.can_install(status.connection_known && status.online, status.battery_percent, status.power_known,
                             status.charging) != UpdateBlock::None) {
            return;
        }
        if (ota_.begin_download()) update_started_ms_ = now_ms();
    };
    callbacks.on_update_restart = [this] {
        // DEMO source: nothing was downloaded or written.
        ota_.reset();
        ctx_.ui->show_message(l10n::companion_el_gr().update_demo_note, false);
    };
}

TurnRequest CompanionRuntime::turn_request(std::string prompt) {
    TurnRequest request;
    request.history = memory_.recent(now_ms());
    DeviceContextInput input;
    input.status = &ctx_.status->snapshot();
    input.weather = &ctx_.weather->snapshot();
    input.clock = &clock_;
    input.notes = &notes_;
    input.phone = &phone_;
    input.unread_messages = static_cast<int>(ctx_.aoraton->snapshot().notifications.size());
    input.question = prompt;
    request.context = build_device_context(input);
    request.prompt = std::move(prompt);
    if (extras_.pending_action) {
        extras_.pending_action.reset();
        bump_extras();
    }
    return request;
}

void CompanionRuntime::deliver_reply(const std::string& raw) {
    auto parsed = parse_assistant_answer(raw);
    auto& controller = *ctx_.controller;
    const std::string question = controller.snapshot().user_text;
    std::string text = parsed.text;
    if (text.empty() && parsed.action) text = l10n::companion_el_gr().action_question;
    if (text.empty()) text = raw;
    controller.dispatch({EventType::FinalResult, text});
    memory_.record(question, text, now_ms());
    extras_.memory_turns = memory_.size();
    std::optional<AssistantAction> action = parsed.action;
    // A message is confirmed on the Aoraton confirmation screen itself.
    if (action && action->type == AssistantActionType::Message) {
        // Show the reply first: applied later, it would cover the confirmation.
        ctx_.ui->apply(controller.snapshot());
        if (!route_message(*action)) ctx_.ui->show_message(l10n::companion_el_gr().action_failed, true);
        action.reset();
    }
    extras_.pending_action = action;
    bump_extras();
    if (action) controller.dispatch({EventType::RequireConfirmation, assistant_action_name(action->type)});
    if (voice_turn_) speak(text, "el");
    voice_turn_ = false;
}

bool CompanionRuntime::execute(const AssistantAction& action) {
    switch (action.type) {
        case AssistantActionType::Timer:
            return clock_.start_timer(static_cast<std::uint32_t>(action.seconds), action.label) == ClockResult::Ok;
        case AssistantActionType::Alarm:
            return clock_.add_alarm(action.hour, action.minute, action.daily, action.label) == ClockResult::Ok;
        case AssistantActionType::Reminder:
            if (!clock_.snapshot().clock_valid) return false;
            return clock_.add_reminder_at(reminder_due_minute(action, clock_.snapshot().now_minute), action.text) ==
                   ClockResult::Ok;
        case AssistantActionType::Note:
            return notes_.add_text(action.text, now_minute()) == NoteResult::Ok;
        case AssistantActionType::Message:
            return route_message(action);
        case AssistantActionType::None: break;
    }
    return false;
}

// A soft chime for a notification that was not there before: Aoraton
// messages, phone notifications and the labelled DEMO ones.
void CompanionRuntime::chime_for_new_messages(std::uint64_t now, AudioState audio_state) {
    const auto& aoraton = ctx_.aoraton->snapshot();
    const auto& phone = phone_.snapshot();
    const int demo = ctx_.controller->snapshot().notification_count;
    if (aoraton.sequence == chime_aoraton_sequence_ && phone.sequence == chime_phone_sequence_ &&
        demo == chime_demo_count_) {
        return;
    }
    chime_aoraton_sequence_ = aoraton.sequence;
    chime_phone_sequence_ = phone.sequence;
    chime_demo_count_ = demo;
    std::vector<std::string> ids;
    for (const auto& item : aoraton.notifications) ids.push_back("a" + item.message_id);
    for (const auto& item : phone.notifications) ids.push_back("p" + std::to_string(item.uid));
    for (int index = 1; index <= demo; ++index) ids.push_back("d" + std::to_string(index));
    // Quiet: face down or muted, while something plays or rings, or while the
    // microphone is open (the chime would be recorded).
    const bool quiet = display_.snapshot().muted_by_flip || ctx_.audio->diagnostics().muted ||
                       audio_state == AudioState::Playing || clock_.snapshot().alert || job_ != VoiceJob::None;
    if (message_chime_.observe(std::move(ids), now, quiet)) ctx_.audio->play_pcm(make_message_chime());
}

// Only a room the owner clearly named (or the only room) is used.
bool CompanionRuntime::route_message(const AssistantAction& action) {
    const auto& rooms = ctx_.aoraton->snapshot().rooms;
    const AoratonRoom* target = nullptr;
    const auto wanted = fold_text(action.room);
    for (const auto& room : rooms) {
        const auto name = fold_text(room.display_name);
        if (!wanted.empty() && (name == wanted || name.find(wanted) != std::string::npos)) {
            target = &room;
            break;
        }
    }
    if (!target && rooms.size() == 1) target = &rooms.front();
    if (!target || !ctx_.aoraton->prepare_send(target->id, action.text)) return false;
    ctx_.ui->show(UiView::AoratonConfirm);
    return true;
}

bool CompanionRuntime::begin_voice(VoiceJob job, TranslateSide side) {
    if (ctx_.deterministic || !capabilities_.microphone || job_ != VoiceJob::None) return false;
    if (job != VoiceJob::Note && !capabilities_.speech_recognition) return false;
    ctx_.audio->stop();
    if (!capture_.start()) {
        voice_.error_code = capture_.error();
        bump_voice();
        return false;
    }
    recorder_.start();
    job_ = job;
    translate_side_ = side;
    voice_.state = VoiceCaptureState::Recording;
    voice_.purpose = job == VoiceJob::Note ? VoicePurpose::Note :
                     (job == VoiceJob::Translate ? VoicePurpose::Translate : VoicePurpose::Assistant);
    voice_.level = 0.0F;
    voice_.elapsed_ms = 0;
    voice_.speech_detected = false;
    voice_.error_code.clear();
    bump_voice();
    if (job == VoiceJob::Translate) translate_entry_ = translation_.begin(side, TranslationStatus::Listening);
    return true;
}

bool CompanionRuntime::begin_listening() { return begin_voice(VoiceJob::Assistant, TranslateSide::Mine); }

void CompanionRuntime::end_listening() {
    if (job_ == VoiceJob::Assistant) finish_voice();
}

void CompanionRuntime::cancel_voice() {
    capture_.stop();
    recorder_.cancel();
    if (job_ == VoiceJob::Translate) translation_.cancel(translate_entry_);
    job_ = VoiceJob::None;
    transcript_ignored_ = true;
    voice_turn_ = false;
    voice_.state = VoiceCaptureState::Idle;
    voice_.level = 0.0F;
    bump_voice();
}

bool CompanionRuntime::handles_transcribing() const noexcept {
    return hold_transcribing_ || job_ == VoiceJob::Assistant || (transcript_ && transcript_job_ == VoiceJob::Assistant && !transcript_ignored_);
}

void CompanionRuntime::finish_voice() {
    if (job_ == VoiceJob::None) return;
    samples_.clear();
    capture_.drain(samples_);
    if (!samples_.empty()) recorder_.append(samples_.data(), samples_.size());
    capture_.stop();
    const bool heard = recorder_.speech_detected();
    const auto elapsed = recorder_.elapsed_ms();
    auto wav = recorder_.finish_wav();
    const auto job = job_;
    job_ = VoiceJob::None;
    voice_.level = 0.0F;
    switch (job) {
        case VoiceJob::Assistant: {
            auto& controller = *ctx_.controller;
            if (controller.snapshot().state == AssistantState::Listening) controller.dispatch({EventType::EndRecording});
            if (!heard) {
                voice_.state = VoiceCaptureState::Idle;
                controller.dispatch({EventType::Fail, "NO_SPEECH"});
                break;
            }
            voice_.state = VoiceCaptureState::Processing;
            transcribe(std::move(wav), "el", VoiceJob::Assistant, 0);
            break;
        }
        case VoiceJob::Note: {
            const auto name = "note-" + std::to_string(std::time(nullptr)) + "-" + std::to_string(++note_file_counter_) +
                              ".wav";
            write_file(std::filesystem::path(ctx_.store_dir) / "notes" / name, std::string(wav.begin(), wav.end()));
            std::uint32_t id = 0;
            if (notes_.add_voice(name, elapsed, now_minute(), &id) != NoteResult::Ok) {
                voice_.error_code = "NOTES_FULL";
            } else if (heard && capabilities_.speech_recognition) {
                transcribe(std::move(wav), "el", VoiceJob::Note, id);
            } else {
                notes_.set_transcript_failed(id);
            }
            voice_.state = VoiceCaptureState::Idle;
            break;
        }
        case VoiceJob::Translate:
            if (!heard) {
                translation_.fail(translate_entry_, "NO_SPEECH");
                voice_.state = VoiceCaptureState::Idle;
                break;
            }
            translation_.set_status(translate_entry_, TranslationStatus::Transcribing);
            voice_.state = VoiceCaptureState::Processing;
            transcribe(std::move(wav), source_language(translate_side_, translation_), VoiceJob::Translate, 0);
            break;
        case VoiceJob::None: break;
    }
    bump_voice();
}

void CompanionRuntime::transcribe(std::vector<std::uint8_t> wav, const std::string& language, VoiceJob job,
                                  std::uint32_t note) {
    if (!speech_) {
        finish_transcript({false, {}, "STT_UNAVAILABLE"});
        return;
    }
    retire(transcript_, retired_transcripts_);
    transcript_ = speech_->transcribe(std::move(wav), language);
    transcript_job_ = job;
    transcript_note_ = note;
    transcript_ignored_ = false;
}

void CompanionRuntime::finish_transcript(TranscriptResult result) {
    const auto job = transcript_job_;
    transcript_job_ = VoiceJob::None;
    if (transcript_ignored_) return;
    switch (job) {
        case VoiceJob::Assistant: {
            voice_.state = VoiceCaptureState::Idle;
            bump_voice();
            auto& controller = *ctx_.controller;
            if (controller.snapshot().state != AssistantState::Transcribing) return;
            if (!result.ok) {
                controller.dispatch({EventType::Fail, result.error_code.empty() ? "STT_FAILED" : result.error_code});
                return;
            }
            voice_turn_ = true;
            ctx_.start_turn(result.text);
            return;
        }
        case VoiceJob::Note:
            if (result.ok) notes_.set_transcript(transcript_note_, result.text);
            else notes_.set_transcript_failed(transcript_note_);
            return;
        case VoiceJob::Translate:
            voice_.state = VoiceCaptureState::Idle;
            bump_voice();
            if (!result.ok) {
                translation_.fail(translate_entry_, result.error_code);
                return;
            }
            translation_.set_source(translate_entry_, result.text);
            translate(translate_entry_, result.text);
            return;
        case VoiceJob::None: return;
    }
}

void CompanionRuntime::translate(std::uint32_t entry, const std::string& text) {
    const auto* item = translation_.find(entry);
    if (!item || !translator_) {
        translation_.fail(entry, "PROVIDER_UNAVAILABLE");
        return;
    }
    TurnRequest request;
    request.prompt = text;
    request.mode = TurnMode::Translate;
    request.source_language = translation_.source_language(item->side);
    request.target_language = translation_.target_language(item->side);
    retire(translation_reply_, retired_translations_);
    translation_reply_ = translator_->start_turn(std::move(request));
    translation_entry_ = entry;
}

void CompanionRuntime::speak(const std::string& text, const std::string& language) {
    if (!speech_ || !speech_->tts_available(language) || display_.snapshot().muted_by_flip) return;
    retire(speech_job_, retired_speech_);
    speech_job_ = speech_->synthesize(utf8_truncate(text, 1500), language);
}

void CompanionRuntime::poll(std::uint32_t elapsed_ms) {
    const auto now = now_ms();
    clock_.tick(elapsed_ms);
    prune(retired_transcripts_);
    prune(retired_speech_);
    prune(retired_translations_);

    if (job_ != VoiceJob::None) {
        samples_.clear();
        capture_.drain(samples_);
        if (!samples_.empty()) recorder_.append(samples_.data(), samples_.size());
        const float level = recorder_.level();
        if (std::fabs(level - voice_.level) > 0.02F || recorder_.elapsed_ms() / 1000U != voice_.elapsed_ms / 1000U) {
            voice_.level = level;
            voice_.elapsed_ms = recorder_.elapsed_ms();
            voice_.speech_detected = recorder_.speech_detected();
            bump_voice();
        }
        if (job_ == VoiceJob::Assistant) ctx_.ui->set_input_amplitude(level, false);
        if (recorder_.should_stop()) finish_voice();
    }
    if (ready(transcript_)) {
        auto result = transcript_->get();
        transcript_.reset();
        finish_transcript(std::move(result));
    }
    if (ready(translation_reply_)) {
        const auto reply = translation_reply_->get();
        translation_reply_.reset();
        if (reply.ok) {
            translation_.complete(translation_entry_, reply.text);
            if (const auto* item = translation_.find(translation_entry_)) {
                speak(item->translated, translation_.target_language(item->side));
            }
        } else if (reply.error_code != "CANCELLED") {
            translation_.fail(translation_entry_, reply.error_code);
        }
    }
    if (ready(speech_job_)) {
        auto result = speech_job_->get();
        speech_job_.reset();
        if (result.ok && !display_.snapshot().muted_by_flip && job_ == VoiceJob::None) {
            ctx_.audio->play_pcm(result.clip);
            extras_.spoken_reply = true;
            bump_extras();
        }
    }
    if (speech_ && now >= next_health_ms_ && !health_) {
        health_ = std::async(std::launch::async, [speech = speech_] { speech->refresh_health(); });
        next_health_ms_ = now + 30000;
    }
    if (ready(health_)) {
        health_->get();
        health_.reset();
        refresh_capabilities();
    }

    if (!ctx_.deterministic) {
        // Any touch or key resets the screen timeout.
        const auto inactive = lv_display_get_inactive_time(nullptr);
        if (inactive < last_inactive_ms_) display_.user_activity(now);
        last_inactive_ms_ = inactive;
        const auto audio_state = ctx_.audio->diagnostics().state;
        const bool busy = is_turn_active(ctx_.controller->snapshot().state) || clock_.snapshot().alert ||
                          job_ != VoiceJob::None || audio_state == AudioState::Playing ||
                          phone_.snapshot().incoming_call || ota_.snapshot().state == UpdateState::Downloading;
        display_.hold_awake(busy, now);
        display_.tick(now);
        const bool dark = display_.snapshot().mode == DisplayMode::AlwaysOn ||
                          display_.snapshot().mode == DisplayMode::Off;
        if (was_dark_ && !dark && should_auto_show_briefing(ctx_.status->snapshot(), last_briefing_day_)) {
            const auto& status = ctx_.status->snapshot();
            last_briefing_day_ = days_from_civil(status.year, status.month, status.day);
            ctx_.ui->open_briefing();
        }
        was_dark_ = dark;
        // The alert rings until it is handled, a little louder each pass; face
        // down silences it.
        if (!clock_.snapshot().alert) alert_since_ms_ = 0;
        else if (alert_since_ms_ == 0) alert_since_ms_ = now;
        if (clock_.snapshot().alert && !display_.snapshot().muted_by_flip && now >= chime_next_ms_ &&
            audio_state != AudioState::Playing) {
            ctx_.audio->play_pcm(make_alarm_melody(alarm_loudness(now - alert_since_ms_)));
            chime_next_ms_ = now + kAlarmCycleMs;
        }
        if (display_.snapshot().muted_by_flip && audio_state == AudioState::Playing) ctx_.audio->stop();
        chime_for_new_messages(now, audio_state);
    }

    if (pair_ready_ms_ != 0 && now >= pair_ready_ms_) {
        pair_ready_ms_ = 0;
        phone_.set_link(PhoneLinkState::Connected, PhonePlatform::Iphone, "iPhone · DEMO");
    }
    if (update_check_ms_ != 0 && now >= update_check_ms_) {
        update_check_ms_ = 0;
        ota_.check_finished(demo_manifest());
    }
    if (ota_.snapshot().state == UpdateState::Downloading) {
        const float progress = static_cast<float>(now - update_started_ms_) / 4000.0F;
        if (progress >= 1.0F) {
            ota_.begin_verify();
            update_verify_ms_ = now + 800;
        } else {
            ota_.set_progress(progress);
        }
    }
    if (ota_.snapshot().state == UpdateState::Verifying && now >= update_verify_ms_) ota_.ready_to_restart();
    if (!ctx_.deterministic) save_stores();
}

void CompanionRuntime::apply() {
    auto& ui = *ctx_.ui;
    if (capabilities_dirty_) {
        capabilities_dirty_ = false;
        ui.set_capabilities(capabilities_);
    }
    voice_.capture_available = capabilities_.microphone;
    voice_.stt_available = capabilities_.speech_recognition;
    voice_.tts_available = capabilities_.speech_output;
    ui.apply_display(display_.snapshot());
    ui.apply_clock(clock_.snapshot());
    ui.apply_notes(notes_.snapshot());
    ui.apply_voice(voice_);
    ui.apply_translation(translation_.snapshot());
    ui.apply_phone(phone_.snapshot());
    ui.apply_update(ota_.snapshot());
    ui.apply_assistant_extras(extras_);
}

void CompanionRuntime::set_clock(const LocalDateTime& now) {
    clock_.set_clock(now);
    display_.set_minute(minute_index(now));
}

void CompanionRuntime::simulate_imu(const std::string& kind) {
    const auto now = now_ms();
    auto feed = [&](float ax, float ay, float az, int samples) {
        for (int index = 0; index < samples; ++index) {
            imu_time_ms_ += 20;
            const auto gesture = imu_.push({ax, ay, az, imu_time_ms_});
            if (gesture != ImuGesture::None) display_.gesture(gesture, now);
        }
    };
    if (kind == "raise") {
        feed(0.0F, -1.0F, 0.0F, 25);
        for (int step = 0; step < 10; ++step) {
            const float angle = static_cast<float>(step) / 10.0F * 1.1F;
            imu_time_ms_ += 20;
            const auto gesture = imu_.push({0.25F, -std::cos(angle) + 0.3F, std::sin(angle) + 0.2F, imu_time_ms_});
            if (gesture != ImuGesture::None) display_.gesture(gesture, now);
        }
        feed(0.0F, -0.45F, 0.89F, 20);
    } else if (kind == "face_down") {
        feed(0.0F, 0.0F, -1.0F, 60);
    } else if (kind == "face_up") {
        feed(0.0F, 0.0F, 1.0F, 30);
    }
}

void CompanionRuntime::demo_phone_notification(const std::string& payload) {
    if (phone_.snapshot().link != PhoneLinkState::Connected) {
        phone_.set_link(PhoneLinkState::Connected, PhonePlatform::Iphone, "iPhone · DEMO");
    }
    PhoneNotification item;
    item.uid = next_phone_uid_++;
    item.category = PhoneCategory::Social;
    item.demo = true;
    item.minute = now_minute();
    // "App|Title|Message"
    const auto first = payload.find('|');
    const auto second = first == std::string::npos ? std::string::npos : payload.find('|', first + 1);
    if (second != std::string::npos) {
        item.app = payload.substr(0, first);
        item.title = payload.substr(first + 1, second - first - 1);
        item.message = payload.substr(second + 1);
    } else {
        item.app = "WhatsApp";
        item.title = "Μαρία";
        item.message = payload.empty() ? std::string("Θα αργήσω 10 λεπτά") : payload;
    }
    phone_.upsert(std::move(item));
}

bool CompanionRuntime::handle_event(const std::string& type, const std::string& payload) {
    const auto now = now_ms();
    if (type == "timer_start") {
        char* end = nullptr;
        const long parsed = std::strtol(payload.c_str(), &end, 10);
        const int seconds = end && end != payload.c_str() ? static_cast<int>(std::clamp(parsed, 1L, 86400L)) : 10;
        clock_.start_timer(static_cast<std::uint32_t>(seconds), "DEMO");
    } else if (type == "alarm_soon") {
        if (clock_.snapshot().clock_valid) {
            const auto next = from_minute_index(clock_.snapshot().now_minute + 1);
            clock_.add_alarm(next.hour, next.minute, false, "DEMO");
        }
    } else if (type == "reminder_soon") {
        clock_.add_reminder_in(1, payload.empty() ? std::string("DEMO υπενθύμιση") : payload);
    } else if (type == "briefing") {
        ctx_.ui->open_briefing();
    } else if (type == "imu") {
        simulate_imu(payload);
    } else if (type == "display_sleep") {
        display_.sleep_now(now);
    } else if (type == "phone_notification") {
        demo_phone_notification(payload);
    } else if (type == "phone_call") {
        if (phone_.snapshot().link != PhoneLinkState::Connected) {
            phone_.set_link(PhoneLinkState::Connected, PhonePlatform::Iphone, "iPhone · DEMO");
        }
        PhoneNotification call;
        call.uid = next_phone_uid_++;
        call.category = PhoneCategory::IncomingCall;
        call.app = "Phone";
        call.title = payload.empty() ? std::string("Μαρία Κ.") : payload;
        call.can_accept = true;
        call.can_decline = true;
        call.demo = true;
        phone_.upsert(std::move(call));
    } else if (type == "phone_call_end") {
        if (const auto& call = phone_.snapshot().incoming_call) phone_.remove(call->uid);
    } else if (type == "phone_clear") {
        phone_.clear();
    } else if (type == "phone_pair") {
        phone_.set_link(PhoneLinkState::Pairing, PhonePlatform::Unknown, {});
        pair_ready_ms_ = now + 1200;
    } else if (type == "update_available") {
        ota_.set_source(true, true);
        ota_.reset();
        if (ota_.begin_check()) ota_.check_finished(demo_manifest());
    } else if (type == "translate_text") {
        // "mine|text" or "theirs|text": the typed stand-in for a spoken phrase.
        const auto bar = payload.find('|');
        const auto side = payload.compare(0, bar, "theirs") == 0 ? TranslateSide::Theirs : TranslateSide::Mine;
        const std::string text = bar == std::string::npos ? payload : payload.substr(bar + 1);
        if (sanitize_text(text, 600).empty()) return true;
        const auto entry = translation_.begin(side, TranslationStatus::Translating);
        translation_.set_source(entry, text);
        translate(entry, text);
    } else if (type == "note_text") {
        notes_.add_text(payload.empty() ? std::string("DEMO σημείωση") : payload, now_minute());
    } else {
        return false;
    }
    return true;
}

void CompanionRuntime::load_stores() {
    const std::filesystem::path root(ctx_.store_dir);
    const auto clock_data = read_file(root / "clock.txt");
    if (!clock_data.empty()) clock_.restore(clock_data);
    const auto notes_data = read_file(root / "notes.txt");
    if (!notes_data.empty()) notes_.restore(notes_data);
    saved_clock_sequence_ = clock_.snapshot().sequence;
    saved_clock_text_ = clock_.serialize();
    saved_notes_sequence_ = notes_.snapshot().sequence;
}

// Only alarms, reminders and notes are stored, in the ignored runtime folder.
void CompanionRuntime::save_stores() {
    const std::filesystem::path root(ctx_.store_dir);
    // Running timers bump the sequence every second; only real changes are written.
    if (clock_.snapshot().sequence != saved_clock_sequence_) {
        saved_clock_sequence_ = clock_.snapshot().sequence;
        auto clock_text = clock_.serialize();
        if (clock_text != saved_clock_text_) {
            write_file(root / "clock.txt", clock_text);
            saved_clock_text_ = std::move(clock_text);
        }
    }
    if (notes_.snapshot().sequence != saved_notes_sequence_) {
        write_file(root / "notes.txt", notes_.serialize());
        saved_notes_sequence_ = notes_.snapshot().sequence;
    }
}

bool CompanionRuntime::capture(const std::string& scenario) {
    auto& ui = *ctx_.ui;
    LocalDateTime evening;
    evening.year = 2026;
    evening.month = 10;
    evening.day = 22;
    evening.hour = 18;
    evening.minute = 24;
    evening.weekday = 4;
    set_clock(evening);
    const auto today = (minute_index(evening) / 1440) * 1440;
    auto seed_clock = [&] {
        clock_.start_timer(600, "Ζυμαρικά · DEMO");
        clock_.tick(18000);
        clock_.add_alarm(7, 30, true, "Γυμναστήριο · DEMO");
        std::uint32_t off = 0;
        clock_.add_alarm(6, 45, false, {}, &off);
        clock_.set_alarm_enabled(off, false);
        clock_.add_reminder_at(today + 20 * 60, "Φάρμακα · DEMO");
    };
    auto seed_notes = [&] {
        notes_.add_text("Αγόρασε γάλα, ψωμί και φρούτα · DEMO", today + 9 * 60 + 5);
        std::uint32_t voice = 0;
        notes_.add_voice("note-demo.wav", 4200, today + 12 * 60 + 40, &voice);
        notes_.set_transcript(voice, "Ραντεβού με τον οδοντίατρο την Πέμπτη στις έξι · DEMO");
        notes_.add_text("Ο κωδικός του Wi‑Fi του γραφείου είναι στο συρτάρι · DEMO", today + 17 * 60 + 52);
    };
    auto seed_phone = [&] {
        phone_.set_link(PhoneLinkState::Connected, PhonePlatform::Iphone, "iPhone · DEMO");
        demo_phone_notification("WhatsApp|Μαρία|Θα αργήσω 10 λεπτά, ξεκίνα χωρίς εμένα");
        demo_phone_notification("Mail|Εφορία|Η δήλωσή σας υποβλήθηκε επιτυχώς");
    };
    CompanionCapabilities voice_capabilities = capabilities_;
    voice_capabilities.microphone = true;
    voice_capabilities.speech_recognition = true;
    voice_capabilities.speech_output = true;

    if (scenario == "clock") {
        seed_clock();
        ui.show(UiView::Clock);
    } else if (scenario == "clock-new") {
        ui.show(UiView::ClockNew);
    } else if (scenario == "timer-new") {
        ui.show(UiView::TimerNew);
    } else if (scenario == "alarm-new") {
        ui.show(UiView::AlarmNew);
    } else if (scenario == "reminder-new") {
        ui.show(UiView::ReminderNew);
    } else if (scenario == "clock-alert") {
        clock_.start_timer(600, "Ζυμαρικά · DEMO");
        clock_.tick(600000);
    } else if (scenario == "home-timer") {
        seed_clock();
        ui.show(UiView::Home);
    } else if (scenario == "notes") {
        seed_notes();
        set_capabilities(voice_capabilities);
        ui.show(UiView::Notes);
    } else if (scenario == "note-detail") {
        seed_notes();
        ui.open_note(notes_.snapshot().notes[1].id);
    } else if (scenario == "note-record") {
        voice_.state = VoiceCaptureState::Recording;
        voice_.purpose = VoicePurpose::Note;
        voice_.level = 0.55F;
        voice_.elapsed_ms = 4200;
        bump_voice();
        ui.show(UiView::NoteRecord);
    } else if (scenario == "briefing") {
        seed_clock();
        seed_phone();
        set_capabilities(voice_capabilities);
        ui.show(UiView::Briefing);
    } else if (scenario == "translate") {
        set_capabilities(voice_capabilities);
        const auto entry = translation_.begin(TranslateSide::Mine, TranslationStatus::Translating);
        translation_.set_source(entry, "Πού είναι ο σταθμός του μετρό;");
        translation_.complete(entry, "Where is the metro station?");
        ui.show(UiView::Translate);
    } else if (scenario == "always-on") {
        seed_clock();
        clock_.cancel_timer(clock_.snapshot().timers.front().id);
        ui.show(UiView::Home);
        display_.sleep_now(now_ms());
    } else if (scenario == "phone-call") {
        seed_phone();
        handle_event("phone_call", "Μαρία Κ.");
    } else if (scenario == "phone-notification") {
        seed_phone();
        ui.open_phone_notification(phone_.snapshot().notifications.front().uid);
    } else if (scenario == "notifications-phone") {
        seed_phone();
        ui.show(UiView::Notifications);
    } else if (scenario == "phone-link") {
        seed_phone();
        ui.show(UiView::PhoneLink);
    } else if (scenario == "display-settings") {
        ui.show(UiView::DisplaySettings);
    } else if (scenario == "update") {
        handle_event("update_available", {});
        ui.show(UiView::Update);
    } else if (scenario == "answer-action") {
        auto& controller = *ctx_.controller;
        controller.dispatch({EventType::Reset});
        controller.dispatch({EventType::TurnStarted, "Βάλε χρονόμετρο 10 λεπτά για τα ζυμαρικά"});
        deliver_reply("DEMO • Εντάξει, βάζω χρονόμετρο 10 λεπτών για τα ζυμαρικά.\n"
                      "ACTION: {\"type\":\"timer\",\"seconds\":600,\"label\":\"Ζυμαρικά\"}");
        set_capabilities(voice_capabilities);
        ui.show(UiView::Assistant);
    } else if (scenario == "transcribing") {
        auto& controller = *ctx_.controller;
        controller.dispatch({EventType::Reset});
        controller.dispatch({EventType::WakeDetected});
        controller.dispatch({EventType::BeginRecording});
        controller.dispatch({EventType::EndRecording});
        hold_transcribing_ = true;  // the evidence frame keeps "recognising speech"
    } else if (scenario == "voice-error") {
        auto& controller = *ctx_.controller;
        controller.dispatch({EventType::Reset});
        controller.dispatch({EventType::WakeDetected});
        controller.dispatch({EventType::BeginRecording});
        controller.dispatch({EventType::EndRecording});
        controller.dispatch({EventType::Fail, "NO_SPEECH"});
    } else {
        return false;
    }
    apply();
    return true;
}

}  // namespace aura::simulator

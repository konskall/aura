#include "aura/companion_runtime.hpp"

#include "aura/briefing.hpp"
#include "aura/companion_strings.hpp"
#include "aura/text_util.hpp"

#include "SDL.h"
#include "lvgl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

namespace aura::simulator {
namespace {

constexpr std::uintmax_t kMaximumStoreBytes = 256U * 1024U;
constexpr std::size_t kMaximumVoiceNoteBytes = 2U * 1024U * 1024U;
constexpr std::size_t kMaximumRetiredJobs = 4;

template <typename T>
bool ready(const std::optional<std::future<T>>& slot) {
    return slot && slot->valid() && slot->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

template <typename T>
void prune(std::vector<std::future<T>>& parked) {
    parked.erase(std::remove_if(parked.begin(), parked.end(), [](std::future<T>& value) {
        return !value.valid() || value.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    }), parked.end());
}

std::string read_file_once(const std::filesystem::path& path, std::uintmax_t maximum) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string data(static_cast<std::size_t>(size), '\0');
    if (!data.empty()) input.read(data.data(), static_cast<std::streamsize>(data.size()));
    return input.good() || input.eof() ? data : std::string{};
}

// A replaced request must not block the UI thread in ~future; park it.  A
// small hard cap prevents repeated cancel/retry input from creating an
// unbounded number of host requests.
template <typename T>
bool retire(std::optional<std::future<T>>& slot, std::vector<std::future<T>>& parked) {
    prune(parked);
    if (slot && slot->valid()) {
        if (slot->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
            parked.size() >= kMaximumRetiredJobs) {
            return false;
        }
        parked.push_back(std::move(*slot));
    }
    slot.reset();
    return true;
}

template <typename T>
bool has_work(const std::optional<std::future<T>>& slot) {
    return slot && slot->valid();
}

template <typename T>
void wait_for_work(std::optional<std::future<T>>& slot) {
    if (slot && slot->valid()) slot->wait();
}

template <typename T>
void wait_for_work(std::vector<std::future<T>>& parked) {
    for (auto& value : parked) {
        if (value.valid()) value.wait();
    }
}

std::string read_file(const std::filesystem::path& path) {
    auto data = read_file_once(path, kMaximumStoreBytes);
    if (!data.empty() || std::filesystem::exists(path)) return data;
    auto backup = path;
    backup += ".bak";
    return read_file_once(backup, kMaximumStoreBytes);
}

bool write_file(const std::filesystem::path& path, const std::string& data,
                std::size_t maximum = static_cast<std::size_t>(kMaximumStoreBytes)) {
    if (data.size() > maximum) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    std::filesystem::remove(temporary, error);
    error.clear();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::remove(backup, error);
    error.clear();
    const bool had_previous = std::filesystem::exists(path);
    if (had_previous) {
        std::filesystem::rename(path, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        const auto replace_error = error;
        if (had_previous) {
            error.clear();
            std::filesystem::rename(backup, path, error);
        }
        std::filesystem::remove(temporary, error);
        (void)replace_error;
        return false;
    }
    std::filesystem::remove(backup, error);
    return true;
}

UpdateManifest demo_manifest(InterfaceLanguage language) {
    UpdateManifest manifest;
    manifest.version = "13.1.0";
    manifest.url = "https://updates.invalid/aura-13.1.0.bin";
    manifest.size_bytes = 3355443;
    manifest.sha256 = std::string(64, '0');
    manifest.notes = language == InterfaceLanguage::English ?
        "DEMO · Faster speech recognition and new voices." :
        "DEMO · Γρηγορότερη αναγνώριση ομιλίας και νέες φωνές.";
    return manifest;
}

const char* source_language(TranslateSide side, const TranslationModel& model) {
    return model.source_language(side).c_str();
}

const l10n::CompanionStrings& strings_for(InterfaceLanguage language) {
    return language == InterfaceLanguage::English ? l10n::companion_en_us() : l10n::companion_el_gr();
}

}  // namespace

Appearance read_stored_appearance(const std::string& store_dir) {
    const auto data = read_file(std::filesystem::path(store_dir) / "display.txt");
    return data.empty() ? Appearance::Halo : parse_display_settings(data).appearance;
}

CompanionRuntime::CompanionRuntime(CompanionContext context) : ctx_(std::move(context)) {
    capabilities_.imu = true;          // simulated through the control panel
    // A normal interactive run exposes pairing only when a real platform
    // transport exists. Deterministic evidence retains its labelled DEMO path.
    capabilities_.phone_link = ctx_.deterministic || static_cast<bool>(ctx_.on_phone_pair);
    phone_.set_link(PhoneLinkState::NotPaired, PhonePlatform::Unknown, {});
    ota_.set_source(true, true);
    if (!ctx_.deterministic && !ctx_.store_dir.empty()) {
        saved_language_text_ = read_file(std::filesystem::path(ctx_.store_dir) / "language.txt");
        language_ = saved_language_text_.rfind("en", 0) == 0 ? InterfaceLanguage::English :
                                                               InterfaceLanguage::Greek;
        stored_appearance_ = read_stored_appearance(ctx_.store_dir);
    }
    if (ctx_.aoraton) ctx_.aoraton->set_interface_language(language_);
}

CompanionRuntime::~CompanionRuntime() {
    if (!ctx_.deterministic && !ctx_.store_dir.empty()) save_stores(true);
    capture_.stop();
    if (job_ != VoiceJob::None && ctx_.on_voice_active) ctx_.on_voice_active(false);
    for (auto& job : note_transcriptions_.release()) retired_transcripts_.push_back(std::move(job));
    const bool pending_work = has_work(transcript_) || has_work(speech_job_) ||
        has_work(translation_reply_) || has_work(health_) || !retired_transcripts_.empty() ||
        !retired_speech_.empty() || !retired_translations_.empty();
    if (pending_work) {
        // std::future values returned by std::async block in their destructor.
        // Transfer bounded, self-contained network jobs to a tiny reaper so
        // closing the simulator never freezes the UI for provider timeouts.
        std::thread([
            transcript = std::move(transcript_), speech_job = std::move(speech_job_),
            translation = std::move(translation_reply_), health = std::move(health_),
            retired_transcripts = std::move(retired_transcripts_),
            retired_speech = std::move(retired_speech_),
            retired_translations = std::move(retired_translations_),
            speech = std::move(speech_), translator = std::move(translator_)
        ]() mutable {
            wait_for_work(transcript);
            wait_for_work(speech_job);
            wait_for_work(translation);
            wait_for_work(health);
            wait_for_work(retired_transcripts);
            wait_for_work(retired_speech);
            wait_for_work(retired_translations);
            // `speech` and `translator` intentionally stay alive until their
            // futures finish, including future platform implementations that
            // may retain their service object.
            speech.reset();
            translator.reset();
        }).detach();
    }
}

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
    refresh_provider_status();
}

void CompanionRuntime::provider_changed() {
    if (ctx_.make_translator) translator_ = ctx_.make_translator();
    refresh_provider_status();
}

void CompanionRuntime::set_language(InterfaceLanguage language) {
    if (language_ == language) return;
    language_ = language;
    refresh_capabilities();
    const std::string serialized = std::string(interface_language_code(language_)) + "\n";
    if (ctx_.aoraton) ctx_.aoraton->set_interface_language(language_);
    if (ctx_.deterministic || ctx_.store_dir.empty() ||
        write_file(std::filesystem::path(ctx_.store_dir) / "language.txt", serialized)) {
        saved_language_text_ = serialized;
    }
}

void CompanionRuntime::set_capabilities(const CompanionCapabilities& capabilities) {
    capabilities_ = capabilities;
    capabilities_dirty_ = true;
}

void CompanionRuntime::refresh_capabilities() {
    if (!speech_) return;
    capabilities_.speech_recognition = speech_->stt_available();
    capabilities_.speech_output = speech_->tts_available(interface_language_code(language_));
    capabilities_dirty_ = true;
    refresh_provider_status();
}

void CompanionRuntime::refresh_provider_status() {
    const std::string assistant = ctx_.assistant_provider_name ? ctx_.assistant_provider_name() : std::string{};
    const std::string stt = speech_ && speech_->stt_available() ? speech_->stt_provider_name() : "";
    const std::string tts = speech_ && speech_->tts_available(interface_language_code(language_)) ?
        speech_->tts_provider_name() : "";
    const bool search = ctx_.web_search_available && ctx_.web_search_available();
    if (assistant == extras_.assistant_provider && stt == extras_.speech_recognition_provider &&
        tts == extras_.speech_output_provider && search == extras_.web_search_ready) {
        return;
    }
    extras_.assistant_provider = assistant;
    extras_.speech_recognition_provider = stt;
    extras_.speech_output_provider = tts;
    extras_.web_search_ready = search;
    bump_extras();
}

void CompanionRuntime::install(AuraUiCallbacks& callbacks) {
    callbacks.on_action_confirm = [this] {
        if (!extras_.pending_action) return;
        const auto action = *extras_.pending_action;
        const bool done = execute(action);
        extras_.pending_action.reset();
        bump_extras();
        ctx_.controller->dispatch({EventType::ConfirmAction});
        const auto& strings = strings_for(language_);
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
        extras_.conversation_history.clear();
        bump_extras();
    };
    callbacks.on_language_changed = [this](InterfaceLanguage language) { set_language(language); };
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
        if (ctx_.on_phone_action) ctx_.on_phone_action(uid, accept);
        phone_.remove(uid);
        const auto& strings = strings_for(language_);
        ctx_.ui->show_message(accept ? strings.call_on_phone : strings.decline_call, false);
    };
    callbacks.on_phone_dismiss = [this](std::uint32_t uid) {
        if (ctx_.on_phone_action) ctx_.on_phone_action(uid, false);
        phone_.remove(uid);
    };
    callbacks.on_phone_pair = [this] {
        if (ctx_.on_phone_pair) {
            phone_.set_link(PhoneLinkState::Pairing, PhonePlatform::Unknown, {});
            ctx_.on_phone_pair();
        } else if (ctx_.deterministic) {
            phone_.set_link(PhoneLinkState::Pairing, PhonePlatform::Unknown, {});
            pair_ready_ms_ = now_ms() + 1200;
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
    callbacks.on_phone_forget = [this] {
        pair_ready_ms_ = 0;
        if (ctx_.on_phone_forget) ctx_.on_phone_forget();
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
    callbacks.on_sound_settings = [this](const SoundSettings& settings) {
        sound_.message_chime = settings.message_chime;
        sound_.spoken_replies = settings.spoken_replies;
        sound_.continuous_conversation = settings.continuous_conversation;
        sound_.barge_in = settings.barge_in;
        sound_.assistant_voice = settings.assistant_voice;
        if (!sound_.barge_in) stop_barge_monitor();
        if (!sound_.continuous_conversation) follow_up_armed_ = false;
    };
    callbacks.on_sound_preview = [this] {
        if (!ctx_.audio->diagnostics().muted) ctx_.audio->play_pcm(make_message_chime());
    };
    callbacks.on_voice_preview = [this](AssistantVoice voice) {
        preview_voice(voice);
    };
    callbacks.on_update_restart = [this] {
        // DEMO source: nothing was downloaded or written.
        ota_.reset();
        ctx_.ui->show_message(strings_for(language_).update_demo_note, false);
    };
}

TurnRequest CompanionRuntime::turn_request(std::string prompt) {
    reset_speech_pipeline();
    streamed_spoken_prefix_.clear();
    TurnRequest request;
    request.history = memory_.recent(now_ms());
    if (extras_.memory_turns != request.history.size()) {
        extras_.memory_turns = request.history.size();
        extras_.conversation_history = request.history;
        bump_extras();
    }
    DeviceContextInput input;
    input.status = &ctx_.status->snapshot();
    input.weather = &ctx_.weather->snapshot();
    input.fuel = ctx_.fuel ? &ctx_.fuel->snapshot() : nullptr;
    input.clock = &clock_;
    input.notes = &notes_;
    input.organizer = &organizer_;
    input.phone = &phone_;
    input.unread_messages = static_cast<int>(ctx_.aoraton->snapshot().notifications.size());
    input.question = prompt;
    request.context = build_device_context(input);
    request.web_search = question_requests_web_search(prompt);
    request.prompt = std::move(prompt);
    request.interface_language = language_;
    if (extras_.pending_action) {
        extras_.pending_action.reset();
        bump_extras();
    }
    return request;
}

void CompanionRuntime::finish_local_reply(const std::string& text, std::optional<AssistantAction> action) {
    auto& controller = *ctx_.controller;
    const std::string question = controller.snapshot().user_text;
    controller.dispatch({EventType::FinalResult, text});
    memory_.record(question, text, now_ms());
    extras_.conversation_history = memory_.recent(now_ms());
    extras_.memory_turns = extras_.conversation_history.size();
    extras_.pending_action = action;
    bump_extras();
    if (action) controller.dispatch({EventType::RequireConfirmation, assistant_action_name(action->type)});
    const bool spoken_voice_turn = voice_turn_ && sound_.spoken_replies;
    if (spoken_voice_turn && speak(text, interface_language_code(language_))) {
        assistant_speech_active_ = true;
        voice_reply_spoken_ = true;
    }
    follow_up_armed_ = spoken_voice_turn && voice_reply_spoken_ && sound_.continuous_conversation && !action;
    if (follow_up_armed_) assistant_speech_active_ = true;
    assistant_reply_final_ = true;
    voice_turn_ = false;
    streamed_spoken_prefix_.clear();
}

void CompanionRuntime::answer_locally(const std::string& text) { finish_local_reply(text); }

void CompanionRuntime::end_voice_turn() {
    voice_turn_ = false;
    reset_speech_pipeline();
}

bool CompanionRuntime::handle_offline_command(const std::string& prompt) {
    const auto command = parse_offline_command(prompt);
    if (command.kind == OfflineCommandKind::None) return false;
    const auto& strings = strings_for(language_);
    char formatted[256]{};
    switch (command.kind) {
        case OfflineCommandKind::ShowTime:
            if (!clock_.snapshot().clock_valid) finish_local_reply(strings.offline_status_unknown);
            else {
                const auto now = from_minute_index(clock_.snapshot().now_minute);
                std::snprintf(formatted, sizeof(formatted), strings.offline_time_format, now.hour, now.minute);
                finish_local_reply(formatted);
            }
            return true;
        case OfflineCommandKind::ShowBattery: {
            const auto& status = ctx_.status->snapshot();
            if (!status.power_known) finish_local_reply(strings.offline_status_unknown);
            else {
                std::snprintf(formatted, sizeof(formatted), strings.offline_battery_format, status.battery_percent);
                std::string reply(formatted);
                if (status.charging) reply += strings.offline_battery_charging;
                finish_local_reply(reply);
            }
            return true;
        }
        case OfflineCommandKind::OpenClock:
        case OfflineCommandKind::OpenNotes:
        case OfflineCommandKind::OpenCalendar:
        case OfflineCommandKind::OpenTasks:
        case OfflineCommandKind::OpenShopping:
        case OfflineCommandKind::OpenRadio:
        case OfflineCommandKind::OpenWeather: {
            UiView view = UiView::Home;
            if (command.kind == OfflineCommandKind::OpenClock) view = UiView::Clock;
            else if (command.kind == OfflineCommandKind::OpenNotes) view = UiView::Notes;
            else if (command.kind == OfflineCommandKind::OpenCalendar ||
                     command.kind == OfflineCommandKind::OpenTasks ||
                     command.kind == OfflineCommandKind::OpenShopping) view = UiView::Calendar;
            else if (command.kind == OfflineCommandKind::OpenRadio) view = UiView::Radio;
            else if (command.kind == OfflineCommandKind::OpenWeather) view = UiView::Weather;
            ctx_.controller->dispatch({EventType::Cancel});
            voice_turn_ = false;
            reset_speech_pipeline();
            if (command.kind == OfflineCommandKind::OpenTasks) {
                ctx_.ui->open_organizer(OrganizerKind::Task);
            } else if (command.kind == OfflineCommandKind::OpenShopping) {
                ctx_.ui->open_organizer(OrganizerKind::Shopping);
            } else if (command.kind == OfflineCommandKind::OpenCalendar) {
                ctx_.ui->open_organizer(OrganizerKind::Event);
            } else {
                ctx_.ui->show(view);
            }
            return true;
        }
        case OfflineCommandKind::ConfirmAction:
            if (!command.has_action) return false;
            finish_local_reply(strings.action_question, command.action);
            return true;
        case OfflineCommandKind::None: break;
    }
    return false;
}

void CompanionRuntime::deliver_stream_update(const AssistantStreamSnapshot& update) {
    if (update.text.empty()) return;
    auto& controller = *ctx_.controller;
    if (controller.snapshot().state != AssistantState::Thinking &&
        controller.snapshot().state != AssistantState::Responding) {
        return;
    }
    controller.dispatch({EventType::PartialResult, update.text});
    if (voice_turn_ && sound_.spoken_replies && streamed_spoken_prefix_.empty() &&
        !update.first_sentence.empty()) {
        streamed_spoken_prefix_ = update.first_sentence;
        if (speak(streamed_spoken_prefix_, interface_language_code(language_))) {
            assistant_speech_active_ = true;
            voice_reply_spoken_ = true;
        }
    }
}

void CompanionRuntime::deliver_reply(const std::string& raw) {
    auto parsed = parse_assistant_answer(raw);
    auto& controller = *ctx_.controller;
    const std::string question = controller.snapshot().user_text;
    const auto& strings = strings_for(language_);
    // A refused action says so; the raw ACTION line is never shown or spoken.
    std::string text = answer_display_text(parsed, strings.action_question, strings.action_refused);
    if (text.empty()) text = strings.empty_answer;
    controller.dispatch({EventType::FinalResult, text});
    memory_.record(question, text, now_ms());
    extras_.conversation_history = memory_.recent(now_ms());
    extras_.memory_turns = extras_.conversation_history.size();
    std::optional<AssistantAction> action = parsed.action;
    // A message is confirmed on the Aoraton confirmation screen itself.
    if (action && action->type == AssistantActionType::Message) {
        // Show the reply first: applied later, it would cover the confirmation.
        ctx_.ui->apply(controller.snapshot());
        if (!route_message(*action)) ctx_.ui->show_message(strings_for(language_).action_failed, true);
        action.reset();
    }
    extras_.pending_action = action;
    bump_extras();
    if (action) controller.dispatch({EventType::RequireConfirmation, assistant_action_name(action->type)});
    const bool spoken_voice_turn = voice_turn_ && sound_.spoken_replies;
    if (spoken_voice_turn) {
        // The first sentence may already be playing from the stream; never say it twice.
        const auto remainder = streamed_spoken_prefix_.empty() ? text : unspoken_remainder(text, streamed_spoken_prefix_);
        if (!remainder.empty() && speak(remainder, interface_language_code(language_))) {
            assistant_speech_active_ = true;
            voice_reply_spoken_ = true;
        }
    }
    follow_up_armed_ = spoken_voice_turn && voice_reply_spoken_ && sound_.continuous_conversation && !action;
    if (follow_up_armed_) assistant_speech_active_ = true;
    assistant_reply_final_ = true;
    voice_turn_ = false;
    streamed_spoken_prefix_.clear();
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
        case AssistantActionType::Task:
            return organizer_.add_task(action.text, now_minute()) == OrganizerResult::Ok;
        case AssistantActionType::Shopping:
            return organizer_.add_shopping(action.text) == OrganizerResult::Ok;
        case AssistantActionType::CalendarEvent:
            organizer_.prune_events_before(now_minute());
            return organizer_.add_event(action.text, action.start_minute, action.duration_minutes) == OrganizerResult::Ok;
        case AssistantActionType::Volume: {
            // "Louder" and "quieter" step from the volume heard now; louder
            // also ends a mute, as on a phone.
            const int current = static_cast<int>(std::lround(ctx_.audio->diagnostics().volume * 100.0F));
            const int target = action.relative ? std::clamp(current + action.value, 0, 100) : action.value;
            if (target < 0 || target > 100) return false;
            sound_.volume = static_cast<float>(target) / 100.0F;
            ctx_.audio->set_volume(sound_.volume);
            if (action.relative && action.value > 0 && ctx_.audio->diagnostics().muted) {
                sound_.muted = false;
                ctx_.audio->set_muted(false);
            }
            return true;
        }
        case AssistantActionType::Brightness: {
            if (action.value < 5 || action.value > 100) return false;
            auto settings = display_.snapshot().settings;
            settings.active_brightness_percent = action.value;
            display_.set_settings(settings);
            return true;
        }
        case AssistantActionType::Mute:
            if (action.value != 0 && action.value != 1) return false;
            sound_.muted = action.value != 0;
            ctx_.audio->set_muted(sound_.muted);
            return true;
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
    const bool quiet = !sound_.message_chime || display_.snapshot().muted_by_flip || ctx_.audio->diagnostics().muted ||
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

bool CompanionRuntime::begin_voice(VoiceJob job, TranslateSide side, ListenOrigin origin) {
    if (ctx_.deterministic || !capabilities_.microphone || job_ != VoiceJob::None) return false;
    if (job != VoiceJob::Note && !capabilities_.speech_recognition) return false;
    stop_barge_monitor();
    ctx_.audio->stop();
    if (!capture_.start()) {
        voice_.error_code = capture_.error();
        bump_voice();
        return false;
    }
    if (ctx_.on_voice_active) ctx_.on_voice_active(true);
    // A note is dictated: it may pause to think and may last half a minute.
    recorder_.start(job == VoiceJob::Note ? VoiceRecorder::Mode::Dictation : VoiceRecorder::Mode::Turn);
    job_ = job;
    listen_origin_ = job == VoiceJob::Assistant ? origin : ListenOrigin::Manual;
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

bool CompanionRuntime::begin_listening() {
    follow_up_armed_ = false;
    return begin_voice(VoiceJob::Assistant, TranslateSide::Mine, ListenOrigin::Manual);
}

bool CompanionRuntime::begin_automatic_listening(ListenOrigin origin) {
    auto& controller = *ctx_.controller;
    if (!controller.dispatch({EventType::WakeDetected}) || !controller.dispatch({EventType::BeginRecording})) {
        return false;
    }
    if (begin_voice(VoiceJob::Assistant, TranslateSide::Mine, origin)) return true;
    controller.dispatch({EventType::ListeningExpired});
    return false;
}

void CompanionRuntime::end_listening() {
    if (job_ == VoiceJob::Assistant) finish_voice();
}

void CompanionRuntime::cancel_voice() {
    stop_barge_monitor();
    capture_.stop();
    if (job_ != VoiceJob::None && ctx_.on_voice_active) ctx_.on_voice_active(false);
    recorder_.cancel();
    if (job_ == VoiceJob::Translate) translation_.cancel(translate_entry_);
    job_ = VoiceJob::None;
    transcript_ignored_ = true;
    voice_turn_ = false;
    reset_speech_pipeline();
    listen_origin_ = ListenOrigin::Manual;
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
    if (ctx_.on_voice_active) ctx_.on_voice_active(false);
    const bool heard = recorder_.speech_detected();
    const auto elapsed = recorder_.elapsed_ms();
    auto wav = recorder_.finish_wav();
    const auto job = job_;
    job_ = VoiceJob::None;
    voice_.level = 0.0F;
    switch (job) {
        case VoiceJob::Assistant: {
            auto& controller = *ctx_.controller;
            if (!heard) {
                voice_.state = VoiceCaptureState::Idle;
                if (listen_origin_ == ListenOrigin::Manual) {
                    if (controller.snapshot().state == AssistantState::Listening) {
                        controller.dispatch({EventType::EndRecording});
                    }
                    controller.dispatch({EventType::Fail, "NO_SPEECH"});
                } else if (controller.snapshot().state == AssistantState::Listening) {
                    controller.dispatch({EventType::ListeningExpired});
                }
                listen_origin_ = ListenOrigin::Manual;
                break;
            }
            if (controller.snapshot().state == AssistantState::Listening) controller.dispatch({EventType::EndRecording});
            listen_origin_ = ListenOrigin::Manual;
            voice_.state = VoiceCaptureState::Processing;
            transcribe(std::move(wav), interface_language_code(language_), VoiceJob::Assistant);
            break;
        }
        case VoiceJob::Note: {
            // The random part keeps the name unique whatever the clock and counter say.
            const auto nonce = static_cast<std::uint32_t>(std::random_device{}()) ^
                static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            const auto name = voice_note_file_name(static_cast<std::int64_t>(std::time(nullptr)),
                                                   ++note_file_counter_, nonce);
            const auto note_path = std::filesystem::path(ctx_.store_dir) / "notes" / name;
            const bool audio_saved = write_file(note_path, std::string(wav.begin(), wav.end()),
                                                kMaximumVoiceNoteBytes);
            std::uint32_t id = 0;
            if (!audio_saved) {
                voice_.error_code = "NOTE_NOT_SAVED";
            } else if (notes_.add_voice(name, elapsed, now_minute(), &id) != NoteResult::Ok) {
                std::error_code remove_error;
                std::filesystem::remove(note_path, remove_error);
                voice_.error_code = "NOTES_FULL";
            } else if (heard && capabilities_.speech_recognition && speech_) {
                // Its own job: a question or a cancelled listen meanwhile never drops it.
                note_transcriptions_.start(id, speech_->transcribe(std::move(wav), interface_language_code(language_)),
                                           notes_);
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
            transcribe(std::move(wav), source_language(translate_side_, translation_), VoiceJob::Translate);
            break;
        case VoiceJob::None: break;
    }
    bump_voice();
}

void CompanionRuntime::transcribe(std::vector<std::uint8_t> wav, const std::string& language, VoiceJob job) {
    // A failure here belongs to `job`, not to whatever ran before it.
    if (!speech_) {
        deliver_transcript(job, {false, {}, "STT_UNAVAILABLE"});
        return;
    }
    if (!retire(transcript_, retired_transcripts_)) {
        deliver_transcript(job, {false, {}, "SPEECH_BUSY"});
        return;
    }
    transcript_ = speech_->transcribe(std::move(wav), language);
    transcript_job_ = job;
    transcript_ignored_ = false;
}

void CompanionRuntime::deliver_transcript(VoiceJob job, TranscriptResult result) {
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
        case VoiceJob::Note: return;  // notes use note_transcriptions_
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
    request.interface_language = language_;
    request.source_language = translation_.source_language(item->side);
    request.target_language = translation_.target_language(item->side);
    if (!retire(translation_reply_, retired_translations_)) {
        translation_.fail(entry, "PROVIDER_BUSY");
        return;
    }
    translation_reply_ = translator_->start_turn(std::move(request));
    translation_entry_ = entry;
}

bool CompanionRuntime::speak(const std::string& text, const std::string& language) {
    if (!speech_ || !speech_->tts_available(language) || display_.snapshot().muted_by_flip) return false;
    auto bounded = utf8_truncate(text, 1500);
    if (bounded.empty()) return false;
    // At most the early sentence plus the final remainder. Drop any extra
    // producer noise instead of growing an unbounded audio queue.
    if (speech_queue_.size() >= 2) return false;
    speech_queue_.push_back({std::move(bounded), language});
    pump_speech();
    return true;
}

void CompanionRuntime::preview_voice(AssistantVoice voice) {
    sound_.assistant_voice = voice;
    speak(strings_for(language_).voice_preview_phrase, interface_language_code(language_));
}

void CompanionRuntime::pump_speech() {
    if (!speech_ || speech_job_ || speech_ready_ || speech_queue_.empty()) return;
    auto request = std::move(speech_queue_.front());
    speech_queue_.pop_front();
    speech_job_ = speech_->synthesize(std::move(request.text), std::move(request.language),
                                      assistant_voice_id(sound_.assistant_voice));
}

void CompanionRuntime::reset_speech_pipeline() {
    stop_barge_monitor();
    speech_queue_.clear();
    speech_ready_.reset();
    if (speech_job_ && !retire(speech_job_, retired_speech_)) {
        // Keep the in-flight future owned until it completes.  Starting more
        // synthesis here would make cancellation spam grow host threads.
        return;
    }
    assistant_speech_active_ = false;
    assistant_audio_started_ = false;
    voice_reply_spoken_ = false;
    assistant_reply_final_ = false;
    follow_up_armed_ = false;
}

void CompanionRuntime::start_barge_monitor() {
    if (barge_monitoring_ || job_ != VoiceJob::None || !assistant_speech_active_ ||
        !sound_.barge_in || !capabilities_.microphone || ctx_.deterministic) {
        return;
    }
    barge_gate_.reset();
    barge_aec_.reset();
    samples_.clear();
    echo_reference_.clear();
    echo_cancelled_.clear();
    if (capture_.start()) barge_monitoring_ = true;
}

void CompanionRuntime::stop_barge_monitor() {
    if (!barge_monitoring_) return;
    capture_.stop();
    samples_.clear();
    echo_reference_.clear();
    echo_cancelled_.clear();
    barge_monitoring_ = false;
}

void CompanionRuntime::interrupt_spoken_reply() {
    if (!barge_monitoring_) return;
    stop_barge_monitor();
    ctx_.audio->stop();
    reset_speech_pipeline();
    if (ctx_.cancel_turn) ctx_.cancel_turn();
    if (is_turn_active(ctx_.controller->snapshot().state)) {
        ctx_.controller->dispatch({EventType::Cancel});
    }
    begin_automatic_listening(ListenOrigin::BargeIn);
}

void CompanionRuntime::finish_spoken_reply_chain() {
    stop_barge_monitor();
    assistant_speech_active_ = false;
    assistant_audio_started_ = false;
    voice_reply_spoken_ = false;
    assistant_reply_final_ = false;
    const bool follow_up = follow_up_armed_;
    follow_up_armed_ = false;
    if (follow_up && sound_.continuous_conversation && !powered_off_ &&
        !extras_.pending_action && !clock_.snapshot().alert) {
        begin_automatic_listening(ListenOrigin::FollowUp);
    }
}

void CompanionRuntime::poll(std::uint32_t elapsed_ms) {
    const auto now = now_ms();
    clock_.tick(elapsed_ms);
    prune(retired_transcripts_);
    prune(retired_speech_);
    prune(retired_translations_);

    if (barge_monitoring_) {
        samples_.clear();
        capture_.drain(samples_);
        const auto audio = ctx_.audio->diagnostics();
        const std::int16_t* observed = samples_.data();
        // The echo arrives 10 to 260 ms after rendering (output buffering, the
        // room, capture buffering); the canceller finds where within that span.
        constexpr std::uint32_t kEchoMinDelayMs = 10;
        constexpr std::size_t kEchoDelaySpan = VoiceRecorder::kSampleRate * 250U / 1000U;
        if (!samples_.empty() &&
            ctx_.audio->copy_echo_reference(samples_.size() + kEchoDelaySpan, echo_reference_, kEchoMinDelayMs)) {
            barge_aec_.process(samples_.data(), echo_reference_.data(), samples_.size(), kEchoDelaySpan,
                               echo_cancelled_);
            observed = echo_cancelled_.data();
        } else if (!samples_.empty()) {
            barge_aec_.process(samples_.data(), nullptr, samples_.size(), echo_cancelled_);
            observed = echo_cancelled_.data();
        }
        if (audio.state == AudioState::Playing && !samples_.empty() &&
            barge_gate_.observe(observed, samples_.size(), audio.envelope)) {
            interrupt_spoken_reply();
        }
    }

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
        const auto job = transcript_job_;
        transcript_job_ = VoiceJob::None;
        if (!transcript_ignored_) deliver_transcript(job, std::move(result));
    }
    note_transcriptions_.poll(notes_);
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
        speech_ready_ = speech_job_->get();
        speech_job_.reset();
    }
    if (speech_ready_) {
        const auto audio_state = ctx_.audio->diagnostics().state;
        if (!speech_ready_->ok) {
            std::clog << "AURA speech synthesis failed: " << speech_ready_->error_code << std::endl;
            speech_ready_.reset();
        } else if (audio_state != AudioState::Playing && !display_.snapshot().muted_by_flip &&
                   job_ == VoiceJob::None) {
            const auto sample_rate = speech_ready_->clip.format.sample_rate;
            const auto channels = speech_ready_->clip.format.channels;
            const auto samples = speech_ready_->clip.samples.size();
            const bool started = ctx_.audio->play_pcm(speech_ready_->clip);
            speech_ready_.reset();
            if (started) {
                std::clog << "AURA speech playback started: rate=" << sample_rate << ", channels="
                          << static_cast<unsigned>(channels) << ", samples=" << samples << std::endl;
                extras_.spoken_reply = true;
                bump_extras();
                if (assistant_speech_active_) {
                    assistant_audio_started_ = true;
                    start_barge_monitor();
                }
            } else {
                std::clog << "AURA speech playback failed: " << ctx_.audio->diagnostics().error_code << std::endl;
            }
        }
    }
    pump_speech();
    const auto speech_audio_state = ctx_.audio->diagnostics().state;
    const bool speech_pipeline_empty = !speech_job_ && !speech_ready_ && speech_queue_.empty();
    if (assistant_speech_active_ && assistant_reply_final_ && speech_pipeline_empty &&
        speech_audio_state != AudioState::Playing && speech_audio_state != AudioState::Paused) {
        if (!assistant_audio_started_) follow_up_armed_ = false;
        finish_spoken_reply_chain();
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

    if (!ctx_.deterministic && !powered_off_) {
        // Any touch or key resets the screen timeout.
        const auto inactive = lv_display_get_inactive_time(nullptr);
        if (inactive < last_inactive_ms_) display_.user_activity(now);
        last_inactive_ms_ = inactive;
        const auto audio_state = ctx_.audio->diagnostics().state;
        // A finished answer or a waiting confirmation lets the screen time out.
        const bool busy = holds_screen_awake(ctx_.controller->snapshot()) || clock_.snapshot().alert ||
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
        // The alert rings until it is handled, a little louder each pass and
        // through mute; face down silences it.
        if (const auto* pass = alarm_ringer_.next(clock_.snapshot().alert.has_value(),
                                                  display_.snapshot().muted_by_flip,
                                                  audio_state == AudioState::Playing, now)) {
            ctx_.audio->play_alert(*pass);
        }
        if (display_.snapshot().muted_by_flip && audio_state == AudioState::Playing) ctx_.audio->stop();
        chime_for_new_messages(now, audio_state);
    }

    if (pair_ready_ms_ != 0 && now >= pair_ready_ms_) {
        pair_ready_ms_ = 0;
        phone_.set_link(PhoneLinkState::Connected, PhonePlatform::Iphone, "DEMO · iPhone");
    }
    if (update_check_ms_ != 0 && now >= update_check_ms_) {
        update_check_ms_ = 0;
        ota_.check_finished(demo_manifest(language_));
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
    if (extras_.memory_turns > 0 && !memory_.active(now)) {
        memory_.clear();
        extras_.memory_turns = 0;
        extras_.conversation_history.clear();
        bump_extras();
    }
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
    ui.apply_organizer(organizer_.snapshot());
    ui.apply_voice(voice_);
    ui.apply_translation(translation_.snapshot());
    ui.apply_phone(phone_.snapshot());
    ui.apply_update(ota_.snapshot());
    ui.apply_assistant_extras(extras_);
    ui.apply_sound(sound_);
}

void CompanionRuntime::set_powered_off(bool off) {
    if (off) {
        if (job_ != VoiceJob::None) cancel_voice();
        reset_speech_pipeline();
        ctx_.audio->stop();
    } else {
        // The screen comes back active, and what arrived while off is not "new".
        display_.user_activity(now_ms());
        message_chime_ = MessageChimeGate{};
    }
    powered_off_ = off;
}

void CompanionRuntime::soft_restart() {
    if (job_ != VoiceJob::None) cancel_voice();
    ctx_.audio->stop();
    memory_.clear();
    extras_.pending_action.reset();
    extras_.memory_turns = 0;
    extras_.conversation_history.clear();
    bump_extras();
    translation_.clear();
    ctx_.controller->dispatch({EventType::Reset});
    set_powered_off(false);
}

void CompanionRuntime::factory_reset() {
    for (const auto& timer : std::vector<TimerItem>(clock_.snapshot().timers)) clock_.cancel_timer(timer.id);
    clock_.dismiss_alert();
    clock_.restore("aura-clock 1\n");
    for (const auto& note : notes_.snapshot().notes) {
        if (!note.audio_file.empty() && !ctx_.deterministic) {
            std::error_code error;
            std::filesystem::remove(std::filesystem::path(ctx_.store_dir) / "notes" / note.audio_file, error);
        }
    }
    notes_.restore("aura-notes 1\n");
    organizer_.clear();
    sound_ = SoundSettings{};
    ctx_.audio->set_volume(sound_.volume);
    ctx_.audio->set_muted(sound_.muted);
    display_.set_settings(DisplaySettings{});
    pair_ready_ms_ = 0;
    phone_.set_link(PhoneLinkState::NotPaired, PhonePlatform::Unknown, {});
    for (const auto& item : std::vector<PhoneNotification>(phone_.snapshot().notifications)) phone_.remove(item.uid);
    ota_.reset();
    last_briefing_day_ = -1;
    language_ = InterfaceLanguage::Greek;
    saved_language_text_ = "el\n";
    if (ctx_.aoraton) ctx_.aoraton->set_interface_language(language_);
    ctx_.ui->set_language(language_);
    refresh_capabilities();
    if (!ctx_.deterministic) {
        const std::filesystem::path root(ctx_.store_dir);
        std::error_code error;
        for (const char* name : {"clock.txt", "notes.txt", "organizer.txt", "sound.txt", "display.txt", "language.txt"}) {
            const auto path = root / name;
            std::filesystem::remove(path, error);
            auto temporary = path;
            temporary += ".tmp";
            std::filesystem::remove(temporary, error);
            auto backup = path;
            backup += ".bak";
            std::filesystem::remove(backup, error);
        }
        saved_clock_sequence_ = clock_.snapshot().sequence;
        saved_clock_text_ = clock_.serialize();
        saved_notes_sequence_ = notes_.snapshot().sequence;
        saved_notes_text_ = notes_.serialize();
        saved_organizer_sequence_ = organizer_.snapshot().sequence;
        saved_organizer_text_ = organizer_.serialize();
        saved_sound_text_ = serialize_sound_settings(sound_);
        saved_display_text_ = serialize_display_settings(display_.snapshot().settings);
    }
    soft_restart();
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
        phone_.set_link(PhoneLinkState::Connected, PhonePlatform::Iphone, "DEMO · iPhone");
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
        item.title = language_ == InterfaceLanguage::English ? "Maria" : "Μαρία";
        item.message = payload.empty() ?
            (language_ == InterfaceLanguage::English ? std::string("I will be 10 minutes late") :
                                                       std::string("Θα αργήσω 10 λεπτά")) : payload;
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
        clock_.add_reminder_in(1, payload.empty() ?
            (language_ == InterfaceLanguage::English ? std::string("DEMO reminder") :
                                                       std::string("DEMO υπενθύμιση")) : payload);
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
            phone_.set_link(PhoneLinkState::Connected, PhonePlatform::Iphone, "DEMO · iPhone");
        }
        PhoneNotification call;
        call.uid = next_phone_uid_++;
        call.category = PhoneCategory::IncomingCall;
        call.app = "Phone";
        call.title = payload.empty() ?
            (language_ == InterfaceLanguage::English ? std::string("Maria K.") : std::string("Μαρία Κ.")) : payload;
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
        if (ota_.begin_check()) ota_.check_finished(demo_manifest(language_));
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
        notes_.add_text(payload.empty() ?
            (language_ == InterfaceLanguage::English ? std::string("DEMO note") :
                                                       std::string("DEMO σημείωση")) : payload, now_minute());
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
    const auto organizer_data = read_file(root / "organizer.txt");
    if (!organizer_data.empty()) organizer_.restore(organizer_data);
    saved_clock_sequence_ = clock_.snapshot().sequence;
    saved_clock_text_ = clock_.serialize();
    saved_notes_sequence_ = notes_.snapshot().sequence;
    saved_notes_text_ = notes_.serialize();
    saved_organizer_sequence_ = organizer_.snapshot().sequence;
    saved_organizer_text_ = organizer_.serialize();
    const auto sound_data = read_file(root / "sound.txt");
    if (!sound_data.empty()) {
        sound_ = parse_sound_settings(sound_data);
        ctx_.audio->set_volume(sound_.volume);
        ctx_.audio->set_muted(sound_.muted);
    }
    saved_sound_text_ = serialize_sound_settings(sound_);
    const auto display_data = read_file(root / "display.txt");
    if (!display_data.empty()) display_.set_settings(parse_display_settings(display_data));
    saved_display_text_ = serialize_display_settings(display_.snapshot().settings);
}

// Alarms, reminders, notes and sound settings are stored, in the ignored runtime folder.
void CompanionRuntime::save_stores(bool flush) {
    const std::filesystem::path root(ctx_.store_dir);
    // Running timers bump the sequence every second; only real changes are written.
    if (clock_.snapshot().sequence != saved_clock_sequence_) {
        const auto sequence = clock_.snapshot().sequence;
        auto clock_text = clock_.serialize();
        if (clock_text == saved_clock_text_) {
            saved_clock_sequence_ = sequence;
        } else if (write_file(root / "clock.txt", clock_text)) {
            saved_clock_sequence_ = sequence;
            saved_clock_text_ = std::move(clock_text);
        }
    }
    if (notes_.snapshot().sequence != saved_notes_sequence_) {
        const auto sequence = notes_.snapshot().sequence;
        auto notes_text = notes_.serialize();
        if (notes_text == saved_notes_text_) {
            saved_notes_sequence_ = sequence;
        } else if (write_file(root / "notes.txt", notes_text)) {
            saved_notes_sequence_ = sequence;
            saved_notes_text_ = std::move(notes_text);
        }
    }
    if (organizer_.snapshot().sequence != saved_organizer_sequence_) {
        const auto sequence = organizer_.snapshot().sequence;
        auto organizer_text = organizer_.serialize();
        if (organizer_text == saved_organizer_text_) {
            saved_organizer_sequence_ = sequence;
        } else if (write_file(root / "organizer.txt", organizer_text)) {
            saved_organizer_sequence_ = sequence;
            saved_organizer_text_ = std::move(organizer_text);
        }
    }
    // Values are compared every poll; text is built only after a change. A
    // dragged volume slider is written once it rests, not on every step.
    sound_ = current_sound_settings(sound_, ctx_.audio->diagnostics());
    if (sound_ != seen_sound_) {
        seen_sound_ = sound_;
        sound_pending_ = true;
    }
    if (sound_pending_ || flush) {
        auto sound_text = serialize_sound_settings(sound_);
        if (sound_text == saved_sound_text_) {
            sound_pending_ = false;
        } else if ((flush || sound_settle_.settled(sound_text, now_ms())) &&
                   write_file(root / "sound.txt", sound_text)) {
            saved_sound_text_ = std::move(sound_text);
            sound_pending_ = false;
        }
    }
    if (display_.snapshot().sequence != saved_display_sequence_) {
        const auto sequence = display_.snapshot().sequence;
        auto display_text = serialize_display_settings(display_.snapshot().settings);
        if (display_text == saved_display_text_) {
            saved_display_sequence_ = sequence;
        } else if (write_file(root / "display.txt", display_text)) {
            saved_display_text_ = std::move(display_text);
            saved_display_sequence_ = sequence;
        }
    }
    if (saved_language_text_.compare(0, 2, interface_language_code(language_)) != 0) {
        const auto language_text = std::string(interface_language_code(language_)) + "\n";
        if (write_file(root / "language.txt", language_text)) saved_language_text_ = language_text;
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
    const bool english = language_ == InterfaceLanguage::English;
    auto seed_clock = [&] {
        clock_.start_timer(600, english ? "DEMO · Pasta" : "DEMO · Ζυμαρικά");
        clock_.tick(18000);
        clock_.add_alarm(7, 30, true, english ? "DEMO · Gym" : "DEMO · Γυμναστήριο");
        std::uint32_t off = 0;
        clock_.add_alarm(6, 45, false, {}, &off);
        clock_.set_alarm_enabled(off, false);
        clock_.add_reminder_at(today + 20 * 60, english ? "DEMO · Medicine" : "DEMO · Φάρμακα");
    };
    auto seed_notes = [&] {
        notes_.add_text(english ? "DEMO · Buy milk, bread and fruit" :
                                  "DEMO · Αγόρασε γάλα, ψωμί και φρούτα", today + 9 * 60 + 5);
        std::uint32_t voice = 0;
        notes_.add_voice("note-demo.wav", 4200, today + 12 * 60 + 40, &voice);
        notes_.set_transcript(voice, english ? "DEMO · Dentist appointment on Thursday at six" :
                                               "DEMO · Ραντεβού με τον οδοντίατρο την Πέμπτη στις έξι");
        notes_.add_text(english ? "DEMO · The office Wi-Fi password is in the drawer" :
                                  "DEMO · Ο κωδικός του Wi‑Fi του γραφείου είναι στο συρτάρι",
                        today + 17 * 60 + 52);
    };
    auto seed_organizer = [&] {
        organizer_.add_event(english ? "DEMO · Dentist" : "DEMO · Οδοντίατρος",
                             today + 19 * 60 + 30, 45);
        organizer_.add_event(english ? "DEMO · Team call" : "DEMO · Κλήση ομάδας",
                             today + 1440 + 10 * 60, 60);
        organizer_.add_task(english ? "DEMO · Send the documents" : "DEMO · Στείλε τα έγγραφα",
                            today + 9 * 60);
        organizer_.add_task(english ? "DEMO · Book service" : "DEMO · Κλείσε service",
                            today + 9 * 60 + 5);
        organizer_.add_shopping(english ? "DEMO · Milk" : "DEMO · Γάλα");
        organizer_.add_shopping(english ? "DEMO · Coffee" : "DEMO · Καφές");
    };
    auto seed_phone = [&] {
        phone_.set_link(PhoneLinkState::Connected, PhonePlatform::Iphone, "DEMO · iPhone");
        demo_phone_notification(english ? "WhatsApp|Maria|I will be 10 minutes late, start without me" :
                                          "WhatsApp|Μαρία|Θα αργήσω 10 λεπτά, ξεκίνα χωρίς εμένα");
        demo_phone_notification(english ? "Mail|Tax office|Your return was submitted successfully" :
                                          "Mail|Εφορία|Η δήλωσή σας υποβλήθηκε επιτυχώς");
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
        clock_.start_timer(600, english ? "DEMO · Pasta" : "DEMO · Ζυμαρικά");
        clock_.tick(600000);
    } else if (scenario == "home-timer") {
        seed_clock();
        ui.show(UiView::Home);
    } else if (scenario == "notes") {
        seed_notes();
        set_capabilities(voice_capabilities);
        ui.show(UiView::Notes);
    } else if (scenario == "calendar") {
        seed_organizer();
        ui.open_organizer(OrganizerKind::Event);
    } else if (scenario == "organizer-tasks") {
        seed_organizer();
        ui.open_organizer(OrganizerKind::Task);
    } else if (scenario == "organizer-shopping") {
        seed_organizer();
        ui.open_organizer(OrganizerKind::Shopping);
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
        // The owner's side speaks Greek in either interface language: every
        // pair is Greek with another language.
        const auto entry = translation_.begin(TranslateSide::Mine, TranslationStatus::Translating);
        translation_.set_source(entry, "Πού είναι ο σταθμός του μετρό;");
        translation_.complete(entry, "Where is the metro station?");
        ui.show(UiView::Translate);
    } else if (scenario == "conversation-history") {
        memory_.clear();
        memory_.record(english ? "DEMO · Find pizza in Sparta" : "DEMO · Ψάξε για πίτσα στη Σπάρτη",
                       english ? "DEMO · Three verified results were found" :
                                 "DEMO · Βρέθηκαν τρία επιβεβαιωμένα αποτελέσματα", now_ms());
        memory_.record(english ? "DEMO · Which one is open late?" : "DEMO · Ποιο μένει ανοιχτό αργά;",
                       english ? "DEMO · Opening times need a live check" :
                                 "DEMO · Τα ωράρια χρειάζονται ζωντανό έλεγχο", now_ms());
        extras_.conversation_history = memory_.recent(now_ms());
        extras_.memory_turns = extras_.conversation_history.size();
        bump_extras();
        ui.show(UiView::ConversationHistory);
    } else if (scenario == "status-services") {
        extras_.assistant_provider = "DEMO · Codex CLI";
        extras_.speech_recognition_provider = "DEMO · Groq STT";
        extras_.speech_output_provider = "DEMO · Azure Neural";
        extras_.web_search_ready = true;
        bump_extras();
        ui.show(UiView::Battery);
    } else if (scenario == "always-on") {
        seed_clock();
        clock_.cancel_timer(clock_.snapshot().timers.front().id);
        ui.show(UiView::Home);
        display_.sleep_now(now_ms());
    } else if (scenario == "phone-call") {
        seed_phone();
        handle_event("phone_call", english ? "Maria K." : "Μαρία Κ.");
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
        controller.dispatch({EventType::TurnStarted, english ? "Set a 10-minute pasta timer" :
                                                               "Βάλε χρονόμετρο 10 λεπτά για τα ζυμαρικά"});
        deliver_reply(english ?
            "DEMO • Okay, setting a 10-minute pasta timer.\n"
            "ACTION: {\"type\":\"timer\",\"seconds\":600,\"label\":\"Pasta\"}" :
            "DEMO • Εντάξει, βάζω χρονόμετρο 10 λεπτών για τα ζυμαρικά.\n"
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

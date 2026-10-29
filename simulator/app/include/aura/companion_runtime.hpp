#pragma once

#include "aura/aoraton.hpp"
#include "aura/assistant_companion.hpp"
#include "aura/assistant_stream.hpp"
#include "aura/assistant_state.hpp"
#include "aura/aura_ui.hpp"
#include "aura/bridge_speech_service.hpp"
#include "aura/clock_services.hpp"
#include "aura/device_status.hpp"
#include "aura/display_power.hpp"
#include "aura/fuel_prices.hpp"
#include "aura/notes.hpp"
#include "aura/offline_commands.hpp"
#include "aura/organizer.hpp"
#include "aura/ota_update.hpp"
#include "aura/phone_link.hpp"
#include "aura/provider.hpp"
#include "aura/sounds.hpp"
#include "aura/translation.hpp"
#include "aura/voice.hpp"
#include "aura/weather.hpp"
#include "aura/windows_audio_backend.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aura::simulator {

struct CompanionContext {
    AssistantController* controller{};
    AuraUi* ui{};
    WindowsAudioBackend* audio{};
    IAoratonService* aoraton{};
    WeatherModel* weather{};
    FuelPriceModel* fuel{};
    DeviceStatusModel* status{};
    // Starts an assistant turn (main.cpp owns the assistant provider).
    std::function<void(std::string)> start_turn;
    // Cancels an in-flight provider turn before a voice interruption starts.
    std::function<void()> cancel_turn;
    // Pauses/resumes continuous media while the microphone owns the room.
    std::function<void(bool)> on_voice_active;
    // Trusted, local service labels for Settings > Device status.
    std::function<std::string()> assistant_provider_name;
    std::function<bool()> web_search_available;
    // Optional real Windows/iPhone transport. Without it, deterministic runs
    // retain the explicitly labelled DEMO pairing behaviour.
    std::function<void(std::uint32_t uid, bool positive)> on_phone_action;
    std::function<void()> on_phone_pair;
    std::function<void()> on_phone_forget;
    // Translations use a provider of their own, so they never cancel a turn.
    std::function<std::unique_ptr<IAssistantProvider>()> make_translator;
    bool deterministic{false};
    std::string store_dir;
};

// The v13 "Companion" half of the simulator: clock, notes, translation,
// display power, phone link (DEMO), software update (DEMO), conversation memory
// and real voice through the PC microphone and the loopback bridge.
class CompanionRuntime {
public:
    explicit CompanionRuntime(CompanionContext context);
    ~CompanionRuntime();
    CompanionRuntime(const CompanionRuntime&) = delete;
    CompanionRuntime& operator=(const CompanionRuntime&) = delete;

    void install(AuraUiCallbacks& callbacks);
    // `speech` is null when no bridge runs (deterministic runs).
    void start(std::shared_ptr<BridgeSpeechService> speech);
    void provider_changed();

    TurnRequest turn_request(std::string prompt);
    // Returns true when a narrow, deterministic offline command consumed the
    // already-started assistant turn. Side effects remain confirmation-gated.
    bool handle_offline_command(const std::string& prompt);
    // A deterministic local answer (namedays, holidays) for the already-started
    // turn: spoken on a voice turn and kept in the conversation memory.
    void answer_locally(const std::string& text);
    // A request answered by a screen instead of words (radio, weather): the
    // voice turn ends, so the next typed answer is not read aloud.
    void end_voice_turn();
    // Consumed on the UI thread from the bounded provider mailbox.
    void deliver_stream_update(const AssistantStreamSnapshot& update);
    void deliver_reply(const std::string& raw);
    // Real microphone for the assistant; false means "use the synthetic path".
    bool begin_listening();
    void end_listening();
    void cancel_voice();
    bool capturing() const noexcept { return job_ == VoiceJob::Assistant; }
    bool handles_transcribing() const noexcept;

    bool handle_event(const std::string& type, const std::string& payload);
    void poll(std::uint32_t elapsed_ms);
    void apply();
    void set_clock(const LocalDateTime& now);
    // Seeds deterministic DEMO data and shows the scenario's screen.
    bool capture(const std::string& scenario);

    ClockServicesModel& clock() noexcept { return clock_; }
    NotesModel& notes() noexcept { return notes_; }
    OrganizerModel& organizer() noexcept { return organizer_; }
    TranslationModel& translation() noexcept { return translation_; }
    PhoneNotificationModel& phone() noexcept { return phone_; }
    OtaUpdateModel& ota() noexcept { return ota_; }
    DisplayPowerModel& display() noexcept { return display_; }
    ConversationMemory& memory() noexcept { return memory_; }
    const AssistantExtras& extras() const noexcept { return extras_; }
    void set_capabilities(const CompanionCapabilities& capabilities);
    std::uint64_t now_ms() const noexcept;
    const SoundSettings& sound() const noexcept { return sound_; }
    InterfaceLanguage language() const noexcept { return language_; }
    // The look stored in display.txt, read at construction so the UI can take
    // it before its first frame (the display store itself loads in start()).
    Appearance appearance() const noexcept { return stored_appearance_; }
    void set_language(InterfaceLanguage language);
    // Requests the localized preview through the configured speech service.
    void preview_voice(AssistantVoice voice);

    // Settings > System. Off: no alarms, chimes or screen changes until power on.
    void set_powered_off(bool off);
    // What a restart forgets: the conversation, playback, recording and a
    // pending action. Stored data stays.
    void soft_restart();
    // Erases alarms, reminders, notes, sound and display settings, the phone
    // link and their files, then restarts.
    void factory_reset();

private:
    enum class VoiceJob { None, Assistant, Note, Translate };
    enum class ListenOrigin { Manual, FollowUp, BargeIn };

    std::int64_t now_minute() const;
    void bump_voice() { ++voice_.sequence; }
    void bump_extras() { ++extras_.sequence; }
    bool begin_voice(VoiceJob job, TranslateSide side, ListenOrigin origin = ListenOrigin::Manual);
    bool begin_automatic_listening(ListenOrigin origin);
    void finish_voice();
    // Recognition for the assistant or a translation (voice notes have their own).
    void transcribe(std::vector<std::uint8_t> wav, const std::string& language, VoiceJob job);
    void deliver_transcript(VoiceJob job, TranscriptResult result);
    void translate(std::uint32_t entry, const std::string& text);
    bool speak(const std::string& text, const std::string& language);
    void pump_speech();
    void reset_speech_pipeline();
    void start_barge_monitor();
    void stop_barge_monitor();
    void interrupt_spoken_reply();
    void finish_spoken_reply_chain();
    bool execute(const AssistantAction& action);
    void finish_local_reply(const std::string& text, std::optional<AssistantAction> action = std::nullopt);
    bool route_message(const AssistantAction& action);
    void chime_for_new_messages(std::uint64_t now, AudioState audio_state);
    void simulate_imu(const std::string& kind);
    void demo_phone_notification(const std::string& payload);
    void load_stores();
    // `flush` writes a setting that has not yet settled (on shutdown).
    void save_stores(bool flush = false);
    void refresh_capabilities();
    void refresh_provider_status();

    CompanionContext ctx_;
    ClockServicesModel clock_;
    NotesModel notes_;
    OrganizerModel organizer_;
    TranslationModel translation_;
    PhoneNotificationModel phone_;
    OtaUpdateModel ota_{"13.0.0"};
    DisplayPowerModel display_;
    ImuGestureDetector imu_;
    ConversationMemory memory_;
    VoiceRecorder recorder_;
    BargeInGate barge_gate_;
    AcousticEchoCanceller barge_aec_;
    SdlVoiceCapture capture_;
    std::shared_ptr<BridgeSpeechService> speech_;
    std::unique_ptr<IAssistantProvider> translator_;
    VoiceSnapshot voice_{};
    AssistantExtras extras_{};
    CompanionCapabilities capabilities_{};
    CompanionCapabilities applied_capabilities_{};
    bool capabilities_dirty_{true};
    std::vector<std::int16_t> samples_;
    std::vector<std::int16_t> echo_reference_;
    std::vector<std::int16_t> echo_cancelled_;
    VoiceJob job_{VoiceJob::None};
    ListenOrigin listen_origin_{ListenOrigin::Manual};
    bool barge_monitoring_{false};
    TranslateSide translate_side_{TranslateSide::Mine};
    std::uint32_t translate_entry_{0};

    std::optional<std::future<TranscriptResult>> transcript_;
    VoiceJob transcript_job_{VoiceJob::None};
    bool transcript_ignored_{false};
    NoteTranscriptions note_transcriptions_;
    std::optional<std::future<SpeechResult>> speech_job_;
    std::optional<SpeechResult> speech_ready_;
    struct PendingSpeech {
        std::string text;
        std::string language;
    };
    std::deque<PendingSpeech> speech_queue_;
    std::optional<std::future<AssistantReply>> translation_reply_;
    std::uint32_t translation_entry_{0};
    std::optional<std::future<void>> health_;
    std::vector<std::future<TranscriptResult>> retired_transcripts_;
    std::vector<std::future<SpeechResult>> retired_speech_;
    std::vector<std::future<AssistantReply>> retired_translations_;
    bool voice_turn_{false};
    std::string streamed_spoken_prefix_;
    bool assistant_speech_active_{false};
    bool assistant_audio_started_{false};
    bool voice_reply_spoken_{false};
    bool assistant_reply_final_{false};
    bool follow_up_armed_{false};

    std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
    std::uint64_t next_health_ms_{0};
    std::uint32_t last_inactive_ms_{0};
    std::int64_t last_briefing_day_{-1};
    bool was_dark_{false};
    std::uint64_t pair_ready_ms_{0};
    std::uint64_t update_check_ms_{0};
    std::uint64_t update_started_ms_{0};
    std::uint64_t update_verify_ms_{0};
    AlarmRinger alarm_ringer_;
    MessageChimeGate message_chime_;
    std::uint64_t chime_aoraton_sequence_{0};
    std::uint64_t chime_phone_sequence_{0};
    int chime_demo_count_{-1};
    std::uint32_t imu_time_ms_{0};
    std::uint32_t next_phone_uid_{100};
    std::uint32_t note_file_counter_{0};
    std::uint64_t saved_clock_sequence_{0};
    std::uint64_t saved_notes_sequence_{0};
    std::uint64_t saved_organizer_sequence_{0};
    std::string saved_clock_text_;
    std::string saved_notes_text_;
    std::string saved_organizer_text_;
    bool hold_transcribing_{false};
    SoundSettings sound_{};
    std::string saved_sound_text_;
    SettingSettle sound_settle_;
    // Settings are compared as values each poll; text is built only after a
    // change, not for every frame.
    SoundSettings seen_sound_{};
    bool sound_pending_{false};
    std::string saved_display_text_;
    std::uint64_t saved_display_sequence_{0};
    InterfaceLanguage language_{InterfaceLanguage::Greek};
    Appearance stored_appearance_{Appearance::Halo};
    std::string saved_language_text_;
    bool powered_off_{false};
};

// The look stored in `store_dir`/display.txt; Halo when there is none.
Appearance read_stored_appearance(const std::string& store_dir);

}  // namespace aura::simulator

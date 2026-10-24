#pragma once

#include "aura/aoraton.hpp"
#include "aura/assistant_companion.hpp"
#include "aura/assistant_state.hpp"
#include "aura/aura_ui.hpp"
#include "aura/bridge_speech_service.hpp"
#include "aura/clock_services.hpp"
#include "aura/device_status.hpp"
#include "aura/display_power.hpp"
#include "aura/notes.hpp"
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
    DeviceStatusModel* status{};
    // Starts an assistant turn (main.cpp owns the assistant provider).
    std::function<void(std::string)> start_turn;
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
    TranslationModel& translation() noexcept { return translation_; }
    PhoneNotificationModel& phone() noexcept { return phone_; }
    OtaUpdateModel& ota() noexcept { return ota_; }
    DisplayPowerModel& display() noexcept { return display_; }
    ConversationMemory& memory() noexcept { return memory_; }
    const AssistantExtras& extras() const noexcept { return extras_; }
    void set_capabilities(const CompanionCapabilities& capabilities);
    std::uint64_t now_ms() const noexcept;
    const SoundSettings& sound() const noexcept { return sound_; }

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

    std::int64_t now_minute() const;
    void bump_voice() { ++voice_.sequence; }
    void bump_extras() { ++extras_.sequence; }
    bool begin_voice(VoiceJob job, TranslateSide side);
    void finish_voice();
    void transcribe(std::vector<std::uint8_t> wav, const std::string& language, VoiceJob job, std::uint32_t note);
    void finish_transcript(TranscriptResult result);
    void translate(std::uint32_t entry, const std::string& text);
    void speak(const std::string& text, const std::string& language);
    bool execute(const AssistantAction& action);
    bool route_message(const AssistantAction& action);
    void chime_for_new_messages(std::uint64_t now, AudioState audio_state);
    void simulate_imu(const std::string& kind);
    void demo_phone_notification(const std::string& payload);
    void load_stores();
    void save_stores();
    void refresh_capabilities();

    CompanionContext ctx_;
    ClockServicesModel clock_;
    NotesModel notes_;
    TranslationModel translation_;
    PhoneNotificationModel phone_;
    OtaUpdateModel ota_{"13.0.0"};
    DisplayPowerModel display_;
    ImuGestureDetector imu_;
    ConversationMemory memory_;
    VoiceRecorder recorder_;
    SdlVoiceCapture capture_;
    std::shared_ptr<BridgeSpeechService> speech_;
    std::unique_ptr<IAssistantProvider> translator_;
    VoiceSnapshot voice_{};
    AssistantExtras extras_{};
    CompanionCapabilities capabilities_{};
    CompanionCapabilities applied_capabilities_{};
    bool capabilities_dirty_{true};
    std::vector<std::int16_t> samples_;
    VoiceJob job_{VoiceJob::None};
    TranslateSide translate_side_{TranslateSide::Mine};
    std::uint32_t translate_entry_{0};

    std::optional<std::future<TranscriptResult>> transcript_;
    VoiceJob transcript_job_{VoiceJob::None};
    std::uint32_t transcript_note_{0};
    bool transcript_ignored_{false};
    std::optional<std::future<SpeechResult>> speech_job_;
    std::optional<std::future<AssistantReply>> translation_reply_;
    std::uint32_t translation_entry_{0};
    std::optional<std::future<void>> health_;
    std::vector<std::future<TranscriptResult>> retired_transcripts_;
    std::vector<std::future<SpeechResult>> retired_speech_;
    std::vector<std::future<AssistantReply>> retired_translations_;
    bool voice_turn_{false};

    std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
    std::uint64_t next_health_ms_{0};
    std::uint32_t last_inactive_ms_{0};
    std::int64_t last_briefing_day_{-1};
    bool was_dark_{false};
    std::uint64_t pair_ready_ms_{0};
    std::uint64_t update_check_ms_{0};
    std::uint64_t update_started_ms_{0};
    std::uint64_t update_verify_ms_{0};
    std::uint64_t chime_next_ms_{0};
    std::uint64_t alert_since_ms_{0};
    MessageChimeGate message_chime_;
    std::uint64_t chime_aoraton_sequence_{0};
    std::uint64_t chime_phone_sequence_{0};
    int chime_demo_count_{-1};
    std::uint32_t imu_time_ms_{0};
    std::uint32_t next_phone_uid_{100};
    std::uint32_t note_file_counter_{0};
    std::uint64_t saved_clock_sequence_{0};
    std::uint64_t saved_notes_sequence_{0};
    std::string saved_clock_text_;
    bool hold_transcribing_{false};
    SoundSettings sound_{};
    std::string saved_sound_text_;
    bool powered_off_{false};
};

}  // namespace aura::simulator

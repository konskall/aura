#pragma once
#include "esp32_photo_storage.hpp"
#include "esp32_file_storage.hpp"
#include "esp32_file_transfer.hpp"

#include "aura/app_layout.hpp"
#include "aura/assistant_companion.hpp"
#include "aura/aura_ui.hpp"
#include "aura/clock_services.hpp"
#include "aura/cloud_keys.hpp"
#include "aura/conversation_log.hpp"
#include "aura/device_status.hpp"
#include "aura/display_power.hpp"
#include "aura/notes.hpp"
#include "aura/organizer.hpp"
#include "aura/ota_update.hpp"
#include "aura/phone_link.hpp"
#include "aura/provider.hpp"
#include "aura/sounds.hpp"
#include "aura/voice.hpp"
#include "aura_board_chips.hpp"
#include "esp32_microphone.hpp"
#include "esp32_motion.hpp"
#include "esp32_ota.hpp"
#include "esp32_phone_link.hpp"

#include <array>
#include <cstdint>
#include <ctime>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace aura {
class IAudioService;
}

namespace aura::firmware {

// The v13 "Companion" features on the device: clock services, notes (voice
// notes on microSD), display power with the QMI8658, the iPhone link and OTA.
// Every method runs on the UI loop under the BSP display lock. The assistant's
// spoken questions are recorded here (the microphone is shared with notes) and
// handed to app_main's VoiceAssistantTurn; speech recognition and spoken
// replies are on once the device has its cloud keys.
// Compiles with ESP-IDF 5.5.5; NOT VERIFIED ON HARDWARE.
class Esp32Companion {
public:
    explicit Esp32Companion(IAudioService& audio, DeviceStatusModel& status);
    void start();
    void install(AuraUiCallbacks& callbacks, IAoratonService* aoraton = nullptr);
    void poll(std::uint32_t elapsed_ms);
    void apply(AuraUi& ui);
    InterfaceLanguage language() const noexcept { return language_; }
    // The stored look (display.txt), restored by start() before the UI exists.
    Appearance appearance() const noexcept { return display_.snapshot().settings.appearance; }
    // The wall clock and the real time it was read from (alarms tell a time
    // change from time passing by the real minute).
    void set_clock(const std::tm& local, std::time_t utc);
    // The screen's power mode (the main loop and the processor follow it).
    DisplayMode display_mode() const noexcept { return display_.snapshot().mode; }
    // A spoken question: recorded until the speaker stops or end_question(),
    // then handed to the sink as a WAV. False: no microphone, or it is busy.
    bool begin_question();
    void end_question();
    void cancel_question();
    // The WAV, and whether any speech was heard in it.
    void set_question_sink(std::function<void(std::vector<std::uint8_t>, bool)> sink) {
        question_sink_ = std::move(sink);
    }
    // An aoraton voice message, dictated like a note (up to 30 s); its WAV
    // goes to the sink for the service's confirmation.
    bool begin_voice_message();
    void end_voice_message();
    void cancel_voice_message();
    void set_message_sink(std::function<void(std::vector<std::uint8_t>)> sink) { message_sink_ = std::move(sink); }
    // Speech recognition and spoken replies, once the cloud keys are there.
    void set_speech_available(bool recognition, bool output);
    // The assistant's voice is on: the read-aloud buttons become stop buttons.
    void set_speaking(bool speaking) {
        if (extras_.speaking == speaking) return;
        extras_.speaking = speaking;
        extras_.voice_on = speaking;
        ++extras_.sequence;
        extras_dirty_ = true;
    }
    // What the owner and the assistant said: kept on the card, to read again.
    void record_conversation(const std::string& question, const std::string& answer);
    void clear_conversation();
    // An action the assistant proposed, kept for the confirmation screen.
    void propose_action(const AssistantAction& action);
    // The owner confirmed it: true when it was done. Declined: dropped.
    bool confirm_action();
    void decline_action();
    // Carries out an action: confirmed on the screen, or said outright by the
    // owner (parse_offline_command). True when it was done.
    bool perform_action(const AssistantAction& action);
    // A restart or a switch-off, its screen at once and the rest outside the
    // display lock (run_deferred_system_action).
    void system_action(SystemAction action);
    // The assistant's speech service (null without the cloud keys): voice
    // notes are transcribed with it, as in the simulator.
    void set_speech_service(ISpeechService* speech) noexcept { speech_ = speech; }
    // Barge-in: while a spoken answer plays the microphone listens for the
    // owner speaking over it. take_barge_in() is true once, when they did.
    void set_barge_monitor(bool on);
    bool barge_monitoring() const noexcept { return barge_monitoring_; }
    bool take_barge_in() noexcept;
    // The Sound settings the voice turn follows (spoken answers, the voice).
    const SoundSettings& sound() const noexcept { return sound_; }
    // The microphone records (a note, a question, a voice message): the radio
    // is silenced meanwhile, so its sound does not end up in the recording.
    bool microphone_active() const noexcept { return voice_.state == VoiceCaptureState::Recording; }
    // Small text files on the microSD card (the radio's favourites).
    std::string load_text(const char* name) const { return store_.read(name); }
    bool save_text(const char* name, const std::string& data) const { return store_.write(name, data); }
    // The microSD card for Κατάσταση συσκευής.
    bool card_mounted() const noexcept { return store_.mounted(); }
    bool card_space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) const {
        return store_.space(total_bytes, free_bytes);
    }
    // Where the transfer session's POST /api/keys puts the assistant's keys.
    void set_cloud_key_sink(ICloudKeySink* sink) { transfer_.set_key_sink(sink); }
    // The PMU, for the PWR key and switching off; null without one.
    void set_power_key(Axp2101Power* power) { power_ = power; }
    // The PC-assisted aoraton.com sign-in over the same session (ADR 0010).
    void set_aoraton_link(IAoratonLinkEndpoint* link) { transfer_.set_aoraton_link(link); }
    // What the phone sign-in follows: File transfer's server (the UI thread).
    TransferSnapshot transfer_snapshot() { return transfer_.session().snapshot(); }
    // The models the assistant's device facts come from.
    const ClockServicesModel& clock() const noexcept { return clock_; }
    const NotesModel& notes() const noexcept { return notes_; }
    const OrganizerModel& organizer() const noexcept { return organizer_; }
    const PhoneNotificationModel& phone() const noexcept { return phone_; }
    // Seconds of healthy running confirm a freshly installed image.
    void confirm_image_after(std::uint32_t uptime_ms);
    // Executes confirmed restart/power/factory actions outside the LVGL lock.
    void run_deferred_system_action();

private:
    std::int64_t now_minute() const;
    std::uint64_t now_ms() const;
    // The bar of the screen a confirmed action shows while it finishes (the UI's
    // show_system_progress), outside the display lock.
    void system_progress(int percent);
    // That screen for `ms` more: the bar glides to `percent`, the core turns.
    void hold_system_screen(int percent, std::uint32_t ms);
    int system_percent_{0};
    void begin_note();
    void finish_note();
    void save();
    enum class StoreJobKind { Clock, Notes, Organizer, Conversations, Sound, Display, Language, Layout, Voice, DeleteVoice,
                              AoratonSync };
    struct StoreJob {
        StoreJobKind kind{StoreJobKind::Clock};
        std::string name;
        std::string text;
        std::vector<std::uint8_t> audio;
        std::uint64_t sequence{0};
        std::uint32_t duration_ms{0};
        std::int64_t minute{0};
        bool success{false};
        bool transcribe{false};  // a voice note heard speaking: keep its WAV for the transcript
    };
    static void storage_task_entry(void* context);
    void storage_task();
    bool enqueue(StoreJob* job);
    void poll_storage_results();

    IAudioService& audio_;
    DeviceStatusModel& status_;
    ClockServicesModel clock_;
    NotesModel notes_;
    OrganizerModel organizer_;
    LauncherLayoutModel launcher_layout_;
    PhoneNotificationModel phone_;
    OtaUpdateModel ota_;
    DisplayPowerModel display_;
    ImuGestureDetector imu_;
    // The screen turns with the device (the owner's request, 2026-11-14).
    OrientationTracker orientation_;
    int applied_rotation_{0};
    VoiceRecorder recorder_;
    // The microphone's raw level over a spoken question, for the log: some
    // questions on the board came back from Whisper as its words for silence
    // (2026-11-16). Numbers only, never the sound.
    struct InputLevel {
        std::uint32_t samples{0};
        std::uint32_t zeros{0};
        std::int32_t peak{0};
        double energy{0.0};
        // The first two seconds in tenths: where the voice begins, and
        // whether the ES7210's settling or the speaker's tail shows.
        std::array<std::uint16_t, 20> tenths{};
        double tenth_energy{0.0};
    };
    InputLevel question_level_{};
    bool recording_question_{false};
    std::function<void(std::vector<std::uint8_t>, bool)> question_sink_;
    bool recording_message_{false};
    std::function<void(std::vector<std::uint8_t>)> message_sink_;
    VoiceSnapshot voice_{};
    // What the UI shows beside the assistant's state: the proposed action and
    // the providers (Κατάσταση συσκευής).
    AssistantExtras extras_{};
    ISpeechService* speech_{nullptr};
    NoteTranscriptions note_transcriptions_;
    // Barge-in, with the speaker's own signal as reference (ES7210 MIC3); in
    // PSRAM (the filter and the owner's first words).
    std::unique_ptr<EchoReferenceGate> barge_gate_;
    std::size_t barge_words_next_{0};   // where the ring of barge_words_ goes on
    std::size_t barge_words_count_{0};  // how much of it is the owner's so far
    std::vector<std::int16_t> reference_;
    // The last second without the device's voice: the words that interrupted
    // it begin the question.
    std::vector<std::int16_t> barge_words_;
    bool barge_monitoring_{false};
    bool barge_heard_{false};
    bool barge_triggered_{false};  // the monitor's microphone stays open for the question
    bool extras_dirty_{true};
    CompanionCapabilities capabilities_{};
    bool capabilities_dirty_{true};
    Esp32Motion motion_;
    Esp32PhoneLink phone_link_;
    Esp32Ota updater_;
    Esp32Microphone microphone_;
    Esp32CompanionStore store_;
    PhotoMailbox photo_mailbox_;
    PhotoModel photos_{photo_mailbox_};
    Esp32PhotoStorage photo_storage_{store_};
    PhotoWorker photo_worker_{photo_mailbox_, photo_storage_};
    FileMailbox file_mailbox_;
    FileBrowserModel files_{file_mailbox_};
    Esp32FileStorage file_storage_{store_};
    FileWorker file_worker_{file_mailbox_, file_storage_};
    std::uint64_t files_erased_{0};
    ConversationLog conversation_log_;
    std::uint64_t saved_conversation_{0};
    bool conversation_save_pending_{false};
    Esp32FileTransfer transfer_;
    AuraUi* active_ui_{nullptr};
    std::uint64_t transfer_photo_revision_{0};
    std::uint64_t transfer_removal_revision_{0};
    bool transfer_removals_pending_{false};
    bool transfer_photos_pending_{false};
    std::vector<ImuSample> imu_samples_;
    std::vector<std::int16_t> samples_;
    bool recording_note_{false};
    bool image_confirmed_{false};
    int applied_brightness_{-1};
    // LVGL's inactive time at the last poll: a smaller value means a touch.
    std::uint32_t last_inactive_ms_{0};
    // The volume keys: pressed since, and the next repeat while held.
    struct VolumeKey {
        bool down{false};
        std::uint64_t next_ms{0};
    };
    void poll_volume_key(VolumeKey& key, bool down, int direction, std::uint64_t now);
    VolumeKey volume_down_key_;
    VolumeKey volume_up_key_;
    Axp2101Power* power_{nullptr};
    std::uint64_t power_key_polled_ms_{0};
    AlarmRinger alarm_ringer_;  // one melody pass per cycle, through mute
    MessageChimeGate message_chime_;
    SoundSettings sound_{};
    IAoratonService* aoraton_{nullptr};
    AoratonSyncSettings aoraton_sync_;
    std::string saved_aoraton_sync_text_;
    bool aoraton_sync_save_pending_{false};
    std::uint64_t chime_phone_sequence_{0};
    std::uint64_t chime_aoraton_sequence_{0};
    std::uint32_t note_counter_{0};
    std::uint64_t saved_clock_{0};
    std::uint64_t saved_notes_{0};
    std::uint64_t saved_organizer_{0};
    std::string saved_clock_text_;
    std::string saved_notes_text_;
    std::string saved_organizer_text_;
    std::string saved_sound_text_;
    SettingSettle sound_settle_;  // a dragged volume slider is written once it rests
    // Settings are compared as values each poll; text is built only after a change.
    SoundSettings seen_sound_{};
    bool sound_pending_{false};
    std::uint64_t saved_display_sequence_{0};
    std::uint64_t saved_layout_sequence_{0};
    std::string saved_display_text_;
    std::string saved_language_text_;
    std::string saved_layout_text_;
    bool clock_save_pending_{false};
    bool notes_save_pending_{false};
    bool organizer_save_pending_{false};
    bool sound_save_pending_{false};
    bool display_save_pending_{false};
    bool language_save_pending_{false};
    bool layout_save_pending_{false};
    QueueHandle_t storage_jobs_{nullptr};
    QueueHandle_t storage_results_{nullptr};
    TaskHandle_t storage_task_handle_{nullptr};
    std::atomic<bool> storage_busy_{false};
    std::atomic<bool> storage_accepting_{true};
    std::atomic<int> pending_system_action_{-1};
    InterfaceLanguage language_{InterfaceLanguage::Greek};
};

}  // namespace aura::firmware

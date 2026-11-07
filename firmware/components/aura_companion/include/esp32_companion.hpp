#pragma once
#include "esp32_photo_storage.hpp"
#include "esp32_file_transfer.hpp"

#include "aura/app_layout.hpp"
#include "aura/aura_ui.hpp"
#include "aura/clock_services.hpp"
#include "aura/cloud_keys.hpp"
#include "aura/device_status.hpp"
#include "aura/display_power.hpp"
#include "aura/notes.hpp"
#include "aura/organizer.hpp"
#include "aura/ota_update.hpp"
#include "aura/phone_link.hpp"
#include "aura/sounds.hpp"
#include "aura/voice.hpp"
#include "esp32_microphone.hpp"
#include "esp32_motion.hpp"
#include "esp32_ota.hpp"
#include "esp32_phone_link.hpp"

#include <cstdint>
#include <ctime>
#include <atomic>
#include <functional>
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
    void set_question_sink(std::function<void(std::vector<std::uint8_t>)> sink) { question_sink_ = std::move(sink); }
    // An aoraton voice message, dictated like a note (up to 30 s); its WAV
    // goes to the sink for the service's confirmation.
    bool begin_voice_message();
    void end_voice_message();
    void cancel_voice_message();
    void set_message_sink(std::function<void(std::vector<std::uint8_t>)> sink) { message_sink_ = std::move(sink); }
    // Speech recognition and spoken replies, once the cloud keys are there.
    void set_speech_available(bool recognition, bool output);
    // The microphone records (a note, a question, a voice message): the radio
    // is silenced meanwhile, so its sound does not end up in the recording.
    bool microphone_active() const noexcept { return voice_.state == VoiceCaptureState::Recording; }
    // Small text files on the microSD card (the radio's favourites).
    std::string load_text(const char* name) const { return store_.read(name); }
    bool save_text(const char* name, const std::string& data) const { return store_.write(name, data); }
    // Where the transfer session's POST /api/keys puts the assistant's keys.
    void set_cloud_key_sink(ICloudKeySink* sink) { transfer_.set_key_sink(sink); }
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
    void begin_note();
    void finish_note();
    void save();
    enum class StoreJobKind { Clock, Notes, Organizer, Sound, Display, Language, Layout, Voice, DeleteVoice, AoratonSync };
    struct StoreJob {
        StoreJobKind kind{StoreJobKind::Clock};
        std::string name;
        std::string text;
        std::vector<std::uint8_t> audio;
        std::uint64_t sequence{0};
        std::uint32_t duration_ms{0};
        std::int64_t minute{0};
        bool success{false};
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
    VoiceRecorder recorder_;
    bool recording_question_{false};
    std::function<void(std::vector<std::uint8_t>)> question_sink_;
    bool recording_message_{false};
    std::function<void(std::vector<std::uint8_t>)> message_sink_;
    VoiceSnapshot voice_{};
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
    bool button_down_{false};
    std::uint64_t button_since_ms_{0};
    AlarmRinger alarm_ringer_;  // one melody pass per cycle, through mute
    MessageChimeGate message_chime_;
    SoundSettings sound_{};
    IAoratonService* aoraton_{nullptr};
    AoratonSyncSettings aoraton_sync_;
    std::string saved_aoraton_sync_text_;
    bool aoraton_sync_save_pending_{false};
    std::uint64_t chime_phone_sequence_{0};
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

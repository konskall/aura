#pragma once

#include "aura/aura_ui.hpp"
#include "aura/clock_services.hpp"
#include "aura/device_status.hpp"
#include "aura/display_power.hpp"
#include "aura/notes.hpp"
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
#include <string>
#include <vector>

namespace aura {
class IAudioService;
}

namespace aura::firmware {

// The v13 "Companion" features on the device: clock services, notes (voice
// notes on microSD), display power with the QMI8658, the iPhone link and OTA.
// Every method runs on the UI loop under the BSP display lock. The assistant's
// voice, translation and spoken replies need the AI gateway (not yet chosen),
// so their capabilities stay off. NOT COMPILED, NOT VERIFIED ON HARDWARE.
class Esp32Companion {
public:
    explicit Esp32Companion(IAudioService& audio, DeviceStatusModel& status);
    void start();
    void install(AuraUiCallbacks& callbacks);
    void poll(std::uint32_t elapsed_ms);
    void apply(AuraUi& ui);
    void set_clock(const std::tm& local);
    // Seconds of healthy running confirm a freshly installed image.
    void confirm_image_after(std::uint32_t uptime_ms);

private:
    std::int64_t now_minute() const;
    std::uint64_t now_ms() const;
    void begin_note();
    void finish_note();
    void save();

    IAudioService& audio_;
    DeviceStatusModel& status_;
    ClockServicesModel clock_;
    NotesModel notes_;
    PhoneNotificationModel phone_;
    OtaUpdateModel ota_;
    DisplayPowerModel display_;
    ImuGestureDetector imu_;
    VoiceRecorder recorder_;
    VoiceSnapshot voice_{};
    CompanionCapabilities capabilities_{};
    bool capabilities_dirty_{true};
    Esp32Motion motion_;
    Esp32PhoneLink phone_link_;
    Esp32Ota updater_;
    Esp32Microphone microphone_;
    Esp32CompanionStore store_;
    std::vector<ImuSample> imu_samples_;
    std::vector<std::int16_t> samples_;
    bool recording_note_{false};
    bool image_confirmed_{false};
    int applied_brightness_{-1};
    bool button_down_{false};
    std::uint64_t button_since_ms_{0};
    std::uint64_t chime_next_ms_{0};
    std::uint64_t alert_since_ms_{0};
    MessageChimeGate message_chime_;
    std::uint64_t chime_phone_sequence_{0};
    std::uint32_t note_counter_{0};
    std::uint64_t saved_clock_{0};
    std::uint64_t saved_notes_{0};
    std::string saved_clock_text_;
};

}  // namespace aura::firmware

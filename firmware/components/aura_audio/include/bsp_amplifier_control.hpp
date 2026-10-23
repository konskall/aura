#pragma once

#include "esp_codec_dev.h"

namespace aura::firmware {

enum class AmplifierStatus { Uninitialized, Disabled, Enabled, Muted, Error };

// GPIO46 ownership remains inside the official BSP/esp_codec_dev ES8311 driver.
// This adapter deliberately never configures or writes the strapping pin.
class BspAmplifierControl {
public:
    bool initialize(esp_codec_dev_handle_t codec) noexcept;
    bool enable() noexcept;
    bool disable() noexcept;
    bool mute() noexcept;
    bool unmute() noexcept;
    void shutdown() noexcept;
    AmplifierStatus status() const noexcept { return status_; }

private:
    bool set_codec_mute(bool muted) noexcept;
    esp_codec_dev_handle_t codec_{nullptr};
    AmplifierStatus status_{AmplifierStatus::Uninitialized};
};

}  // namespace aura::firmware

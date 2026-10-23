#include "bsp_amplifier_control.hpp"

namespace aura::firmware {

bool BspAmplifierControl::initialize(esp_codec_dev_handle_t codec) noexcept {
    codec_ = codec;
    if (!codec_) {
        status_ = AmplifierStatus::Error;
        return false;
    }
    return disable();
}

bool BspAmplifierControl::set_codec_mute(bool muted) noexcept {
    if (!codec_ || esp_codec_dev_set_out_mute(codec_, muted) != ESP_CODEC_DEV_OK) {
        status_ = AmplifierStatus::Error;
        return false;
    }
    status_ = muted ? AmplifierStatus::Muted : AmplifierStatus::Enabled;
    return true;
}

bool BspAmplifierControl::enable() noexcept { return set_codec_mute(false); }
bool BspAmplifierControl::disable() noexcept { return set_codec_mute(true); }
bool BspAmplifierControl::mute() noexcept { return set_codec_mute(true); }
bool BspAmplifierControl::unmute() noexcept { return set_codec_mute(false); }

void BspAmplifierControl::shutdown() noexcept {
    if (codec_) set_codec_mute(true);
    codec_ = nullptr;
    status_ = AmplifierStatus::Uninitialized;
}

}  // namespace aura::firmware

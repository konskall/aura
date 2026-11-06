#pragma once

#include "aura/voice_decoder.hpp"

namespace aura::firmware {

// esp_audio_codec's AAC (fed as ADTS frames) and Opus decoders for voice
// messages. Its decoders need about 20 KiB of stack, so each message decodes
// on a short-lived thread whose stack is in PSRAM; the account worker's own
// stack stays small. The PCM buffer is checked against the heap before it grows.
class Esp32VoiceDecoder final : public IVoiceDecoder {
public:
    bool decode(const std::uint8_t* data, const VoiceTrack& track, PcmClip& clip) override;
};

}  // namespace aura::firmware

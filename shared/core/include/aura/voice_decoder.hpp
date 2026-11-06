#pragma once

#include "aura/audio_service.hpp"
#include "aura/voice_container.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {

// The platform's decoders for voice messages: Media Foundation in the
// simulator, esp_audio_codec on the device. PCM (AURA's WAV) needs none.
class IVoiceDecoder {
public:
    virtual ~IVoiceDecoder() = default;
    // The AAC or Opus frames of `track`, which point into `data`, to 16-bit
    // PCM; false when this platform has no decoder for the codec.
    virtual bool decode(const std::uint8_t* data, const VoiceTrack& track, PcmClip& clip) = 0;
};

// Voice is kept at 16 kHz mono: a minute of 48 kHz stereo would not fit the
// device's PSRAM. Frames arrive as decoders produce them; each output sample
// is the average of the input it covers (channels averaged too). Rates at or
// below the output rate pass through unchanged.
class VoiceResampler {
public:
    explicit VoiceResampler(std::uint32_t in_rate, std::uint32_t channels, std::uint32_t out_rate = 16000);
    void push(const std::int16_t* interleaved, std::size_t frames);
    std::vector<std::int16_t>& output() noexcept { return output_; }
    std::uint32_t rate() const noexcept { return in_rate_ > out_rate_ ? out_rate_ : in_rate_; }

private:
    std::uint32_t in_rate_;
    std::uint32_t channels_;
    std::uint32_t out_rate_;
    std::uint64_t position_{0};
    std::int64_t sum_{0};
    std::uint32_t count_{0};
    std::vector<std::int16_t> output_;
};

// A voice message's file to mono 16-bit PCM of at most `max_seconds`. On
// failure `clip` is empty and `error` is a code: VOICE_DAMAGED (unreadable),
// VOICE_UNSUPPORTED (no decoder here) or VOICE_TOO_LONG.
// (16 kHz at most).
bool voice_decode(const std::uint8_t* data, std::size_t size, IVoiceDecoder* decoder, PcmClip& clip,
                  std::string& error, std::uint32_t max_seconds = 300);

}  // namespace aura

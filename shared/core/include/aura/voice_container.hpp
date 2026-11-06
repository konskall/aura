#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aura {

// The containers aoraton's browsers record voice messages in (MediaRecorder):
// MP4 (Safari's AAC; Chrome's Opus or AAC; whole or fragmented), WebM
// (Firefox's Opus, often with unknown sizes) and WAV (AURA's own). Demuxing
// only: the codec frames go to the platform's decoder. Bounded and strict:
// anything malformed or truncated is refused whole.
enum class VoiceCodec { None, Aac, Opus, Pcm16 };

struct VoiceFrame {
    std::size_t offset{0};  // into the demuxed buffer, which the caller keeps
    std::size_t size{0};
};

struct VoiceTrack {
    VoiceCodec codec{VoiceCodec::None};
    std::uint32_t sample_rate{0};
    std::uint8_t channels{0};
    std::uint16_t pre_skip{0};          // Opus: samples (at 48 kHz) to drop first
    std::vector<std::uint8_t> config;   // AAC: AudioSpecificConfig; Opus: OpusHead (WebM)
    std::vector<VoiceFrame> frames;     // PCM: one frame holding all the samples
    std::uint32_t duration_ms{0};
};

constexpr std::size_t kMaxVoiceFrames = 20000;  // over five minutes of 20 ms packets

bool voice_demux(const std::uint8_t* data, std::size_t size, VoiceTrack& track);

// A raw AAC frame with the ADTS header its decoders accept, from the
// AudioSpecificConfig (object type, sampling index, channels).
bool voice_adts(const std::vector<std::uint8_t>& audio_specific_config, const std::uint8_t* frame, std::size_t size,
                std::vector<std::uint8_t>& adts);

// A RIFF/WAVE file of 16-bit mono PCM: what AURA sends, since every browser plays it.
bool voice_wav(const std::int16_t* samples, std::size_t count, std::uint32_t sample_rate, std::vector<std::uint8_t>& wav);

}  // namespace aura

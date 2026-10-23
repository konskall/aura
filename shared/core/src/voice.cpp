#include "aura/voice.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aura {
namespace {

constexpr std::size_t kFrameSamples = VoiceRecorder::kSampleRate / 50;  // 20 ms
constexpr std::uint32_t kFrameMs = 20;

void put_u16(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
}

void put_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    put_u16(output, value & 0xFFFFU);
    put_u16(output, (value >> 16) & 0xFFFFU);
}

std::uint32_t read_u32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint16_t read_u16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

}  // namespace

void VoiceRecorder::start() {
    pcm_.clear();
    pcm_.reserve(kSampleRate * 4);
    frame_.clear();
    recording_ = true;
    speech_detected_ = false;
    level_ = 0.0F;
    noise_floor_ = 120.0F;
    silence_ms_ = 0;
}

void VoiceRecorder::append(const std::int16_t* samples, std::size_t count) {
    if (!recording_ || !samples || count == 0) return;
    const std::size_t limit = static_cast<std::size_t>(kSampleRate) * kMaxMs / 1000U;
    const std::size_t room = limit > pcm_.size() ? limit - pcm_.size() : 0;
    const std::size_t take = std::min(count, room);
    pcm_.insert(pcm_.end(), samples, samples + take);
    for (std::size_t index = 0; index < take; ++index) {
        frame_.push_back(samples[index]);
        if (frame_.size() == kFrameSamples) {
            analyse_frame(frame_.data(), frame_.size());
            frame_.clear();
        }
    }
}

void VoiceRecorder::analyse_frame(const std::int16_t* samples, std::size_t count) {
    double energy = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const double value = samples[index];
        energy += value * value;
    }
    const auto rms = static_cast<float>(std::sqrt(energy / static_cast<double>(count)));
    // Fast attack, slow release, for a calm halo.
    const float target = std::clamp(rms / 4000.0F, 0.0F, 1.0F);
    level_ = std::max(target, level_ * 0.85F);
    const float threshold = std::max(noise_floor_ * 3.0F, 450.0F);
    if (rms > threshold) {
        speech_detected_ = true;
        silence_ms_ = 0;
    } else {
        if (speech_detected_) silence_ms_ += kFrameMs;
        noise_floor_ = noise_floor_ * 0.95F + rms * 0.05F;
    }
}

void VoiceRecorder::cancel() {
    recording_ = false;
    pcm_.clear();
    frame_.clear();
    level_ = 0.0F;
}

std::vector<std::uint8_t> VoiceRecorder::finish_wav() {
    recording_ = false;
    auto wav = make_wav(pcm_, kSampleRate);
    pcm_.clear();
    frame_.clear();
    level_ = 0.0F;
    return wav;
}

bool VoiceRecorder::no_speech() const noexcept { return !speech_detected_ && elapsed_ms() >= kNoSpeechMs; }

bool VoiceRecorder::should_stop() const noexcept {
    return elapsed_ms() >= kMaxMs || (speech_detected_ && silence_ms_ >= kSilenceEndMs) || no_speech();
}

std::uint32_t VoiceRecorder::elapsed_ms() const noexcept {
    return static_cast<std::uint32_t>(pcm_.size() * 1000U / kSampleRate);
}

std::vector<std::uint8_t> make_wav(const std::vector<std::int16_t>& pcm, std::uint32_t sample_rate) {
    const auto data_bytes = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    std::vector<std::uint8_t> wav;
    wav.reserve(44 + data_bytes);
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    put_u32(wav, 36 + data_bytes);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put_u32(wav, 16);
    put_u16(wav, 1);  // PCM
    put_u16(wav, 1);  // mono
    put_u32(wav, sample_rate);
    put_u32(wav, sample_rate * 2);
    put_u16(wav, 2);
    put_u16(wav, 16);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    put_u32(wav, data_bytes);
    for (const auto sample : pcm) put_u16(wav, static_cast<std::uint16_t>(sample));
    return wav;
}

bool parse_wav(const std::uint8_t* data, std::size_t size, PcmClip& clip) {
    if (!data || size < 44 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return false;
    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    std::uint32_t rate = 0;
    bool format_ok = false;
    std::size_t position = 12;
    while (position + 8 <= size) {
        const std::uint8_t* id = data + position;
        const std::uint32_t length = read_u32(data + position + 4);
        position += 8;
        if (length > size - position) return false;
        if (std::memcmp(id, "fmt ", 4) == 0 && length >= 16) {
            const auto format = read_u16(data + position);
            channels = read_u16(data + position + 2);
            rate = read_u32(data + position + 4);
            bits = read_u16(data + position + 14);
            format_ok = (format == 1 || format == 0xFFFE) && bits == 16 && (channels == 1 || channels == 2) &&
                        rate >= 8000 && rate <= 48000;
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (!format_ok) return false;
            const std::size_t frames = length / (2U * channels);
            clip.format = {rate, 1, 16};
            clip.samples.resize(frames);
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const std::uint8_t* sample = data + position + frame * 2U * channels;
                int value = static_cast<std::int16_t>(read_u16(sample));
                if (channels == 2) value = (value + static_cast<std::int16_t>(read_u16(sample + 2))) / 2;
                clip.samples[frame] = static_cast<std::int16_t>(value);
            }
            return frames > 0;
        }
        position += length + (length & 1U);
    }
    return false;
}

}  // namespace aura

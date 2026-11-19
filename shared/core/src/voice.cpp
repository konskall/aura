#include "aura/voice.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

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

void VoiceFrontEnd::reset() noexcept {
    previous_input_ = 0.0F;
    previous_high_pass_ = 0.0F;
    suppression_ = 1.0F;
    diagnostics_ = VoiceFrontEndDiagnostics{};
}

void VoiceFrontEnd::process(const std::int16_t* samples, std::size_t count,
                            std::vector<std::int16_t>& output) {
    output.clear();
    if (!samples || count == 0) return;
    output.resize(count);
    double energy = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const float input = static_cast<float>(samples[index]);
        const float high_pass = input - previous_input_ + 0.995F * previous_high_pass_;
        previous_input_ = input;
        previous_high_pass_ = high_pass;
        const float bounded = std::clamp(high_pass, -32768.0F, 32767.0F);
        output[index] = static_cast<std::int16_t>(bounded);
        energy += static_cast<double>(bounded) * bounded;
    }
    const float rms = static_cast<float>(std::sqrt(energy / static_cast<double>(count)));
    const float noise = std::max(diagnostics_.noise_rms, 30.0F);
    const float ratio = rms / noise;
    diagnostics_.speech_probability = std::clamp((ratio - 1.3F) / 3.0F, 0.0F, 1.0F);
    if (diagnostics_.speech_probability < 0.25F) {
        diagnostics_.noise_rms = std::clamp(noise * 0.97F + rms * 0.03F, 30.0F, 6000.0F);
    }

    float suppression_target = 1.0F;
    if (diagnostics_.speech_probability < 0.15F) suppression_target = 0.25F;
    else if (diagnostics_.speech_probability < 0.45F) {
        suppression_target = 0.25F + (diagnostics_.speech_probability - 0.15F) / 0.30F * 0.75F;
    }
    const float suppression_rate = suppression_target < suppression_ ? 0.28F : 0.10F;
    suppression_ += (suppression_target - suppression_) * suppression_rate;

    float target_gain = std::clamp(4200.0F / std::max(rms, 1.0F), 0.65F, 3.2F);
    if (diagnostics_.speech_probability < 0.35F) target_gain = std::min(target_gain, 1.25F);
    const float gain_rate = target_gain < diagnostics_.gain ? 0.22F : 0.04F;
    diagnostics_.gain += (target_gain - diagnostics_.gain) * gain_rate;
    const float total_gain = diagnostics_.gain * suppression_;
    for (auto& sample : output) {
        sample = static_cast<std::int16_t>(std::clamp(static_cast<float>(sample) * total_gain,
                                                      -32768.0F, 32767.0F));
    }
}

void AcousticEchoCanceller::reset() noexcept {
    offset_ = 0;
    echo_gain_ = 0.0F;
    suppression_ = 0.0F;
    reference_active_ = false;
}

namespace {

// Normalised correlation of the microphone with one alignment of the
// reference, sampled every `step` samples.
double coherence_at(const std::int16_t* microphone, const std::int16_t* reference, std::size_t count,
                    std::size_t step) noexcept {
    double mic_energy = 0.0;
    double reference_energy = 0.0;
    double correlation = 0.0;
    for (std::size_t index = 0; index < count; index += step) {
        const double mic = microphone[index];
        const double rendered = reference[index];
        mic_energy += mic * mic;
        reference_energy += rendered * rendered;
        correlation += mic * rendered;
    }
    return std::abs(correlation) / std::sqrt(std::max(1.0, mic_energy * reference_energy));
}

}  // namespace

bool AcousticEchoCanceller::process(const std::int16_t* microphone, const std::int16_t* playback_reference,
                                    std::size_t count, std::vector<std::int16_t>& output) noexcept {
    return process(microphone, playback_reference, count, 0, output);
}

bool AcousticEchoCanceller::process(const std::int16_t* microphone, const std::int16_t* playback_reference,
                                    std::size_t count, std::size_t delay_span,
                                    std::vector<std::int16_t>& output) noexcept {
    output.clear();
    if (!microphone || count == 0) return false;
    output.resize(count);
    if (!playback_reference) {
        std::copy(microphone, microphone + count, output.begin());
        reference_active_ = false;
        suppression_ = 0.0F;
        return false;
    }
    // Find the echo in the reference window: coarsely over the whole span,
    // then sample-exact around the best candidate. Speech is smooth enough
    // for an 8-sample coarse step; a new alignment must clearly beat the
    // tracked one, so the estimate does not jump with every block.
    offset_ = std::min(offset_, delay_span);
    if (delay_span > 0) {
        std::size_t best = offset_;
        const double tracked_score = coherence_at(microphone, playback_reference + offset_, count, 2);
        double coarse_score = 0.0;
        std::size_t coarse = offset_;
        for (std::size_t candidate = 0; candidate <= delay_span; candidate += 8) {
            const double score = coherence_at(microphone, playback_reference + candidate, count, 2);
            if (score > coarse_score) {
                coarse_score = score;
                coarse = candidate;
            }
        }
        if (coarse_score > tracked_score + 0.05) best = coarse;
        const std::size_t from = best > 8 ? best - 8 : 0;
        const std::size_t to = std::min(delay_span, best + 8);
        double fine_score = -1.0;
        for (std::size_t candidate = from; candidate <= to; ++candidate) {
            const double score = coherence_at(microphone, playback_reference + candidate, count, 1);
            if (score > fine_score) {
                fine_score = score;
                best = candidate;
            }
        }
        offset_ = best;
        playback_reference += offset_;
    }
    double mic_energy = 0.0;
    double reference_energy = 0.0;
    double correlation = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const double mic = microphone[index];
        const double reference = playback_reference[index];
        mic_energy += mic * mic;
        reference_energy += reference * reference;
        correlation += mic * reference;
    }
    const double coherence = std::abs(correlation) /
        std::sqrt(std::max(1.0, mic_energy * reference_energy));
    reference_active_ = reference_energy / static_cast<double>(count) > 400.0 * 400.0 && coherence >= 0.20;
    if (reference_active_) {
        const float target = static_cast<float>(std::clamp(correlation / reference_energy, -2.0, 2.0));
        echo_gain_ += (target - echo_gain_) * (std::abs(echo_gain_) < 0.01F ? 1.0F : 0.35F);
    } else {
        echo_gain_ *= 0.8F;
    }
    double residual_energy = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const float value = static_cast<float>(microphone[index]) -
                            (reference_active_ ? echo_gain_ * playback_reference[index] : 0.0F);
        output[index] = static_cast<std::int16_t>(std::clamp(value, -32768.0F, 32767.0F));
        residual_energy += static_cast<double>(output[index]) * output[index];
    }
    suppression_ = reference_active_ && mic_energy > 1.0 ?
        static_cast<float>(std::clamp(1.0 - residual_energy / mic_energy, 0.0, 1.0)) : 0.0F;
    return reference_active_;
}

void VoiceRecorder::start(Mode mode) {
    mode_ = mode;
    pcm_.clear();
    pcm_.reserve(kSampleRate * 4);
    frame_.clear();
    front_end_.reset();
    dc_input_ = 0.0F;
    dc_output_ = 0.0F;
    recording_ = true;
    speech_detected_ = false;
    level_ = 0.0F;
    silence_ms_ = 0;
    candidate_ms_ = 0;
}

void VoiceRecorder::append(const std::int16_t* samples, std::size_t count) {
    if (!recording_ || !samples || count == 0) return;
    const std::size_t limit = static_cast<std::size_t>(kSampleRate) * max_ms() / 1000U;
    const std::size_t buffered = pcm_.size() + frame_.size();
    const std::size_t room = limit > buffered ? limit - buffered : 0;
    const std::size_t take = std::min(count, room);
    for (std::size_t index = 0; index < take; ++index) {
        frame_.push_back(samples[index]);
        if (frame_.size() == kFrameSamples) {
            keep(frame_.data(), frame_.size());
            front_end_.process(frame_.data(), frame_.size(), processed_);
            analyse_frame(processed_.data(), processed_.size());
            frame_.clear();
        }
    }
}

void VoiceRecorder::keep(const std::int16_t* samples, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        const float input = static_cast<float>(samples[index]);
        dc_output_ = input - dc_input_ + 0.995F * dc_output_;
        dc_input_ = input;
        pcm_.push_back(static_cast<std::int16_t>(std::clamp(dc_output_, -32768.0F, 32767.0F)));
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
    const float probability = front_end_.diagnostics().speech_probability;
    if (probability >= 0.45F) {
        candidate_ms_ = std::min<std::uint32_t>(candidate_ms_ + kFrameMs, 1000U);
        if (candidate_ms_ >= 80U) speech_detected_ = true;
        if (speech_detected_) silence_ms_ = 0;
    } else {
        candidate_ms_ = candidate_ms_ > kFrameMs ? candidate_ms_ - kFrameMs : 0U;
        if (speech_detected_ && probability < 0.20F) silence_ms_ += kFrameMs;
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
    if (!frame_.empty()) keep(frame_.data(), frame_.size());
    auto wav = make_wav(pcm_, kSampleRate);
    pcm_.clear();
    frame_.clear();
    level_ = 0.0F;
    return wav;
}

bool VoiceRecorder::no_speech() const noexcept {
    return !speech_detected_ && elapsed_ms() >= (mode_ == Mode::Dictation ? kDictationNoSpeechMs : kNoSpeechMs);
}

bool VoiceRecorder::should_stop() const noexcept {
    const auto silence_end = mode_ == Mode::Dictation ? kDictationSilenceEndMs : kSilenceEndMs;
    return elapsed_ms() >= max_ms() || (speech_detected_ && silence_ms_ >= silence_end) || no_speech();
}

std::uint32_t VoiceRecorder::elapsed_ms() const noexcept {
    return static_cast<std::uint32_t>((pcm_.size() + frame_.size()) * 1000U / kSampleRate);
}

void BargeInGate::reset() noexcept {
    reference_rms_ = 200.0F;
    level_ = 0.0F;
    calibration_count_ = 0;
    calibrated_ms_ = 0;
    active_ms_ = 0;
    triggered_ = false;
}

bool BargeInGate::observe(const std::int16_t* samples, std::size_t count, float playback_envelope) noexcept {
    if (triggered_ || !samples || count == 0) return triggered_;
    double energy = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const double value = samples[index];
        energy += value * value;
    }
    const auto rms = static_cast<float>(std::sqrt(energy / static_cast<double>(count)));
    level_ = std::max(std::clamp(rms / 5000.0F, 0.0F, 1.0F), level_ * 0.78F);
    const auto duration_ms = static_cast<std::uint32_t>(count * 1000U / VoiceRecorder::kSampleRate);
    if (calibrated_ms_ < kCalibrationMs) {
        // The owner may already be talking in this window: the 30th percentile
        // of its block levels is the speaker bleed, not their voice.
        if (calibration_count_ < calibration_rms_.size()) calibration_rms_[calibration_count_++] = rms;
        calibrated_ms_ = std::min(kCalibrationMs, calibrated_ms_ + duration_ms);
        if (calibrated_ms_ >= kCalibrationMs && calibration_count_ > 0) {
            auto* first = calibration_rms_.data();
            auto* quiet = first + (calibration_count_ - 1) * 3 / 10;
            std::nth_element(first, quiet, first + calibration_count_);
            reference_rms_ = *quiet;
        }
        return false;
    }

    const float envelope = std::clamp(playback_envelope, 0.0F, 1.0F);
    const float threshold = std::max({900.0F, reference_rms_ * 2.2F, 500.0F + envelope * 1800.0F});
    if (rms > threshold) {
        active_ms_ += duration_ms;
    } else {
        active_ms_ = duration_ms * 2U >= active_ms_ ? 0U : active_ms_ - duration_ms * 2U;
        reference_rms_ = reference_rms_ * 0.98F + rms * 0.02F;
    }
    triggered_ = active_ms_ >= kActivationMs;
    return triggered_;
}

namespace {

constexpr std::uint32_t kGateFrameSamples = 320;   // 20 ms
constexpr float kGateStep = 0.5F;                  // NLMS step
constexpr float kGateRegularisation = 128.0F * 20.0F * 20.0F;
constexpr int kReferenceGlitch = 16000;            // no voice reaches this in the reference slot
constexpr float kReferenceSounding = 20.0F;        // rms: the device is speaking
constexpr float kOwnerMinimum = 120.0F;            // rms at 24 dB of microphone gain

}  // namespace

void EchoReferenceGate::reset() noexcept {
    history_.fill(0.0F);
    weights_.fill(0.0F);
    kept_weights_.fill(0.0F);
    position_ = 0;
    power_ = 0;
    update_turn_ = 0;
    adapt_ = false;
    owner_ = false;
    glitch_ = false;
    frame_samples_ = 0;
    frame_microphone_ = frame_reference_ = 0;
    frame_residual_ = 0.0;
    settle_ms_ = kSettleMs;
    learned_ms_ = 0;
    reference_hold_ = 0.0F;
    noise_ = 30.0F;
    ratios_.fill(0.0F);
    ratio_count_ = ratio_next_ = 0;
    recent_ = 0;
    recent_ratios_.fill(0.0F);
    recent_next_ = 0;
    echo_energy_ = residual_energy_ = 0.0;
    loudest_ = 0.0F;
    triggered_ = false;
    residual_.clear();
}

bool EchoReferenceGate::observe(const std::int16_t* microphone, const std::int16_t* reference, std::size_t count) {
    residual_.clear();
    if (triggered_ || !microphone || !reference || count == 0) return triggered_;
    residual_.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float input = static_cast<float>(reference[index]);
        const float heard = static_cast<float>(microphone[index]);
        if (std::abs(reference[index]) >= kReferenceGlitch && !glitch_) {
            // The ES7210 restarting: what was learnt before it stands.
            glitch_ = true;
            adapt_ = false;
            weights_ = kept_weights_;
        }
        position_ = position_ == 0 ? kTaps - 1 : position_ - 1;
        // Whole samples: the window's power stays exact, never below zero.
        const std::int64_t entering = reference[index];
        const auto leaving = static_cast<std::int64_t>(history_[position_]);
        power_ += entering * entering - leaving * leaving;
        history_[position_] = input;
        history_[position_ + kTaps] = input;
        const float* window = history_.data() + position_;
        float echo = 0.0F;
        for (std::size_t tap = 0; tap < kTaps; ++tap) echo += weights_[tap] * window[tap];
        const float left = heard - echo;
        // Once it has learnt the room, which does not move, one sample in four
        // keeps it there: this runs beside the animation, and at every sample it
        // took a third of the frames away while the device spoke (2026-11-18).
        update_turn_ = (update_turn_ + 1U) & 3U;
        if (adapt_ && (learned_ms_ < kLearnMs || update_turn_ == 0U)) {
            // Slower while the owner may be speaking: a filter that learnt too
            // little would otherwise freeze, and take its own echo for them.
            const float step = (owner_ ? kGateStep * 0.1F : kGateStep) * left /
                               (static_cast<float>(power_) + kGateRegularisation);
            for (std::size_t tap = 0; tap < kTaps; ++tap) weights_[tap] += step * window[tap];
        }
        residual_[index] = static_cast<std::int16_t>(std::clamp(left, -32768.0F, 32767.0F));
        frame_microphone_ += static_cast<std::int64_t>(microphone[index]) * microphone[index];
        frame_reference_ += entering * entering;
        frame_residual_ += static_cast<double>(left) * left;
        if (++frame_samples_ == kGateFrameSamples) {
            end_frame();
            if (triggered_) {
                residual_.resize(index + 1);
                break;
            }
        }
    }
    return triggered_;
}

void EchoReferenceGate::end_frame() noexcept {
    const auto rms = [](double energy) {
        return static_cast<float>(std::sqrt(energy / static_cast<double>(kGateFrameSamples)));
    };
    const float heard = rms(static_cast<double>(frame_microphone_));
    const float reference = rms(static_cast<double>(frame_reference_));
    const float left = rms(frame_residual_);
    frame_samples_ = 0;
    frame_microphone_ = frame_reference_ = 0;
    frame_residual_ = 0.0;
    if (glitch_) {
        glitch_ = false;
        settle_ms_ = kSettleMs;
        recent_ = 0;
        return;
    }
    if (settle_ms_ > 0) {
        settle_ms_ = settle_ms_ > 20U ? settle_ms_ - 20U : 0U;
        adapt_ = false;
        return;
    }
    kept_weights_ = weights_;
    reference_hold_ = std::max(reference, reference_hold_ * 0.8F);
    const bool speaking = reference > kReferenceSounding;
    const bool learned = learned_ms_ >= kLearnMs;
    bool owner = false;
    if (learned && ratio_count_ > 0) {
        // The echo left over, as the last second's median while the device
        // spoke (its syllables' gaps would make any lower share far too low).
        std::array<float, 50> sorted{};
        std::copy_n(ratios_.begin(), ratio_count_, sorted.begin());
        auto* middle = sorted.data() + ratio_count_ / 2;
        std::nth_element(sorted.data(), middle, sorted.data() + ratio_count_);
        const float expected = *middle * reference_hold_ + noise_;
        owner = left > kOwnerMargin * expected && left > kOwnerMinimum;
        recent_ratios_[recent_next_] = left > kOwnerMinimum ? left / expected : 0.0F;
        recent_next_ = (recent_next_ + 1) % recent_ratios_.size();
        auto window = recent_ratios_;
        auto* sustained = window.data() + (kOwnerFrames - 1);
        std::nth_element(window.data(), sustained, window.data() + window.size(), std::greater<float>());
        loudest_ = std::max(loudest_, *sustained);
    }
    if (!owner) {
        if (speaking) {
            ratios_[ratio_next_] = left / reference_hold_;
            ratio_next_ = (ratio_next_ + 1) % ratios_.size();
            ratio_count_ = std::min(ratio_count_ + 1, ratios_.size());
            learned_ms_ += 20U;
            echo_energy_ = echo_energy_ * 0.95 + static_cast<double>(heard) * heard;
            residual_energy_ = residual_energy_ * 0.95 + static_cast<double>(left) * left;
        } else if (reference_hold_ <= kReferenceSounding) {
            noise_ += (left - noise_) * (left < noise_ ? 0.5F : 0.02F);
        }
    }
    owner_ = owner;
    adapt_ = speaking;
    recent_ = ((recent_ << 1U) | (owner ? 1U : 0U)) & ((1U << kWindowFrames) - 1U);
    std::uint32_t owner_frames = 0;
    for (std::uint32_t bits = recent_; bits != 0; bits &= bits - 1U) ++owner_frames;
    triggered_ = learned && owner_frames >= kOwnerFrames;
}

float EchoReferenceGate::cancelled_db() const noexcept {
    if (residual_energy_ <= 0.0 || echo_energy_ <= 0.0) return 0.0F;
    return static_cast<float>(10.0 * std::log10(echo_energy_ / residual_energy_));
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

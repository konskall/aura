#include "aura/windows_audio_backend.hpp"
#include "aura/voice.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace aura::simulator {
namespace {
constexpr std::size_t kMaxSourceBytes = 8U * 1024U * 1024U;
constexpr double kPi = 3.14159265358979323846;
}

WindowsAudioBackend::WindowsAudioBackend() {
    for (auto& sample : echo_reference_) sample.store(0, std::memory_order_relaxed);
}
WindowsAudioBackend::~WindowsAudioBackend() { shutdown(); }

bool WindowsAudioBackend::initialize() {
    if (device_) return true;
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fail("SDL_AUDIO_INIT_FAILED");
            return false;
        }
        owns_audio_subsystem_ = true;
    }

    SDL_AudioSpec desired{};
    // 48 kHz stereo: music quality for the radio; clips are converted to it.
    desired.freq = 48000;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = &WindowsAudioBackend::audio_callback;
    desired.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_, 0);
    if (!device_) {
        fail("NO_AUDIO_DEVICE");
        return false;
    }
    // Allocate outside the real-time callback. SDL currently requests
    // `obtained_.samples` frames, but keep one extra callback of headroom for
    // backends that return a larger block than requested.
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        const auto samples = static_cast<std::size_t>(std::max<Uint16>(obtained_.samples, 1024U)) *
                             std::max<unsigned>(obtained_.channels, 1U) * 2U;
        stream_scratch_.assign(samples, 0);
    }
    machine_.dispatch(AudioCommand::Initialize);
    error_code_.clear();
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void WindowsAudioBackend::shutdown() {
    if (device_) {
        SDL_PauseAudioDevice(device_, 1);
    }
    stream_close();
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        cursor_ = 0;
    }
    machine_.dispatch(AudioCommand::Shutdown);
    active_ = false;
    envelope_ = 0.0F;
    completed_ = false;
    echo_reference_total_.store(0, std::memory_order_release);
    echo_resample_phase_ = 0;
    if (owns_audio_subsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        owns_audio_subsystem_ = false;
    }
}

bool WindowsAudioBackend::play_converted(const Uint8* bytes, std::size_t length, const SDL_AudioSpec& source,
                                         bool alert) {
    if (!device_ || !bytes || length == 0 || length > kMaxSourceBytes || source.format != AUDIO_S16SYS ||
        source.channels == 0 || source.channels > 2 || source.freq < 8000 || source.freq > 48000) {
        fail("INVALID_PCM");
        return false;
    }
    if (!machine_.dispatch(AudioCommand::BeginBuffering)) return false;

    SDL_AudioCVT converter{};
    if (SDL_BuildAudioCVT(&converter, source.format, source.channels, source.freq,
                          obtained_.format, obtained_.channels, obtained_.freq) < 0) {
        fail("AUDIO_CONVERSION_FAILED");
        return false;
    }
    if (length > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        fail("AUDIO_TOO_LARGE");
        return false;
    }
    converter.len = static_cast<int>(length);
    std::vector<Uint8> converted(length * static_cast<std::size_t>(std::max(1, converter.len_mult)));
    std::memcpy(converted.data(), bytes, length);
    converter.buf = converted.data();
    if (SDL_ConvertAudio(&converter) != 0 || converter.len_cvt <= 0 ||
        (converter.len_cvt % static_cast<int>(sizeof(std::int16_t))) != 0) {
        fail("AUDIO_CONVERSION_FAILED");
        return false;
    }

    SDL_LockAudioDevice(device_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto* begin = reinterpret_cast<const std::int16_t*>(converted.data());
        samples_.assign(begin, begin + converter.len_cvt / static_cast<int>(sizeof(std::int16_t)));
        cursor_ = 0;
        alert_ = alert;
    }
    SDL_UnlockAudioDevice(device_);
    completed_ = false;
    paused_ = false;
    active_ = true;
    envelope_ = 0.0F;
    error_code_.clear();
    machine_.dispatch(AudioCommand::BeginPlayback);
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

bool WindowsAudioBackend::play_pcm(const PcmClip& clip) {
    SDL_AudioSpec source{};
    source.freq = static_cast<int>(clip.format.sample_rate);
    source.format = clip.format.bits_per_sample == 16 ? AUDIO_S16SYS : 0;
    source.channels = clip.format.channels;
    return play_converted(reinterpret_cast<const Uint8*>(clip.samples.data()),
                          clip.samples.size() * sizeof(std::int16_t), source);
}

bool WindowsAudioBackend::play_alert(const PcmClip& clip) {
    SDL_AudioSpec source{};
    source.freq = static_cast<int>(clip.format.sample_rate);
    source.format = clip.format.bits_per_sample == 16 ? AUDIO_S16SYS : 0;
    source.channels = clip.format.channels;
    return play_converted(reinterpret_cast<const Uint8*>(clip.samples.data()),
                          clip.samples.size() * sizeof(std::int16_t), source, true);
}

bool WindowsAudioBackend::play_wav(const std::string& path) {
    if (path.empty()) {
        fail("INVALID_WAV");
        return false;
    }
    SDL_RWops* stream = SDL_RWFromFile(path.c_str(), "rb");
    if (!stream) {
        fail("INVALID_WAV");
        return false;
    }
    return play_wav_stream(stream);
}

bool WindowsAudioBackend::play_wav_stream(SDL_RWops* stream) {
    SDL_AudioSpec source{};
    Uint8* bytes = nullptr;
    Uint32 length = 0;
    if (!stream || !SDL_LoadWAV_RW(stream, 1, &source, &bytes, &length)) {
        fail("INVALID_WAV");
        return false;
    }
    const bool result = play_converted(bytes, length, source);
    SDL_FreeWAV(bytes);
    return result;
}

bool WindowsAudioBackend::play_bundled_wav() {
    constexpr std::uint32_t sample_rate = 16000;
    constexpr std::uint32_t frames = sample_rate * 650 / 1000;
    constexpr std::uint32_t data_bytes = frames * sizeof(std::int16_t);
    std::vector<Uint8> wav(44 + data_bytes, 0);
    auto put_u16 = [&](std::size_t offset, std::uint16_t value) {
        wav[offset] = static_cast<Uint8>(value & 0xffU);
        wav[offset + 1] = static_cast<Uint8>((value >> 8) & 0xffU);
    };
    auto put_u32 = [&](std::size_t offset, std::uint32_t value) {
        for (int index = 0; index < 4; ++index) wav[offset + index] = static_cast<Uint8>((value >> (index * 8)) & 0xffU);
    };
    std::memcpy(wav.data(), "RIFF", 4);
    put_u32(4, 36 + data_bytes);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    put_u32(16, 16);
    put_u16(20, 1);
    put_u16(22, 1);
    put_u32(24, sample_rate);
    put_u32(28, sample_rate * sizeof(std::int16_t));
    put_u16(32, sizeof(std::int16_t));
    put_u16(34, 16);
    std::memcpy(wav.data() + 36, "data", 4);
    put_u32(40, data_bytes);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const double fade = std::min(1.0, std::min(static_cast<double>(i) / 500.0,
                                                   static_cast<double>(frames - i) / 500.0));
        const double carrier = std::sin(2.0 * kPi * 659.25 * i / sample_rate);
        const double overtone = std::sin(2.0 * kPi * 987.77 * i / sample_rate) * 0.35;
        const auto sample = static_cast<std::int16_t>((carrier + overtone) * fade * 2800.0);
        std::memcpy(wav.data() + 44 + i * sizeof(sample), &sample, sizeof(sample));
    }
    SDL_RWops* stream = SDL_RWFromConstMem(wav.data(), static_cast<int>(wav.size()));
    if (!stream) {
        fail("INVALID_WAV");
        return false;
    }
    return play_wav_stream(stream);
}

void WindowsAudioBackend::simulate_complete() { stop(); }
void WindowsAudioBackend::simulate_failure(const char* code) { fail(code); }

bool WindowsAudioBackend::play_test_tone(std::uint32_t duration_ms) {
    if (!device_) {
        fail("NO_AUDIO_DEVICE");
        return false;
    }
    if (duration_ms < 50 || duration_ms > 5000) {
        fail("INVALID_TONE_DURATION");
        return false;
    }
    PcmClip clip;
    clip.format = {24000, 1, 16};
    const std::size_t frames = static_cast<std::size_t>(clip.format.sample_rate) * duration_ms / 1000;
    clip.samples.resize(frames);
    const std::size_t fade = std::min<std::size_t>(frames / 2, clip.format.sample_rate / 50);
    for (std::size_t i = 0; i < frames; ++i) {
        double gain = 0.16;
        if (i < fade) gain *= static_cast<double>(i) / std::max<std::size_t>(1, fade);
        if (frames - i <= fade) gain *= static_cast<double>(frames - i) / std::max<std::size_t>(1, fade);
        clip.samples[i] = static_cast<std::int16_t>(std::sin(2.0 * kPi * 523.25 * i / clip.format.sample_rate) *
                                                    gain * 32767.0);
    }
    return play_pcm(clip);
}

bool WindowsAudioBackend::pause() {
    const bool accepted = machine_.dispatch(AudioCommand::Pause);
    if (accepted) {
        paused_ = true;
        envelope_ = 0.0F;
    }
    return accepted;
}

bool WindowsAudioBackend::resume() {
    const bool accepted = machine_.dispatch(AudioCommand::Resume);
    if (accepted) paused_ = false;
    return accepted;
}

void WindowsAudioBackend::stop() {
    if (!device_) return;
    if (machine_.dispatch(AudioCommand::Stop)) {
        active_ = false;
        SDL_LockAudioDevice(device_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_.clear();
            cursor_ = 0;
            alert_ = false;
        }
        SDL_UnlockAudioDevice(device_);
        machine_.dispatch(AudioCommand::Complete);
    }
    paused_ = false;
    completed_ = false;
    envelope_ = 0.0F;
}

void WindowsAudioBackend::set_volume(float volume) { volume_ = std::clamp(volume, 0.0F, 1.0F); }
void WindowsAudioBackend::set_muted(bool muted) {
    muted_ = muted;
    if (muted) envelope_ = 0.0F;
}

void WindowsAudioBackend::tick(std::uint32_t) {
    if (completed_.exchange(false) && machine_.state() == AudioState::Playing) {
        active_ = false;
        machine_.dispatch(AudioCommand::Complete);
        envelope_ = 0.0F;
    }
}

AudioDiagnostics WindowsAudioBackend::diagnostics() const {
    AudioDiagnostics result;
    result.state = machine_.state();
    result.backend = "SDL2 Windows Audio";
    result.format = {static_cast<std::uint32_t>(obtained_.freq), obtained_.channels, 16};
    result.volume = volume_.load();
    result.envelope = envelope_.load();
    result.muted = muted_.load();
    result.device_available = device_ != 0;
    result.error_code = error_code_;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t total_frames = obtained_.channels ? samples_.size() / obtained_.channels : 0;
    const std::size_t cursor_frames = obtained_.channels ? cursor_ / obtained_.channels : 0;
    result.buffered_frames = total_frames > cursor_frames ? total_frames - cursor_frames : 0;
    result.progress = total_frames ? std::min(1.0F, static_cast<float>(cursor_frames) / total_frames) : 0.0F;
    return result;
}

void WindowsAudioBackend::audio_callback(void* userdata, Uint8* stream, int length) {
    static_cast<WindowsAudioBackend*>(userdata)->render(stream, length);
}

void WindowsAudioBackend::render(Uint8* stream, int length) {
    std::memset(stream, 0, static_cast<std::size_t>(length));
    auto* output = reinterpret_cast<std::int16_t*>(stream);
    const std::size_t wanted = static_cast<std::size_t>(length) / sizeof(std::int16_t);
    const float volume = muted_.load() ? 0.0F : volume_.load();
    // A clock alert is heard through mute and never below the alert minimum.
    const float clip_gain = playback_gain(volume_.load(), muted_.load(), alert_.load());
    const bool clip_playing = !paused_.load() && active_.load();
    // The music bed first; a clip on top turns it down smoothly.
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (stream_) {
            const auto available_samples = std::min(wanted, stream_scratch_.size());
            const auto available_bytes = available_samples * sizeof(std::int16_t);
            const int got = available_bytes > 0 ? SDL_AudioStreamGet(
                stream_, stream_scratch_.data(), static_cast<int>(available_bytes)) : 0;
            const float target = stream_hold_.load() ? 0.0F : (clip_playing ? 0.2F : 1.0F);
            float duck = stream_duck_.load();
            const std::size_t count = got > 0 ? static_cast<std::size_t>(got) / sizeof(std::int16_t) : 0;
            for (std::size_t i = 0; i < count; ++i) {
                duck += (target - duck) * 0.0005F;
                output[i] = static_cast<std::int16_t>(
                    std::clamp(static_cast<float>(stream_scratch_[i]) * volume * duck, -32768.0F, 32767.0F));
            }
            stream_duck_ = count > 0 ? duck : target;
        }
    }
    float peak = 0.0F;
    bool finished = false;
    if (clip_playing) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t available = samples_.size() > cursor_ ? samples_.size() - cursor_ : 0;
        const std::size_t count = std::min(wanted, available);
        for (std::size_t i = 0; i < count; ++i) {
            const float scaled = static_cast<float>(samples_[cursor_ + i]) * clip_gain;
            const float mixed = static_cast<float>(output[i]) + scaled;
            output[i] = static_cast<std::int16_t>(std::clamp(mixed, -32768.0F, 32767.0F));
            peak = std::max(peak, std::abs(scaled) / 32768.0F);
        }
        cursor_ += count;
        finished = !samples_.empty() && cursor_ >= samples_.size();
    }
    // The echo reference records everything rendered, the radio and silence
    // included, so its timeline never stops and stays in step with the
    // microphone's.
    const auto channels = static_cast<std::size_t>(std::max<unsigned>(obtained_.channels, 1U));
    const auto frames = wanted / channels;
    const auto sample_rate = static_cast<std::uint32_t>(std::max(obtained_.freq, 16000));
    auto total = echo_reference_total_.load(std::memory_order_relaxed);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        echo_resample_phase_ += VoiceRecorder::kSampleRate;
        if (echo_resample_phase_ < sample_rate) continue;
        echo_resample_phase_ -= sample_rate;
        int mixed = 0;
        for (std::size_t channel = 0; channel < channels; ++channel) mixed += output[frame * channels + channel];
        const auto mono = static_cast<std::int16_t>(mixed / static_cast<int>(channels));
        echo_reference_[static_cast<std::size_t>(total % kEchoReferenceSamples)].store(mono,
                                                                                      std::memory_order_relaxed);
        ++total;
    }
    echo_reference_total_.store(total, std::memory_order_release);
    if (!clip_playing) return;
    envelope_ = envelope_.load() * 0.72F + peak * 0.28F;
    if (finished) {
        active_ = false;
        completed_ = true;
    }
}

bool WindowsAudioBackend::copy_echo_reference(std::size_t samples, std::vector<std::int16_t>& output,
                                              std::uint32_t acoustic_delay_ms) const {
    output.clear();
    if (samples == 0 || samples > kEchoReferenceSamples) return false;
    const std::uint64_t delay = static_cast<std::uint64_t>(VoiceRecorder::kSampleRate) * acoustic_delay_ms / 1000U;
    const auto total = echo_reference_total_.load(std::memory_order_acquire);
    if (total < delay + samples) return false;
    const auto end = total - delay;
    const auto begin = end - samples;
    output.resize(samples);
    for (std::size_t index = 0; index < samples; ++index) {
        output[index] = echo_reference_[static_cast<std::size_t>((begin + index) % kEchoReferenceSamples)]
                            .load(std::memory_order_relaxed);
    }
    return true;
}

bool WindowsAudioBackend::stream_open(std::uint32_t sample_rate, std::uint8_t channels) {
    if (!device_ || sample_rate < 8000 || sample_rate > 48000 || channels == 0 || channels > 2) return false;
    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (stream_ && stream_rate_ == sample_rate && stream_channels_ == channels) return true;
    if (stream_) SDL_FreeAudioStream(stream_);
    stream_ = SDL_NewAudioStream(AUDIO_S16SYS, channels, static_cast<int>(sample_rate), obtained_.format,
                                 obtained_.channels, obtained_.freq);
    stream_rate_ = stream_ ? sample_rate : 0;
    stream_channels_ = stream_ ? channels : 0;
    return stream_ != nullptr;
}

bool WindowsAudioBackend::stream_push(const std::int16_t* samples, std::size_t frames) {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    constexpr std::size_t kMaxPushFrames = 1152U * 32U;
    if (!stream_ || !samples || frames == 0 || frames > kMaxPushFrames) return false;
    const auto bytes = frames * stream_channels_ * sizeof(std::int16_t);
    return SDL_AudioStreamPut(stream_, samples, static_cast<int>(bytes)) == 0;
}

std::uint32_t WindowsAudioBackend::stream_buffered_ms() const {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (!stream_ || obtained_.freq <= 0 || obtained_.channels == 0) return 0;
    const auto bytes_per_second = static_cast<std::uint64_t>(obtained_.freq) * obtained_.channels * sizeof(std::int16_t);
    const int available = SDL_AudioStreamAvailable(stream_);
    if (available <= 0) return 0;
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(available) * 1000U / bytes_per_second);
}

void WindowsAudioBackend::stream_close() {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (stream_) SDL_FreeAudioStream(stream_);
    stream_ = nullptr;
    stream_rate_ = 0;
    stream_channels_ = 0;
    stream_duck_ = 1.0F;
}

void WindowsAudioBackend::stream_hold(bool hold) { stream_hold_ = hold; }

void WindowsAudioBackend::fail(const char* code) {
    if (machine_.state() == AudioState::Uninitialized) {
        machine_.dispatch(AudioCommand::Initialize);
    }
    machine_.dispatch(AudioCommand::Fail);
    error_code_ = code ? code : "AUDIO_ERROR";
    active_ = false;
    envelope_ = 0.0F;
}

}  // namespace aura::simulator

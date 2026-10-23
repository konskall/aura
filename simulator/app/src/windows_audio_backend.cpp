#include "aura/windows_audio_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace aura::simulator {
namespace {
constexpr std::size_t kMaxSourceBytes = 8U * 1024U * 1024U;
constexpr double kPi = 3.14159265358979323846;
}

WindowsAudioBackend::WindowsAudioBackend() = default;
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
    desired.freq = 24000;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 512;
    desired.callback = &WindowsAudioBackend::audio_callback;
    desired.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_, 0);
    if (!device_) {
        fail("NO_AUDIO_DEVICE");
        return false;
    }
    machine_.dispatch(AudioCommand::Initialize);
    error_code_.clear();
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void WindowsAudioBackend::shutdown() {
    if (device_) {
        SDL_PauseAudioDevice(device_, 1);
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
    if (owns_audio_subsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        owns_audio_subsystem_ = false;
    }
}

bool WindowsAudioBackend::play_converted(const Uint8* bytes, std::size_t length, const SDL_AudioSpec& source) {
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
    if (paused_.load() || !active_.load()) return;
    auto* output = reinterpret_cast<std::int16_t*>(stream);
    const std::size_t wanted = static_cast<std::size_t>(length) / sizeof(std::int16_t);
    float peak = 0.0F;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t available = samples_.size() > cursor_ ? samples_.size() - cursor_ : 0;
        const std::size_t count = std::min(wanted, available);
        const float gain = muted_.load() ? 0.0F : volume_.load();
        for (std::size_t i = 0; i < count; ++i) {
            const float scaled = static_cast<float>(samples_[cursor_ + i]) * gain;
            output[i] = static_cast<std::int16_t>(std::clamp(scaled, -32768.0F, 32767.0F));
            peak = std::max(peak, std::abs(static_cast<float>(output[i])) / 32768.0F);
        }
        cursor_ += count;
        finished = !samples_.empty() && cursor_ >= samples_.size();
    }
    envelope_ = envelope_.load() * 0.72F + peak * 0.28F;
    if (finished) {
        active_ = false;
        completed_ = true;
    }
}

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

#pragma once

#include "aura/audio_service.hpp"

#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "SDL.h"

namespace aura::simulator {

class WindowsAudioBackend final : public IAudioService {
public:
    WindowsAudioBackend();
    ~WindowsAudioBackend() override;
    WindowsAudioBackend(const WindowsAudioBackend&) = delete;
    WindowsAudioBackend& operator=(const WindowsAudioBackend&) = delete;

    bool initialize() override;
    void shutdown() override;
    bool play_pcm(const PcmClip& clip) override;
    bool play_alert(const PcmClip& clip) override;
    bool play_wav(const std::string& path) override;
    bool play_test_tone(std::uint32_t duration_ms = 900) override;
    bool play_bundled_wav();
    void simulate_complete();
    void simulate_failure(const char* code = "DEMO_AUDIO_FAILURE");
    bool pause() override;
    bool resume() override;
    void stop() override;
    void set_volume(float volume) override;
    void set_muted(bool muted) override;
    void tick(std::uint32_t elapsed_ms) override;
    AudioDiagnostics diagnostics() const override;

    // A continuous music channel (the radio) under the clips. Chimes, alarms
    // and speech play over it while it is turned down; volume and mute apply
    // to both. Safe to feed from another thread.
    bool stream_open(std::uint32_t sample_rate, std::uint8_t channels);
    // Takes interleaved samples; returns false when the format is not open.
    bool stream_push(const std::int16_t* samples, std::size_t frames);
    std::uint32_t stream_buffered_ms() const;
    void stream_close();
    // Silences the music without stopping it (the microphone is open).
    void stream_hold(bool hold);
    // Copies the recent 16 kHz mono render reference for acoustic echo
    // cancellation: the `samples` that end `acoustic_delay_ms` before now.
    // False means the ring does not yet contain enough audio.
    bool copy_echo_reference(std::size_t samples, std::vector<std::int16_t>& output,
                             std::uint32_t acoustic_delay_ms = 80) const;

private:
    static void audio_callback(void* userdata, Uint8* stream, int length);
    void render(Uint8* stream, int length);
    bool play_converted(const Uint8* bytes, std::size_t length, const SDL_AudioSpec& source, bool alert = false);
    bool play_wav_stream(SDL_RWops* stream);
    void fail(const char* code);

    mutable std::mutex mutex_;
    AudioStateMachine machine_{};
    SDL_AudioDeviceID device_{0};
    SDL_AudioSpec obtained_{};
    std::vector<std::int16_t> samples_;
    std::size_t cursor_{0};
    std::atomic<float> volume_{0.55F};
    std::atomic<float> envelope_{0.0F};
    std::atomic<bool> muted_{false};
    std::atomic<bool> alert_{false};  // the clip is a clock alert: heard through mute
    std::atomic<bool> paused_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> completed_{false};
    std::string error_code_;
    bool owns_audio_subsystem_{false};

    mutable std::mutex stream_mutex_;
    SDL_AudioStream* stream_{nullptr};
    std::uint32_t stream_rate_{0};
    std::uint8_t stream_channels_{0};
    std::vector<std::int16_t> stream_scratch_;
    std::atomic<bool> stream_hold_{false};
    std::atomic<float> stream_duck_{1.0F};
    static constexpr std::size_t kEchoReferenceSamples = 16000U * 2U;
    std::array<std::atomic<std::int16_t>, kEchoReferenceSamples> echo_reference_{};
    std::atomic<std::uint64_t> echo_reference_total_{0};
    std::uint32_t echo_resample_phase_{0};
};

}  // namespace aura::simulator

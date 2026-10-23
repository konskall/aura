#pragma once

#include "aura/audio_service.hpp"

#include <atomic>
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

private:
    static void audio_callback(void* userdata, Uint8* stream, int length);
    void render(Uint8* stream, int length);
    bool play_converted(const Uint8* bytes, std::size_t length, const SDL_AudioSpec& source);
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
    std::atomic<bool> paused_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> completed_{false};
    std::string error_code_;
    bool owns_audio_subsystem_{false};
};

}  // namespace aura::simulator

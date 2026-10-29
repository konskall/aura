#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {

enum class AudioState {
    Uninitialized,
    Ready,
    Buffering,
    Playing,
    Paused,
    Stopping,
    Error,
};

enum class AudioCommand {
    Initialize,
    BeginBuffering,
    BeginPlayback,
    Pause,
    Resume,
    Stop,
    Complete,
    Fail,
    Reset,
    Shutdown,
};

struct AudioFormat {
    std::uint32_t sample_rate{24000};
    std::uint8_t channels{1};
    std::uint8_t bits_per_sample{16};
};

struct PcmClip {
    AudioFormat format{};
    std::vector<std::int16_t> samples;
};

struct AudioDiagnostics {
    AudioState state{AudioState::Uninitialized};
    std::string backend{"Unavailable"};
    AudioFormat format{};
    float volume{0.55F};
    float progress{0.0F};
    float envelope{0.0F};
    bool muted{false};
    bool device_available{false};
    std::size_t buffered_frames{0};
    std::uint32_t underruns{0};
    std::string error_code;
};

class AudioStateMachine {
public:
    bool dispatch(AudioCommand command) noexcept;
    AudioState state() const noexcept { return state_; }

private:
    AudioState state_{AudioState::Uninitialized};
};

// Alarms, timers and reminders ring through mute and never quieter than this.
constexpr float kAlertMinimumVolume = 0.5F;

// The gain a platform applies to a clip: the owner's volume, silenced by mute,
// except for an alert, which ignores mute and keeps kAlertMinimumVolume.
float playback_gain(float volume, bool muted, bool alert) noexcept;

class IAudioService {
public:
    virtual ~IAudioService() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool play_pcm(const PcmClip& clip) = 0;
    // A clock alert: played like play_pcm, but heard through mute (playback_gain).
    virtual bool play_alert(const PcmClip& clip) = 0;
    virtual bool play_wav(const std::string& path) = 0;
    virtual bool play_test_tone(std::uint32_t duration_ms = 900) = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual void stop() = 0;
    virtual void set_volume(float volume) = 0;
    virtual void set_muted(bool muted) = 0;
    virtual void tick(std::uint32_t elapsed_ms) = 0;
    virtual AudioDiagnostics diagnostics() const = 0;
};

class MockAudioService final : public IAudioService {
public:
    bool initialize() override;
    void shutdown() override;
    bool play_pcm(const PcmClip& clip) override;
    bool play_alert(const PcmClip& clip) override;
    bool play_wav(const std::string& path) override;
    bool play_test_tone(std::uint32_t duration_ms = 900) override;
    bool pause() override;
    bool resume() override;
    void stop() override;
    void set_volume(float volume) override;
    void set_muted(bool muted) override;
    void tick(std::uint32_t elapsed_ms) override;
    AudioDiagnostics diagnostics() const override;
    void fail_for_test(std::string error_code);

private:
    bool start(std::uint32_t duration_ms, AudioFormat format);

    AudioStateMachine machine_{};
    AudioDiagnostics diagnostics_{};
    std::uint32_t elapsed_ms_{0};
    std::uint32_t duration_ms_{0};
    bool alert_{false};
};

const char* audio_state_name(AudioState state) noexcept;

}  // namespace aura

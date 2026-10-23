#pragma once

#include "aura/audio_service.hpp"

#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace aura {

enum class VoicePurpose { Assistant, Note, Translate };
enum class VoiceCaptureState { Idle, Recording, Processing };

struct VoiceSnapshot {
    VoiceCaptureState state{VoiceCaptureState::Idle};
    VoicePurpose purpose{VoicePurpose::Assistant};
    float level{0.0F};
    std::uint32_t elapsed_ms{0};
    bool speech_detected{false};
    // What this platform can do right now.
    bool capture_available{false};
    bool stt_available{false};
    bool tts_available{false};
    std::string error_code;
    std::uint64_t sequence{0};
};

// Collects 16 kHz mono PCM while the owner speaks: level for the halo,
// a simple energy VAD that ends the recording after a pause, and a WAV for
// speech recognition. Shared by the simulator (SDL capture) and the device
// (ES7210 capture).
class VoiceRecorder {
public:
    static constexpr std::uint32_t kSampleRate = 16000;
    static constexpr std::uint32_t kMaxMs = 15000;
    static constexpr std::uint32_t kSilenceEndMs = 1200;
    static constexpr std::uint32_t kNoSpeechMs = 6000;

    void start();
    void append(const std::int16_t* samples, std::size_t count);
    void cancel();
    // A finished recording: a mono 16-bit WAV at kSampleRate.
    std::vector<std::uint8_t> finish_wav();

    bool recording() const noexcept { return recording_; }
    // Speech followed by a pause, the length limit, or no speech at all.
    bool should_stop() const noexcept;
    bool no_speech() const noexcept;
    float level() const noexcept { return level_; }
    bool speech_detected() const noexcept { return speech_detected_; }
    std::uint32_t elapsed_ms() const noexcept;

private:
    void analyse_frame(const std::int16_t* samples, std::size_t count);

    std::vector<std::int16_t> pcm_;
    std::vector<std::int16_t> frame_;
    bool recording_{false};
    bool speech_detected_{false};
    float level_{0.0F};
    float noise_floor_{120.0F};
    std::uint32_t silence_ms_{0};
};

std::vector<std::uint8_t> make_wav(const std::vector<std::int16_t>& pcm, std::uint32_t sample_rate);
// Accepts PCM 16-bit mono/stereo WAV (stereo is mixed down).
bool parse_wav(const std::uint8_t* data, std::size_t size, PcmClip& clip);

struct TranscriptResult {
    bool ok{false};
    std::string text;
    std::string error_code;
};

struct SpeechResult {
    bool ok{false};
    PcmClip clip;
    std::string error_code;
};

// Speech recognition and synthesis. Simulator: the loopback bridge (local
// whisper.cpp and the Windows voices). Device: the future AI gateway.
class ISpeechService {
public:
    virtual ~ISpeechService() = default;
    virtual bool stt_available() const = 0;
    virtual bool tts_available(const std::string& language) const = 0;
    virtual std::future<TranscriptResult> transcribe(std::vector<std::uint8_t> wav, std::string language) = 0;
    virtual std::future<SpeechResult> synthesize(std::string text, std::string language) = 0;
};

}  // namespace aura

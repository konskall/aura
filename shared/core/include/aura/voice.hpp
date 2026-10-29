#pragma once

#include "aura/audio_service.hpp"

#include <array>
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

struct VoiceFrontEndDiagnostics {
    float noise_rms{120.0F};
    float gain{1.0F};
    float speech_probability{0.0F};
};

// Small-footprint 16 kHz front-end for both simulator and ESP32 capture.
// It removes DC, suppresses stationary room noise and applies bounded AGC.
// It owns no thread or hardware and allocates only through the caller's output.
class VoiceFrontEnd {
public:
    void reset() noexcept;
    void process(const std::int16_t* samples, std::size_t count, std::vector<std::int16_t>& output);
    const VoiceFrontEndDiagnostics& diagnostics() const noexcept { return diagnostics_; }

private:
    float previous_input_{0.0F};
    float previous_high_pass_{0.0F};
    float suppression_{1.0F};
    VoiceFrontEndDiagnostics diagnostics_{};
};

// Reference-based echo suppressor used only when the platform can provide the
// samples it is currently rendering. Without that reference it passes the mic
// signal through, so an unavailable AEC is never represented as active.
class AcousticEchoCanceller {
public:
    void reset() noexcept;
    // `playback_reference` is aligned sample for sample with the microphone.
    bool process(const std::int16_t* microphone, const std::int16_t* playback_reference,
                 std::size_t count, std::vector<std::int16_t>& output) noexcept;
    // The delay between rendering and the microphone (output buffering, the
    // room, capture buffering) is unknown: `playback_reference` holds
    // `count + delay_span` samples and the echo's alignment within them is
    // found and then tracked.
    bool process(const std::int16_t* microphone, const std::int16_t* playback_reference,
                 std::size_t count, std::size_t delay_span, std::vector<std::int16_t>& output) noexcept;
    float suppression() const noexcept { return suppression_; }
    bool reference_active() const noexcept { return reference_active_; }
    // Where in the reference window the echo was last found.
    std::size_t delay_offset() const noexcept { return offset_; }

private:
    std::size_t offset_{0};
    float echo_gain_{0.0F};
    float suppression_{0.0F};
    bool reference_active_{false};
};

// Collects 16 kHz mono PCM while the owner speaks: level for the halo,
// an adaptive VAD that ends the recording after a pause, and a WAV for
// speech recognition. Shared by the simulator (SDL capture) and the device
// (ES7210 capture).
class VoiceRecorder {
public:
    static constexpr std::uint32_t kSampleRate = 16000;
    static constexpr std::uint32_t kMaxMs = 15000;
    static constexpr std::uint32_t kSilenceEndMs = 1200;
    static constexpr std::uint32_t kNoSpeechMs = 6000;
    // A dictated note may pause to think and may last half a minute.
    static constexpr std::uint32_t kDictationMaxMs = 30000;
    static constexpr std::uint32_t kDictationSilenceEndMs = 3500;
    static constexpr std::uint32_t kDictationNoSpeechMs = 8000;

    // A spoken question or phrase (Turn), or a dictated note.
    enum class Mode { Turn, Dictation };

    void start(Mode mode = Mode::Turn);
    void append(const std::int16_t* samples, std::size_t count);
    void cancel();
    // A finished recording: a mono 16-bit WAV at kSampleRate.
    std::vector<std::uint8_t> finish_wav();

    bool recording() const noexcept { return recording_; }
    // Speech followed by a pause, the length limit, or no speech at all.
    bool should_stop() const noexcept;
    bool no_speech() const noexcept;
    float level() const noexcept { return level_; }
    float noise_level() const noexcept { return front_end_.diagnostics().noise_rms; }
    float input_gain() const noexcept { return front_end_.diagnostics().gain; }
    bool speech_detected() const noexcept { return speech_detected_; }
    std::uint32_t elapsed_ms() const noexcept;

private:
    void analyse_frame(const std::int16_t* samples, std::size_t count);
    std::uint32_t max_ms() const noexcept { return mode_ == Mode::Dictation ? kDictationMaxMs : kMaxMs; }

    std::vector<std::int16_t> pcm_;
    std::vector<std::int16_t> frame_;
    std::vector<std::int16_t> processed_;  // reused: no allocation per 20 ms frame
    VoiceFrontEnd front_end_;
    Mode mode_{Mode::Turn};
    bool recording_{false};
    bool speech_detected_{false};
    float level_{0.0F};
    std::uint32_t silence_ms_{0};
    std::uint32_t candidate_ms_{0};
};

// Conservative speech-over-playback gate. It calibrates against the room and
// speaker bleed at the beginning of each spoken answer, then requires a
// sustained rise before requesting barge-in. Platform AEC may feed cleaner PCM
// into the same gate on hardware; the class itself owns no device or UI state.
class BargeInGate {
public:
    static constexpr std::uint32_t kCalibrationMs = 650;
    static constexpr std::uint32_t kActivationMs = 280;

    void reset() noexcept;
    bool observe(const std::int16_t* samples, std::size_t count, float playback_envelope) noexcept;

    bool calibrated() const noexcept { return calibrated_ms_ >= kCalibrationMs; }
    float level() const noexcept { return level_; }

private:
    float reference_rms_{200.0F};
    float level_{0.0F};
    // Block levels seen while calibrating; the quiet part of the window, not
    // its average, becomes the reference.
    std::array<float, 72> calibration_rms_{};
    std::size_t calibration_count_{0};
    std::uint32_t calibrated_ms_{0};
    std::uint32_t active_ms_{0};
    bool triggered_{false};
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

// Speech recognition and synthesis. Simulator: the loopback bridge. Device:
// the future authenticated AI gateway. `voice` is a validated shared profile
// id (currently "athina" or "nestoras"), never a raw provider voice name.
class ISpeechService {
public:
    virtual ~ISpeechService() = default;
    virtual bool stt_available() const = 0;
    virtual bool tts_available(const std::string& language) const = 0;
    virtual std::future<TranscriptResult> transcribe(std::vector<std::uint8_t> wav, std::string language) = 0;
    virtual std::future<SpeechResult> synthesize(std::string text, std::string language, std::string voice) = 0;
};

}  // namespace aura

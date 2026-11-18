#pragma once

#include "aura/audio_service.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace aura {

enum class VoicePurpose { Assistant, Note, Message };
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

// Speech over the device's own voice, with the speaker's signal as reference.
// The board wires the ES8311's output back into the ES7210 (MIC3, "AEC" in
// its schematic), sampled with the microphones: an NLMS filter learns the way
// from the speaker to the microphone and takes the device's voice out, and
// the owner's voice is what is left. Measured on the board (2026-11-16): the
// microphone hears the speaker about as loud as the owner, so a level alone
// cannot tell them apart, and the ES7210 starts with ~250 ms of noise in every
// slot, again when the speaker starts while it listens.
class EchoReferenceGate {
public:
    static constexpr std::size_t kTaps = 128;           // 8 ms at 16 kHz
    static constexpr std::uint32_t kSettleMs = 300;     // after a start or a restart
    static constexpr std::uint32_t kLearnMs = 500;      // of the device's voice, before deciding
    static constexpr std::uint32_t kWindowFrames = 20;  // 400 ms of 20 ms frames...
    static constexpr std::uint32_t kOwnerFrames = 12;   // ...240 ms of them with the owner's voice
    // The owner's voice: 8 dB above the device's echo left over.
    static constexpr float kOwnerMargin = 2.5F;

    EchoReferenceGate() { reset(); }
    void reset() noexcept;
    // `microphone` and `reference` come from the same ADC frames.
    bool observe(const std::int16_t* microphone, const std::int16_t* reference, std::size_t count);
    bool triggered() const noexcept { return triggered_; }
    // How much of the device's voice the filter takes out, in dB.
    float cancelled_db() const noexcept;
    // The loudest sound over the device's voice so far that lasted 240 ms of
    // 400, as a multiple of the echo left over (kOwnerMargin interrupts): for
    // the log, how close the owner came.
    float loudest() const noexcept { return loudest_; }
    // The microphone without the device's voice, from the last observe().
    const std::vector<std::int16_t>& residual() const noexcept { return residual_; }

private:
    void end_frame() noexcept;

    std::array<float, kTaps * 2> history_{};  // each sample twice: the window is contiguous
    std::array<float, kTaps> weights_{};
    std::array<float, kTaps> kept_weights_{};  // the last frame's, restored after a glitch
    std::size_t position_{0};
    bool adapt_{false};
    bool owner_{false};  // the last frame sounded like the owner
    bool glitch_{false};
    std::uint32_t frame_samples_{0};
    double frame_microphone_{0.0};
    double frame_reference_{0.0};
    double frame_residual_{0.0};
    std::uint32_t settle_ms_{kSettleMs};
    std::uint32_t learned_ms_{0};
    float reference_hold_{0.0F};
    float noise_{30.0F};
    std::array<float, 50> ratios_{};  // residual / reference, the last second without the owner
    std::size_t ratio_count_{0};
    std::size_t ratio_next_{0};
    std::uint32_t recent_{0};  // a bit a frame: the owner's voice
    std::array<float, kWindowFrames> recent_ratios_{};  // each frame's voice over the echo left
    std::size_t recent_next_{0};
    double echo_energy_{0.0};
    double residual_energy_{0.0};
    float loudest_{0.0F};
    bool triggered_{false};
    std::vector<std::int16_t> residual_;
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
    // The WAV keeps the voice as heard, only its DC removed: the front end's
    // gain and gate (for the level and the pauses) clipped loud syllables and
    // thinned soft ones before speech recognition (2026-11-16).
    void keep(const std::int16_t* samples, std::size_t count);
    std::uint32_t max_ms() const noexcept { return mode_ == Mode::Dictation ? kDictationMaxMs : kMaxMs; }

    std::vector<std::int16_t> pcm_;
    std::vector<std::int16_t> frame_;
    std::vector<std::int16_t> processed_;  // reused: no allocation per 20 ms frame
    float dc_input_{0.0F};
    float dc_output_{0.0F};
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

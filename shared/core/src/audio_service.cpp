#include "aura/audio_service.hpp"

#include <algorithm>
#include <cmath>

namespace aura {

bool AudioStateMachine::dispatch(AudioCommand command) noexcept {
    AudioState next = state_;
    bool accepted = true;
    switch (command) {
        case AudioCommand::Initialize:
            accepted = state_ == AudioState::Uninitialized;
            next = accepted ? AudioState::Ready : state_;
            break;
        case AudioCommand::BeginBuffering:
            accepted = state_ == AudioState::Ready || state_ == AudioState::Playing ||
                       state_ == AudioState::Paused || state_ == AudioState::Error;
            next = accepted ? AudioState::Buffering : state_;
            break;
        case AudioCommand::BeginPlayback:
            accepted = state_ == AudioState::Buffering;
            next = accepted ? AudioState::Playing : state_;
            break;
        case AudioCommand::Pause:
            accepted = state_ == AudioState::Playing;
            next = accepted ? AudioState::Paused : state_;
            break;
        case AudioCommand::Resume:
            accepted = state_ == AudioState::Paused;
            next = accepted ? AudioState::Playing : state_;
            break;
        case AudioCommand::Stop:
            accepted = state_ == AudioState::Buffering || state_ == AudioState::Playing ||
                       state_ == AudioState::Paused;
            next = accepted ? AudioState::Stopping : state_;
            break;
        case AudioCommand::Complete:
            accepted = state_ == AudioState::Playing || state_ == AudioState::Stopping;
            next = accepted ? AudioState::Ready : state_;
            break;
        case AudioCommand::Fail:
            accepted = state_ != AudioState::Uninitialized;
            next = accepted ? AudioState::Error : state_;
            break;
        case AudioCommand::Reset:
            accepted = state_ == AudioState::Error;
            next = accepted ? AudioState::Ready : state_;
            break;
        case AudioCommand::Shutdown:
            accepted = true;
            next = AudioState::Uninitialized;
            break;
    }
    if (accepted) state_ = next;
    return accepted;
}

bool MockAudioService::initialize() {
    if (!machine_.dispatch(AudioCommand::Initialize)) return machine_.state() == AudioState::Ready;
    diagnostics_.backend = "Deterministic Mock";
    diagnostics_.device_available = true;
    diagnostics_.state = machine_.state();
    diagnostics_.error_code.clear();
    return true;
}

void MockAudioService::shutdown() {
    machine_.dispatch(AudioCommand::Shutdown);
    diagnostics_.state = machine_.state();
    diagnostics_.progress = 0.0F;
    diagnostics_.envelope = 0.0F;
    diagnostics_.buffered_frames = 0;
}

bool MockAudioService::start(std::uint32_t duration_ms, AudioFormat format) {
    if (duration_ms == 0 || format.bits_per_sample != 16 || format.channels == 0 ||
        format.channels > 2 || format.sample_rate < 8000 || format.sample_rate > 48000) {
        fail_for_test("INVALID_AUDIO_FORMAT");
        return false;
    }
    if (!machine_.dispatch(AudioCommand::BeginBuffering)) return false;
    elapsed_ms_ = 0;
    duration_ms_ = duration_ms;
    diagnostics_.format = format;
    diagnostics_.progress = 0.0F;
    diagnostics_.buffered_frames = static_cast<std::size_t>(format.sample_rate) * duration_ms / 1000;
    diagnostics_.error_code.clear();
    machine_.dispatch(AudioCommand::BeginPlayback);
    diagnostics_.state = machine_.state();
    return true;
}

bool MockAudioService::play_pcm(const PcmClip& clip) {
    if (clip.samples.empty() || clip.format.channels == 0) {
        fail_for_test("EMPTY_PCM");
        return false;
    }
    const auto frames = clip.samples.size() / clip.format.channels;
    return start(static_cast<std::uint32_t>(frames * 1000 / clip.format.sample_rate), clip.format);
}

bool MockAudioService::play_wav(const std::string& path) {
    if (path.empty()) {
        fail_for_test("INVALID_WAV");
        return false;
    }
    return start(1200, {});
}

bool MockAudioService::play_test_tone(std::uint32_t duration_ms) { return start(duration_ms, {}); }

bool MockAudioService::pause() {
    const bool accepted = machine_.dispatch(AudioCommand::Pause);
    diagnostics_.state = machine_.state();
    diagnostics_.envelope = 0.0F;
    return accepted;
}

bool MockAudioService::resume() {
    const bool accepted = machine_.dispatch(AudioCommand::Resume);
    diagnostics_.state = machine_.state();
    return accepted;
}

void MockAudioService::stop() {
    if (machine_.dispatch(AudioCommand::Stop)) machine_.dispatch(AudioCommand::Complete);
    diagnostics_.state = machine_.state();
    diagnostics_.progress = 0.0F;
    diagnostics_.envelope = 0.0F;
    diagnostics_.buffered_frames = 0;
}

void MockAudioService::set_volume(float volume) { diagnostics_.volume = std::clamp(volume, 0.0F, 1.0F); }
void MockAudioService::set_muted(bool muted) {
    diagnostics_.muted = muted;
    if (muted) diagnostics_.envelope = 0.0F;
}

void MockAudioService::tick(std::uint32_t elapsed_ms) {
    diagnostics_.state = machine_.state();
    if (machine_.state() != AudioState::Playing) return;
    elapsed_ms_ = std::min(duration_ms_, elapsed_ms_ + elapsed_ms);
    diagnostics_.progress = duration_ms_ ? static_cast<float>(elapsed_ms_) / duration_ms_ : 1.0F;
    const float phase = static_cast<float>(elapsed_ms_) / 90.0F;
    diagnostics_.envelope = diagnostics_.muted ? 0.0F :
        diagnostics_.volume * (0.32F + 0.18F * std::abs(std::sin(phase)));
    const auto played = static_cast<std::size_t>(diagnostics_.format.sample_rate) * elapsed_ms_ / 1000;
    diagnostics_.buffered_frames = diagnostics_.buffered_frames > played ? diagnostics_.buffered_frames - played : 0;
    if (elapsed_ms_ >= duration_ms_) {
        machine_.dispatch(AudioCommand::Complete);
        diagnostics_.state = machine_.state();
        diagnostics_.envelope = 0.0F;
        diagnostics_.buffered_frames = 0;
    }
}

AudioDiagnostics MockAudioService::diagnostics() const { return diagnostics_; }

void MockAudioService::fail_for_test(std::string error_code) {
    machine_.dispatch(AudioCommand::Fail);
    diagnostics_.state = machine_.state();
    diagnostics_.error_code = std::move(error_code);
    diagnostics_.envelope = 0.0F;
}

const char* audio_state_name(AudioState state) noexcept {
    switch (state) {
        case AudioState::Uninitialized: return "UNINITIALIZED";
        case AudioState::Ready: return "READY";
        case AudioState::Buffering: return "BUFFERING";
        case AudioState::Playing: return "PLAYING";
        case AudioState::Paused: return "PAUSED";
        case AudioState::Stopping: return "STOPPING";
        case AudioState::Error: return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace aura

#include "aura/audio_service.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace aura;
    AudioStateMachine transitions;
    expect(!transitions.dispatch(AudioCommand::Pause), "cannot pause before init");
    expect(transitions.dispatch(AudioCommand::Initialize), "initialize");
    expect(transitions.dispatch(AudioCommand::BeginBuffering), "buffering");
    expect(transitions.dispatch(AudioCommand::BeginPlayback), "playing");
    expect(transitions.dispatch(AudioCommand::Pause), "pause");
    expect(!transitions.dispatch(AudioCommand::Pause), "duplicate pause rejected");
    expect(transitions.dispatch(AudioCommand::Resume), "resume");
    expect(transitions.dispatch(AudioCommand::Stop), "stop");
    expect(transitions.dispatch(AudioCommand::Complete), "stopped to ready");

    MockAudioService audio;
    expect(audio.initialize(), "mock initializes");
    audio.set_volume(2.0F);
    expect(audio.diagnostics().volume == 1.0F, "volume upper bounded");
    audio.set_volume(-1.0F);
    expect(audio.diagnostics().volume == 0.0F, "volume lower bounded");
    audio.set_volume(0.6F);
    expect(audio.play_test_tone(100), "tone starts");
    audio.tick(25);
    expect(audio.diagnostics().state == AudioState::Playing, "tone remains active");
    expect(audio.diagnostics().progress > 0.0F, "progress advances");
    audio.set_muted(true);
    audio.tick(10);
    expect(audio.diagnostics().envelope == 0.0F, "mute suppresses envelope");
    expect(audio.pause(), "mock pauses");
    const float paused = audio.diagnostics().progress;
    audio.tick(100);
    expect(audio.diagnostics().progress == paused, "paused progress is stable");
    expect(audio.resume(), "mock resumes");
    audio.tick(100);
    expect(audio.diagnostics().state == AudioState::Ready, "completion returns ready");
    expect(!audio.play_wav(""), "invalid wav rejected");
    expect(audio.diagnostics().state == AudioState::Error, "invalid wav reports error");
    audio.shutdown();
    expect(audio.diagnostics().state == AudioState::Uninitialized, "shutdown is deterministic");
    std::cout << "audio_service_test: PASS\n";
    return 0;
}

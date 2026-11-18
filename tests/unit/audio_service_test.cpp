#include "aura/audio_service.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

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

    // Alarms, timers and reminders ring through mute, never too quietly to hear.
    expect(playback_gain(0.3F, false, false) == 0.3F && playback_gain(0.8F, true, false) == 0.0F,
           "ordinary clips follow volume and mute");
    expect(playback_gain(0.8F, true, true) == 0.8F, "an alert rings through mute at the owner's volume");
    expect(playback_gain(0.0F, true, true) == kAlertMinimumVolume &&
           playback_gain(0.1F, false, true) == kAlertMinimumVolume,
           "an alert is never quieter than the alert minimum");
    MockAudioService muted;
    expect(muted.initialize(), "a second mock initializes");
    muted.set_volume(0.0F);
    muted.set_muted(true);
    PcmClip beep;
    beep.format = {16000, 1, 16};
    beep.samples.assign(1600, 8000);
    expect(muted.play_alert(beep), "a muted device starts an alert");
    muted.tick(20);
    expect(muted.diagnostics().state == AudioState::Playing && muted.diagnostics().envelope > 0.0F,
           "the alert is heard through mute");
    expect(muted.play_pcm(beep), "an ordinary clip starts");
    muted.tick(20);
    expect(muted.diagnostics().envelope == 0.0F, "an ordinary clip stays muted");

    // A voice still arriving: the speaker reads what has come while the rest is
    // being decoded.
    PcmFeed feed({16000, 1, 16});
    expect(feed.format().sample_rate == 16000 && feed.size() == 0, "a feed knows its format");
    std::int16_t heard[8]{};
    bool ended = true;
    expect(feed.read(0, heard, 8, ended) == 0 && !ended, "nothing has come yet, and more will");
    const std::int16_t first[5] = {1, 2, 3, 4, 5};
    expect(feed.append(first, 5) && feed.size() == 5, "samples arrive");
    expect(feed.read(0, heard, 3, ended) == 3 && heard[0] == 1 && heard[2] == 3 && !ended, "the first of them");
    expect(feed.read(3, heard, 8, ended) == 2 && heard[0] == 4 && heard[1] == 5 && !ended, "and the rest that came");
    expect(feed.read(5, heard, 8, ended) == 0 && !ended, "the reader waits at the end of what came");
    expect(feed.read(9, heard, 8, ended) == 0 && !ended, "a reader past the end reads nothing");
    feed.finish();
    expect(feed.read(0, heard, 3, ended) == 3 && !ended, "with more left to read it has not ended yet");
    expect(feed.finished() && feed.read(3, heard, 8, ended) == 2 && ended, "the last samples say they are the last");
    expect(feed.read(5, heard, 8, ended) == 0 && ended, "and after them it has ended");
    expect(!feed.append(first, 5) && feed.size() == 5, "nothing comes after the end");

    // A voice has a length: forty seconds at most, what the decoder may hold.
    PcmFeed bounded({16000, 1, 16});
    const std::vector<std::int16_t> second(16000, 7);
    int seconds = 0;
    while (seconds < 100 && bounded.append(second.data(), second.size())) ++seconds;
    expect(seconds == 40 && bounded.size() == 40U * 16000U, "a feed takes forty seconds and no more");

    // Written and read at once, every sample arrives once and in order.
    PcmFeed shared({16000, 1, 16});
    std::thread writer([&shared] {
        std::int16_t next = 0;
        for (int block = 0; block < 400; ++block) {
            std::int16_t samples[64];
            for (auto& sample : samples) sample = next++;
            shared.append(samples, 64);
        }
        shared.finish();
    });
    std::size_t offset = 0;
    bool in_order = true;
    for (bool finished = false; !finished;) {
        std::int16_t samples[50];
        const auto count = shared.read(offset, samples, 50, finished);
        for (std::size_t index = 0; index < count; ++index) {
            in_order = in_order && samples[index] == static_cast<std::int16_t>(offset + index);
        }
        offset += count;
        if (count == 0 && !finished) std::this_thread::yield();
    }
    writer.join();
    expect(in_order && offset == 400U * 64U, "a feed read while it is written loses nothing");

    std::cout << "audio_service_test: PASS\n";
    return 0;
}

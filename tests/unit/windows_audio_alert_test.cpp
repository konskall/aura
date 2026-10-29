// SDL's dummy audio driver only (no speaker): the render reference shows what
// the device would have played. An alert (alarm, timer, reminder) is heard
// through mute and at the alert minimum when the volume is 0; an ordinary clip
// stays silent while muted.
#include "aura/windows_audio_backend.hpp"

#include "SDL.h"

#include <chrono>
#include <cmath>
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

// The loudest of the last 200 ms the device rendered.
int rendered_peak(const aura::simulator::WindowsAudioBackend& audio) {
    std::vector<std::int16_t> reference;
    if (!audio.copy_echo_reference(3200, reference, 0)) return -1;
    int peak = 0;
    for (const auto sample : reference) peak = std::max(peak, std::abs(static_cast<int>(sample)));
    return peak;
}

}  // namespace

int main() {
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    aura::simulator::WindowsAudioBackend audio;
    expect(audio.initialize(), "dummy audio opens");
    audio.set_volume(0.0F);
    audio.set_muted(true);

    aura::PcmClip tone;
    tone.format = {16000, 1, 16};
    tone.samples.resize(16000);
    for (std::size_t index = 0; index < tone.samples.size(); ++index) {
        tone.samples[index] = static_cast<std::int16_t>(12000.0 * std::sin(static_cast<double>(index) * 0.21));
    }

    expect(audio.play_pcm(tone), "an ordinary clip starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    expect(rendered_peak(audio) == 0, "an ordinary clip stays silent while muted");
    audio.stop();

    expect(audio.play_alert(tone), "an alert starts while muted");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const int peak = rendered_peak(audio);
    std::cout << "alert peak while muted at volume 0: " << peak << '\n';
    expect(peak > 4000, "an alert is heard through mute, at the alert minimum");
    audio.stop();
    audio.shutdown();
    std::cout << "aura_windows_audio_alert_tests passed\n";
    return 0;
}

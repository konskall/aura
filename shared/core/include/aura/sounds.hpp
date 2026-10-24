#pragma once

#include "aura/audio_service.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aura {

// Short sounds synthesised on the device (no assets): 16 kHz mono PCM, so the
// simulator and the ESP32 play exactly the same samples.
constexpr std::uint32_t kSoundSampleRate = 16000;
// One alarm pass plus the pause before the next one.
constexpr std::uint32_t kAlarmCycleMs = 2000;

// Two soft rising bell notes for a new message.
PcmClip make_message_chime();
// One pass of the alarm melody; `loudness` is clamped to 0..1.
PcmClip make_alarm_melody(float loudness);
// The alarm starts at 30 % and reaches full loudness after 30 s of ringing.
float alarm_loudness(std::uint64_t ringing_ms) noexcept;

// Decides when a new notification deserves the chime: only for one that was
// not there at the previous look, never at the first look (start-up), never
// while `quiet`, and at most once every few seconds. What arrives while quiet
// counts as seen, so it does not chime later.
class MessageChimeGate {
public:
    bool observe(std::vector<std::string> ids, std::uint64_t now_ms, bool quiet);

private:
    std::vector<std::string> seen_;
    bool primed_{false};
    bool chimed_{false};
    std::uint64_t last_chime_ms_{0};
};

// The owner's sound choices. The alarm ignores mute: it always rings.
struct SoundSettings {
    float volume{0.55F};
    bool muted{false};
    bool message_chime{true};   // a chime for new messages and notifications
    bool spoken_replies{true};  // read answers aloud after a spoken question
};

// One "key=value" line each; unknown keys and bad values keep the defaults.
std::string serialize_sound_settings(const SoundSettings& settings);
SoundSettings parse_sound_settings(const std::string& data);

}  // namespace aura

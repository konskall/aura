#include "aura/sounds.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <sstream>

namespace aura {
namespace {

constexpr float kPi = 3.14159265F;
constexpr float kAttackS = 0.006F;   // soft start, no click
constexpr float kReleaseS = 0.020F;  // fade to zero before the note ends
constexpr float kOvertone = 0.35F;   // a quieter octave makes the sine a bell
constexpr float kChimePeak = 0.28F;
constexpr float kAlarmPeak = 0.90F;
constexpr std::uint64_t kAlarmRampMs = 30000;
constexpr std::uint64_t kChimeSpacingMs = 3000;

struct Note {
    float start_s;
    float frequency;
    float length_s;
    float decay_s;
};

float note_value(const Note& note, float t) {
    float envelope = std::exp(-t / note.decay_s);
    if (t < kAttackS) envelope *= t / kAttackS;
    const float left = note.length_s - t;
    if (left < kReleaseS) envelope *= left / kReleaseS;
    const float phase = 2.0F * kPi * note.frequency * t;
    return envelope * (std::sin(phase) + kOvertone * std::exp(-2.0F * t / note.decay_s) * std::sin(2.0F * phase));
}

// Float math only (the ESP32-S3 FPU is single precision) and no float buffer:
// the notes are written with headroom for all of them at once, then the clip
// is scaled so its loudest sample is `peak` of full scale.
PcmClip render(const Note* notes, std::size_t count, float peak) {
    PcmClip clip;
    clip.format = {kSoundSampleRate, 1, 16};
    float end_s = 0.0F;
    for (std::size_t index = 0; index < count; ++index) end_s = std::max(end_s, notes[index].start_s + notes[index].length_s);
    const auto frames = static_cast<std::size_t>(end_s * static_cast<float>(kSoundSampleRate)) + 1;
    clip.samples.assign(frames, 0);
    if (!(peak > 0.0F)) return clip;
    const float headroom = 32000.0F / ((1.0F + kOvertone) * static_cast<float>(count));
    int loudest = 0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float t = static_cast<float>(frame) / static_cast<float>(kSoundSampleRate);
        float value = 0.0F;
        for (std::size_t index = 0; index < count; ++index) {
            const auto& note = notes[index];
            if (t >= note.start_s && t < note.start_s + note.length_s) value += note_value(note, t - note.start_s);
        }
        const int sample = static_cast<int>(std::lround(value * headroom));
        clip.samples[frame] = static_cast<std::int16_t>(sample);
        loudest = std::max(loudest, std::abs(sample));
    }
    if (loudest == 0) return clip;
    const float scale = peak * 32767.0F / static_cast<float>(loudest);
    for (auto& sample : clip.samples) {
        sample = static_cast<std::int16_t>(std::lround(static_cast<float>(sample) * scale));
    }
    return clip;
}

}  // namespace

const char* assistant_voice_id(AssistantVoice voice) noexcept {
    return voice == AssistantVoice::Athina ? "athina" : "nestoras";
}

AssistantVoice assistant_voice_from_id(const std::string& id) noexcept {
    return id == "athina" ? AssistantVoice::Athina : AssistantVoice::Nestoras;
}

PcmClip make_message_chime() {
    // G5 then C6: a short, rising "ding-dong".
    static constexpr Note kNotes[]{
        {0.00F, 783.99F, 0.22F, 0.09F},
        {0.12F, 1046.50F, 0.30F, 0.12F},
    };
    return render(kNotes, std::size(kNotes), kChimePeak);
}

PcmClip make_alarm_melody(float loudness) {
    if (!(loudness > 0.0F)) loudness = 0.0F;
    if (loudness > 1.0F) loudness = 1.0F;
    // C5 E5 G5 C6: a rising arpeggio that ends on a longer note.
    static constexpr Note kNotes[]{
        {0.00F, 523.25F, 0.40F, 0.18F},
        {0.18F, 659.25F, 0.40F, 0.18F},
        {0.36F, 783.99F, 0.40F, 0.18F},
        {0.54F, 1046.50F, 0.60F, 0.30F},
    };
    return render(kNotes, std::size(kNotes), kAlarmPeak * loudness);
}

float alarm_loudness(std::uint64_t ringing_ms) noexcept {
    const auto clamped = std::min(ringing_ms, kAlarmRampMs);
    return 0.3F + 0.7F * static_cast<float>(clamped) / static_cast<float>(kAlarmRampMs);
}

bool MessageChimeGate::observe(std::vector<std::string> ids, std::uint64_t now_ms, bool quiet) {
    const bool fresh = std::any_of(ids.begin(), ids.end(), [&](const std::string& id) {
        return std::find(seen_.begin(), seen_.end(), id) == seen_.end();
    });
    seen_ = std::move(ids);
    if (!primed_) {
        primed_ = true;
        return false;
    }
    if (!fresh || quiet) return false;
    if (chimed_ && now_ms - last_chime_ms_ < kChimeSpacingMs) return false;
    chimed_ = true;
    last_chime_ms_ = now_ms;
    return true;
}

std::string serialize_sound_settings(const SoundSettings& settings) {
    char volume[16];
    std::snprintf(volume, sizeof(volume), "%.2f", static_cast<double>(std::clamp(settings.volume, 0.0F, 1.0F)));
    std::string text = "aura-sound 1\n";
    text += std::string("volume=") + volume + "\n";
    text += std::string("muted=") + (settings.muted ? "1" : "0") + "\n";
    text += std::string("chime=") + (settings.message_chime ? "1" : "0") + "\n";
    text += std::string("spoken=") + (settings.spoken_replies ? "1" : "0") + "\n";
    text += std::string("continuous=") + (settings.continuous_conversation ? "1" : "0") + "\n";
    text += std::string("barge_in=") + (settings.barge_in ? "1" : "0") + "\n";
    text += std::string("voice=") + assistant_voice_id(settings.assistant_voice) + "\n";
    return text;
}

SoundSettings parse_sound_settings(const std::string& data) {
    SoundSettings settings;
    std::istringstream input(data);
    std::string line;
    if (!std::getline(input, line) || line != "aura-sound 1") return settings;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto key = line.substr(0, equals);
        const auto value = line.substr(equals + 1);
        if (key == "voice") {
            if (value == "athina" || value == "nestoras") settings.assistant_voice = assistant_voice_from_id(value);
            continue;
        }
        if (key == "volume") {
            char* end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (end != value.c_str() && *end == '\0' && parsed >= 0.0F && parsed <= 1.0F) settings.volume = parsed;
            continue;
        }
        if (value != "0" && value != "1") continue;
        const bool on = value == "1";
        if (key == "muted") settings.muted = on;
        else if (key == "chime") settings.message_chime = on;
        else if (key == "spoken") settings.spoken_replies = on;
        else if (key == "continuous") settings.continuous_conversation = on;
        else if (key == "barge_in") settings.barge_in = on;
    }
    return settings;
}

}  // namespace aura

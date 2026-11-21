#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace aura {

// «Αύρα» without a touch (the owner, 2026-11-19): microWakeWord's streaming
// model on the device (tools/wakeword). The owner turns it on, since the
// microphone then stays open while AURA is quiet, and picks how readily it
// wakes.
enum class WakeWordSensitivity { Low, Normal, High };

struct WakeWordSettings {
    bool enabled{false};
    WakeWordSensitivity sensitivity{WakeWordSensitivity::Normal};
};

// "aura-wake-word 1", then enabled= and sensitivity= lines.
std::string serialize_wake_word_settings(const WakeWordSettings& settings);
// A text that is not ours gives the defaults; an unknown sensitivity, Normal.
WakeWordSettings parse_wake_word_settings(std::string_view text);

// The window mean that wakes AURA. Low and Normal from the final test of the
// first model (tools/wakeword/README.md, 5.3 hours of a dinner party): Low 0.99
// (10.3 % missed, 0.56 false wakes an hour), Normal 0.98 (8.7 %, 0.75). High
// 0.75, the owner's choice (2026-11-21): the owner's tries from a distance came
// to 0.58-0.85; the test measured down to 0.95 only (about 2 false wakes an
// hour there), so below it false wakes are the owner's to count.
float wake_word_cutoff(WakeWordSensitivity sensitivity) noexcept;

// A feature from microWakeWord's frontend (uint16, 40 every 10 ms) into the
// model's int8 input as its own test feeds it: scaled as in training
// (x 0.0390625), divided by the input's scale, its zero point added, cut
// toward zero (numpy's astype) and clamped.
std::int8_t quantize_wake_feature(std::uint16_t feature, float input_scale, int input_zero_point) noexcept;

// The model's outputs (0-255, one every 30 ms) into words, as microWakeWord's
// test counts them: the mean of the last kWindow outputs over the cutoff
// (probability = output / 255), and none in the first kCooldown means after a
// reset or a detection (the streaming model's state fills; one «Αύρα» is
// heard once).
class WakeWordDetector {
public:
    static constexpr std::size_t kWindow = 5;
    static constexpr int kCooldown = 75;  // 2.25 s of means

    void reset() noexcept;
    void set_cutoff(float probability) noexcept { cutoff_ = probability; }
    // True when this output completes a word.
    bool push(std::uint8_t output) noexcept;
    // The highest mean since the last call, 0-1, for the device's log.
    float take_peak() noexcept;
    // A try that came near without waking, for the device's log (the owner had
    // to shout from a metre, 2026-11-20): the highest mean of a rise over
    // kNearMiss that fell back under kNearMissEnd without a word; 0 if none
    // ended since the last call. A word and its tail are not a miss.
    static constexpr float kNearMiss = 0.5F;
    static constexpr float kNearMissEnd = 0.2F;
    float take_near_miss() noexcept;

private:
    std::array<std::uint8_t, kWindow> window_{};
    std::size_t count_{0};
    std::size_t next_{0};
    int cooldown_{kCooldown};
    float cutoff_{0.98F};
    float peak_{0.0F};
    float rise_{0.0F};       // the highest mean of the rise under way, 0 outside one
    bool woke_{false};       // a word's tail, until the mean falls back
    float near_miss_{0.0F};  // the highest that ended since take_near_miss()
};

}  // namespace aura

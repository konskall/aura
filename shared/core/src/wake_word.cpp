#include "aura/wake_word.hpp"

#include <algorithm>

namespace aura {

namespace {

constexpr std::string_view kHeader = "aura-wake-word 1";

const char* sensitivity_name(WakeWordSensitivity sensitivity) noexcept {
    switch (sensitivity) {
        case WakeWordSensitivity::Low: return "low";
        case WakeWordSensitivity::High: return "high";
        case WakeWordSensitivity::Normal: break;
    }
    return "normal";
}

}  // namespace

std::string serialize_wake_word_settings(const WakeWordSettings& settings) {
    std::string text(kHeader);
    text += "\nenabled=";
    text += settings.enabled ? "1" : "0";
    text += "\nsensitivity=";
    text += sensitivity_name(settings.sensitivity);
    text += '\n';
    return text;
}

WakeWordSettings parse_wake_word_settings(std::string_view text) {
    WakeWordSettings settings;
    bool header = false;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!header) {
            if (line != kHeader) return {};
            header = true;
        } else if (line == "enabled=1") {
            settings.enabled = true;
        } else if (line == "enabled=0") {
            settings.enabled = false;
        } else if (line == "sensitivity=low") {
            settings.sensitivity = WakeWordSensitivity::Low;
        } else if (line == "sensitivity=high") {
            settings.sensitivity = WakeWordSensitivity::High;
        } else if (line.rfind("sensitivity=", 0) == 0) {
            settings.sensitivity = WakeWordSensitivity::Normal;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return header ? settings : WakeWordSettings{};
}

float wake_word_cutoff(WakeWordSensitivity sensitivity) noexcept {
    switch (sensitivity) {
        case WakeWordSensitivity::Low: return 0.99F;
        case WakeWordSensitivity::High: return 0.75F;
        case WakeWordSensitivity::Normal: break;
    }
    return 0.98F;
}

std::int8_t quantize_wake_feature(std::uint16_t feature, float input_scale, int input_zero_point) noexcept {
    if (!(input_scale > 0.0F)) return static_cast<std::int8_t>(std::clamp(input_zero_point, -128, 127));
    const float value =
        static_cast<float>(feature) * 0.0390625F / input_scale + static_cast<float>(input_zero_point);
    // A float to an int cuts toward zero, as numpy's astype(int8) did in the test.
    return static_cast<std::int8_t>(static_cast<int>(std::clamp(value, -128.0F, 127.0F)));
}

void WakeWordDetector::reset() noexcept {
    count_ = 0;
    next_ = 0;
    cooldown_ = kCooldown;
    // A new listen: a rise of the last one is not a miss of this one.
    rise_ = 0.0F;
    woke_ = false;
    near_miss_ = 0.0F;
}

bool WakeWordDetector::push(std::uint8_t output) noexcept {
    window_[next_] = output;
    next_ = (next_ + 1) % kWindow;
    if (count_ < kWindow) ++count_;
    if (count_ < kWindow) return false;
    unsigned sum = 0;
    for (const auto value : window_) sum += value;
    const float mean = static_cast<float>(sum) / (static_cast<float>(kWindow) * 255.0F);
    peak_ = std::max(peak_, mean);
    if (mean < kNearMissEnd) {
        if (rise_ > 0.0F && !woke_) near_miss_ = std::max(near_miss_, rise_);
        rise_ = 0.0F;
        woke_ = false;
    } else if (mean >= kNearMiss) {
        rise_ = std::max(rise_, mean);
    }
    if (cooldown_ > 0) --cooldown_;
    if (cooldown_ > 0 || !(mean > cutoff_)) return false;
    cooldown_ = kCooldown;
    woke_ = true;
    return true;
}

float WakeWordDetector::take_peak() noexcept {
    const float peak = peak_;
    peak_ = 0.0F;
    return peak;
}

float WakeWordDetector::take_near_miss() noexcept {
    const float miss = near_miss_;
    near_miss_ = 0.0F;
    return miss;
}

}  // namespace aura

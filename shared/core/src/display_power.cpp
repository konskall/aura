#include "aura/display_power.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace aura {
namespace {

constexpr float kGravityFilter = 0.25F;
constexpr float kFacingZ = 0.45F;      // screen tilted up towards the viewer
constexpr float kAwayZ = 0.3F;         // hanging, in a pocket or screen vertical
constexpr float kMaxFacingRoll = 0.6F;
constexpr float kMotion = 0.12F;       // g of deviation from gravity
constexpr float kSettled = 0.06F;
constexpr std::uint32_t kSettleMs = 150;
constexpr std::uint32_t kAwayWindowMs = 1500;
constexpr std::uint32_t kMotionWindowMs = 900;
constexpr std::uint32_t kRaiseCooldownMs = 1500;
constexpr float kFaceDownZ = -0.75F;
constexpr std::uint32_t kFaceDownMs = 800;
constexpr float kFaceUpZ = -0.2F;
constexpr std::uint32_t kFaceUpMs = 300;

// Eight positions within +/-5 px, one per minute, for the always-on clock.
constexpr std::array<std::array<int, 2>, 8> kShift{{
    {{0, 0}}, {{4, 2}}, {{-3, 4}}, {{5, -3}}, {{-4, -2}}, {{2, 5}}, {{-5, 1}}, {{3, -5}}
}};

}  // namespace

std::string serialize_display_settings(const DisplaySettings& settings) {
    std::string text = "aura-display 1\n";
    text += std::string("always_on=") + (settings.always_on ? "1\n" : "0\n");
    text += std::string("raise_to_wake=") + (settings.raise_to_wake ? "1\n" : "0\n");
    text += std::string("flip_to_mute=") + (settings.flip_to_mute ? "1\n" : "0\n");
    text += "active_brightness_percent=" + std::to_string(std::clamp(settings.active_brightness_percent, 5, 100)) + "\n";
    text += "timeout_ms=" + std::to_string(std::clamp<std::uint32_t>(settings.timeout_ms, 5000, 300000)) + "\n";
    text += "dim_ms=" + std::to_string(std::clamp<std::uint32_t>(settings.dim_ms, 1000, 60000)) + "\n";
    text += std::string("appearance=") + appearance_id(settings.appearance) + "\n";
    return text;
}

DisplaySettings parse_display_settings(const std::string& data) {
    DisplaySettings settings;
    std::istringstream input(data);
    std::string line;
    if (!std::getline(input, line) || line != "aura-display 1") return settings;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto key = line.substr(0, equals);
        const auto value = line.substr(equals + 1);
        if (key == "appearance") {
            settings.appearance = appearance_from_id(value);
            continue;
        }
        if (key == "always_on" || key == "raise_to_wake" || key == "flip_to_mute") {
            if (value != "0" && value != "1") continue;
            const bool enabled = value == "1";
            if (key == "always_on") settings.always_on = enabled;
            else if (key == "raise_to_wake") settings.raise_to_wake = enabled;
            else settings.flip_to_mute = enabled;
            continue;
        }
        std::uint32_t parsed = 0;
        bool valid = !value.empty();
        for (const char ch : value) {
            if (ch < '0' || ch > '9' || parsed > 1000000U) { valid = false; break; }
            parsed = parsed * 10U + static_cast<std::uint32_t>(ch - '0');
        }
        if (!valid) continue;
        if (key == "active_brightness_percent" && parsed >= 5 && parsed <= 100) {
            settings.active_brightness_percent = static_cast<int>(parsed);
        } else if (key == "timeout_ms" && parsed >= 5000 && parsed <= 300000) settings.timeout_ms = parsed;
        else if (key == "dim_ms" && parsed >= 1000 && parsed <= 60000) settings.dim_ms = parsed;
    }
    return settings;
}

ImuGesture ImuGestureDetector::push(const ImuSample& sample) {
    const std::uint32_t now = sample.t_ms;
    if (!initialized_) {
        gx_ = sample.ax;
        gy_ = sample.ay;
        gz_ = sample.az;
        initialized_ = true;
        last_ms_ = now;
        if (gz_ < kAwayZ) away_until_ms_ = now + 1;
        return ImuGesture::None;
    }
    last_ms_ = now;
    gx_ += kGravityFilter * (sample.ax - gx_);
    gy_ += kGravityFilter * (sample.ay - gy_);
    gz_ += kGravityFilter * (sample.az - gz_);
    const float dx = sample.ax - gx_;
    const float dy = sample.ay - gy_;
    const float dz = sample.az - gz_;
    const float deviation = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (gz_ <= kFaceDownZ) {
        if (down_since_ms_ == 0) down_since_ms_ = now + 1;
        up_since_ms_ = 0;
        if (!face_down_ && now + 1 - down_since_ms_ >= kFaceDownMs) {
            face_down_ = true;
            return ImuGesture::FaceDown;
        }
    } else {
        down_since_ms_ = 0;
        if (face_down_) {
            if (gz_ >= kFaceUpZ) {
                if (up_since_ms_ == 0) up_since_ms_ = now + 1;
                if (now + 1 - up_since_ms_ >= kFaceUpMs) {
                    face_down_ = false;
                    up_since_ms_ = 0;
                    return ImuGesture::FaceUp;
                }
            } else {
                up_since_ms_ = 0;
            }
        }
    }
    if (face_down_) return ImuGesture::None;

    // Times are stored +1 so that 0 means "never".
    if (gz_ < kAwayZ) away_until_ms_ = now + 1;
    if (deviation > kMotion) motion_at_ms_ = now + 1;
    const bool facing = gz_ >= kFacingZ && std::fabs(gx_) <= kMaxFacingRoll;
    if (facing && deviation < kSettled) {
        if (settled_since_ms_ == 0) settled_since_ms_ = now + 1;
    } else {
        settled_since_ms_ = 0;
    }
    const bool settled = settled_since_ms_ != 0 && now + 1 - settled_since_ms_ >= kSettleMs;
    const bool was_away = away_until_ms_ != 0 && now + 1 - away_until_ms_ <= kAwayWindowMs;
    const bool moved = motion_at_ms_ != 0 && now + 1 - motion_at_ms_ <= kMotionWindowMs;
    const bool cooled = !raise_seen_ || now - last_raise_ms_ >= kRaiseCooldownMs;
    if (settled && was_away && moved && cooled) {
        raise_seen_ = true;
        last_raise_ms_ = now;
        away_until_ms_ = 0;
        return ImuGesture::Raise;
    }
    return ImuGesture::None;
}

void ImuGestureDetector::reset() noexcept { *this = ImuGestureDetector{}; }

void DisplayPowerModel::set_settings(const DisplaySettings& settings) {
    snapshot_.settings = settings;
    snapshot_.settings.active_brightness_percent =
        std::clamp(snapshot_.settings.active_brightness_percent, 5, 100);
    snapshot_.brightness_percent = brightness_for(snapshot_.mode);
    if (!settings.always_on && snapshot_.mode == DisplayMode::AlwaysOn) set_mode(DisplayMode::Off);
    if (!settings.flip_to_mute) snapshot_.muted_by_flip = false;
    changed();
}

void DisplayPowerModel::set_imu_available(bool available) {
    if (snapshot_.imu_available == available) return;
    snapshot_.imu_available = available;
    changed();
}

bool DisplayPowerModel::user_activity(std::uint64_t now_ms) {
    last_activity_ms_ = now_ms;
    const bool was_dark = snapshot_.mode == DisplayMode::AlwaysOn || snapshot_.mode == DisplayMode::Off;
    if (snapshot_.mode != DisplayMode::Active) set_mode(DisplayMode::Active);
    return was_dark;
}

void DisplayPowerModel::gesture(ImuGesture gesture, std::uint64_t now_ms) {
    switch (gesture) {
        case ImuGesture::Raise:
            if (snapshot_.settings.raise_to_wake && snapshot_.mode != DisplayMode::Active) user_activity(now_ms);
            break;
        case ImuGesture::FaceDown:
            if (!snapshot_.settings.flip_to_mute) break;
            snapshot_.muted_by_flip = true;
            set_mode(DisplayMode::Off);
            changed();
            break;
        case ImuGesture::FaceUp:
            if (snapshot_.muted_by_flip) {
                snapshot_.muted_by_flip = false;
                changed();
                user_activity(now_ms);
            }
            break;
        case ImuGesture::None: break;
    }
}

void DisplayPowerModel::hold_awake(bool hold, std::uint64_t now_ms) {
    if (hold == hold_) return;
    hold_ = hold;
    last_activity_ms_ = now_ms;
    // Face down still wins: an alert must not light a screen lying on its face.
    if (hold && snapshot_.mode != DisplayMode::Active && !snapshot_.muted_by_flip) set_mode(DisplayMode::Active);
}

void DisplayPowerModel::sleep_now(std::uint64_t now_ms) {
    last_activity_ms_ = now_ms;
    set_mode(snapshot_.settings.always_on ? DisplayMode::AlwaysOn : DisplayMode::Off);
}

void DisplayPowerModel::tick(std::uint64_t now_ms) {
    if (hold_ || now_ms < last_activity_ms_) return;
    const auto idle = now_ms - last_activity_ms_;
    const auto& settings = snapshot_.settings;
    if (snapshot_.mode == DisplayMode::Active && idle >= settings.timeout_ms) {
        set_mode(DisplayMode::Dimmed);
    } else if (snapshot_.mode == DisplayMode::Dimmed &&
               idle >= static_cast<std::uint64_t>(settings.timeout_ms) + settings.dim_ms) {
        set_mode(settings.always_on ? DisplayMode::AlwaysOn : DisplayMode::Off);
    }
}

void DisplayPowerModel::set_minute(std::int64_t minute_index) {
    const auto& shift = kShift[static_cast<std::size_t>(((minute_index % 8) + 8) % 8)];
    if (shift[0] == snapshot_.shift_x && shift[1] == snapshot_.shift_y) return;
    snapshot_.shift_x = shift[0];
    snapshot_.shift_y = shift[1];
    // Only the always-on clock draws the shift; other modes need no redraw.
    if (snapshot_.mode == DisplayMode::AlwaysOn) changed();
}

// Dimming never brightens: the dimmed screen is 40% of the owner's level (at
// most 30%) and the always-on clock 20% of it (at most 12%).
int DisplayPowerModel::brightness_for(DisplayMode mode) const noexcept {
    const int active = snapshot_.settings.active_brightness_percent;
    switch (mode) {
        case DisplayMode::Active: return active;
        case DisplayMode::Dimmed: return std::clamp(active * 2 / 5, 1, 30);
        case DisplayMode::AlwaysOn: return std::clamp(active / 5, 1, 12);
        case DisplayMode::Off: return 0;
    }
    return 0;
}

void DisplayPowerModel::set_mode(DisplayMode mode) {
    if (snapshot_.mode == mode) return;
    snapshot_.mode = mode;
    snapshot_.brightness_percent = brightness_for(mode);
    changed();
}

const char* display_mode_name(DisplayMode mode) noexcept {
    switch (mode) {
        case DisplayMode::Active: return "ACTIVE";
        case DisplayMode::Dimmed: return "DIMMED";
        case DisplayMode::AlwaysOn: return "ALWAYS_ON";
        case DisplayMode::Off: return "OFF";
    }
    return "UNKNOWN";
}

}  // namespace aura

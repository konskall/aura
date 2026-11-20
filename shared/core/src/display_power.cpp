#include "aura/display_power.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace aura {
namespace {

constexpr float kGravityFilter = 0.25F;
// Raise (picking AURA up): still this long first, within kStill g of gravity...
constexpr float kStill = 0.05F;
constexpr std::uint32_t kRestMs = 500;
// ...then this far (g) from where it rested for this many samples in a row (a
// knock on the desk lasts a sample or two at 50 Hz)...
constexpr float kPickup = 0.15F;
constexpr int kPickupSamples = 4;
// ...then held (settled), or still moving after kHandledMs.
constexpr float kSettled = 0.06F;
constexpr std::uint32_t kSettleMs = 150;
constexpr std::uint32_t kHandledMs = 2000;
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
    text += std::string("battery_percent=") + (settings.battery_percent ? "1\n" : "0\n");
    text += std::string("clock_24h=") + (settings.clock_24h ? "1\n" : "0\n");
    text += std::string("rotation_locked=") + (settings.rotation_locked ? "1\n" : "0\n");
    text += std::string("large_text=") + (settings.large_text ? "1\n" : "0\n");
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
        if (key == "always_on" || key == "raise_to_wake" || key == "flip_to_mute" || key == "battery_percent" ||
            key == "clock_24h" || key == "rotation_locked" || key == "large_text") {
            if (value != "0" && value != "1") continue;
            const bool enabled = value == "1";
            if (key == "always_on") settings.always_on = enabled;
            else if (key == "raise_to_wake") settings.raise_to_wake = enabled;
            else if (key == "flip_to_mute") settings.flip_to_mute = enabled;
            else if (key == "battery_percent") settings.battery_percent = enabled;
            else if (key == "rotation_locked") settings.rotation_locked = enabled;
            else if (key == "large_text") settings.large_text = enabled;
            else settings.clock_24h = enabled;
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

namespace {

// The rotation's thresholds: a sample far from 1 g is motion, not gravity; the
// screen must stand within ~53 degrees of vertical for its edges to mean
// anything; a new edge must lean within 30 degrees of its own centre.
constexpr float kOrientationMinG = 0.6F;
constexpr float kOrientationMaxG = 1.4F;
constexpr float kOrientationUpright = 0.6F;
constexpr float kOrientationCaptureDegrees = 30.0F;

// The edge "up" points away from: 0 degrees is upright (up = the top edge),
// +90 the left edge down (up = the right edge), -90 the right edge down.
ScreenRotation edge_for(float degrees) noexcept {
    if (degrees > -45.0F && degrees < 45.0F) return ScreenRotation::Bottom;
    if (degrees >= 45.0F && degrees < 135.0F) return ScreenRotation::Left;
    if (degrees <= -45.0F && degrees > -135.0F) return ScreenRotation::Right;
    return ScreenRotation::Top;
}

float centre_of(ScreenRotation rotation) noexcept {
    switch (rotation) {
        case ScreenRotation::Bottom: return 0.0F;
        case ScreenRotation::Left: return 90.0F;
        case ScreenRotation::Top: return 180.0F;
        case ScreenRotation::Right: return -90.0F;
    }
    return 0.0F;
}

}  // namespace

bool OrientationTracker::push(const ImuSample& sample) {
    const float squared = sample.ax * sample.ax + sample.ay * sample.ay + sample.az * sample.az;
    const float planar = sample.ax * sample.ax + sample.ay * sample.ay;
    const bool gravity_only = squared >= kOrientationMinG * kOrientationMinG &&
                              squared <= kOrientationMaxG * kOrientationMaxG;
    const bool standing = planar >= kOrientationUpright * kOrientationUpright * squared;
    if (locked_ || !gravity_only || !standing) {
        has_pending_ = false;
        return false;
    }
    const float degrees = std::atan2(sample.ax, sample.ay) * 57.29578F;
    const ScreenRotation edge = edge_for(degrees);
    float away = std::fabs(degrees - centre_of(edge));
    if (away > 180.0F) away = 360.0F - away;
    if (edge == rotation_ || away > kOrientationCaptureDegrees) {
        has_pending_ = false;
        return false;
    }
    if (!has_pending_ || pending_ != edge) {
        pending_ = edge;
        pending_since_ms_ = sample.t_ms;
        has_pending_ = true;
        return false;
    }
    if (sample.t_ms - pending_since_ms_ < settle_ms) return false;
    rotation_ = edge;
    has_pending_ = false;
    return true;
}

void OrientationTracker::set_locked(bool locked) noexcept {
    locked_ = locked;
    has_pending_ = false;
}

void rotate_touch_point(ScreenRotation rotation, int size, int& x, int& y) noexcept {
    const int last = size - 1;
    const int px = x;
    const int py = y;
    switch (rotation) {
        case ScreenRotation::Bottom: return;
        case ScreenRotation::Right:  // the content's top along the panel's left edge
            x = last - py;
            y = px;
            return;
        case ScreenRotation::Top:
            x = last - px;
            y = last - py;
            return;
        case ScreenRotation::Left:  // the content's top along the panel's right edge
            x = py;
            y = last - px;
            return;
    }
}

ImuGesture ImuGestureDetector::push(const ImuSample& sample) {
    const std::uint32_t now = sample.t_ms;
    if (!initialized_) {
        gx_ = sample.ax;
        gy_ = sample.ay;
        gz_ = sample.az;
        initialized_ = true;
        last_ms_ = now;
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
    if (face_down_) {
        resting_ = false;
        moved_at_ms_ = 0;
        return ImuGesture::None;
    }

    // Raise: picking AURA up. It rests on a desk standing on any edge or lying
    // flat, and is held however its screen is turned; the earlier rule (the
    // screen had to end tilted up towards the face, as a phone's) never fired
    // on the board (the first flash, 2026-11-14). Times are stored +1 so that 0
    // means "never".
    if (deviation < kStill) {
        if (still_since_ms_ == 0) still_since_ms_ = now + 1;
    } else {
        still_since_ms_ = 0;
    }
    if (moved_at_ms_ == 0) {
        if (still_since_ms_ != 0 && now + 1 - still_since_ms_ >= kRestMs) {
            // At rest: follow its reading, so a slow drift never counts.
            resting_ = true;
            rest_x_ = gx_;
            rest_y_ = gy_;
            rest_z_ = gz_;
        }
        if (!resting_) return ImuGesture::None;
        const float away_x = sample.ax - rest_x_;
        const float away_y = sample.ay - rest_y_;
        const float away_z = sample.az - rest_z_;
        const bool away = std::sqrt(away_x * away_x + away_y * away_y + away_z * away_z) > kPickup;
        moving_samples_ = away ? moving_samples_ + 1 : 0;
        if (moving_samples_ < kPickupSamples) return ImuGesture::None;
        resting_ = false;
        moving_samples_ = 0;
        moved_at_ms_ = now + 1;
        settled_since_ms_ = 0;
    }
    // Moved: a raise once it is held, or after a while of moving, unless it is
    // on its way face down (that mutes it instead).
    if (deviation < kSettled) {
        if (settled_since_ms_ == 0) settled_since_ms_ = now + 1;
    } else {
        settled_since_ms_ = 0;
    }
    const bool settled = settled_since_ms_ != 0 && now + 1 - settled_since_ms_ >= kSettleMs;
    const bool handled = now + 1 - moved_at_ms_ >= kHandledMs;
    if (!settled && !handled) return ImuGesture::None;
    moved_at_ms_ = 0;
    const bool cooled = !raise_seen_ || now - last_raise_ms_ >= kRaiseCooldownMs;
    if (gz_ <= kFaceUpZ || !cooled) return ImuGesture::None;
    raise_seen_ = true;
    last_raise_ms_ = now;
    return ImuGesture::Raise;
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

// Dimming never brightens: the dimmed screen is half the owner's level (at most
// 50%) and the always-on clock 30% of it (at most 30%). The first flash
// (2026-11-14) showed 30% and 12% too dark to read on the AMOLED, whose black
// pixels cost nothing; the owner chose these.
std::uint32_t host_loop_interval_ms(DisplayMode mode) noexcept {
    switch (mode) {
        case DisplayMode::Active:
        case DisplayMode::Dimmed: return 33U;
        case DisplayMode::AlwaysOn: return 100U;
        case DisplayMode::Off: return 250U;
    }
    return 33U;
}

bool needs_full_speed(DisplayMode mode) noexcept {
    return mode == DisplayMode::Active || mode == DisplayMode::Dimmed;
}

bool realtime_sync_allowed(DisplayMode mode, bool external_power) noexcept {
    return external_power || needs_full_speed(mode);
}

int DisplayPowerModel::brightness_for(DisplayMode mode) const noexcept {
    const int active = snapshot_.settings.active_brightness_percent;
    switch (mode) {
        case DisplayMode::Active: return active;
        case DisplayMode::Dimmed: return std::clamp(active / 2, 1, 50);
        case DisplayMode::AlwaysOn: return std::clamp(active * 3 / 10, 1, 30);
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

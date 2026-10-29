#pragma once

#include <cstdint>
#include <string>

#include "aura/appearance.hpp"

namespace aura {

// One accelerometer reading in g, in the display frame: +z points out of the
// screen, +y towards the top edge, +x towards the right edge.
struct ImuSample {
    float ax{0.0F};
    float ay{0.0F};
    float az{0.0F};
    std::uint32_t t_ms{0};
};

enum class ImuGesture { None, Raise, FaceDown, FaceUp };

// Raise-to-wake and flip detection from accelerometer samples (25-100 Hz).
// Pure logic: the QMI8658 adapter (firmware) and the simulator both feed it.
class ImuGestureDetector {
public:
    ImuGesture push(const ImuSample& sample);
    bool face_down() const noexcept { return face_down_; }
    void reset() noexcept;

private:
    bool initialized_{false};
    float gx_{0.0F};
    float gy_{0.0F};
    float gz_{0.0F};
    std::uint32_t last_ms_{0};
    // Raise: the screen was not facing the viewer, then motion, then it settles
    // facing up and towards the viewer.
    std::uint32_t away_until_ms_{0};
    std::uint32_t motion_at_ms_{0};
    std::uint32_t settled_since_ms_{0};
    std::uint32_t last_raise_ms_{0};
    bool raise_seen_{false};
    // Flip: screen down against a surface for a while.
    std::uint32_t down_since_ms_{0};
    std::uint32_t up_since_ms_{0};
    bool face_down_{false};
};

enum class DisplayMode { Active, Dimmed, AlwaysOn, Off };

struct DisplaySettings {
    bool always_on{true};
    bool raise_to_wake{true};
    bool flip_to_mute{true};
    int active_brightness_percent{100};
    std::uint32_t timeout_ms{30000};
    std::uint32_t dim_ms{5000};
    Appearance appearance{Appearance::Halo};
};

std::string serialize_display_settings(const DisplaySettings& settings);
DisplaySettings parse_display_settings(const std::string& data);

struct DisplaySnapshot {
    DisplayMode mode{DisplayMode::Active};
    DisplaySettings settings{};
    int brightness_percent{100};
    // Always-on clock burn-in protection: the content moves a few pixels a minute.
    int shift_x{0};
    int shift_y{0};
    bool muted_by_flip{false};
    bool imu_available{false};
    std::uint64_t sequence{0};
};

// Screen power: active -> dimmed -> always-on clock (or off). Touch, the
// button and a raise wake it; face-down turns it off and silences the device.
class DisplayPowerModel {
public:
    void set_settings(const DisplaySettings& settings);
    void set_imu_available(bool available);
    // Returns true when the screen was not active: that first touch only wakes it.
    bool user_activity(std::uint64_t now_ms);
    void gesture(ImuGesture gesture, std::uint64_t now_ms);
    // An alert, a running assistant turn, recording or playback keeps it awake.
    void hold_awake(bool hold, std::uint64_t now_ms);
    void sleep_now(std::uint64_t now_ms);
    void tick(std::uint64_t now_ms);
    void set_minute(std::int64_t minute_index);

    const DisplaySnapshot& snapshot() const noexcept { return snapshot_; }

private:
    void set_mode(DisplayMode mode);
    int brightness_for(DisplayMode mode) const noexcept;
    void changed() noexcept { ++snapshot_.sequence; }

    DisplaySnapshot snapshot_{};
    std::uint64_t last_activity_ms_{0};
    bool hold_{false};
};

const char* display_mode_name(DisplayMode mode) noexcept;

}  // namespace aura

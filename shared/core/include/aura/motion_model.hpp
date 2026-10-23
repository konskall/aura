#pragma once

#include <cstdint>

namespace aura {

enum class MotionMode { None, Idle, Listening, Thinking, Speaking, Success, Error };

struct MotionFrame {
    int scale{256};
    int opacity{255};
    int rotation_tenths{0};
    float pulse{0.0F};
    float amplitude{0.0F};
};

class MotionModel {
public:
    void enter(MotionMode mode) noexcept;
    void leave() noexcept;
    void set_reduced(bool reduced) noexcept { reduced_ = reduced; }
    void set_amplitude(float amplitude) noexcept;
    MotionFrame tick(std::uint32_t elapsed_ms) noexcept;
    MotionMode mode() const noexcept { return mode_; }
    bool active() const noexcept { return active_; }
    std::uint64_t update_count() const noexcept { return update_count_; }
    std::uint32_t elapsed_ms() const noexcept { return elapsed_ms_; }

private:
    MotionMode mode_{MotionMode::None};
    bool active_{false};
    bool reduced_{false};
    float amplitude_{0.0F};
    std::uint32_t elapsed_ms_{0};
    std::uint64_t update_count_{0};
};

}  // namespace aura


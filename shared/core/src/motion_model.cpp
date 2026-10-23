#include "aura/motion_model.hpp"

#include <algorithm>
#include <cmath>

namespace aura {

void MotionModel::enter(MotionMode mode) noexcept {
    mode_ = mode;
    active_ = mode != MotionMode::None;
    elapsed_ms_ = 0;
}

void MotionModel::leave() noexcept {
    mode_ = MotionMode::None;
    active_ = false;
    elapsed_ms_ = 0;
    amplitude_ = 0.0F;
}

void MotionModel::set_amplitude(float amplitude) noexcept {
    amplitude_ = std::clamp(amplitude, 0.0F, 1.0F);
}

MotionFrame MotionModel::tick(std::uint32_t elapsed_ms) noexcept {
    MotionFrame frame{};
    frame.amplitude = amplitude_;
    if (!active_) return frame;
    elapsed_ms_ += elapsed_ms;
    ++update_count_;
    if (reduced_) {
        frame.scale = 256;
        frame.opacity = 246;
        frame.pulse = 0.25F;
        return frame;
    }

    const float seconds = static_cast<float>(elapsed_ms_) / 1000.0F;
    const float slow = 0.5F + 0.5F * std::sin(seconds * 1.95F);
    const float fast = 0.5F + 0.5F * std::sin(seconds * 7.2F);
    frame.pulse = slow;

    switch (mode_) {
        case MotionMode::Idle:
            frame.scale = 250 + static_cast<int>(slow * 10.0F);
            frame.opacity = 232 + static_cast<int>(slow * 23.0F);
            frame.rotation_tenths = static_cast<int>(elapsed_ms_ / 18U) % 3600;
            break;
        case MotionMode::Listening: {
            const float energy = std::max(amplitude_, 0.12F + fast * 0.18F);
            frame.scale = 248 + static_cast<int>(energy * 18.0F);
            frame.opacity = 230 + static_cast<int>(energy * 25.0F);
            frame.pulse = energy;
            break;
        }
        case MotionMode::Thinking:
            frame.scale = 252 + static_cast<int>(slow * 8.0F);
            frame.opacity = 236 + static_cast<int>(fast * 19.0F);
            frame.rotation_tenths = static_cast<int>(elapsed_ms_ / 7U) % 3600;
            break;
        case MotionMode::Speaking: {
            const float energy = std::max(amplitude_, 0.04F);
            frame.scale = 248 + static_cast<int>(energy * 22.0F);
            frame.opacity = 225 + static_cast<int>(energy * 30.0F);
            frame.pulse = energy;
            break;
        }
        case MotionMode::Success:
            frame.scale = 256 + static_cast<int>(std::sin(std::min(seconds, 1.0F) * 3.14159F) * 12.0F);
            break;
        case MotionMode::Error:
            frame.opacity = 238;
            frame.pulse = 0.0F;
            break;
        case MotionMode::None:
            break;
    }
    return frame;
}

}  // namespace aura


#pragma once

#include <array>
#include <cstddef>

namespace aura {

template <typename Route, std::size_t Capacity = 12>
class NavigationStack {
public:
    explicit NavigationStack(Route home) : home_(home), current_(home) {}

    Route current() const noexcept { return current_; }
    Route home() const noexcept { return home_; }
    std::size_t depth() const noexcept { return depth_; }

    void navigate(Route route) noexcept {
        if (route == current_) return;
        if (route == home_) {
            reset();
            return;
        }
        if (depth_ == Capacity) {
            for (std::size_t index = 1; index < Capacity; ++index) history_[index - 1] = history_[index];
            --depth_;
        }
        history_[depth_++] = current_;
        current_ = route;
    }

    Route back() noexcept {
        if (depth_ == 0) {
            current_ = home_;
            return current_;
        }
        current_ = history_[--depth_];
        return current_;
    }

    void reset() noexcept {
        depth_ = 0;
        current_ = home_;
    }

    void replace(Route route) noexcept { current_ = route; }

private:
    Route home_;
    Route current_;
    std::array<Route, Capacity> history_{};
    std::size_t depth_{0};
};

}  // namespace aura


#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace aura {

// Durations sampled over a window: how many, their mean and the longest.
class DurationStats {
public:
    void add(std::uint32_t microseconds) noexcept;
    void reset() noexcept;
    std::uint32_t count() const noexcept { return count_; }
    std::uint32_t mean_us() const noexcept;
    std::uint32_t max_us() const noexcept { return max_; }

private:
    std::uint64_t total_{};
    std::uint32_t count_{};
    std::uint32_t max_{};
};

// What the first boot on a board must report, because the simulator cannot:
// how long LVGL takes per frame and the main loop per pass, what Synapse costs,
// and how much internal RAM and PSRAM stay free. The firmware prints it as one
// line on the serial console (format_runtime_metrics).
struct RuntimeMetrics {
    std::uint32_t uptime_ms{};
    DurationStats frame;  // one LVGL render of the changed areas, flushing included
    DurationStats loop;   // one pass of the main loop under the display lock
    std::uint32_t presence_frame_ms{};  // the last Synapse frame's drawing time
    bool presence_reduced{};
    double presence_fps{};
    std::size_t internal_free{};
    std::size_t internal_min_free{};
    std::size_t internal_largest{};
    std::size_t psram_free{};
    std::size_t psram_min_free{};
    std::size_t psram_largest{};
};

std::string format_runtime_metrics(const RuntimeMetrics& metrics);

}  // namespace aura

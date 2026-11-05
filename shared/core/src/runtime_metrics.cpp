#include "aura/runtime_metrics.hpp"

#include <algorithm>
#include <cstdio>

namespace aura {

void DurationStats::add(std::uint32_t microseconds) noexcept {
    total_ += microseconds;
    ++count_;
    max_ = std::max(max_, microseconds);
}

void DurationStats::reset() noexcept {
    total_ = 0;
    count_ = 0;
    max_ = 0;
}

std::uint32_t DurationStats::mean_us() const noexcept {
    return count_ == 0 ? 0U : static_cast<std::uint32_t>((total_ + count_ / 2) / count_);
}

namespace {

double milliseconds(std::uint32_t microseconds) { return static_cast<double>(microseconds) / 1000.0; }

unsigned long kibibytes(std::size_t bytes) { return static_cast<unsigned long>(bytes / 1024U); }

}  // namespace

std::string format_runtime_metrics(const RuntimeMetrics& metrics) {
    char line[320];
    std::snprintf(line, sizeof(line),
                  "t=%.1f s | frame avg %.1f max %.1f ms n=%lu | loop avg %.1f max %.1f ms n=%lu | "
                  "synapse %lu ms %s %.1f fps | internal free %lu min %lu largest %lu KiB | "
                  "psram free %lu min %lu largest %lu KiB",
                  static_cast<double>(metrics.uptime_ms) / 1000.0, milliseconds(metrics.frame.mean_us()),
                  milliseconds(metrics.frame.max_us()), static_cast<unsigned long>(metrics.frame.count()),
                  milliseconds(metrics.loop.mean_us()), milliseconds(metrics.loop.max_us()),
                  static_cast<unsigned long>(metrics.loop.count()),
                  static_cast<unsigned long>(metrics.presence_frame_ms), metrics.presence_reduced ? "reduced" : "full",
                  metrics.presence_fps, kibibytes(metrics.internal_free), kibibytes(metrics.internal_min_free),
                  kibibytes(metrics.internal_largest), kibibytes(metrics.psram_free), kibibytes(metrics.psram_min_free),
                  kibibytes(metrics.psram_largest));
    return line;
}

}  // namespace aura

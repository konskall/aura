#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aura {

// One reading of the battery and of what draws from it.
struct BatterySample {
    std::uint64_t at_ms{0};        // monotonic, the platform's own clock
    std::int64_t unix_seconds{0};  // 0 while the clock is not set
    int percent{0};
    int battery_mv{0};
    bool external_power{false};
    bool charging{false};
    bool screen_on{false};
    bool bluetooth_on{false};
    bool wifi_on{false};
};

// How long the battery lasts (the owner, 2026-11-19): a sample every five
// minutes (and at once when the USB comes or goes), the drain since the USB
// last went out, the time left at that rate, and the card's battery-log.csv.
class BatteryHistory {
public:
    static constexpr std::uint64_t kSampleMs = 5ULL * 60ULL * 1000ULL;
    static constexpr std::size_t kMaxSamples = 288;  // a day
    static constexpr std::uint64_t kMeasureMs = 30ULL * 60ULL * 1000ULL;
    static constexpr int kMeasurePercent = 2;

    // A reading, as often as the platform reads the battery. True when it was
    // kept as a sample (the log changed).
    bool add(const BatterySample& reading);
    // Percent an hour since the USB last went out, by least squares over the
    // samples, once at least kMeasureMs and kMeasurePercent have passed; none
    // on USB power.
    std::optional<float> drain_percent_per_hour() const;
    // At that drain, from the latest percentage.
    std::optional<int> minutes_left() const;
    // Minutes since the USB last went out; -1 on USB power or with no sample.
    int minutes_on_battery() const;
    // A header line, then one line a sample: time_utc,percent,battery_mv,usb,
    // charging,screen,bluetooth,wifi (time empty while the clock was not set).
    std::string csv() const;
    const std::vector<BatterySample>& samples() const noexcept { return samples_; }

private:
    // The first sample of the current stretch on battery, or samples_.size().
    std::size_t battery_start() const;
    std::vector<BatterySample> samples_;
};

}  // namespace aura

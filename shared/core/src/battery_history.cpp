#include "aura/battery_history.hpp"

#include <cmath>
#include <cstdio>

namespace aura {

namespace {

// Days since 1970-01-01 to the civil date (proleptic Gregorian, UTC).
void civil_from_days(std::int64_t days, int& year, int& month, int& day) {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto day_of_era = static_cast<unsigned>(days - era * 146097);
    const unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned shifted_month = (5 * day_of_year + 2) / 153;
    day = static_cast<int>(day_of_year - (153 * shifted_month + 2) / 5 + 1);
    month = static_cast<int>(shifted_month < 10 ? shifted_month + 3 : shifted_month - 9);
    year = static_cast<int>(static_cast<std::int64_t>(year_of_era) + era * 400 + (month <= 2 ? 1 : 0));
}

}  // namespace

bool keep_previous_battery_log(const std::string& directory) {
    const std::string current = directory + "/" + kBatteryLogFile;
    const std::string backup = current + ".bak";
    const std::string previous = directory + "/" + kBatteryLogPreviousFile;
    const auto exists = [](const std::string& path) {
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) return false;
        std::fclose(file);
        return true;
    };
    // The store writes through a .bak: alone, it is a write cut short.
    const bool has_current = exists(current);
    const std::string& source = has_current ? current : backup;
    if (!has_current && !exists(backup)) return false;
    std::remove(previous.c_str());  // FAT renames onto no existing file
    if (std::rename(source.c_str(), previous.c_str()) != 0) return false;
    if (has_current) std::remove(backup.c_str());
    return true;
}

bool BatteryHistory::add(const BatterySample& reading) {
    if (!samples_.empty()) {
        const auto& last = samples_.back();
        const bool power_changed = last.external_power != reading.external_power;
        if (!power_changed && reading.at_ms - last.at_ms < kSampleMs) return false;
    }
    if (samples_.size() == kMaxSamples) samples_.erase(samples_.begin());
    samples_.push_back(reading);
    return true;
}

std::size_t BatteryHistory::battery_start() const {
    if (samples_.empty() || samples_.back().external_power) return samples_.size();
    std::size_t start = samples_.size() - 1;
    while (start > 0 && !samples_[start - 1].external_power) --start;
    return start;
}

int BatteryHistory::minutes_on_battery() const {
    const std::size_t start = battery_start();
    if (start == samples_.size()) return -1;
    return static_cast<int>((samples_.back().at_ms - samples_[start].at_ms) / 60000ULL);
}

std::optional<float> BatteryHistory::drain_percent_per_hour() const {
    const std::size_t start = battery_start();
    if (start == samples_.size()) return std::nullopt;
    const auto& first = samples_[start];
    const auto& last = samples_.back();
    if (last.at_ms - first.at_ms < kMeasureMs || first.percent - last.percent < kMeasurePercent) return std::nullopt;
    // Least squares of percent over hours: the gauge moves in whole percents,
    // so the line through all samples is steadier than the two ends.
    double sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
    const auto count = static_cast<double>(samples_.size() - start);
    for (std::size_t index = start; index < samples_.size(); ++index) {
        const double hours = static_cast<double>(samples_[index].at_ms - first.at_ms) / 3600000.0;
        const double percent = samples_[index].percent;
        sum_x += hours;
        sum_y += percent;
        sum_xx += hours * hours;
        sum_xy += hours * percent;
    }
    const double denominator = count * sum_xx - sum_x * sum_x;
    if (denominator <= 0) return std::nullopt;
    const double slope = (count * sum_xy - sum_x * sum_y) / denominator;
    if (!(slope < 0)) return std::nullopt;
    return static_cast<float>(-slope);
}

std::optional<int> BatteryHistory::minutes_left() const {
    const auto drain = drain_percent_per_hour();
    if (!drain || *drain <= 0.0F) return std::nullopt;
    return static_cast<int>(std::lround(static_cast<double>(samples_.back().percent) / *drain * 60.0));
}

std::string BatteryHistory::csv() const {
    std::string text = "time_utc,percent,battery_mv,usb,charging,screen,bluetooth,wifi\n";
    for (const auto& sample : samples_) {
        char time[64] = "";  // room for any int the compiler can imagine
        if (sample.unix_seconds > 0) {
            const std::int64_t days = sample.unix_seconds / 86400;
            const std::int64_t seconds = sample.unix_seconds % 86400;
            int year = 0, month = 0, day = 0;
            civil_from_days(days, year, month, day);
            std::snprintf(time, sizeof(time), "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day,
                          static_cast<int>(seconds / 3600), static_cast<int>(seconds % 3600 / 60),
                          static_cast<int>(seconds % 60));
        }
        char line[160];
        std::snprintf(line, sizeof(line), "%s,%d,%d,%d,%d,%d,%d,%d\n", time, sample.percent, sample.battery_mv,
                      sample.external_power ? 1 : 0, sample.charging ? 1 : 0, sample.screen_on ? 1 : 0,
                      sample.bluetooth_on ? 1 : 0, sample.wifi_on ? 1 : 0);
        text += line;
    }
    return text;
}

}  // namespace aura

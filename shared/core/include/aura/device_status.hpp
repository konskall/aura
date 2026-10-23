#pragma once

#include <cstdint>
#include <string>

namespace aura {

enum class StatusOrigin { Unavailable, Live, Simulated };

struct DeviceStatusSnapshot {
    bool clock_valid{false};
    int hour{0};
    int minute{0};
    StatusOrigin clock_origin{StatusOrigin::Unavailable};
    // Calendar date for the Home glance; weekday 0 is Sunday (struct tm).
    bool date_valid{false};
    int year{0};
    int month{0};
    int day{0};
    int weekday{0};
    bool power_known{false};
    int battery_percent{0};
    bool charging{false};
    StatusOrigin power_origin{StatusOrigin::Unavailable};
    bool connection_known{false};
    bool online{false};
    int wifi_signal_level{0};
    StatusOrigin connection_origin{StatusOrigin::Unavailable};
    bool bluetooth_known{false};
    bool bluetooth_enabled{false};
    StatusOrigin bluetooth_origin{StatusOrigin::Unavailable};
    std::uint64_t sequence{0};
};

class DeviceStatusModel {
public:
    void set_clock(int hour, int minute, StatusOrigin origin) noexcept;
    void set_clock_unavailable() noexcept;
    // An impossible date (month 1-12, day 1-31, weekday 0-6) is ignored.
    void set_date(int year, int month, int day, int weekday, StatusOrigin origin) noexcept;
    void set_date_unavailable() noexcept;
    void set_power(int percent, bool charging, StatusOrigin origin) noexcept;
    void set_power_unavailable() noexcept;
    void set_connection(bool online, StatusOrigin origin, int signal_level = 3) noexcept;
    void set_connection_unavailable() noexcept;
    void set_bluetooth(bool enabled, StatusOrigin origin) noexcept;
    void set_bluetooth_unavailable() noexcept;
    const DeviceStatusSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    DeviceStatusSnapshot snapshot_{};
};

const char* status_origin_name(StatusOrigin origin) noexcept;

}  // namespace aura

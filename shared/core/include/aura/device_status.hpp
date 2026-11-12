#pragma once

#include <cstdint>
#include <string>

namespace aura {

enum class StatusOrigin { Unavailable, Live, Simulated };

// The charger's stage, as the power manager reports it.
enum class ChargeStage { Trickle, Precharge, ConstantCurrent, ConstantVoltage, Done, Stopped, Unknown };

// What the power manager measures besides the percentage. It reports no
// health, cycle count or measured capacity; the capacity is the battery's rating.
struct BatteryDetails {
    bool known{false};
    int battery_mv{0};
    int usb_mv{0};  // 0 without USB
    int system_mv{0};
    bool chip_temperature_known{false};
    int chip_celsius{0};
    ChargeStage stage{ChargeStage::Unknown};
    int rated_mah{0};  // 0 when not known
    StatusOrigin origin{StatusOrigin::Unavailable};
    bool operator==(const BatteryDetails& other) const noexcept {
        return known == other.known && battery_mv == other.battery_mv && usb_mv == other.usb_mv &&
               system_mv == other.system_mv && chip_temperature_known == other.chip_temperature_known &&
               chip_celsius == other.chip_celsius && stage == other.stage && rated_mah == other.rated_mah &&
               origin == other.origin;
    }
    bool operator!=(const BatteryDetails& other) const noexcept { return !(*this == other); }
};

// The microSD card: whether one is in, and its space.
struct StorageStatus {
    bool known{false};
    bool present{false};
    std::uint64_t total_bytes{0};
    std::uint64_t free_bytes{0};
    StatusOrigin origin{StatusOrigin::Unavailable};
    bool operator==(const StorageStatus& other) const noexcept {
        return known == other.known && present == other.present && total_bytes == other.total_bytes &&
               free_bytes == other.free_bytes && origin == other.origin;
    }
    bool operator!=(const StorageStatus& other) const noexcept { return !(*this == other); }
};

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
    BatteryDetails battery{};
    StorageStatus storage{};
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
    void set_battery_details(const BatteryDetails& details) noexcept;
    // Free space above the card's size is cut to it.
    void set_storage(StorageStatus storage) noexcept;
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

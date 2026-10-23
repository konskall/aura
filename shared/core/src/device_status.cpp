#include "aura/device_status.hpp"

#include <algorithm>

namespace aura {

// Every setter bumps the sequence only on a real change: the platforms poll
// once per second, and an unchanged clock or link must not redraw the UI.

void DeviceStatusModel::set_clock(int hour, int minute, StatusOrigin origin) noexcept {
    const bool valid = origin != StatusOrigin::Unavailable;
    hour = std::clamp(hour, 0, 23);
    minute = std::clamp(minute, 0, 59);
    if (snapshot_.clock_valid == valid && snapshot_.hour == hour && snapshot_.minute == minute &&
        snapshot_.clock_origin == origin) return;
    snapshot_.clock_valid = valid;
    snapshot_.hour = hour;
    snapshot_.minute = minute;
    snapshot_.clock_origin = origin;
    ++snapshot_.sequence;
}

void DeviceStatusModel::set_clock_unavailable() noexcept {
    if (!snapshot_.clock_valid && snapshot_.clock_origin == StatusOrigin::Unavailable) return;
    snapshot_.clock_valid = false;
    snapshot_.clock_origin = StatusOrigin::Unavailable;
    ++snapshot_.sequence;
}

void DeviceStatusModel::set_date(int year, int month, int day, int weekday, StatusOrigin origin) noexcept {
    if (origin == StatusOrigin::Unavailable) {
        set_date_unavailable();
        return;
    }
    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31 ||
        weekday < 0 || weekday > 6) return;
    if (snapshot_.date_valid && snapshot_.year == year && snapshot_.month == month &&
        snapshot_.day == day && snapshot_.weekday == weekday) return;
    snapshot_.date_valid = true;
    snapshot_.year = year;
    snapshot_.month = month;
    snapshot_.day = day;
    snapshot_.weekday = weekday;
    ++snapshot_.sequence;
}

void DeviceStatusModel::set_date_unavailable() noexcept {
    if (!snapshot_.date_valid) return;
    snapshot_.date_valid = false;
    ++snapshot_.sequence;
}

void DeviceStatusModel::set_power(int percent, bool charging, StatusOrigin origin) noexcept {
    const bool known = origin != StatusOrigin::Unavailable;
    percent = std::clamp(percent, 0, 100);
    if (snapshot_.power_known == known && snapshot_.battery_percent == percent &&
        snapshot_.charging == charging && snapshot_.power_origin == origin) return;
    snapshot_.power_known = known;
    snapshot_.battery_percent = percent;
    snapshot_.charging = charging;
    snapshot_.power_origin = origin;
    ++snapshot_.sequence;
}

void DeviceStatusModel::set_power_unavailable() noexcept {
    if (!snapshot_.power_known && !snapshot_.charging && snapshot_.power_origin == StatusOrigin::Unavailable) return;
    snapshot_.power_known = false;
    snapshot_.charging = false;
    snapshot_.power_origin = StatusOrigin::Unavailable;
    ++snapshot_.sequence;
}

void DeviceStatusModel::set_connection(bool online, StatusOrigin origin, int signal_level) noexcept {
    const bool known = origin != StatusOrigin::Unavailable;
    const int level = online ? std::clamp(signal_level, 1, 3) : 0;
    if (snapshot_.connection_known == known && snapshot_.online == online &&
        snapshot_.wifi_signal_level == level && snapshot_.connection_origin == origin) return;
    snapshot_.connection_known = known;
    snapshot_.online = online;
    snapshot_.wifi_signal_level = level;
    snapshot_.connection_origin = origin;
    ++snapshot_.sequence;
}

void DeviceStatusModel::set_connection_unavailable() noexcept {
    if (!snapshot_.connection_known && !snapshot_.online &&
        snapshot_.connection_origin == StatusOrigin::Unavailable) return;
    snapshot_.connection_known = false;
    snapshot_.online = false;
    snapshot_.wifi_signal_level = 0;
    snapshot_.connection_origin = StatusOrigin::Unavailable;
    ++snapshot_.sequence;
}

void DeviceStatusModel::set_bluetooth(bool enabled, StatusOrigin origin) noexcept {
    const bool known = origin != StatusOrigin::Unavailable;
    if (snapshot_.bluetooth_known == known && snapshot_.bluetooth_enabled == enabled &&
        snapshot_.bluetooth_origin == origin) return;
    snapshot_.bluetooth_known = known;
    snapshot_.bluetooth_enabled = enabled;
    snapshot_.bluetooth_origin = origin;
    ++snapshot_.sequence;
}

void DeviceStatusModel::set_bluetooth_unavailable() noexcept {
    if (!snapshot_.bluetooth_known && !snapshot_.bluetooth_enabled &&
        snapshot_.bluetooth_origin == StatusOrigin::Unavailable) return;
    snapshot_.bluetooth_known = false;
    snapshot_.bluetooth_enabled = false;
    snapshot_.bluetooth_origin = StatusOrigin::Unavailable;
    ++snapshot_.sequence;
}

const char* status_origin_name(StatusOrigin origin) noexcept {
    switch (origin) {
        case StatusOrigin::Live: return "LIVE";
        case StatusOrigin::Simulated: return "SIMULATED";
        case StatusOrigin::Unavailable: return "UNAVAILABLE";
    }
    return "UNAVAILABLE";
}

}  // namespace aura

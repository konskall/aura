#pragma once

#include "aura/clock_services.hpp"
#include "aura/device_status.hpp"
#include "aura/weather.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aura {

enum class BriefingPeriod { Morning, Afternoon, Evening, Night };

enum class BriefingItemKind { Weather, Alarm, Reminders, Timers, Messages, PhoneNotifications };

// Structured facts; the UI turns them into localized text and speech.
struct BriefingItem {
    BriefingItemKind kind{BriefingItemKind::Weather};
    int count{0};
    int hour{0};
    int minute{0};
    int value{0};       // temperature now / timer seconds left
    int high{0};
    int low{0};
    int rain_percent{-1};
    WeatherCondition condition{WeatherCondition::Unknown};
    bool demo{false};
    std::string text;   // city, alarm label or next reminder text
};

struct Briefing {
    BriefingPeriod period{BriefingPeriod::Morning};
    bool date_valid{false};
    int weekday{0};
    int day{0};
    int month{0};
    std::vector<BriefingItem> items;
};

struct BriefingInput {
    const DeviceStatusSnapshot* status{};
    const WeatherSnapshot* weather{};
    const ClockSnapshot* clock{};
    int unread_messages{0};
    int phone_notifications{0};
};

Briefing build_briefing(const BriefingInput& input);
BriefingPeriod briefing_period(int hour) noexcept;
// The morning briefing opens by itself once a day, on the first wake between
// 05:00 and 11:59; `last_shown_day` is days_from_civil() of the last showing.
bool should_auto_show_briefing(const DeviceStatusSnapshot& status, std::int64_t last_shown_day) noexcept;

}  // namespace aura

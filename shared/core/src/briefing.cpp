#include "aura/briefing.hpp"

#include "aura/text_util.hpp"

#include <cmath>

namespace aura {

BriefingPeriod briefing_period(int hour) noexcept {
    if (hour >= 5 && hour < 12) return BriefingPeriod::Morning;
    if (hour >= 12 && hour < 18) return BriefingPeriod::Afternoon;
    if (hour >= 18 && hour < 23) return BriefingPeriod::Evening;
    return BriefingPeriod::Night;
}

Briefing build_briefing(const BriefingInput& input) {
    Briefing briefing;
    if (input.status && input.status->clock_valid) briefing.period = briefing_period(input.status->hour);
    if (input.status && input.status->date_valid) {
        briefing.date_valid = true;
        briefing.year = input.status->year;
        briefing.weekday = input.status->weekday;
        briefing.day = input.status->day;
        briefing.month = input.status->month;

        const auto calendar = greek_calendar_day(input.status->year, input.status->month, input.status->day);
        if (!calendar.namedays.empty()) {
            BriefingItem item;
            item.kind = BriefingItemKind::Namedays;
            item.text = join_namedays(calendar.namedays);
            briefing.items.push_back(std::move(item));
        }
        if (const auto next = next_greek_holiday(input.status->year, input.status->month, input.status->day)) {
            BriefingItem item;
            item.kind = BriefingItemKind::NextHoliday;
            item.value = next->days_until;
            item.date_year = next->date.year;
            item.date_month = next->date.month;
            item.date_day = next->date.day;
            item.text = input.language == InterfaceLanguage::English ? greek_holiday_name_en(next->kind) :
                                                                      greek_holiday_name_el(next->kind);
            briefing.items.push_back(std::move(item));
        }
    }

    if (input.fuel && input.fuel->state == FuelPriceState::Ready) {
        const auto& data = input.fuel->data;
        BriefingItem item;
        item.kind = BriefingItemKind::FuelPrices;
        item.value = data.unleaded95_millieur_per_litre;
        item.high = data.diesel_millieur_per_litre;
        item.low = data.lpg_millieur_per_litre.value_or(-1);
        item.rain_percent = data.stale ? 1 : 0;
        item.demo = data.demo;
        item.text = data.period_date;
        briefing.items.push_back(std::move(item));
    }

    if (input.weather && input.weather->has_data) {
        const auto& data = input.weather->data;
        BriefingItem item;
        item.kind = BriefingItemKind::Weather;
        item.value = static_cast<int>(std::lround(data.temperature_c));
        item.high = static_cast<int>(std::lround(data.daily_maximum_c));
        item.low = static_cast<int>(std::lround(data.daily_minimum_c));
        item.condition = data.condition;
        item.rain_percent = data.daily_count > 0 ? data.daily[0].rain_probability_max_percent :
                                                   data.rain_probability_percent;
        item.demo = data.origin == WeatherDataOrigin::Demo;
        item.text = data.location.name;
        briefing.items.push_back(std::move(item));
    }

    if (input.clock) {
        const auto& clock = *input.clock;
        if (auto alarm = next_alarm(clock)) {
            // Only alarms in the next 24 hours belong to "today".
            BriefingItem item;
            item.kind = BriefingItemKind::Alarm;
            item.hour = alarm->hour;
            item.minute = alarm->minute;
            item.text = alarm->label;
            item.count = static_cast<int>(clock.alarms.size());
            briefing.items.push_back(std::move(item));
        }
        if (!clock.reminders.empty() && clock.clock_valid) {
            const std::int64_t day_end = (clock.now_minute / 1440 + 1) * 1440;
            int today = 0;
            for (const auto& reminder : clock.reminders) {
                if (reminder.due_minute < day_end) ++today;
            }
            if (today > 0) {
                const auto& next = clock.reminders.front();
                const auto due = from_minute_index(next.due_minute);
                BriefingItem item;
                item.kind = BriefingItemKind::Reminders;
                item.count = today;
                item.hour = due.hour;
                item.minute = due.minute;
                item.text = next.text;
                briefing.items.push_back(std::move(item));
            }
        }
        if (!clock.timers.empty()) {
            // The running timer that ends first (a paused one only if all are).
            const TimerItem* soonest = nullptr;
            for (const auto& timer : clock.timers) {
                if (!timer.paused && (!soonest || timer.remaining_ms < soonest->remaining_ms)) soonest = &timer;
            }
            if (!soonest) soonest = &clock.timers.front();
            BriefingItem item;
            item.kind = BriefingItemKind::Timers;
            item.count = static_cast<int>(clock.timers.size());
            item.value = static_cast<int>((soonest->remaining_ms + 999U) / 1000U);
            item.text = soonest->label;
            briefing.items.push_back(std::move(item));
        }
    }

    if (input.unread_messages > 0) {
        BriefingItem item;
        item.kind = BriefingItemKind::Messages;
        item.count = input.unread_messages;
        briefing.items.push_back(std::move(item));
    }
    if (input.phone_notifications > 0) {
        BriefingItem item;
        item.kind = BriefingItemKind::PhoneNotifications;
        item.count = input.phone_notifications;
        briefing.items.push_back(std::move(item));
    }
    return briefing;
}

bool should_auto_show_briefing(const DeviceStatusSnapshot& status, std::int64_t last_shown_day) noexcept {
    if (!status.clock_valid || !status.date_valid) return false;
    if (status.hour < 5 || status.hour >= 12) return false;
    return days_from_civil(status.year, status.month, status.day) != last_shown_day;
}

}  // namespace aura

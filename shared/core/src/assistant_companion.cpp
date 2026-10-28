#include "aura/assistant_companion.hpp"

#include "aura/greek_calendar.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace aura {
namespace {

constexpr std::size_t kContextLimit = 3000;
constexpr const char* kWeekdays[]{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

const char* condition_name(WeatherCondition condition) {
    switch (condition) {
        case WeatherCondition::Clear: return "clear";
        case WeatherCondition::PartlyCloudy: return "partly cloudy";
        case WeatherCondition::Cloudy: return "cloudy";
        case WeatherCondition::Fog: return "fog";
        case WeatherCondition::Drizzle: return "drizzle";
        case WeatherCondition::Rain: return "rain";
        case WeatherCondition::Snow: return "snow";
        case WeatherCondition::Thunderstorm: return "thunderstorm";
        case WeatherCondition::Windy: return "windy";
        case WeatherCondition::Unknown: break;
    }
    return "unknown";
}

std::string format(const char* pattern, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char* pattern, ...) {
    char buffer[256];
    va_list arguments;
    va_start(arguments, pattern);
    std::vsnprintf(buffer, sizeof(buffer), pattern, arguments);
    va_end(arguments);
    return buffer;
}

std::string duration_text(std::uint64_t seconds) {
    const auto hours = seconds / 3600U;
    const auto minutes = (seconds % 3600U) / 60U;
    const auto rest = seconds % 60U;
    if (hours > 0) {
        return format("%u:%02u:%02u", static_cast<unsigned>(hours), static_cast<unsigned>(minutes),
                      static_cast<unsigned>(rest));
    }
    return format("%02u:%02u", static_cast<unsigned>(minutes), static_cast<unsigned>(rest));
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t`");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t`");
    return text.substr(first, last - first + 1);
}

bool parse_clock_text(const std::string& text, int& hour, int& minute) {
    const auto colon = text.find(':');
    if (colon == std::string::npos || colon == 0 || colon > 2 || text.size() != colon + 3) return false;
    int values[2]{0, 0};
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index == colon) continue;
        if (text[index] < '0' || text[index] > '9') return false;
        int& value = values[index < colon ? 0 : 1];
        value = value * 10 + (text[index] - '0');
    }
    if (values[0] > 23 || values[1] > 59) return false;
    hour = values[0];
    minute = values[1];
    return true;
}

bool parse_local_minute(const std::string& text, std::int64_t& minute) {
    // Exact, timezone-free local timestamp supplied by the device context.
    if (text.size() != 16 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':') return false;
    auto number = [&](std::size_t first, std::size_t count, int& value) {
        value = 0;
        for (std::size_t index = first; index < first + count; ++index) {
            if (text[index] < '0' || text[index] > '9') return false;
            value = value * 10 + (text[index] - '0');
        }
        return true;
    };
    LocalDateTime date;
    if (!number(0, 4, date.year) || !number(5, 2, date.month) || !number(8, 2, date.day) ||
        !number(11, 2, date.hour) || !number(14, 2, date.minute) || date.year < 2020 || date.year > 2100 ||
        date.month < 1 || date.month > 12 || date.day < 1 || date.day > 31 || date.hour > 23 || date.minute > 59) {
        return false;
    }
    minute = minute_index(date);
    const auto roundtrip = from_minute_index(minute);
    return roundtrip.year == date.year && roundtrip.month == date.month && roundtrip.day == date.day &&
           roundtrip.hour == date.hour && roundtrip.minute == date.minute;
}

std::optional<AssistantAction> validate_action(const FlatJsonObject& object) {
    const auto type = object.string("type");
    if (!type) return std::nullopt;
    AssistantAction action;
    if (*type == "timer") {
        action.type = AssistantActionType::Timer;
        std::int64_t seconds = 0;
        if (const auto value = object.integer("seconds")) seconds = *value;
        else if (const auto minutes = object.integer("minutes")) seconds = *minutes * 60;
        if (seconds < 1 || seconds > static_cast<std::int64_t>(ClockServicesModel::kMaxTimerSeconds)) return std::nullopt;
        action.seconds = static_cast<int>(seconds);
        action.label = sanitize_text(object.string("label").value_or(""), ClockServicesModel::kMaxLabelBytes);
        return action;
    }
    if (*type == "alarm") {
        action.type = AssistantActionType::Alarm;
        if (const auto time = object.string("time")) {
            if (!parse_clock_text(*time, action.hour, action.minute)) return std::nullopt;
        } else {
            const auto hour = object.integer("hour");
            const auto minute = object.integer("minute");
            if (!hour || *hour < 0 || *hour > 23 || (minute && (*minute < 0 || *minute > 59))) return std::nullopt;
            action.hour = static_cast<int>(*hour);
            action.minute = minute ? static_cast<int>(*minute) : 0;
        }
        const auto repeat = object.string("repeat").value_or("once");
        if (repeat != "once" && repeat != "daily") return std::nullopt;
        action.daily = repeat == "daily" || object.boolean("daily").value_or(false);
        action.label = sanitize_text(object.string("label").value_or(""), ClockServicesModel::kMaxLabelBytes);
        return action;
    }
    if (*type == "reminder") {
        action.type = AssistantActionType::Reminder;
        action.text = sanitize_text(object.string("text").value_or(""), ClockServicesModel::kMaxReminderBytes);
        if (action.text.empty()) return std::nullopt;
        if (const auto minutes = object.integer("in_minutes")) {
            if (*minutes < 1 || *minutes > 30 * 24 * 60) return std::nullopt;
            action.in_minutes = static_cast<int>(*minutes);
            return action;
        }
        const auto time = object.string("time");
        if (!time || !parse_clock_text(*time, action.hour, action.minute)) return std::nullopt;
        const auto day = object.string("day").value_or("today");
        if (day != "today" && day != "tomorrow") return std::nullopt;
        action.tomorrow = day == "tomorrow";
        return action;
    }
    if (*type == "note") {
        action.type = AssistantActionType::Note;
        action.text = sanitize_text(object.string("text").value_or(""), NotesModel::kMaxTextBytes, true);
        if (action.text.empty()) return std::nullopt;
        return action;
    }
    if (*type == "task" || *type == "shopping") {
        action.type = *type == "task" ? AssistantActionType::Task : AssistantActionType::Shopping;
        action.text = sanitize_text(object.string("text").value_or(""), OrganizerModel::kMaxTextBytes, true);
        if (action.text.empty()) return std::nullopt;
        return action;
    }
    if (*type == "calendar_event") {
        action.type = AssistantActionType::CalendarEvent;
        action.text = sanitize_text(object.string("text").value_or(""), OrganizerModel::kMaxTextBytes, true);
        const auto start = object.string("start");
        const auto duration = object.integer("duration_minutes").value_or(60);
        if (action.text.empty() || !start || !parse_local_minute(*start, action.start_minute) ||
            duration < 1 || duration > OrganizerModel::kMaxDurationMinutes) {
            return std::nullopt;
        }
        action.duration_minutes = static_cast<std::uint32_t>(duration);
        return action;
    }
    if (*type == "message") {
        action.type = AssistantActionType::Message;
        action.text = sanitize_text(object.string("text").value_or(""), 500, true);
        action.room = sanitize_text(object.string("room").value_or(""), 48);
        if (action.text.empty()) return std::nullopt;
        return action;
    }
    return std::nullopt;
}

}  // namespace

void ConversationMemory::record(const std::string& question, const std::string& answer, std::uint64_t now_ms) {
    if (!active(now_ms)) turns_.clear();
    ConversationTurn turn;
    turn.question = sanitize_text(question, kMaxTextBytes);
    turn.answer = sanitize_text(answer, kMaxTextBytes);
    if (turn.question.empty() || turn.answer.empty()) return;
    turns_.push_back(std::move(turn));
    while (turns_.size() > kMaxTurns) turns_.erase(turns_.begin());
    last_activity_ms_ = now_ms;
}

std::vector<ConversationTurn> ConversationMemory::recent(std::uint64_t now_ms) {
    if (!active(now_ms)) {
        clear();
        return {};
    }
    return turns_;
}

void ConversationMemory::clear() noexcept { turns_.clear(); }

bool ConversationMemory::active(std::uint64_t now_ms) const noexcept {
    if (turns_.empty()) return false;
    return now_ms < last_activity_ms_ || now_ms - last_activity_ms_ <= kExpiryMs;
}

bool question_requests_web_search(std::string_view question) {
    const auto folded = fold_text(question);
    if (folded.empty()) return false;
    const auto has = [&](std::string_view token) { return folded.find(token) != std::string::npos; };
    // Requests that explicitly name private/local collections stay on-device.
    if (has("σημειω") || has("λιστα αγορ") || has("εργασι") || has("μηνυμα") ||
        has("notification") || has("my notes") || has("shopping list") || has("my tasks")) {
        return false;
    }
    const bool greek = has("ψαξε") || has("αναζητησε") || has("βρες στο ιντερνετ") ||
                       has("βρες online") || has("βρες στο web");
    const bool english = has("search for") || has("search the web") || has("look up") ||
                         has("find online") || has("find on the web");
    return greek || english;
}

std::string build_device_context(const DeviceContextInput& input) {
    std::string out;
    auto add = [&out](const std::string& line) {
        out += line;
        out += '\n';
    };
    if (const auto* status = input.status) {
        if (status->clock_valid && status->date_valid && status->weekday >= 0 && status->weekday <= 6) {
            add(format("local time: %02d:%02d, %s %04d-%02d-%02d", status->hour, status->minute,
                       kWeekdays[status->weekday], status->year, status->month, status->day));
        } else if (status->clock_valid) {
            add(format("local time: %02d:%02d", status->hour, status->minute));
        } else {
            add("local time: unknown");
        }
        if (status->power_known) {
            add(format("battery: %d%%%s%s", status->battery_percent, status->charging ? ", charging" : "",
                       status->power_origin == StatusOrigin::Simulated ? " (simulated)" : ""));
        }
        if (status->connection_known) add(status->online ? "internet: connected" : "internet: offline");
        if (status->date_valid) {
            const auto calendar = greek_calendar_day(status->year, status->month, status->day);
            add(calendar.namedays.empty() ? "Greek namedays today: none in the local common-name calendar" :
                                           "Greek namedays today: " + join_namedays(calendar.namedays));
            if (calendar.holiday != GreekHolidayKind::None) {
                add(std::string("Greek public holiday today: ") + greek_holiday_name_en(calendar.holiday));
            }
            if (const auto next = next_greek_holiday(status->year, status->month, status->day, false)) {
                add(format("next Greek public holiday: %04d-%02d-%02d, %s", next->date.year, next->date.month,
                           next->date.day, greek_holiday_name_en(next->kind)));
            }
        }
    }
    if (const auto* weather = input.weather) {
        if (weather->has_data) {
            const auto& data = weather->data;
            std::string header = "weather for " + data.location.name + " (source: " +
                (data.provider == WeatherProvider::TlesDetector ? "tlesdetector" : "Open-Meteo");
            if (!data.observed_at.empty()) header += ", observed " + data.observed_at;
            header += ")";
            if (data.origin == WeatherDataOrigin::Demo) header += " DEMO sample data, not real weather";
            if (data.stale) header += " (cached, may be outdated)";
            add(header);
            add(format("now: %.0f C (feels %.0f C), %s, wind %.0f km/h, humidity %d%%, rain chance %d%%",
                       static_cast<double>(data.temperature_c), static_cast<double>(data.apparent_temperature_c),
                       condition_name(data.condition), static_cast<double>(data.wind_kmh), data.humidity_percent,
                       data.rain_probability_percent));
            add(format("today: min %.0f C, max %.0f C", static_cast<double>(data.daily_minimum_c),
                       static_cast<double>(data.daily_maximum_c)));
            std::string hourly;
            for (std::size_t index = 0; index < std::min<std::size_t>(data.hourly_count, 8); ++index) {
                const auto& hour = data.hourly[index];
                const auto t = hour.time.find('T');
                const std::string when = t == std::string::npos ? hour.time : hour.time.substr(t + 1, 5);
                hourly += format("%s %.0fC %s %d%%; ", when.c_str(), static_cast<double>(hour.temperature_c),
                                 condition_name(hour.condition), hour.rain_probability_percent);
            }
            if (!hourly.empty()) add("hourly: " + hourly);
            for (std::size_t index = 0; index < std::min<std::size_t>(data.daily_count, 5); ++index) {
                const auto& day = data.daily[index];
                add(format("day %s: min %.0f C, max %.0f C, %s, rain chance %d%%", day.date.c_str(),
                           static_cast<double>(day.minimum_c), static_cast<double>(day.maximum_c),
                           condition_name(day.condition), day.rain_probability_max_percent));
            }
        } else if (weather->has_saved_location) {
            add("weather: no data loaded yet for " + weather->saved_location.name);
        } else {
            add("weather: no city configured on the device");
        }
    }
    if (const auto* fuel = input.fuel; fuel && fuel->state == FuelPriceState::Ready) {
        const auto& data = fuel->data;
        std::string line = format("Greece weekly average fuel prices: unleaded 95 %.3f EUR/l, diesel %.3f EUR/l",
                                  data.unleaded95_millieur_per_litre / 1000.0,
                                  data.diesel_millieur_per_litre / 1000.0);
        if (data.lpg_millieur_per_litre) {
            line += format(", LPG %.3f EUR/l", *data.lpg_millieur_per_litre / 1000.0);
        } else {
            line += ", LPG unavailable from source";
        }
        line += "; period " + data.period_date + "; source: " + data.source;
        if (data.stale) line += " (cached, may be outdated)";
        if (data.demo) line += " DEMO sample data, not live";
        add(line);
    }
    if (const auto* clock = input.clock) {
        const auto& snapshot = clock->snapshot();
        if (!snapshot.timers.empty()) {
            std::string line = "timers:";
            for (const auto& timer : snapshot.timers) {
                line += " " + duration_text((timer.remaining_ms + 999U) / 1000U) + " left";
                if (timer.paused) line += " (paused)";
                if (!timer.label.empty()) line += " \"" + timer.label + "\"";
                line += ";";
            }
            add(line);
        }
        if (!snapshot.alarms.empty()) {
            std::string line = "alarms:";
            for (const auto& alarm : snapshot.alarms) {
                line += format(" %02d:%02d %s %s", alarm.hour, alarm.minute, alarm.daily ? "daily" : "once",
                               alarm.enabled ? "on" : "off");
                if (!alarm.label.empty()) line += " \"" + alarm.label + "\"";
                line += ";";
            }
            add(line);
        }
        if (!snapshot.reminders.empty()) {
            std::string line = "reminders:";
            std::size_t shown = 0;
            for (const auto& reminder : snapshot.reminders) {
                if (++shown > 5) break;
                const auto due = from_minute_index(reminder.due_minute);
                line += format(" %04d-%02d-%02d %02d:%02d", due.year, due.month, due.day, due.hour, due.minute) +
                        " \"" + reminder.text + "\";";
            }
            add(line);
        }
    }
    if (input.unread_messages > 0) add(format("unread Aoraton messages: %d", input.unread_messages));
    if (input.notes) {
        const auto notes = input.notes->context_for(input.question);
        if (!notes.empty()) out += "notes (newest first):\n" + notes;
        else if (question_mentions_notes(input.question)) add("notes: none saved");
    }
    if (input.organizer) {
        const auto organizer = input.organizer->context_for(input.question,
            input.clock ? input.clock->snapshot().now_minute : 0);
        if (!organizer.empty()) out += "organizer:\n" + organizer;
        else if (question_mentions_organizer(input.question)) add("organizer: no matching saved items");
    }
    if (input.phone && question_mentions_phone(input.question)) {
        const auto& phone = input.phone->snapshot();
        const auto items = input.phone->context_for(input.question);
        if (phone.link != PhoneLinkState::Connected) add("phone: not connected");
        if (!items.empty()) out += "phone notifications (newest first):\n" + items;
        else if (phone.link == PhoneLinkState::Connected) add("phone notifications: none");
    }
    return utf8_truncate(out, kContextLimit);
}

ParsedAnswer parse_assistant_answer(const std::string& raw) {
    ParsedAnswer result;
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const auto end = raw.find('\n', start);
        lines.push_back(raw.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    int action_line = -1;
    bool any_action = false;
    for (int index = static_cast<int>(lines.size()) - 1; index >= 0; --index) {
        const auto trimmed = trim(lines[static_cast<std::size_t>(index)]);
        if (trimmed.empty()) continue;
        if (trimmed.rfind("ACTION:", 0) == 0) action_line = index;
        break;
    }
    std::string display;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto trimmed = trim(lines[index]);
        if (trimmed.rfind("ACTION:", 0) == 0) {
            any_action = true;
            continue;
        }
        // A code fence that only wrapped the action line is dropped with it.
        if (action_line >= 0 && lines[index].find("```") != std::string::npos && trimmed.size() <= 8) continue;
        if (!display.empty()) display += '\n';
        display += lines[index];
    }
    result.text = sanitize_text(display, 12000, true);
    if (action_line >= 0) {
        const auto json = trim(trim(lines[static_cast<std::size_t>(action_line)]).substr(7));
        const auto object = parse_flat_json(json, 1024);
        if (object) result.action = validate_action(*object);
        result.action_rejected = !result.action;
    } else {
        result.action_rejected = any_action;
    }
    return result;
}

std::int64_t reminder_due_minute(const AssistantAction& action, std::int64_t now_minute) noexcept {
    if (action.in_minutes > 0) return now_minute + action.in_minutes;
    const std::int64_t day_start = (now_minute >= 0 ? now_minute / 1440 : -((-now_minute + 1439) / 1440)) * 1440;
    std::int64_t due = day_start + action.hour * 60 + action.minute;
    if (action.tomorrow) due += 1440;
    else if (due <= now_minute) due += 1440;
    return due;
}

const char* assistant_action_name(AssistantActionType type) noexcept {
    switch (type) {
        case AssistantActionType::None: return "none";
        case AssistantActionType::Timer: return "timer";
        case AssistantActionType::Alarm: return "alarm";
        case AssistantActionType::Reminder: return "reminder";
        case AssistantActionType::Note: return "note";
        case AssistantActionType::Task: return "task";
        case AssistantActionType::Shopping: return "shopping";
        case AssistantActionType::CalendarEvent: return "calendar_event";
        case AssistantActionType::Volume: return "volume";
        case AssistantActionType::Brightness: return "brightness";
        case AssistantActionType::Mute: return "mute";
        case AssistantActionType::Message: return "message";
    }
    return "none";
}

}  // namespace aura

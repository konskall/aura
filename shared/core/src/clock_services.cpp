#include "aura/clock_services.hpp"

#include <algorithm>
#include <sstream>

namespace aura {
namespace {

constexpr std::int64_t kMinutesPerDay = 24 * 60;
// After a long sleep the device does not ring every alarm it missed; only the
// last hour is caught up. Reminders, which are one-off, always fire late.
constexpr std::int64_t kAlarmCatchUpMinutes = 60;

std::int64_t time_of_day(std::int64_t minute) {
    return ((minute % kMinutesPerDay) + kMinutesPerDay) % kMinutesPerDay;
}

std::uint64_t whole_seconds(std::uint64_t remaining_ms) { return (remaining_ms + 999U) / 1000U; }

bool parse_int(const std::string& text, std::int64_t& value) {
    if (text.empty() || text.size() > 18) return false;
    std::size_t index = 0;
    bool negative = false;
    if (text[0] == '-') { negative = true; index = 1; }
    if (index == text.size()) return false;
    std::int64_t result = 0;
    for (; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
        result = result * 10 + (text[index] - '0');
    }
    value = negative ? -result : result;
    return true;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const auto tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return fields;
}

}  // namespace

ClockResult ClockServicesModel::start_timer(std::uint32_t seconds, const std::string& label, std::uint32_t* id) {
    if (seconds == 0 || seconds > kMaxTimerSeconds) return ClockResult::Invalid;
    if (snapshot_.timers.size() >= kMaxTimers) return ClockResult::Full;
    TimerItem timer;
    timer.id = next_id_++;
    timer.label = sanitize_text(label, kMaxLabelBytes);
    timer.duration_s = seconds;
    timer.remaining_ms = static_cast<std::uint64_t>(seconds) * 1000U;
    snapshot_.timers.insert(snapshot_.timers.begin(), timer);
    if (id) *id = timer.id;
    changed();
    return ClockResult::Ok;
}

ClockResult ClockServicesModel::pause_timer(std::uint32_t id, bool paused) {
    for (auto& timer : snapshot_.timers) {
        if (timer.id != id) continue;
        if (timer.paused != paused) {
            timer.paused = paused;
            changed();
        }
        return ClockResult::Ok;
    }
    return ClockResult::NotFound;
}

ClockResult ClockServicesModel::cancel_timer(std::uint32_t id) {
    auto& timers = snapshot_.timers;
    const auto found = std::find_if(timers.begin(), timers.end(), [id](const TimerItem& item) { return item.id == id; });
    if (found == timers.end()) return ClockResult::NotFound;
    timers.erase(found);
    changed();
    return ClockResult::Ok;
}

ClockResult ClockServicesModel::add_alarm(int hour, int minute, bool daily, const std::string& label,
                                          std::uint32_t* id) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return ClockResult::Invalid;
    if (snapshot_.alarms.size() >= kMaxAlarms) return ClockResult::Full;
    AlarmItem alarm;
    alarm.id = next_id_++;
    alarm.hour = hour;
    alarm.minute = minute;
    alarm.daily = daily;
    alarm.label = sanitize_text(label, kMaxLabelBytes);
    snapshot_.alarms.push_back(alarm);
    if (id) *id = alarm.id;
    sort_lists();
    changed();
    return ClockResult::Ok;
}

ClockResult ClockServicesModel::set_alarm_enabled(std::uint32_t id, bool enabled) {
    for (auto& alarm : snapshot_.alarms) {
        if (alarm.id != id) continue;
        if (alarm.enabled != enabled) {
            alarm.enabled = enabled;
            changed();
        }
        return ClockResult::Ok;
    }
    return ClockResult::NotFound;
}

ClockResult ClockServicesModel::remove_alarm(std::uint32_t id) {
    auto& alarms = snapshot_.alarms;
    const auto found = std::find_if(alarms.begin(), alarms.end(), [id](const AlarmItem& item) { return item.id == id; });
    if (found == alarms.end()) return ClockResult::NotFound;
    alarms.erase(found);
    changed();
    return ClockResult::Ok;
}

ClockResult ClockServicesModel::add_reminder_at(std::int64_t due_minute, const std::string& text, std::uint32_t* id) {
    if (!snapshot_.clock_valid) return ClockResult::ClockUnavailable;
    const auto clean = sanitize_text(text, kMaxReminderBytes);
    if (clean.empty() || due_minute <= snapshot_.now_minute ||
        due_minute > snapshot_.now_minute + 366 * kMinutesPerDay) {
        return ClockResult::Invalid;
    }
    if (snapshot_.reminders.size() >= kMaxReminders) return ClockResult::Full;
    ReminderItem reminder;
    reminder.id = next_id_++;
    reminder.due_minute = due_minute;
    reminder.text = clean;
    snapshot_.reminders.push_back(reminder);
    if (id) *id = reminder.id;
    sort_lists();
    changed();
    return ClockResult::Ok;
}

ClockResult ClockServicesModel::add_reminder_in(int minutes, const std::string& text, std::uint32_t* id) {
    if (!snapshot_.clock_valid) return ClockResult::ClockUnavailable;
    if (minutes < 1 || minutes > 30 * kMinutesPerDay) return ClockResult::Invalid;
    return add_reminder_at(snapshot_.now_minute + minutes, text, id);
}

ClockResult ClockServicesModel::remove_reminder(std::uint32_t id) {
    auto& reminders = snapshot_.reminders;
    const auto found = std::find_if(reminders.begin(), reminders.end(),
                                    [id](const ReminderItem& item) { return item.id == id; });
    if (found == reminders.end()) return ClockResult::NotFound;
    reminders.erase(found);
    changed();
    return ClockResult::Ok;
}

void ClockServicesModel::tick(std::uint32_t elapsed_ms) {
    if (elapsed_ms == 0) return;
    bool visible_change = false;
    auto& timers = snapshot_.timers;
    for (auto timer = timers.begin(); timer != timers.end();) {
        if (timer->paused) {
            ++timer;
            continue;
        }
        if (timer->remaining_ms <= elapsed_ms) {
            ClockAlert alert;
            alert.kind = ClockAlertKind::Timer;
            alert.id = timer->id;
            alert.label = timer->label;
            alert.duration_s = timer->duration_s;
            timer = timers.erase(timer);
            queue_alert(std::move(alert));
            visible_change = true;
            continue;
        }
        const auto before = whole_seconds(timer->remaining_ms);
        timer->remaining_ms -= elapsed_ms;
        if (whole_seconds(timer->remaining_ms) != before) visible_change = true;
        ++timer;
    }
    if (visible_change) changed();
}

void ClockServicesModel::set_clock(const LocalDateTime& now) {
    if (!local_date_time_valid(now)) {
        set_clock_unavailable();
        return;
    }
    const auto minute = minute_index(now);
    const bool became_valid = !snapshot_.clock_valid;
    snapshot_.clock_valid = true;
    if (evaluated_minute_ < 0) {
        snapshot_.now_minute = minute;
        evaluate_minute(minute - 1, minute);
        evaluated_minute_ = minute;
        changed();
        return;
    }
    if (minute != evaluated_minute_) {
        snapshot_.now_minute = minute;
        evaluate_minute(evaluated_minute_, minute);
        evaluated_minute_ = minute;
        changed();
    } else if (became_valid) {
        changed();
    }
}

void ClockServicesModel::set_clock_unavailable() {
    if (!snapshot_.clock_valid) return;
    snapshot_.clock_valid = false;
    evaluated_minute_ = -1;
    changed();
}

void ClockServicesModel::evaluate_minute(std::int64_t previous, std::int64_t now) {
    // A clock that moved backwards (time zone or NTP correction) rings nothing.
    if (now <= previous) return;
    const std::int64_t first = std::max(previous + 1, now - kAlarmCatchUpMinutes + 1);
    for (auto& alarm : snapshot_.alarms) {
        if (!alarm.enabled) continue;
        const std::int64_t target = alarm.hour * 60 + alarm.minute;
        for (std::int64_t minute = first; minute <= now; ++minute) {
            if (time_of_day(minute) != target) continue;
            ClockAlert alert;
            alert.kind = ClockAlertKind::Alarm;
            alert.id = alarm.id;
            alert.label = alarm.label;
            alert.hour = alarm.hour;
            alert.minute = alarm.minute;
            queue_alert(std::move(alert));
            if (!alarm.daily) alarm.enabled = false;
            break;
        }
    }
    auto& reminders = snapshot_.reminders;
    for (auto reminder = reminders.begin(); reminder != reminders.end();) {
        if (reminder->due_minute > now) {
            ++reminder;
            continue;
        }
        const auto due = from_minute_index(reminder->due_minute);
        ClockAlert alert;
        alert.kind = ClockAlertKind::Reminder;
        alert.id = reminder->id;
        alert.label = reminder->text;
        alert.hour = due.hour;
        alert.minute = due.minute;
        reminder = reminders.erase(reminder);
        queue_alert(std::move(alert));
    }
    for (auto snooze = snoozes_.begin(); snooze != snoozes_.end();) {
        if (snooze->due_minute > now) {
            ++snooze;
            continue;
        }
        ClockAlert alert = snooze->alert;
        snooze = snoozes_.erase(snooze);
        queue_alert(std::move(alert));
    }
}

void ClockServicesModel::queue_alert(ClockAlert alert) {
    // One alert per source: a snoozed alarm that rings again replaces itself.
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&alert](const ClockAlert& item) {
        return item.kind == alert.kind && item.id == alert.id;
    }), queue_.end());
    queue_.push_back(std::move(alert));
    snapshot_.alert = queue_.front();
    snapshot_.queued_alerts = queue_.size();
    changed();
}

void ClockServicesModel::dismiss_alert() {
    if (queue_.empty()) return;
    queue_.erase(queue_.begin());
    if (queue_.empty()) snapshot_.alert.reset();
    else snapshot_.alert = queue_.front();
    snapshot_.queued_alerts = queue_.size();
    changed();
}

ClockResult ClockServicesModel::snooze_alert(int minutes) {
    if (queue_.empty()) return ClockResult::NotFound;
    if (minutes < 1 || minutes > 60) return ClockResult::Invalid;
    const ClockAlert alert = queue_.front();
    if (alert.kind == ClockAlertKind::Timer) {
        const auto result = start_timer(static_cast<std::uint32_t>(minutes) * 60U, alert.label);
        if (result == ClockResult::Ok) dismiss_alert();
        return result;
    }
    if (!snapshot_.clock_valid) return ClockResult::ClockUnavailable;
    // Alarms and reminders ring again, unchanged, after the snooze.
    snoozes_.push_back({alert, snapshot_.now_minute + minutes});
    dismiss_alert();
    return ClockResult::Ok;
}

std::optional<AlarmItem> ClockServicesModel::next_alarm() const { return aura::next_alarm(snapshot_); }

int ClockServicesModel::minutes_until(const AlarmItem& alarm) const { return aura::minutes_until(snapshot_, alarm); }

std::optional<AlarmItem> next_alarm(const ClockSnapshot& snapshot) {
    if (!snapshot.clock_valid) return std::nullopt;
    std::optional<AlarmItem> best;
    int best_minutes = 0;
    for (const auto& alarm : snapshot.alarms) {
        if (!alarm.enabled) continue;
        const int minutes = minutes_until(snapshot, alarm);
        if (!best || minutes < best_minutes) {
            best = alarm;
            best_minutes = minutes;
        }
    }
    return best;
}

int minutes_until(const ClockSnapshot& snapshot, const AlarmItem& alarm) {
    const auto now = time_of_day(snapshot.now_minute);
    const std::int64_t target = alarm.hour * 60 + alarm.minute;
    return static_cast<int>(((target - now) % kMinutesPerDay + kMinutesPerDay) % kMinutesPerDay);
}

void ClockServicesModel::sort_lists() {
    std::sort(snapshot_.alarms.begin(), snapshot_.alarms.end(), [](const AlarmItem& left, const AlarmItem& right) {
        if (left.hour != right.hour) return left.hour < right.hour;
        if (left.minute != right.minute) return left.minute < right.minute;
        return left.id < right.id;
    });
    std::sort(snapshot_.reminders.begin(), snapshot_.reminders.end(),
              [](const ReminderItem& left, const ReminderItem& right) {
                  if (left.due_minute != right.due_minute) return left.due_minute < right.due_minute;
                  return left.id < right.id;
              });
}

std::string ClockServicesModel::serialize() const {
    std::ostringstream output;
    output << "aura-clock 1\n";
    output << "N\t" << next_id_ << '\n';
    for (const auto& alarm : snapshot_.alarms) {
        output << "A\t" << alarm.id << '\t' << alarm.hour << '\t' << alarm.minute << '\t'
               << (alarm.daily ? 1 : 0) << '\t' << (alarm.enabled ? 1 : 0) << '\t' << alarm.label << '\n';
    }
    for (const auto& reminder : snapshot_.reminders) {
        output << "R\t" << reminder.id << '\t' << reminder.due_minute << '\t' << reminder.text << '\n';
    }
    // An alert nobody answered and a snoozed one ring again after a restart:
    // a reminder has already left its list when it rings.
    const auto alert_fields = [&output](const ClockAlert& alert) {
        output << static_cast<int>(alert.kind) << '\t' << alert.id << '\t' << alert.hour << '\t' << alert.minute << '\t'
               << alert.duration_s << '\t' << alert.label;
    };
    for (const auto& alert : queue_) {
        output << "Q\t";
        alert_fields(alert);
        output << '\n';
    }
    for (const auto& snooze : snoozes_) {
        output << "Z\t" << snooze.due_minute << '\t';
        alert_fields(snooze.alert);
        output << '\n';
    }
    return output.str();
}

bool ClockServicesModel::restore(const std::string& data) {
    std::istringstream input(data);
    std::string line;
    if (!std::getline(input, line) || line != "aura-clock 1") return false;
    std::vector<AlarmItem> alarms;
    std::vector<ReminderItem> reminders;
    std::vector<ClockAlert> queue;
    std::vector<Snooze> snoozes;
    std::uint32_t next_id = 1;
    // kind, id, hour, minute, duration and label from `fields[first]` on.
    const auto parse_alert = [](const std::vector<std::string>& fields, std::size_t first, ClockAlert& alert) {
        std::int64_t values[5]{};
        for (std::size_t index = 0; index < 5; ++index) {
            if (!parse_int(fields[first + index], values[index])) return false;
        }
        if (values[0] < 0 || values[0] > 2 || values[1] <= 0 || values[1] >= 0x7FFFFFFF || values[2] < 0 ||
            values[2] > 23 || values[3] < 0 || values[3] > 59 || values[4] < 0 || values[4] > kMaxTimerSeconds) {
            return false;
        }
        alert.kind = static_cast<ClockAlertKind>(values[0]);
        alert.id = static_cast<std::uint32_t>(values[1]);
        alert.hour = static_cast<int>(values[2]);
        alert.minute = static_cast<int>(values[3]);
        alert.duration_s = static_cast<std::uint32_t>(values[4]);
        alert.label = sanitize_text(fields[first + 5], kMaxReminderBytes);
        return true;
    };
    while (std::getline(input, line)) {
        const auto fields = split_tabs(line);
        std::int64_t values[5]{};
        ClockAlert alert;
        if (fields.size() == 7 && fields[0] == "Q" && queue.size() < kMaxReminders && parse_alert(fields, 1, alert)) {
            queue.push_back(std::move(alert));
        } else if (fields.size() == 8 && fields[0] == "Z" && snoozes.size() < kMaxReminders &&
                   parse_int(fields[1], values[0]) && parse_alert(fields, 2, alert)) {
            snoozes.push_back({std::move(alert), values[0]});
        } else if (fields.size() == 2 && fields[0] == "N" && parse_int(fields[1], values[0]) && values[0] > 0 &&
            values[0] < 0x7FFFFFFF) {
            next_id = std::max(next_id, static_cast<std::uint32_t>(values[0]));
        } else if (fields.size() == 7 && fields[0] == "A" && alarms.size() < kMaxAlarms) {
            bool ok = true;
            for (int index = 0; index < 5; ++index) ok = ok && parse_int(fields[static_cast<std::size_t>(index) + 1], values[index]);
            if (!ok || values[0] <= 0 || values[0] >= 0x7FFFFFFF || values[1] < 0 || values[1] > 23 ||
                values[2] < 0 || values[2] > 59) {
                continue;
            }
            AlarmItem alarm;
            alarm.id = static_cast<std::uint32_t>(values[0]);
            alarm.hour = static_cast<int>(values[1]);
            alarm.minute = static_cast<int>(values[2]);
            alarm.daily = values[3] != 0;
            alarm.enabled = values[4] != 0;
            alarm.label = sanitize_text(fields[6], kMaxLabelBytes);
            alarms.push_back(alarm);
            next_id = std::max(next_id, alarm.id + 1);
        } else if (fields.size() == 4 && fields[0] == "R" && reminders.size() < kMaxReminders) {
            if (!parse_int(fields[1], values[0]) || !parse_int(fields[2], values[1]) || values[0] <= 0 ||
                values[0] >= 0x7FFFFFFF) {
                continue;
            }
            ReminderItem reminder;
            reminder.id = static_cast<std::uint32_t>(values[0]);
            reminder.due_minute = values[1];
            reminder.text = sanitize_text(fields[3], kMaxReminderBytes);
            if (reminder.text.empty()) continue;
            reminders.push_back(reminder);
            next_id = std::max(next_id, reminder.id + 1);
        }
    }
    snapshot_.alarms = std::move(alarms);
    snapshot_.reminders = std::move(reminders);
    queue_ = std::move(queue);
    snoozes_ = std::move(snoozes);
    if (queue_.empty()) snapshot_.alert.reset();
    else snapshot_.alert = queue_.front();
    snapshot_.queued_alerts = queue_.size();
    next_id_ = std::max(next_id_, next_id);
    sort_lists();
    changed();
    return true;
}

const char* clock_result_name(ClockResult result) noexcept {
    switch (result) {
        case ClockResult::Ok: return "OK";
        case ClockResult::Full: return "FULL";
        case ClockResult::Invalid: return "INVALID";
        case ClockResult::ClockUnavailable: return "CLOCK_UNAVAILABLE";
        case ClockResult::NotFound: return "NOT_FOUND";
    }
    return "UNKNOWN";
}

}  // namespace aura

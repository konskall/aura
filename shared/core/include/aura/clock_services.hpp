#pragma once

#include "aura/text_util.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aura {

enum class ClockAlertKind { Timer, Alarm, Reminder };

struct TimerItem {
    std::uint32_t id{0};
    std::string label;
    std::uint32_t duration_s{0};
    std::uint64_t remaining_ms{0};
    bool paused{false};
};

struct AlarmItem {
    std::uint32_t id{0};
    int hour{0};
    int minute{0};
    bool daily{false};
    bool enabled{true};
    std::string label;
};

struct ReminderItem {
    std::uint32_t id{0};
    std::int64_t due_minute{0};  // minute_index() in local time
    std::string text;
};

struct ClockAlert {
    ClockAlertKind kind{ClockAlertKind::Timer};
    std::uint32_t id{0};
    std::string label;  // timer/alarm label or reminder text (may be empty)
    int hour{0};        // alarm time / reminder due time
    int minute{0};
    std::uint32_t duration_s{0};  // timer length
};

struct ClockSnapshot {
    std::vector<TimerItem> timers;        // newest first
    std::vector<AlarmItem> alarms;        // by time of day
    std::vector<ReminderItem> reminders;  // pending, soonest first
    std::optional<ClockAlert> alert;      // ringing now (the front of the queue)
    std::size_t queued_alerts{0};
    bool clock_valid{false};
    std::int64_t now_minute{0};
    std::uint64_t sequence{0};
};

enum class ClockResult { Ok, Full, Invalid, ClockUnavailable, NotFound };

// Timers, alarms and reminders. Timers count monotonic time (tick); alarms and
// reminders follow the wall clock (set_clock) and need a valid clock. Labels
// and texts are user content: kept locally, never logged.
class ClockServicesModel {
public:
    static constexpr std::size_t kMaxTimers = 4;
    static constexpr std::size_t kMaxAlarms = 8;
    static constexpr std::size_t kMaxReminders = 16;
    static constexpr std::uint32_t kMaxTimerSeconds = 24U * 60U * 60U;
    static constexpr std::size_t kMaxLabelBytes = 48;
    static constexpr std::size_t kMaxReminderBytes = 160;

    ClockResult start_timer(std::uint32_t seconds, const std::string& label, std::uint32_t* id = nullptr);
    ClockResult pause_timer(std::uint32_t id, bool paused);
    ClockResult cancel_timer(std::uint32_t id);

    ClockResult add_alarm(int hour, int minute, bool daily, const std::string& label, std::uint32_t* id = nullptr);
    ClockResult set_alarm_enabled(std::uint32_t id, bool enabled);
    ClockResult remove_alarm(std::uint32_t id);

    ClockResult add_reminder_at(std::int64_t due_minute, const std::string& text, std::uint32_t* id = nullptr);
    ClockResult add_reminder_in(int minutes, const std::string& text, std::uint32_t* id = nullptr);
    ClockResult remove_reminder(std::uint32_t id);

    void tick(std::uint32_t elapsed_ms);
    // Call at least once per minute (the platforms call it every second).
    void set_clock(const LocalDateTime& now);
    void set_clock_unavailable();

    // The ringing alert: dismiss it, or snooze (alarm/reminder: again in
    // `minutes`; timer: restart for `minutes`).
    void dismiss_alert();
    ClockResult snooze_alert(int minutes);

    // The next enabled alarm from now, if the clock is valid.
    std::optional<AlarmItem> next_alarm() const;
    // Minutes from now until `alarm` next rings (0..1439).
    int minutes_until(const AlarmItem& alarm) const;

    // Alarms and reminders survive restarts; timers do not.
    std::string serialize() const;
    bool restore(const std::string& data);

    const ClockSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    void changed() noexcept { ++snapshot_.sequence; }
    void queue_alert(ClockAlert alert);
    void evaluate_minute(std::int64_t previous, std::int64_t now);
    void sort_lists();

    struct Snooze {
        ClockAlert alert;
        std::int64_t due_minute{0};
    };

    ClockSnapshot snapshot_{};
    std::vector<ClockAlert> queue_;
    std::vector<Snooze> snoozes_;
    std::uint32_t next_id_{1};
    std::int64_t evaluated_minute_{-1};
};

// The next enabled alarm from the snapshot's clock, if the clock is valid.
std::optional<AlarmItem> next_alarm(const ClockSnapshot& snapshot);
// Minutes from the snapshot's clock until `alarm` next rings (0..1439).
int minutes_until(const ClockSnapshot& snapshot, const AlarmItem& alarm);
const char* clock_result_name(ClockResult result) noexcept;

}  // namespace aura

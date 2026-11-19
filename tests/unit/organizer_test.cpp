#include "aura/organizer.hpp"
#include "aura/text_util.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "organizer test failed: " << message << '\n';
    std::exit(1);
}

}  // namespace

int main() {
    aura::OrganizerModel model;
    std::uint32_t event = 0;
    std::uint32_t task = 0;
    std::uint32_t shopping = 0;
    expect(model.add_event("Οδοντίατρος", 30000000, 45, &event) == aura::OrganizerResult::Ok,
           "add event");
    expect(model.add_task("Πλήρωσε ρεύμα", 29999000, 0, &task) == aura::OrganizerResult::Ok,
           "add task");
    expect(model.add_shopping("Γάλα", &shopping) == aura::OrganizerResult::Ok, "add shopping");
    expect(model.toggle(aura::OrganizerKind::Task, task) == aura::OrganizerResult::Ok &&
           model.snapshot().tasks.front().completed, "complete task");
    expect(model.toggle(aura::OrganizerKind::Shopping, shopping) == aura::OrganizerResult::Ok &&
           model.snapshot().shopping.front().checked, "check shopping");
    expect(model.events_between(29999999, 30000100).size() == 1, "agenda range");

    const auto data = model.serialize();
    aura::OrganizerModel restored;
    expect(restored.restore(data), "restore store");
    expect(restored.snapshot().events.size() == 1 && restored.snapshot().tasks.size() == 1 &&
           restored.snapshot().shopping.size() == 1, "round trip counts");
    expect(restored.snapshot().events.front().title == "Οδοντίατρος", "UTF-8 round trip");
    expect(restored.context_for("Τι έχω στο ημερολόγιο;", 29999000).find("Οδοντίατρος") != std::string::npos,
           "calendar context");
    expect(restored.context_for("Τι έχει η λίστα αγορών;", 29999000).find("Γάλα") == std::string::npos,
           "checked shopping excluded");
    expect(restored.remove(aura::OrganizerKind::Event, event) == aura::OrganizerResult::Ok,
           "remove event");
    expect(!restored.restore("bad\n"), "reject unknown store");

    // Events that have ended are pruned, so the bounded agenda never fills
    // up with the past.
    aura::OrganizerModel agenda;
    for (std::size_t index = 0; index < aura::OrganizerModel::kMaxEvents; ++index) {
        expect(agenda.add_event("Παλιό", 1000 + static_cast<std::int64_t>(index) * 60, 30) == aura::OrganizerResult::Ok,
               "fill the agenda");
    }
    expect(agenda.add_event("Νέο", 900000, 30) == aura::OrganizerResult::Full, "a full agenda refuses");
    const auto before_prune = agenda.snapshot().sequence;
    expect(agenda.prune_events_before(1000 + 10 * 60) == 10 && agenda.snapshot().events.size() == 54 &&
               agenda.snapshot().sequence > before_prune,
           "only events that have ended are pruned");
    expect(agenda.prune_events_before(1000 + 10 * 60) == 0, "pruning again changes nothing");
    expect(agenda.add_event("Νέο", 900000, 30) == aura::OrganizerResult::Ok, "room after pruning");

    // A restore is a change like any other: the sequence never goes back.
    const auto before_restore = agenda.snapshot().sequence;
    expect(agenda.restore(data) && agenda.snapshot().sequence > before_restore, "restore keeps counting");

    // The interface packs an id into 30 bits; stored ids stay below that.
    aura::OrganizerModel large;
    expect(large.restore("aura-organizer 1\nN\t1073741830\nS\t1073741825\t0\tΨωμί\n") &&
               large.snapshot().shopping.size() == 1 && large.snapshot().shopping.front().id < (1U << 30),
           "a huge stored id is renumbered");
    std::uint32_t next = 0;
    expect(large.add_shopping("Τυρί", &next) == aura::OrganizerResult::Ok && next < (1U << 30) &&
               next != large.snapshot().shopping.back().id,
           "new ids stay small and unique");

    // A new event suggests the next full half hour at least 30 minutes away;
    // late in the evening that is early tomorrow, not tomorrow evening.
    const std::int64_t day = 20000LL * 1440;
    expect(aura::organizer_default_start(day + 14 * 60 + 10) == day + 15 * 60, "14:10 suggests 15:00");
    expect(aura::organizer_default_start(day + 14 * 60 + 40) == day + 15 * 60 + 30, "14:40 suggests 15:30");
    expect(aura::organizer_default_start(day + 23 * 60 + 10) == day + 1440, "23:10 suggests midnight");
    expect(aura::organizer_default_start(day + 23 * 60 + 50) == day + 1440 + 30, "23:50 suggests 00:30");

    // The calendar's month: 1 November 2026 is a Sunday, the last column.
    const auto november = aura::calendar_month(2026, 11);
    expect(november.year == 2026 && november.month == 11 && november.days == 30 && november.first_column == 6,
           "November 2026 starts on Sunday");
    const auto leap = aura::calendar_month(2028, 2);
    expect(leap.days == 29 && leap.first_column == 1, "February 2028 has 29 days and starts on Tuesday");
    expect(aura::calendar_month(2027, 2).days == 28, "February 2027 has 28 days");
    const auto next_year = aura::calendar_month(2026, 13);
    expect(next_year.year == 2027 && next_year.month == 1 && next_year.days == 31 && next_year.first_column == 4,
           "the month after December is next January, a Friday");
    const auto last_year = aura::calendar_month(2027, 0);
    expect(last_year.year == 2026 && last_year.month == 12 && last_year.days == 31,
           "the month before January is last December");

    aura::OrganizerModel calendar;
    const auto at = [](int y, int m, int d, int hour, int minute) {
        return aura::days_from_civil(y, m, d) * 1440 + hour * 60 + minute;
    };
    expect(calendar.add_event("Οδοντίατρος", at(2026, 11, 18, 10, 0), 60) == aura::OrganizerResult::Ok, "an appointment");
    expect(calendar.add_event("Ταξίδι", at(2026, 11, 30, 23, 0), 180) == aura::OrganizerResult::Ok, "past midnight");
    expect(calendar.add_event("Μεσάνυχτα", at(2026, 11, 20, 23, 0), 60) == aura::OrganizerResult::Ok, "up to midnight");
    expect(aura::organizer_event_days(calendar.snapshot(), 2026, 11) ==
               ((1U << 18U) | (1U << 20U) | (1U << 30U)),
           "the month's days that hold something");
    expect(aura::organizer_event_days(calendar.snapshot(), 2026, 12) == (1U << 1U),
           "an appointment past midnight reaches the next month");
    expect(aura::organizer_event_days(calendar.snapshot(), 2026, 10) == 0U, "an empty month");
    const auto on_18 = aura::organizer_events_on(calendar.snapshot(), 2026, 11, 18);
    expect(on_18.size() == 1 && on_18.front().title == "Οδοντίατρος", "the day's appointments");
    expect(aura::organizer_events_on(calendar.snapshot(), 2026, 12, 1).size() == 1, "the second day of a long one");
    expect(aura::organizer_events_on(calendar.snapshot(), 2026, 11, 21).empty(), "ending at midnight is not the next day");
    std::cout << "organizer tests passed\n";
}

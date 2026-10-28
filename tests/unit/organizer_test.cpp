#include "aura/organizer.hpp"

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
    std::cout << "organizer tests passed\n";
}

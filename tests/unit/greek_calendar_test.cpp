#include "aura/greek_calendar.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool has(const aura::GreekCalendarDay& day, const char* name) {
    return std::find(day.namedays.begin(), day.namedays.end(), name) != day.namedays.end();
}
}  // namespace

int main() {
    using namespace aura;

    const auto easter_2024 = orthodox_easter_date(2024);
    const auto easter_2025 = orthodox_easter_date(2025);
    const auto easter_2026 = orthodox_easter_date(2026);
    const auto easter_2027 = orthodox_easter_date(2027);
    expect(easter_2024.month == 5 && easter_2024.day == 5, "2024 Orthodox Easter");
    expect(easter_2025.month == 4 && easter_2025.day == 20, "2025 Orthodox Easter");
    expect(easter_2026.month == 4 && easter_2026.day == 12, "2026 Orthodox Easter");
    expect(easter_2027.month == 5 && easter_2027.day == 2, "2027 Orthodox Easter");

    expect(greek_calendar_day(2026, 2, 23).holiday == GreekHolidayKind::CleanMonday,
           "Clean Monday is Easter minus 48 days");
    expect(greek_calendar_day(2026, 4, 10).holiday == GreekHolidayKind::GoodFriday,
           "Good Friday is Easter minus two days");
    expect(greek_calendar_day(2026, 6, 1).holiday == GreekHolidayKind::HolySpiritMonday,
           "Holy Spirit Monday is Easter plus 50 days");

    expect(has(greek_calendar_day(2026, 4, 4), "Λάζαρος"), "Lazarus Saturday nameday");
    expect(has(greek_calendar_day(2026, 4, 5), "Βάιος"), "Palm Sunday nameday");
    expect(has(greek_calendar_day(2026, 4, 12), "Αναστασία"), "Easter nameday");
    expect(has(greek_calendar_day(2026, 4, 17), "Ζωή"), "Bright Friday nameday");
    expect(has(greek_calendar_day(2026, 4, 19), "Θωμάς"), "Thomas Sunday nameday");

    // In 2024 Easter followed 23/25 April, so George and Mark are transferred
    // to Monday and Tuesday of Bright Week rather than shown on their fixed day.
    expect(!has(greek_calendar_day(2024, 4, 23), "Γεώργιος"), "George fixed date is suppressed before Easter");
    expect(has(greek_calendar_day(2024, 5, 6), "Γεώργιος"), "George moves to Easter Monday");
    expect(!has(greek_calendar_day(2024, 4, 25), "Μάρκος"), "Mark fixed date is suppressed before Easter");
    expect(has(greek_calendar_day(2024, 5, 7), "Μάρκος"), "Mark moves to Easter Tuesday");
    expect(has(greek_calendar_day(2026, 4, 23), "Γεώργιος"), "George remains fixed after Easter");

    const auto next = next_greek_holiday(2026, 10, 24, false);
    expect(next && next->kind == GreekHolidayKind::OhiDay && next->days_until == 4,
           "next holiday is calculated locally");
    expect(analyze_greek_calendar_intent("Ποιος γιορτάζει σήμερα;") == GreekCalendarIntent::Namedays,
           "Greek nameday question is recognised");
    expect(analyze_greek_calendar_intent("Πότε είναι η επόμενη αργία;") == GreekCalendarIntent::NextHoliday,
           "Greek holiday question is recognised");
    expect(analyze_greek_calendar_intent("Who celebrates today?") == GreekCalendarIntent::Namedays,
           "English nameday question is recognised");
    expect(analyze_greek_calendar_intent("When is the next public holiday?") == GreekCalendarIntent::NextHoliday,
           "English holiday question is recognised");

    // "When does X celebrate?" names a person; it is not today's list.
    expect(analyze_greek_calendar_intent("Πότε γιορτάζει ο Γιώργος;") == GreekCalendarIntent::NamedayOf,
           "a when-question asks for a person's name day");
    const auto george = greek_nameday_of("Πότε γιορτάζει ο Γιώργος;", 2026);
    expect(george && george->name == "Γιώργος" && george->dates.size() == 1 && george->dates[0].month == 4 &&
               george->dates[0].day == 23,
           "George in 2026 is on 23 April");
    const auto george_2024 = greek_nameday_of("Πότε γιορτάζει ο Γιώργος;", 2024);
    expect(george_2024 && george_2024->dates.size() == 1 && george_2024->dates[0].month == 5 &&
               george_2024->dates[0].day == 6,
           "George follows Easter Monday when Easter is late");
    const auto maria = greek_nameday_of("Πότε είναι η γιορτή της Μαρίας;", 2026);
    expect(maria && maria->name == "Μαρία" && maria->dates.size() == 2 && maria->dates[0].month == 8 &&
               maria->dates[1].month == 11,
           "an inflected name finds both of its days");
    const auto kostas = greek_nameday_of("Πότε γιορτάζει ο Κώστας;", 2026);
    expect(kostas && kostas->dates.size() == 1 && kostas->dates[0].month == 5 && kostas->dates[0].day == 21,
           "a common short form is listed");
    expect(!greek_nameday_of("Ποιος γιορτάζει σήμερα;", 2026), "today's question names nobody");
    expect(!greek_nameday_of("Πότε γιορτάζει ο Ξενοφών;", 2026), "an unlisted name is left to the assistant");
    expect(has(greek_calendar_day(2026, 12, 22), "Αναστασία"), "Anastasia on 22 December");
    expect(has(greek_calendar_day(2026, 1, 18), "Θανάσης") && has(greek_calendar_day(2026, 3, 25), "Βαγγέλης"),
           "short forms are listed with the full names");

    std::cout << "Greek calendar tests passed\n";
    return 0;
}

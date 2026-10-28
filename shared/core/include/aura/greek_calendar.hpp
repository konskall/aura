#pragma once

#include <optional>
#include <string>
#include <vector>

namespace aura {

struct GreekDate {
    int year{0};
    int month{0};
    int day{0};
};

enum class GreekHolidayKind {
    None,
    NewYear,
    Epiphany,
    CleanMonday,
    IndependenceDay,
    GoodFriday,
    EasterSunday,
    EasterMonday,
    LabourDay,
    HolySpiritMonday,
    Dormition,
    OhiDay,
    Christmas,
    BoxingDay,
};

struct GreekCalendarDay {
    GreekDate date{};
    GreekHolidayKind holiday{GreekHolidayKind::None};
    std::vector<std::string> namedays;
};

struct GreekHolidayOccurrence {
    GreekDate date{};
    GreekHolidayKind kind{GreekHolidayKind::None};
    int days_until{0};
};

// Namedays: who celebrates today. NamedayOf: when a named person celebrates.
enum class GreekCalendarIntent { None, Namedays, NamedayOf, NextHoliday };

struct NamedayMatch {
    std::string name;              // the listed spelling, e.g. "Γιώργος"
    std::vector<GreekDate> dates;  // its days in the requested year, in order
};

GreekDate orthodox_easter_date(int year) noexcept;
GreekCalendarDay greek_calendar_day(int year, int month, int day);
std::optional<GreekHolidayOccurrence> next_greek_holiday(int year, int month, int day,
                                                         bool include_today = true);
std::string join_namedays(const std::vector<std::string>& names);
GreekCalendarIntent analyze_greek_calendar_intent(const std::string& text);
// "Πότε γιορτάζει ο Γιώργος;": the listed name asked about (inflected forms
// such as "της Μαρίας" match) and its days in `year`. Nullopt when the text
// asks no such question or names nobody in the offline list, so the
// assistant can answer instead.
std::optional<NamedayMatch> greek_nameday_of(const std::string& text, int year);

const char* greek_holiday_name_el(GreekHolidayKind kind) noexcept;
const char* greek_holiday_name_en(GreekHolidayKind kind) noexcept;

}  // namespace aura

#include "aura/greek_calendar.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace aura {
namespace {

struct NamedayEntry {
    int month;
    int day;
    const char* names;
};

// A deliberately compact offline set of common Greek given names. It is not
// presented as a complete ecclesiastical calendar; movable feasts are added
// below from the Orthodox Easter date.
constexpr NamedayEntry kFixedNamedays[] = {
    {1, 1, "Βασίλης, Βασίλειος, Βασιλική"},
    {1, 6, "Φώτης, Φωτεινή, Θεοφάνης, Θεοφανία"},
    {1, 7, "Γιάννης, Ιωάννης, Ιωάννα"},
    {1, 17, "Αντώνης, Αντωνία"},
    {1, 18, "Αθανάσιος, Αθανασία, Θανάσης, Νάσος"},
    {1, 20, "Ευθύμιος, Ευθυμία"},
    {1, 25, "Γρηγόρης, Γρηγορία"},
    {2, 10, "Χαράλαμπος, Χαρίκλεια, Χάρης, Μπάμπης"},
    {2, 17, "Θεόδωρος, Θεοδώρα, Θοδωρής"},
    {3, 1, "Ευδοκία"},
    {3, 25, "Ευάγγελος, Ευαγγελία, Βαγγέλης, Βαγγελιώ"},
    {4, 23, "Γεώργιος, Γεωργία, Γιώργος"},
    {4, 25, "Μάρκος, Μαρκία"},
    {5, 5, "Ειρήνη"},
    {5, 21, "Κωνσταντίνος, Κωνσταντίνα, Κώστας, Ντίνος, Ελένη"},
    {6, 29, "Πέτρος, Παύλος, Πετρούλα, Παυλίνα"},
    {6, 30, "Απόστολος"},
    {7, 7, "Κυριακή"},
    {7, 17, "Μαρίνα"},
    {7, 20, "Ηλίας"},
    {7, 22, "Μαγδαληνή"},
    {7, 24, "Χριστίνα"},
    {7, 25, "Άννα"},
    {7, 26, "Παρασκευή"},
    {7, 27, "Παντελής, Παντελεήμων"},
    {8, 6, "Σωτήρης, Σωτηρία"},
    {8, 15, "Μαρία, Παναγιώτης, Παναγιώτα, Πάνος, Γιώτα, Δέσποινα"},
    {8, 27, "Φανούριος"},
    {8, 30, "Αλέξανδρος, Αλεξάνδρα"},
    {9, 14, "Σταύρος, Σταυρούλα"},
    {9, 17, "Σοφία, Πίστη, Ελπίδα, Αγάπη"},
    {9, 20, "Ευστάθιος, Ευσταθία"},
    {10, 3, "Διονύσιος, Διονυσία"},
    {10, 18, "Λουκάς"},
    {10, 20, "Γεράσιμος"},
    {10, 26, "Δημήτρης, Δημήτριος, Δήμητρα, Μήτσος"},
    {11, 1, "Κοσμάς, Δαμιανός"},
    {11, 8, "Μιχάλης, Μιχαήλ, Άγγελος, Αγγελική, Γαβριήλ"},
    {11, 9, "Νεκτάριος, Νεκταρία"},
    {11, 11, "Μηνάς, Βίκτωρ, Βικτωρία"},
    {11, 13, "Χρυσόστομος"},
    {11, 14, "Φίλιππος"},
    {11, 16, "Ματθαίος"},
    {11, 21, "Μαρία, Δέσποινα"},
    {11, 25, "Αικατερίνη, Κατερίνα"},
    {11, 26, "Στυλιανός, Στέλιος, Στέλλα"},
    {11, 30, "Ανδρέας"},
    {12, 4, "Βαρβάρα"},
    {12, 5, "Σάββας"},
    {12, 6, "Νικόλαος, Νίκος, Νίκη"},
    {12, 9, "Άννα"},
    {12, 12, "Σπύρος, Σπυρίδων"},
    {12, 13, "Λουκία"},
    {12, 15, "Ελευθέριος, Ελευθερία, Λευτέρης"},
    {12, 17, "Δανιήλ"},
    {12, 20, "Ιγνάτιος"},
    {12, 21, "Ιουλία, Θεμιστοκλής"},
    {12, 22, "Αναστασία"},
    {12, 24, "Ευγενία"},
    {12, 25, "Χρήστος, Χριστίνα, Εμμανουήλ, Μανώλης, Μάνος"},
    {12, 27, "Στέφανος, Στεφανία"},
};

bool before(const GreekDate& left, const GreekDate& right) noexcept {
    return days_from_civil(left.year, left.month, left.day) < days_from_civil(right.year, right.month, right.day);
}

void append_names(std::vector<std::string>& output, std::string_view names) {
    std::size_t start = 0;
    while (start < names.size()) {
        const auto comma = names.find(',', start);
        const auto end = comma == std::string_view::npos ? names.size() : comma;
        auto item = names.substr(start, end - start);
        while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
        if (!item.empty()) output.emplace_back(item);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
}

GreekHolidayKind holiday_for_date(int year, int month, int day) noexcept {
    if (month == 1 && day == 1) return GreekHolidayKind::NewYear;
    if (month == 1 && day == 6) return GreekHolidayKind::Epiphany;
    if (month == 3 && day == 25) return GreekHolidayKind::IndependenceDay;
    if (month == 5 && day == 1) return GreekHolidayKind::LabourDay;
    if (month == 8 && day == 15) return GreekHolidayKind::Dormition;
    if (month == 10 && day == 28) return GreekHolidayKind::OhiDay;
    if (month == 12 && day == 25) return GreekHolidayKind::Christmas;
    if (month == 12 && day == 26) return GreekHolidayKind::BoxingDay;

    const auto easter = orthodox_easter_date(year);
    const auto current = days_from_civil(year, month, day);
    const auto easter_day = days_from_civil(easter.year, easter.month, easter.day);
    const int delta = static_cast<int>(current - easter_day);
    switch (delta) {
        case -48: return GreekHolidayKind::CleanMonday;
        case -2: return GreekHolidayKind::GoodFriday;
        case 0: return GreekHolidayKind::EasterSunday;
        case 1: return GreekHolidayKind::EasterMonday;
        case 50: return GreekHolidayKind::HolySpiritMonday;
        default: return GreekHolidayKind::None;
    }
}

// Greek names inflect ("ο Γιώργος", "του Γιώργου", "της Μαρίας"), so names
// are compared by stem; at least two letters always remain.
std::string name_stem(std::string folded) {
    static constexpr std::string_view kEndings[] = {"ουσ", "οσ", "ου", "ησ", "ασ", "εσ", "ων", "ο", "η", "α", "ε"};
    for (const auto ending : kEndings) {
        if (folded.size() >= ending.size() + 4 &&
            folded.compare(folded.size() - ending.size(), ending.size(), ending) == 0) {
            folded.resize(folded.size() - ending.size());
            break;
        }
    }
    return folded;
}

// "Πότε γιορτάζει…", "Πότε είναι η γιορτή…", "When is … name day?"
bool nameday_when_question(const std::vector<std::string>& words, const std::string& folded) {
    return folded_has_word(words, {"ποτε", "when"}) &&
           (folded_has_word_prefix(words, {"γιορτ", "εορτ"}) ||
            folded_has_word(words, {"celebrate", "celebrates", "nameday"}) ||
            folded_contains_phrase(folded, "name day"));
}

}  // namespace

GreekDate orthodox_easter_date(int year) noexcept {
    // Meeus' Julian-calendar algorithm, followed by an exact JDN conversion
    // to Gregorian. This remains correct across the supported 2000-2199 range,
    // including the Julian/Gregorian offset change in 2100.
    const int a = year % 4;
    const int b = year % 7;
    const int c = year % 19;
    const int d = (19 * c + 15) % 30;
    const int e = (2 * a + 4 * b - d + 34) % 7;
    const int julian_month = (d + e + 114) / 31;
    const int julian_day = (d + e + 114) % 31 + 1;

    const int ja = (14 - julian_month) / 12;
    const int jy = year + 4800 - ja;
    const int jm = julian_month + 12 * ja - 3;
    const std::int64_t jdn = julian_day + (153 * jm + 2) / 5 + 365LL * jy + jy / 4 - 32083;

    const std::int64_t ga = jdn + 32044;
    const std::int64_t gb = (4 * ga + 3) / 146097;
    const std::int64_t gc = ga - (146097 * gb) / 4;
    const std::int64_t gd = (4 * gc + 3) / 1461;
    const std::int64_t ge = gc - (1461 * gd) / 4;
    const std::int64_t gm = (5 * ge + 2) / 153;
    return {static_cast<int>(100 * gb + gd - 4800 + gm / 10),
            static_cast<int>(gm + 3 - 12 * (gm / 10)),
            static_cast<int>(ge - (153 * gm + 2) / 5 + 1)};
}

GreekCalendarDay greek_calendar_day(int year, int month, int day) {
    GreekCalendarDay result{{year, month, day}, holiday_for_date(year, month, day), {}};
    const auto easter = orthodox_easter_date(year);
    const GreekDate george{year, 4, 23};
    const GreekDate mark{year, 4, 25};

    for (const auto& entry : kFixedNamedays) {
        if (entry.month != month || entry.day != day) continue;
        if (entry.month == 4 && entry.day == 23 && before(george, easter)) continue;
        if (entry.month == 4 && entry.day == 25 && before(mark, easter)) continue;
        append_names(result.namedays, entry.names);
    }

    const auto current_days = days_from_civil(year, month, day);
    const auto easter_days = days_from_civil(easter.year, easter.month, easter.day);
    const int delta = static_cast<int>(current_days - easter_days);
    switch (delta) {
        case -8: append_names(result.namedays, "Λάζαρος, Λαζαρία"); break;
        case -7: append_names(result.namedays, "Βάιος, Βάια"); break;
        case 0: append_names(result.namedays, "Αναστάσιος, Αναστασία, Τάσος, Λάμπρος, Λαμπρινή, Πασχάλης"); break;
        case 1:
            if (before(george, easter)) append_names(result.namedays, "Γεώργιος, Γεωργία, Γιώργος");
            break;
        case 2:
            if (before(mark, easter)) append_names(result.namedays, "Μάρκος, Μαρκία");
            break;
        case 5: append_names(result.namedays, "Ζωή"); break;
        case 7: append_names(result.namedays, "Θωμάς, Θωμαΐς"); break;
        default: break;
    }
    return result;
}

std::optional<GreekHolidayOccurrence> next_greek_holiday(int year, int month, int day, bool include_today) {
    const auto start = days_from_civil(year, month, day);
    for (int offset = include_today ? 0 : 1; offset <= 370; ++offset) {
        const auto value = from_minute_index((start + offset) * 1440);
        const auto kind = holiday_for_date(value.year, value.month, value.day);
        if (kind != GreekHolidayKind::None) return GreekHolidayOccurrence{{value.year, value.month, value.day}, kind, offset};
    }
    return std::nullopt;
}

std::string join_namedays(const std::vector<std::string>& names) {
    std::string result;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0) result += ", ";
        result += names[index];
    }
    return result;
}

std::optional<NamedayMatch> greek_nameday_of(const std::string& text, int year) {
    const auto folded = fold_text(text);
    const auto words = folded_words(folded);
    if (!nameday_when_question(words, folded)) return std::nullopt;
    std::vector<std::string> stems;
    for (const auto& word : words) {
        if (word.size() >= 6) stems.push_back(name_stem(word));
    }
    NamedayMatch match;
    const auto last = days_from_civil(year, 12, 31);
    for (auto day = days_from_civil(year, 1, 1); day <= last; ++day) {
        const auto date = from_minute_index(day * 1440);
        for (const auto& name : greek_calendar_day(date.year, date.month, date.day).namedays) {
            if (std::find(stems.begin(), stems.end(), name_stem(fold_text(name))) == stems.end()) continue;
            if (match.name.empty()) match.name = name;
            match.dates.push_back({date.year, date.month, date.day});
            break;
        }
    }
    if (match.dates.empty()) return std::nullopt;
    return match;
}

GreekCalendarIntent analyze_greek_calendar_intent(const std::string& text) {
    const auto folded = fold_text(text);
    if (nameday_when_question(folded_words(folded), folded)) return GreekCalendarIntent::NamedayOf;
    if (folded.find("γιορταζει") != std::string::npos || folded.find("γιορταζουν") != std::string::npos ||
        folded.find("εορτολογιο") != std::string::npos || folded.find("ονομαστικη γιορτη") != std::string::npos ||
        folded.find("who celebrates") != std::string::npos || folded.find("who is celebrating") != std::string::npos ||
        folded.find("nameday") != std::string::npos || folded.find("name day") != std::string::npos) {
        return GreekCalendarIntent::Namedays;
    }
    if (folded.find("αργια") != std::string::npos || folded.find("αργιες") != std::string::npos ||
        folded.find("next holiday") != std::string::npos || folded.find("public holiday") != std::string::npos) {
        return GreekCalendarIntent::NextHoliday;
    }
    return GreekCalendarIntent::None;
}

const char* greek_holiday_name_el(GreekHolidayKind kind) noexcept {
    switch (kind) {
        case GreekHolidayKind::NewYear: return "Πρωτοχρονιά";
        case GreekHolidayKind::Epiphany: return "Θεοφάνεια";
        case GreekHolidayKind::CleanMonday: return "Καθαρά Δευτέρα";
        case GreekHolidayKind::IndependenceDay: return "25η Μαρτίου";
        case GreekHolidayKind::GoodFriday: return "Μεγάλη Παρασκευή";
        case GreekHolidayKind::EasterSunday: return "Κυριακή του Πάσχα";
        case GreekHolidayKind::EasterMonday: return "Δευτέρα του Πάσχα";
        case GreekHolidayKind::LabourDay: return "Πρωτομαγιά";
        case GreekHolidayKind::HolySpiritMonday: return "Αγίου Πνεύματος";
        case GreekHolidayKind::Dormition: return "Κοίμηση της Θεοτόκου";
        case GreekHolidayKind::OhiDay: return "28η Οκτωβρίου";
        case GreekHolidayKind::Christmas: return "Χριστούγεννα";
        case GreekHolidayKind::BoxingDay: return "Δεύτερη ημέρα Χριστουγέννων";
        case GreekHolidayKind::None: break;
    }
    return "";
}

const char* greek_holiday_name_en(GreekHolidayKind kind) noexcept {
    switch (kind) {
        case GreekHolidayKind::NewYear: return "New Year's Day";
        case GreekHolidayKind::Epiphany: return "Epiphany";
        case GreekHolidayKind::CleanMonday: return "Clean Monday";
        case GreekHolidayKind::IndependenceDay: return "Greek Independence Day";
        case GreekHolidayKind::GoodFriday: return "Orthodox Good Friday";
        case GreekHolidayKind::EasterSunday: return "Orthodox Easter Sunday";
        case GreekHolidayKind::EasterMonday: return "Orthodox Easter Monday";
        case GreekHolidayKind::LabourDay: return "Labour Day";
        case GreekHolidayKind::HolySpiritMonday: return "Holy Spirit Monday";
        case GreekHolidayKind::Dormition: return "Dormition of the Mother of God";
        case GreekHolidayKind::OhiDay: return "Ohi Day";
        case GreekHolidayKind::Christmas: return "Christmas Day";
        case GreekHolidayKind::BoxingDay: return "Second day of Christmas";
        case GreekHolidayKind::None: break;
    }
    return "";
}

}  // namespace aura

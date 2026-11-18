#include "aura/offline_commands.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string_view>

namespace aura {
namespace {

using Words = std::vector<std::string>;

bool is_one_of(const std::string& word, std::initializer_list<std::string_view> options) {
    return std::find(options.begin(), options.end(), word) != options.end();
}

bool all_digits(const std::string& word) {
    return !word.empty() && std::all_of(word.begin(), word.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
}

// Words that add nothing to a short request ("Αύρα, τι ώρα είναι τώρα;").
bool filler_word(const std::string& word) {
    return is_one_of(word, {"αυρα", "aura", "παρακαλω", "please", "μου", "μας", "τωρα", "now", "εδω", "λοιπον",
                            "hey", "ok", "οκ"});
}

// True when the words are one of the phrases plus filler words only, so a
// longer sentence that merely contains the phrase is left to the assistant.
bool only_phrase(const Words& words, std::initializer_list<std::string_view> phrases,
                 std::initializer_list<std::string_view> extra_fillers = {}) {
    for (const auto phrase : phrases) {
        const auto wanted = folded_words(phrase);
        if (wanted.empty() || wanted.size() > words.size()) continue;
        for (std::size_t start = 0; start + wanted.size() <= words.size(); ++start) {
            if (!std::equal(wanted.begin(), wanted.end(), words.begin() + static_cast<std::ptrdiff_t>(start))) {
                continue;
            }
            bool rest_is_filler = true;
            for (std::size_t index = 0; index < words.size() && rest_is_filler; ++index) {
                if (index >= start && index < start + wanted.size()) continue;
                rest_is_filler = filler_word(words[index]) || is_one_of(words[index], extra_fillers);
            }
            if (rest_is_filler) return true;
        }
    }
    return false;
}

// A request verb turns a sentence that names a setting into a command
// ("Κάνε σίγαση", "Βάλε ένταση 55").
bool has_request_verb(const Words& words) {
    return folded_has_word(words, {"βαλε", "βαλεισ", "κανε", "κανεισ", "ρυθμισε", "ρυθμισεισ", "χαμηλωσε",
                                   "χαμηλωσεισ", "ανεβασε", "ανεβασεισ", "δυναμωσε", "αλλαξε", "θεσε",
                                   "ενεργοποιησε", "αυξησε", "αυξησεισ", "μειωσε", "μειωσεισ", "κατεβασε",
                                   "κατεβασεισ", "set", "make", "turn", "change", "lower", "raise", "put",
                                   "increase", "decrease", "reduce"});
}

// "ένταση στο 40", "σίγαση": the setting alone, with numbers and filler.
bool bare_request(const Words& words, std::initializer_list<std::string_view> setting) {
    bool named = false;
    for (const auto& word : words) {
        if (is_one_of(word, setting)) {
            named = true;
            continue;
        }
        if (all_digits(word) || filler_word(word) || is_one_of(word, {"στο", "σε", "to", "at", "τοισ", "εκατο", "percent"})) {
            continue;
        }
        return false;
    }
    return named;
}

int first_number(const std::string& text, int minimum, int maximum) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9') continue;
        int value = 0;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            value = std::min(100000, value * 10 + (text[index] - '0'));
            ++index;
        }
        return value >= minimum && value <= maximum ? value : -1;
    }
    return -1;
}

int number_word(const std::string& word) {
    struct Number { std::string_view text; int value; };
    static constexpr Number numbers[]{
        {"ενα", 1}, {"εναν", 1}, {"μια", 1}, {"μιασ", 1}, {"δυο", 2}, {"τρια", 3}, {"τρεισ", 3},
        {"τεσσερα", 4}, {"τεσσερισ", 4}, {"πεντε", 5}, {"εξι", 6}, {"εφτα", 7}, {"επτα", 7}, {"οχτω", 8},
        {"οκτω", 8}, {"εννια", 9}, {"εννεα", 9}, {"δεκα", 10}, {"εντεκα", 11}, {"δωδεκα", 12},
        {"δεκατρια", 13}, {"δεκατεσσερα", 14}, {"δεκαπεντε", 15}, {"δεκαεξι", 16}, {"δεκαεφτα", 17},
        {"δεκαεπτα", 17}, {"δεκαοχτω", 18}, {"δεκαοκτω", 18}, {"δεκαεννια", 19}, {"δεκαεννεα", 19},
        {"εικοσι", 20}, {"τριαντα", 30}, {"σαραντα", 40}, {"πενηντα", 50}, {"εξηντα", 60}, {"ενενηντα", 90},
        {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7},
        {"eight", 8}, {"nine", 9}, {"ten", 10}, {"eleven", 11}, {"twelve", 12}, {"fifteen", 15},
        {"twenty", 20}, {"thirty", 30}, {"forty", 40}, {"fifty", 50}, {"sixty", 60}, {"ninety", 90},
    };
    for (const auto& number : numbers) {
        if (word == number.text) return number.value;
    }
    return 0;
}

// "20", "1.5" or "1,5" as a value; 0 for anything else.
double digits_value(const std::string& word) {
    if (word.empty() || word.front() < '0' || word.front() > '9') return 0;
    double value = 0;
    double scale = 0;
    for (const char ch : word) {
        if (ch == '.' || ch == ',') {
            if (scale != 0) return 0;
            scale = 1;
        } else if (ch >= '0' && ch <= '9') {
            if (scale != 0) {
                scale /= 10;
                value += (ch - '0') * scale;
            } else {
                value = std::min(100000.0, value * 10 + (ch - '0'));
            }
        } else {
            return 0;
        }
    }
    return value;
}

// The amount said right before the unit word at `end` ("είκοσι πέντε λεπτά",
// "μισή ώρα", "half an hour"); 0 when none.
double amount_before(const Words& words, std::size_t end) {
    if (end == 0) return 0;
    const auto& last = words[end - 1];
    if (const double digits = digits_value(last); digits > 0) return digits;
    if (is_one_of(last, {"μιαμιση", "μιαμιστη"})) return 1.5;
    if (is_one_of(last, {"μιση", "μισο", "half"})) return 0.5;
    if (is_one_of(last, {"a", "an"})) return end >= 2 && words[end - 2] == "half" ? 0.5 : 1;
    int value = number_word(last);
    if (value > 0 && value < 10 && end >= 2) {
        const int tens = number_word(words[end - 2]);
        if (tens >= 20 && tens % 10 == 0) value += tens;
    }
    return value;
}

// The timer length in seconds. The amount is the number right before the
// unit word, so the article in "βάλε ένα χρονόμετρο για πέντε λεπτά" is not
// a length and "εμένα" never reads as "ένα"; whole words only. 0 when no
// length is said.
long timer_length_seconds(const Words& words) {
    if (folded_has_word(words, {"τεταρτο", "quarter"})) return 900;
    std::size_t unit_index = words.size();
    double unit = 60;
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto& word = words[index];
        if (word.rfind("δευτερολεπτ", 0) == 0 || is_one_of(word, {"second", "seconds", "sec", "secs"})) {
            unit = 1;
        } else if (word.rfind("λεπτ", 0) == 0 || is_one_of(word, {"minute", "minutes", "min", "mins"})) {
            unit = 60;
        } else if (is_one_of(word, {"ωρα", "ωρεσ", "ωρων", "ωρασ", "hour", "hours"})) {
            unit = 3600;
        } else {
            continue;
        }
        unit_index = index;
        break;
    }
    double amount = amount_before(words, unit_index);
    if (amount <= 0) {
        for (const auto& word : words) {
            amount = digits_value(word);
            if (amount > 0) break;
        }
    }
    const double seconds = amount * unit;
    return seconds >= 1 && seconds <= 86400 ? static_cast<long>(seconds + 0.5) : 0;
}

std::string text_after(const std::string& original, std::initializer_list<const char*> prefixes) {
    for (const auto* prefix : prefixes) {
        const std::string value(prefix);
        if (original.rfind(value, 0) == 0 && original.size() > value.size()) {
            return sanitize_text(original.substr(value.size()), 160, true);
        }
    }
    return {};
}

// "buy me a coffee", "αγόρασέ μου κάτι": a favour, not a shopping item.
bool starts_with_pronoun(const std::string& text) {
    const auto words = folded_words(fold_text(text));
    return !words.empty() && is_one_of(words.front(), {"me", "us", "him", "her", "them", "you", "myself", "μου",
                                                       "μασ", "σου", "του", "τησ", "τουσ"});
}

// Greek past tense is written like a command ("Σημείωσε γκολ ο Ολυμπιακός;",
// "Αγόρασε η Μαρία τα εισιτήρια;"): a question is never a dictated item.
bool ends_as_question(const std::string& text) {
    if (text.empty()) return false;
    const char last = text.back();
    return last == '?' || last == ';' || (text.size() >= 2 && text.compare(text.size() - 2, 2, "\xCD\xBE") == 0);
}

constexpr int kVolumeStep = 10;

// "Δυνάμωσε την ένταση", "πιο σιγά", "turn the volume down": one step up or
// down; 0 when the words ask for no step.
int volume_step(const Words& words) {
    const bool volume_word = folded_has_word_prefix(words, {"εντασ"}) ||
                             folded_has_word(words, {"ηχο", "ηχοσ", "ηχου", "volume", "sound"});
    const bool turn = folded_has_word(words, {"turn"});
    const bool up = folded_has_word(words, {"δυναμωσε", "δυναμωσεισ", "ανεβασε", "ανεβασεισ", "αυξησε", "αυξησεισ",
                                            "raise", "increase"}) ||
                    (turn && folded_has_word(words, {"up"}));
    const bool down = folded_has_word(words, {"χαμηλωσε", "χαμηλωσεισ", "μειωσε", "μειωσεισ", "κατεβασε",
                                              "κατεβασεισ", "lower", "decrease", "reduce"}) ||
                      (turn && folded_has_word(words, {"down"}));
    if (volume_word && up != down) return up ? kVolumeStep : -kVolumeStep;
    if (only_phrase(words, {"πιο δυνατα", "louder", "volume up"})) return kVolumeStep;
    if (only_phrase(words, {"πιο σιγα", "πιο χαμηλα", "quieter", "volume down"})) return -kVolumeStep;
    return 0;
}

constexpr int kBrightnessStep = 20;

// "Αύξησε τη φωτεινότητα", "κάνε την οθόνη πιο φωτεινή": one step up or down; 0
// when the words ask for no step.
int brightness_step(const Words& words) {
    const bool named = folded_has_word_prefix(words, {"φωτεινοτητ"}) || folded_has_word(words, {"brightness"});
    const bool turn = folded_has_word(words, {"turn"});
    const bool up = folded_has_word(words, {"αυξησε", "αυξησεισ", "ανεβασε", "ανεβασεισ", "δυναμωσε", "δυναμωσεισ",
                                            "raise", "increase"}) ||
                    (turn && folded_has_word(words, {"up"}));
    const bool down = folded_has_word(words, {"μειωσε", "μειωσεισ", "χαμηλωσε", "χαμηλωσεισ", "κατεβασε", "κατεβασεισ",
                                              "lower", "decrease", "reduce", "dim"}) ||
                      (turn && folded_has_word(words, {"down"}));
    if (named && up != down) return up ? kBrightnessStep : -kBrightnessStep;
    const std::initializer_list<std::string_view> screen{"κανε", "κανεισ", "την", "τη", "η", "οθονη", "λιγο", "make",
                                                         "the", "screen", "a", "little", "bit"};
    if (only_phrase(words, {"πιο φωτεινη", "πιο φωτεινα", "brighter"}, screen)) return kBrightnessStep;
    if (only_phrase(words, {"πιο σκοτεινη", "πιο σκοτεινα", "dimmer", "darker"}, screen)) return -kBrightnessStep;
    return 0;
}

// What may stand beside a command for the device itself ("Μπορείς να κάνεις
// επανεκκίνηση στη συσκευή;"). Any other word makes it a sentence for the
// assistant: "τι κάνει η επανεκκίνηση", "στο κινητό μου".
bool device_request(const Words& words, std::initializer_list<std::string_view> own_words) {
    return std::all_of(words.begin(), words.end(), [&](const std::string& word) {
        return filler_word(word) || is_one_of(word, own_words) ||
               is_one_of(word, {"κανε", "κανεισ", "μπορεισ", "να", "θελω", "ξανα", "τη", "την", "το", "τον", "στη",
                                "στην", "στο", "σου", "εαυτο", "συσκευη", "συσκευησ", "the", "this", "your", "device",
                                "yourself", "can", "could", "would", "you"});
    });
}

bool asks_restart(const Words& words) {
    const std::initializer_list<std::string_view> restart{"επανεκκινηση", "επανεκκινησε", "επανεκκινησεισ",
                                                          "επανεκκινησου", "restart", "reboot"};
    return folded_has_word(words, restart) && device_request(words, restart);
}

bool asks_power_off(const Words& words) {
    if (!device_request(words, {"απενεργοποιησου", "απενεργοποιηση", "απενεργοποιησε", "απενεργοποιησεισ", "κλεισου",
                                "σβησου", "κλεισε", "κλεισεισ", "σβησε", "σβησεισ", "turn", "switch", "shut", "power",
                                "down", "off", "shutdown", "poweroff"})) {
        return false;
    }
    const bool itself = folded_has_word(words, {"απενεργοποιησου", "απενεργοποιηση", "κλεισου", "σβησου", "shutdown",
                                                "poweroff"});
    const bool device = folded_has_word(words, {"συσκευη", "συσκευησ", "εαυτο", "device", "yourself"});
    const bool closes = folded_has_word(words, {"απενεργοποιησε", "απενεργοποιησεισ", "κλεισε", "κλεισεισ", "σβησε",
                                                "σβησεισ"});
    const bool off = folded_has_word(words, {"off"});
    const bool english = (folded_has_word(words, {"power"}) && off) ||
                         (folded_has_word(words, {"shut"}) && folded_has_word(words, {"down"})) ||
                         (folded_has_word(words, {"turn", "switch"}) && off && device);
    return itself || (closes && device) || english;
}

// "Άνοιξε τις ρυθμίσεις": a verb, the app's name and nothing that makes it
// another request ("Show me the shower settings").
bool opens_only(const Words& words, std::initializer_list<std::string_view> names) {
    bool named = false;
    for (const auto& word : words) {
        if (is_one_of(word, names)) {
            named = true;
        } else if (!filler_word(word) &&
                   !is_one_of(word, {"ανοιξε", "ανοιξεισ", "δειξε", "δειξεισ", "πηγαινε", "παμε", "open", "show", "go",
                                     "to", "the", "my", "me", "το", "τα", "τη", "την", "τον", "τισ", "στο", "στα",
                                     "στη", "στην", "στον", "στισ", "σε", "να", "μπορεισ"})) {
            return false;
        }
    }
    return named;
}

bool parse_clock(const std::string& text, int& hour, int& minute) {
    for (std::size_t index = 0; index + 3 < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) continue;
        // Whisper writes a time as 7:30 or as 7.30.
        std::size_t colon = text.find_first_of(":.", index);
        if (colon == std::string::npos || colon > index + 2 || colon + 2 >= text.size()) continue;
        std::size_t end = colon + 1;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) ++end;
        if (end - colon - 1 != 2) continue;
        const auto hs = text.substr(index, colon - index);
        if (hs.empty() || hs.size() > 2) continue;
        const int h = std::atoi(hs.c_str());
        const int m = std::atoi(text.substr(colon + 1, 2).c_str());
        if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            hour = h;
            minute = m;
            return true;
        }
    }
    return false;
}

// The hour said after "στις", "at" or "for"; -1 when the word is none.
int hour_word(const std::string& word) {
    if (all_digits(word)) return word.size() <= 2 && std::atoi(word.c_str()) <= 23 ? std::atoi(word.c_str()) : -1;
    const int value = number_word(word);
    return value >= 1 && value <= 12 ? value : -1;
}

// An alarm's time: "07:30", "7.30", "στις 7", "στις εφτά και μισή", "στις οχτώ
// παρά τέταρτο", "8 το βράδυ", "at 7 pm". False when no time is said.
bool alarm_time(const std::string& clean, const Words& words, int& hour, int& minute) {
    if (!parse_clock(clean, hour, minute)) {
        std::size_t at = words.size();
        for (std::size_t index = 0; index + 1 < words.size() && at == words.size(); ++index) {
            if (is_one_of(words[index], {"στισ", "at", "for"}) && hour_word(words[index + 1]) >= 0) at = index + 1;
        }
        if (at == words.size()) return false;
        hour = hour_word(words[at]);
        minute = 0;
        if (at + 2 < words.size() && words[at + 1] == "και") {
            if (words[at + 2] == "μιση") minute = 30;
            else if (words[at + 2] == "τεταρτο") minute = 15;
        } else if (at + 2 < words.size() && words[at + 1] == "παρα" && words[at + 2] == "τεταρτο") {
            hour = (hour + 23) % 24;
            minute = 45;
        }
    }
    if (folded_has_word(words, {"βραδυ", "απογευμα", "pm"}) && hour < 12) hour += 12;
    else if (folded_has_word(words, {"μεσημερι"}) && hour < 6) hour += 12;
    else if (folded_has_word(words, {"πρωι", "am"}) && hour == 12) hour = 0;
    return true;
}

OfflineCommand action(AssistantActionType type) {
    OfflineCommand command;
    command.kind = OfflineCommandKind::ConfirmAction;
    command.has_action = true;
    command.action.type = type;
    return command;
}

}  // namespace

OfflineCommand parse_offline_command(const std::string& text) {
    OfflineCommand command;
    const auto clean = sanitize_text(text, 600, true);
    const auto folded = fold_text(clean);
    const auto words = folded_words(folded);
    if (words.empty()) return command;

    if (only_phrase(words, {"τι ωρα ειναι", "τι ωρα εχουμε", "τι ωρα", "πεσ την ωρα", "πεσ μου την ωρα",
                            "what time is it", "what s the time", "what is the time", "tell me the time",
                            "current time"})) {
        command.kind = OfflineCommandKind::ShowTime;
        return command;
    }
    if (only_phrase(words, {"ποση μπαταρια", "σταθμη μπαταριασ", "battery level", "how much battery"},
                    {"εχεισ", "εχει", "εχουμε", "μενει", "απομενει", "εμεινε", "ακομα", "ειναι", "η", "have",
                     "has", "do", "you", "is", "left", "there", "the", "what", "s", "it", "your"})) {
        command.kind = OfflineCommandKind::ShowBattery;
        return command;
    }

    if (asks_restart(words)) {
        command.kind = OfflineCommandKind::Restart;
        return command;
    }
    if (asks_power_off(words)) {
        command.kind = OfflineCommandKind::PowerOff;
        return command;
    }

    const bool open = folded_has_word(words, {"ανοιξε", "ανοιξεισ", "δειξε", "δειξεισ", "πηγαινε", "παμε", "open",
                                              "show"});
    if (open && ((folded_has_word(words, {"λιστα"}) && folded_has_word_prefix(words, {"αγορ"})) ||
                 folded_has_word_prefix(words, {"ψων"}) || folded_has_word(words, {"shopping", "groceries"}))) {
        command.kind = OfflineCommandKind::OpenShopping;
        return command;
    }
    if (open && (folded_has_word_prefix(words, {"εργασ", "δουλει"}) || folded_has_word(words, {"tasks", "todo"}) ||
                 folded_contains_phrase(folded, "to do"))) {
        command.kind = OfflineCommandKind::OpenTasks;
        return command;
    }
    if (open && (folded_has_word_prefix(words, {"ημερολογ", "προγραμμα"}) ||
                 folded_has_word(words, {"agenda", "calendar"}))) {
        command.kind = OfflineCommandKind::OpenCalendar;
        return command;
    }
    if (open && (folded_has_word_prefix(words, {"ξυπνητ", "χρονομετρ", "ρολοι"}) ||
                 folded_has_word(words, {"alarm", "alarms", "timer", "timers", "clock"}))) {
        command.kind = OfflineCommandKind::OpenClock;
        return command;
    }
    if (open && (folded_has_word_prefix(words, {"σημειωσ"}) || folded_has_word(words, {"notes", "memo", "memos"}))) {
        command.kind = OfflineCommandKind::OpenNotes;
        return command;
    }
    if (open && (folded_has_word_prefix(words, {"ραδιοφων"}) || folded_has_word(words, {"radio"}))) {
        command.kind = OfflineCommandKind::OpenRadio;
        return command;
    }
    if (open && (folded_has_word_prefix(words, {"καιρ"}) || folded_has_word(words, {"weather"}))) {
        command.kind = OfflineCommandKind::OpenWeather;
        return command;
    }
    if (open) {
        struct App { OfflineCommandKind kind; std::initializer_list<std::string_view> names; };
        const App apps[]{
            {OfflineCommandKind::OpenToday, {"σημερα", "today"}},
            {OfflineCommandKind::OpenSettings, {"ρυθμισεισ", "settings"}},
            {OfflineCommandKind::OpenMessages, {"μηνυματα", "messages", "aoraton", "αορατον"}},
            {OfflineCommandKind::OpenPhotos, {"φωτογραφιεσ", "photos", "pictures", "gallery"}},
            {OfflineCommandKind::OpenHome, {"αρχικη", "home"}},
            {OfflineCommandKind::OpenApps, {"μενου", "εφαρμογεσ", "menu", "apps"}},
            {OfflineCommandKind::OpenBattery, {"μπαταρια", "battery"}},
            {OfflineCommandKind::OpenWifi, {"wifi", "wi", "fi"}},
            {OfflineCommandKind::OpenFiles, {"μεταφορα", "αρχειων", "αρχεια", "files", "transfer"}},
        };
        for (const auto& app : apps) {
            if (opens_only(words, app.names)) {
                command.kind = app.kind;
                return command;
            }
        }
    }

    // Mute and unmute only on a request ("Κάνε σίγαση", "σίγασε", "mute"), never
    // on a sentence that mentions them ("Τι κάνει η σίγαση;").
    const bool unmute = folded_has_word(words, {"unmute"}) ||
                        (folded_has_word(words, {"βγαλε", "βγαλεισ", "κλεισε", "κλεισεισ", "απενεργοποιησε"}) &&
                         folded_has_word(words, {"σιγαση"})) ||
                        folded_contains_phrase(folded, "ανοιξε τον ηχο");
    if (unmute) {
        command = action(AssistantActionType::Mute);
        command.action.value = 0;
        return command;
    }
    const bool mute = folded_has_word(words, {"σιγασε", "σιγασεισ"}) || words.front() == "mute" ||
                      (folded_has_word(words, {"σιγαση", "mute"}) && has_request_verb(words)) ||
                      bare_request(words, {"σιγαση", "mute"}) ||
                      (folded_contains_phrase(folded, "χωρισ ηχο") && has_request_verb(words));
    if (mute) {
        command = action(AssistantActionType::Mute);
        command.action.value = 1;
        return command;
    }
    if (folded_has_word_prefix(words, {"εντασ"}) || folded_has_word(words, {"volume"})) {
        const int value = first_number(folded, 0, 100);
        if (value >= 0 && (has_request_verb(words) || bare_request(words, {"ενταση", "volume"}))) {
            command = action(AssistantActionType::Volume);
            command.action.value = value;
            return command;
        }
    }
    if (const int step = volume_step(words); step != 0 && first_number(folded, 0, 100) < 0) {
        command = action(AssistantActionType::Volume);
        command.action.value = step;
        command.action.relative = true;
        return command;
    }
    if (folded_has_word_prefix(words, {"φωτεινοτητ"}) || folded_has_word(words, {"brightness"})) {
        const int value = first_number(folded, 5, 100);
        if (value >= 0 && (has_request_verb(words) || bare_request(words, {"φωτεινοτητα", "brightness"}))) {
            command = action(AssistantActionType::Brightness);
            command.action.value = value;
            return command;
        }
    }
    if (const int step = brightness_step(words); step != 0 && first_number(folded, 0, 100000) < 0) {
        command = action(AssistantActionType::Brightness);
        command.action.value = step;
        command.action.relative = true;
        return command;
    }

    if ((folded_has_word_prefix(words, {"χρονομετρ"}) || folded_has_word(words, {"timer"})) &&
        folded_has_word(words, {"βαλε", "βαλεισ", "ξεκινα", "ξεκινησε", "start", "set"})) {
        const long seconds = timer_length_seconds(words);
        if (seconds > 0) {
            command = action(AssistantActionType::Timer);
            command.action.seconds = static_cast<int>(seconds);
            return command;
        }
    }
    const bool wake_me = (folded_has_word(words, {"ξυπνησε", "ξυπνησεισ"}) && folded_has_word(words, {"με"})) ||
                         folded_contains_phrase(folded, "wake me");
    if (wake_me || ((folded_has_word_prefix(words, {"ξυπνητ"}) || folded_has_word(words, {"alarm"})) &&
                    folded_has_word(words, {"βαλε", "βαλεισ", "ρυθμισε", "set"}))) {
        int hour = 0;
        int minute = 0;
        if (alarm_time(clean, words, hour, minute)) {
            command = action(AssistantActionType::Alarm);
            command.action.hour = hour;
            command.action.minute = minute;
            command.action.daily = folded_contains_phrase(folded, "καθε μερα") ||
                                   folded_has_word(words, {"daily"}) || folded_contains_phrase(folded, "every day");
            return command;
        }
    }

    if (ends_as_question(clean)) return command;
    std::string value = text_after(clean, {"σημείωσε ", "Σημείωσε ", "note ", "Note "});
    if (!value.empty()) {
        command = action(AssistantActionType::Note);
        command.action.text = std::move(value);
        return command;
    }
    value = text_after(clean, {"εργασία ", "Εργασία ", "task ", "Task ", "todo "});
    if (!value.empty()) {
        command = action(AssistantActionType::Task);
        command.action.text = std::move(value);
        return command;
    }
    value = text_after(clean, {"αγόρασε ", "Αγόρασε ", "ψώνια ", "Ψώνια ", "buy ", "Buy "});
    if (!value.empty() && !starts_with_pronoun(value)) {
        command = action(AssistantActionType::Shopping);
        command.action.text = std::move(value);
        return command;
    }
    return command;
}

const char* offline_command_app(OfflineCommandKind kind) noexcept {
    switch (kind) {
        case OfflineCommandKind::OpenClock: return "clock";
        case OfflineCommandKind::OpenNotes: return "notes";
        case OfflineCommandKind::OpenCalendar: return "calendar";
        case OfflineCommandKind::OpenTasks: return "tasks";
        case OfflineCommandKind::OpenShopping: return "shopping";
        case OfflineCommandKind::OpenRadio: return "radio";
        case OfflineCommandKind::OpenWeather: return "weather";
        case OfflineCommandKind::OpenToday: return "today";
        case OfflineCommandKind::OpenSettings: return "settings";
        case OfflineCommandKind::OpenMessages: return "messages";
        case OfflineCommandKind::OpenPhotos: return "photos";
        case OfflineCommandKind::OpenHome: return "home";
        case OfflineCommandKind::OpenApps: return "apps";
        case OfflineCommandKind::OpenBattery: return "battery";
        case OfflineCommandKind::OpenWifi: return "wifi";
        case OfflineCommandKind::OpenFiles: return "files";
        case OfflineCommandKind::None:
        case OfflineCommandKind::ShowTime:
        case OfflineCommandKind::ShowBattery:
        case OfflineCommandKind::Restart:
        case OfflineCommandKind::PowerOff:
        case OfflineCommandKind::ConfirmAction: break;
    }
    return nullptr;
}

}  // namespace aura

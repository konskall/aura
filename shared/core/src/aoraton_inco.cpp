#include "aura/aoraton_inco.hpp"

#include "aura/aoraton_crypto.hpp"
#include "aura/json_reader.hpp"
#include "aura/text_util.hpp"

#include <algorithm>
#include <cstdio>

namespace aura {
namespace {

constexpr std::size_t kMaxTurns = 24;
constexpr std::size_t kMaxTurnChars = 700;
constexpr std::size_t kMaxSources = 10;
constexpr std::size_t kMaxSourceTitleBytes = 300;
constexpr std::size_t kMaxSourceUriBytes = 2048;
constexpr std::size_t kMaxAvatarBytes = 2048;

constexpr const char* kWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
constexpr const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};

// [a-z0-9] after lower-casing, as in the web client's /(^|[^a-z0-9])@?inco([^a-z0-9]|$)/i.
bool word_char(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
// JavaScript's \s, restricted to ASCII.
bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string trim(std::string_view text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && space(text[first])) ++first;
    while (last > first && space(text[last - 1])) --last;
    return std::string(text.substr(first, last - first));
}

bool starts_with_https(const std::string& value) {
    static constexpr char kScheme[] = "https://";
    if (value.size() < sizeof(kScheme) - 1) return false;
    for (std::size_t index = 0; index + 1 < sizeof(kScheme); ++index) {
        if (lower(value[index]) != kScheme[index]) return false;
    }
    return true;
}

// The web normaliser works line by line first: "# heading" loses its marks
// (up to three spaces before one to six #), "* ", "- " and "+ " become "• ".
std::string strip_line(std::string line) {
    std::size_t lead = 0;
    while (lead < line.size() && space(line[lead])) ++lead;
    if (lead <= 3) {
        std::size_t hashes = lead;
        while (hashes < line.size() && line[hashes] == '#') ++hashes;
        const std::size_t count = hashes - lead;
        if (count >= 1 && count <= 6 && hashes < line.size() && (line[hashes] == ' ' || line[hashes] == '\t')) {
            std::size_t text = hashes;
            while (text < line.size() && (line[text] == ' ' || line[text] == '\t')) ++text;
            line.erase(0, text);
        }
    }
    std::size_t indent = 0;
    while (indent < line.size() && space(line[indent])) ++indent;
    if (indent + 1 < line.size() && (line[indent] == '*' || line[indent] == '+' || line[indent] == '-') &&
        (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        std::size_t text = indent + 1;
        while (text < line.size() && (line[text] == ' ' || line[text] == '\t')) ++text;
        line = line.substr(0, indent) + "\xE2\x80\xA2 " + line.substr(text);
    }
    return line;
}

// [text](url) -> text
std::string strip_links(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == '[') {
            const auto close = text.find(']', at + 1);
            if (close != std::string::npos && close > at + 1 && close + 1 < text.size() && text[close + 1] == '(') {
                const auto paren = text.find(')', close + 2);
                if (paren != std::string::npos) {
                    out.append(text, at + 1, close - at - 1);
                    at = paren + 1;
                    continue;
                }
            }
        }
        out.push_back(text[at++]);
    }
    return out;
}

// **x** / __x__ -> x (the shortest non-empty span).
std::string strip_pairs(const std::string& text, const char* mark) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        if (text.compare(at, 2, mark) == 0) {
            const auto close = text.find(mark, at + 3);
            if (close != std::string::npos) {
                out.append(text, at + 2, close - at - 2);
                at = close + 2;
                continue;
            }
        }
        out.push_back(text[at++]);
    }
    return out;
}

// *x* -> x, only when no space follows the opening * nor precedes the closing
// one, so "2 * 3" stays.
std::string strip_italic(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == '*' && at + 1 < text.size() && !space(text[at + 1])) {
            std::size_t close = at + 2;
            while (close < text.size() && !(text[close] == '*' && !space(text[close - 1]))) ++close;
            if (close < text.size()) {
                out.append(text, at + 1, close - at - 1);
                at = close + 1;
                continue;
            }
        }
        out.push_back(text[at++]);
    }
    return out;
}

// `x` -> x
std::string strip_code(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == '`') {
            const auto close = text.find('`', at + 1);
            if (close != std::string::npos && close > at + 1) {
                out.append(text, at + 1, close - at - 1);
                at = close + 1;
                continue;
            }
        }
        out.push_back(text[at++]);
    }
    return out;
}

struct LocalTime {
    int year{1970};
    int month{1};
    int day{1};
    int weekday{4};
    int hour{0};
    int minute{0};
};

// Civil date from Unix days (H. Hinnant's algorithm), at the given UTC offset.
LocalTime local_time(std::int64_t utc_unix, int offset_minutes) {
    const std::int64_t local = utc_unix + static_cast<std::int64_t>(offset_minutes) * 60;
    std::int64_t days = local / 86400;
    std::int64_t rest = local % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    LocalTime out;
    out.weekday = static_cast<int>(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday
    out.hour = static_cast<int>(rest / 3600);
    out.minute = static_cast<int>((rest % 3600) / 60);
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t doe = days - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    out.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    out.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    out.year = static_cast<int>(yoe + era * 400 + (out.month <= 2 ? 1 : 0));
    return out;
}

}  // namespace

bool aoraton_mentions_inco(std::string_view text) {
    std::string lowered(text);
    for (auto& c : lowered) c = lower(c);
    for (auto at = lowered.find("inco"); at != std::string::npos; at = lowered.find("inco", at + 1)) {
        const std::size_t end = at + 4;
        if ((at == 0 || !word_char(lowered[at - 1])) && (end == lowered.size() || !word_char(lowered[end]))) {
            return true;
        }
    }
    return false;
}

bool aoraton_inco_should_answer(const AoratonIncoCandidate& candidate, bool enabled, bool busy,
                                bool quota_exhausted) {
    if (!enabled || busy || quota_exhausted) return false;
    if (!candidate.sent_here || candidate.row_id.empty()) return false;
    if (candidate.uid.empty() || candidate.uid == kAoratonIncoUid) return false;
    if (!candidate.room_ai_enabled || candidate.type == "system") return false;
    if (trim(candidate.text).empty()) return false;
    return candidate.reply_to_inco || aoraton_mentions_inco(trim(candidate.text));
}

std::vector<AoratonIncoTurn> aoraton_inco_turns(const std::vector<AoratonIncoRow>& rows) {
    struct Item {
        const AoratonIncoRow* row;
        std::string body;
    };
    std::vector<Item> items;
    for (const auto& row : rows) {
        if (row.type == "system") continue;
        std::string body;
        if (row.has_poll) {
            // A poll usually has no text; inco should still see it. Attachments
            // are not described: inco cannot see them.
            body = "[poll: " + row.poll_question;
            if (!row.poll_options.empty()) {
                body += " \xE2\x80\x94 options: ";
                for (std::size_t index = 0; index < row.poll_options.size(); ++index) {
                    if (index) body += ", ";
                    body += row.poll_options[index];
                }
            }
            body += "]";
        } else {
            body = row.text;
        }
        if (!body.empty()) items.push_back({&row, std::move(body)});
    }
    const std::size_t first = items.size() > kMaxTurns ? items.size() - kMaxTurns : 0;
    std::vector<AoratonIncoTurn> turns;
    for (std::size_t index = first; index < items.size(); ++index) {
        const auto& row = *items[index].row;
        std::string text = aoraton_inco_prefix(items[index].body, kMaxTurnChars);
        if (row.uid == kAoratonIncoUid) {
            turns.push_back({true, std::move(text)});
            continue;
        }
        std::string who = row.username;
        if (!row.reply_to_username.empty()) who += " (reply to " + row.reply_to_username + ")";
        turns.push_back({false, who + ": " + text});
    }
    return turns;
}

std::string aoraton_inco_plain_text(const std::string& raw) {
    if (raw.empty()) return {};
    std::string lines;
    std::size_t start = 0;
    while (true) {
        const auto end = raw.find('\n', start);
        lines += strip_line(raw.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        lines.push_back('\n');
        start = end + 1;
    }
    std::string text = strip_links(lines);
    text = strip_pairs(text, "**");
    text = strip_pairs(text, "__");
    text = strip_italic(text);
    text = strip_code(text);
    text = trim(text);
    return text.empty() ? raw : text;
}

std::string aoraton_inco_prefix(std::string_view text, std::size_t count) {
    std::size_t at = 0;
    std::size_t seen = 0;
    while (at < text.size()) {
        if ((static_cast<unsigned char>(text[at]) & 0xC0) != 0x80) {
            if (seen == count) break;
            ++seen;
        }
        ++at;
    }
    return std::string(text.substr(0, at));
}

std::string aoraton_inco_client_date_time(std::int64_t utc_unix, int utc_offset_minutes) {
    const auto time = local_time(utc_unix, utc_offset_minutes);
    const int hour12 = time.hour % 12 == 0 ? 12 : time.hour % 12;
    char zone[16] = "UTC";
    if (utc_offset_minutes != 0) {
        const int magnitude = utc_offset_minutes < 0 ? -utc_offset_minutes : utc_offset_minutes;
        const char sign = utc_offset_minutes < 0 ? '-' : '+';
        if (magnitude % 60 == 0) std::snprintf(zone, sizeof(zone), "GMT%c%d", sign, magnitude / 60);
        else std::snprintf(zone, sizeof(zone), "GMT%c%d:%02d", sign, magnitude / 60, magnitude % 60);
    }
    char text[96];
    std::snprintf(text, sizeof(text), "%s, %s %d, %d at %02d:%02d %s %s", kWeekdays[time.weekday],
                  kMonths[time.month - 1], time.day, time.year, hour12, time.minute, time.hour < 12 ? "AM" : "PM",
                  zone);
    return text;
}

std::string aoraton_inco_local_day(std::int64_t utc_unix, int utc_offset_minutes) {
    const auto time = local_time(utc_unix, utc_offset_minutes);
    char text[16];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d", time.year, time.month, time.day);
    return text;
}

std::string aoraton_inco_request_body(const std::string& room_key,
                                      const std::vector<AoratonIncoTurn>& turns,
                                      const std::string& trigger_text,
                                      const std::string& trigger_username,
                                      const std::string& client_date_time,
                                      AoratonIncoLocation location,
                                      const std::string& city) {
    std::string body = "{\"roomKey\":\"" + json_escape(room_key) + "\",\"history\":[";
    for (std::size_t index = 0; index < turns.size(); ++index) {
        if (index) body.push_back(',');
        body += std::string("{\"role\":\"") + (turns[index].model ? "model" : "user") + "\",\"text\":\"" +
                json_escape(turns[index].text) + "\"}";
    }
    body += "],\"triggerText\":\"" + json_escape(trigger_text) + "\",\"triggerUsername\":\"" +
            json_escape(trigger_username) + "\",\"clientDateTime\":\"" + json_escape(client_date_time) + "\"";
    if (location == AoratonIncoLocation::City && !city.empty()) {
        body += ",\"locationCity\":\"" + json_escape(city) + "\"";
    } else if (location != AoratonIncoLocation::Unasked) {
        body += ",\"locationDenied\":true";
    }
    return body + "}";
}

bool aoraton_parse_inco_reply(std::string_view json, AoratonIncoReply& reply) {
    JsonValue value;
    if (!parse_json(json, value) || value.type() != JsonType::Object) return false;
    AoratonIncoReply parsed;
    const JsonValue* need = value.find("needLocation");
    const JsonValue* quota = value.find("quota");
    if (need && need->as_bool(false)) {
        parsed.kind = AoratonIncoReplyKind::NeedLocation;
    } else if (quota && quota->as_bool(false)) {
        parsed.kind = AoratonIncoReplyKind::Quota;
    } else {
        parsed.text = value.string_at("text");
        if (!aoraton_is_valid_utf8(parsed.text)) return false;
        parsed.kind = parsed.text.empty() ? AoratonIncoReplyKind::Empty : AoratonIncoReplyKind::Text;
        if (const JsonValue* sources = value.find("sources")) {
            for (const auto& item : sources->items()) {
                if (parsed.sources.size() == kMaxSources) break;
                AoratonIncoSource source;
                source.uri = item.string_at("uri");
                source.title = utf8_truncate(item.string_at("title"), kMaxSourceTitleBytes);
                if (!starts_with_https(source.uri) || source.uri.size() > kMaxSourceUriBytes ||
                    !aoraton_is_valid_utf8(source.uri) || !aoraton_is_valid_utf8(source.title)) {
                    continue;
                }
                parsed.sources.push_back(std::move(source));
            }
        }
    }
    reply = std::move(parsed);
    return true;
}

std::string aoraton_inco_avatar(const std::string& room_avatar) {
    const std::string fallback = kAoratonIncoDefaultAvatar;
    if (room_avatar.size() > kMaxAvatarBytes || !starts_with_https(room_avatar)) return fallback;
    for (const char c : room_avatar) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F || c == '\\') return fallback;
    }
    const auto authority_end = room_avatar.find_first_of("/?#", 8);
    const std::string authority =
        room_avatar.substr(8, authority_end == std::string::npos ? std::string::npos : authority_end - 8);
    // No user or password in the URL (the web client's isDisplayableAvatar).
    if (authority.empty() || authority.find('@') != std::string::npos) return fallback;
    return room_avatar;
}

std::string aoraton_inco_insert_body(const std::string& room_key,
                                     const std::string& avatar_url,
                                     const std::string& wire,
                                     const AoratonIncoQuote* quote,
                                     const std::vector<AoratonIncoSource>& sources) {
    std::string body = "{\"room_key\":\"" + json_escape(room_key) + "\",\"uid\":\"" + kAoratonIncoUid +
                       "\",\"username\":\"inco\",\"avatar_url\":\"" + json_escape(avatar_url) + "\",\"text\":\"" +
                       json_escape(wire) + "\",\"type\":\"text\"";
    if (quote) {
        body += ",\"reply_to\":{\"id\":\"" + json_escape(quote->id) + "\",\"username\":\"" +
                json_escape(quote->username) + "\",\"text\":\"" + json_escape(quote->wire) +
                "\",\"isAttachment\":" + (quote->is_attachment ? "true" : "false") + "}";
        body += ",\"grounding_metadata\":[";
        for (std::size_t index = 0; index < sources.size(); ++index) {
            if (index) body.push_back(',');
            body += "{\"title\":\"" + json_escape(sources[index].title) + "\",\"uri\":\"" +
                    json_escape(sources[index].uri) + "\"}";
        }
        body += "]";
    }
    return body + "}";
}

}  // namespace aura

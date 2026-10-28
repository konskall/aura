#include "aura/text_util.hpp"

#include <algorithm>
#include <cstdio>

namespace aura {
namespace {

// Decodes one UTF-8 code point at `position`; returns false (and skips one
// byte) for an invalid or overlong sequence.
bool next_code_point(std::string_view text, std::size_t& position, std::uint32_t& code) {
    const auto lead = static_cast<unsigned char>(text[position]);
    std::size_t length = 0;
    if (lead < 0x80) {
        code = lead;
        ++position;
        return true;
    }
    if ((lead & 0xE0) == 0xC0) { length = 2; code = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { length = 3; code = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { length = 4; code = lead & 0x07; }
    else { ++position; return false; }
    if (position + length > text.size()) { ++position; return false; }
    for (std::size_t index = 1; index < length; ++index) {
        const auto next = static_cast<unsigned char>(text[position + index]);
        if ((next & 0xC0) != 0x80) { ++position; return false; }
        code = (code << 6) | (next & 0x3F);
    }
    const bool overlong = (length == 2 && code < 0x80) || (length == 3 && code < 0x800) ||
                          (length == 4 && code < 0x10000);
    if (overlong || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) { ++position; return false; }
    position += length;
    return true;
}

void append_utf8(std::string& output, std::uint32_t code) {
    if (code < 0x80) {
        output.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        output.push_back(static_cast<char>(0xC0 | (code >> 6)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        output.push_back(static_cast<char>(0xE0 | (code >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (code >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

std::uint32_t fold_code_point(std::uint32_t code) {
    if (code >= 'A' && code <= 'Z') return code + 0x20;
    switch (code) {
        case 0x0386: case 0x03AC: return 0x03B1;  // Ά ά -> α
        case 0x0388: case 0x03AD: return 0x03B5;  // Έ έ -> ε
        case 0x0389: case 0x03AE: return 0x03B7;  // Ή ή -> η
        case 0x038A: case 0x03AF: case 0x0390: case 0x03AA: case 0x03CA: return 0x03B9;  // ι
        case 0x038C: case 0x03CC: return 0x03BF;  // Ό ό -> ο
        case 0x038E: case 0x03CD: case 0x03B0: case 0x03AB: case 0x03CB: return 0x03C5;  // υ
        case 0x038F: case 0x03CE: return 0x03C9;  // Ώ ώ -> ω
        case 0x03C2: return 0x03C3;               // final sigma
        default: break;
    }
    if (code >= 0x0391 && code <= 0x03A9) return code + 0x20;
    return code;
}

class FlatJsonParser {
public:
    explicit FlatJsonParser(std::string_view json) : json_(json) {}

    bool parse(std::vector<std::pair<std::string, FlatJsonValue>>& fields) {
        skip_space();
        if (!consume('{')) return false;
        skip_space();
        if (consume('}')) return finish();
        for (;;) {
            std::string key;
            skip_space();
            if (!parse_string(key)) return false;
            skip_space();
            if (!consume(':')) return false;
            skip_space();
            FlatJsonValue value;
            if (!parse_value(value)) return false;
            for (const auto& field : fields) {
                if (field.first == key) return false;
            }
            if (fields.size() >= 32) return false;
            fields.emplace_back(std::move(key), std::move(value));
            skip_space();
            if (consume(',')) continue;
            if (consume('}')) return finish();
            return false;
        }
    }

private:
    bool finish() {
        skip_space();
        return position_ == json_.size();
    }

    void skip_space() {
        while (position_ < json_.size() &&
               (json_[position_] == ' ' || json_[position_] == '\t' || json_[position_] == '\n' ||
                json_[position_] == '\r')) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < json_.size() && json_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool literal(std::string_view word) {
        if (json_.substr(position_, word.size()) != word) return false;
        position_ += word.size();
        return true;
    }

    bool hex4(std::uint32_t& value) {
        if (position_ + 4 > json_.size()) return false;
        value = 0;
        for (int index = 0; index < 4; ++index) {
            const char ch = json_[position_++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') value |= static_cast<std::uint32_t>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') value |= static_cast<std::uint32_t>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') value |= static_cast<std::uint32_t>(ch - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parse_string(std::string& output) {
        if (!consume('"')) return false;
        while (position_ < json_.size()) {
            const auto ch = static_cast<unsigned char>(json_[position_]);
            if (ch == '"') {
                ++position_;
                return true;
            }
            if (ch < 0x20) return false;
            if (ch != '\\') {
                output.push_back(static_cast<char>(ch));
                ++position_;
                continue;
            }
            ++position_;
            if (position_ >= json_.size()) return false;
            const char escaped = json_[position_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    std::uint32_t code = 0;
                    if (!hex4(code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        std::uint32_t low = 0;
                        if (!literal("\\u") || !hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        return false;
                    }
                    append_utf8(output, code);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool parse_value(FlatJsonValue& value) {
        if (position_ >= json_.size()) return false;
        const char ch = json_[position_];
        if (ch == '"') {
            value.kind = FlatJsonValue::Kind::String;
            return parse_string(value.text);
        }
        if (literal("true")) { value.kind = FlatJsonValue::Kind::Bool; value.boolean = true; return true; }
        if (literal("false")) { value.kind = FlatJsonValue::Kind::Bool; value.boolean = false; return true; }
        if (literal("null")) { value.kind = FlatJsonValue::Kind::Null; return true; }
        if (ch != '-' && (ch < '0' || ch > '9')) return false;
        bool negative = false;
        if (consume('-')) negative = true;
        std::int64_t number = 0;
        int digits = 0;
        while (position_ < json_.size() && json_[position_] >= '0' && json_[position_] <= '9') {
            if (++digits > 18) return false;
            number = number * 10 + (json_[position_] - '0');
            ++position_;
        }
        if (digits == 0) return false;
        // Fractions and exponents are not part of any AURA payload.
        if (position_ < json_.size() &&
            (json_[position_] == '.' || json_[position_] == 'e' || json_[position_] == 'E')) {
            return false;
        }
        value.kind = FlatJsonValue::Kind::Integer;
        value.integer = negative ? -number : number;
        return true;
    }

    std::string_view json_;
    std::size_t position_{0};
};

}  // namespace

std::string utf8_truncate(std::string_view text, std::size_t max_bytes) {
    if (text.size() <= max_bytes) return std::string(text);
    std::size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return std::string(text.substr(0, cut));
}

std::string sanitize_text(std::string_view text, std::size_t max_bytes, bool keep_newlines) {
    const auto output = sanitize_fragment(text, keep_newlines);
    const auto first = output.find_first_not_of(" \n");
    if (first == std::string::npos) return {};
    const auto last = output.find_last_not_of(" \n");
    return utf8_truncate(std::string_view(output).substr(first, last - first + 1), max_bytes);
}

std::size_t complete_utf8_prefix(std::string_view bytes) noexcept {
    std::size_t index = bytes.size();
    std::size_t continuation = 0;
    while (index > 0 && continuation < 3 && (static_cast<unsigned char>(bytes[index - 1]) & 0xC0) == 0x80) {
        --index;
        ++continuation;
    }
    if (index == 0) return bytes.size();
    const auto lead = static_cast<unsigned char>(bytes[index - 1]);
    std::size_t length = 0;
    if ((lead & 0xE0) == 0xC0) length = 2;
    else if ((lead & 0xF0) == 0xE0) length = 3;
    else if ((lead & 0xF8) == 0xF0) length = 4;
    else return bytes.size();
    return continuation + 1 < length ? index - 1 : bytes.size();
}

std::string sanitize_fragment(std::string_view text, bool keep_newlines) {
    std::string output;
    output.reserve(text.size());
    std::size_t position = 0;
    while (position < text.size()) {
        std::uint32_t code = 0;
        if (!next_code_point(text, position, code)) continue;
        if (code == '\r') continue;
        if (code == '\t') code = ' ';
        if (code == '\n') {
            if (!keep_newlines) code = ' ';
        } else if (code < 0x20 || code == 0x7F || (code >= 0x80 && code <= 0x9F)) {
            continue;
        }
        // Bidirectional overrides can make stored text display deceptively.
        if ((code >= 0x202A && code <= 0x202E) || (code >= 0x2066 && code <= 0x2069)) continue;
        append_utf8(output, code);
    }
    return output;
}

std::string fold_text(std::string_view text) {
    std::string output;
    output.reserve(text.size());
    std::size_t position = 0;
    while (position < text.size()) {
        std::uint32_t code = 0;
        if (!next_code_point(text, position, code)) continue;
        // Decomposed tonos/dialytika marks carry no meaning for matching.
        if (code == 0x0301 || code == 0x0308 || code == 0x0344) continue;
        append_utf8(output, fold_code_point(code));
    }
    return output;
}

bool folded_contains_any(std::string_view folded, std::initializer_list<std::string_view> needles) {
    for (const auto needle : needles) {
        if (!needle.empty() && folded.find(needle) != std::string_view::npos) return true;
    }
    return false;
}

namespace {

// Letters (Latin, Greek, Cyrillic) and digits; punctuation such as the Greek
// question mark, the ano teleia and guillemets separates words.
bool word_code_point(std::uint32_t code) {
    if ((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return true;
    if (code == 0x037E || code == 0x0387 || code == 0x00D7 || code == 0x00F7) return false;
    return (code >= 0x00C0 && code <= 0x024F) || (code >= 0x0370 && code <= 0x03FF) ||
           (code >= 0x0400 && code <= 0x04FF) || (code >= 0x1F00 && code <= 0x1FFF);
}

}  // namespace

std::vector<std::string> folded_words(std::string_view folded) {
    std::vector<std::string> words;
    std::string word;
    std::size_t position = 0;
    while (position < folded.size()) {
        const std::size_t start = position;
        std::uint32_t code = 0;
        if (!next_code_point(folded, position, code)) continue;
        if (word_code_point(code)) {
            word.append(folded.substr(start, position - start));
            continue;
        }
        const bool inside_number = (code == '.' || code == ',') && !word.empty() && word.back() >= '0' &&
                                   word.back() <= '9' && position < folded.size() && folded[position] >= '0' &&
                                   folded[position] <= '9';
        if (inside_number) {
            word.push_back(static_cast<char>(code));
        } else if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    return words;
}

bool folded_has_word(const std::vector<std::string>& words, std::initializer_list<std::string_view> needles) {
    for (const auto& word : words) {
        for (const auto needle : needles) {
            if (word == needle) return true;
        }
    }
    return false;
}

bool folded_has_word_prefix(const std::vector<std::string>& words, std::initializer_list<std::string_view> stems) {
    for (const auto& word : words) {
        for (const auto stem : stems) {
            if (!stem.empty() && word.compare(0, stem.size(), stem) == 0) return true;
        }
    }
    return false;
}

bool folded_contains_phrase(std::string_view folded, std::string_view phrase) {
    const auto words = folded_words(folded);
    const auto wanted = folded_words(phrase);
    if (wanted.empty() || wanted.size() > words.size()) return false;
    for (std::size_t start = 0; start + wanted.size() <= words.size(); ++start) {
        if (std::equal(wanted.begin(), wanted.end(), words.begin() + static_cast<std::ptrdiff_t>(start))) return true;
    }
    return false;
}

std::string json_escape(std::string_view text) {
    std::string result;
    result.reserve(text.size() + 16);
    for (const unsigned char ch : text) {
        switch (ch) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(ch));
                    result += escaped;
                } else {
                    result.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return result;
}

const FlatJsonValue* FlatJsonObject::find(std::string_view key) const noexcept {
    for (const auto& field : fields_) {
        if (field.first == key) return &field.second;
    }
    return nullptr;
}

std::optional<std::string> FlatJsonObject::string(std::string_view key) const {
    const auto* value = find(key);
    if (!value || value->kind != FlatJsonValue::Kind::String) return std::nullopt;
    return value->text;
}

std::optional<std::int64_t> FlatJsonObject::integer(std::string_view key) const {
    const auto* value = find(key);
    if (!value || value->kind != FlatJsonValue::Kind::Integer) return std::nullopt;
    return value->integer;
}

std::optional<bool> FlatJsonObject::boolean(std::string_view key) const {
    const auto* value = find(key);
    if (!value || value->kind != FlatJsonValue::Kind::Bool) return std::nullopt;
    return value->boolean;
}

std::optional<FlatJsonObject> parse_flat_json(std::string_view json, std::size_t max_bytes) {
    if (json.empty() || json.size() > max_bytes) return std::nullopt;
    FlatJsonObject object;
    FlatJsonParser parser(json);
    if (!parser.parse(object.fields_)) return std::nullopt;
    return object;
}

namespace {
int days_in_month(int year, int month) noexcept {
    static constexpr int kDays[12]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}
}  // namespace

bool local_date_time_valid(const LocalDateTime& value) noexcept {
    return value.year >= 2000 && value.year <= 2199 && value.month >= 1 && value.month <= 12 &&
           value.day >= 1 && value.day <= days_in_month(value.year, value.month) &&
           value.hour >= 0 && value.hour <= 23 && value.minute >= 0 && value.minute <= 59 &&
           value.second >= 0 && value.second <= 59 && value.weekday >= 0 && value.weekday <= 6;
}

std::int64_t days_from_civil(int year, int month, int day) noexcept {
    const std::int64_t y = static_cast<std::int64_t>(year) - (month <= 2 ? 1 : 0);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<std::uint64_t>(y - era * 400);
    const auto doy = static_cast<std::uint64_t>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const std::uint64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::int64_t minute_index(const LocalDateTime& value) noexcept {
    return days_from_civil(value.year, value.month, value.day) * 1440 + value.hour * 60 + value.minute;
}

LocalDateTime from_minute_index(std::int64_t index) noexcept {
    std::int64_t days = index >= 0 ? index / 1440 : -((-index + 1439) / 1440);
    const std::int64_t minutes = index - days * 1440;
    LocalDateTime result;
    result.hour = static_cast<int>(minutes / 60);
    result.minute = static_cast<int>(minutes % 60);
    result.weekday = static_cast<int>(((days % 7) + 7 + 4) % 7);
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto doe = static_cast<std::uint64_t>(days - era * 146097);
    const std::uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::uint64_t mp = (5 * doy + 2) / 153;
    const auto month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    result.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    result.month = month;
    result.year = static_cast<int>(static_cast<std::int64_t>(yoe) + era * 400 + (month <= 2 ? 1 : 0));
    return result;
}

}  // namespace aura

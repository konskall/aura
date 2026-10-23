#include "aura/json_reader.hpp"

#include <cstdlib>

namespace aura {

namespace {

const std::string kEmptyString;
const std::vector<JsonValue> kEmptyItems;

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

// Recursion depth is bounded by JsonLimits::max_depth, so a hostile payload
// cannot exhaust the stack; every failure leaves the output untouched.
class JsonParser final {
public:
    JsonParser(std::string_view text, const JsonLimits& limits) : text_(text), limits_(limits) {}

    bool parse(JsonValue& out) {
        if (text_.size() > limits_.max_bytes) return false;
        JsonValue value;
        skip_space();
        if (!parse_value(value, 0)) return false;
        skip_space();
        if (position_ != text_.size()) return false;
        out = std::move(value);
        return true;
    }

private:
    bool at_end() const noexcept { return position_ >= text_.size(); }
    char peek() const noexcept { return at_end() ? '\0' : text_[position_]; }

    void skip_space() {
        while (!at_end()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++position_;
        }
    }

    bool literal(std::string_view word) {
        if (text_.substr(position_, word.size()) != word) return false;
        position_ += word.size();
        return true;
    }

    bool count_node() { return ++nodes_ <= limits_.max_nodes; }

    bool parse_value(JsonValue& value, std::size_t depth) {
        if (!count_node()) return false;
        switch (peek()) {
            case '{': return parse_object(value, depth + 1);
            case '[': return parse_array(value, depth + 1);
            case '"':
                value.type_ = JsonType::String;
                return parse_string(value.text_);
            case 't':
                value.type_ = JsonType::Bool;
                value.boolean_ = true;
                return literal("true");
            case 'f':
                value.type_ = JsonType::Bool;
                value.boolean_ = false;
                return literal("false");
            case 'n':
                value.type_ = JsonType::Null;
                return literal("null");
            default: return parse_number(value);
        }
    }

    bool parse_object(JsonValue& value, std::size_t depth) {
        if (depth > limits_.max_depth) return false;
        value.type_ = JsonType::Object;
        ++position_;
        skip_space();
        if (peek() == '}') {
            ++position_;
            return true;
        }
        while (true) {
            skip_space();
            if (peek() != '"') return false;
            std::string key;
            if (!parse_string(key)) return false;
            skip_space();
            if (peek() != ':') return false;
            ++position_;
            skip_space();
            JsonValue member;
            if (!parse_value(member, depth)) return false;
            value.keys_.push_back(std::move(key));
            value.items_.push_back(std::move(member));
            skip_space();
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == '}') {
                ++position_;
                return true;
            }
            return false;
        }
    }

    bool parse_array(JsonValue& value, std::size_t depth) {
        if (depth > limits_.max_depth) return false;
        value.type_ = JsonType::Array;
        ++position_;
        skip_space();
        if (peek() == ']') {
            ++position_;
            return true;
        }
        while (true) {
            skip_space();
            JsonValue item;
            if (!parse_value(item, depth)) return false;
            value.items_.push_back(std::move(item));
            skip_space();
            if (peek() == ',') {
                ++position_;
                continue;
            }
            if (peek() == ']') {
                ++position_;
                return true;
            }
            return false;
        }
    }

    bool read_hex4(std::uint32_t& code) {
        if (position_ + 4 > text_.size()) return false;
        code = 0;
        for (int index = 0; index < 4; ++index) {
            const int digit = hex_value(text_[position_++]);
            if (digit < 0) return false;
            code = (code << 4) | static_cast<std::uint32_t>(digit);
        }
        return true;
    }

    bool parse_string(std::string& out) {
        ++position_;  // opening quote
        while (true) {
            if (at_end()) return false;
            const char c = text_[position_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                out.push_back(c);
            } else {
                if (at_end()) return false;
                const char escape = text_[position_++];
                switch (escape) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t code = 0;
                        if (!read_hex4(code)) return false;
                        if (code >= 0xDC00 && code <= 0xDFFF) return false;
                        if (code >= 0xD800 && code <= 0xDBFF) {
                            std::uint32_t low = 0;
                            if (!literal("\\u") || !read_hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        }
                        append_utf8(out, code);
                        break;
                    }
                    default: return false;
                }
            }
            if (out.size() > limits_.max_string_bytes) return false;
        }
    }

    bool parse_number(JsonValue& value) {
        const std::size_t start = position_;
        if (peek() == '-') ++position_;
        if (peek() == '0') {
            ++position_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') ++position_;
        } else {
            return false;
        }
        if (peek() == '.') {
            ++position_;
            if (!(peek() >= '0' && peek() <= '9')) return false;
            while (peek() >= '0' && peek() <= '9') ++position_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++position_;
            if (peek() == '+' || peek() == '-') ++position_;
            if (!(peek() >= '0' && peek() <= '9')) return false;
            while (peek() >= '0' && peek() <= '9') ++position_;
        }
        if (position_ - start > 64) return false;
        value.type_ = JsonType::Number;
        value.text_.assign(text_.substr(start, position_ - start));
        return true;
    }

    std::string_view text_;
    const JsonLimits& limits_;
    std::size_t position_{0};
    std::size_t nodes_{0};
};

const std::string& JsonValue::as_string() const noexcept {
    return type_ == JsonType::String ? text_ : kEmptyString;
}

const std::string& JsonValue::number_text() const noexcept {
    return type_ == JsonType::Number ? text_ : kEmptyString;
}

std::int64_t JsonValue::as_int(std::int64_t fallback) const noexcept {
    if (type_ != JsonType::Number || text_.find_first_of(".eE") != std::string::npos) return fallback;
    char* end = nullptr;
    const long long parsed = std::strtoll(text_.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<std::int64_t>(parsed) : fallback;
}

bool JsonValue::as_bool(bool fallback) const noexcept {
    return type_ == JsonType::Bool ? boolean_ : fallback;
}

const std::vector<JsonValue>& JsonValue::items() const noexcept {
    return type_ == JsonType::Array ? items_ : kEmptyItems;
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    if (type_ != JsonType::Object) return nullptr;
    for (std::size_t index = 0; index < keys_.size(); ++index) {
        if (keys_[index] == key) return &items_[index];
    }
    return nullptr;
}

std::string JsonValue::string_at(std::string_view key) const {
    const JsonValue* member = find(key);
    if (!member) return {};
    if (member->type_ == JsonType::String || member->type_ == JsonType::Number) return member->text_;
    return {};
}

bool parse_json(std::string_view text, JsonValue& out, const JsonLimits& limits) {
    JsonParser parser(text, limits);
    return parser.parse(out);
}

}  // namespace aura

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aura {

enum class JsonType { Null, Bool, Number, String, Array, Object };

// Bounds for untrusted network JSON (Supabase REST rows). Anything larger,
// deeper or busier is rejected as a whole rather than partially read.
struct JsonLimits {
    std::size_t max_bytes = 524288;
    std::size_t max_depth = 8;
    std::size_t max_nodes = 16384;
    std::size_t max_string_bytes = 16384;
};

class JsonValue final {
public:
    JsonType type() const noexcept { return type_; }
    bool is_null() const noexcept { return type_ == JsonType::Null; }

    // Empty unless the value is a String.
    const std::string& as_string() const noexcept;
    // The validated number token exactly as received (ids keep every digit).
    const std::string& number_text() const noexcept;
    std::int64_t as_int(std::int64_t fallback = 0) const noexcept;
    bool as_bool(bool fallback = false) const noexcept;

    // Empty unless the value is an Array.
    const std::vector<JsonValue>& items() const noexcept;
    // nullptr unless the value is an Object holding `key`.
    const JsonValue* find(std::string_view key) const noexcept;
    // A String member, or a Number member's text; empty otherwise.
    std::string string_at(std::string_view key) const;

private:
    friend class JsonParser;

    JsonType type_{JsonType::Null};
    bool boolean_{false};
    std::string text_;
    std::vector<JsonValue> items_;
    std::vector<std::string> keys_;
};

bool parse_json(std::string_view text, JsonValue& out, const JsonLimits& limits = {});

}  // namespace aura

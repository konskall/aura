#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aura {

// Cuts `text` to at most `max_bytes` without splitting a UTF-8 sequence.
std::string utf8_truncate(std::string_view text, std::size_t max_bytes);

// Untrusted text for display or storage: drops control bytes (except '\n'
// when `keep_newlines`), invalid UTF-8 and surrounding whitespace.
std::string sanitize_text(std::string_view text, std::size_t max_bytes, bool keep_newlines = false);
// sanitize_text's per-character rules without trimming or a size limit, for
// text that arrives in pieces (pass complete_utf8_prefix() bytes).
std::string sanitize_fragment(std::string_view text, bool keep_newlines);
// The length of the longest prefix of `bytes` that does not end inside a
// UTF-8 sequence.
std::size_t complete_utf8_prefix(std::string_view bytes) noexcept;

// Lower-case ASCII and Greek, without tonos/dialytika, for keyword matching.
std::string fold_text(std::string_view text);

// True when the folded `text` contains any of the (already folded) needles.
bool folded_contains_any(std::string_view folded, std::initializer_list<std::string_view> needles);

// The words of folded text: runs of letters and digits. A '.' or ',' between
// two digits stays inside a number ("92.9"); everything else separates.
std::vector<std::string> folded_words(std::string_view folded);
// True when one of `words` is exactly one of the (folded) needles.
bool folded_has_word(const std::vector<std::string>& words, std::initializer_list<std::string_view> needles);
// True when one of `words` starts with one of the stems (Greek words inflect).
bool folded_has_word_prefix(const std::vector<std::string>& words, std::initializer_list<std::string_view> stems);
// True when folded text contains the folded `phrase` as whole, adjacent words.
bool folded_contains_phrase(std::string_view folded, std::string_view phrase);

std::string json_escape(std::string_view text);

// A flat JSON object: string, integer, boolean or null values only. Nested
// objects/arrays, duplicate keys and trailing garbage are rejected.
struct FlatJsonValue {
    enum class Kind { Null, Bool, Integer, String };
    Kind kind{Kind::Null};
    bool boolean{false};
    std::int64_t integer{0};
    std::string text;
};

class FlatJsonObject {
public:
    const FlatJsonValue* find(std::string_view key) const noexcept;
    std::optional<std::string> string(std::string_view key) const;
    std::optional<std::int64_t> integer(std::string_view key) const;
    std::optional<bool> boolean(std::string_view key) const;
    std::size_t size() const noexcept { return fields_.size(); }

private:
    friend std::optional<FlatJsonObject> parse_flat_json(std::string_view json, std::size_t max_bytes);
    std::vector<std::pair<std::string, FlatJsonValue>> fields_;
};

std::optional<FlatJsonObject> parse_flat_json(std::string_view json, std::size_t max_bytes = 4096);

// Civil calendar helpers (proleptic Gregorian, no time zones): a "minute
// index" counts minutes since 1970-01-01 00:00 local time.
struct LocalDateTime {
    int year{0};
    int month{0};
    int day{0};
    int hour{0};
    int minute{0};
    int second{0};
    int weekday{0};  // 0 = Sunday
};

bool local_date_time_valid(const LocalDateTime& value) noexcept;
std::int64_t days_from_civil(int year, int month, int day) noexcept;
std::int64_t minute_index(const LocalDateTime& value) noexcept;
LocalDateTime from_minute_index(std::int64_t index) noexcept;

}  // namespace aura

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aura {

// The device lies between two people: the bottom half faces the owner, the
// top half (drawn upside down) faces the other person.
enum class TranslateSide { Mine, Theirs };

enum class TranslationStatus { Listening, Transcribing, Translating, Ready, Failed };

struct TranslationEntry {
    std::uint32_t id{0};
    TranslateSide side{TranslateSide::Mine};
    TranslationStatus status{TranslationStatus::Listening};
    std::string source;
    std::string translated;
    std::string error_code;
};

struct TranslationSnapshot {
    std::string my_language{"el"};
    std::string their_language{"en"};
    std::vector<TranslationEntry> entries;  // oldest first, bounded
    std::uint64_t sequence{0};
};

class TranslationModel {
public:
    static constexpr std::size_t kMaxEntries = 6;
    static constexpr std::size_t kMaxTextBytes = 600;

    std::uint32_t begin(TranslateSide side, TranslationStatus status = TranslationStatus::Listening);
    bool set_status(std::uint32_t id, TranslationStatus status);
    bool set_source(std::uint32_t id, const std::string& text);
    bool complete(std::uint32_t id, const std::string& translated);
    bool fail(std::uint32_t id, const std::string& code);
    bool cancel(std::uint32_t id);
    // Supported pairs: Greek with English, German, French, Italian or Spanish.
    bool set_their_language(const std::string& code);
    void clear();

    const TranslationEntry* find(std::uint32_t id) const noexcept;
    const std::string& source_language(TranslateSide side) const noexcept;
    const std::string& target_language(TranslateSide side) const noexcept;
    const TranslationSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    TranslationEntry* find_mutable(std::uint32_t id) noexcept;
    void changed() noexcept { ++snapshot_.sequence; }

    TranslationSnapshot snapshot_{};
    std::uint32_t next_id_{1};
};

bool translation_language_supported(const std::string& code) noexcept;

}  // namespace aura

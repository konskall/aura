#pragma once

#include "aura/provider.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

// What the owner asked and what the assistant answered, kept so they can be
// read again (the owner, 2026-11-18). It stays on the device: the model is sent
// only ConversationMemory's few recent turns, never this. Bounded, and stored
// as text on the card; deleting it from the screen deletes the file's contents.
class ConversationLog {
public:
    static constexpr std::size_t kMaxTurns = 40;
    static constexpr std::size_t kMaxQuestionBytes = 600;
    static constexpr std::size_t kMaxAnswerBytes = 2400;

    // `minute` is the local minute index of the answer; 0 when the time is unknown.
    void add(const std::string& question, const std::string& answer, std::int64_t minute);
    void clear() noexcept;

    const std::vector<ConversationTurn>& turns() const noexcept { return turns_; }  // oldest first
    std::uint64_t sequence() const noexcept { return sequence_; }

    std::string serialize() const;
    // False leaves the log as it was (not this format, or damaged).
    bool restore(std::string_view data);

private:
    std::vector<ConversationTurn> turns_;
    std::uint64_t sequence_{0};
};

}  // namespace aura

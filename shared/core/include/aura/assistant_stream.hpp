#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace aura {

// The assistant provider runs outside the LVGL thread. It publishes only a
// bounded, sanitized snapshot here; the UI thread consumes the newest value.
// Intermediate snapshots may be coalesced, but the accumulated text is not
// lost. This keeps slow rendering from creating an unbounded token queue.
struct AssistantStreamSnapshot {
    std::string text;
    std::string first_sentence;
    std::uint64_t sequence{0};
    bool truncated{false};
};

// Returns the first complete sentence suitable for early TTS. Greek uses ';'
// as a question mark, so it is treated as a sentence terminator too.
std::optional<std::string> first_complete_sentence(std::string_view text);

// What is left to say of `final_text` after `spoken_prefix` (its first
// sentence, spoken early from the stream). Differences in spacing,
// punctuation or markup do not make that sentence play twice; a final answer
// that does not begin with it is returned whole.
std::string unspoken_remainder(std::string_view final_text, std::string_view spoken_prefix);

class AssistantStreamMailbox {
public:
    static constexpr std::size_t kMaxTextBytes = 12000;
    static constexpr std::size_t kMaxRawBytes = kMaxTextBytes * 2;

    // Called by a provider worker. `delta` is untrusted model output.
    void publish_delta(std::string_view delta);

    // Called only by the owner/UI thread. Returns false if nothing changed
    // since `after_sequence`.
    bool take_after(std::uint64_t after_sequence, AssistantStreamSnapshot& output) const;

private:
    mutable std::mutex mutex_;
    std::size_t raw_bytes_{0};
    std::string clean_;       // cleaned once per delta, not again for every delta
    std::string incomplete_;  // the start of a UTF-8 sequence split across deltas
    AssistantStreamSnapshot latest_{};
};

// Incremental decoder for the device gateway's newline-delimited JSON:
// {"type":"delta","text":"..."}, {"type":"done","text":"..."}, or
// {"type":"error","code":"..."}. It is shared by Windows and ESP-IDF.
class AssistantStreamDecoder {
public:
    static constexpr std::size_t kMaxLineBytes = 16384;

    explicit AssistantStreamDecoder(AssistantStreamMailbox* mailbox = nullptr) : mailbox_(mailbox) {}
    bool push(std::string_view bytes);
    bool finish();
    const std::string& final_text() const noexcept { return final_text_; }
    const std::string& error_code() const noexcept { return error_code_; }
    bool done() const noexcept { return done_; }

private:
    bool consume_line(std::string_view line);

    AssistantStreamMailbox* mailbox_{};
    std::string pending_;
    std::string final_text_;
    std::string error_code_;
    bool done_{false};
};

}  // namespace aura

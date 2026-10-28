#include "aura/assistant_stream.hpp"

#include "aura/text_util.hpp"

#include <algorithm>

namespace aura {
namespace {

bool ascii_digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

std::size_t code_points(std::string_view text) noexcept {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char value) {
        return (static_cast<unsigned char>(value) & 0xC0) != 0x80;
    }));
}

// A dot after "π.χ", "κ.λπ", "e.g", an initial ("Α. Παπαδόπουλος"), a known
// abbreviation ("δηλ.", "Dr.") or a list number at the start of a line ("1.")
// does not end a sentence, so early speech never says just "1." or "π.".
bool abbreviation_dot(std::string_view text, std::size_t index) {
    std::size_t start = index;
    while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '\n' && text[start - 1] != '\t') --start;
    const auto word = text.substr(start, index - start);
    if (word.empty()) return false;
    if (word.find('.') != std::string_view::npos) return true;
    const bool digits = std::all_of(word.begin(), word.end(), ascii_digit);
    if (digits) return start == 0 || text[start - 1] == '\n';
    if (code_points(word) == 1) return true;
    const auto folded = fold_text(word);
    static constexpr std::string_view kAbbreviations[] = {"δηλ", "βλ", "αρ", "κλπ", "κτλ", "περ", "etc", "vs",
                                                          "mr", "mrs", "ms", "dr", "st", "no", "approx", "fig"};
    return std::find(std::begin(kAbbreviations), std::end(kAbbreviations), folded) != std::end(kAbbreviations);
}

bool boundary_at(std::string_view text, std::size_t index) {
    const char value = text[index];
    if (value != '.' && value != '!' && value != '?' && value != ';' && value != '\n') return false;
    // A decimal number is not a sentence (for example 21.5 °C).
    if (value == '.' && index > 0 && index + 1 < text.size() &&
        ascii_digit(text[index - 1]) && ascii_digit(text[index + 1])) {
        return false;
    }
    // Wait for the final dot of an ellipsis already present in this chunk.
    if (value == '.' && index + 1 < text.size() && text[index + 1] == '.') return false;
    if (value == '.' && abbreviation_dot(text, index)) return false;
    return true;
}

// Folded letters and digits only: two texts that differ just in spacing,
// punctuation or markup compare equal.
std::string letters_only(std::string_view text) {
    std::string letters;
    for (const auto& word : folded_words(fold_text(text))) letters += word;
    return letters;
}

}  // namespace

std::optional<std::string> first_complete_sentence(std::string_view text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (!boundary_at(text, index)) continue;
        std::size_t end = index + 1;
        while (end < text.size() && (text[end] == '.' || text[end] == '!' || text[end] == '?')) ++end;
        auto sentence = sanitize_text(text.substr(0, end), AssistantStreamMailbox::kMaxTextBytes, true);
        if (!sentence.empty() && sentence.rfind("ACTION:", 0) != 0) return sentence;
    }
    return std::nullopt;
}

std::string unspoken_remainder(std::string_view final_text, std::string_view spoken_prefix) {
    const auto spoken = letters_only(spoken_prefix);
    if (!spoken.empty()) {
        for (std::size_t index = 0; index < final_text.size(); ++index) {
            const char value = final_text[index];
            if (value != '.' && value != '!' && value != '?' && value != ';' && value != ':' && value != '\n') {
                continue;
            }
            std::size_t end = index + 1;
            while (end < final_text.size() && (final_text[end] == '.' || final_text[end] == '!' ||
                                               final_text[end] == '?' || final_text[end] == '*')) {
                ++end;
            }
            const auto said = letters_only(final_text.substr(0, end));
            if (said == spoken) {
                return sanitize_text(final_text.substr(end), AssistantStreamMailbox::kMaxTextBytes, true);
            }
            if (said.size() > spoken.size()) break;
        }
    }
    return sanitize_text(final_text, AssistantStreamMailbox::kMaxTextBytes, true);
}

void AssistantStreamMailbox::publish_delta(std::string_view delta) {
    if (delta.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto room = raw_bytes_ < kMaxRawBytes ? kMaxRawBytes - raw_bytes_ : 0;
    const auto accepted = std::min(room, delta.size());
    const bool truncated = delta.size() > room;
    raw_bytes_ += accepted;
    // Only the new bytes are cleaned, once; a letter whose bytes are split
    // across deltas waits for the rest of them.
    incomplete_.append(delta.data(), accepted);
    const auto complete = complete_utf8_prefix(incomplete_);
    clean_ += sanitize_fragment(std::string_view(incomplete_).substr(0, complete), true);
    incomplete_.erase(0, complete);
    std::string display;
    if (const auto first = clean_.find_first_not_of(" \n"); first != std::string::npos) {
        const auto last = clean_.find_last_not_of(" \n");
        display = utf8_truncate(std::string_view(clean_).substr(first, last - first + 1), kMaxTextBytes);
    }
    // The typed ACTION protocol is parsed and confirmed only from the final
    // reply. Never flash an incomplete command in the user-facing stream.
    if (const auto action = display.find("\nACTION:"); action != std::string::npos) display.resize(action);
    if (display.rfind("ACTION:", 0) == 0) display.clear();
    if (display == latest_.text && !truncated) return;
    latest_.text = std::move(display);
    if (latest_.first_sentence.empty()) {
        if (auto sentence = first_complete_sentence(latest_.text)) latest_.first_sentence = std::move(*sentence);
    }
    latest_.truncated = latest_.truncated || truncated || raw_bytes_ > kMaxTextBytes;
    ++latest_.sequence;
}

bool AssistantStreamMailbox::take_after(std::uint64_t after_sequence, AssistantStreamSnapshot& output) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (latest_.sequence <= after_sequence) return false;
    output = latest_;
    return true;
}

bool AssistantStreamDecoder::consume_line(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty()) return true;
    const auto object = parse_flat_json(line, kMaxLineBytes);
    if (!object) return false;
    const auto type = object->string("type");
    if (!type) return false;
    if (*type == "delta") {
        const auto text = object->string("text");
        if (!text || done_) return false;
        if (mailbox_) mailbox_->publish_delta(*text);
        return true;
    }
    if (*type == "done") {
        const auto text = object->string("text");
        if (!text || text->empty() || done_) return false;
        final_text_ = sanitize_text(*text, AssistantStreamMailbox::kMaxTextBytes, true);
        done_ = !final_text_.empty();
        return done_;
    }
    if (*type == "error") {
        const auto code = object->string("code");
        if (!code || code->empty() || code->size() > 80 || done_) return false;
        error_code_ = sanitize_text(*code, 80);
        done_ = !error_code_.empty();
        return done_;
    }
    return false;
}

bool AssistantStreamDecoder::push(std::string_view bytes) {
    if (done_) return false;
    // One read may carry many lines; only the line being assembled is bounded.
    while (!bytes.empty()) {
        const auto newline = bytes.find('\n');
        const auto piece = bytes.substr(0, newline);
        if (pending_.size() + piece.size() > kMaxLineBytes) return false;
        pending_.append(piece.data(), piece.size());
        if (newline == std::string_view::npos) return true;
        bytes.remove_prefix(newline + 1);
        const std::string line = std::move(pending_);
        pending_.clear();
        if (!consume_line(line)) return false;
        if (done_ && !bytes.empty()) return false;
    }
    return true;
}

bool AssistantStreamDecoder::finish() {
    if (!pending_.empty()) {
        const auto line = std::move(pending_);
        pending_.clear();
        if (!consume_line(line)) return false;
    }
    return done_;
}

}  // namespace aura

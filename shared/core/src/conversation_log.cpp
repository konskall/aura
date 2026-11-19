#include "aura/conversation_log.hpp"

#include "aura/text_util.hpp"

#include <charconv>
#include <utility>

namespace aura {
namespace {

constexpr std::string_view kHeader{"AURA_CONVERSATIONS_V1"};

void append_escaped(std::string& out, std::string_view text) {
    for (const char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
}

bool unescape(std::string_view text, std::string& out) {
    out.clear();
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\\') {
            out += text[index];
            continue;
        }
        if (++index == text.size()) return false;
        switch (text[index]) {
            case '\\': out += '\\'; break;
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            default: return false;
        }
    }
    return true;
}

}  // namespace

void ConversationLog::add(const std::string& question, const std::string& answer, std::int64_t minute) {
    ConversationTurn turn;
    turn.question = sanitize_text(question, kMaxQuestionBytes, true);
    turn.answer = sanitize_text(answer, kMaxAnswerBytes, true);
    turn.minute = minute;
    if (turn.question.empty() || turn.answer.empty()) return;
    turns_.push_back(std::move(turn));
    if (turns_.size() > kMaxTurns) turns_.erase(turns_.begin());
    ++sequence_;
}

void ConversationLog::clear() noexcept {
    if (turns_.empty()) return;
    turns_.clear();
    ++sequence_;
}

std::string ConversationLog::serialize() const {
    std::string out(kHeader);
    out += '\n';
    for (const auto& turn : turns_) {
        out += std::to_string(turn.minute);
        out += '\t';
        append_escaped(out, turn.question);
        out += '\t';
        append_escaped(out, turn.answer);
        out += '\n';
    }
    return out;
}

bool ConversationLog::restore(std::string_view data) {
    const auto first = data.find('\n');
    if (first == std::string_view::npos) return false;
    auto header = data.substr(0, first);
    if (!header.empty() && header.back() == '\r') header.remove_suffix(1);
    if (header != kHeader) return false;
    std::vector<ConversationTurn> restored;
    auto rest = data.substr(first + 1);
    while (!rest.empty()) {
        const auto end = rest.find('\n');
        auto line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        const auto tab = line.find('\t');
        const auto second = tab == std::string_view::npos ? tab : line.find('\t', tab + 1);
        if (second == std::string_view::npos) return false;
        ConversationTurn turn;
        const auto minute = line.substr(0, tab);
        const auto parsed = std::from_chars(minute.data(), minute.data() + minute.size(), turn.minute);
        if (parsed.ec != std::errc{} || parsed.ptr != minute.data() + minute.size()) return false;
        std::string question;
        std::string answer;
        if (!unescape(line.substr(tab + 1, second - tab - 1), question) ||
            !unescape(line.substr(second + 1), answer)) {
            return false;
        }
        turn.question = sanitize_text(question, kMaxQuestionBytes, true);
        turn.answer = sanitize_text(answer, kMaxAnswerBytes, true);
        if (turn.question.empty() || turn.answer.empty()) return false;
        restored.push_back(std::move(turn));
    }
    if (restored.size() > kMaxTurns) restored.erase(restored.begin(), restored.end() - kMaxTurns);
    turns_ = std::move(restored);
    ++sequence_;
    return true;
}

}  // namespace aura

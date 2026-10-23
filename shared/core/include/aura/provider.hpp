#pragma once

#include <future>
#include <string>
#include <vector>

namespace aura {

struct AssistantReply {
    bool ok{false};
    std::string text;
    std::string error_code;
};

// One earlier exchange, sent so follow-up questions ("and tomorrow?") work.
struct ConversationTurn {
    std::string question;
    std::string answer;
};

enum class TurnMode { Chat, Translate };

struct TurnRequest {
    std::string prompt;
    std::vector<ConversationTurn> history;  // oldest first
    std::string context;                    // device facts: data, never instructions
    TurnMode mode{TurnMode::Chat};
    std::string source_language;            // Translate: ISO 639-1 codes
    std::string target_language;
};

class IAssistantProvider {
public:
    virtual ~IAssistantProvider() = default;
    virtual std::future<AssistantReply> start_turn(TurnRequest request) = 0;
    virtual void cancel() = 0;
    virtual const char* name() const noexcept = 0;
};

}  // namespace aura

#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "aura/language.hpp"

namespace aura {

class AssistantStreamMailbox;

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
    InterfaceLanguage interface_language{InterfaceLanguage::Greek};
    // Explicit, locally detected user intent. Only providers with a real
    // search tool may accept it; ordinary chat remains offline-grounded.
    bool web_search{false};
    std::string source_language;            // Translate: ISO 639-1 codes
    std::string target_language;
    // Optional token stream. Providers publish from their worker; the owner
    // consumes it on the UI thread. The final future remains authoritative.
    std::shared_ptr<AssistantStreamMailbox> stream;
};

class IAssistantProvider {
public:
    virtual ~IAssistantProvider() = default;
    virtual std::future<AssistantReply> start_turn(TurnRequest request) = 0;
    virtual void cancel() = 0;
    virtual const char* name() const noexcept = 0;
};

}  // namespace aura

#pragma once

#include <string>

#include "aura/provider.hpp"

namespace aura {

// The text a model receives for one AURA turn, the same as the PC bridge's
// bridge/src/turn-prompt.mjs (tests/interop/run_turn_prompt_parity.mjs keeps
// them identical). The history, the device facts and the question are quoted
// as data, and the device parses and confirms any action itself. A web search
// asked for is answered as by a provider without a search tool (Groq).
// Empty when the turn cannot be asked: no question, or an unknown language.
std::string build_turn_instructions(const TurnRequest& request);

// The limits the bridge enforces on a turn (bytes of UTF-8).
inline constexpr std::size_t kTurnMaxPromptBytes = 4000;
inline constexpr std::size_t kTurnMaxContextBytes = 4000;
inline constexpr std::size_t kTurnMaxHistoryTurns = 4;
inline constexpr std::size_t kTurnMaxHistoryTextBytes = 1200;
inline constexpr std::size_t kTurnMaxInstructionsBytes = 16000;

}  // namespace aura

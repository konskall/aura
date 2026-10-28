#pragma once

#include "aura/assistant_companion.hpp"

#include <string>

namespace aura {

// Deterministic commands that never need an AI provider. Parsing is deliberately
// narrow: ambiguous text falls through to the assistant instead of guessing a
// side effect. Durable changes are returned as typed AssistantAction values and
// still use the normal on-device confirmation card.
enum class OfflineCommandKind {
    None,
    ShowTime,
    ShowBattery,
    OpenClock,
    OpenNotes,
    OpenCalendar,
    OpenTasks,
    OpenShopping,
    OpenRadio,
    OpenWeather,
    ConfirmAction,
};

struct OfflineCommand {
    OfflineCommandKind kind{OfflineCommandKind::None};
    AssistantAction action{};
    bool has_action{false};
};

OfflineCommand parse_offline_command(const std::string& text);

}  // namespace aura

#pragma once

#include "aura/assistant_companion.hpp"

#include <string>

namespace aura {

// Deterministic commands that never need an AI provider. Parsing is deliberately
// narrow: ambiguous text falls through to the assistant instead of guessing a
// side effect. Changes are returned as typed AssistantAction values: the simulator
// shows its confirmation card, and the device does what its owner said outright.
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
    OpenToday,
    OpenSettings,
    OpenMessages,
    OpenPhotos,
    OpenHome,
    OpenApps,
    OpenBattery,
    OpenWifi,
    OpenFiles,
    Restart,
    PowerOff,
    ConfirmAction,
};

struct OfflineCommand {
    OfflineCommandKind kind{OfflineCommandKind::None};
    AssistantAction action{};
    bool has_action{false};
};

OfflineCommand parse_offline_command(const std::string& text);

// The app (one of kAssistantApps) an opening command is for; null for the others.
const char* offline_command_app(OfflineCommandKind kind) noexcept;

}  // namespace aura

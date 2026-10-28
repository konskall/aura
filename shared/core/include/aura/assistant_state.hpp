#pragma once

#include <cstdint>
#include <string>

namespace aura {

enum class AssistantState {
    Idle,
    WakeDetected,
    Listening,
    Transcribing,
    Thinking,
    Responding,
    ActionConfirmation,
    Offline,
    Error,
    Sleep,
};

enum class EventType {
    Reset,
    WakeDetected,
    BeginRecording,
    EndRecording,
    ListeningExpired,
    TurnStarted,
    PartialResult,
    FinalResult,
    RequireConfirmation,
    ConfirmAction,
    DeclineAction,
    Cancel,
    ProviderUnavailable,
    Fail,
    Retry,
    Sleep,
    WakeScreen,
    WifiChanged,
    BatteryChanged,
    ChargingChanged,
    NotificationReceived,
    NotificationsCleared,
};

struct AssistantEvent {
    EventType type{EventType::Reset};
    std::string text;
    int value{0};
};

struct AssistantSnapshot {
    AssistantState state{AssistantState::Idle};
    std::string user_text;
    std::string answer_text;
    std::string partial_text;
    std::string error_code;
    bool wifi_connected{true};
    bool charging{false};
    int battery_percent{0};
    // Starts empty: a fresh device has no notifications. The simulator injects
    // its labelled DEMO notification explicitly.
    int notification_count{0};
    std::uint64_t sequence{0};
    std::uint64_t turn_generation{0};
};

class AssistantController {
public:
    bool dispatch(const AssistantEvent& event);
    const AssistantSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    AssistantSnapshot snapshot_{};
};

const char* state_name(AssistantState state) noexcept;
bool is_turn_active(AssistantState state) noexcept;

}  // namespace aura

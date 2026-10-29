#include "aura/assistant_state.hpp"

#include <algorithm>

namespace aura {

namespace {
bool can_start_turn(AssistantState state) {
    return state == AssistantState::Idle || state == AssistantState::Responding ||
           state == AssistantState::Offline || state == AssistantState::Error;
}
}

bool AssistantController::dispatch(const AssistantEvent& event) {
    bool accepted = true;
    switch (event.type) {
        case EventType::Reset:
            snapshot_.state = AssistantState::Idle;
            snapshot_.user_text.clear();
            snapshot_.answer_text.clear();
            snapshot_.partial_text.clear();
            snapshot_.error_code.clear();
            ++snapshot_.turn_generation;
            break;
        case EventType::WakeDetected:
            if (!can_start_turn(snapshot_.state)) { accepted = false; break; }
            snapshot_.state = AssistantState::WakeDetected;
            snapshot_.error_code.clear();
            break;
        case EventType::BeginRecording:
            if (snapshot_.state != AssistantState::WakeDetected && snapshot_.state != AssistantState::Idle) {
                accepted = false;
                break;
            }
            snapshot_.state = AssistantState::Listening;
            break;
        case EventType::EndRecording:
            if (snapshot_.state != AssistantState::Listening) { accepted = false; break; }
            snapshot_.state = AssistantState::Transcribing;
            break;
        case EventType::ListeningExpired:
            if (snapshot_.state != AssistantState::Listening) { accepted = false; break; }
            // An automatic follow-up window is allowed to close silently. The
            // completed answer remains visible instead of becoming an error.
            snapshot_.state = snapshot_.answer_text.empty() ? AssistantState::Idle : AssistantState::Responding;
            snapshot_.partial_text.clear();
            break;
        case EventType::TurnStarted:
            if (is_turn_active(snapshot_.state) && snapshot_.state != AssistantState::Transcribing) {
                accepted = false;
                break;
            }
            snapshot_.state = AssistantState::Thinking;
            snapshot_.user_text = event.text;
            snapshot_.answer_text.clear();
            snapshot_.partial_text.clear();
            snapshot_.error_code.clear();
            ++snapshot_.turn_generation;
            break;
        case EventType::PartialResult:
            if (snapshot_.state != AssistantState::Thinking && snapshot_.state != AssistantState::Responding) {
                accepted = false;
                break;
            }
            snapshot_.state = AssistantState::Responding;
            snapshot_.partial_text = event.text;
            break;
        case EventType::FinalResult:
            if (snapshot_.state != AssistantState::Thinking && snapshot_.state != AssistantState::Responding) {
                accepted = false;
                break;
            }
            snapshot_.state = AssistantState::Responding;
            snapshot_.answer_text = event.text;
            snapshot_.partial_text.clear();
            break;
        case EventType::RequireConfirmation:
            if (!is_turn_active(snapshot_.state)) { accepted = false; break; }
            snapshot_.state = AssistantState::ActionConfirmation;
            snapshot_.partial_text = event.text;
            break;
        case EventType::ConfirmAction:
        case EventType::DeclineAction:
            // Either way the answer stays on screen; only a confirmed action runs.
            if (snapshot_.state != AssistantState::ActionConfirmation) { accepted = false; break; }
            snapshot_.state = AssistantState::Responding;
            snapshot_.partial_text.clear();
            break;
        case EventType::Cancel:
            if (!is_turn_active(snapshot_.state)) { accepted = false; break; }
            snapshot_.state = AssistantState::Idle;
            snapshot_.partial_text.clear();
            ++snapshot_.turn_generation;
            break;
        case EventType::ProviderUnavailable:
            snapshot_.state = AssistantState::Offline;
            snapshot_.error_code = event.text.empty() ? "PROVIDER_UNAVAILABLE" : event.text;
            ++snapshot_.turn_generation;
            break;
        case EventType::Fail:
            snapshot_.state = AssistantState::Error;
            snapshot_.error_code = event.text.empty() ? "UNKNOWN" : event.text;
            ++snapshot_.turn_generation;
            break;
        case EventType::Retry:
            if (snapshot_.state != AssistantState::Offline && snapshot_.state != AssistantState::Error) {
                accepted = false;
                break;
            }
            snapshot_.state = AssistantState::Idle;
            snapshot_.error_code.clear();
            break;
        case EventType::Sleep:
            snapshot_.state = AssistantState::Sleep;
            ++snapshot_.turn_generation;
            break;
        case EventType::WakeScreen:
            if (snapshot_.state != AssistantState::Sleep) { accepted = false; break; }
            snapshot_.state = AssistantState::Idle;
            break;
        case EventType::WifiChanged:
            snapshot_.wifi_connected = event.value != 0;
            // A delivered final answer no longer depends on the network; only
            // in-flight work is interrupted by a connection loss.
            if (!snapshot_.wifi_connected && is_turn_active(snapshot_.state) &&
                !(snapshot_.state == AssistantState::Responding && snapshot_.partial_text.empty() &&
                  !snapshot_.answer_text.empty())) {
                snapshot_.state = AssistantState::Offline;
                snapshot_.error_code = "NETWORK_UNAVAILABLE";
                ++snapshot_.turn_generation;
            }
            break;
        case EventType::BatteryChanged:
            snapshot_.battery_percent = std::clamp(event.value, 0, 100);
            break;
        case EventType::ChargingChanged:
            snapshot_.charging = event.value != 0;
            break;
        case EventType::NotificationReceived:
            snapshot_.notification_count = std::min(snapshot_.notification_count + 1, 99);
            break;
        case EventType::NotificationsCleared:
            snapshot_.notification_count = 0;
            break;
    }
    if (accepted) ++snapshot_.sequence;
    return accepted;
}

const char* state_name(AssistantState state) noexcept {
    switch (state) {
        case AssistantState::Idle: return "IDLE";
        case AssistantState::WakeDetected: return "WAKE_DETECTED";
        case AssistantState::Listening: return "LISTENING";
        case AssistantState::Transcribing: return "TRANSCRIBING";
        case AssistantState::Thinking: return "THINKING";
        case AssistantState::Responding: return "RESPONDING";
        case AssistantState::ActionConfirmation: return "ACTION_CONFIRMATION";
        case AssistantState::Offline: return "OFFLINE";
        case AssistantState::Error: return "ERROR";
        case AssistantState::Sleep: return "SLEEP";
    }
    return "UNKNOWN";
}

bool holds_screen_awake(const AssistantSnapshot& snapshot) noexcept {
    switch (snapshot.state) {
        case AssistantState::WakeDetected:
        case AssistantState::Listening:
        case AssistantState::Transcribing:
        case AssistantState::Thinking: return true;
        case AssistantState::Responding: return !snapshot.partial_text.empty();
        default: return false;
    }
}

bool is_turn_active(AssistantState state) noexcept {
    return state == AssistantState::WakeDetected || state == AssistantState::Listening ||
           state == AssistantState::Transcribing || state == AssistantState::Thinking ||
           state == AssistantState::Responding || state == AssistantState::ActionConfirmation;
}

}  // namespace aura

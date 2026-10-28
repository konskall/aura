#include "aura/assistant_state.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace aura;
    AssistantController controller;
    expect(controller.snapshot().state == AssistantState::Idle, "starts idle");
    expect(controller.dispatch({EventType::WakeDetected}), "wake accepted");
    expect(controller.dispatch({EventType::BeginRecording}), "listen accepted");
    expect(controller.dispatch({EventType::EndRecording}), "transcribe accepted");
    expect(controller.dispatch({EventType::TurnStarted, "Πες μου για τον Ταΰγετο"}), "turn accepted");
    const auto generation = controller.snapshot().turn_generation;
    expect(controller.dispatch({EventType::PartialResult, "Ο Ταΰγετος"}), "partial accepted");
    expect(controller.dispatch({EventType::FinalResult, "Ο Ταΰγετος είναι οροσειρά."}), "final accepted");
    expect(controller.snapshot().answer_text == "Ο Ταΰγετος είναι οροσειρά.", "final stored");
    expect(controller.dispatch({EventType::Cancel}), "cancel responding turn");
    expect(controller.snapshot().turn_generation > generation, "cancel invalidates stale reply");
    expect(!controller.dispatch({EventType::EndRecording}), "invalid transition rejected");
    AssistantController quiet_controller;
    expect(quiet_controller.dispatch({EventType::WakeDetected}) &&
           quiet_controller.dispatch({EventType::BeginRecording}) &&
           quiet_controller.dispatch({EventType::ListeningExpired}) &&
           quiet_controller.snapshot().state == AssistantState::Idle,
           "an empty automatic listening window returns to idle");
    expect(controller.dispatch({EventType::BatteryChanged, {}, 140}), "battery event accepted");
    expect(controller.snapshot().battery_percent == 100, "battery bounded");
    expect(controller.dispatch({EventType::NotificationReceived, "DEMO"}), "notification accepted");
    expect(controller.snapshot().notification_count == 1, "notification increments count from an empty start");
    expect(controller.dispatch({EventType::NotificationsCleared}), "notifications clear accepted");
    expect(controller.snapshot().notification_count == 0, "notification count clears");
    expect(controller.dispatch({EventType::TurnStarted, "δοκιμή"}), "new turn accepted");
    expect(controller.dispatch({EventType::WifiChanged, {}, 0}), "wifi loss accepted");
    expect(controller.snapshot().state == AssistantState::Offline, "wifi loss interrupts active turn");
    expect(controller.dispatch({EventType::Retry}), "offline retry accepted");
    expect(controller.snapshot().state == AssistantState::Idle, "retry returns idle");
    expect(controller.dispatch({EventType::WifiChanged, {}, 1}), "wifi restore accepted");
    expect(controller.dispatch({EventType::TurnStarted, "δοκιμή"}), "answered turn starts");
    expect(controller.dispatch({EventType::FinalResult, "Τελική απάντηση."}), "answer delivered");
    const auto delivered_answer = controller.snapshot().answer_text;
    expect(controller.dispatch({EventType::WakeDetected}) && controller.dispatch({EventType::BeginRecording}) &&
           controller.dispatch({EventType::ListeningExpired}) &&
           controller.snapshot().state == AssistantState::Responding &&
           controller.snapshot().answer_text == delivered_answer,
           "a silent follow-up preserves the completed answer");
    expect(controller.dispatch({EventType::WifiChanged, {}, 0}), "late wifi loss accepted");
    expect(controller.snapshot().state == AssistantState::Responding &&
           controller.snapshot().answer_text == "Τελική απάντηση.",
           "wifi loss after a delivered answer keeps the answer");
    expect(controller.dispatch({EventType::RequireConfirmation, "timer"}) &&
           controller.snapshot().state == AssistantState::ActionConfirmation, "an action waits for the owner");
    expect(controller.dispatch({EventType::DeclineAction}) &&
           controller.snapshot().state == AssistantState::Responding &&
           controller.snapshot().answer_text == "Τελική απάντηση.", "declining keeps the answer");
    expect(!controller.dispatch({EventType::DeclineAction}), "nothing to decline twice");
    std::cout << "assistant_state_test: PASS\n";
    return 0;
}

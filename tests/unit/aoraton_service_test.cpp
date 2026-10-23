#include "aura/aoraton.hpp"

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
    MockAoratonService service;
    expect(service.snapshot().connection == AoratonConnectionState::Connected, "mock connects");
    expect(service.snapshot().rooms.size() == 2, "fictional room fixtures loaded");
    expect(service.select_room("demo-family"), "room selection");
    expect(service.snapshot().rooms.front().unread == 0, "opening clears unread");
    expect(service.inject_incoming("demo-lab", "Νέο δοκιμαστικό μήνυμα."), "incoming accepted");
    expect(service.snapshot().rooms[1].unread == 1, "background incoming increments unread");
    expect(service.prepare_send("demo-family", "Θα αργήσω δέκα λεπτά."), "send prepared");
    expect(service.snapshot().awaiting_confirmation, "confirmation is mandatory");
    expect(!service.prepare_send("demo-family", "duplicate"), "second pending action rejected");
    expect(service.confirm_send(), "confirmed send completes");
    expect(!service.confirm_send(), "same action cannot send twice");

    service.set_network_available(false);
    expect(service.prepare_send("demo-family", "Offline draft"), "offline draft can be prepared");
    expect(!service.confirm_send(), "offline send does not claim success");
    expect(service.cancel_send(), "failed pending action can be cancelled");
    service.set_network_available(true);
    service.tick(600);
    expect(service.snapshot().connection == AoratonConnectionState::Connected, "bounded reconnect succeeds");
    const auto message_count = service.snapshot().messages.size();
    service.reject_invalid_payload("INVALID_AUTHENTICATED_PAYLOAD");
    expect(service.snapshot().messages.size() == message_count, "invalid ciphertext is never displayed");
    const auto sequence_before_reset = service.snapshot().sequence;
    service.reset_mock();
    expect(!service.snapshot().awaiting_confirmation, "reset clears sensitive pending state");
    expect(service.snapshot().sequence > sequence_before_reset,
           "reset keeps the sequence increasing so the UI applies it");
    std::cout << "aoraton_service_test: PASS\n";
    return 0;
}

#include "aura/fail_closed_aoraton_transport.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    aura::AoratonMailbox mailbox;
    aura::simulator::FailClosedAoratonTransport transport(mailbox);

    aura::AoratonCommand pairing;
    pairing.type = aura::AoratonCommandType::BeginPairing;
    expect(mailbox.push_command(std::move(pairing)), "pairing command queued");
    expect(transport.poll() == 1, "pairing command consumed");
    aura::AoratonEvent event;
    expect(mailbox.pop_event(event) && event.type == aura::AoratonEventType::PairingFailed,
           "pairing fails closed without a backend");
    expect(event.error == "PAIRING_TEST_BACKEND_NOT_CONFIGURED", "pairing error is explicit");

    aura::AoratonCommand send;
    send.type = aura::AoratonCommandType::SendText;
    send.client_message_id = "synthetic-client-id";
    send.plaintext = "sensitive synthetic test text";
    expect(mailbox.push_command(std::move(send)), "send command queued");
    expect(transport.poll() == 1, "send command consumed");
    expect(mailbox.pop_event(event) && event.type == aura::AoratonEventType::SendFailed,
           "send cannot become a fake success");
    expect(event.client_message_id == "synthetic-client-id", "failure reconciles the stable client id");
    expect(event.error.find("sensitive") == std::string::npos, "plaintext is absent from failure metadata");

    std::cout << "fail_closed_aoraton_transport_test: PASS\n";
    return 0;
}

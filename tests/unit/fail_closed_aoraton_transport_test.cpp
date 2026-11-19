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

    aura::AoratonCommand unlock;
    unlock.type = aura::AoratonCommandType::UnlockVault;
    unlock.plaintext = "synthetic-passphrase";
    expect(mailbox.push_command(std::move(unlock)), "vault command queued");
    aura::AoratonCommand invite;
    invite.type = aura::AoratonCommandType::AddInviteKey;
    invite.plaintext = "synthetic-invite-key";
    expect(mailbox.push_command(std::move(invite)), "invite command queued");
    expect(transport.poll() == 2, "new key commands consumed");
    expect(mailbox.pop_event(event) && event.type == aura::AoratonEventType::Failure &&
               event.error == "AORATON_KEY_BACKEND_NOT_CONFIGURED",
           "vault unlock fails closed with an explicit error");
    expect(mailbox.pop_event(event) && event.type == aura::AoratonEventType::Failure &&
               event.error == "AORATON_KEY_BACKEND_NOT_CONFIGURED",
           "invite import fails closed with an explicit error");

    // The full check of 2026-11-19: four commands had no answer of their own and
    // went out as a Failure with no error, so a voice message stayed "sending".
    aura::AoratonCommand voice;
    voice.type = aura::AoratonCommandType::SendVoice;
    voice.client_message_id = "synthetic-voice-id";
    expect(mailbox.push_command(std::move(voice)), "voice command queued");
    aura::AoratonCommand attachment;
    attachment.type = aura::AoratonCommandType::OpenAttachment;
    attachment.message_id = "synthetic-message-id";
    expect(mailbox.push_command(std::move(attachment)), "attachment command queued");
    aura::AoratonCommand saving;
    saving.type = aura::AoratonCommandType::PowerSaving;
    saving.power_saving = true;
    expect(mailbox.push_command(std::move(saving)), "power saving queued");
    aura::AoratonCommand sync;
    sync.type = aura::AoratonCommandType::ConfigureSync;
    expect(mailbox.push_command(std::move(sync)), "sync settings queued");
    expect(transport.poll() == 4, "the four commands consumed");
    expect(mailbox.pop_event(event) && event.type == aura::AoratonEventType::SendFailed &&
               event.client_message_id == "synthetic-voice-id" && event.error == "AORATON_TEST_BACKEND_NOT_CONFIGURED",
           "a voice message fails as a sent text does");
    expect(mailbox.pop_event(event) && event.type == aura::AoratonEventType::AttachmentFailed &&
               event.message_id == "synthetic-message-id" && event.error == "AORATON_TEST_BACKEND_NOT_CONFIGURED",
           "an attachment fails for the message it belongs to");
    expect(!mailbox.pop_event(event), "power saving and sync settings need no answer");

    std::cout << "fail_closed_aoraton_transport_test: PASS\n";
    return 0;
}

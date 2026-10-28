#include "aura/fail_closed_aoraton_transport.hpp"

#include <algorithm>
#include <utility>

namespace aura::simulator {

std::size_t FailClosedAoratonTransport::poll(std::size_t maximum_commands) {
    const std::size_t bounded_maximum = std::min<std::size_t>(maximum_commands, 8);
    std::size_t handled = 0;
    AoratonCommand command;
    while (handled < bounded_maximum && mailbox_.pop_command(command)) {
        AoratonEvent event;
        bool publish = true;
        switch (command.type) {
            case AoratonCommandType::BeginPairing:
            case AoratonCommandType::ConfirmPairingSas:
                event.type = AoratonEventType::PairingFailed;
                event.error = "PAIRING_TEST_BACKEND_NOT_CONFIGURED";
                break;
            case AoratonCommandType::SendText:
                event.type = AoratonEventType::SendFailed;
                event.client_message_id = command.client_message_id;
                event.error = "AORATON_TEST_BACKEND_NOT_CONFIGURED";
                break;
            case AoratonCommandType::Connect:
            case AoratonCommandType::RefreshRooms:
            case AoratonCommandType::FetchRecentMessages:
            case AoratonCommandType::MarkRoomRead:
                event.type = AoratonEventType::Failure;
                event.error = "AORATON_TEST_BACKEND_NOT_CONFIGURED";
                break;
            case AoratonCommandType::UnlockVault:
            case AoratonCommandType::AddInviteKey:
                event.type = AoratonEventType::Failure;
                event.error = "AORATON_KEY_BACKEND_NOT_CONFIGURED";
                break;
            case AoratonCommandType::Disconnect:
                event.type = AoratonEventType::ConnectionChanged;
                event.connection = AoratonConnectionState::Disconnected;
                event.realtime_active = false;
                break;
            case AoratonCommandType::CancelPairing:
            case AoratonCommandType::Unpair:
                publish = false;
                break;
        }
        command.clear_plaintext();
        if (publish && !mailbox_.push_event(std::move(event))) break;
        command = {};
        ++handled;
    }
    return handled;
}

}  // namespace aura::simulator

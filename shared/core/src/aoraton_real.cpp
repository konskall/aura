#include "aura/aoraton_real.hpp"

#include "aura/aoraton_crypto.hpp"

#include <algorithm>
#include <utility>

namespace aura {
namespace {

constexpr std::size_t kMaxRooms = 64;
constexpr std::size_t kMaxMessages = 100;
constexpr std::size_t kMaxNotifications = 8;
constexpr std::size_t kMaxRoomIdBytes = 128;
constexpr std::size_t kMaxDisplayNameBytes = 160;

void clear_string(std::string& value) noexcept {
    volatile char* data = value.empty() ? nullptr : &value[0];
    for (std::size_t index = 0; index < value.size(); ++index) data[index] = 0;
    value.clear();
}

std::string bounded_utf8(std::string value, std::size_t maximum) {
    if (value.size() <= maximum) return value;
    value.resize(maximum);
    while (!value.empty() && !aoraton_is_valid_utf8(value)) value.pop_back();
    return value;
}

bool valid_identifier(const std::string& value) {
    return !value.empty() && value.size() <= kMaxRoomIdBytes && aoraton_is_valid_utf8(value);
}

// Local sends have no server timestamp until acknowledged. They stay newest, in
// insertion order, so an incoming row neither reorders nor evicts them.
bool message_order(const AoratonMessage& left, const AoratonMessage& right) {
    if (left.received_at.empty() || right.received_at.empty()) {
        return !left.received_at.empty() && right.received_at.empty();
    }
    if (left.received_at != right.received_at) return left.received_at < right.received_at;
    return left.id < right.id;
}

}  // namespace

AoratonCommand::~AoratonCommand() { clear_plaintext(); }

AoratonCommand::AoratonCommand(AoratonCommand&& other) noexcept
    : type(other.type),
      room_id(std::move(other.room_id)),
      client_message_id(std::move(other.client_message_id)),
      plaintext(std::move(other.plaintext)) {
    other.clear_plaintext();
}

AoratonCommand& AoratonCommand::operator=(AoratonCommand&& other) noexcept {
    if (this == &other) return *this;
    clear_plaintext();
    type = other.type;
    room_id = std::move(other.room_id);
    client_message_id = std::move(other.client_message_id);
    plaintext = std::move(other.plaintext);
    other.clear_plaintext();
    return *this;
}

void AoratonCommand::clear_plaintext() noexcept { clear_string(plaintext); }

bool AoratonMailbox::push_command(AoratonCommand command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (commands_.size() >= kMaxCommands) return false;
    commands_.push_back(std::move(command));
    return true;
}

bool AoratonMailbox::pop_command(AoratonCommand& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (commands_.empty()) return false;
    command = std::move(commands_.front());
    commands_.pop_front();
    return true;
}

bool AoratonMailbox::push_event(AoratonEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() >= kMaxEvents) return false;
    events_.push_back(std::move(event));
    return true;
}

bool AoratonMailbox::pop_event(AoratonEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return false;
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

std::size_t AoratonMailbox::command_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_.size();
}

std::size_t AoratonMailbox::event_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

void AoratonMailbox::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.clear();
    events_.clear();
}

RealAoratonService::RealAoratonService(AoratonMailbox& mailbox,
                                       AoratonRuntimeMode mode,
                                       ClientMessageIdSource client_message_id_source)
    : mailbox_(mailbox), client_message_id_source_(std::move(client_message_id_source)) {
    snapshot_.mock_mode = false;
    snapshot_.max_message_bytes = kAoratonMaxPlaintextBytes;
    snapshot_.runtime_mode = mode == AoratonRuntimeMode::Mock ? AoratonRuntimeMode::IntegrationTest : mode;
    snapshot_.pairing = AoratonPairingState::NotPaired;
}

RealAoratonService::~RealAoratonService() {
    clear_pending();
    mailbox_.clear();
}

void RealAoratonService::touch() { ++snapshot_.sequence; }

AoratonRoom* RealAoratonService::find_room(const std::string& room_id) {
    const auto found = std::find_if(snapshot_.rooms.begin(), snapshot_.rooms.end(),
                                    [&](const AoratonRoom& room) { return room.id == room_id; });
    return found == snapshot_.rooms.end() ? nullptr : &*found;
}

AoratonMessage* RealAoratonService::find_outgoing(const std::string& client_message_id) {
    const auto found = std::find_if(snapshot_.messages.begin(), snapshot_.messages.end(),
                                    [&](const AoratonMessage& message) {
                                        return message.client_message_id == client_message_id;
                                    });
    return found == snapshot_.messages.end() ? nullptr : &*found;
}

void RealAoratonService::clear_pending() noexcept {
    clear_string(snapshot_.pending_text);
    snapshot_.pending_room_id.clear();
    snapshot_.awaiting_confirmation = false;
}

void RealAoratonService::clear_session_data() noexcept {
    clear_pending();
    for (auto& message : snapshot_.messages) clear_string(message.local_display_text);
    snapshot_.messages.clear();
    snapshot_.rooms.clear();
    snapshot_.notifications.clear();
    snapshot_.selected_room_id.clear();
    snapshot_.account_label.clear();
    snapshot_.last_sync_at.clear();
    snapshot_.realtime_active = false;
    snapshot_.pairing_qr_payload.clear();
    snapshot_.pairing_code.clear();
    snapshot_.pairing_sas.clear();
    snapshot_.pairing_seconds_remaining = 0;
    snapshot_.key_state = AoratonKeyState::Unknown;
    snapshot_.key_count = 0;
    pairing_elapsed_ms_ = 0;
}

bool RealAoratonService::enqueue(AoratonCommand command) {
    if (mailbox_.push_command(std::move(command))) return true;
    snapshot_.last_error = "COMMAND_QUEUE_FULL";
    touch();
    return false;
}

bool RealAoratonService::connect() {
    if (snapshot_.pairing != AoratonPairingState::Paired) {
        snapshot_.last_error = "PAIRING_REQUIRED";
        touch();
        return false;
    }
    if (!network_available_) {
        snapshot_.connection = AoratonConnectionState::Disconnected;
        snapshot_.last_error = "NETWORK_UNAVAILABLE";
        touch();
        return false;
    }
    AoratonCommand command;
    command.type = AoratonCommandType::Connect;
    if (!enqueue(std::move(command))) return false;
    snapshot_.connection = AoratonConnectionState::Connecting;
    snapshot_.last_error.clear();
    touch();
    return true;
}

void RealAoratonService::disconnect() {
    AoratonCommand command;
    command.type = AoratonCommandType::Disconnect;
    enqueue(std::move(command));
    snapshot_.connection = AoratonConnectionState::Disconnected;
    snapshot_.realtime_active = false;
    snapshot_.last_error.clear();
    touch();
}

bool RealAoratonService::select_room(const std::string& room_id) {
    AoratonRoom* room = find_room(room_id);
    if (!room || !room->authorized || room->expired) {
        snapshot_.last_error = room && room->expired ? "ROOM_EXPIRED" : "ROOM_NOT_AUTHORIZED";
        touch();
        return false;
    }
    if (!room->key_available) {
        snapshot_.last_error = "ROOM_KEY_REQUIRED";
        touch();
        return false;
    }
    AoratonCommand fetch;
    fetch.type = AoratonCommandType::FetchRecentMessages;
    fetch.room_id = room_id;
    if (!enqueue(std::move(fetch))) return false;
    AoratonCommand read;
    read.type = AoratonCommandType::MarkRoomRead;
    read.room_id = room_id;
    if (!enqueue(std::move(read))) return false;
    snapshot_.selected_room_id = room_id;
    room->unread = 0;
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool RealAoratonService::inject_incoming(const std::string&, const std::string&) {
    snapshot_.last_error = "MOCK_INJECTION_DISABLED";
    touch();
    return false;
}

bool RealAoratonService::prepare_send(const std::string& room_id, const std::string& plaintext) {
    const AoratonRoom* room = find_room(room_id);
    if (snapshot_.awaiting_confirmation) {
        snapshot_.last_error = "CONFIRMATION_ALREADY_PENDING";
    } else if (snapshot_.pairing != AoratonPairingState::Paired) {
        snapshot_.last_error = "PAIRING_REQUIRED";
    } else if (!room || !room->authorized || room->expired) {
        snapshot_.last_error = room && room->expired ? "ROOM_EXPIRED" : "ROOM_NOT_AUTHORIZED";
    } else if (!room->key_available) {
        snapshot_.last_error = "ROOM_KEY_REQUIRED";
    } else if (plaintext.empty()) {
        snapshot_.last_error = "MESSAGE_EMPTY";
    } else if (plaintext.size() > kAoratonMaxPlaintextBytes) {
        snapshot_.last_error = "MESSAGE_TOO_LARGE";
    } else if (!aoraton_is_valid_utf8(plaintext)) {
        snapshot_.last_error = "MESSAGE_INVALID_UTF8";
    } else {
        snapshot_.pending_room_id = room_id;
        snapshot_.pending_text = plaintext;
        snapshot_.awaiting_confirmation = true;
        snapshot_.last_error.clear();
        touch();
        return true;
    }
    touch();
    return false;
}

bool RealAoratonService::confirm_send() {
    if (!snapshot_.awaiting_confirmation) {
        snapshot_.last_error = "NO_PENDING_MESSAGE";
        touch();
        return false;
    }
    if (!network_available_ || snapshot_.connection != AoratonConnectionState::Connected) {
        snapshot_.last_error = "NETWORK_UNAVAILABLE";
        touch();
        return false;
    }
    AoratonRoom* room = find_room(snapshot_.pending_room_id);
    if (!room || !room->authorized || !room->key_available || room->expired) {
        snapshot_.last_error = "ROOM_ACCESS_CHANGED";
        touch();
        return false;
    }
    const std::string client_id = client_message_id_source_ ? client_message_id_source_() : std::string{};
    if (!valid_identifier(client_id)) {
        snapshot_.last_error = "CLIENT_ID_SOURCE_UNAVAILABLE";
        touch();
        return false;
    }
    AoratonCommand command;
    command.type = AoratonCommandType::SendText;
    command.room_id = snapshot_.pending_room_id;
    command.client_message_id = client_id;
    command.plaintext = snapshot_.pending_text;
    if (!enqueue(std::move(command))) return false;

    AoratonMessage message;
    message.id = client_id;
    message.client_message_id = client_id;
    message.room_id = snapshot_.pending_room_id;
    message.author = snapshot_.account_label.empty() ? "AURA" : snapshot_.account_label;
    message.local_display_text = snapshot_.pending_text;
    message.direction = AoratonDirection::Outgoing;
    message.delivery = AoratonDelivery::Queued;
    snapshot_.messages.push_back(std::move(message));
    if (snapshot_.messages.size() > kMaxMessages) snapshot_.messages.erase(snapshot_.messages.begin());
    room->preview = bounded_utf8(snapshot_.pending_text, 96);
    snapshot_.selected_room_id = snapshot_.pending_room_id;
    clear_pending();
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool RealAoratonService::cancel_send() {
    if (!snapshot_.awaiting_confirmation) return false;
    clear_pending();
    snapshot_.last_error.clear();
    touch();
    return true;
}

void RealAoratonService::set_network_available(bool available) {
    network_available_ = available;
    if (!available) {
        snapshot_.connection = AoratonConnectionState::Disconnected;
        snapshot_.realtime_active = false;
        snapshot_.last_error = "NETWORK_UNAVAILABLE";
    } else if (snapshot_.pairing == AoratonPairingState::Paired) {
        snapshot_.connection = AoratonConnectionState::Reconnecting;
        snapshot_.last_error.clear();
        AoratonCommand command;
        command.type = AoratonCommandType::Connect;
        enqueue(std::move(command));
    }
    touch();
}

void RealAoratonService::reject_invalid_payload(const std::string&) {
    snapshot_.last_error = "INVALID_AUTHENTICATED_PAYLOAD";
    touch();
}

bool RealAoratonService::accept_message(const AoratonMessage& message) const {
    if (!valid_identifier(message.id) || !valid_identifier(message.room_id) ||
        message.local_display_text.size() > kAoratonMaxPlaintextBytes ||
        !aoraton_is_valid_utf8(message.local_display_text)) return false;
    const auto room = std::find_if(snapshot_.rooms.begin(), snapshot_.rooms.end(),
                                   [&](const AoratonRoom& candidate) {
                                       return candidate.id == message.room_id && candidate.authorized &&
                                              candidate.key_available && !candidate.expired;
                                   });
    return room != snapshot_.rooms.end();
}

bool RealAoratonService::message_exists(const AoratonMessage& message) const {
    return std::any_of(snapshot_.messages.begin(), snapshot_.messages.end(),
                       [&](const AoratonMessage& candidate) {
                           return candidate.id == message.id ||
                                  (!message.client_message_id.empty() &&
                                   candidate.client_message_id == message.client_message_id);
                       });
}

void RealAoratonService::add_message(AoratonMessage message, bool notify) {
    if (!accept_message(message) || message_exists(message)) return;
    AoratonRoom* room = find_room(message.room_id);
    if (!room) return;
    room->preview = bounded_utf8(message.local_display_text, 96);
    room->latest_timestamp = message.received_at;
    if (message.direction == AoratonDirection::Incoming && snapshot_.selected_room_id != message.room_id) {
        room->unread = std::min<std::uint32_t>(99, room->unread + 1);
    }
    if (notify && message.direction == AoratonDirection::Incoming) {
        AoratonNotification notification;
        notification.message_id = message.id;
        notification.room_id = message.room_id;
        notification.room_alias = room->display_name;
        notification.timestamp = message.received_at;
        if (snapshot_.notification_previews) notification.preview = bounded_utf8(message.local_display_text, 96);
        snapshot_.notifications.push_back(std::move(notification));
        if (snapshot_.notifications.size() > kMaxNotifications) snapshot_.notifications.erase(snapshot_.notifications.begin());
    }
    snapshot_.messages.push_back(std::move(message));
    sort_messages();
    if (snapshot_.messages.size() > kMaxMessages) snapshot_.messages.erase(snapshot_.messages.begin());
}

void RealAoratonService::sort_messages() {
    std::stable_sort(snapshot_.messages.begin(), snapshot_.messages.end(), message_order);
}

void RealAoratonService::process(AoratonEvent event) {
    switch (event.type) {
        case AoratonEventType::Paired:
            clear_session_data();
            snapshot_.pairing = AoratonPairingState::Paired;
            snapshot_.account_label = bounded_utf8(std::move(event.account_label), kMaxDisplayNameBytes);
            snapshot_.last_error.clear();
            break;
        case AoratonEventType::Unpaired:
            clear_session_data();
            snapshot_.pairing = AoratonPairingState::NotPaired;
            snapshot_.connection = AoratonConnectionState::Disconnected;
            snapshot_.last_error.clear();
            break;
        case AoratonEventType::ConnectionChanged:
            snapshot_.connection = event.connection;
            snapshot_.realtime_active = event.realtime_active;
            if (event.connection == AoratonConnectionState::Connected) {
                snapshot_.last_error.clear();
                AoratonCommand refresh;
                refresh.type = AoratonCommandType::RefreshRooms;
                enqueue(std::move(refresh));
            }
            break;
        case AoratonEventType::AuthorizedRooms:
            snapshot_.rooms.clear();
            for (auto& room : event.rooms) {
                if (!valid_identifier(room.id) || !room.authorized) continue;
                room.display_name = bounded_utf8(std::move(room.display_name), kMaxDisplayNameBytes);
                room.preview = room.key_available ? bounded_utf8(std::move(room.preview), 96) : std::string{};
                snapshot_.rooms.push_back(std::move(room));
                if (snapshot_.rooms.size() == kMaxRooms) break;
            }
            snapshot_.last_sync_at = event.timestamp;
            if (!snapshot_.selected_room_id.empty() && !find_room(snapshot_.selected_room_id)) {
                snapshot_.selected_room_id.clear();
            }
            break;
        case AoratonEventType::MessageHistory:
            for (auto& message : event.messages) add_message(std::move(message), false);
            snapshot_.last_sync_at = event.timestamp;
            break;
        case AoratonEventType::IncomingMessage:
            add_message(std::move(event.message), true);
            snapshot_.last_sync_at = event.timestamp;
            break;
        case AoratonEventType::SendSubmitted:
            if (auto* message = find_outgoing(event.client_message_id)) {
                message->delivery = AoratonDelivery::Submitted;
            }
            break;
        case AoratonEventType::SendAcknowledged:
            if (auto* message = find_outgoing(event.client_message_id)) {
                if (valid_identifier(event.server_message_id)) message->id = event.server_message_id;
                message->received_at = event.timestamp;
                message->delivery = AoratonDelivery::Acknowledged;
                sort_messages();
            }
            break;
        case AoratonEventType::SendFailed:
            if (auto* message = find_outgoing(event.client_message_id)) message->delivery = AoratonDelivery::Failed;
            snapshot_.last_error = event.error.empty() ? "SEND_FAILED" : bounded_utf8(std::move(event.error), 96);
            break;
        case AoratonEventType::SendUncertain:
            if (auto* message = find_outgoing(event.client_message_id)) message->delivery = AoratonDelivery::Uncertain;
            snapshot_.last_error = "SEND_STATUS_UNCERTAIN";
            break;
        case AoratonEventType::RoomExpired:
            if (auto* room = find_room(event.room_id)) {
                room->expired = true;
                room->key_available = false;
                room->preview.clear();
            }
            if (snapshot_.selected_room_id == event.room_id) snapshot_.last_error = "ROOM_EXPIRED";
            break;
        case AoratonEventType::SessionRevoked:
            clear_session_data();
            snapshot_.pairing = AoratonPairingState::ReauthorizationRequired;
            snapshot_.connection = AoratonConnectionState::Disconnected;
            snapshot_.last_error = "SESSION_REVOKED";
            break;
        case AoratonEventType::PairingChallenge:
            snapshot_.pairing = AoratonPairingState::WaitingForBrowser;
            snapshot_.pairing_qr_payload = bounded_utf8(std::move(event.pairing_qr_payload), 1024);
            snapshot_.pairing_code = bounded_utf8(std::move(event.pairing_code), 24);
            snapshot_.pairing_sas.clear();
            snapshot_.pairing_seconds_remaining = std::min<std::uint32_t>(event.pairing_seconds_remaining, 600);
            pairing_elapsed_ms_ = 0;
            snapshot_.last_error.clear();
            break;
        case AoratonEventType::PairingAwaitingSas:
            snapshot_.pairing = AoratonPairingState::AwaitingSasConfirmation;
            snapshot_.pairing_sas = bounded_utf8(std::move(event.pairing_sas), 24);
            snapshot_.pairing_seconds_remaining = std::min<std::uint32_t>(event.pairing_seconds_remaining, 600);
            pairing_elapsed_ms_ = 0;
            snapshot_.last_error.clear();
            break;
        case AoratonEventType::PairingExpired:
            snapshot_.pairing = AoratonPairingState::Expired;
            snapshot_.pairing_qr_payload.clear();
            snapshot_.pairing_code.clear();
            snapshot_.pairing_sas.clear();
            snapshot_.pairing_seconds_remaining = 0;
            snapshot_.last_error = "PAIRING_EXPIRED";
            break;
        case AoratonEventType::PairingFailed:
            snapshot_.pairing = AoratonPairingState::Error;
            snapshot_.pairing_qr_payload.clear();
            snapshot_.pairing_sas.clear();
            snapshot_.pairing_seconds_remaining = 0;
            snapshot_.last_error = event.error.empty() ? "PAIRING_FAILED" : bounded_utf8(std::move(event.error), 96);
            break;
        case AoratonEventType::KeysChanged:
            snapshot_.key_state = event.key_state;
            snapshot_.key_count = std::min<std::uint32_t>(event.key_count, static_cast<std::uint32_t>(kMaxRooms * 8));
            break;
        case AoratonEventType::Failure:
            snapshot_.last_error = event.error.empty() ? "AORATON_FAILURE" : bounded_utf8(std::move(event.error), 96);
            break;
    }
    touch();
}

void RealAoratonService::tick(std::uint32_t elapsed_ms) {
    AoratonEvent event;
    std::size_t handled = 0;
    while (handled < 8 && mailbox_.pop_event(event)) {
        process(std::move(event));
        event = {};
        ++handled;
    }
    if ((snapshot_.pairing == AoratonPairingState::WaitingForBrowser ||
         snapshot_.pairing == AoratonPairingState::AwaitingSasConfirmation) &&
        snapshot_.pairing_seconds_remaining > 0) {
        pairing_elapsed_ms_ += elapsed_ms;
        if (pairing_elapsed_ms_ >= 1000) {
            const std::uint32_t seconds = pairing_elapsed_ms_ / 1000;
            pairing_elapsed_ms_ %= 1000;
            snapshot_.pairing_seconds_remaining = seconds >= snapshot_.pairing_seconds_remaining
                ? 0 : snapshot_.pairing_seconds_remaining - seconds;
            if (snapshot_.pairing_seconds_remaining == 0) {
                snapshot_.pairing = AoratonPairingState::Expired;
                snapshot_.pairing_qr_payload.clear();
                snapshot_.pairing_code.clear();
                snapshot_.pairing_sas.clear();
                snapshot_.last_error = "PAIRING_EXPIRED";
            }
            touch();
        }
    }
}

void RealAoratonService::reset_mock() {
    snapshot_.last_error = "MOCK_RESET_DISABLED";
    touch();
}

bool RealAoratonService::begin_pairing() {
    if (!network_available_) {
        snapshot_.last_error = "NETWORK_UNAVAILABLE";
        touch();
        return false;
    }
    if (snapshot_.pairing == AoratonPairingState::WaitingForBrowser ||
        snapshot_.pairing == AoratonPairingState::AwaitingSasConfirmation) {
        snapshot_.last_error = "PAIRING_ALREADY_ACTIVE";
        touch();
        return false;
    }
    AoratonCommand command;
    command.type = AoratonCommandType::BeginPairing;
    if (!enqueue(std::move(command))) return false;
    snapshot_.pairing = AoratonPairingState::WaitingForBrowser;
    snapshot_.pairing_qr_payload.clear();
    snapshot_.pairing_code.clear();
    snapshot_.pairing_sas.clear();
    snapshot_.pairing_seconds_remaining = 0;
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool RealAoratonService::confirm_pairing_sas() {
    if (snapshot_.pairing != AoratonPairingState::AwaitingSasConfirmation || snapshot_.pairing_sas.empty()) {
        snapshot_.last_error = "PAIRING_SAS_NOT_READY";
        touch();
        return false;
    }
    AoratonCommand command;
    command.type = AoratonCommandType::ConfirmPairingSas;
    if (!enqueue(std::move(command))) return false;
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool RealAoratonService::cancel_pairing() {
    if (snapshot_.pairing != AoratonPairingState::WaitingForBrowser &&
        snapshot_.pairing != AoratonPairingState::AwaitingSasConfirmation &&
        snapshot_.pairing != AoratonPairingState::Expired &&
        snapshot_.pairing != AoratonPairingState::Error) return false;
    AoratonCommand command;
    command.type = AoratonCommandType::CancelPairing;
    if (!enqueue(std::move(command))) return false;
    snapshot_.pairing = AoratonPairingState::NotPaired;
    snapshot_.pairing_qr_payload.clear();
    snapshot_.pairing_code.clear();
    snapshot_.pairing_sas.clear();
    snapshot_.pairing_seconds_remaining = 0;
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool RealAoratonService::enqueue_secret(AoratonCommandType type, const std::string& secret) {
    if (snapshot_.pairing != AoratonPairingState::Paired) {
        snapshot_.last_error = "PAIRING_REQUIRED";
        touch();
        return false;
    }
    if (secret.empty() || secret.size() > 4096) {
        snapshot_.last_error = type == AoratonCommandType::UnlockVault ? "PASSPHRASE_EMPTY" : "INVITE_INVALID";
        touch();
        return false;
    }
    if (snapshot_.key_state == AoratonKeyState::Unlocking) {
        snapshot_.last_error = "KEYS_BUSY";
        touch();
        return false;
    }
    AoratonCommand command;
    command.type = type;
    command.plaintext = secret;
    if (!enqueue(std::move(command))) return false;
    snapshot_.key_state = AoratonKeyState::Unlocking;
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool RealAoratonService::unlock_keys(const std::string& passphrase) {
    return enqueue_secret(AoratonCommandType::UnlockVault, passphrase);
}

bool RealAoratonService::add_invite_key(const std::string& invite) {
    return enqueue_secret(AoratonCommandType::AddInviteKey, invite);
}

bool RealAoratonService::unpair() {
    if (snapshot_.pairing != AoratonPairingState::Paired &&
        snapshot_.pairing != AoratonPairingState::ReauthorizationRequired) return false;
    AoratonCommand command;
    command.type = AoratonCommandType::Unpair;
    if (!enqueue(std::move(command))) return false;
    clear_session_data();
    snapshot_.pairing = AoratonPairingState::NotPaired;
    snapshot_.connection = AoratonConnectionState::Disconnected;
    snapshot_.last_error.clear();
    touch();
    return true;
}

}  // namespace aura

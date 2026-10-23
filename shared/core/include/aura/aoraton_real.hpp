#pragma once

#include "aura/aoraton.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace aura {

enum class AoratonCommandType {
    Connect,
    Disconnect,
    RefreshRooms,
    FetchRecentMessages,
    MarkRoomRead,
    SendText,
    BeginPairing,
    ConfirmPairingSas,
    CancelPairing,
    Unpair,
};

class AoratonCommand final {
public:
    AoratonCommand() = default;
    ~AoratonCommand();
    AoratonCommand(const AoratonCommand&) = delete;
    AoratonCommand& operator=(const AoratonCommand&) = delete;
    AoratonCommand(AoratonCommand&& other) noexcept;
    AoratonCommand& operator=(AoratonCommand&& other) noexcept;

    AoratonCommandType type{AoratonCommandType::Connect};
    std::string room_id;
    std::string client_message_id;
    std::string plaintext;

    void clear_plaintext() noexcept;
};

enum class AoratonEventType {
    Paired,
    Unpaired,
    ConnectionChanged,
    AuthorizedRooms,
    MessageHistory,
    IncomingMessage,
    SendSubmitted,
    SendAcknowledged,
    SendFailed,
    SendUncertain,
    RoomExpired,
    SessionRevoked,
    PairingChallenge,
    PairingAwaitingSas,
    PairingExpired,
    PairingFailed,
    Failure,
};

struct AoratonEvent {
    AoratonEventType type{AoratonEventType::Failure};
    AoratonConnectionState connection{AoratonConnectionState::Disconnected};
    std::vector<AoratonRoom> rooms;
    std::vector<AoratonMessage> messages;
    AoratonMessage message;
    std::string room_id;
    std::string client_message_id;
    std::string server_message_id;
    std::string timestamp;
    std::string account_label;
    std::string error;
    bool realtime_active{false};
    std::string pairing_qr_payload;
    std::string pairing_code;
    std::string pairing_sas;
    std::uint32_t pairing_seconds_remaining{0};
};

class AoratonMailbox final {
public:
    static constexpr std::size_t kMaxCommands = 16;
    static constexpr std::size_t kMaxEvents = 64;

    bool push_command(AoratonCommand command);
    bool pop_command(AoratonCommand& command);
    bool push_event(AoratonEvent event);
    bool pop_event(AoratonEvent& event);
    std::size_t command_count() const;
    std::size_t event_count() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::deque<AoratonCommand> commands_;
    std::deque<AoratonEvent> events_;
};

class RealAoratonService final : public IAoratonService {
public:
    using ClientMessageIdSource = std::function<std::string()>;

    RealAoratonService(AoratonMailbox& mailbox,
                       AoratonRuntimeMode mode,
                       ClientMessageIdSource client_message_id_source);
    ~RealAoratonService() override;

    bool connect() override;
    void disconnect() override;
    bool select_room(const std::string& room_id) override;
    bool inject_incoming(const std::string& room_id, const std::string& fictional_text) override;
    bool prepare_send(const std::string& room_id, const std::string& plaintext) override;
    bool confirm_send() override;
    bool cancel_send() override;
    void set_network_available(bool available) override;
    void reject_invalid_payload(const std::string& reason) override;
    void tick(std::uint32_t elapsed_ms) override;
    void reset_mock() override;
    bool begin_pairing() override;
    bool confirm_pairing_sas() override;
    bool cancel_pairing() override;
    bool unpair() override;
    const AoratonSnapshot& snapshot() const noexcept override { return snapshot_; }

private:
    AoratonRoom* find_room(const std::string& room_id);
    AoratonMessage* find_outgoing(const std::string& client_message_id);
    void process(AoratonEvent event);
    void touch();
    void clear_pending() noexcept;
    void clear_session_data() noexcept;
    bool enqueue(AoratonCommand command);
    bool accept_message(const AoratonMessage& message) const;
    bool message_exists(const AoratonMessage& message) const;
    void add_message(AoratonMessage message, bool notify);
    void sort_messages();

    AoratonMailbox& mailbox_;
    ClientMessageIdSource client_message_id_source_;
    AoratonSnapshot snapshot_{};
    bool network_available_{true};
    std::uint32_t pairing_elapsed_ms_{0};
};

}  // namespace aura

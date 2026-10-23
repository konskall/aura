#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {

enum class AoratonConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error,
};

enum class AoratonDirection { Incoming, Outgoing, System };
enum class AoratonDelivery {
    Received,
    Prepared,
    Queued,
    Sending,
    Submitted,
    Acknowledged,
    Sent,
    Failed,
    Uncertain,
};
enum class AoratonRuntimeMode { Mock, IntegrationTest, Live };
enum class AoratonPairingState {
    NotPaired,
    WaitingForBrowser,
    AwaitingSasConfirmation,
    Paired,
    Expired,
    ReauthorizationRequired,
    Error,
};

struct AoratonRoom {
    std::string id;
    std::string display_name;
    std::string preview;
    std::uint32_t unread{0};
    bool encrypted{true};
    bool authorized{true};
    bool key_available{true};
    bool expired{false};
    std::string latest_timestamp;
};

struct AoratonMessage {
    std::string id;
    std::string room_id;
    std::string author;
    std::string local_display_text;
    std::string received_at;
    AoratonDirection direction{AoratonDirection::Incoming};
    AoratonDelivery delivery{AoratonDelivery::Received};
    std::string client_message_id;
};

struct AoratonNotification {
    std::string message_id;
    std::string room_id;
    std::string room_alias;
    std::string timestamp;
    std::string preview;
};

struct AoratonSnapshot {
    AoratonConnectionState connection{AoratonConnectionState::Disconnected};
    std::vector<AoratonRoom> rooms;
    std::vector<AoratonMessage> messages;
    std::string selected_room_id;
    std::string pending_room_id;
    std::string pending_text;
    std::string last_error;
    bool awaiting_confirmation{false};
    bool mock_mode{true};
    std::uint64_t sequence{0};
    AoratonRuntimeMode runtime_mode{AoratonRuntimeMode::Mock};
    AoratonPairingState pairing{AoratonPairingState::NotPaired};
    std::string account_label;
    std::string last_sync_at;
    std::vector<AoratonNotification> notifications;
    bool notification_previews{false};
    bool realtime_active{false};
    std::string pairing_qr_payload;
    std::string pairing_code;
    std::string pairing_sas;
    std::uint32_t pairing_seconds_remaining{0};
    std::size_t max_message_bytes{512};
};

class IAoratonService {
public:
    virtual ~IAoratonService() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool select_room(const std::string& room_id) = 0;
    virtual bool inject_incoming(const std::string& room_id, const std::string& fictional_text) = 0;
    virtual bool prepare_send(const std::string& room_id, const std::string& plaintext) = 0;
    virtual bool confirm_send() = 0;
    virtual bool cancel_send() = 0;
    virtual void set_network_available(bool available) = 0;
    virtual void reject_invalid_payload(const std::string& reason) = 0;
    virtual void tick(std::uint32_t elapsed_ms) = 0;
    virtual void reset_mock() = 0;
    virtual bool begin_pairing() { return false; }
    virtual bool confirm_pairing_sas() { return false; }
    virtual bool cancel_pairing() { return false; }
    virtual bool unpair() { return false; }
    virtual const AoratonSnapshot& snapshot() const noexcept = 0;
};

class MockAoratonService final : public IAoratonService {
public:
    MockAoratonService();
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
    const AoratonSnapshot& snapshot() const noexcept override { return snapshot_; }

private:
    AoratonRoom* find_room(const std::string& room_id);
    void touch();
    void update_preview(AoratonRoom& room, const std::string& text);

    AoratonSnapshot snapshot_{};
    bool network_available_{true};
    std::uint32_t reconnect_elapsed_ms_{0};
    std::uint32_t next_id_{1};
};

const char* aoraton_connection_name(AoratonConnectionState state) noexcept;

}  // namespace aura

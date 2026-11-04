#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

struct AoratonRealtimeUpdate {
    bool active{false};            // all requested room subscriptions acknowledged
    bool refresh_required{false};  // coalesced invalidation, never message text
};

// Called only by the account worker. Platform adapters own TLS/WebSocket I/O;
// tokens never enter the UI mailbox. configure/stop/poll must not block on I/O.
class IAoratonRealtime {
public:
    virtual ~IAoratonRealtime() = default;
    virtual void configure(const std::string& base_url, const std::string& anon_key, const std::string& access_token,
                           const std::vector<std::string>& rooms) = 0;
    virtual AoratonRealtimeUpdate poll() = 0;
    virtual void stop() = 0;
};

// Supabase/Phoenix v1 JSON framing, implemented from the public protocol.
// Receives only subscription status and INSERT invalidations. REST remains
// authoritative for membership, ciphertext, history, unread counts and dedupe.
class AoratonRealtimeProtocol {
public:
    static constexpr std::size_t kMaxRooms = 64;
    static constexpr std::size_t kMaxFrameBytes = 65536;
    explicit AoratonRealtimeProtocol(std::vector<std::string> rooms);
    static bool valid_room(std::string_view room);
    std::vector<std::string> joins(const std::string& access_token) const;
    std::string heartbeat(const std::string& ref) const;
    bool receive(std::string_view frame);  // false: malformed/fatal channel error
    bool active() const;
    bool take_refresh();
    const std::string& heartbeat_reply() const { return heartbeat_reply_; }

private:
    std::vector<std::string> rooms_;
    std::vector<bool> joined_;
    bool refresh_{false};
    std::string heartbeat_reply_;
};

}  // namespace aura

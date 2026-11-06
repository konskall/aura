#pragma once

#include "aura/aoraton.hpp"
#include "aura/aoraton_account_worker.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

// The project's public values, the same ones aoraton.com ships to every
// browser (config/aoraton.local.json on the PC), and the owner's inco opt-in.
struct AoratonLinkConfig {
    std::string base_url;
    std::string anon_key;
    bool inco_enabled{false};
};

// An https origin (no path or query) and a JWT-shaped key, bounded.
bool aoraton_link_config_valid(const AoratonLinkConfig& config);
// The authorization code the browser returned: a bounded token (no spaces).
bool aoraton_link_code_valid(std::string_view code);

// The device's encrypted-NVS record of the account: a version tag, the refresh
// token (at most 4 KiB) and at most 64 room keys (names of at most 128 bytes).
// Strict and bounded: a damaged record is refused whole, never half-read.
constexpr std::size_t kAoratonMaxStoredKeys = 64;
bool aoraton_encode_secrets(const AoratonStoredSecrets& secrets, std::vector<std::uint8_t>& record);
bool aoraton_decode_secrets(const std::vector<std::uint8_t>& record, AoratonStoredSecrets& secrets);

enum class AoratonLinkState { NotConfigured, SignedOut, Starting, Waiting, SignedIn, Failed };

// What the PC helper may learn about the device's account: never a token.
struct AoratonLinkStatus {
    AoratonLinkState state{AoratonLinkState::NotConfigured};
    std::string authorize_url;  // only while Waiting; it carries no secret
    std::string error;          // an error code, only when Failed
};

// The calls the file-transfer server makes for POST /api/aoraton/*. Each
// returns at once; the server already checked the signed-in session.
class IAoratonLinkEndpoint {
public:
    virtual ~IAoratonLinkEndpoint() = default;
    virtual bool configure(const AoratonLinkConfig& config) = 0;
    virtual bool request_sign_in() = 0;
    virtual AoratonLinkStatus status() const = 0;
    // `cancelled` reports the browser's refusal; then `code` is empty.
    virtual bool deliver(const std::string& code, bool cancelled) = 0;
    virtual bool add_invite(const std::string& invite) = 0;
};

// The device cannot show Google's sign-in page, so the owner's PC relays it
// (ADR 0010): scripts/aoraton-device-sign-in.mjs reads the authorize URL over
// the signed-in file-transfer session, opens it in the PC browser, receives
// the code on 127.0.0.1 and hands it back. The PKCE verifier, the session and
// the room keys never leave the device. Thread-safe: the transfer server, the
// UI thread and the account worker all call it.
class AoratonDeviceLink final : public IAoratonLinkEndpoint, public IAoratonSignInChannel {
public:
    static constexpr std::size_t kMaxInvites = 4;
    using ConfigSink = std::function<bool(const AoratonLinkConfig&)>;

    explicit AoratonDeviceLink(ConfigSink config_sink);
    ~AoratonDeviceLink() override;
    AoratonDeviceLink(const AoratonDeviceLink&) = delete;
    AoratonDeviceLink& operator=(const AoratonDeviceLink&) = delete;

    // IAoratonLinkEndpoint (the file-transfer server's thread).
    bool configure(const AoratonLinkConfig& config) override;
    bool request_sign_in() override;
    AoratonLinkStatus status() const override;
    bool deliver(const std::string& code, bool cancelled) override;
    bool add_invite(const std::string& invite) override;

    // The UI thread: take the PC's requests and report the account's state
    // after each snapshot. The caller wipes a taken invite.
    bool take_sign_in_request();
    bool take_invite(std::string& invite);
    void publish(bool configured, AoratonPairingState pairing, const std::string& error);

    // IAoratonSignInChannel (the account worker's thread).
    bool open(const std::string& authorize_url) override;
    AoratonCallbackStatus poll(std::string& code) override;
    void close() override;

private:
    enum class Answer { None, Code, Refused };
    void clear_answer_locked() noexcept;

    ConfigSink config_sink_;
    mutable std::mutex mutex_;
    bool configured_{false};
    bool sign_in_requested_{false};
    bool open_{false};
    std::string authorize_url_;
    Answer answer_{Answer::None};
    std::string code_;
    AoratonPairingState pairing_{AoratonPairingState::NotPaired};
    std::string error_;
    std::deque<std::string> invites_;
};

}  // namespace aura

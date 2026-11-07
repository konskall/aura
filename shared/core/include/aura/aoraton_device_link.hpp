#pragma once

#include "aura/aoraton.hpp"
#include "aura/aoraton_account_worker.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

struct TransferSnapshot;

// The project's public values, the same ones aoraton.com ships to every
// browser (config/aoraton.local.json on the PC), and the owner's inco opt-in.
struct AoratonLinkConfig {
    std::string base_url;
    std::string anon_key;
    bool inco_enabled{false};
};

// An https origin (no path or query) and a JWT-shaped key, bounded.
// `allow_loopback` also takes http://127.0.0.1:<port>: only a test's local
// fake, when the simulator runs with AURA_AORATON_ALLOW_LOOPBACK=1. The device
// never asks for it.
bool aoraton_link_config_valid(const AoratonLinkConfig& config, bool allow_loopback = false);
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

// What GET /aoraton/phone (the QR's address) shows the owner's phone:
// Google's page once the device's sign-in has it, a short wait before that,
// nothing for a stale or wrong link.
enum class AoratonPhonePage { Gone, Wait, Google };
// What became of the answer Google's page sent back to the device.
enum class AoratonPhoneAnswer { Gone, Taken, Refused };

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
    // The phone's way (no PC): GET /aoraton/phone?s=<secret> and
    // GET /aoraton/callback?s=<secret>&code=... (or &error=...). There is no
    // PIN on these: the secret in the QR stands in for it.
    virtual AoratonPhonePage phone_page(std::string_view secret, std::string& authorize_url) {
        (void)secret;
        authorize_url.clear();
        return AoratonPhonePage::Gone;
    }
    virtual AoratonPhoneAnswer phone_answer(std::string_view secret, const std::string& code, bool refused) {
        (void)secret;
        (void)code;
        (void)refused;
        return AoratonPhoneAnswer::Gone;
    }
};

// The device cannot show Google's sign-in page, so the owner's PC or phone
// does (ADR 0010). The PC: scripts/aoraton-device-sign-in.mjs reads the
// authorize URL over the signed-in file-transfer session, opens it in the PC
// browser, receives the code on 127.0.0.1 and hands it back. The phone: it
// scans the device's QR, the device's transfer server sends it on to Google's
// page, and Google's page returns it, with the code, to that server over the
// owner's Wi-Fi. The PKCE verifier, the session and the room keys never leave
// the device. Thread-safe: the transfer server, the UI thread and the account
// worker all call it.
class AoratonDeviceLink final : public IAoratonLinkEndpoint, public IAoratonSignInChannel {
public:
    static constexpr std::size_t kMaxInvites = 4;
    // Wrong secrets a phone sign-in survives before it is closed.
    static constexpr unsigned kMaxPhoneMisses = 8;
    using ConfigSink = std::function<bool(const AoratonLinkConfig&)>;

    explicit AoratonDeviceLink(ConfigSink config_sink, bool allow_loopback = false);
    ~AoratonDeviceLink() override;
    AoratonDeviceLink(const AoratonDeviceLink&) = delete;
    AoratonDeviceLink& operator=(const AoratonDeviceLink&) = delete;

    // IAoratonLinkEndpoint (the file-transfer server's thread).
    bool configure(const AoratonLinkConfig& config) override;
    bool request_sign_in() override;
    AoratonLinkStatus status() const override;
    bool deliver(const std::string& code, bool cancelled) override;
    bool add_invite(const std::string& invite) override;
    AoratonPhonePage phone_page(std::string_view secret, std::string& authorize_url) override;
    AoratonPhoneAnswer phone_answer(std::string_view secret, const std::string& code, bool refused) override;

    // The phone's way (the UI thread). `origin` is the device's transfer
    // server, http://<private IPv4>:<port>; `random` makes the QR's secret.
    // The QR shows phone_url(); Google's page returns to
    // <origin>/aoraton/callback?s=<secret>. A new request replaces the last
    // one and the PC's request_sign_in() ends it. False: not configured, or
    // not such an address.
    bool request_phone_sign_in(const std::string& origin, const std::array<std::uint8_t, 16>& random);
    std::string phone_url() const;  // empty unless a phone sign-in is on
    // The owner stopped (File transfer closed): the account is told at once.
    void cancel_phone_sign_in();

    // The UI thread: take the PC's requests and report the account's state
    // after each snapshot. The caller wipes a taken invite.
    bool take_sign_in_request();
    bool take_invite(std::string& invite);
    void publish(bool configured, AoratonPairingState pairing, const std::string& error);

    // IAoratonSignInChannel (the account worker's thread).
    bool open(const std::string& authorize_url) override;
    AoratonCallbackStatus poll(std::string& code) override;
    void close() override;
    std::string redirect_url() const override;

private:
    enum class Answer { None, Code, Refused };
    void clear_answer_locked() noexcept;
    bool phone_secret_matches_locked(std::string_view secret);
    void stop_phone_locked() noexcept;
    void end_phone_locked() noexcept;

    ConfigSink config_sink_;
    bool allow_loopback_{false};
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
    // A phone sign-in: the transfer server's origin and the QR's secret (both
    // empty when there is none); `phone_open_` once the account opened it.
    std::string phone_origin_;
    std::string phone_secret_;
    bool phone_open_{false};
    unsigned phone_misses_{0};
    // A phone sign-in stopped after the account took the request but before it
    // opened: that opening is refused at once.
    bool refuse_next_open_{false};
};

// The UI thread's share, once per frame, on the device and in the simulator's
// device mode. `configured`: the platform holds the PC's settings (it says so
// as soon as it stores them). `account_ready`: its account runs. A sign-in or
// an invite the PC sent while the account (re)starts waits for it; the
// account's state then goes back to the PC.
void aoraton_link_pump(AoratonDeviceLink& link, IAoratonService& service, bool configured, bool account_ready);

// The phone's way, on the UI thread after each File transfer snapshot. Once
// the owner asked (`wanted`: the Aoraton screen's sign-in button) and the
// transfer server is ready, starts the phone sign-in with a secret from
// `random` (false: no randomness, so no sign-in), once; when the server
// stops, ends it. Returns what the QR shows:
// phone_url(), empty when there is no phone sign-in (an unconfigured device,
// a server only the PC reaches, the PC's own sign-in, the account done).
std::string aoraton_phone_pump(AoratonDeviceLink& link, const TransferSnapshot& transfer, bool& wanted,
                               const std::function<bool(std::array<std::uint8_t, 16>&)>& random);

}  // namespace aura

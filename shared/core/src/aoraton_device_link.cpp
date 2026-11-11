#include "aura/aoraton_device_link.hpp"

#include "aura/aoraton_account.hpp"
#include "aura/file_transfer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>
#include <utility>

namespace aura {
namespace {

constexpr std::size_t kMaxUrlChars = 200;
constexpr std::size_t kMinKeyChars = 40;
constexpr std::size_t kMaxKeyChars = 2048;
constexpr std::size_t kMaxAuthorizeUrlChars = 1024;
constexpr std::size_t kMinCodeChars = 8;
constexpr std::size_t kMaxCodeChars = 512;
constexpr std::size_t kMaxErrorChars = 40;

void wipe(std::string& value) noexcept {
    volatile char* data = value.empty() ? nullptr : &value[0];
    for (std::size_t index = 0; index < value.size(); ++index) data[index] = 0;
    value.clear();
}

bool host_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

bool base64url_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

bool valid_origin(std::string_view url) {
    constexpr std::string_view kScheme = "https://";
    if (url.size() > kMaxUrlChars || url.substr(0, kScheme.size()) != kScheme) return false;
    std::string_view host = url.substr(kScheme.size());
    if (!host.empty() && host.back() == '/') host.remove_suffix(1);
    const auto colon = host.find(':');
    std::string_view port;
    if (colon != std::string_view::npos) {
        port = host.substr(colon + 1);
        host = host.substr(0, colon);
        if (port.empty() || port.size() > 5) return false;
        for (char c : port) {
            if (c < '0' || c > '9') return false;
        }
    }
    if (host.empty() || host.front() == '.' || host.front() == '-' || host.find('.') == std::string_view::npos)
        return false;
    for (char c : host) {
        if (!host_char(c)) return false;
    }
    return true;
}

// header.payload.signature, each base64url, as Supabase's anon key is.
bool jwt_shaped(std::string_view key) {
    if (key.size() < kMinKeyChars || key.size() > kMaxKeyChars) return false;
    std::size_t dots = 0;
    std::size_t part = 0;
    for (char c : key) {
        if (c == '.') {
            if (part == 0) return false;
            ++dots;
            part = 0;
        } else if (base64url_char(c)) {
            ++part;
        } else {
            return false;
        }
    }
    return dots == 2 && part > 0;
}

bool code_shaped(std::string_view code) {
    if (code.size() < kMinCodeChars || code.size() > kMaxCodeChars) return false;
    for (char c : code) {
        if (!base64url_char(c) && c != '.') return false;
    }
    return true;
}

// The QR's secret: 16 random bytes as 22 base64url characters.
std::string phone_secret(const std::array<std::uint8_t, 16>& random) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const auto byte : random) {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(kAlphabet[(buffer >> bits) & 0x3F]);
        }
    }
    if (bits > 0) out.push_back(kAlphabet[(buffer << (6 - bits)) & 0x3F]);
    return out;
}

// Constant time over the secret's (public, fixed) length.
bool same_secret(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (std::size_t index = 0; index < a.size(); ++index)
        difference |= static_cast<unsigned char>(a[index] ^ b[index]);
    return difference == 0;
}

// The device's transfer server: http://<private IPv4>:<port>, nothing more.
// 127.0.0.1 only for a test's link.
bool phone_origin(std::string_view origin, bool allow_loopback) {
    if (!transfer_address_valid(origin)) return false;
    return allow_loopback || origin.substr(0, 17) != "http://127.0.0.1:";
}

bool error_code_shaped(std::string_view error) {
    if (error.empty() || error.size() > kMaxErrorChars) return false;
    for (char c : error) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

}  // namespace

// http://127.0.0.1:<port>, nothing more: a test's local fake.
bool loopback_origin(std::string_view url) {
    constexpr std::string_view kPrefix = "http://127.0.0.1:";
    if (url.substr(0, kPrefix.size()) != kPrefix) return false;
    std::string_view port = url.substr(kPrefix.size());
    if (!port.empty() && port.back() == '/') port.remove_suffix(1);
    if (port.empty() || port.size() > 5) return false;
    for (char c : port) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

bool aoraton_link_config_valid(const AoratonLinkConfig& config, bool allow_loopback) {
    return (valid_origin(config.base_url) || (allow_loopback && loopback_origin(config.base_url))) &&
           jwt_shaped(config.anon_key);
}

bool aoraton_link_code_valid(std::string_view code) { return code_shaped(code); }

namespace {
constexpr std::uint8_t kRecordMagic[4] = {'A', 'A', 'S', '1'};
constexpr std::size_t kMaxTokenBytes = 4096;
constexpr std::size_t kMaxRoomKeyBytes = 128;

void put16(std::vector<std::uint8_t>& out, std::size_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}
}  // namespace

bool aoraton_encode_secrets(const AoratonStoredSecrets& secrets, std::vector<std::uint8_t>& record) {
    record.clear();
    if (secrets.refresh_token.size() > kMaxTokenBytes || secrets.keys.size() > kAoratonMaxStoredKeys) return false;
    for (const auto& key : secrets.keys) {
        if (key.room_key.empty() || key.room_key.size() > kMaxRoomKeyBytes) return false;
    }
    record.assign(std::begin(kRecordMagic), std::end(kRecordMagic));
    put16(record, secrets.refresh_token.size());
    record.insert(record.end(), secrets.refresh_token.begin(), secrets.refresh_token.end());
    put16(record, secrets.keys.size());
    for (const auto& key : secrets.keys) {
        record.push_back(static_cast<std::uint8_t>(key.room_key.size()));
        record.insert(record.end(), key.room_key.begin(), key.room_key.end());
        record.insert(record.end(), key.rk.begin(), key.rk.end());
    }
    return true;
}

bool aoraton_decode_secrets(const std::vector<std::uint8_t>& record, AoratonStoredSecrets& secrets) {
    secrets.clear();
    std::size_t at = 0;
    const auto take16 = [&](std::size_t& value) {
        if (record.size() - at < 2) return false;
        value = (static_cast<std::size_t>(record[at]) << 8) | record[at + 1];
        at += 2;
        return true;
    };
    if (record.size() < 8 || !std::equal(std::begin(kRecordMagic), std::end(kRecordMagic), record.begin()))
        return false;
    at = 4;
    AoratonStoredSecrets decoded;
    std::size_t token_bytes = 0;
    if (!take16(token_bytes) || token_bytes > kMaxTokenBytes || record.size() - at < token_bytes) return false;
    decoded.refresh_token.assign(record.begin() + static_cast<std::ptrdiff_t>(at),
                                 record.begin() + static_cast<std::ptrdiff_t>(at + token_bytes));
    at += token_bytes;
    std::size_t count = 0;
    if (!take16(count) || count > kAoratonMaxStoredKeys) return false;
    for (std::size_t index = 0; index < count; ++index) {
        if (record.size() - at < 1) return false;
        const std::size_t name_bytes = record[at++];
        if (name_bytes == 0 || name_bytes > kMaxRoomKeyBytes || record.size() - at < name_bytes + 32) return false;
        AoratonUnlockedKey key;
        key.room_key.assign(record.begin() + static_cast<std::ptrdiff_t>(at),
                            record.begin() + static_cast<std::ptrdiff_t>(at + name_bytes));
        at += name_bytes;
        std::copy(record.begin() + static_cast<std::ptrdiff_t>(at),
                  record.begin() + static_cast<std::ptrdiff_t>(at + 32), key.rk.begin());
        at += 32;
        decoded.keys.push_back(std::move(key));
    }
    if (at != record.size()) return false;
    secrets = std::move(decoded);
    return true;
}

AoratonDeviceLink::AoratonDeviceLink(ConfigSink config_sink, bool allow_loopback)
    : config_sink_(std::move(config_sink)), allow_loopback_(allow_loopback) {}

AoratonDeviceLink::~AoratonDeviceLink() {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_answer_locked();
    for (auto& invite : invites_) wipe(invite);
}

void AoratonDeviceLink::clear_answer_locked() noexcept {
    answer_ = Answer::None;
    wipe(code_);
}

bool AoratonDeviceLink::configure(const AoratonLinkConfig& config) {
    if (!aoraton_link_config_valid(config, allow_loopback_) || !config_sink_) return false;
    if (!config_sink_(config)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    configured_ = true;
    return true;
}

bool AoratonDeviceLink::request_sign_in() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!configured_) return false;
    end_phone_locked();  // the PC's sign-in replaces a phone's
    refuse_next_open_ = false;
    sign_in_requested_ = true;
    return true;
}

AoratonLinkStatus AoratonDeviceLink::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    AoratonLinkStatus status;
    if (!configured_) return status;
    if (open_) {
        status.state = AoratonLinkState::Waiting;
        if (!phone_open_) status.authorize_url = authorize_url_;  // never a phone's
        return status;
    }
    if (sign_in_requested_) {
        status.state = AoratonLinkState::Starting;
        return status;
    }
    switch (pairing_) {
        case AoratonPairingState::Paired: status.state = AoratonLinkState::SignedIn; break;
        case AoratonPairingState::WaitingForBrowser:
        case AoratonPairingState::AwaitingSasConfirmation: status.state = AoratonLinkState::Starting; break;
        case AoratonPairingState::Expired:
        case AoratonPairingState::Error:
            status.state = AoratonLinkState::Failed;
            status.error = error_;
            break;
        case AoratonPairingState::NotPaired:
        case AoratonPairingState::ReauthorizationRequired: status.state = AoratonLinkState::SignedOut; break;
    }
    return status;
}

bool AoratonDeviceLink::deliver(const std::string& code, bool cancelled) {
    if (!cancelled && !code_shaped(code)) return false;
    if (cancelled && !code.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_ || answer_ != Answer::None) return false;
    answer_ = cancelled ? Answer::Refused : Answer::Code;
    if (!cancelled) code_ = code;
    return true;
}

bool AoratonDeviceLink::add_invite(const std::string& invite) {
    AoratonInvite parsed;
    const bool valid = aoraton_parse_invite(invite, parsed);
    parsed.clear();
    if (!valid) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (invites_.size() >= kMaxInvites) return false;
    invites_.push_back(invite);
    return true;
}

bool AoratonDeviceLink::take_sign_in_request() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool requested = sign_in_requested_;
    sign_in_requested_ = false;
    return requested;
}

bool AoratonDeviceLink::take_invite(std::string& invite) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (invites_.empty()) return false;
    invite = std::move(invites_.front());
    invites_.pop_front();
    return true;
}

void AoratonDeviceLink::publish(bool configured, AoratonPairingState pairing, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    configured_ = configured;
    pairing_ = pairing;
    error_ = error_code_shaped(error) ? error : std::string{};
}

bool AoratonDeviceLink::open(const std::string& authorize_url) {
    if (authorize_url.empty() || authorize_url.size() > kMaxAuthorizeUrlChars ||
        (authorize_url.rfind("https://", 0) != 0 &&
         !(allow_loopback_ && authorize_url.rfind("http://127.0.0.1:", 0) == 0)))
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    clear_answer_locked();
    authorize_url_ = authorize_url;
    open_ = true;
    sign_in_requested_ = false;
    phone_open_ = !phone_secret_.empty();
    if (refuse_next_open_) {
        refuse_next_open_ = false;
        answer_ = Answer::Refused;
    }
    return true;
}

AoratonCallbackStatus AoratonDeviceLink::poll(std::string& code) {
    std::lock_guard<std::mutex> lock(mutex_);
    code.clear();
    if (!open_ || answer_ == Answer::None) return AoratonCallbackStatus::Pending;
    const Answer answer = answer_;
    if (answer == Answer::Code) code = std::move(code_);
    clear_answer_locked();
    open_ = false;
    authorize_url_.clear();
    if (phone_open_) end_phone_locked();
    return answer == Answer::Code ? AoratonCallbackStatus::Received : AoratonCallbackStatus::Failed;
}

void AoratonDeviceLink::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_answer_locked();
    open_ = false;
    authorize_url_.clear();
    // A phone sign-in the account gave up on (its timeout, or a restart). A
    // new tap's secret, not yet opened, stays for the restart's opening.
    if (phone_open_) end_phone_locked();
}

// Supabase returns a phone to no IP address but 127.0.0.1 (its referrer check,
// RFC 9700, since 2026-06-01), so Google's page returns to a static page on
// aoraton.com (public/aura-phone/ in its repository), which forwards the code
// to `d`, the device on the local network, at /aoraton/callback.
std::string AoratonDeviceLink::redirect_url() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (phone_secret_.empty()) return {};
    return std::string(kPhoneRelay) + "?d=" + phone_origin_.substr(7) + "&s=" + phone_secret_;
}

bool AoratonDeviceLink::request_phone_sign_in(const std::string& origin, const std::array<std::uint8_t, 16>& random) {
    if (!phone_origin(origin, allow_loopback_)) return false;
    std::string secret = phone_secret(random);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!configured_) {
        wipe(secret);
        return false;
    }
    end_phone_locked();
    phone_origin_ = origin;
    phone_secret_ = std::move(secret);
    refuse_next_open_ = false;
    sign_in_requested_ = true;
    return true;
}

std::string AoratonDeviceLink::phone_url() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return phone_secret_.empty() ? std::string{} : phone_origin_ + "/aoraton/phone?s=" + phone_secret_;
}

void AoratonDeviceLink::cancel_phone_sign_in() {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_phone_locked();
}

AoratonPhonePage AoratonDeviceLink::phone_page(std::string_view secret, std::string& authorize_url) {
    authorize_url.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!phone_secret_matches_locked(secret)) return AoratonPhonePage::Gone;
    if (!phone_open_) return AoratonPhonePage::Wait;
    if (!open_ || answer_ != Answer::None) return AoratonPhonePage::Gone;
    authorize_url = authorize_url_;
    return AoratonPhonePage::Google;
}

AoratonPhoneAnswer AoratonDeviceLink::phone_answer(std::string_view secret, const std::string& code, bool refused) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!phone_secret_matches_locked(secret) || !phone_open_ || !open_ || answer_ != Answer::None)
        return AoratonPhoneAnswer::Gone;
    if (!refused && code_shaped(code)) {
        answer_ = Answer::Code;
        code_ = code;
        return AoratonPhoneAnswer::Taken;
    }
    answer_ = Answer::Refused;  // declined on the phone, or not a code
    return AoratonPhoneAnswer::Refused;
}

// A wrong secret counts; kMaxPhoneMisses of them close the phone sign-in.
bool AoratonDeviceLink::phone_secret_matches_locked(std::string_view secret) {
    if (phone_secret_.empty()) return false;
    if (same_secret(secret, phone_secret_)) return true;
    if (++phone_misses_ >= kMaxPhoneMisses) stop_phone_locked();
    return false;
}

// Ends a phone sign-in so that the account hears of it: refused if it is open,
// never started if only asked for, refused at its opening if the account has
// already taken the request.
void AoratonDeviceLink::stop_phone_locked() noexcept {
    if (phone_secret_.empty()) return;
    if (phone_open_) {
        if (open_ && answer_ == Answer::None) answer_ = Answer::Refused;
    } else if (sign_in_requested_) {
        sign_in_requested_ = false;
    } else {
        refuse_next_open_ = true;
    }
    end_phone_locked();
}

void AoratonDeviceLink::end_phone_locked() noexcept {
    wipe(phone_secret_);
    phone_origin_.clear();
    phone_open_ = false;
    phone_misses_ = 0;
}

void aoraton_link_pump(AoratonDeviceLink& link, IAoratonService& service, bool configured, bool account_ready) {
    if (account_ready) {
        if (link.take_sign_in_request()) service.begin_pairing();
        std::string invite;
        while (link.take_invite(invite)) {
            service.add_invite_key(invite);
            wipe(invite);
        }
    }
    const auto& snapshot = service.snapshot();
    link.publish(configured, snapshot.pairing, snapshot.last_error);
}

std::string aoraton_phone_pump(AoratonDeviceLink& link, const TransferSnapshot& transfer, bool& wanted,
                               const std::function<bool(std::array<std::uint8_t, 16>&)>& random) {
    const bool running = transfer.state == TransferState::Ready || transfer.state == TransferState::Receiving;
    if (running && wanted) {
        wanted = false;
        std::array<std::uint8_t, 16> secret{};
        if (random(secret)) link.request_phone_sign_in(transfer.address, secret);
        volatile std::uint8_t* bytes = secret.data();
        for (std::size_t i = 0; i < secret.size(); ++i)
            bytes[i] = 0;
    } else if (!running && transfer.state != TransferState::Starting) {
        wanted = false;
        link.cancel_phone_sign_in();  // nothing to do when there is none
    }
    return link.phone_url();
}

}  // namespace aura

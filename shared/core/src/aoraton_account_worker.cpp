#include "aura/aoraton_account_worker.hpp"

#include "aura/json_reader.hpp"
#include "aura/photos.hpp"
#include "aura/text_util.hpp"

#include <algorithm>
#include <cstdio>

namespace aura {
namespace {

constexpr std::size_t kMaxRooms = 64;
constexpr std::size_t kMaxSeenIds = 120;
constexpr std::size_t kMaxAttachments = 100;
constexpr std::size_t kThumbnailsPerRoom = 6;
constexpr unsigned kThumbnailEdge = 216;
constexpr std::uint32_t kAttachmentTimeoutMs = 30000;

AoratonAttachmentKind attachment_kind(const std::string& type) {
    if (type.rfind("image/", 0) == 0) return AoratonAttachmentKind::Image;
    if (type.rfind("audio/", 0) == 0) return AoratonAttachmentKind::Voice;
    return AoratonAttachmentKind::File;
}
constexpr std::size_t kMaxPendingSends = 16;
constexpr std::size_t kMaxNotifiedPerPoll = 8;
constexpr std::size_t kCommandsPerStep = 8;
// The edge function retries Gemini up to three times, 20 s each.
constexpr std::uint32_t kIncoTimeoutMs = 75000;
constexpr std::size_t kIncoQuoteChars = 100;

void wipe(std::string& value) noexcept {
    volatile char* data = value.empty() ? nullptr : &value[0];
    for (std::size_t index = 0; index < value.size(); ++index) data[index] = 0;
    value.clear();
}

bool ok_status(int status) { return status >= 200 && status < 300; }

std::string trim_base(const std::string& base) {
    std::string value = base;
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

// Server timestamps are UTC ISO-8601; the first 19 characters order correctly.
std::string iso_prefix(const std::string& value) { return value.substr(0, std::min<std::size_t>(19, value.size())); }

bool newer(const std::string& candidate, const std::string& reference) {
    if (candidate.empty()) return false;
    if (reference.empty()) return true;
    return candidate > reference;
}

void remember(std::vector<std::string>& ids, const std::string& id) {
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) return;
    ids.push_back(id);
    if (ids.size() > kMaxSeenIds) ids.erase(ids.begin());
}

bool contains(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Wipes a plaintext copy on every way out of a scope.
struct WipeOnExit {
    std::string& value;
    ~WipeOnExit() { wipe(value); }
};

// The edge function's documented failures, for the log line.
std::string inco_failure(int status) {
    switch (status) {
        case 0: return "NETWORK";
        case 400: return "BAD_REQUEST (400)";
        case 401: return "AUTH_REQUIRED (401)";
        case 403: return "NOT_A_MEMBER (403)";
        case 429: return "RATE_LIMITED (429)";
        case 500: return "SERVER_ERROR (500)";
        case 502: return "AI_REQUEST_FAILED (502)";
        case 503: return "AI_NOT_CONFIGURED (503)";
        default: return "HTTP " + std::to_string(status);
    }
}

}  // namespace

AoratonStoredSecrets::~AoratonStoredSecrets() { clear(); }

void AoratonStoredSecrets::clear() noexcept {
    wipe(refresh_token);
    for (auto& key : keys) key.clear();
    keys.clear();
}

AoratonAccountWorker::AoratonAccountWorker(AoratonMailbox& mailbox,
                                           AoratonAccountConfig config,
                                           IAoratonHttp& http,
                                           IAoratonAccountCrypto& account_crypto,
                                           IAoratonCryptoV2& message_crypto,
                                           IAoratonSecretStore& store,
                                           IAoratonSignInChannel& channel,
                                           IAoratonRealtime* realtime,
                                           IVoiceDecoder* voice_decoder,
                                           IAoratonAttachmentCache* attachment_cache)
    : mailbox_(mailbox),
      config_(std::move(config)),
      http_(http),
      account_crypto_(account_crypto),
      message_crypto_(message_crypto),
      store_(store),
      channel_(channel), realtime_(realtime), voice_decoder_(voice_decoder), attachment_cache_(attachment_cache) {
    config_.base_url = trim_base(config_.base_url);
}

AoratonAccountWorker::~AoratonAccountWorker() {
    if (realtime_) realtime_->stop();
    if (signing_in_) channel_.close();
    wipe(verifier_);
}

void AoratonAccountWorker::emit(AoratonEvent event) { mailbox_.push_event(std::move(event)); }

std::string AoratonAccountWorker::now_iso() const {
    // Civil date from Unix days (H. Hinnant's algorithm), UTC.
    const std::int64_t seconds = now_unix_ < 0 ? 0 : now_unix_;
    std::int64_t days = seconds / 86400;
    const std::int64_t rest = seconds % 86400;
    days += 719468;
    const std::int64_t era = days / 146097;
    const std::int64_t doe = days - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t year = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
    if (month <= 2) ++year;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d", static_cast<int>(year),
                  static_cast<int>(month), static_cast<int>(day), static_cast<int>(rest / 3600),
                  static_cast<int>((rest % 3600) / 60), static_cast<int>(rest % 60));
    return buffer;
}

void AoratonAccountWorker::start(std::int64_t now_ms, std::int64_t now_unix) {
    now_ms_ = now_ms;
    now_unix_ = now_unix;
    AoratonStoredSecrets loaded;
    if (store_.load(loaded)) secrets_ = std::move(loaded);
    if (secrets_.refresh_token.empty()) return;
    session_.refresh_token = secrets_.refresh_token;
    switch (refresh_session()) {
        case RefreshResult::Ok: {
            AoratonSession session = std::move(session_);
            finish_sign_in(std::move(session));
            break;
        }
        case RefreshResult::Revoked: session_revoked(); break;
        case RefreshResult::Transient: next_retry_ms_ = now_ms_ + kRetryIntervalMs; break;
    }
}

void AoratonAccountWorker::step(std::int64_t now_ms, std::int64_t now_unix) {
    now_ms_ = now_ms;
    now_unix_ = now_unix;
    AoratonCommand command;
    for (std::size_t handled = 0; handled < kCommandsPerStep && mailbox_.pop_command(command); ++handled) {
        handle(command);
        command = {};
    }
    if (signing_in_) poll_sign_in();
    if (!signed_in_ && !signing_in_ && !secrets_.refresh_token.empty() && next_retry_ms_ != 0 &&
        now_ms_ >= next_retry_ms_) {
        next_retry_ms_ = 0;
        start(now_ms, now_unix);
        return;
    }
    bool realtime_refresh = false;
    // Token maintenance is independent of the optional overview poll. Turning
    // polling off must never let a quiet realtime session expire an hour later.
    if (signed_in_ && connected_ && session_.expires_at_unix - now_unix_ < 60 && now_ms_ >= next_auth_retry_ms_) {
        const auto refreshed = refresh_session();
        if (refreshed == RefreshResult::Revoked) { session_revoked(); return; }
        next_auth_retry_ms_ = now_ms_ + kRetryIntervalMs;
        if (refreshed == RefreshResult::Ok) configure_realtime();
        else { connection_lost(); next_poll_ms_ = now_ms_ + kRetryIntervalMs; }
    }
    if (signed_in_ && connected_ && realtime_ && !power_saving_) {
        const auto update = realtime_->poll();
        realtime_status(update.active && !reconnecting_);
        realtime_refresh = update.refresh_required;
    }
    if (signed_in_ && connected_ && !thumbnail_queue_.empty()) load_next_thumbnail();
    const bool periodic_due = (sync_settings_.polling_enabled || power_saving_) && now_ms_ >= next_poll_ms_;
    const bool recovery_due = reconnecting_ && now_ms_ >= next_poll_ms_;
    if (signed_in_ && connected_ && (sync_requested_ || periodic_due || recovery_due || realtime_refresh)) {
        sync_requested_ = false;
        if (refresh_rooms()) {
            if (reconnecting_) {
                reconnecting_ = false;
                AoratonEvent event;
                event.type = AoratonEventType::ConnectionChanged;
                event.connection = AoratonConnectionState::Connected;
                emit(std::move(event));
            }
        }
        next_poll_ms_ = now_ms_ + (reconnecting_ && !sync_settings_.polling_enabled && !power_saving_ ?
                                  kRetryIntervalMs : poll_interval_ms());
    }
}

void AoratonAccountWorker::handle(AoratonCommand& command) {
    switch (command.type) {
        case AoratonCommandType::BeginPairing: begin_sign_in(); break;
        case AoratonCommandType::CancelPairing:
            if (signing_in_) channel_.close();
            signing_in_ = false;
            wipe(verifier_);
            break;
        case AoratonCommandType::ConfirmPairingSas: break;
        case AoratonCommandType::Unpair: sign_out(); break;
        case AoratonCommandType::Connect: connect(); break;
        case AoratonCommandType::Disconnect:
            connected_ = false;
            if (realtime_) realtime_->stop();
            realtime_active_ = false;
            break;
        case AoratonCommandType::RefreshRooms:
            if (signed_in_) {
                refresh_rooms();
                sync_requested_ = false;
                next_poll_ms_ = now_ms_ + poll_interval_ms();
            }
            break;
        case AoratonCommandType::FetchRecentMessages:
            for (const auto& id : command.message_ids) {
                thumbnails_fetched_.erase(std::remove(thumbnails_fetched_.begin(), thumbnails_fetched_.end(), id),
                                          thumbnails_fetched_.end());
            }
            if (signed_in_) fetch_history(command.room_id);
            break;
        case AoratonCommandType::MarkRoomRead: {
            auto found = tracks_.find(command.room_id);
            if (found != tracks_.end() && !found->second.last_seen_at.empty()) {
                found->second.marker = found->second.last_seen_at;
            }
            break;
        }
        case AoratonCommandType::SendText: send_text(command); break;
        case AoratonCommandType::UnlockVault: unlock_vault(command.plaintext); break;
        case AoratonCommandType::AddInviteKey: add_invite(command.plaintext); break;
        case AoratonCommandType::ConfigureSync:
            if (aoraton_sync_settings_valid(command.sync_settings)) {
                sync_requested_ = sync_requested_ || (!sync_settings_.polling_enabled && command.sync_settings.polling_enabled);
                sync_settings_ = command.sync_settings;
                next_poll_ms_ = now_ms_ + poll_interval_ms();
            }
            break;
        case AoratonCommandType::PowerSaving: set_power_saving(command.power_saving); break;
        case AoratonCommandType::SendVoice: send_voice(command); break;
        case AoratonCommandType::OpenAttachment:
            if (signed_in_) open_attachment(command);
            break;
    }
    command.clear_plaintext();
}

void AoratonAccountWorker::begin_sign_in() {
    if (signing_in_) channel_.close();
    signing_in_ = false;
    wipe(verifier_);
    verifier_ = aoraton_pkce_verifier(account_crypto_);
    const std::string challenge = aoraton_pkce_challenge(verifier_, account_crypto_);
    AoratonEvent event;
    if (verifier_.empty() || challenge.empty() || config_.base_url.empty() ||
        !channel_.open(aoraton_authorize_url(config_, challenge))) {
        wipe(verifier_);
        event.type = AoratonEventType::PairingFailed;
        event.error = "SIGN_IN_UNAVAILABLE";
        emit(std::move(event));
        return;
    }
    signing_in_ = true;
    sign_in_started_ms_ = now_ms_;
    event.type = AoratonEventType::PairingChallenge;
    event.pairing_seconds_remaining = kSignInSeconds;
    emit(std::move(event));
}

void AoratonAccountWorker::poll_sign_in() {
    std::string code;
    const auto status = channel_.poll(code);
    if (status == AoratonCallbackStatus::Pending) {
        if (now_ms_ - sign_in_started_ms_ < kSignInTimeoutMs) return;
        channel_.close();
        signing_in_ = false;
        wipe(verifier_);
        AoratonEvent event;
        event.type = AoratonEventType::PairingFailed;
        event.error = "SIGN_IN_TIMEOUT";
        emit(std::move(event));
        return;
    }
    channel_.close();
    signing_in_ = false;
    AoratonEvent failure;
    failure.type = AoratonEventType::PairingFailed;
    if (status == AoratonCallbackStatus::Failed || code.empty()) {
        wipe(verifier_);
        failure.error = "SIGN_IN_CANCELLED";
        emit(std::move(failure));
        return;
    }
    const std::string body = "{\"auth_code\":\"" + json_escape(code) + "\",\"code_verifier\":\"" +
                             json_escape(verifier_) + "\"}";
    wipe(code);
    wipe(verifier_);
    const auto response = auth_post("/auth/v1/token?grant_type=pkce", body, false);
    AoratonSession session;
    if (response.status == 0) {
        failure.error = "SIGN_IN_NETWORK";
    } else if (!ok_status(response.status) || !aoraton_parse_session(response.body, now_unix_, session)) {
        failure.error = "SIGN_IN_REJECTED";
    } else {
        tracks_.clear();
        rooms_.clear();
        pending_sends_.clear();
        finish_sign_in(std::move(session));
        return;
    }
    emit(std::move(failure));
}

void AoratonAccountWorker::finish_sign_in(AoratonSession session) {
    session_ = std::move(session);
    signed_in_ = true;
    next_retry_ms_ = 0;
    wipe(secrets_.refresh_token);
    secrets_.refresh_token = session_.refresh_token;
    persist();
    AoratonEvent paired;
    paired.type = AoratonEventType::Paired;
    paired.account_label = !session_.email.empty() ? session_.email : session_.display_name;
    emit(std::move(paired));
    keys_changed(AoratonKeyState::Ready);
    connect();
}

void AoratonAccountWorker::connect() {
    if (!signed_in_) return;
    connected_ = true;
    reconnecting_ = false;
    next_poll_ms_ = 0;
    sync_requested_ = true;
    AoratonEvent event;
    event.type = AoratonEventType::ConnectionChanged;
    event.connection = AoratonConnectionState::Connected;
    emit(std::move(event));
}

std::int64_t AoratonAccountWorker::poll_interval_ms() const {
    const std::int64_t chosen = sync_settings_.interval_seconds * 1000LL;
    return power_saving_ && chosen < kPowerSavingPollMs ? kPowerSavingPollMs : chosen;
}

void AoratonAccountWorker::set_power_saving(bool saving) {
    if (saving == power_saving_) return;
    power_saving_ = saving;
    if (saving) {
        if (realtime_) realtime_->stop();
        realtime_status(false);
        next_poll_ms_ = now_ms_ + kPowerSavingPollMs;
        return;
    }
    // Waking: catch up at once, then realtime takes over again.
    sync_requested_ = true;
    configure_realtime();
}

void AoratonAccountWorker::configure_realtime() {
    if (!realtime_ || !signed_in_ || !connected_ || power_saving_) return;
    std::vector<std::string> keys;
    for (const auto& room : rooms_) keys.push_back(room.room_key);
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    realtime_->configure(config_.base_url, config_.anon_key, session_.access_token, keys);
}

void AoratonAccountWorker::realtime_status(bool active) {
    if (active == realtime_active_) return;
    realtime_active_ = active;
    AoratonEvent event;
    event.type = AoratonEventType::ConnectionChanged;
    event.connection = reconnecting_ ? AoratonConnectionState::Reconnecting : AoratonConnectionState::Connected;
    event.realtime_active = active;
    emit(std::move(event));
}

AoratonHttpResponse AoratonAccountWorker::auth_post(const std::string& path, const std::string& body, bool bearer) {
    AoratonHttpRequest request;
    request.method = "POST";
    request.url = config_.base_url + path;
    request.headers.push_back({"apikey", config_.anon_key});
    if (bearer) request.headers.push_back({"Authorization", "Bearer " + session_.access_token});
    request.headers.push_back({"Content-Type", "application/json"});
    request.body = body;
    auto response = http_.send(request);
    wipe(request.body);
    for (auto& header : request.headers) wipe(header.second);
    return response;
}

AoratonAccountWorker::RefreshResult AoratonAccountWorker::refresh_session() {
    if (session_.refresh_token.empty()) return RefreshResult::Revoked;
    std::string body = "{\"refresh_token\":\"" + json_escape(session_.refresh_token) + "\"}";
    auto response = auth_post("/auth/v1/token?grant_type=refresh_token", body, false);
    wipe(body);
    if (response.status == 0 || response.status >= 500 || response.status == 429) {
        wipe(response.body);
        return RefreshResult::Transient;
    }
    AoratonSession refreshed;
    const bool parsed = ok_status(response.status) && aoraton_parse_session(response.body, now_unix_, refreshed);
    wipe(response.body);
    if (!parsed) return RefreshResult::Revoked;
    session_ = std::move(refreshed);
    // Supabase rotates refresh tokens: the stored one must follow every refresh.
    wipe(secrets_.refresh_token);
    secrets_.refresh_token = session_.refresh_token;
    persist();
    return RefreshResult::Ok;
}

AoratonAccountWorker::RestResult AoratonAccountWorker::rest(const std::string& method,
                                                            const std::string& path,
                                                            const std::string& body,
                                                            const char* prefer,
                                                            std::uint32_t timeout_ms) {
    RestResult result;
    if (!signed_in_) return result;
    if (session_.expires_at_unix - now_unix_ < 60) {
        const auto refreshed = refresh_session();
        if (refreshed == RefreshResult::Revoked) {
            result.outcome = RestOutcome::Revoked;
            return result;
        }
        if (refreshed == RefreshResult::Transient) return result;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        AoratonHttpRequest request;
        request.method = method;
        request.url = config_.base_url + path;
        request.headers.push_back({"apikey", config_.anon_key});
        request.headers.push_back({"Authorization", "Bearer " + session_.access_token});
        if (!body.empty()) request.headers.push_back({"Content-Type", "application/json"});
        if (prefer) request.headers.push_back({"Prefer", prefer});
        request.body = body;
        request.timeout_ms = timeout_ms;
        result.response = http_.send(request);
        for (auto& header : request.headers) wipe(header.second);
        wipe(request.body);
        if (result.response.status != 401 || attempt == 1) break;
        const auto refreshed = refresh_session();
        if (refreshed == RefreshResult::Revoked) {
            result.outcome = RestOutcome::Revoked;
            return result;
        }
        if (refreshed == RefreshResult::Transient) {
            result.outcome = RestOutcome::Transient;
            return result;
        }
    }
    if (result.response.status == 401) {
        result.outcome = RestOutcome::Revoked;
    } else if (result.response.status == 0 || result.response.status >= 500 || result.response.status == 429) {
        result.outcome = RestOutcome::Transient;
    } else {
        result.outcome = RestOutcome::Ok;
    }
    return result;
}

void AoratonAccountWorker::session_revoked() {
    if (realtime_) realtime_->stop();
    realtime_active_ = false;
    signed_in_ = false;
    connected_ = false;
    reconnecting_ = false;
    session_.clear();
    wipe(secrets_.refresh_token);
    persist();  // room keys stay; only the account session is gone
    next_retry_ms_ = 0;
    AoratonEvent event;
    event.type = AoratonEventType::SessionRevoked;
    emit(std::move(event));
}

void AoratonAccountWorker::connection_lost() {
    if (realtime_) realtime_->stop();
    realtime_active_ = false;
    if (reconnecting_) return;
    reconnecting_ = true;
    AoratonEvent event;
    event.type = AoratonEventType::ConnectionChanged;
    event.connection = AoratonConnectionState::Reconnecting;
    emit(std::move(event));
}

void AoratonAccountWorker::sign_out() {
    if (realtime_) realtime_->stop();
    realtime_active_ = false;
    if (signed_in_) auth_post("/auth/v1/logout?scope=local", {}, true);
    if (signing_in_) channel_.close();
    signing_in_ = false;
    wipe(verifier_);
    signed_in_ = false;
    connected_ = false;
    reconnecting_ = false;
    session_.clear();
    secrets_.clear();
    store_.erase();
    if (attachment_cache_) attachment_cache_->clear();
    rooms_.clear();
    tracks_.clear();
    pending_sends_.clear();
    AoratonEvent event;
    event.type = AoratonEventType::Unpaired;
    emit(std::move(event));
}

bool AoratonAccountWorker::persist() { return store_.save(secrets_); }

const AoratonUnlockedKey* AoratonAccountWorker::key_for(const std::string& room_key) const {
    for (const auto& key : secrets_.keys) {
        if (key.room_key == room_key) return &key;
    }
    return nullptr;
}

bool AoratonAccountWorker::decrypt(const std::string& room_key, const std::string& wire, std::string& plaintext) {
    const auto* stored = key_for(room_key);
    if (!stored) return false;
    AoratonRoomKey key(stored->rk);
    AoratonCryptoError error = AoratonCryptoError::None;
    return message_crypto_.decrypt_text(wire, key, plaintext, error);
}

bool AoratonAccountWorker::is_member(const std::string& room_key) const {
    return std::any_of(rooms_.begin(), rooms_.end(),
                       [&](const AoratonRemoteRoom& room) { return room.room_key == room_key; });
}

void AoratonAccountWorker::keys_changed(AoratonKeyState state) {
    AoratonEvent event;
    event.type = AoratonEventType::KeysChanged;
    event.key_state = state;
    event.key_count = static_cast<std::uint32_t>(secrets_.keys.size());
    emit(std::move(event));
}

bool AoratonAccountWorker::refresh_rooms() {
    auto owned = rest("GET", aoraton_rooms_owned_path(session_.user_id), {});
    if (owned.outcome == RestOutcome::Revoked) {
        session_revoked();
        return false;
    }
    std::vector<AoratonRemoteRoom> rooms;
    std::vector<std::string> member_keys;
    if (owned.outcome == RestOutcome::Transient || !ok_status(owned.response.status) ||
        !aoraton_parse_rooms(owned.response.body, rooms)) {
        connection_lost();
        return false;
    }
    auto members = rest("GET", aoraton_member_keys_path(session_.user_id), {});
    if (members.outcome == RestOutcome::Revoked) {
        session_revoked();
        return false;
    }
    if (members.outcome == RestOutcome::Transient || !ok_status(members.response.status) ||
        !aoraton_parse_member_keys(members.response.body, member_keys)) {
        connection_lost();
        return false;
    }
    std::vector<std::string> missing;
    for (const auto& key : member_keys) {
        if (std::none_of(rooms.begin(), rooms.end(), [&](const AoratonRemoteRoom& r) { return r.room_key == key; })) {
            missing.push_back(key);
        }
    }
    for (std::size_t start = 0; start < missing.size() && start < kMaxRooms * 2; start += 64) {
        const std::vector<std::string> chunk(missing.begin() + static_cast<std::ptrdiff_t>(start),
                                             missing.begin() + static_cast<std::ptrdiff_t>(
                                                 std::min(missing.size(), start + 64)));
        auto joined = rest("GET", aoraton_rooms_by_keys_path(chunk), {});
        std::vector<AoratonRemoteRoom> extra;
        if (joined.outcome == RestOutcome::Revoked) {
            session_revoked();
            return false;
        }
        if (joined.outcome == RestOutcome::Transient || !ok_status(joined.response.status) ||
            !aoraton_parse_rooms(joined.response.body, extra)) {
            connection_lost();
            return false;
        }
        for (auto& room : extra) rooms.push_back(std::move(room));
    }
    const std::string now = now_iso();
    rooms.erase(std::remove_if(rooms.begin(), rooms.end(),
                               [&](const AoratonRemoteRoom& room) {
                                   return !room.expires_at.empty() && iso_prefix(room.expires_at) < now;
                               }),
                rooms.end());
    if (rooms.size() > kMaxRooms) rooms.resize(kMaxRooms);
    rooms_ = rooms;

    std::vector<std::pair<std::string, std::string>> items;
    for (const auto& room : rooms_) {
        const auto found = tracks_.find(room.room_key);
        items.push_back({room.room_key, found == tracks_.end() ? std::string{} : found->second.marker});
    }
    std::vector<AoratonOverview> overview;
    if (!items.empty()) {
        auto result = rest("POST", "/rest/v1/rpc/room_overview", aoraton_overview_body(items));
        if (result.outcome == RestOutcome::Revoked) {
            session_revoked();
            return false;
        }
        if (result.outcome == RestOutcome::Transient || !ok_status(result.response.status) ||
            !aoraton_parse_overview(result.response.body, overview)) {
            connection_lost();
            return false;
        }
    }

    // New rows are delivered before the room list, whose overview unread count
    // is authoritative and must not be added to by these deliveries.
    for (const auto& room : rooms_) {
        const auto info = std::find_if(overview.begin(), overview.end(),
                                       [&](const AoratonOverview& item) { return item.room_key == room.room_key; });
        auto& track = tracks_[room.room_key];
        if (!track.seen) continue;
        if (info != overview.end() && key_for(room.room_key) && newer(info->last_message_at, track.last_seen_at)) {
            deliver_new_messages(room.room_key);
        }
    }

    AoratonEvent event;
    event.type = AoratonEventType::AuthorizedRooms;
    event.timestamp = now;
    for (const auto& room : rooms_) {
        const auto info = std::find_if(overview.begin(), overview.end(),
                                       [&](const AoratonOverview& item) { return item.room_key == room.room_key; });
        auto& track = tracks_[room.room_key];
        AoratonRoom out;
        out.id = room.room_key;
        out.display_name = room.name.empty() ? room.room_key : room.name;
        out.encrypted = true;
        out.authorized = true;
        out.key_available = key_for(room.room_key) != nullptr;
        if (info != overview.end()) {
            out.latest_timestamp = info->last_message_at;
            if (!track.seen) {
                // First sight on this device: existing history is not "unread".
                track.marker = info->last_message_at;
                track.last_seen_at = info->last_message_at;
            } else {
                out.unread = info->unread;
            }
            std::string preview;
            if (out.key_available && info->last_type == "text" && decrypt(room.room_key, info->last_text, preview)) {
                out.preview = utf8_truncate(preview, 96);
            }
            wipe(preview);
        }
        track.seen = true;
        event.rooms.push_back(std::move(out));
    }
    std::stable_sort(event.rooms.begin(), event.rooms.end(), [](const AoratonRoom& left, const AoratonRoom& right) {
        return left.latest_timestamp > right.latest_timestamp;
    });
    emit(std::move(event));
    configure_realtime();
    return true;
}

bool AoratonAccountWorker::fetch_history(const std::string& room_key) {
    if (!key_for(room_key)) return false;
    auto result = rest("GET", aoraton_messages_path(room_key), {});
    if (result.outcome == RestOutcome::Revoked) {
        session_revoked();
        return false;
    }
    std::vector<AoratonRemoteMessage> rows;
    if (result.outcome == RestOutcome::Transient || !ok_status(result.response.status) ||
        !aoraton_parse_messages(result.response.body, rows)) {
        connection_lost();
        return false;
    }
    std::reverse(rows.begin(), rows.end());
    auto& track = tracks_[room_key];
    AoratonEvent history;
    history.type = AoratonEventType::MessageHistory;
    history.timestamp = now_iso();
    for (auto& row : rows) {
        if (row.room_key != room_key) continue;
        remember(track.seen_ids, row.id);
        if (newer(row.created_at, track.last_seen_at)) track.last_seen_at = row.created_at;
        const bool mine = row.uid == session_.user_id;
        if (mine && !row.username.empty()) track.my_username = row.username;
        AoratonMessage message;
        if (!build_message(room_key, row, mine, message)) continue;
        history.messages.push_back(std::move(message));
    }
    queue_thumbnails(history.messages);
    if (!history.messages.empty()) emit(std::move(history));
    return true;
}

void AoratonAccountWorker::deliver_new_messages(const std::string& room_key) {
    auto result = rest("GET", aoraton_messages_path(room_key), {});
    std::vector<AoratonRemoteMessage> rows;
    if (result.outcome != RestOutcome::Ok || !ok_status(result.response.status) ||
        !aoraton_parse_messages(result.response.body, rows)) return;
    std::reverse(rows.begin(), rows.end());
    auto& track = tracks_[room_key];
    std::vector<AoratonMessage> incoming;
    AoratonEvent history;
    history.type = AoratonEventType::MessageHistory;
    history.timestamp = now_iso();
    const std::string since = track.last_seen_at;
    for (auto& row : rows) {
        if (row.room_key != room_key || contains(track.seen_ids, row.id)) continue;
        remember(track.seen_ids, row.id);
        if (!newer(row.created_at, since)) continue;
        if (newer(row.created_at, track.last_seen_at)) track.last_seen_at = row.created_at;
        const bool mine = row.uid == session_.user_id;
        if (mine && !row.username.empty()) track.my_username = row.username;
        const auto pending = std::find_if(pending_sends_.begin(), pending_sends_.end(), [&](const PendingSend& send) {
            return !row.client_msg_id.empty() && send.client_message_id == row.client_msg_id;
        });
        if (pending != pending_sends_.end()) {
            AoratonEvent ack;
            ack.type = AoratonEventType::SendAcknowledged;
            ack.client_message_id = pending->client_message_id;
            ack.server_message_id = row.id;
            ack.timestamp = row.created_at;
            emit(std::move(ack));
            pending_sends_.erase(pending);
            continue;
        }
        AoratonMessage message;
        if (!build_message(room_key, row, mine, message)) continue;
        if (mine) history.messages.push_back(std::move(message));
        else incoming.push_back(std::move(message));
    }
    queue_thumbnails(history.messages);
    queue_thumbnails(incoming);
    // A long absence must not become a burst of notifications: only the newest
    // few notify, the rest arrive as plain history.
    const std::size_t quiet = incoming.size() > kMaxNotifiedPerPoll ? incoming.size() - kMaxNotifiedPerPoll : 0;
    for (std::size_t index = 0; index < quiet; ++index) history.messages.push_back(std::move(incoming[index]));
    if (!history.messages.empty()) emit(std::move(history));
    for (std::size_t index = quiet; index < incoming.size(); ++index) {
        AoratonEvent event;
        event.type = AoratonEventType::IncomingMessage;
        event.message = std::move(incoming[index]);
        event.timestamp = now_iso();
        emit(std::move(event));
    }
}

bool AoratonAccountWorker::build_message(const std::string& room_key, const AoratonRemoteMessage& row, bool mine,
                                         AoratonMessage& message) {
    if (row.type != "text") return false;
    // A file may come without a caption; text that does not open is skipped.
    if ((!row.text.empty() || !row.has_attachment) && !decrypt(room_key, row.text, message.local_display_text))
        return false;
    message.id = row.id;
    message.room_id = room_key;
    message.author = row.username;
    message.received_at = row.created_at;
    message.client_message_id = row.client_msg_id;
    message.direction = mine ? AoratonDirection::Outgoing : AoratonDirection::Incoming;
    message.delivery = mine ? AoratonDelivery::Acknowledged : AoratonDelivery::Received;
    if (row.has_attachment) {
        message.attachment = attachment_kind(row.attachment.type);
        message.attachment_name = row.attachment.name;
        if (attachments_.find(row.id) == attachments_.end()) {
            attachments_[row.id] = {room_key, row.attachment};
            attachment_order_.push_back(row.id);
            if (attachment_order_.size() > kMaxAttachments) {
                attachments_.erase(attachment_order_.front());
                attachment_order_.pop_front();
            }
        }
    }
    return true;
}

// The newest few photos of what was just delivered, newest first.
void AoratonAccountWorker::queue_thumbnails(const std::vector<AoratonMessage>& messages) {
    std::size_t queued = 0;
    for (auto message = messages.rbegin(); message != messages.rend() && queued < kThumbnailsPerRoom; ++message) {
        if (message->attachment != AoratonAttachmentKind::Image || contains(thumbnails_fetched_, message->id) ||
            std::find(thumbnail_queue_.begin(), thumbnail_queue_.end(), message->id) != thumbnail_queue_.end())
            continue;
        thumbnail_queue_.push_back(message->id);
        ++queued;
    }
    while (thumbnail_queue_.size() > kThumbnailsPerRoom * 2) thumbnail_queue_.pop_front();
}

void AoratonAccountWorker::load_next_thumbnail() {
    const std::string id = thumbnail_queue_.front();
    thumbnail_queue_.pop_front();
    const auto found = attachments_.find(id);
    if (found == attachments_.end()) return;
    remember(thumbnails_fetched_, id);
    std::vector<std::uint8_t> plaintext;
    std::string error;
    if (!fetch_attachment(found->second, plaintext, error)) return;  // the bubble keeps its placeholder
    PhotoError photo_error = PhotoError::None;
    const auto image = decode_photo(plaintext.data(), plaintext.size(), photo_error);
    std::fill(plaintext.begin(), plaintext.end(), std::uint8_t{0});
    if (!image) return;
    AoratonEvent event;
    event.type = AoratonEventType::AttachmentThumbnail;
    event.room_id = found->second.room_key;
    event.message_id = id;
    event.image = photo_thumbnail(*image, kThumbnailEdge);
    if (event.image) emit(std::move(event));
}

void AoratonAccountWorker::open_attachment(const AoratonCommand& command) {
    AoratonEvent failed;
    failed.type = AoratonEventType::AttachmentFailed;
    failed.message_id = command.message_id;
    const auto found = attachments_.find(command.message_id);
    const auto kind = found == attachments_.end() ? AoratonAttachmentKind::None : attachment_kind(found->second.file.type);
    if (kind == AoratonAttachmentKind::Voice) {
        std::vector<std::uint8_t> sound;
        if (!fetch_attachment(found->second, sound, failed.error)) {
            emit(std::move(failed));
            return;
        }
        auto clip = std::make_shared<PcmClip>();
        // Two minutes at most: decoded at 16 kHz that is under 4 MiB of PSRAM.
        const bool decoded = voice_decode(sound.data(), sound.size(), voice_decoder_, *clip, failed.error, 120);
        std::fill(sound.begin(), sound.end(), std::uint8_t{0});
        if (!decoded) {
            emit(std::move(failed));
            return;
        }
        AoratonEvent audio;
        audio.type = AoratonEventType::AttachmentAudio;
        audio.room_id = found->second.room_key;
        audio.message_id = command.message_id;
        audio.clip = std::move(clip);
        emit(std::move(audio));
        return;
    }
    if (kind != AoratonAttachmentKind::Image) {
        failed.error = "ATTACHMENT_UNSUPPORTED";
        emit(std::move(failed));
        return;
    }
    std::vector<std::uint8_t> plaintext;
    if (!fetch_attachment(found->second, plaintext, failed.error)) {
        emit(std::move(failed));
        return;
    }
    PhotoError photo_error = PhotoError::None;
    auto image = decode_photo(plaintext.data(), plaintext.size(), photo_error);
    std::fill(plaintext.begin(), plaintext.end(), std::uint8_t{0});
    if (!image) {
        failed.error = "ATTACHMENT_UNSUPPORTED";
        emit(std::move(failed));
        return;
    }
    AoratonEvent opened;
    opened.type = AoratonEventType::AttachmentOpened;
    opened.room_id = found->second.room_key;
    opened.message_id = command.message_id;
    opened.image = std::move(image);
    emit(std::move(opened));
}

// Only an encrypted file in the project's bucket, of at most 4 MiB, opened
// with the room's key; a code for every other outcome.
bool AoratonAccountWorker::fetch_attachment(const AttachmentRef& ref, std::vector<std::uint8_t>& plaintext,
                                            std::string& error) {
    plaintext.clear();
    const auto& file = ref.file;
    if (!file.encrypted || file.wrapped_key.empty() || file.iv.empty() ||
        !aoraton_attachment_url_valid(config_.base_url, file.url)) {
        error = "ATTACHMENT_UNSUPPORTED";
        return false;
    }
    const auto* stored = key_for(ref.room_key);
    if (!stored) {
        error = "ATTACHMENT_LOCKED";
        return false;
    }
    const std::size_t limit = (file.size > 0 && file.size <= kMaxPhotoFileBytes ? file.size : kMaxPhotoFileBytes) +
                              kAoratonGcmTagBytes;
    const auto open = [&](const std::string& sealed) {
        AoratonRoomKey key(stored->rk);
        std::array<std::uint8_t, 32> file_key{};
        const bool opened = message_crypto_.unwrap_file_key(file.wrapped_key, key, file_key) &&
                            message_crypto_.open_file(file_key, file.iv,
                                                      reinterpret_cast<const std::uint8_t*>(sealed.data()),
                                                      sealed.size(), plaintext);
        std::fill(file_key.begin(), file_key.end(), std::uint8_t{0});
        if (!opened) plaintext.clear();
        return opened;
    };
    // A copy on the card opens like a download: the same sealed bytes, checked
    // by AES-GCM. One that no longer opens is dropped and fetched again.
    const std::string cache_name = attachment_cache_ ? aoraton_sha256_hex(file.url, account_crypto_) : std::string();
    if (!cache_name.empty()) {
        std::string sealed;
        if (attachment_cache_->load(cache_name, limit, sealed)) {
            if (open(sealed)) return true;
            attachment_cache_->erase(cache_name);
        }
    }
    AoratonHttpRequest request;
    request.method = "GET";
    request.url = file.url;
    request.headers.push_back({"apikey", config_.anon_key});
    request.timeout_ms = kAttachmentTimeoutMs;
    request.max_response_bytes = limit;
    auto response = http_.send(request);
    if (response.status == 0 || !ok_status(response.status) || response.body.size() > limit) {
        error = "ATTACHMENT_UNAVAILABLE";
        return false;
    }
    if (!open(response.body)) {
        error = "ATTACHMENT_DAMAGED";
        return false;
    }
    if (!cache_name.empty()) attachment_cache_->store(cache_name, response.body);
    return true;
}

void AoratonAccountWorker::send_text(AoratonCommand& command) {
    AoratonEvent failed;
    failed.type = AoratonEventType::SendFailed;
    failed.client_message_id = command.client_message_id;
    const auto* stored = key_for(command.room_id);
    if (!signed_in_ || !stored || !is_member(command.room_id)) {
        failed.error = "ROOM_ACCESS_CHANGED";
        emit(std::move(failed));
        return;
    }
    std::string wire;
    {
        AoratonRoomKey key(stored->rk);
        AoratonCryptoError error = AoratonCryptoError::None;
        if (!message_crypto_.encrypt_text(command.plaintext, key, wire, error)) {
            failed.error = "ENCRYPTION_FAILED";
            emit(std::move(failed));
            return;
        }
    }
    // A question for inco is kept only until its stored row is known; nothing
    // else is ever kept after encryption.
    std::string question;
    WipeOnExit wipe_question{question};
    const auto* remote = room_for(command.room_id);
    if (config_.inco.enabled && remote && remote->ai_enabled && aoraton_mentions_inco(command.plaintext)) {
        question = command.plaintext;
    }
    command.clear_plaintext();
    AoratonEvent submitted;
    submitted.type = AoratonEventType::SendSubmitted;
    submitted.client_message_id = command.client_message_id;
    emit(std::move(submitted));

    const auto& track = tracks_[command.room_id];
    std::string username = !track.my_username.empty() ? track.my_username :
                           (!session_.display_name.empty() ? session_.display_name : std::string("AURA"));
    auto result = rest("POST", "/rest/v1/messages?select=id,created_at",
                       aoraton_insert_body(command.room_id, session_.user_id, username, wire,
                                           command.client_message_id),
                       "return=representation");
    if (result.outcome == RestOutcome::Revoked) {
        failed.error = "SESSION_REVOKED";
        emit(std::move(failed));
        session_revoked();
        return;
    }
    if (result.outcome == RestOutcome::Transient) {
        // The row may or may not exist; the next poll reconciles by client_msg_id.
        pending_sends_.push_back({command.client_message_id, command.room_id});
        if (pending_sends_.size() > kMaxPendingSends) pending_sends_.erase(pending_sends_.begin());
        AoratonEvent uncertain;
        uncertain.type = AoratonEventType::SendUncertain;
        uncertain.client_message_id = command.client_message_id;
        emit(std::move(uncertain));
        connection_lost();
        return;
    }
    std::string id;
    std::string created_at;
    if (!ok_status(result.response.status) || !aoraton_parse_insert_ack(result.response.body, id, created_at)) {
        failed.error = "SEND_REJECTED";
        emit(std::move(failed));
        return;
    }
    auto& sent_track = tracks_[command.room_id];
    remember(sent_track.seen_ids, id);
    if (newer(created_at, sent_track.last_seen_at)) sent_track.last_seen_at = created_at;
    AoratonEvent ack;
    ack.type = AoratonEventType::SendAcknowledged;
    ack.client_message_id = command.client_message_id;
    ack.server_message_id = id;
    ack.timestamp = created_at;
    emit(std::move(ack));
    if (!question.empty()) answer_inco(command.room_id, id, username, question);
}

// A recording as the web client sends a file: sealed with its own key, the
// key wrapped with the room's, uploaded to the public bucket under the
// hashed room folder (never overwriting), then a row with the attachment.
void AoratonAccountWorker::send_voice(AoratonCommand& command) {
    AoratonEvent failed;
    failed.type = AoratonEventType::SendFailed;
    failed.client_message_id = command.client_message_id;
    const auto* stored = key_for(command.room_id);
    if (!signed_in_ || !stored || !is_member(command.room_id) || command.bytes.empty()) {
        failed.error = "ROOM_ACCESS_CHANGED";
        emit(std::move(failed));
        return;
    }
    AoratonRemoteAttachment file;
    std::vector<std::uint8_t> sealed;
    {
        AoratonRoomKey key(stored->rk);
        std::array<std::uint8_t, 32> file_key{};
        const bool ok = message_crypto_.seal_file(command.bytes.data(), command.bytes.size(), file_key, file.iv,
                                                  sealed) &&
                        message_crypto_.wrap_file_key(file_key, key, file.wrapped_key);
        std::fill(file_key.begin(), file_key.end(), std::uint8_t{0});
        if (!ok) {
            failed.error = "ENCRYPTION_FAILED";
            emit(std::move(failed));
            return;
        }
    }
    file.size = command.bytes.size();
    command.clear_plaintext();
    // Math.random()-like digits, as the web client names its files.
    std::array<std::uint8_t, 8> random{};
    account_crypto_.random_bytes(random.data(), random.size());
    std::string name = "0.";
    for (const auto byte : random) name += std::to_string(byte % 100);
    const std::string folder = aoraton_storage_folder(command.room_id, account_crypto_);
    const std::string object = folder + "/" + name + ".enc";
    file.url = config_.base_url + "/storage/v1/object/public/attachments/" + object;
    file.type = "audio/wav";
    file.name = "voice_" + std::to_string(now_unix_ * 1000) + ".wav";
    file.encrypted = true;
    AoratonEvent submitted;
    submitted.type = AoratonEventType::SendSubmitted;
    submitted.client_message_id = command.client_message_id;
    emit(std::move(submitted));
    AoratonHttpRequest upload;
    upload.method = "POST";
    upload.url = config_.base_url + "/storage/v1/object/attachments/" + object;
    upload.headers.push_back({"apikey", config_.anon_key});
    upload.headers.push_back({"Authorization", "Bearer " + session_.access_token});
    upload.headers.push_back({"Content-Type", "application/octet-stream"});
    upload.headers.push_back({"cache-control", "max-age=31536000"});
    upload.headers.push_back({"x-upsert", "false"});
    upload.body.assign(sealed.begin(), sealed.end());
    upload.timeout_ms = kAttachmentTimeoutMs;
    const auto stored_file = http_.send(upload);
    for (auto& header : upload.headers) wipe(header.second);
    if (folder.empty() || stored_file.status == 0 || !ok_status(stored_file.status)) {
        failed.error = stored_file.status == 401 ? "SESSION_REVOKED" : "UPLOAD_FAILED";
        emit(std::move(failed));
        if (stored_file.status == 0) connection_lost();
        return;
    }
    const auto& track = tracks_[command.room_id];
    const std::string username = !track.my_username.empty() ? track.my_username :
                                 (!session_.display_name.empty() ? session_.display_name : std::string("AURA"));
    auto result = rest("POST", "/rest/v1/messages?select=id,created_at",
                       aoraton_insert_attachment_body(command.room_id, session_.user_id, username,
                                                      command.client_message_id, file),
                       "return=representation");
    if (result.outcome == RestOutcome::Revoked) {
        failed.error = "SESSION_REVOKED";
        emit(std::move(failed));
        session_revoked();
        return;
    }
    if (result.outcome == RestOutcome::Transient) {
        pending_sends_.push_back({command.client_message_id, command.room_id});
        if (pending_sends_.size() > kMaxPendingSends) pending_sends_.erase(pending_sends_.begin());
        AoratonEvent uncertain;
        uncertain.type = AoratonEventType::SendUncertain;
        uncertain.client_message_id = command.client_message_id;
        emit(std::move(uncertain));
        connection_lost();
        return;
    }
    std::string id;
    std::string created_at;
    if (!ok_status(result.response.status) || !aoraton_parse_insert_ack(result.response.body, id, created_at)) {
        failed.error = "SEND_REJECTED";
        emit(std::move(failed));
        return;
    }
    auto& sent_track = tracks_[command.room_id];
    remember(sent_track.seen_ids, id);
    if (newer(created_at, sent_track.last_seen_at)) sent_track.last_seen_at = created_at;
    AoratonEvent ack;
    ack.type = AoratonEventType::SendAcknowledged;
    ack.client_message_id = command.client_message_id;
    ack.server_message_id = id;
    ack.timestamp = created_at;
    emit(std::move(ack));
}

const AoratonRemoteRoom* AoratonAccountWorker::room_for(const std::string& room_key) const {
    for (const auto& room : rooms_) {
        if (room.room_key == room_key) return &room;
    }
    return nullptr;
}

void AoratonAccountWorker::inco_log(const std::string& line) const {
    if (config_.inco.log) config_.inco.log("inco: " + line);
}

int AoratonAccountWorker::utc_offset_minutes() const {
    return config_.inco.utc_offset_minutes ? config_.inco.utc_offset_minutes(now_unix_) : 0;
}

// As the web client does for the message its own device sent: ask the edge
// function, then encrypt and insert the answer under inco's uid. One at a
// time; failures are logged as codes and never retried.
void AoratonAccountWorker::answer_inco(const std::string& room_key, const std::string& row_id,
                                       const std::string& username, const std::string& question) {
    const auto* room = room_for(room_key);
    AoratonIncoCandidate candidate;
    candidate.sent_here = true;
    candidate.row_id = row_id;
    candidate.uid = session_.user_id;
    candidate.type = "text";
    candidate.text = question;
    WipeOnExit wipe_candidate{candidate.text};
    candidate.room_ai_enabled = room && room->ai_enabled;
    const int offset = utc_offset_minutes();
    const std::string today = aoraton_inco_local_day(now_unix_, offset);
    if (!aoraton_inco_should_answer(candidate, config_.inco.enabled, inco_busy_, inco_quota_day_ == today)) return;
    inco_busy_ = true;
    struct Idle {
        bool& busy;
        ~Idle() { busy = false; }
    } idle{inco_busy_};

    std::vector<AoratonIncoTurn> turns;
    if (!inco_history(room_key, row_id, username, question, turns)) {
        inco_log("HISTORY_UNAVAILABLE for " + row_id);
        return;
    }
    const std::string when = aoraton_inco_client_date_time(now_unix_, offset);
    AoratonIncoReply reply;
    auto location = AoratonIncoLocation::Unasked;
    std::string city;
    bool failed = false;
    for (int pass = 0; pass < 2 && !failed; ++pass) {
        std::string body = aoraton_inco_request_body(room_key, turns, question, username, when, location, city);
        auto result = rest("POST", "/functions/v1/inco-ai", body, nullptr, kIncoTimeoutMs);
        wipe(body);
        const int status = result.response.status;
        const bool parsed = ok_status(status) && aoraton_parse_inco_reply(result.response.body, reply);
        wipe(result.response.body);
        failed = !parsed;
        if (!ok_status(status)) {
            const bool revoked = result.outcome == RestOutcome::Revoked && status == 0;
            inco_log((revoked ? std::string("SESSION_REVOKED") : inco_failure(status)) + " for " + row_id);
        } else if (!parsed) {
            inco_log("BAD_RESPONSE for " + row_id);
        } else if (reply.kind == AoratonIncoReplyKind::NeedLocation && pass == 0) {
            // The model wants a place: ask once more, with the city AURA knows
            // or saying that none was given.
            city = config_.inco.location_city ? config_.inco.location_city() : std::string{};
            location = city.empty() ? AoratonIncoLocation::Denied : AoratonIncoLocation::City;
        } else {
            break;
        }
    }
    for (auto& turn : turns) wipe(turn.text);
    if (failed) return;

    switch (reply.kind) {
        case AoratonIncoReplyKind::Text: {
            const std::string text = aoraton_inco_plain_text(reply.text);
            wipe(reply.text);
            const auto* stored = key_for(room_key);
            AoratonIncoQuote quote;
            quote.id = row_id;
            quote.username = username;
            if (stored) {
                AoratonRoomKey key(stored->rk);
                AoratonCryptoError error = AoratonCryptoError::None;
                std::string excerpt = aoraton_inco_prefix(question, kIncoQuoteChars);
                const bool sealed = message_crypto_.encrypt_text(excerpt, key, quote.wire, error);
                wipe(excerpt);
                if (!sealed) {
                    inco_log("ENCRYPTION_FAILED for " + row_id);
                    return;
                }
            }
            if (write_inco(room_key, text, &quote, reply.sources)) inco_log("answered " + row_id);
            break;
        }
        case AoratonIncoReplyKind::Quota:
            inco_quota_day_ = today;
            inco_log("QUOTA_EXHAUSTED for " + row_id);
            write_inco(room_key, kAoratonIncoQuotaText, nullptr, {});
            break;
        case AoratonIncoReplyKind::NeedLocation:
        case AoratonIncoReplyKind::Empty:
            if (reply.kind == AoratonIncoReplyKind::NeedLocation) inco_log("NO_ANSWER for " + row_id);
            break;
    }
}

// The newest rows up to and including the question, decrypted, as inco's turns.
bool AoratonAccountWorker::inco_history(const std::string& room_key, const std::string& row_id,
                                        const std::string& username, const std::string& question,
                                        std::vector<AoratonIncoTurn>& turns) {
    auto result = rest("GET", aoraton_inco_history_path(room_key), {});
    std::vector<AoratonRemoteMessage> rows;
    if (result.outcome != RestOutcome::Ok || !ok_status(result.response.status) ||
        !aoraton_parse_messages(result.response.body, rows)) {
        return false;
    }
    std::reverse(rows.begin(), rows.end());
    std::vector<AoratonIncoRow> plain;
    bool asked = false;
    for (const auto& row : rows) {
        if (row.room_key != room_key) continue;
        AoratonIncoRow item;
        item.id = row.id;
        item.uid = row.uid;
        item.username = row.username;
        item.type = row.type;
        if (!row.reply_to_id.empty()) item.reply_to_username = row.reply_to_username;
        if (row.has_poll && decrypt(room_key, row.poll_question, item.poll_question)) {
            item.has_poll = true;
            for (const auto& option : row.poll_options) {
                std::string text;
                if (decrypt(room_key, option, text)) item.poll_options.push_back(std::move(text));
            }
        } else if (!row.text.empty() && !decrypt(room_key, row.text, item.text)) {
            continue;  // unreadable here: left out rather than guessed
        }
        plain.push_back(std::move(item));
        if (row.id == row_id) {
            asked = true;
            break;  // the question is the last turn; later rows are not part of it
        }
    }
    if (!asked) {
        AoratonIncoRow item;
        item.id = row_id;
        item.uid = session_.user_id;
        item.username = username;
        item.type = "text";
        item.text = question;
        plain.push_back(std::move(item));
    }
    turns = aoraton_inco_turns(plain);
    for (auto& item : plain) {
        wipe(item.text);
        wipe(item.poll_question);
        for (auto& option : item.poll_options) wipe(option);
    }
    return true;
}

// Encrypts and inserts an inco row with this account's session (RLS allows
// inco's uid to members of rooms that enable it), then shows it at once.
bool AoratonAccountWorker::write_inco(const std::string& room_key, const std::string& text,
                                      const AoratonIncoQuote* quote, const std::vector<AoratonIncoSource>& sources) {
    const auto* stored = key_for(room_key);
    const auto* room = room_for(room_key);
    if (!stored || !room) {
        inco_log("ROOM_ACCESS_CHANGED");
        return false;
    }
    std::string wire;
    {
        AoratonRoomKey key(stored->rk);
        AoratonCryptoError error = AoratonCryptoError::None;
        if (!message_crypto_.encrypt_text(text, key, wire, error)) {
            inco_log("ENCRYPTION_FAILED");
            return false;
        }
    }
    auto result = rest("POST", "/rest/v1/messages?select=id,created_at",
                       aoraton_inco_insert_body(room_key, aoraton_inco_avatar(room->ai_avatar_url), wire, quote, sources),
                       "return=representation");
    std::string id;
    std::string created_at;
    if (!ok_status(result.response.status) || !aoraton_parse_insert_ack(result.response.body, id, created_at)) {
        inco_log("INSERT_REJECTED (" + inco_failure(result.response.status) + ")");
        return false;
    }
    // Shown now rather than at the next poll; the poll's copy is the same id.
    AoratonEvent event;
    event.type = AoratonEventType::IncomingMessage;
    event.message.id = id;
    event.message.room_id = room_key;
    event.message.author = "inco";
    event.message.local_display_text = text;
    event.message.received_at = created_at;
    event.message.direction = AoratonDirection::Incoming;
    event.message.delivery = AoratonDelivery::Received;
    event.timestamp = now_iso();
    emit(std::move(event));
    return true;
}

void AoratonAccountWorker::unlock_vault(std::string& passphrase) {
    if (!signed_in_) {
        wipe(passphrase);
        keys_changed(AoratonKeyState::Failed);
        return;
    }
    auto meta_result = rest("GET", "/rest/v1/key_vault_meta?select=kdf_salt,kdf_iters,check_blob", {});
    AoratonVaultMeta meta;
    bool configured = false;
    if (meta_result.outcome == RestOutcome::Revoked) {
        wipe(passphrase);
        session_revoked();
        return;
    }
    if (meta_result.outcome != RestOutcome::Ok || !ok_status(meta_result.response.status) ||
        !aoraton_parse_vault_meta(meta_result.response.body, meta, configured)) {
        wipe(passphrase);
        keys_changed(AoratonKeyState::Failed);
        return;
    }
    if (!configured) {
        wipe(passphrase);
        keys_changed(AoratonKeyState::NotConfigured);
        return;
    }
    auto rows_result = rest("GET", "/rest/v1/key_vault?select=room_key,blob", {});
    std::vector<AoratonVaultRow> rows;
    if (rows_result.outcome != RestOutcome::Ok || !ok_status(rows_result.response.status) ||
        !aoraton_parse_vault_rows(rows_result.response.body, rows)) {
        wipe(passphrase);
        keys_changed(AoratonKeyState::Failed);
        return;
    }
    std::vector<AoratonUnlockedKey> keys;
    const auto opened = aoraton_open_vault(meta, rows, passphrase, account_crypto_, keys);
    wipe(passphrase);
    if (opened != AoratonVaultResult::Ok) {
        keys_changed(opened == AoratonVaultResult::WrongPassphrase ? AoratonKeyState::WrongPassphrase :
                     (opened == AoratonVaultResult::NotConfigured ? AoratonKeyState::NotConfigured :
                                                                    AoratonKeyState::Failed));
        return;
    }
    for (auto& key : keys) {
        auto existing = std::find_if(secrets_.keys.begin(), secrets_.keys.end(),
                                     [&](const AoratonUnlockedKey& held) { return held.room_key == key.room_key; });
        if (existing != secrets_.keys.end()) *existing = std::move(key);
        else secrets_.keys.push_back(std::move(key));
    }
    persist();
    keys_changed(AoratonKeyState::Ready);
    refresh_rooms();
}

void AoratonAccountWorker::add_invite(std::string& invite) {
    AoratonInvite parsed;
    const bool ok = aoraton_parse_invite(invite, parsed);
    wipe(invite);
    if (!ok) {
        keys_changed(AoratonKeyState::InvalidInvite);
        return;
    }
    if (!signed_in_) {
        keys_changed(AoratonKeyState::Failed);
        return;
    }
    if (rooms_.empty()) refresh_rooms();
    if (!is_member(parsed.room_key)) {
        keys_changed(AoratonKeyState::NotMember);
        return;
    }
    AoratonUnlockedKey key;
    key.room_key = parsed.room_key;
    key.rk = parsed.rk;
    auto existing = std::find_if(secrets_.keys.begin(), secrets_.keys.end(),
                                 [&](const AoratonUnlockedKey& held) { return held.room_key == key.room_key; });
    if (existing != secrets_.keys.end()) *existing = std::move(key);
    else secrets_.keys.push_back(std::move(key));
    persist();
    keys_changed(AoratonKeyState::Ready);
    refresh_rooms();
}

}  // namespace aura

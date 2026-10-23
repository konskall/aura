#include "aura/aoraton_account_worker.hpp"

#include "aura/json_reader.hpp"
#include "aura/text_util.hpp"

#include <algorithm>
#include <cstdio>

namespace aura {
namespace {

constexpr std::size_t kMaxRooms = 64;
constexpr std::size_t kMaxSeenIds = 120;
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
                                           IAoratonSignInChannel& channel)
    : mailbox_(mailbox),
      config_(std::move(config)),
      http_(http),
      account_crypto_(account_crypto),
      message_crypto_(message_crypto),
      store_(store),
      channel_(channel) {
    config_.base_url = trim_base(config_.base_url);
}

AoratonAccountWorker::~AoratonAccountWorker() {
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
    if (signed_in_ && connected_ && now_ms_ >= next_poll_ms_) {
        if (refresh_rooms()) {
            if (reconnecting_) {
                reconnecting_ = false;
                AoratonEvent event;
                event.type = AoratonEventType::ConnectionChanged;
                event.connection = AoratonConnectionState::Connected;
                emit(std::move(event));
            }
        }
        next_poll_ms_ = now_ms_ + kPollIntervalMs;
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
        case AoratonCommandType::Disconnect: connected_ = false; break;
        case AoratonCommandType::RefreshRooms:
            if (signed_in_) {
                refresh_rooms();
                next_poll_ms_ = now_ms_ + kPollIntervalMs;
            }
            break;
        case AoratonCommandType::FetchRecentMessages:
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
    AoratonEvent event;
    event.type = AoratonEventType::ConnectionChanged;
    event.connection = AoratonConnectionState::Connected;
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
    if (reconnecting_) return;
    reconnecting_ = true;
    AoratonEvent event;
    event.type = AoratonEventType::ConnectionChanged;
    event.connection = AoratonConnectionState::Reconnecting;
    emit(std::move(event));
}

void AoratonAccountWorker::sign_out() {
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
        if (row.type != "text") continue;
        AoratonMessage message;
        if (!decrypt(room_key, row.text, message.local_display_text)) continue;
        message.id = row.id;
        message.room_id = room_key;
        message.author = row.username;
        message.received_at = row.created_at;
        message.client_message_id = row.client_msg_id;
        message.direction = mine ? AoratonDirection::Outgoing : AoratonDirection::Incoming;
        message.delivery = mine ? AoratonDelivery::Acknowledged : AoratonDelivery::Received;
        history.messages.push_back(std::move(message));
    }
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
        if (row.type != "text") continue;
        AoratonMessage message;
        if (!decrypt(room_key, row.text, message.local_display_text)) continue;
        message.id = row.id;
        message.room_id = room_key;
        message.author = row.username;
        message.received_at = row.created_at;
        message.client_message_id = row.client_msg_id;
        message.direction = mine ? AoratonDirection::Outgoing : AoratonDirection::Incoming;
        message.delivery = mine ? AoratonDelivery::Acknowledged : AoratonDelivery::Received;
        if (mine) history.messages.push_back(std::move(message));
        else incoming.push_back(std::move(message));
    }
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

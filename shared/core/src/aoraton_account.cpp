#include "aura/aoraton_account.hpp"

#include "aura/aoraton_crypto.hpp"
#include "aura/json_reader.hpp"
#include "aura/text_util.hpp"

#include <algorithm>

namespace aura {
namespace {

constexpr char kVaultCheckPlaintext[] = "inco-vault-check-v1";
// A hostile or broken server must not make AURA spin on PBKDF2 for minutes.
constexpr std::uint32_t kMaxVaultIterations = 5000000;
constexpr std::size_t kMaxRows = 512;

void wipe(std::string& value) noexcept {
    volatile char* data = value.empty() ? nullptr : &value[0];
    for (std::size_t index = 0; index < value.size(); ++index) data[index] = 0;
    value.clear();
}

template <std::size_t N>
void wipe(std::array<std::uint8_t, N>& value) noexcept {
    volatile std::uint8_t* data = value.data();
    for (std::size_t index = 0; index < N; ++index) data[index] = 0;
}

void wipe(std::vector<std::uint8_t>& value) noexcept {
    volatile std::uint8_t* data = value.empty() ? nullptr : value.data();
    for (std::size_t index = 0; index < value.size(); ++index) data[index] = 0;
    value.clear();
}

std::string trim_base(const std::string& base) {
    std::string value = base;
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

bool parse_rows(std::string_view json, JsonValue& rows) {
    return parse_json(json, rows) && rows.type() == JsonType::Array && rows.items().size() <= kMaxRows;
}

std::string member_text(const JsonValue& row, std::string_view key) { return row.string_at(key); }

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// URLSearchParams semantics: '+' is a space and %XX is a byte.
bool form_decode(std::string_view value, std::string& out) {
    out.clear();
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char c = value[index];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%') {
            if (index + 2 >= value.size()) return false;
            const int high = hex_digit(value[index + 1]);
            const int low = hex_digit(value[index + 2]);
            if (high < 0 || low < 0) return false;
            out.push_back(static_cast<char>((high << 4) | low));
            index += 2;
        } else {
            out.push_back(c);
        }
    }
    return true;
}

}  // namespace

AoratonSession::~AoratonSession() { clear(); }

AoratonSession::AoratonSession(AoratonSession&& other) noexcept
    : access_token(std::move(other.access_token)),
      refresh_token(std::move(other.refresh_token)),
      user_id(std::move(other.user_id)),
      email(std::move(other.email)),
      display_name(std::move(other.display_name)),
      expires_at_unix(other.expires_at_unix) {
    other.clear();
}

AoratonSession& AoratonSession::operator=(AoratonSession&& other) noexcept {
    if (this == &other) return *this;
    clear();
    access_token = std::move(other.access_token);
    refresh_token = std::move(other.refresh_token);
    user_id = std::move(other.user_id);
    email = std::move(other.email);
    display_name = std::move(other.display_name);
    expires_at_unix = other.expires_at_unix;
    other.clear();
    return *this;
}

void AoratonSession::clear() noexcept {
    wipe(access_token);
    wipe(refresh_token);
    user_id.clear();
    email.clear();
    display_name.clear();
    expires_at_unix = 0;
}

AoratonUnlockedKey::~AoratonUnlockedKey() { clear(); }

AoratonUnlockedKey::AoratonUnlockedKey(AoratonUnlockedKey&& other) noexcept
    : room_key(std::move(other.room_key)), rk(other.rk) {
    other.clear();
}

AoratonUnlockedKey& AoratonUnlockedKey::operator=(AoratonUnlockedKey&& other) noexcept {
    if (this == &other) return *this;
    clear();
    room_key = std::move(other.room_key);
    rk = other.rk;
    other.clear();
    return *this;
}

void AoratonUnlockedKey::clear() noexcept {
    wipe(rk);
    room_key.clear();
}

AoratonInvite::~AoratonInvite() { clear(); }

void AoratonInvite::clear() noexcept {
    wipe(rk);
    room_name.clear();
    room_key.clear();
}

std::string aoraton_pkce_verifier(IAoratonAccountCrypto& crypto) {
    std::array<std::uint8_t, 32> bytes{};
    if (!crypto.random_bytes(bytes.data(), bytes.size())) return {};
    std::string verifier = aoraton_base64url_encode(bytes.data(), bytes.size());
    wipe(bytes);
    return verifier;
}

std::string aoraton_pkce_challenge(const std::string& verifier, IAoratonAccountCrypto& crypto) {
    if (verifier.size() < 43 || verifier.size() > 128) return {};
    std::array<std::uint8_t, 32> digest{};
    if (!crypto.sha256(reinterpret_cast<const std::uint8_t*>(verifier.data()), verifier.size(), digest)) return {};
    return aoraton_base64url_encode(digest.data(), digest.size());
}

std::string aoraton_url_encode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
            byte == '-' || byte == '.' || byte == '_' || byte == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0F]);
        }
    }
    return out;
}

std::string aoraton_authorize_url(const AoratonAccountConfig& config, const std::string& challenge) {
    return trim_base(config.base_url) + "/auth/v1/authorize?provider=google&redirect_to=" +
           aoraton_url_encode(config.redirect_url) + "&code_challenge=" + aoraton_url_encode(challenge) +
           "&code_challenge_method=s256";
}

std::string aoraton_new_client_message_id(IAoratonAccountCrypto& crypto) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::array<std::uint8_t, 16> bytes{};
    if (!crypto.random_bytes(bytes.data(), bytes.size())) return {};
    std::string id = "temp_aura";
    for (const auto byte : bytes) {
        id.push_back(kHex[byte >> 4]);
        id.push_back(kHex[byte & 0x0F]);
    }
    return id;
}

std::string aoraton_rooms_owned_path(const std::string& user_id) {
    return "/rest/v1/rooms?select=*&created_by=eq." + aoraton_url_encode(user_id);
}

std::string aoraton_member_keys_path(const std::string& user_id) {
    return "/rest/v1/subscribers?select=room_key&uid=eq." + aoraton_url_encode(user_id);
}

std::string aoraton_rooms_by_keys_path(const std::vector<std::string>& room_keys) {
    std::string list;
    for (const auto& key : room_keys) {
        // PostgREST quoting cannot carry a quote or backslash unescaped; no real
        // room key contains one, so such a key is skipped rather than escaped.
        if (key.empty() || key.find_first_of("\"\\") != std::string::npos) continue;
        if (!list.empty()) list.push_back(',');
        list += "%22" + aoraton_url_encode(key) + "%22";
    }
    return "/rest/v1/rooms?select=*&room_key=in.(" + list + ")";
}

std::string aoraton_messages_path(const std::string& room_key) {
    return "/rest/v1/messages?select=id,room_key,uid,username,text,type,created_at,client_msg_id&room_key=eq." +
           aoraton_url_encode(room_key) + "&order=created_at.desc,id.desc&limit=50";
}

std::string aoraton_overview_body(const std::vector<std::pair<std::string, std::string>>& room_since) {
    std::string body = "{\"p_items\":[";
    for (std::size_t index = 0; index < room_since.size(); ++index) {
        if (index) body.push_back(',');
        body += "{\"room_key\":\"" + json_escape(room_since[index].first) + "\",\"since\":";
        body += room_since[index].second.empty() ? std::string("null")
                                                 : "\"" + json_escape(room_since[index].second) + "\"";
        body.push_back('}');
    }
    return body + "]}";
}

std::string aoraton_insert_body(const std::string& room_key,
                                const std::string& user_id,
                                const std::string& username,
                                const std::string& wire,
                                const std::string& client_message_id) {
    return "{\"room_key\":\"" + json_escape(room_key) + "\",\"uid\":\"" + json_escape(user_id) +
           "\",\"username\":\"" + json_escape(username) + "\",\"avatar_url\":null,\"text\":\"" + json_escape(wire) +
           "\",\"type\":\"text\",\"client_msg_id\":\"" + json_escape(client_message_id) + "\",\"reactions\":{}}";
}

bool aoraton_parse_session(std::string_view json, std::int64_t now_unix, AoratonSession& session) {
    JsonValue value;
    if (!parse_json(json, value) || value.type() != JsonType::Object) return false;
    AoratonSession parsed;
    parsed.access_token = value.string_at("access_token");
    parsed.refresh_token = value.string_at("refresh_token");
    const JsonValue* user = value.find("user");
    if (user) {
        parsed.user_id = user->string_at("id");
        parsed.email = user->string_at("email");
        if (const JsonValue* metadata = user->find("user_metadata")) {
            parsed.display_name = metadata->string_at("full_name");
            if (parsed.display_name.empty()) parsed.display_name = metadata->string_at("name");
        }
    }
    const JsonValue* expires_in = value.find("expires_in");
    const JsonValue* expires_at = value.find("expires_at");
    if (expires_in && expires_in->as_int(0) > 0) {
        parsed.expires_at_unix = now_unix + expires_in->as_int(0);
    } else if (expires_at && expires_at->as_int(0) > 0) {
        parsed.expires_at_unix = expires_at->as_int(0);
    }
    if (parsed.access_token.empty() || parsed.refresh_token.empty() || parsed.user_id.empty() ||
        parsed.expires_at_unix <= 0) return false;
    parsed.display_name = utf8_truncate(parsed.display_name, 64);
    parsed.email = utf8_truncate(parsed.email, 160);
    session = std::move(parsed);
    return true;
}

bool aoraton_parse_rooms(std::string_view json, std::vector<AoratonRemoteRoom>& rooms) {
    JsonValue rows;
    if (!parse_rows(json, rows)) return false;
    std::vector<AoratonRemoteRoom> parsed;
    for (const auto& row : rows.items()) {
        AoratonRemoteRoom room;
        room.room_key = member_text(row, "room_key");
        if (room.room_key.empty()) continue;
        room.name = member_text(row, "display_name");
        if (room.name.empty()) room.name = member_text(row, "room_name");
        room.expires_at = member_text(row, "expires_at");
        if (const JsonValue* e2ee = row.find("e2ee")) room.e2ee = e2ee->as_bool(false);
        if (const JsonValue* notes = row.find("is_notes")) room.is_notes = notes->as_bool(false);
        parsed.push_back(std::move(room));
    }
    rooms = std::move(parsed);
    return true;
}

bool aoraton_parse_member_keys(std::string_view json, std::vector<std::string>& room_keys) {
    JsonValue rows;
    if (!parse_rows(json, rows)) return false;
    std::vector<std::string> parsed;
    for (const auto& row : rows.items()) {
        auto key = member_text(row, "room_key");
        if (!key.empty() && std::find(parsed.begin(), parsed.end(), key) == parsed.end()) {
            parsed.push_back(std::move(key));
        }
    }
    room_keys = std::move(parsed);
    return true;
}

bool aoraton_parse_overview(std::string_view json, std::vector<AoratonOverview>& overview) {
    JsonValue rows;
    if (!parse_rows(json, rows)) return false;
    std::vector<AoratonOverview> parsed;
    for (const auto& row : rows.items()) {
        AoratonOverview item;
        item.room_key = member_text(row, "room_key");
        if (item.room_key.empty()) continue;
        item.last_message_at = member_text(row, "last_message_at");
        item.last_text = member_text(row, "last_text");
        item.last_username = member_text(row, "last_username");
        item.last_type = member_text(row, "last_type");
        if (item.last_type.empty()) item.last_type = "text";
        if (const JsonValue* unread = row.find("unread_count")) {
            const auto count = unread->as_int(0);
            item.unread = count <= 0 ? 0U : static_cast<std::uint32_t>(std::min<std::int64_t>(count, 999));
        }
        parsed.push_back(std::move(item));
    }
    overview = std::move(parsed);
    return true;
}

bool aoraton_parse_messages(std::string_view json, std::vector<AoratonRemoteMessage>& messages) {
    JsonValue rows;
    if (!parse_rows(json, rows)) return false;
    std::vector<AoratonRemoteMessage> parsed;
    for (const auto& row : rows.items()) {
        AoratonRemoteMessage message;
        message.id = member_text(row, "id");
        message.room_key = member_text(row, "room_key");
        if (message.id.empty() || message.room_key.empty()) continue;
        message.uid = member_text(row, "uid");
        message.username = utf8_truncate(member_text(row, "username"), 64);
        message.text = member_text(row, "text");
        message.type = member_text(row, "type");
        if (message.type.empty()) message.type = "text";
        message.created_at = member_text(row, "created_at");
        message.client_msg_id = member_text(row, "client_msg_id");
        parsed.push_back(std::move(message));
    }
    messages = std::move(parsed);
    return true;
}

bool aoraton_parse_vault_meta(std::string_view json, AoratonVaultMeta& meta, bool& configured) {
    JsonValue rows;
    if (!parse_rows(json, rows)) return false;
    configured = false;
    if (rows.items().empty()) return true;
    const JsonValue& row = rows.items()[0];
    AoratonVaultMeta parsed;
    if (!aoraton_base64url_decode(member_text(row, "kdf_salt"), parsed.salt) || parsed.salt.empty()) return false;
    if (const JsonValue* iterations = row.find("kdf_iters")) {
        const auto value = iterations->as_int(0);
        if (value > 0) parsed.iterations = static_cast<std::uint32_t>(std::min<std::int64_t>(value, 0xFFFFFFFF));
    }
    parsed.check_blob = member_text(row, "check_blob");
    if (parsed.check_blob.empty()) return false;
    meta = std::move(parsed);
    configured = true;
    return true;
}

bool aoraton_parse_vault_rows(std::string_view json, std::vector<AoratonVaultRow>& rows) {
    JsonValue values;
    if (!parse_rows(json, values)) return false;
    std::vector<AoratonVaultRow> parsed;
    for (const auto& row : values.items()) {
        AoratonVaultRow item;
        item.room_key = member_text(row, "room_key");
        item.blob = member_text(row, "blob");
        if (!item.room_key.empty() && !item.blob.empty()) parsed.push_back(std::move(item));
    }
    rows = std::move(parsed);
    return true;
}

bool aoraton_parse_insert_ack(std::string_view json, std::string& id, std::string& created_at) {
    JsonValue value;
    if (!parse_json(json, value)) return false;
    const JsonValue* row = &value;
    if (value.type() == JsonType::Array) {
        if (value.items().size() != 1) return false;
        row = &value.items()[0];
    }
    if (row->type() != JsonType::Object) return false;
    id = row->string_at("id");
    created_at = row->string_at("created_at");
    return !id.empty();
}

bool aoraton_parse_invite(std::string_view raw, AoratonInvite& invite) {
    invite.clear();
    std::string_view text = raw;
    while (!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\r' ||
                             text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' ||
                             text.back() == '\t')) text.remove_suffix(1);
    if (text.empty() || text.size() > 4096) return false;
    const auto hash = text.find('#');
    if (hash != std::string_view::npos) {
        text.remove_prefix(hash + 1);
    } else if (const auto query = text.find('?'); query != std::string_view::npos) {
        text.remove_prefix(query + 1);
    }
    std::string k;
    bool ok = true;
    while (!text.empty() && ok) {
        const auto amp = text.find('&');
        const std::string_view pair = text.substr(0, amp);
        text = amp == std::string_view::npos ? std::string_view{} : text.substr(amp + 1);
        const auto equals = pair.find('=');
        if (equals == std::string_view::npos) continue;
        std::string name;
        std::string value;
        if (!form_decode(pair.substr(0, equals), name) || !form_decode(pair.substr(equals + 1), value)) {
            ok = false;
            break;
        }
        if (name == "room") invite.room_name = utf8_truncate(value, 160);
        else if (name == "key") invite.room_key = value;
        else if (name == "k") k = std::move(value);
        wipe(value);
    }
    std::vector<std::uint8_t> bytes;
    ok = ok && !invite.room_key.empty() && invite.room_key.size() <= 128 && aoraton_base64url_decode(k, bytes) &&
         bytes.size() == invite.rk.size();
    if (ok) std::copy(bytes.begin(), bytes.end(), invite.rk.begin());
    wipe(bytes);
    wipe(k);
    if (!ok) invite.clear();
    return ok;
}

AoratonVaultResult aoraton_open_vault(const AoratonVaultMeta& meta,
                                      const std::vector<AoratonVaultRow>& rows,
                                      const std::string& passphrase,
                                      IAoratonAccountCrypto& crypto,
                                      std::vector<AoratonUnlockedKey>& keys) {
    keys.clear();
    if (meta.check_blob.empty() || meta.salt.empty()) return AoratonVaultResult::NotConfigured;
    if (meta.iterations == 0 || meta.iterations > kMaxVaultIterations) return AoratonVaultResult::Corrupt;
    std::array<std::uint8_t, 32> vault_key{};
    if (!crypto.pbkdf2_sha256(passphrase, meta.salt, meta.iterations, vault_key)) {
        return AoratonVaultResult::CryptoFailure;
    }
    std::string check;
    if (!crypto.aes_gcm_open(vault_key, meta.check_blob, check) || check != kVaultCheckPlaintext) {
        wipe(vault_key);
        wipe(check);
        return AoratonVaultResult::WrongPassphrase;
    }
    for (const auto& row : rows) {
        std::string plain;
        if (!crypto.aes_gcm_open(vault_key, row.blob, plain)) continue;
        JsonValue payload;
        std::vector<std::uint8_t> rk;
        if (parse_json(plain, payload) && aoraton_base64url_decode(payload.string_at("rk"), rk) && rk.size() == 32 &&
            std::none_of(keys.begin(), keys.end(), [&](const AoratonUnlockedKey& key) {
                return key.room_key == row.room_key;
            })) {
            AoratonUnlockedKey key;
            key.room_key = row.room_key;
            std::copy(rk.begin(), rk.end(), key.rk.begin());
            keys.push_back(std::move(key));
        }
        wipe(rk);
        wipe(plain);
    }
    wipe(vault_key);
    return AoratonVaultResult::Ok;
}

}  // namespace aura

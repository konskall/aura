#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aura {

// The owner's own aoraton.com account, reached exactly the way a new browser
// would (Supabase PKCE sign-in, PostgREST rows, the opt-in key vault). These
// are pure functions over strings; the network, the browser and key storage
// belong to the platform (see aoraton_account_worker.hpp).

class IAoratonAccountCrypto {
public:
    virtual ~IAoratonAccountCrypto() = default;
    virtual bool random_bytes(std::uint8_t* destination, std::size_t size) = 0;
    virtual bool sha256(const std::uint8_t* data, std::size_t size, std::array<std::uint8_t, 32>& digest) = 0;
    virtual bool pbkdf2_sha256(const std::string& passphrase,
                               const std::vector<std::uint8_t>& salt,
                               std::uint32_t iterations,
                               std::array<std::uint8_t, 32>& key) = 0;
    // Opens `v2:<b64url iv>:<b64url ciphertext+tag>` sealed directly under
    // `key` (AES-256-GCM, no AAD), as the browser's vault does.
    virtual bool aes_gcm_open(const std::array<std::uint8_t, 32>& key,
                              const std::string& wire,
                              std::string& plaintext) = 0;
};

// Answering inco mentions in this device's own messages (aoraton_inco.hpp).
struct AoratonIncoConfig {
    // The owner's one-time opt-in: the question and the recent conversation
    // leave end-to-end encryption for the edge function and Google's Gemini.
    bool enabled{false};
    // Minutes east of UTC at that moment (local time for inco, quota day).
    std::function<int(std::int64_t utc_unix)> utc_offset_minutes;
    // The city AURA knows (the saved weather city), or empty.
    std::function<std::string()> location_city;
    // One line per outcome: message ids and codes only, never any text.
    std::function<void(const std::string&)> log;
};

struct AoratonAccountConfig {
    std::string base_url;
    std::string anon_key;
    std::string redirect_url;
    AoratonIncoConfig inco;
};

struct AoratonSession {
    AoratonSession() = default;
    ~AoratonSession();
    AoratonSession(const AoratonSession&) = delete;
    AoratonSession& operator=(const AoratonSession&) = delete;
    AoratonSession(AoratonSession&& other) noexcept;
    AoratonSession& operator=(AoratonSession&& other) noexcept;

    std::string access_token;
    std::string refresh_token;
    std::string user_id;
    std::string email;
    std::string display_name;
    std::int64_t expires_at_unix{0};

    void clear() noexcept;
};

struct AoratonRemoteRoom {
    std::string room_key;
    std::string name;
    std::string expires_at;
    bool e2ee{false};
    bool is_notes{false};
    bool ai_enabled{false};
    std::string ai_avatar_url;
};

struct AoratonOverview {
    std::string room_key;
    std::string last_message_at;
    std::string last_text;
    std::string last_username;
    std::string last_type;
    std::uint32_t unread{0};
};

struct AoratonRemoteMessage {
    std::string id;
    std::string room_key;
    std::string uid;
    std::string username;
    std::string text;
    std::string type;
    std::string created_at;
    std::string client_msg_id;
    // Only in inco's history fetch: who a reply answers, and a poll (its
    // question and option texts are encrypted like a message body).
    std::string reply_to_id;
    std::string reply_to_username;
    bool has_poll{false};
    std::string poll_question;
    std::vector<std::string> poll_options;
};

struct AoratonVaultMeta {
    std::vector<std::uint8_t> salt;
    std::uint32_t iterations{600000};
    std::string check_blob;
};

struct AoratonVaultRow {
    std::string room_key;
    std::string blob;
};

struct AoratonUnlockedKey {
    AoratonUnlockedKey() = default;
    ~AoratonUnlockedKey();
    AoratonUnlockedKey(const AoratonUnlockedKey&) = delete;
    AoratonUnlockedKey& operator=(const AoratonUnlockedKey&) = delete;
    AoratonUnlockedKey(AoratonUnlockedKey&& other) noexcept;
    AoratonUnlockedKey& operator=(AoratonUnlockedKey&& other) noexcept;

    std::string room_key;
    std::array<std::uint8_t, 32> rk{};

    void clear() noexcept;
};

struct AoratonInvite {
    AoratonInvite() = default;
    ~AoratonInvite();
    AoratonInvite(const AoratonInvite&) = delete;
    AoratonInvite& operator=(const AoratonInvite&) = delete;

    std::string room_name;
    std::string room_key;
    std::array<std::uint8_t, 32> rk{};

    void clear() noexcept;
};

enum class AoratonVaultResult { Ok, NotConfigured, WrongPassphrase, Corrupt, CryptoFailure };

// 32 random bytes as unpadded base64url (43 characters); empty on failure.
std::string aoraton_pkce_verifier(IAoratonAccountCrypto& crypto);
// base64url(SHA-256(verifier)), RFC 7636 S256; empty on failure.
std::string aoraton_pkce_challenge(const std::string& verifier, IAoratonAccountCrypto& crypto);
// RFC 3986 percent-encoding; only unreserved characters stay literal.
std::string aoraton_url_encode(std::string_view value);
std::string aoraton_authorize_url(const AoratonAccountConfig& config, const std::string& challenge);
// `temp_aura` + 32 lowercase hex characters (the web client's temp_ prefix); empty on failure.
std::string aoraton_new_client_message_id(IAoratonAccountCrypto& crypto);

std::string aoraton_rooms_owned_path(const std::string& user_id);
std::string aoraton_member_keys_path(const std::string& user_id);
std::string aoraton_rooms_by_keys_path(const std::vector<std::string>& room_keys);
std::string aoraton_messages_path(const std::string& room_key);
// The newest 50 rows with replies and polls, for the conversation inco gets.
std::string aoraton_inco_history_path(const std::string& room_key);
std::string aoraton_overview_body(const std::vector<std::pair<std::string, std::string>>& room_since);
std::string aoraton_insert_body(const std::string& room_key,
                                const std::string& user_id,
                                const std::string& username,
                                const std::string& wire,
                                const std::string& client_message_id);

bool aoraton_parse_session(std::string_view json, std::int64_t now_unix, AoratonSession& session);
bool aoraton_parse_rooms(std::string_view json, std::vector<AoratonRemoteRoom>& rooms);
bool aoraton_parse_member_keys(std::string_view json, std::vector<std::string>& room_keys);
bool aoraton_parse_overview(std::string_view json, std::vector<AoratonOverview>& overview);
bool aoraton_parse_messages(std::string_view json, std::vector<AoratonRemoteMessage>& messages);
// An empty array means the account has no vault; `configured` is then false.
bool aoraton_parse_vault_meta(std::string_view json, AoratonVaultMeta& meta, bool& configured);
bool aoraton_parse_vault_rows(std::string_view json, std::vector<AoratonVaultRow>& rows);
bool aoraton_parse_insert_ack(std::string_view json, std::string& id, std::string& created_at);

// `#room=<name>&key=<room_key>&k=<b64url 32 bytes>` (a whole link, a fragment
// or a query string). A link without a 32-byte key is rejected.
bool aoraton_parse_invite(std::string_view raw, AoratonInvite& invite);

// Rows that fail to open are skipped; the check blob decides WrongPassphrase.
AoratonVaultResult aoraton_open_vault(const AoratonVaultMeta& meta,
                                      const std::vector<AoratonVaultRow>& rows,
                                      const std::string& passphrase,
                                      IAoratonAccountCrypto& crypto,
                                      std::vector<AoratonUnlockedKey>& keys);

}  // namespace aura

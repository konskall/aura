#pragma once

#include "aura/aoraton_account.hpp"
#include "aura/aoraton_crypto.hpp"
#include "aura/aoraton_real.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aura {

struct AoratonHttpRequest {
    std::string method;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

// status 0 means the request never got an HTTP answer (DNS, TLS, timeout).
struct AoratonHttpResponse {
    int status{0};
    std::string body;
};

class IAoratonHttp {
public:
    virtual ~IAoratonHttp() = default;
    virtual AoratonHttpResponse send(const AoratonHttpRequest& request) = 0;
};

enum class AoratonCallbackStatus { Pending, Received, Failed };

// The platform's way back from the Google sign-in page: the simulator opens
// the PC browser and listens on 127.0.0.1; a phone could do the same later.
class IAoratonSignInChannel {
public:
    virtual ~IAoratonSignInChannel() = default;
    virtual bool open(const std::string& authorize_url) = 0;
    virtual AoratonCallbackStatus poll(std::string& code) = 0;
    virtual void close() = 0;
};

struct AoratonStoredSecrets {
    AoratonStoredSecrets() = default;
    ~AoratonStoredSecrets();
    AoratonStoredSecrets(const AoratonStoredSecrets&) = delete;
    AoratonStoredSecrets& operator=(const AoratonStoredSecrets&) = delete;
    AoratonStoredSecrets(AoratonStoredSecrets&&) noexcept = default;
    AoratonStoredSecrets& operator=(AoratonStoredSecrets&&) noexcept = default;

    std::string refresh_token;
    std::vector<AoratonUnlockedKey> keys;

    void clear() noexcept;
};

class IAoratonSecretStore {
public:
    virtual ~IAoratonSecretStore() = default;
    virtual bool load(AoratonStoredSecrets& secrets) = 0;
    virtual bool save(const AoratonStoredSecrets& secrets) = 0;
    virtual bool erase() = 0;
};

// Runs on one platform thread: `step` handles bounded mailbox commands, the
// pending sign-in and the 10 s poll, blocking on HTTP as it goes. It never logs
// and never puts a token, passphrase or room key into an event.
class AoratonAccountWorker final {
public:
    static constexpr std::int64_t kSignInTimeoutMs = 290000;
    static constexpr std::uint32_t kSignInSeconds = 300;
    static constexpr std::int64_t kPollIntervalMs = 10000;
    static constexpr std::int64_t kRetryIntervalMs = 30000;

    AoratonAccountWorker(AoratonMailbox& mailbox,
                         AoratonAccountConfig config,
                         IAoratonHttp& http,
                         IAoratonAccountCrypto& account_crypto,
                         IAoratonCryptoV2& message_crypto,
                         IAoratonSecretStore& store,
                         IAoratonSignInChannel& channel);
    ~AoratonAccountWorker();

    AoratonAccountWorker(const AoratonAccountWorker&) = delete;
    AoratonAccountWorker& operator=(const AoratonAccountWorker&) = delete;

    void start(std::int64_t now_ms, std::int64_t now_unix);
    void step(std::int64_t now_ms, std::int64_t now_unix);

private:
    enum class RestOutcome { Ok, Revoked, Transient };
    struct RestResult {
        RestOutcome outcome{RestOutcome::Transient};
        AoratonHttpResponse response;
    };
    struct RoomTrack {
        bool seen{false};
        std::string marker;
        std::string last_seen_at;
        std::string my_username;
        std::vector<std::string> seen_ids;
    };
    struct PendingSend {
        std::string client_message_id;
        std::string room_key;
    };
    enum class RefreshResult { Ok, Revoked, Transient };

    void handle(AoratonCommand& command);
    void begin_sign_in();
    void poll_sign_in();
    void finish_sign_in(AoratonSession session);
    RefreshResult refresh_session();
    RestResult rest(const std::string& method, const std::string& path, const std::string& body,
                    const char* prefer = nullptr);
    AoratonHttpResponse auth_post(const std::string& path, const std::string& body, bool bearer);
    void session_revoked();
    void connection_lost();
    void connect();
    void sign_out();
    bool refresh_rooms();
    bool fetch_history(const std::string& room_key);
    void deliver_new_messages(const std::string& room_key);
    void send_text(AoratonCommand& command);
    void unlock_vault(std::string& passphrase);
    void add_invite(std::string& invite);
    void keys_changed(AoratonKeyState state);
    const AoratonUnlockedKey* key_for(const std::string& room_key) const;
    bool decrypt(const std::string& room_key, const std::string& wire, std::string& plaintext);
    bool persist();
    bool is_member(const std::string& room_key) const;
    void emit(AoratonEvent event);
    std::string now_iso() const;

    AoratonMailbox& mailbox_;
    AoratonAccountConfig config_;
    IAoratonHttp& http_;
    IAoratonAccountCrypto& account_crypto_;
    IAoratonCryptoV2& message_crypto_;
    IAoratonSecretStore& store_;
    IAoratonSignInChannel& channel_;

    AoratonSession session_;
    AoratonStoredSecrets secrets_;
    bool signed_in_{false};
    bool connected_{false};
    bool reconnecting_{false};
    bool signing_in_{false};
    std::string verifier_;
    std::int64_t sign_in_started_ms_{0};
    std::int64_t now_ms_{0};
    std::int64_t now_unix_{0};
    std::int64_t next_poll_ms_{0};
    std::int64_t next_retry_ms_{0};
    std::vector<AoratonRemoteRoom> rooms_;
    std::map<std::string, RoomTrack> tracks_;
    std::vector<PendingSend> pending_sends_;
};

}  // namespace aura

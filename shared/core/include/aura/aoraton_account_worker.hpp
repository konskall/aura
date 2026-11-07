#pragma once

#include "aura/aoraton_account.hpp"
#include "aura/aoraton_attachment_cache.hpp"
#include "aura/aoraton_crypto.hpp"
#include "aura/aoraton_inco.hpp"
#include "aura/aoraton_real.hpp"
#include "aura/aoraton_realtime.hpp"
#include "aura/voice_decoder.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
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
    // How long to wait for the answer; 0 keeps the client's default. inco's
    // edge function may retry Gemini for up to a minute.
    std::uint32_t timeout_ms{0};
    // The largest answer the request may bring (attachments); 0 keeps the
    // client's default for JSON.
    std::size_t max_response_bytes{0};
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
// the PC browser and listens on 127.0.0.1; the device hands the page to the
// owner's PC or phone (AoratonDeviceLink).
class IAoratonSignInChannel {
public:
    virtual ~IAoratonSignInChannel() = default;
    virtual bool open(const std::string& authorize_url) = 0;
    virtual AoratonCallbackStatus poll(std::string& code) = 0;
    virtual void close() = 0;
    // Where Google's page returns, asked as each sign-in starts. Empty: the
    // configured address (the PC's loopback). The device's phone way returns
    // to the device's own address on the owner's Wi-Fi.
    virtual std::string redirect_url() const { return {}; }
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
// pending sign-in, realtime invalidations and a 10 s fallback poll. HTTP is
// blocking on this worker only. It never puts
// a token, passphrase or room key into an event. Its only log is inco's
// outcome line (ids and codes) through AoratonIncoConfig::log.
class AoratonAccountWorker final {
public:
    static constexpr std::int64_t kSignInTimeoutMs = 290000;
    static constexpr std::uint32_t kSignInSeconds = 300;
    static constexpr std::int64_t kPollIntervalMs = 10000;
    static constexpr std::int64_t kRetryIntervalMs = 30000;
    // While saving power (screen dark on battery) realtime is off and the
    // overview is checked at least this often, whatever the polling setting.
    static constexpr std::int64_t kPowerSavingPollMs = 300000;

    AoratonAccountWorker(AoratonMailbox& mailbox,
                         AoratonAccountConfig config,
                         IAoratonHttp& http,
                         IAoratonAccountCrypto& account_crypto,
                         IAoratonCryptoV2& message_crypto,
                         IAoratonSecretStore& store,
                         IAoratonSignInChannel& channel,
                         IAoratonRealtime* realtime = nullptr,
                         IVoiceDecoder* voice_decoder = nullptr,
                         IAoratonAttachmentCache* attachment_cache = nullptr);
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
                    const char* prefer = nullptr, std::uint32_t timeout_ms = 0);
    AoratonHttpResponse auth_post(const std::string& path, const std::string& body, bool bearer);
    void session_revoked();
    void connection_lost();
    void connect();
    void configure_realtime();
    void realtime_status(bool active);
    void set_power_saving(bool saving);
    std::int64_t poll_interval_ms() const;
    void sign_out();
    bool refresh_rooms();
    bool fetch_history(const std::string& room_key);
    void deliver_new_messages(const std::string& room_key);
    void send_text(AoratonCommand& command);
    void answer_inco(const std::string& room_key, const std::string& row_id, const std::string& username,
                     const std::string& question);
    bool inco_history(const std::string& room_key, const std::string& row_id, const std::string& username,
                      const std::string& question, std::vector<AoratonIncoTurn>& turns);
    bool write_inco(const std::string& room_key, const std::string& text, const AoratonIncoQuote* quote,
                    const std::vector<AoratonIncoSource>& sources);
    void inco_log(const std::string& line) const;
    int utc_offset_minutes() const;
    const AoratonRemoteRoom* room_for(const std::string& room_key) const;
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
    IAoratonRealtime* realtime_{nullptr};
    IVoiceDecoder* voice_decoder_{nullptr};
    IAoratonAttachmentCache* attachment_cache_{nullptr};  // the microSD card, when there is one
    bool realtime_active_{false};
    bool power_saving_{false};
    AoratonSyncSettings sync_settings_;
    bool sync_requested_{false};
    std::int64_t next_auth_retry_ms_{0};

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
    bool inco_busy_{false};
    std::string inco_quota_day_;  // the local day inco ran out of questions

    // Files of the messages delivered so far (the newest 100), and the
    // photos whose thumbnails are still to fetch, one per step.
    struct AttachmentRef {
        std::string room_key;
        AoratonRemoteAttachment file;
    };
    bool build_message(const std::string& room_key, const AoratonRemoteMessage& row, bool mine,
                       AoratonMessage& message);
    void queue_thumbnails(const std::vector<AoratonMessage>& messages);
    void load_next_thumbnail();
    void open_attachment(const AoratonCommand& command);
    void send_voice(AoratonCommand& command);
    bool fetch_attachment(const AttachmentRef& ref, std::vector<std::uint8_t>& plaintext, std::string& error);
    std::map<std::string, AttachmentRef> attachments_;
    std::deque<std::string> attachment_order_;
    std::deque<std::string> thumbnail_queue_;
    std::vector<std::string> thumbnails_fetched_;
};

}  // namespace aura

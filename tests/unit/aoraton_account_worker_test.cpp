#include "aura/aoraton_account_worker.hpp"
#include "aura/aoraton_real.hpp"
#include "aura/json_reader.hpp"
#include "aura/text_util.hpp"
#include "aura/windows_aoraton_crypto.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef AURA_FIXTURE_DIR
#error "AURA_FIXTURE_DIR must point at tests/fixtures"
#endif

namespace {

using namespace aura;

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::string read_fixture(const char* name) {
    std::ifstream file(std::string(AURA_FIXTURE_DIR) + "/" + name, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

constexpr char kBase[] = "https://fake.supabase.test";
constexpr char kAnon[] = "anon-key";
constexpr char kUser[] = "user-1";
constexpr char kAlpha[] = "v2.QUFBQUFBQUFBQUFBQUFBQUFB";  // key 0x11.. in the vault fixture
constexpr char kBeta[] = "demo_room_1234";               // key 0x22.. in the vault fixture
constexpr char kLocked[] = "v2.TG9ja2VkTG9ja2VkTG9ja2Vk";

std::array<std::uint8_t, 32> filled(std::uint8_t value) {
    std::array<std::uint8_t, 32> bytes{};
    bytes.fill(value);
    return bytes;
}

// A small in-memory stand-in for the aoraton Supabase project: the endpoints,
// headers, rotation and failure modes the worker relies on. Synthetic data only.
class FakeSupabase final : public IAoratonHttp {
public:
    struct Row {
        std::string id, room_key, uid, username, text, type, created_at, client_msg_id;
    };
    enum class InsertMode { Ok, LostResponse, Reject };

    FakeSupabase() {
        rooms = {{kAlpha, "DEMO Alpha", true}, {kBeta, "DEMO Beta", false}, {kLocked, "DEMO Locked", false}};
    }

    std::string wire(const char* room_key, const std::string& text) {
        AoratonRoomKey key(filled(std::string(room_key) == kAlpha ? 0x11 : (std::string(room_key) == kBeta ? 0x22 : 0x33)));
        std::string out;
        AoratonCryptoError error = AoratonCryptoError::None;
        expect(crypto.encrypt_text(text, key, out, error), "fake encrypts");
        return out;
    }

    void add(const char* room_key, const char* uid, const char* username, const std::string& text,
             const char* type = "text", const std::string& client = {}) {
        rows.push_back({"m-" + std::to_string(++next_id), room_key, uid, username,
                        std::string(type) == "text" ? wire(room_key, text) : std::string{}, type, stamp(), client});
    }

    std::string stamp() {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "2026-10-23T10:%02d:%02d.5+00:00", seconds / 60, seconds % 60);
        ++seconds;
        return buffer;
    }

    std::string issue_session() {
        ++generation;
        access = "at-" + std::to_string(generation);
        refresh = "rt-" + std::to_string(generation);
        return std::string(R"({"access_token":")") + access + R"(","token_type":"bearer","expires_in":3600,)"
               R"("refresh_token":")" + refresh + R"(","user":{"id":")" + kUser +
               R"(","email":"owner@example.com","user_metadata":{"full_name":"Demo Owner"}}})";
    }

    static std::string header(const AoratonHttpRequest& request, const char* name) {
        for (const auto& item : request.headers) {
            if (item.first == name) return item.second;
        }
        return {};
    }

    AoratonHttpResponse send(const AoratonHttpRequest& request) override {
        ++requests;
        if (header(request, "apikey") != kAnon) return {400, "{}"};
        const std::string base = kBase;
        if (request.url.compare(0, base.size(), base) != 0) return {404, "{}"};
        const std::string path = request.url.substr(base.size());
        if (network_down) return {0, {}};
        if (path == "/auth/v1/token?grant_type=pkce") {
            JsonValue body;
            expect(parse_json(request.body, body), "pkce body is JSON");
            const std::string verifier = body.string_at("code_verifier");
            WindowsAoratonAccountCrypto account;
            if (body.string_at("auth_code") != "code-1" || aoraton_pkce_challenge(verifier, account) != challenge) {
                return {400, R"({"error":"invalid_grant"})"};
            }
            return {200, issue_session()};
        }
        if (path == "/auth/v1/token?grant_type=refresh_token") {
            ++refreshes;
            JsonValue body;
            expect(parse_json(request.body, body), "refresh body is JSON");
            if (revoked || body.string_at("refresh_token") != refresh) return {400, R"({"error":"invalid_grant"})"};
            return {200, issue_session()};
        }
        if (path.rfind("/auth/v1/logout", 0) == 0) {
            ++logouts;
            return {204, {}};
        }
        if (header(request, "Authorization") != "Bearer " + access || access_expired) {
            access_expired = false;
            return {401, R"({"message":"JWT expired"})"};
        }
        if (request.method == "GET" && path.rfind("/rest/v1/rooms?select=*&created_by=eq.user-1", 0) == 0) {
            return {200, rooms_json(true, {})};
        }
        if (request.method == "GET" && path.rfind("/rest/v1/subscribers?select=room_key&uid=eq.user-1", 0) == 0) {
            std::string out = "[";
            for (const auto& room : rooms) {
                if (room.owned) continue;
                if (out.size() > 1) out += ",";
                out += R"({"room_key":")" + room.key + R"("})";
            }
            return {200, out + "]"};
        }
        if (request.method == "GET" && path.rfind("/rest/v1/rooms?select=*&room_key=in.(", 0) == 0) {
            return {200, rooms_json(false, path)};
        }
        if (request.method == "POST" && path == "/rest/v1/rpc/room_overview") return {200, overview(request.body)};
        if (request.method == "GET" && path.rfind("/rest/v1/messages?", 0) == 0) return {200, messages(path)};
        if (request.method == "POST" && path == "/rest/v1/messages?select=id,created_at") {
            if (header(request, "Prefer") != "return=representation") return {400, "{}"};
            if (insert_mode == InsertMode::Reject) return {403, R"({"message":"denied"})"};
            JsonValue body;
            expect(parse_json(request.body, body), "insert body is JSON");
            expect(body.string_at("type") == "text" && body.string_at("uid") == kUser &&
                       body.find("avatar_url") && body.find("avatar_url")->is_null(),
                   "insert carries the web client's fields");
            last_insert_username = body.string_at("username");
            Row row{"m-" + std::to_string(++next_id), body.string_at("room_key"), kUser, last_insert_username,
                    body.string_at("text"), "text", stamp(), body.string_at("client_msg_id")};
            rows.push_back(row);
            if (insert_mode == InsertMode::LostResponse) return {0, {}};
            return {201, R"([{"id":")" + row.id + R"(","created_at":")" + row.created_at + R"("}])"};
        }
        if (request.method == "GET" && path.rfind("/rest/v1/key_vault_meta?", 0) == 0) {
            if (!vault) return {200, "[]"};
            JsonValue fixture;
            expect(parse_json(read_fixture("aoraton_vault_vectors.json"), fixture), "fixture");
            return {200, R"([{"kdf_salt":")" + fixture.string_at("kdf_salt") + R"(","kdf_iters":)" +
                             fixture.string_at("kdf_iters") + R"(,"check_blob":")" + fixture.string_at("check_blob") +
                             R"("}])"};
        }
        if (request.method == "GET" && path.rfind("/rest/v1/key_vault?", 0) == 0) {
            JsonValue fixture;
            expect(parse_json(read_fixture("aoraton_vault_vectors.json"), fixture), "fixture");
            std::string out = "[";
            for (const auto& row : fixture.find("rows")->items()) {
                if (out.size() > 1) out += ",";
                out += R"({"room_key":")" + row.string_at("room_key") + R"(","blob":")" + row.string_at("blob") +
                       R"("})";
            }
            return {200, out + "]"};
        }
        return {404, R"({"message":"unexpected"})"};
    }

    std::string rooms_json(bool owned, const std::string& path) {
        std::string out = "[";
        for (const auto& room : rooms) {
            if (owned != room.owned) continue;
            if (!owned && path.find("%22" + aoraton_url_encode(room.key) + "%22") == std::string::npos) continue;
            if (out.size() > 1) out += ",";
            out += R"({"room_key":")" + room.key + R"(","room_name":")" + json_escape(room.name) +
                   R"(","display_name":null,"e2ee":true,"expires_at":null})";
        }
        return out + "]";
    }

    std::string overview(const std::string& body_text) {
        JsonValue body;
        expect(parse_json(body_text, body), "overview body is JSON");
        std::string out = "[";
        for (const auto& item : body.find("p_items")->items()) {
            const std::string key = item.string_at("room_key");
            const std::string since = item.string_at("since");
            const Row* last = nullptr;
            int unread = 0;
            for (const auto& row : rows) {
                if (row.room_key != key) continue;
                last = &row;
                if (row.uid != kUser && (since.empty() || row.created_at > since)) ++unread;
            }
            if (out.size() > 1) out += ",";
            out += R"({"room_key":")" + key + R"(","unread_count":)" + std::to_string(unread);
            if (last) {
                out += R"(,"last_message_at":")" + last->created_at + R"(","last_text":")" + last->text +
                       R"(","last_username":")" + json_escape(last->username) + R"(","last_type":")" + last->type +
                       R"("})";
            } else {
                out += R"(,"last_message_at":null,"last_text":null,"last_username":null,"last_type":null})";
            }
        }
        return out + "]";
    }

    std::string messages(const std::string& path) {
        std::string out = "[";
        int count = 0;
        for (auto row = rows.rbegin(); row != rows.rend() && count < 50; ++row) {
            if (path.find("room_key=eq." + aoraton_url_encode(row->room_key) + "&") == std::string::npos) continue;
            if (out.size() > 1) out += ",";
            out += R"({"id":")" + row->id + R"(","room_key":")" + row->room_key + R"(","uid":")" + row->uid +
                   R"(","username":")" + json_escape(row->username) + R"(","text":")" + row->text +
                   R"(","type":")" + row->type + R"(","created_at":")" + row->created_at + R"(","client_msg_id":)" +
                   (row->client_msg_id.empty() ? std::string("null") : "\"" + row->client_msg_id + "\"") + "}";
            ++count;
        }
        return out + "]";
    }

    struct Room {
        std::string key;
        std::string name;
        bool owned;
    };
    WindowsAoratonCryptoV2 crypto;
    std::vector<Room> rooms;
    std::vector<Row> rows;
    std::string challenge;
    std::string access;
    std::string refresh;
    std::string last_insert_username;
    InsertMode insert_mode{InsertMode::Ok};
    bool network_down{false};
    bool revoked{false};
    bool access_expired{false};
    bool vault{true};
    int generation{0};
    int next_id{0};
    int seconds{0};
    int requests{0};
    int refreshes{0};
    int logouts{0};
};

class FakeChannel final : public IAoratonSignInChannel {
public:
    bool open(const std::string& authorize_url) override {
        url = authorize_url;
        opened = true;
        return true;
    }
    AoratonCallbackStatus poll(std::string& out) override {
        if (!code.empty()) {
            out = code;
            code.clear();
            return AoratonCallbackStatus::Received;
        }
        return AoratonCallbackStatus::Pending;
    }
    void close() override { opened = false; }
    std::string url;
    std::string code;
    bool opened{false};
};

class MemoryStore final : public IAoratonSecretStore {
public:
    bool load(AoratonStoredSecrets& secrets) override {
        if (!present) return false;
        secrets.refresh_token = refresh;
        secrets.keys.clear();
        for (const auto& item : keys) {
            AoratonUnlockedKey key;
            key.room_key = item.first;
            key.rk = item.second;
            secrets.keys.push_back(std::move(key));
        }
        return true;
    }
    bool save(const AoratonStoredSecrets& secrets) override {
        present = true;
        refresh = secrets.refresh_token;
        keys.clear();
        for (const auto& key : secrets.keys) keys.push_back({key.room_key, key.rk});
        ++saves;
        return true;
    }
    bool erase() override {
        present = false;
        refresh.clear();
        keys.clear();
        return true;
    }
    bool present{false};
    std::string refresh;
    std::vector<std::pair<std::string, std::array<std::uint8_t, 32>>> keys;
    int saves{0};
};

struct Rig {
    Rig()
        : service(mailbox, AoratonRuntimeMode::Live, [this] {
              return aoraton_new_client_message_id(account);
          }),
          worker(mailbox, {kBase, kAnon, "http://127.0.0.1:4779/aoraton/callback"}, server, account, messages, store,
                 channel) {}

    void pump(int rounds = 6) {
        for (int index = 0; index < rounds; ++index) {
            worker.step(now_ms, 1792760000);
            service.tick(16);
        }
    }
    void advance(std::int64_t ms) {
        now_ms += ms;
        pump();
    }
    void capture_challenge() {
        const auto at = channel.url.find("code_challenge=");
        server.challenge = channel.url.substr(at + 15, channel.url.find('&', at) - at - 15);
    }
    void sign_in() {
        expect(service.begin_pairing(), "sign-in starts");
        pump();
        expect(service.snapshot().pairing == AoratonPairingState::WaitingForBrowser &&
                   service.snapshot().pairing_seconds_remaining == 300,
               "waiting for the browser with a visible deadline");
        capture_challenge();
        channel.code = "code-1";
        pump();
    }
    std::size_t notifications() const { return service.snapshot().notifications.size(); }

    AoratonMailbox mailbox;
    FakeSupabase server;
    FakeChannel channel;
    MemoryStore store;
    WindowsAoratonAccountCrypto account;
    WindowsAoratonCryptoV2 messages;
    RealAoratonService service;
    AoratonAccountWorker worker;
    std::int64_t now_ms{1000};
};

const AoratonRoom* room(const AoratonSnapshot& snapshot, const char* id) {
    for (const auto& item : snapshot.rooms) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

std::string fixture_passphrase() {
    JsonValue fixture;
    expect(parse_json(read_fixture("aoraton_vault_vectors.json"), fixture), "fixture");
    return fixture.string_at("passphrase");
}

}  // namespace

int main() {
    const std::string passphrase = fixture_passphrase();

    // sign_in_happy_path, rooms_overview_and_locked_room, unlock_vault_then_rooms_unlocked
    {
        Rig rig;
        rig.server.add(kAlpha, "u-maria", "Maria", "DEMO καλημέρα");
        rig.worker.start(rig.now_ms, 1792760000);
        rig.pump();
        expect(rig.service.snapshot().pairing == AoratonPairingState::NotPaired, "no stored session: signed out");
        rig.sign_in();
        const auto& snap = rig.service.snapshot();
        expect(rig.channel.url.find("https://fake.supabase.test/auth/v1/authorize?provider=google&redirect_to=") == 0 &&
                   rig.channel.url.find("code_challenge_method=s256") != std::string::npos,
               "browser opened at the Supabase Google authorize URL with PKCE");
        expect(snap.pairing == AoratonPairingState::Paired && snap.account_label == "owner@example.com",
               "signed in as the account email");
        expect(snap.connection == AoratonConnectionState::Connected && !snap.realtime_active,
               "connected by polling, never claimed realtime");
        expect(rig.store.refresh == rig.server.refresh, "refresh token stored for restarts");
        expect(snap.rooms.size() == 3 && room(snap, kAlpha) && !room(snap, kAlpha)->key_available &&
                   room(snap, kAlpha)->preview.empty() && room(snap, kAlpha)->unread == 0,
               "member rooms listed; without keys they are locked with no preview");
        expect(snap.key_state == AoratonKeyState::Ready && snap.key_count == 0, "no keys yet");

        expect(rig.service.unlock_keys("wrong passphrase"), "unlock attempt queued");
        rig.pump();
        expect(rig.service.snapshot().key_state == AoratonKeyState::WrongPassphrase &&
                   rig.service.snapshot().key_count == 0,
               "vault_wrong_passphrase: reported, nothing restored");
        expect(rig.service.unlock_keys(passphrase), "unlock queued");
        rig.pump();
        const auto& unlocked = rig.service.snapshot();
        expect(unlocked.key_state == AoratonKeyState::Ready && unlocked.key_count == 2, "two room keys restored");
        expect(rig.store.keys.size() == 2, "restored keys stored");
        expect(room(unlocked, kAlpha)->key_available && room(unlocked, kAlpha)->preview == "DEMO καλημέρα" &&
                   room(unlocked, kBeta)->key_available && !room(unlocked, kLocked)->key_available,
               "unlocked rooms show a decrypted preview; the third stays locked");

        // History, polling and notifications.
        expect(rig.service.select_room(kAlpha), "open room");
        rig.pump();
        expect(rig.service.snapshot().messages.size() == 1 &&
                   rig.service.snapshot().messages[0].local_display_text == "DEMO καλημέρα" &&
                   rig.service.snapshot().messages[0].direction == AoratonDirection::Incoming,
               "history decrypted");
        rig.server.add(kBeta, "u-nikos", "Nikos", "DEMO νέο μήνυμα");
        rig.advance(AoratonAccountWorker::kPollIntervalMs);
        expect(rig.notifications() == 1 && rig.service.snapshot().notifications[0].room_id == kBeta,
               "incoming_after_baseline_notifies_once");
        rig.advance(AoratonAccountWorker::kPollIntervalMs);
        expect(rig.notifications() == 1, "a quiet poll does not notify again");
        expect(room(rig.service.snapshot(), kBeta)->unread == 1, "overview unread count shown");

        rig.server.add(kAlpha, kUser, "Owner in browser", "DEMO από τον browser");
        rig.advance(AoratonAccountWorker::kPollIntervalMs);
        expect(rig.notifications() == 1, "own_messages_from_other_devices do not notify");
        const auto& own = rig.service.snapshot().messages.back();
        expect(own.direction == AoratonDirection::Outgoing && own.delivery == AoratonDelivery::Acknowledged,
               "own message shown as sent");
        rig.server.add(kAlpha, "u-maria", "Maria", "", "image");
        rig.advance(AoratonAccountWorker::kPollIntervalMs);
        expect(rig.notifications() == 1, "non-text rows are not shown in v1");

        // Sending.
        expect(rig.service.prepare_send(kAlpha, "DEMO απάντηση") && rig.service.confirm_send(), "send confirmed");
        rig.pump();
        expect(rig.service.snapshot().messages.back().delivery == AoratonDelivery::Acknowledged,
               "send_acknowledged");
        expect(rig.server.last_insert_username == "Owner in browser", "sends under the name used in the room");
        expect(rig.server.rows.back().client_msg_id.rfind("temp_aura", 0) == 0, "client id format");

        rig.server.insert_mode = FakeSupabase::InsertMode::Reject;
        expect(rig.service.prepare_send(kAlpha, "DEMO rejected") && rig.service.confirm_send(), "second send");
        rig.pump();
        expect(rig.service.snapshot().messages.back().delivery == AoratonDelivery::Failed &&
                   rig.service.snapshot().last_error == "SEND_REJECTED",
               "send_rejected");

        rig.server.insert_mode = FakeSupabase::InsertMode::LostResponse;
        expect(rig.service.prepare_send(kAlpha, "DEMO uncertain") && rig.service.confirm_send(), "third send");
        rig.pump();
        const auto uncertain_id = rig.service.snapshot().messages.back().client_message_id;
        expect(rig.service.snapshot().messages.back().delivery == AoratonDelivery::Uncertain, "lost response");
        rig.server.insert_mode = FakeSupabase::InsertMode::Ok;
        const auto rows_before = rig.server.rows.size();
        rig.advance(AoratonAccountWorker::kPollIntervalMs);
        bool reconciled = false;
        for (const auto& message : rig.service.snapshot().messages) {
            if (message.client_message_id == uncertain_id) {
                reconciled = message.delivery == AoratonDelivery::Acknowledged;
            }
        }
        expect(reconciled && rig.server.rows.size() == rows_before, "uncertain_send_reconciles without a resend");
        expect(rig.service.snapshot().connection == AoratonConnectionState::Connected, "connection recovered");

        // Expired access token: one refresh, one retry, rotated token stored.
        const int refreshes = rig.server.refreshes;
        rig.server.access_expired = true;
        rig.advance(AoratonAccountWorker::kPollIntervalMs);
        expect(rig.server.refreshes == refreshes + 1 && rig.store.refresh == rig.server.refresh &&
                   rig.service.snapshot().pairing == AoratonPairingState::Paired,
               "401 refreshes once and keeps the rotated token");

        // Network loss is shown, not hidden.
        rig.server.network_down = true;
        rig.advance(AoratonAccountWorker::kPollIntervalMs);
        expect(rig.service.snapshot().connection == AoratonConnectionState::Reconnecting, "network loss shown");
        rig.server.network_down = false;
        rig.advance(AoratonAccountWorker::kPollIntervalMs);
        expect(rig.service.snapshot().connection == AoratonConnectionState::Connected, "reconnects by itself");

        // no_secret_in_events: nothing secret reached the UI snapshot.
        const auto& final_snapshot = rig.service.snapshot();
        std::string visible = final_snapshot.account_label + final_snapshot.last_error;
        for (const auto& item : final_snapshot.rooms) visible += item.display_name + item.preview;
        for (const auto& item : final_snapshot.messages) visible += item.local_display_text + item.author;
        expect(visible.find("at-") == std::string::npos && visible.find("rt-") == std::string::npos &&
                   visible.find(passphrase) == std::string::npos,
               "no token or passphrase in the snapshot");

        // revoked_session_requires_sign_in
        rig.server.revoked = true;
        rig.server.access_expired = true;
        rig.advance(AoratonAccountWorker::kPollIntervalMs);
        expect(rig.service.snapshot().pairing == AoratonPairingState::ReauthorizationRequired &&
                   rig.store.refresh.empty() && rig.store.keys.size() == 2,
               "revoked session needs a new sign-in; room keys are kept");
    }

    // restart_uses_stored_refresh and sign_out_erases_store
    {
        Rig rig;
        rig.sign_in();
        const std::string stored = rig.store.refresh;
        Rig restarted;
        restarted.server.generation = rig.server.generation;
        restarted.server.refresh = rig.server.refresh;
        restarted.store.present = true;
        restarted.store.refresh = stored;
        restarted.worker.start(restarted.now_ms, 1792760000);
        restarted.pump();
        expect(restarted.service.snapshot().pairing == AoratonPairingState::Paired &&
                   restarted.service.snapshot().connection == AoratonConnectionState::Connected,
               "a stored session signs in on start");
        expect(restarted.service.unpair(), "sign out");
        restarted.pump();
        expect(!restarted.store.present && restarted.server.logouts == 1 &&
                   restarted.service.snapshot().pairing == AoratonPairingState::NotPaired &&
                   restarted.service.snapshot().rooms.empty(),
               "sign out erases the store and the session");
    }

    // sign_in_times_out and a cancelled / rejected exchange
    {
        Rig rig;
        expect(rig.service.begin_pairing(), "sign-in starts");
        rig.pump();
        rig.advance(AoratonAccountWorker::kSignInTimeoutMs + 1);
        expect(rig.service.snapshot().pairing == AoratonPairingState::Error &&
                   rig.service.snapshot().last_error == "SIGN_IN_TIMEOUT" && !rig.channel.opened,
               "sign_in_times_out with the redirect hint code");
        expect(rig.service.begin_pairing(), "retry");
        rig.pump();
        rig.server.challenge = "not-the-real-challenge";
        rig.channel.code = "code-1";
        rig.pump();
        expect(rig.service.snapshot().last_error == "SIGN_IN_REJECTED" && !rig.store.present,
               "an exchange the server refuses stores nothing");
    }

    // invites: invalid, not a member, then a member room
    {
        Rig rig;
        rig.sign_in();
        expect(rig.service.add_invite_key("https://aoraton.com/#room=x&pin=1234"), "invite queued");
        rig.pump();
        expect(rig.service.snapshot().key_state == AoratonKeyState::InvalidInvite, "invalid invite reported");
        const auto k = aoraton_base64url_encode(filled(0x33).data(), 32);
        expect(rig.service.add_invite_key("https://aoraton.com/#room=x&key=v2.Tm90TWVtYmVy&k=" + k), "queued");
        rig.pump();
        expect(rig.service.snapshot().key_state == AoratonKeyState::NotMember && rig.store.keys.empty(),
               "invite_not_member keeps nothing");
        rig.server.add(kLocked, "u-maria", "Maria", "DEMO κλειδωμένο");
        expect(rig.service.add_invite_key(std::string("https://aoraton.com/#room=DEMO&key=") + kLocked + "&k=" + k),
               "member invite queued");
        rig.pump();
        expect(rig.service.snapshot().key_state == AoratonKeyState::Ready && rig.store.keys.size() == 1 &&
                   room(rig.service.snapshot(), kLocked)->key_available &&
                   room(rig.service.snapshot(), kLocked)->preview == "DEMO κλειδωμένο",
               "invite unlocks a room the account belongs to");
    }

    // A vault the account never set up.
    {
        Rig rig;
        rig.server.vault = false;
        rig.sign_in();
        expect(rig.service.unlock_keys("anything"), "queued");
        rig.pump();
        expect(rig.service.snapshot().key_state == AoratonKeyState::NotConfigured, "vault_not_configured");
    }

    std::cout << "aoraton account worker tests passed\n";
    return 0;
}

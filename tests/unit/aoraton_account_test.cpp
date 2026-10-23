#include "aura/aoraton_account.hpp"
#include "aura/aoraton_crypto.hpp"
#include "aura/json_reader.hpp"
#include "aura/windows_aoraton_crypto.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef AURA_FIXTURE_DIR
#error "AURA_FIXTURE_DIR must point at tests/fixtures"
#endif

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::string hex(const std::uint8_t* data, std::size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (std::size_t index = 0; index < size; ++index) {
        out.push_back(kHex[data[index] >> 4]);
        out.push_back(kHex[data[index] & 0x0F]);
    }
    return out;
}

std::string read_fixture(const char* name) {
    std::ifstream file(std::string(AURA_FIXTURE_DIR) + "/" + name, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Deterministic randomness for exact-string checks.
class CountingCrypto final : public aura::IAoratonAccountCrypto {
public:
    bool random_bytes(std::uint8_t* destination, std::size_t size) override {
        for (std::size_t index = 0; index < size; ++index) destination[index] = static_cast<std::uint8_t>(index);
        return true;
    }
    bool sha256(const std::uint8_t* data, std::size_t size, std::array<std::uint8_t, 32>& digest) override {
        return real.sha256(data, size, digest);
    }
    bool pbkdf2_sha256(const std::string& p, const std::vector<std::uint8_t>& s, std::uint32_t i,
                       std::array<std::uint8_t, 32>& k) override {
        return real.pbkdf2_sha256(p, s, i, k);
    }
    bool aes_gcm_open(const std::array<std::uint8_t, 32>& k, const std::string& w, std::string& p) override {
        return real.aes_gcm_open(k, w, p);
    }
    aura::WindowsAoratonAccountCrypto real;
};

}  // namespace

int main() {
    using namespace aura;
    WindowsAoratonAccountCrypto crypto;

    // pkce_rfc7636 (RFC 7636 Appendix B)
    expect(aoraton_pkce_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", crypto) ==
               "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
           "PKCE S256 challenge matches RFC 7636");
    const auto verifier = aoraton_pkce_verifier(crypto);
    expect(verifier.size() == 43 && verifier != aoraton_pkce_verifier(crypto), "verifiers are random 43-char strings");
    expect(aoraton_pkce_challenge("short", crypto).empty(), "too-short verifier refused");

    // pbkdf2_vector (RFC 7914 section 11, first 32 bytes)
    {
        std::array<std::uint8_t, 32> key{};
        expect(crypto.pbkdf2_sha256("passwd", {'s', 'a', 'l', 't'}, 1, key), "PBKDF2 runs");
        expect(hex(key.data(), key.size()) == "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc",
               "PBKDF2-HMAC-SHA256 matches RFC 7914");
    }

    // authorize_url and builders
    {
        AoratonAccountConfig config{"https://x.supabase.co/", "anon", "http://127.0.0.1:4779/aoraton/callback"};
        expect(aoraton_authorize_url(config, "abc-_") ==
                   "https://x.supabase.co/auth/v1/authorize?provider=google&redirect_to="
                   "http%3A%2F%2F127.0.0.1%3A4779%2Faoraton%2Fcallback&code_challenge=abc-_&code_challenge_method=s256",
               "authorize URL is exact");
        expect(aoraton_rooms_by_keys_path({"v2.AbC", "n_1", "bad\"key"}) ==
                   "/rest/v1/rooms?select=*&room_key=in.(%22v2.AbC%22,%22n_1%22)",
               "room keys are quoted and unsafe keys skipped");
        expect(aoraton_messages_path("v2.A b") ==
                   "/rest/v1/messages?select=id,room_key,uid,username,text,type,created_at,client_msg_id"
                   "&room_key=eq.v2.A%20b&order=created_at.desc,id.desc&limit=50",
               "messages path");
        expect(aoraton_overview_body({{"a", ""}, {"b\"", "2026-10-23T10:00:00Z"}}) ==
                   R"({"p_items":[{"room_key":"a","since":null},{"room_key":"b\"","since":"2026-10-23T10:00:00Z"}]})",
               "overview body");
        JsonValue insert;
        expect(parse_json(aoraton_insert_body("k", "u", "Κώστας", "v2:a:b", "temp_aura1"), insert) &&
                   insert.string_at("type") == "text" && insert.string_at("username") == "Κώστας" &&
                   insert.find("avatar_url")->is_null() && insert.find("reactions")->type() == JsonType::Object,
               "insert body is valid JSON with the web client's fields");
        CountingCrypto counting;
        expect(aoraton_new_client_message_id(counting) == "temp_aura000102030405060708090a0b0c0d0e0f",
               "client message id format");
    }

    // parse_session
    {
        AoratonSession session;
        expect(aoraton_parse_session(
                   R"({"access_token":"at","token_type":"bearer","expires_in":3600,"expires_at":1,"refresh_token":"rt",)"
                   R"("user":{"id":"u-1","email":"me@example.com","user_metadata":{"full_name":"Demo Owner"}}})",
                   1000, session),
               "session parses");
        expect(session.expires_at_unix == 4600 && session.user_id == "u-1" && session.email == "me@example.com" &&
                   session.display_name == "Demo Owner",
               "session fields");
        AoratonSession broken;
        expect(!aoraton_parse_session(R"({"access_token":"at","expires_in":3600,"user":{"id":"u"}})", 0, broken),
               "a session without a refresh token is rejected");
    }

    // parse_rooms / member keys / overview / messages / insert ack
    {
        std::vector<AoratonRemoteRoom> rooms;
        expect(aoraton_parse_rooms(
                   R"([{"room_key":"v2.A","room_name":"alpha","display_name":null,"e2ee":true,"expires_at":null},)"
                   R"({"room_key":"b_1","room_name":"beta","display_name":"Beta!","is_notes":true}])",
                   rooms) &&
                   rooms.size() == 2 && rooms[0].name == "alpha" && rooms[0].e2ee && rooms[1].name == "Beta!" &&
                   rooms[1].is_notes,
               "rooms parse with display name preference");
        std::vector<std::string> keys;
        expect(aoraton_parse_member_keys(R"([{"room_key":"a"},{"room_key":"a"},{"room_key":"b"}])", keys) &&
                   keys.size() == 2,
               "member keys deduplicate");
        std::vector<AoratonOverview> overview;
        expect(aoraton_parse_overview(
                   R"([{"room_key":"a","unread_count":3,"last_message_at":"2026-10-23T10:00:00Z","last_text":"v2:x:y",)"
                   R"("last_username":"Maria","last_type":null}])",
                   overview) &&
                   overview.size() == 1 && overview[0].unread == 3 && overview[0].last_type == "text",
               "overview parses");
        std::vector<AoratonRemoteMessage> messages;
        expect(aoraton_parse_messages(
                   R"([{"id":42,"room_key":"a","uid":"u","username":"Maria","text":"v2:x:y","type":"text",)"
                   R"("created_at":"2026-10-23T10:00:00Z","client_msg_id":null}])",
                   messages) &&
                   messages.size() == 1 && messages[0].id == "42" && messages[0].client_msg_id.empty(),
               "messages parse; numeric ids keep their text");
        std::string id;
        std::string created;
        expect(aoraton_parse_insert_ack(R"([{"id":"m-9","created_at":"2026-10-23T10:00:01Z"}])", id, created) &&
                   id == "m-9" && !created.empty(),
               "insert acknowledgement parses");
        expect(!aoraton_parse_rooms("{}", rooms), "rows must be an array");
    }

    // invites
    {
        AoratonInvite invite;
        const std::string k = aoraton_base64url_encode(std::array<std::uint8_t, 32>{1, 2, 3}.data(), 32);
        expect(aoraton_parse_invite("https://aoraton.com/#room=My%20Room&key=v2.QUFB&k=" + k, invite) &&
                   invite.room_name == "My Room" && invite.room_key == "v2.QUFB" && invite.rk[2] == 3,
               "invite fragment parses");
        expect(aoraton_parse_invite("  ?key=abc_1&k=" + k + "\n", invite) && invite.room_key == "abc_1",
               "invite query string parses");
        expect(!aoraton_parse_invite("#room=x&key=v2.Q&k=AAAA", invite) && invite.room_key.empty(),
               "short key rejected and wiped");
        expect(!aoraton_parse_invite("#room=x&pin=1234", invite), "legacy PIN invite rejected");
    }

    // vault (synthetic WebCrypto vectors)
    {
        JsonValue fixture;
        expect(parse_json(read_fixture("aoraton_vault_vectors.json"), fixture), "vault fixture readable");
        std::string meta_json = R"([{"kdf_salt":")" + fixture.string_at("kdf_salt") + R"(","kdf_iters":)" +
                                fixture.string_at("kdf_iters") + R"(,"check_blob":")" +
                                fixture.string_at("check_blob") + R"("}])";
        AoratonVaultMeta meta;
        bool configured = false;
        expect(aoraton_parse_vault_meta(meta_json, meta, configured) && configured && meta.iterations == 1000,
               "vault meta parses");
        std::vector<AoratonVaultRow> rows;
        for (const auto& row : fixture.find("rows")->items()) {
            rows.push_back({row.string_at("room_key"), row.string_at("blob")});
        }
        std::vector<AoratonUnlockedKey> keys;
        expect(aoraton_open_vault(meta, rows, fixture.string_at("passphrase"), crypto, keys) == AoratonVaultResult::Ok,
               "vault opens with the passphrase");
        const auto& expected = fixture.find("expected")->items();
        expect(keys.size() == expected.size(), "foreign row skipped, both rooms restored");
        for (std::size_t index = 0; index < keys.size(); ++index) {
            expect(keys[index].room_key == expected[index].string_at("room_key") &&
                       hex(keys[index].rk.data(), 32) == expected[index].string_at("rk_hex"),
                   "restored room key matches WebCrypto");
        }
        expect(aoraton_open_vault(meta, rows, "wrong", crypto, keys) == AoratonVaultResult::WrongPassphrase &&
                   keys.empty(),
               "wrong passphrase reported, nothing restored");
        AoratonVaultMeta empty;
        expect(aoraton_parse_vault_meta("[]", empty, configured) && !configured, "no vault is not an error");
        expect(aoraton_open_vault(empty, rows, "x", crypto, keys) == AoratonVaultResult::NotConfigured,
               "unconfigured vault");
        AoratonVaultMeta hostile = meta;
        hostile.iterations = 50000000;
        expect(aoraton_open_vault(hostile, rows, "x", crypto, keys) == AoratonVaultResult::Corrupt,
               "absurd iteration count refused");
    }

    std::cout << "aoraton account tests passed\n";
    return 0;
}

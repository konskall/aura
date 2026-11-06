#include "aura/aoraton_device_link.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void expect(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

const std::string kUrl = "https://demo-project.supabase.co";
const std::string kAuthorize = kUrl + "/auth/v1/authorize?provider=google&code_challenge=DEMOchallenge";
const char* kInvite = "https://aoraton.com/#room=DEMO&key=v2.demo-room&k=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

std::string anon_key() { return "eyJhbGciOiJIUzI1NiJ9." + std::string(120, 'a') + ".DEMO_signature-0"; }

aura::AoratonLinkConfig config(std::string url, std::string key, bool inco = false) {
    aura::AoratonLinkConfig value;
    value.base_url = std::move(url);
    value.anon_key = std::move(key);
    value.inco_enabled = inco;
    return value;
}
}  // namespace

int main() {
    using aura::AoratonCallbackStatus;
    using aura::AoratonLinkState;
    using aura::AoratonPairingState;

    expect(aura::aoraton_link_config_valid(config(kUrl, anon_key())), "an https origin and a JWT-shaped anon key");
    expect(!aura::aoraton_link_config_valid(config("http://demo-project.supabase.co", anon_key())),
           "plain http is refused");
    expect(!aura::aoraton_link_config_valid(config(kUrl + "/rest/v1?x=1", anon_key())), "only an origin");
    expect(!aura::aoraton_link_config_valid(config("https://", anon_key())), "a host is needed");
    expect(!aura::aoraton_link_config_valid(config(kUrl, "short")), "the anon key has a JWT's shape");
    expect(!aura::aoraton_link_config_valid(config(kUrl, anon_key() + "\"")), "no quotes or controls in the key");

    int saves = 0;
    std::string saved_url;
    bool saved_inco = false;
    aura::AoratonDeviceLink link([&](const aura::AoratonLinkConfig& value) {
        ++saves;
        saved_url = value.base_url;
        saved_inco = value.inco_enabled;
        return true;
    });
    expect(link.status().state == AoratonLinkState::NotConfigured, "an unconfigured device says so");
    expect(!link.request_sign_in(), "no sign-in before the configuration");
    expect(!link.configure(config("http://x", anon_key())) && saves == 0, "an invalid configuration is not stored");
    expect(link.configure(config(kUrl, anon_key(), true)) && saves == 1 && saved_url == kUrl && saved_inco,
           "the configuration goes to the platform's store");
    link.publish(true, AoratonPairingState::NotPaired, "");
    expect(link.status().state == AoratonLinkState::SignedOut, "configured and signed out");

    expect(link.request_sign_in(), "the PC asks for a sign-in");
    expect(link.status().state == AoratonLinkState::Starting, "until the worker opens the relay it is starting");
    expect(link.take_sign_in_request() && !link.take_sign_in_request(), "the UI thread takes the request once");

    expect(link.open(kAuthorize), "the worker opens the relay");
    auto status = link.status();
    expect(status.state == AoratonLinkState::Waiting && status.authorize_url == kAuthorize,
           "the PC reads the authorize URL");
    std::string code;
    expect(link.poll(code) == AoratonCallbackStatus::Pending && code.empty(), "pending until the PC answers");
    expect(!link.deliver("DEMO code with spaces", false) && !link.deliver(std::string(600, 'a'), false),
           "a code has a bounded token shape");
    expect(link.deliver("DEMO-code_1.2", false), "the PC hands the code over");
    expect(!link.deliver("DEMO-again", false), "one answer per sign-in");
    expect(link.poll(code) == AoratonCallbackStatus::Received && code == "DEMO-code_1.2",
           "the worker receives the code");
    link.close();
    status = link.status();
    expect(status.state != AoratonLinkState::Waiting && status.authorize_url.empty(), "closing forgets the URL");
    expect(!link.deliver("DEMO-late", false), "no answer without a waiting sign-in");

    expect(link.open(kAuthorize) && link.deliver("", true), "the PC reports a refused sign-in");
    code = "stale";
    expect(link.poll(code) == AoratonCallbackStatus::Failed && code.empty(), "the worker sees the refusal");
    link.close();

    link.publish(true, AoratonPairingState::Paired, "");
    expect(link.status().state == AoratonLinkState::SignedIn, "a signed-in device says so");
    link.publish(true, AoratonPairingState::Error, "SIGN_IN_TIMEOUT");
    status = link.status();
    expect(status.state == AoratonLinkState::Failed && status.error == "SIGN_IN_TIMEOUT", "the failure code is shown");
    link.publish(true, AoratonPairingState::Error, "a sentence, not a code");
    expect(link.status().error.empty(), "only error codes reach the PC");
    link.publish(false, AoratonPairingState::NotPaired, "");
    expect(link.status().state == AoratonLinkState::NotConfigured, "the platform can report a lost configuration");

    expect(!link.add_invite("not an invite link"), "an invalid invite is refused");
    expect(link.add_invite(kInvite), "a valid invite waits for the UI thread");
    std::string invite;
    expect(link.take_invite(invite) && invite == kInvite && !link.take_invite(invite), "the invite is taken once");
    for (int i = 0; i < 4; ++i) expect(link.add_invite(kInvite), "up to four waiting invites");
    expect(!link.add_invite(kInvite), "no more than four");

    aura::AoratonStoredSecrets secrets;
    secrets.refresh_token = "DEMO-refresh-token";
    for (const char* room : {"v2.demo-one", "v2.demo-two"}) {
        aura::AoratonUnlockedKey key;
        key.room_key = room;
        key.rk.fill(static_cast<std::uint8_t>(room[8]));
        secrets.keys.push_back(std::move(key));
    }
    std::vector<std::uint8_t> record;
    expect(aura::aoraton_encode_secrets(secrets, record) && record.size() < 200, "a compact record for NVS");
    aura::AoratonStoredSecrets decoded;
    expect(aura::aoraton_decode_secrets(record, decoded) && decoded.refresh_token == "DEMO-refresh-token" &&
               decoded.keys.size() == 2 && decoded.keys[1].room_key == "v2.demo-two" &&
               decoded.keys[1].rk == secrets.keys[1].rk,
           "the record round-trips");
    auto damaged = record;
    damaged[0] ^= 1;
    expect(!aura::aoraton_decode_secrets(damaged, decoded) && decoded.refresh_token.empty() && decoded.keys.empty(),
           "a damaged record is refused and leaves nothing behind");
    damaged = record;
    damaged.pop_back();
    expect(!aura::aoraton_decode_secrets(damaged, decoded), "a truncated record is refused");
    damaged = record;
    damaged.push_back(0);
    expect(!aura::aoraton_decode_secrets(damaged, decoded), "trailing bytes are refused");
    aura::AoratonStoredSecrets keys_only;
    keys_only.keys.push_back(aura::AoratonUnlockedKey{});
    keys_only.keys[0].room_key = "v2.demo-kept";
    expect(aura::aoraton_encode_secrets(keys_only, record) && aura::aoraton_decode_secrets(record, decoded) &&
               decoded.refresh_token.empty() && decoded.keys.size() == 1,
           "room keys survive a signed-out account");
    aura::AoratonStoredSecrets too_many;
    for (int i = 0; i < 65; ++i) {
        too_many.keys.push_back(aura::AoratonUnlockedKey{});
        too_many.keys.back().room_key = "v2.demo-" + std::to_string(i);
    }
    expect(!aura::aoraton_encode_secrets(too_many, record), "at most 64 room keys");
    aura::AoratonStoredSecrets long_room;
    long_room.keys.push_back(aura::AoratonUnlockedKey{});
    long_room.keys[0].room_key = std::string(129, 'r');
    expect(!aura::aoraton_encode_secrets(long_room, record), "a room key name has a bounded length");

    std::cout << "aoraton device link tests passed\n";
    return 0;
}

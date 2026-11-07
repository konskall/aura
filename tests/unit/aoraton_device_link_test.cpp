#include "aura/aoraton_device_link.hpp"
#include "aura/aoraton_real.hpp"
#include "aura/file_transfer.hpp"

#include <array>
#include <cstdint>
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

    {
        // The UI thread's pump, between new settings and the account's restart:
        // the PC's sign-in is accepted at once and starts when the account is
        // ready; invites wait for it too.
        aura::AoratonMailbox mailbox;
        aura::RealAoratonService service(mailbox, aura::AoratonRuntimeMode::Live, [] { return std::string("p-1"); });
        aura::AoratonDeviceLink pumped([](const aura::AoratonLinkConfig&) { return true; });
        expect(pumped.configure(config(kUrl, anon_key())), "settings from the PC");
        aura::aoraton_link_pump(pumped, service, true, false);
        expect(pumped.request_sign_in() && pumped.status().state == AoratonLinkState::Starting,
               "a sign-in asked for while the account restarts is accepted");
        aura::aoraton_link_pump(pumped, service, true, false);
        aura::AoratonCommand command;
        expect(!mailbox.pop_command(command) && pumped.status().state == AoratonLinkState::Starting,
               "and waits for the account");
        aura::aoraton_link_pump(pumped, service, true, true);
        expect(mailbox.pop_command(command) && command.type == aura::AoratonCommandType::BeginPairing,
               "then the account starts it");
        aura::AoratonEvent paired;
        paired.type = aura::AoratonEventType::Paired;
        expect(mailbox.push_event(std::move(paired)), "sign-in finished");
        service.tick(1);
        aura::aoraton_link_pump(pumped, service, true, true);
        expect(pumped.status().state == AoratonLinkState::SignedIn, "the PC sees the account signed in");
        expect(pumped.add_invite(kInvite), "an invite from the PC");
        aura::aoraton_link_pump(pumped, service, true, false);
        expect(!mailbox.pop_command(command), "an invite waits while the account restarts");
        aura::aoraton_link_pump(pumped, service, true, true);
        expect(mailbox.pop_command(command) && command.type == aura::AoratonCommandType::AddInviteKey &&
                   command.plaintext == kInvite,
               "then it reaches the account, inside its command");
        command.clear_plaintext();
        aura::aoraton_link_pump(pumped, service, false, false);
        expect(pumped.status().state == AoratonLinkState::NotConfigured, "no settings: the PC is told so");
    }

    {
        // Only a test's local fake may be plain http on 127.0.0.1, and only when
        // the platform asks for it (the simulator's AURA_AORATON_ALLOW_LOOPBACK=1;
        // the device never does).
        const auto loopback = config("http://127.0.0.1:47123", anon_key());
        expect(!aura::aoraton_link_config_valid(loopback), "loopback http is refused by default");
        expect(aura::aoraton_link_config_valid(loopback, true), "a local fake is taken when asked for");
        expect(!aura::aoraton_link_config_valid(config("http://127.0.0.1.example.com:47123", anon_key()), true) &&
                   !aura::aoraton_link_config_valid(config("http://192.168.1.5:47123", anon_key()), true) &&
                   !aura::aoraton_link_config_valid(config("http://127.0.0.1:47123/rest", anon_key()), true),
               "and nothing else");
        const std::string authorize = "http://127.0.0.1:47123/auth/v1/authorize?provider=google";
        aura::AoratonDeviceLink strict([](const aura::AoratonLinkConfig&) { return true; });
        expect(!strict.configure(loopback) && !strict.open(authorize), "a device link refuses it");
        aura::AoratonDeviceLink test_link([](const aura::AoratonLinkConfig&) { return true; }, true);
        expect(test_link.configure(loopback) && test_link.open(authorize), "a test's link takes it");
        test_link.close();
    }

    {
        // The phone's way (no PC): the device's own transfer server sends the
        // phone to Google's page and takes the code back over the owner's Wi-Fi.
        using aura::AoratonPhoneAnswer;
        using aura::AoratonPhonePage;
        const auto bytes = [](int step, int start) {
            std::array<std::uint8_t, 16> value{};
            for (std::size_t index = 0; index < value.size(); ++index)
                value[index] = static_cast<std::uint8_t>((static_cast<int>(index) * step + start) & 0xFF);
            return value;
        };
        const std::string origin = "http://192.168.1.23:8788";
        const std::string nonce = "AxQlNkdYaXqLnK2-z-DxAg";  // base64url of bytes(17, 3)
        const std::string wrong = "AAAAAAAAAAAAAAAAAAAAAA";
        const std::string code_value = "de300000-0000-4000-8000-00000000c0de";
        std::string url;
        std::string code;
        aura::AoratonDeviceLink phone([](const aura::AoratonLinkConfig&) { return true; });
        expect(!phone.request_phone_sign_in(origin, bytes(17, 3)), "no phone sign-in before the configuration");
        expect(phone.configure(config(kUrl, anon_key())), "the project's settings");
        expect(!phone.request_phone_sign_in("http://8.8.8.8:8788", bytes(17, 3)) &&
                   !phone.request_phone_sign_in("https://192.168.1.23:8788", bytes(17, 3)) &&
                   !phone.request_phone_sign_in("http://192.168.1.23:8788/x", bytes(17, 3)) &&
                   !phone.request_phone_sign_in("http://192.168.1.23", bytes(17, 3)) &&
                   !phone.request_phone_sign_in("http://192.168.1.23:0", bytes(17, 3)) &&
                   !phone.request_phone_sign_in("http://192.168.1.23:65536", bytes(17, 3)) &&
                   !phone.request_phone_sign_in("http://192.168.1:8788", bytes(17, 3)) &&
                   !phone.request_phone_sign_in("http://127.0.0.1:8788", bytes(17, 3)),
               "only the device's own address on a private network");
        expect(phone.phone_url().empty() && phone.redirect_url().empty() && !phone.take_sign_in_request(),
               "nothing was asked for");
        expect(phone.phone_page(nonce, url) == AoratonPhonePage::Gone, "no phone sign-in: no page");

        expect(phone.request_phone_sign_in(origin, bytes(17, 3)), "a phone sign-in");
        expect(phone.phone_url() == origin + "/aoraton/phone?s=" + nonce,
               "the QR's address carries a 22-character secret");
        expect(phone.redirect_url() == origin + "/aoraton/callback?s=" + nonce, "Google's page returns to the device");
        expect(phone.take_sign_in_request(), "the account is asked to start it");
        expect(phone.phone_page(nonce, url) == AoratonPhonePage::Wait && url.empty(),
               "until the account has Google's page, the phone waits");
        expect(phone.open(kAuthorize), "the account opens its sign-in");
        expect(phone.phone_page(nonce, url) == AoratonPhonePage::Google && url == kAuthorize,
               "then the phone goes to Google's page");
        expect(phone.status().state == AoratonLinkState::Waiting && phone.status().authorize_url.empty(),
               "a PC helper does not get a phone's sign-in");
        expect(phone.phone_page(wrong, url) == AoratonPhonePage::Gone && url.empty(), "a wrong link shows nothing");
        expect(phone.phone_answer(wrong, code_value, false) == AoratonPhoneAnswer::Gone &&
                   phone.poll(code) == AoratonCallbackStatus::Pending,
               "nor takes a code");
        expect(phone.phone_answer(nonce, code_value, false) == AoratonPhoneAnswer::Taken, "the code comes back");
        expect(phone.phone_answer(nonce, code_value, false) == AoratonPhoneAnswer::Gone, "once");
        expect(phone.poll(code) == AoratonCallbackStatus::Received && code == code_value, "the account takes it");
        expect(phone.phone_url().empty() && phone.redirect_url().empty() &&
                   phone.phone_page(nonce, url) == AoratonPhonePage::Gone,
               "then the link is spent");

        // Google's page declined, or a code of the wrong shape: the sign-in ends.
        expect(phone.request_phone_sign_in(origin, bytes(29, 7)) && phone.take_sign_in_request() &&
                   phone.open(kAuthorize),
               "a second phone sign-in");
        expect(phone.phone_answer("ByRBXnuYtdLvDClGY4Cdug", "", true) == AoratonPhoneAnswer::Refused &&
                   phone.poll(code) == AoratonCallbackStatus::Failed && code.empty() && phone.phone_url().empty(),
               "declined on the phone: the account is told");
        expect(phone.request_phone_sign_in(origin, bytes(29, 7)) && phone.take_sign_in_request() &&
                   phone.open(kAuthorize) &&
                   phone.phone_answer("ByRBXnuYtdLvDClGY4Cdug", "bad code!", false) == AoratonPhoneAnswer::Refused &&
                   phone.poll(code) == AoratonCallbackStatus::Failed,
               "a malformed code ends it too");

        // Guessing: eight wrong links close the sign-in.
        expect(phone.request_phone_sign_in(origin, bytes(31, 11)) && phone.take_sign_in_request() &&
                   phone.open(kAuthorize),
               "a third phone sign-in");
        for (unsigned miss = 0; miss < aura::AoratonDeviceLink::kMaxPhoneMisses; ++miss) phone.phone_page(wrong, url);
        expect(phone.phone_page("CypJaIemxeQDIkFgf5693A", url) == AoratonPhonePage::Gone &&
                   phone.poll(code) == AoratonCallbackStatus::Failed && phone.phone_url().empty(),
               "after eight wrong links the sign-in is closed and the account told");

        // A new tap replaces the QR, even across the account's restart (close, then
        // open); the PC's request ends a phone's.
        expect(phone.request_phone_sign_in(origin, bytes(17, 3)) && phone.take_sign_in_request() &&
                   phone.open(kAuthorize),
               "a phone sign-in");
        expect(phone.request_phone_sign_in(origin, bytes(37, 13)) && phone.take_sign_in_request(), "tapped again");
        phone.close();
        expect(phone.redirect_url() == origin + "/aoraton/callback?s=DTJXfKHG6xA1Wn-kye4TOA" && phone.open(kAuthorize),
               "the new secret survives the restart");
        expect(phone.phone_page(nonce, url) == AoratonPhonePage::Gone &&
                   phone.phone_page("DTJXfKHG6xA1Wn-kye4TOA", url) == AoratonPhonePage::Google,
               "only the newest QR works");
        expect(phone.request_sign_in() && phone.redirect_url().empty() && phone.phone_url().empty() &&
                   phone.phone_page("DTJXfKHG6xA1Wn-kye4TOA", url) == AoratonPhonePage::Gone,
               "the PC's request ends the phone's");
        phone.close();
        expect(phone.take_sign_in_request(), "and the PC's sign-in starts");

        // The owner stops: before the account started it, and while it waits. The
        // account's own timeout (close) ends it as well.
        expect(phone.request_phone_sign_in(origin, bytes(17, 3)), "a phone sign-in");
        phone.cancel_phone_sign_in();
        expect(!phone.take_sign_in_request() && phone.phone_url().empty(), "stopped before it started");
        expect(phone.request_phone_sign_in(origin, bytes(17, 3)) && phone.take_sign_in_request() &&
                   phone.open(kAuthorize),
               "a phone sign-in");
        phone.cancel_phone_sign_in();
        expect(phone.poll(code) == AoratonCallbackStatus::Failed && phone.phone_url().empty(),
               "stopped while waiting: the account is told");
        expect(phone.request_phone_sign_in(origin, bytes(17, 3)) && phone.take_sign_in_request() &&
                   phone.open(kAuthorize),
               "a phone sign-in");
        phone.close();
        expect(phone.phone_url().empty() && phone.phone_page(nonce, url) == AoratonPhonePage::Gone,
               "the account's timeout ends it");

        aura::AoratonDeviceLink test_link([](const aura::AoratonLinkConfig&) { return true; }, true);
        expect(test_link.configure(config("http://127.0.0.1:47123", anon_key())) &&
                   test_link.request_phone_sign_in("http://127.0.0.1:47124", bytes(17, 3)),
               "a test's link takes a loopback transfer server");
    }

    {
        // The UI thread's part of the phone's way, the same on both platforms:
        // the owner's tap asks, and File transfer's snapshot decides when the
        // phone sign-in starts and ends.
        using aura::TransferState;
        aura::AoratonDeviceLink link([](const aura::AoratonLinkConfig&) { return true; });
        expect(link.configure(config(kUrl, anon_key())), "a configured device");
        unsigned draws = 0;
        const auto random = [&draws](std::array<std::uint8_t, 16>& value) {
            ++draws;
            value.fill(static_cast<std::uint8_t>(40 + draws));
            return true;
        };
        aura::TransferSnapshot transfer;
        transfer.address = "http://192.168.1.23:8788";
        transfer.state = TransferState::Ready;
        bool wanted = false;
        std::string code;
        expect(aura::aoraton_phone_pump(link, transfer, wanted, random).empty() && draws == 0 &&
                   !link.take_sign_in_request(),
               "File transfer on its own starts no sign-in");
        wanted = true;
        transfer.state = TransferState::Starting;
        expect(aura::aoraton_phone_pump(link, transfer, wanted, random).empty() && wanted && draws == 0,
               "the tap waits for the transfer server");
        transfer.state = TransferState::Ready;
        const auto qr = aura::aoraton_phone_pump(link, transfer, wanted, random);
        expect(!wanted && draws == 1 && qr == link.phone_url() &&
                   qr.rfind("http://192.168.1.23:8788/aoraton/phone?s=", 0) == 0,
               "then the phone sign-in starts once, at the server's address");
        transfer.state = TransferState::Receiving;
        expect(aura::aoraton_phone_pump(link, transfer, wanted, random) == qr && draws == 1,
               "and keeps its QR while the server runs");
        expect(link.take_sign_in_request(), "the account is asked to sign in");
        transfer.state = TransferState::Expired;
        expect(aura::aoraton_phone_pump(link, transfer, wanted, random).empty() && link.phone_url().empty(),
               "a stopped server ends it");
        expect(link.open(kAuthorize) && link.poll(code) == AoratonCallbackStatus::Failed,
               "and the account, which had taken the request, hears of it");
        link.close();

        wanted = true;
        transfer.state = TransferState::Failed;
        expect(aura::aoraton_phone_pump(link, transfer, wanted, random).empty() && !wanted && draws == 1,
               "a server that failed drops the tap");
        wanted = true;
        transfer.state = TransferState::Ready;
        transfer.address = "http://127.0.0.1:8788";
        transfer.loopback = true;
        expect(aura::aoraton_phone_pump(link, transfer, wanted, random).empty() && !wanted,
               "a server only this computer reaches shows no phone QR");
        transfer.address = "http://192.168.1.23:8788";
        transfer.loopback = false;
        wanted = true;
        aura::AoratonDeviceLink unconfigured([](const aura::AoratonLinkConfig&) { return true; });
        expect(aura::aoraton_phone_pump(unconfigured, transfer, wanted, random).empty() && !wanted,
               "a device without the project's settings shows no phone QR");
        wanted = true;
        const auto no_random = [](std::array<std::uint8_t, 16>& value) {
            value.fill(0);
            return false;
        };
        expect(aura::aoraton_phone_pump(link, transfer, wanted, no_random).empty() && !wanted &&
                   !link.take_sign_in_request(),
               "without randomness there is no secret, so no phone QR");
        wanted = true;
        expect(!aura::aoraton_phone_pump(link, transfer, wanted, random).empty(), "another phone sign-in");
        transfer.state = TransferState::Off;
        expect(aura::aoraton_phone_pump(link, transfer, wanted, random).empty() && !link.take_sign_in_request(),
               "File transfer stopped before the account took it: nothing starts");
        transfer.state = TransferState::Locked;
        expect(aura::aoraton_phone_pump(link, transfer, wanted, random).empty(), "nothing to end twice");
    }

    std::cout << "aoraton device link tests passed\n";
    return 0;
}

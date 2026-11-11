#include "aura/aoraton_device_link.hpp"
#include "aura/cloud_keys.hpp"
#include "aura/file_transfer.hpp"
#include "aura/text_util.hpp"
#include "aura/transfer_page.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace aura;
namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
struct Reader : ITransferReader {
    std::vector<std::uint8_t> data;
    std::size_t position{0};
    std::function<void()> on_read;
    Reader() = default;
    explicit Reader(std::string_view s) : data(s.begin(), s.end()) {}
    int read(std::uint8_t* out, std::size_t capacity) override {
        if (on_read) {
            auto fn = std::move(on_read);
            fn();
        }
        const auto n = std::min({capacity, data.size() - position, std::size_t{17}});
        std::copy_n(data.data() + position, n, out);
        position += n;
        return static_cast<int>(n);
    }
};
struct Store : ITransferStorage {
    std::vector<std::uint8_t> staged, stored;
    TransferError begin_error{TransferError::None};
    bool begun{false}, cancelled{false}, fail_write{false};
    TransferError begin(TransferArea, const std::string&) override {
        begun = true;
        staged.clear();
        cancelled = false;
        return begin_error;
    }
    bool write(const std::uint8_t* p, std::size_t n) override {
        if (fail_write) return false;
        staged.insert(staged.end(), p, p + n);
        return true;
    }
    bool seal() override { return true; }
    bool read_staged(PhotoBytes& bytes) override {
        if (!bytes.resize(staged.size())) return false;
        std::copy(staged.begin(), staged.end(), bytes.data());
        return true;
    }
    TransferError commit() override {
        stored = staged;
        return TransferError::None;
    }
    void cancel() override {
        staged.clear();
        cancelled = true;
    }
};
}  // namespace
// The owner's PC sends the assistant's keys over the same code-protected
// session; the device keeps them (the sink) and never echoes them.
struct KeySink : ICloudKeySink {
    std::vector<CloudKeys> stored;
    bool store(const CloudKeys& keys) override {
        stored.push_back(keys);
        return true;
    }
};

void cloud_key_tests() {
    std::uint64_t now = 1000;
    TransferSession session;
    Store files;
    KeySink keys;
    TransferProtocol protocol(session, files, [&] { return now; }, &keys);
    std::array<std::uint8_t, 36> entropy{};
    entropy.fill(41);
    expect(session.start(entropy, now), "a session for the keys");
    session.ready(session.snapshot().generation, "http://127.0.0.1:8788", now);
    TransferRequest request;
    request.local_peer = true;
    request.host = "127.0.0.1:8788";
    request.method = "POST";
    request.content_type = "application/json";
    const auto send = [&](const std::string& path, const std::string& body, TransferProtocol& handler) {
        Reader reader(body);
        request.path = path;
        request.length = body.size();
        return handler.handle(request, reader);
    };
    const auto token = parse_flat_json(send("/api/session", "{\"pin\":\"" + session.snapshot().pin + "\"}", protocol).body,
                                       128)->string("token");
    expect(token.has_value(), "the PIN signs in");
    const std::string groq = "gsk_" + std::string(52, 'A');
    const std::string azure(32, 'b');
    const std::string body = "{\"groq_api_key\":\"" + groq + "\",\"azure_speech_key\":\"" + azure +
                             "\",\"azure_speech_region\":\"westeurope\"}";
    expect(send("/api/keys", body, protocol).status == 401 && keys.stored.empty(), "keys need the signed-in session");
    request.authorization = "Bearer " + *token;
    const auto stored = send("/api/keys", body, protocol);
    expect(stored.status == 200 && stored.body == "{\"ok\":true}", "the keys are kept");
    expect(keys.stored.size() == 1 && keys.stored[0].groq_api_key == groq && keys.stored[0].azure_speech_key == azure &&
               keys.stored[0].azure_speech_region == "westeurope",
           "exactly the keys that were sent");
    expect(stored.body.find(groq) == std::string::npos, "the response never echoes a key");
    expect(send("/api/keys", "{\"groq_api_key\":\"sk-123\",\"azure_speech_key\":\"" + azure +
                                 "\",\"azure_speech_region\":\"westeurope\"}", protocol).status == 400,
           "a key in the wrong format is refused");
    expect(send("/api/keys", "{\"groq_api_key\":\"" + groq + "\",\"extra\":\"x\",\"azure_speech_key\":\"\","
                                 "\"azure_speech_region\":\"\"}", protocol).status == 400,
           "only the three fields");
    expect(send("/api/keys", "{\"groq_api_key\":\"" + groq + "\",\"azure_speech_key\":\"\",\"azure_speech_region\":\"\"}",
                protocol).status == 200 && keys.stored.size() == 2,
           "Azure may be left out");
    request.content_type = "text/plain";
    expect(send("/api/keys", body, protocol).status == 400, "keys arrive as JSON only");
    request.content_type = "application/json";
    expect(send("/api/keys", std::string(1100, ' '), protocol).status == 400, "a long body is refused");
    TransferProtocol without_keys(session, files, [&] { return now; });
    expect(send("/api/keys", body, without_keys).status == 404, "a platform that keeps no keys has no such endpoint");
    expect(keys.stored.size() == 2, "refused requests store nothing");
}

void aoraton_link_tests() {
    std::uint64_t now = 1000;
    TransferSession session;
    Store files;
    int saves = 0;
    aura::AoratonDeviceLink link([&](const aura::AoratonLinkConfig&) {
        ++saves;
        return true;
    });
    TransferProtocol protocol(session, files, [&] { return now; }, nullptr, &link);
    std::array<std::uint8_t, 36> entropy{};
    entropy.fill(43);
    expect(session.start(entropy, now), "a session for the account link");
    session.ready(session.snapshot().generation, "http://127.0.0.1:8788", now);
    TransferRequest request;
    request.local_peer = true;
    request.host = "127.0.0.1:8788";
    request.method = "POST";
    request.content_type = "application/json";
    const auto send = [&](const std::string& path, const std::string& body, TransferProtocol& handler) {
        Reader reader(body);
        request.path = path;
        request.length = body.size();
        return handler.handle(request, reader);
    };
    const auto state_of = [&](const TransferResponse& response) {
        const auto json = parse_flat_json(response.body, 2048);
        return json ? json->string("state").value_or("") : std::string{};
    };
    const std::string anon = "eyJhbGciOiJIUzI1NiJ9." + std::string(120, 'a') + ".DEMO_signature-0";
    const std::string config = "{\"url\":\"https://demo-project.supabase.co\",\"anon_key\":\"" + anon +
                               "\",\"inco_enabled\":false}";
    expect(send("/api/aoraton/config", config, protocol).status == 401 && saves == 0,
           "the account link needs the signed-in session");
    const auto token = parse_flat_json(send("/api/session", "{\"pin\":\"" + session.snapshot().pin + "\"}", protocol).body,
                                       128)->string("token");
    request.authorization = "Bearer " + *token;

    expect(state_of(send("/api/aoraton/status", "{}", protocol)) == "not_configured", "an unconfigured device says so");
    expect(send("/api/aoraton/sign-in", "{}", protocol).status == 409, "no sign-in before the configuration");
    expect(send("/api/aoraton/config", "{\"url\":\"http://demo.supabase.co\",\"anon_key\":\"" + anon +
                                           "\",\"inco_enabled\":false}", protocol).status == 400,
           "plain http is refused");
    expect(send("/api/aoraton/config", "{\"url\":\"https://demo-project.supabase.co\",\"anon_key\":\"" + anon + "\"}",
                protocol).status == 400,
           "the three fields are required");
    const auto configured = send("/api/aoraton/config", config, protocol);
    expect(configured.status == 200 && configured.body == "{\"ok\":true}" && saves == 1, "the configuration is kept");
    expect(configured.body.find(anon) == std::string::npos, "the key is never echoed");
    link.publish(true, aura::AoratonPairingState::NotPaired, "");
    expect(state_of(send("/api/aoraton/status", "{}", protocol)) == "signed_out", "configured and signed out");

    expect(send("/api/aoraton/sign-in", "{}", protocol).status == 200 && link.take_sign_in_request(),
           "the PC starts the sign-in on the UI thread");
    expect(link.open("https://demo-project.supabase.co/auth/v1/authorize?provider=google&code_challenge=DEMO"),
           "the worker opens the relay");
    const auto waiting = send("/api/aoraton/status", "{}", protocol);
    const auto waiting_json = parse_flat_json(waiting.body, 2048);
    expect(waiting_json && waiting_json->string("state") == "waiting" &&
               waiting_json->string("authorize_url").value_or("").find("/auth/v1/authorize?") != std::string::npos,
           "the PC reads the authorize URL");
    expect(send("/api/aoraton/code", "{\"code\":\"bad code\"}", protocol).status == 400, "a code has a token's shape");
    const auto delivered = send("/api/aoraton/code", "{\"code\":\"DEMO-code_123456\"}", protocol);
    expect(delivered.status == 200 && delivered.body.find("DEMO-code") == std::string::npos,
           "the code is handed over and not echoed");
    expect(send("/api/aoraton/code", "{\"code\":\"DEMO-code_654321\"}", protocol).status == 409,
           "one answer per sign-in");
    std::string code;
    expect(link.poll(code) == aura::AoratonCallbackStatus::Received && code == "DEMO-code_123456",
           "the worker receives exactly that code");
    link.close();
    expect(link.open("https://demo-project.supabase.co/auth/v1/authorize?provider=google&code_challenge=DEMO") &&
               send("/api/aoraton/code", "{\"error\":\"access_denied\"}", protocol).status == 200 &&
               link.poll(code) == aura::AoratonCallbackStatus::Failed,
           "the browser's refusal reaches the worker");
    link.close();

    const std::string invite =
        "https://aoraton.com/#room=DEMO&key=v2.demo-room&k=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    expect(send("/api/aoraton/invite", "{\"invite\":\"not a link\"}", protocol).status == 400,
           "an invalid invite is refused");
    const auto invited = send("/api/aoraton/invite", "{\"invite\":\"" + invite + "\"}", protocol);
    std::string taken;
    expect(invited.status == 200 && invited.body.find("v2.demo-room") == std::string::npos &&
               link.take_invite(taken) && taken == invite,
           "a valid invite waits for the UI thread, not echoed");
    expect(send("/api/aoraton/status", std::string(5000, ' '), protocol).status == 400, "a long body is refused");
    request.content_type = "text/plain";
    expect(send("/api/aoraton/status", "{}", protocol).status == 400, "JSON only");
    request.content_type = "application/json";
    TransferProtocol without_link(session, files, [&] { return now; });
    expect(send("/api/aoraton/status", "{}", without_link).status == 404,
           "a platform that signs in by itself has no such endpoint");
}

// The phone's way (ADR 0010): the QR opens GET /aoraton/phone?s=<secret> on
// the device, which sends the phone on to Google; Google's page comes back to
// GET /aoraton/callback?s=<secret>&code=... over the owner's Wi-Fi. No PIN:
// the secret stands in for it.
void aoraton_phone_tests() {
    using l10n::PhoneSignInPage;
    std::uint64_t now = 1000;
    TransferSession session;
    Store files;
    aura::AoratonDeviceLink link([](const aura::AoratonLinkConfig&) { return true; });
    TransferProtocol protocol(session, files, [&] { return now; }, nullptr, &link);
    std::array<std::uint8_t, 36> entropy{};
    entropy.fill(47);
    expect(session.start(entropy, now), "a session for the phone sign-in");
    const std::string origin = "http://192.168.1.23:8788";
    session.ready(session.snapshot().generation, origin, now);
    aura::AoratonLinkConfig config;
    config.base_url = "https://demo-project.supabase.co";
    config.anon_key = "eyJhbGciOiJIUzI1NiJ9." + std::string(120, 'a') + ".DEMO_signature-0";
    expect(link.configure(config), "a configured device");
    std::array<std::uint8_t, 16> random{};
    for (std::size_t i = 0; i < random.size(); ++i)
        random[i] = static_cast<std::uint8_t>(5 + 19 * i);
    const auto start = [&] {
        expect(link.request_phone_sign_in(origin, random), "the device shows a QR");
        const auto qr = link.phone_url();
        expect(qr.rfind(origin + "/aoraton/phone?s=", 0) == 0, "the QR points at the device's own server");
        return qr.substr(qr.find("?s=") + 3);
    };
    const std::string authorize =
        "https://demo-project.supabase.co/auth/v1/authorize?provider=google&redirect_to="
        "http%3A%2F%2F192.168.1.23%3A8788%2Faoraton%2Fcallback%3Fs%3DDEMO&code_challenge=DEMO&code_challenge_method=s256";
    TransferRequest request;
    request.local_peer = true;
    request.host = "192.168.1.23:8788";
    request.method = "GET";
    const auto get = [&](const std::string& path) {
        Reader reader;
        request.path = path;
        return protocol.handle(request, reader);
    };
    std::string code;

    const auto secret = start();
    const auto waiting = get("/aoraton/phone?s=" + secret);
    expect(waiting.status == 200 && waiting.html && waiting.body == l10n::phone_sign_in_page(PhoneSignInPage::Wait),
           "the phone waits while the device starts the sign-in, with no PIN");
    expect(waiting.body.find(secret) == std::string::npos, "the waiting page does not repeat the secret");
    const auto wrong = get("/aoraton/phone?s=AAAAAAAAAAAAAAAAAAAAAA");
    expect(wrong.status == 404 && wrong.html && wrong.body == l10n::phone_sign_in_page(PhoneSignInPage::Gone),
           "a wrong secret finds nothing");
    expect(link.take_sign_in_request() && link.open(authorize), "the account opens the sign-in");
    const auto google = get("/aoraton/phone?s=" + secret);
    expect(google.status == 200 && google.html &&
               google.body == l10n::phone_sign_in_page(PhoneSignInPage::Google, authorize),
           "then the phone goes on to Google's page");
    const auto done = get("/aoraton/callback?code=DEMO-code_123456&s=" + secret);
    expect(done.status == 200 && done.html && done.body == l10n::phone_sign_in_page(PhoneSignInPage::Done) &&
               done.body.find("DEMO-code") == std::string::npos,
           "Google's answer reaches the device in either order and is not echoed");
    expect(get("/aoraton/callback?s=" + secret + "&code=DEMO-code_654321").status == 404, "one answer per sign-in");
    expect(link.poll(code) == aura::AoratonCallbackStatus::Received && code == "DEMO-code_123456",
           "the account receives exactly that code");
    link.close();

    random[0] ^= 0xff;
    const auto second = start();
    expect(second != secret && link.take_sign_in_request() && link.open(authorize), "each QR has its own secret");
    expect(get("/aoraton/phone?s=" + secret).status == 404, "the last QR is gone");
    const auto refused = get("/aoraton/callback?error=access_denied&error_code=bad_oauth_callback"
                             "&error_description=Unable+to+exchange+external+code&s=" + second);
    expect(refused.status == 200 && refused.html &&
               refused.body == l10n::phone_sign_in_page(PhoneSignInPage::Cancelled) &&
               link.poll(code) == aura::AoratonCallbackStatus::Failed,
           "a refusal on the phone reaches the account");
    link.close();

    random[1] ^= 0xff;
    const auto third = start();
    expect(link.take_sign_in_request() && link.open(authorize), "a third sign-in");
    for (const std::string bad :
         {std::string("/aoraton/phone"), std::string("/aoraton/phone?"), "/aoraton/phone?s=" + third + "&s=" + third,
          "/aoraton/phone?t=" + third, "/aoraton/phone?s=" + third + "%", "/aoraton/phone?s=" + third + "&x=1",
          std::string("/aoraton/callback?code=DEMO-code_123456"),
          "/aoraton/callback?s=" + third + "&s=" + third + "&code=DEMO-code_123456",
          "/aoraton/callback?s=" + third + "&code=DEMO-code_123456&code=DEMO-code_123456"}) {
        const auto response = get(bad);
        expect(response.status == 404 && response.html, "a malformed address finds nothing");
    }
    expect(link.poll(code) == aura::AoratonCallbackStatus::Pending, "and leaves the sign-in as it was");
    request.local_peer = false;
    expect(get("/aoraton/phone?s=" + third).status == 403, "only from the local network");
    request.local_peer = true;
    request.host = "aura.example:8788";
    expect(get("/aoraton/phone?s=" + third).status == 403, "only at the device's own address");
    request.host = "192.168.1.23:8788";
    request.method = "POST";
    expect(get("/aoraton/phone?s=" + third).status == 404, "the phone's pages are GET only");
    request.method = "GET";
    expect(get("/aoraton/phone?s=" + third).status == 200, "none of these used up the sign-in");
    const auto empty = get("/aoraton/callback?s=" + third);
    expect(empty.status == 200 && empty.body == l10n::phone_sign_in_page(PhoneSignInPage::Cancelled) &&
               link.poll(code) == aura::AoratonCallbackStatus::Failed,
           "an answer with neither a code nor an error ends the sign-in");
    link.close();

    random[2] ^= 0xff;
    const auto fourth = start();
    now += kTransferLeaseMs;
    const auto expired = get("/aoraton/phone?s=" + fourth);
    expect(expired.status == 410 && expired.html && expired.body == l10n::phone_sign_in_page(PhoneSignInPage::Gone),
           "a closed transfer server tells the phone the QR has expired");
    TransferProtocol without_link(session, files, [&] { return now; });
    expect(session.start(entropy, now), "a new session");
    session.ready(session.snapshot().generation, origin, now);
    Reader reader;
    request.path = "/aoraton/phone?s=" + fourth;
    expect(without_link.handle(request, reader).status == 405, "a platform that signs in by itself has no such page");

    // The pages: Greek with English beneath, no script, and the authorize URL
    // escaped wherever it is written.
    const std::string tricky = "https://demo-project.supabase.co/auth/v1/authorize?a=1&b=\"<x>'";
    const auto page = l10n::phone_sign_in_page(PhoneSignInPage::Google, tricky);
    const std::string escaped = "https://demo-project.supabase.co/auth/v1/authorize?a=1&amp;b=&quot;&lt;x&gt;&#39;";
    expect(page.find("<meta http-equiv=\"refresh\" content=\"0;url=" + escaped + "\">") != std::string::npos &&
               page.find("href=\"" + escaped + "\"") != std::string::npos && page.find(tricky) == std::string::npos,
           "the Google page moves on by itself, with a link as well, the URL escaped");
    expect(l10n::phone_sign_in_page(PhoneSignInPage::Wait).find("<meta http-equiv=\"refresh\" content=\"1\">") !=
               std::string::npos,
           "the waiting page asks again every second");
    for (const auto kind : {PhoneSignInPage::Wait, PhoneSignInPage::Google, PhoneSignInPage::Done,
                            PhoneSignInPage::Cancelled, PhoneSignInPage::Gone}) {
        const auto html = l10n::phone_sign_in_page(kind, "https://demo-project.supabase.co/auth/v1/authorize");
        expect(html.rfind("<!doctype html>", 0) == 0 && html.find("<script") == std::string::npos &&
                   html.find("lang=\"el\"") != std::string::npos && html.find("lang=\"en\"") != std::string::npos &&
                   html.find("name=\"referrer\" content=\"no-referrer\"") != std::string::npos,
               "every page is Greek and English, script-free and sends no referrer");
        expect((html.find("http-equiv=\"refresh\"") != std::string::npos) ==
                   (kind == PhoneSignInPage::Wait || kind == PhoneSignInPage::Google),
               "only the waiting and Google pages move on by themselves");
    }
    const std::vector<std::string> pages{
        l10n::phone_sign_in_page(PhoneSignInPage::Wait), l10n::phone_sign_in_page(PhoneSignInPage::Google, tricky),
        l10n::phone_sign_in_page(PhoneSignInPage::Done), l10n::phone_sign_in_page(PhoneSignInPage::Cancelled),
        l10n::phone_sign_in_page(PhoneSignInPage::Gone)};
    for (std::size_t i = 0; i < pages.size(); ++i)
        for (std::size_t j = i + 1; j < pages.size(); ++j)
            expect(pages[i] != pages[j], "each step has its own page");

    // The look (the owner's request, 2026-11-11), as aoraton.com's relay page:
    // aoraton's mark and Aura's wordmark, both in the page itself (the device's
    // CSP allows data: images and nothing from elsewhere), and the step the
    // page is styled by.
    const std::pair<PhoneSignInPage, std::string_view> steps[] = {
        {PhoneSignInPage::Wait, "wait"}, {PhoneSignInPage::Google, "google"}, {PhoneSignInPage::Done, "done"},
        {PhoneSignInPage::Cancelled, "cancelled"}, {PhoneSignInPage::Gone, "gone"}};
    for (const auto& [kind, step] : steps) {
        const auto html = l10n::phone_sign_in_page(kind, "https://demo-project.supabase.co/auth/v1/authorize");
        expect(html.find("<html lang=\"el\" data-state=\"" + std::string(step) + "\">") != std::string::npos,
               "each page names its step");
        expect(html.find("<img class=\"mark\" src=\"data:image/webp;base64,UklGR") != std::string::npos &&
                   html.find("alt=\"aoraton\"") != std::string::npos,
               "aoraton's mark is in the page, as WebP");
        expect(html.find("<svg class=\"aura\" viewBox=\"64 400 1136 420\" role=\"img\" aria-label=\"Aura\">") !=
                   std::string::npos,
               "Aura's wordmark is in the page");
        expect(html.find("src=\"http") == std::string::npos && html.find("url(http") == std::string::npos &&
                   html.find("<link") == std::string::npos && html.find("@import") == std::string::npos,
               "nothing comes from elsewhere");
    }
    const auto joined = l10n::phone_sign_in_page(PhoneSignInPage::Done);
    const auto mark = joined.find("base64,");
    expect(mark != std::string::npos && joined.find('"', mark) - mark < 4096,
           "the mark stays small: the firmware is on its size floor");
    // Aura's wordmark is the brand's own, never recoloured: every path, colour
    // stop, gradient and clip of its source is in the page, unaltered.
    std::ifstream source_file(AURA_WORDMARK_SVG, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(source_file), {}};
    std::size_t pieces = 0;
    for (const std::string_view open : {" d=\"", "<stop", "<linearGradient", "<clipPath"}) {
        for (auto at = source.find(open); at != std::string::npos; at = source.find(open, at + 1)) {
            const auto end = source.find(open.front() == ' ' ? '"' : '>', at + open.size());
            const auto piece = source.substr(at, end + 1 - at);
            if (joined.find(piece) == std::string::npos) std::cerr << "not in the page: " << piece.substr(0, 60) << '\n';
            expect(joined.find(piece) != std::string::npos, "Aura's wordmark is unaltered");
            ++pieces;
        }
    }
    expect(pieces == 38, "the wordmark's 12 paths, 17 stops, 5 gradients and 4 clips were compared");
}

int main() {
    aoraton_link_tests();
    aoraton_phone_tests();
    for (auto name : {"DEMO φωτογραφία.jpg", "DEMO notes.txt", "DEMO 20%.pdf"})
        expect(transfer_filename_valid(name), "safe Unicode filename accepted");
    for (auto name : {"../x", ".hidden", "a/b", "a\\b", "x:", "a\n.txt", "a.", "a ", "CON.txt", "CON .txt", "lpt1.csv",
                      "COM¹.txt", "x?.txt"})
        expect(!transfer_filename_valid(name), "unsafe filename rejected");
    expect(transfer_percent_decode("DEMO%20a.txt") == "DEMO a.txt" && transfer_percent_decode("%Q1").empty(),
           "percent decoding strict");
    expect(transfer_percent_decode("%252e%252e%252fx") == "%2e%2e%2fx", "exactly one decoding pass");
    expect(transfer_address_valid("http://192.168.1.2:8788") && transfer_address_valid("http://127.0.0.1:12345"),
           "local numeric endpoints accepted");
    for (auto url : {"http://8.8.8.8:80", "http://aura.test:8788", "http://192.168.1.2:0", "http://192.168.1.2:8788/x",
                     "https://192.168.1.2:8"})
        expect(!transfer_address_valid(url), "nonlocal or ambiguous endpoints rejected");
    TransferRequest parsed;
    expect(parse_transfer_headers("POST /api/upload HTTP/1.1\r\nHost: 127.0.0.1:8788\r\nContent-Length: 3\r\n\r\n",
                                  parsed) &&
               parsed.length == 3,
           "bounded HTTP framing");
    const std::string callback = "/aoraton/callback?code=" + std::string(36, 'c') + "&s=" + std::string(22, 's') +
                                 "&error_description=" + std::string(360, 'e');
    expect(callback.size() <= 512 &&
               parse_transfer_headers("GET " + callback + " HTTP/1.1\r\nHost: 192.168.1.23:8788\r\n\r\n", parsed) &&
               parsed.path == callback,
           "the phone's answer from Google fits");
    expect(!parse_transfer_headers("GET /" + std::string(512, 'a') + " HTTP/1.1\r\nHost: 192.168.1.23:8788\r\n\r\n",
                                   parsed),
           "a longer address is refused");
    for (auto bad :
         {"POST /api/upload HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\n",
          "POST /api/upload HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n",
          "POST /api/upload HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n", "GET / HTTP/1.1\r\n Host: x\r\n\r\n"})
        expect(!parse_transfer_headers(bad, parsed), "ambiguous framing rejected");
    std::uint64_t now = 1000;
    TransferSession session;
    Store store;
    TransferProtocol protocol(session, store, [&] { return now; });
    std::array<std::uint8_t, 36> entropy{};
    entropy.fill(37);
    auto start = [&] {
        expect(session.start(entropy, now), "start with supplied test entropy");
        session.ready(session.snapshot().generation, "http://127.0.0.1:8788", now);
    };
    start();
    TransferRequest request;
    request.local_peer = true;
    request.host = "127.0.0.1:8788";
    request.method = "GET";
    request.path = "/";
    Reader empty;
    const auto page = protocol.handle(request, empty);
    expect(page.status == 200 && page.html && page.body.find(session.snapshot().pin) == std::string::npos,
           "landing page never contains device PIN");
    request.origin = "http://attacker.invalid";
    expect(protocol.handle(request, empty).status == 403, "cross-origin blocked");
    request.origin.clear();
    request.local_peer = false;
    expect(protocol.handle(request, empty).status == 403, "public peer blocked");
    request.local_peer = true;
    auto login = [&] {
        const auto text = "{\"pin\":\"" + session.snapshot().pin + "\"}";
        Reader reader(text);
        request.method = "POST";
        request.path = "/api/session";
        request.content_type = "application/json";
        request.length = text.size();
        const auto response = protocol.handle(request, reader);
        const auto json = parse_flat_json(response.body, 128);
        expect(response.status == 200 && json && json->string("token"), "correct PIN issues memory-only bearer");
        return *json->string("token");
    };
    const auto token = login();
    request.path = "/api/upload";
    request.filename = "DEMO%20notes.txt";
    request.area = "files";
    request.content_type = "application/octet-stream";
    Reader file("DEMO contents");
    request.length = file.data.size();
    expect(protocol.handle(request, file).status == 401 && !store.begun, "upload cannot bypass login");
    request.authorization = "Bearer " + token;
    expect(
        protocol.handle(request, file).status == 201 && store.stored == file.data && session.snapshot().completed == 1,
        "streamed upload committed");
    store.begin_error = TransferError::Exists;
    Reader same("DEMO contents");
    expect(protocol.handle(request, same).status == 409, "collision rejected");
    store.begin_error = TransferError::None;
    request.filename = "..%2Fclock.txt";
    Reader no_read;
    expect(protocol.handle(request, no_read).status == 400, "encoded traversal rejected");
    request.filename = "DEMO.txt";
    request.length = kTransferFileLimit + 1;
    expect(protocol.handle(request, no_read).status == 413, "generic size limit before reading body");
    request.area = "photos";
    request.filename = "DEMO.png";
    std::ifstream png(std::string(AURA_PHOTO_FIXTURES) + "/DEMO-image.png", std::ios::binary);
    Reader image;
    image.data = {std::istreambuf_iterator<char>(png), {}};
    request.length = image.data.size();
    expect(protocol.handle(request, image).status == 201 && session.snapshot().photos_revision == 1,
           "actual photo validated before commit");
    std::ifstream tall_png(std::string(AURA_PHOTO_FIXTURES) + "/DEMO-tall.png", std::ios::binary);
    Reader tall;
    tall.data = {std::istreambuf_iterator<char>(tall_png), {}};
    request.length = tall.data.size();
    const auto resolution_error = protocol.handle(request, tall);
    expect(tall.data.size() < 100000 && resolution_error.status == 400 &&
               resolution_error.body.find("dimensions") != std::string::npos,
           "resolution errors are not reported as upload byte limits");
    Reader invalid("not an image");
    request.length = invalid.data.size();
    expect(
        protocol.handle(request, invalid).status == 400 && store.cancelled && session.snapshot().photos_revision == 1,
        "invalid photo removed without catalog notification");
    request.area = "files";
    request.filename = "DEMO.txt";
    Reader interrupted("DEMO interrupted");
    request.length = 100;
    interrupted.on_read = [&] { session.stop(); };
    expect(protocol.handle(request, interrupted).status == 400 && store.cancelled && store.staged.empty(),
           "Stop cancels an incomplete file");
    expect(!session.authorize("Bearer " + token, now), "old token invalid after Stop");
    start();
    const auto network_token = session.login(session.snapshot().pin, now);
    const auto network_generation = session.snapshot().generation;
    request.authorization = "Bearer " + network_token;
    Reader disconnected("DEMO network disconnect must cancel this file");
    request.length = disconnected.data.size();
    const auto previously_stored = store.stored;
    disconnected.on_read = [&] { session.fail(network_generation, TransferError::Network); };
    expect(protocol.handle(request, disconnected).status == 400 && store.staged.empty() &&
               store.stored == previously_stored,
           "network loss during a read cancels staging without committing");
    expect(session.snapshot().state == TransferState::Failed && session.snapshot().error == TransferError::Network &&
               session.snapshot().pin.empty() && !session.valid(network_generation, now) &&
               !session.authorize("Bearer " + network_token, now),
           "stale progress/completion cannot revive a failed network session");
    entropy.fill(41);
    start();
    expect(!session.authorize("Bearer " + token, now), "new activation rotates token");
    for (int i = 0; i < 5; ++i)
        expect(session.login("badpin", now).empty(), "wrong PIN rejected");
    expect(session.snapshot().state == TransferState::Locked && session.snapshot().pin.empty(),
           "five failures close the session");
    start();
    now += kTransferLeaseMs;
    session.tick(now);
    expect(session.snapshot().state == TransferState::Expired && session.snapshot().pin.empty(),
           "lease expires and clears credentials");
    cloud_key_tests();
    std::cout << "Transfer framing, auth, expiry, validation, cancellation and streaming checks passed\n";
}

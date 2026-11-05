#include "aura/cloud_keys.hpp"
#include "aura/file_transfer.hpp"
#include "aura/text_util.hpp"
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

int main() {
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

#include "aura/file_transfer.hpp"
#include "aura/text_util.hpp"
#include "aura/transfer_page.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>

namespace aura {
namespace {
void wipe(std::string& text) {
    volatile char* p = text.empty() ? nullptr : &text[0];
    for (std::size_t i = 0; i < text.size(); ++i)
        p[i] = 0;
    text.clear();
}
bool equal_secret(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}
std::string lower(std::string_view v) {
    std::string s(v);
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}
bool ipv4(std::string_view s, unsigned (&parts)[4]) {
    for (unsigned i = 0; i < 4; ++i) {
        const auto dot = s.find('.');
        const auto piece = s.substr(0, dot);
        if (piece.empty() || piece.size() > 3 || (piece.size() > 1 && piece.front() == '0')) return false;
        unsigned n = 0;
        for (char c : piece) {
            if (c < '0' || c > '9') return false;
            n = n * 10 + static_cast<unsigned>(c - '0');
        }
        if (n > 255 || (i < 3 && dot == std::string_view::npos) || (i == 3 && dot != std::string_view::npos))
            return false;
        parts[i] = n;
        if (dot != std::string_view::npos) s.remove_prefix(dot + 1);
    }
    return true;
}
TransferResponse error_response(int status, TransferError error) {
    return {status, "{\"error\":\"" + std::string(transfer_error_code(error)) + "\"}", false};
}
}  // namespace
bool transfer_private_ipv4(std::string_view s) {
    unsigned p[4]{};
    if (!ipv4(s, p)) return false;
    return p[0] == 10 || (p[0] == 172 && p[1] >= 16 && p[1] <= 31) || (p[0] == 192 && p[1] == 168) ||
           (p[0] == 127 && p[1] == 0 && p[2] == 0 && p[3] == 1);
}
bool transfer_address_valid(std::string_view s) {
    if (s.size() > 64 || s.substr(0, 7) != "http://") return false;
    s.remove_prefix(7);
    const auto colon = s.find(':');
    if (colon == std::string_view::npos || !transfer_private_ipv4(s.substr(0, colon))) return false;
    const auto port = s.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned value = 0;
    for (char c : port) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    return value > 0 && value <= 65535;
}
bool transfer_filename_valid(std::string_view name) {
    if (name.empty() || name.size() > 120 || name.front() == '.' || name.back() == '.' ||
        sanitize_text(name, 120, false) != name)
        return false;
    for (unsigned char c : name)
        if (c < 32 || c == 127 || std::string_view("\\/:*?\"<>|").find(static_cast<char>(c)) != std::string_view::npos)
            return false;
    auto stem = lower(name.substr(0, name.find('.')));
    while (!stem.empty() && stem.back() == ' ')
        stem.pop_back();
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
    if (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") {
        const auto suffix = stem.substr(3);
        if ((suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9') || suffix == "¹" || suffix == "²" ||
            suffix == "³")
            return false;
    }
    return true;
}
std::string transfer_percent_decode(std::string_view s) {
    if (s.size() > 360) return {};
    const auto hex = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') {
            out.push_back(s[i]);
            continue;
        }
        if (i + 2 >= s.size()) return {};
        const int a = hex(s[i + 1]), b = hex(s[i + 2]);
        if (a < 0 || b < 0) return {};
        out.push_back(static_cast<char>(a * 16 + b));
        i += 2;
    }
    return out;
}
const char* transfer_error_code(TransferError e) {
    switch (e) {
        case TransferError::None: return "ok";
        case TransferError::Network: return "network";
        case TransferError::Storage: return "storage";
        case TransferError::Name: return "name";
        case TransferError::Size: return "size";
        case TransferError::Exists: return "exists";
        case TransferError::Image: return "image";
        case TransferError::Interrupted: return "interrupted";
        case TransferError::Auth: return "auth";
        case TransferError::Limit: return "limit";
        case TransferError::Dimensions: return "dimensions";
        case TransferError::Missing: return "missing";
    }
    return "storage";
}
void transfer_list_add(std::vector<TransferFile>& files, TransferFile file, bool& more) {
    auto pos = std::lower_bound(files.begin(), files.end(), file.name,
                                [](const auto& a, const auto& name) { return a.name < name; });
    files.insert(pos, std::move(file));
    if (files.size() > 128) {
        files.pop_back();
        more = true;
    }
}
bool TransferSession::start(const std::array<std::uint8_t, 36>& entropy, std::uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::all_of(entropy.begin(), entropy.end(), [](auto b) { return b == 0; })) return false;
    clear_secret();
    const auto revision = state_.photos_revision, removals = state_.removals_revision, sequence = state_.sequence,
               generation = state_.generation;
    state_ = {};
    state_.photos_revision = revision;
    state_.removals_revision = removals;
    state_.sequence = sequence + 1;
    state_.generation = generation + 1;
    state_.state = TransferState::Starting;
    state_.remaining_seconds = 600;
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value = (value << 8) | entropy[i];
    char pin[8];
    std::snprintf(pin, sizeof(pin), "%06u", static_cast<unsigned>(value % 1000000U));
    state_.pin = pin;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 4; i < entropy.size(); ++i) {
        token_ += hex[entropy[i] >> 4];
        token_ += hex[entropy[i] & 15];
    }
    expires_ = now + kTransferLeaseMs;
    attempts_ = 0;
    return true;
}
void TransferSession::clear_secret() {
    wipe(token_);
    wipe(state_.pin);
}
void TransferSession::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_secret();
    state_.state = TransferState::Off;
    state_.error = TransferError::None;
    state_.address.clear();
    state_.remaining_seconds = 0;
    ++state_.generation;
    ++state_.sequence;
}
bool TransferSession::active_locked(std::uint64_t now) {
    const bool active = state_.state == TransferState::Starting || state_.state == TransferState::Ready ||
                        state_.state == TransferState::Receiving;
    if (active && now >= expires_) {
        clear_secret();
        state_.state = TransferState::Expired;
        state_.remaining_seconds = 0;
        ++state_.generation;
        ++state_.sequence;
        return false;
    }
    return active;
}
void TransferSession::tick(std::uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_locked(now)) return;
    const auto remaining = static_cast<unsigned>((expires_ - now + 999) / 1000);
    if (remaining != state_.remaining_seconds) {
        state_.remaining_seconds = remaining;
        ++state_.sequence;
    }
}
TransferSnapshot TransferSession::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}
void TransferSession::ready(std::uint64_t generation, std::string address, std::uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != state_.generation || !active_locked(now)) return;
    if (!transfer_address_valid(address)) {
        clear_secret();
        state_.state = TransferState::Failed;
        state_.error = TransferError::Network;
        ++state_.sequence;
        return;
    }
    state_.address = std::move(address);
    state_.loopback = state_.address.find("http://127.0.0.1:") == 0;
    state_.state = TransferState::Ready;
    ++state_.sequence;
}
void TransferSession::fail(std::uint64_t generation, TransferError error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != state_.generation) return;
    // Invalidate in-flight progress/completion as well as the bearer. A read
    // already waiting on the socket may return after the network was lost.
    clear_secret();
    state_.error = error;
    state_.state = TransferState::Failed;
    ++state_.generation;
    ++state_.sequence;
}
std::string TransferSession::login(std::string_view pin, std::uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_locked(now) || state_.state == TransferState::Starting) return {};
    if (pin.size() == 6 && equal_secret(pin, state_.pin)) return token_;
    if (++attempts_ >= 5) {
        clear_secret();
        state_.state = TransferState::Locked;
        ++state_.generation;
        ++state_.sequence;
    }
    return {};
}
std::uint64_t TransferSession::authorize(std::string_view bearer, std::uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_locked(now) || state_.state == TransferState::Starting || bearer.size() != token_.size() + 7 ||
        bearer.substr(0, 7) != "Bearer " || !equal_secret(bearer.substr(7), token_))
        return 0;
    return state_.generation;
}
bool TransferSession::valid(std::uint64_t generation, std::uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation == state_.generation && active_locked(now);
}
void TransferSession::progress(std::uint64_t generation, std::string_view name, std::uint64_t received,
                               std::uint64_t total) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != state_.generation) return;
    const unsigned percent = total ? static_cast<unsigned>(received * 100 / total) : 100;
    const bool changed = state_.state != TransferState::Receiving || percent != displayed_percent_;
    state_.state = TransferState::Receiving;
    state_.error = TransferError::None;
    state_.last_name = std::string(name.substr(0, 120));
    state_.received = received;
    state_.total = total;
    displayed_percent_ = percent;
    if (changed) ++state_.sequence;
}
void TransferSession::finished(std::uint64_t generation, TransferArea area, TransferError error) {
    std::lock_guard<std::mutex> lock(mutex_);
    // A committed image remains a new photo even if Stop raced the final rename.
    if (error == TransferError::None && area == TransferArea::Photos) ++state_.photos_revision;
    if (generation != state_.generation) {
        ++state_.sequence;
        return;
    }
    state_.state = TransferState::Ready;
    state_.error = error;
    if (error == TransferError::None) ++state_.completed;
    ++state_.sequence;
}
void TransferSession::seed_demo() {
    std::array<std::uint8_t, 36> seed{};
    seed.fill(17);
    start(seed, 0);
    const auto generation = snapshot().generation;
    ready(generation, "http://127.0.0.1:8788", 0);
    std::lock_guard<std::mutex> lock(mutex_);
    state_.demo = true;
    state_.pin = "123456";
    ++state_.sequence;
}
void TransferSession::removed(TransferArea area) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (area == TransferArea::Photos) {
        ++state_.photos_revision;
        ++state_.removals_revision;
    }
    ++state_.sequence;
}
bool parse_transfer_headers(std::string_view text, TransferRequest& request) {
    if (text.size() > 8192 || text.size() < 18 || text.substr(text.size() - 4) != "\r\n\r\n") return false;
    const auto first = text.find("\r\n");
    const auto line = text.substr(0, first);
    const auto space = line.find(' '), last = line.rfind(' ');
    if (space == std::string_view::npos || last <= space || line.substr(last + 1) != "HTTP/1.1") return false;
    TransferRequest r;
    r.method = std::string(line.substr(0, space));
    r.path = std::string(line.substr(space + 1, last - space - 1));
    if (r.path.size() > 64 || r.method.size() > 8 || r.path.empty()) return false;
    bool length_seen = false;
    std::vector<std::string> seen;
    text.remove_prefix(first + 2);
    while (text.size() > 2) {
        const auto end = text.find("\r\n");
        if (end == std::string_view::npos) return false;
        const auto header = text.substr(0, end);
        text.remove_prefix(end + 2);
        const auto colon = header.find(':');
        if (colon == std::string_view::npos || colon == 0 || header.front() == ' ' || header.front() == '\t')
            return false;
        const auto name = lower(header.substr(0, colon));
        const auto value = trim(header.substr(colon + 1));
        if (name.size() > 64 || value.size() > 512 || seen.size() >= 32 ||
            std::find(seen.begin(), seen.end(), name) != seen.end())
            return false;
        for (char c : name)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) return false;
        for (char c : value)
            if (static_cast<unsigned char>(c) < 32 || c == 127) return false;
        seen.push_back(name);
        if (name == "transfer-encoding" || name == "expect") return false;
        if (name == "host")
            r.host = value;
        else if (name == "origin")
            r.origin = value;
        else if (name == "authorization")
            r.authorization = value;
        else if (name == "content-type")
            r.content_type = value;
        else if (name == "x-aura-filename")
            r.filename = value;
        else if (name == "x-aura-area")
            r.area = value;
        else if (name == "content-length") {
            if (value.empty() || value.size() > 10) return false;
            for (char c : value) {
                if (c < '0' || c > '9') return false;
                r.length = r.length * 10 + static_cast<unsigned>(c - '0');
            }
            length_seen = true;
        }
    }
    if (r.host.empty() || (r.method == "POST" && !length_seen)) return false;
    request = std::move(r);
    return true;
}
TransferResponse TransferProtocol::handle(const TransferRequest& r, ITransferReader& reader) {
    if (!r.local_peer) return error_response(403, TransferError::Auth);
    session_.tick(clock_());
    const auto state = session_.snapshot();
    if (!session_.valid(state.generation, clock_()) || state.state == TransferState::Starting)
        return error_response(410, TransferError::Interrupted);
    const auto host = state.address.substr(7);
    if (r.host != host || (!r.origin.empty() && r.origin != state.address))
        return error_response(403, TransferError::Auth);
    if (r.method == "GET" && r.path == "/") return {200, std::string(l10n::transfer_page()), true};
    if (r.method != "POST") return error_response(405, TransferError::Auth);
    if (r.path == "/api/session") {
        if (r.length > 80 || r.length < 2 || r.content_type.substr(0, 16) != "application/json")
            return error_response(400, TransferError::Auth);
        std::string body(static_cast<std::size_t>(r.length), '\0');
        std::size_t done = 0;
        while (done < body.size()) {
            const int n = reader.read(reinterpret_cast<std::uint8_t*>(&body[done]), body.size() - done);
            if (n <= 0 || static_cast<std::size_t>(n) > body.size() - done)
                return error_response(400, TransferError::Interrupted);
            done += static_cast<std::size_t>(n);
        }
        const auto json = parse_flat_json(body, 80);
        const auto pin = json ? json->string("pin") : std::optional<std::string>{};
        const auto token = session_.login(json && json->size() == 1 && pin ? *pin : std::string{}, clock_());
        wipe(body);
        return token.empty() ? error_response(401, TransferError::Auth)
                             : TransferResponse{200, "{\"token\":\"" + token + "\"}", false};
    }
    if (r.path != "/api/upload" && r.path != "/api/files" && r.path != "/api/delete")
        return error_response(404, TransferError::Name);
    const auto ticket = session_.authorize(r.authorization, clock_());
    if (!ticket) return error_response(401, TransferError::Auth);
    if (r.path == "/api/files" || r.path == "/api/delete") {
        if (r.length < 2 || r.length > 768 || r.content_type != "application/json")
            return error_response(400, TransferError::Name);
        std::string body(static_cast<std::size_t>(r.length), '\0');
        std::size_t done = 0;
        while (done < body.size()) {
            const int n = reader.read(reinterpret_cast<std::uint8_t*>(&body[done]), body.size() - done);
            if (n <= 0 || static_cast<std::size_t>(n) > body.size() - done)
                return error_response(400, TransferError::Interrupted);
            done += static_cast<std::size_t>(n);
        }
        const auto json = parse_flat_json(body, 768);
        const auto area_name = json ? json->string("area") : std::optional<std::string>{};
        if (!json || !area_name || (*area_name != "photos" && *area_name != "files"))
            return error_response(400, TransferError::Name);
        const auto area = *area_name == "photos" ? TransferArea::Photos : TransferArea::Files;
        if (!session_.valid(ticket, clock_())) return error_response(410, TransferError::Interrupted);
        if (r.path == "/api/files") {
            const auto after = json->string("after");
            if (json->size() != 2 || !after || (!after->empty() && !transfer_filename_valid(*after)))
                return error_response(400, TransferError::Name);
            std::vector<TransferFile> files;
            bool more = false;
            const auto error = storage_.list(area, *after, files, more);
            if (error != TransferError::None) return error_response(507, error);
            std::string response = "{\"files\":[";
            for (const auto& file : files) {
                if (response.back() != '[') response += ',';
                response +=
                    "{\"name\":\"" + json_escape(file.name) + "\",\"bytes\":" + std::to_string(file.bytes) + "}";
            }
            response += std::string("],\"more\":") + (more ? "true}" : "false}");
            return {200, std::move(response), false};
        }
        const auto name = json->string("name");
        if (json->size() != 3 || !name || json->boolean("confirm") != true || !transfer_filename_valid(*name) ||
            (area == TransferArea::Photos && !photo_filename_valid(*name)))
            return error_response(400, TransferError::Name);
        const auto error = storage_.erase(area, *name);
        if (error != TransferError::None) return error_response(error == TransferError::Missing ? 404 : 507, error);
        session_.removed(area);
        return {200, "{\"ok\":true}", false};
    }
    if (state.completed >= 128) return error_response(429, TransferError::Limit);
    const auto name = transfer_percent_decode(r.filename);
    const bool photo = r.area == "photos";
    const auto area = photo ? TransferArea::Photos : TransferArea::Files;
    if ((r.area != "photos" && r.area != "files") || !transfer_filename_valid(name) ||
        (photo && !photo_filename_valid(name)))
        return error_response(400, TransferError::Name);
    if (r.length > (photo ? kMaxPhotoFileBytes : kTransferFileLimit)) return error_response(413, TransferError::Size);
    if (r.content_type != "application/octet-stream") return error_response(415, TransferError::Name);
    auto error = storage_.begin(area, name);
    if (error != TransferError::None) return error_response(error == TransferError::Exists ? 409 : 507, error);
    struct Cancel {
        ITransferStorage& storage;
        ~Cancel() { storage.cancel(); }
    } cancel{storage_};
    std::array<std::uint8_t, 4096> bytes{};
    std::uint64_t received = 0;
    session_.progress(ticket, name, 0, r.length);
    const auto started = clock_();
    while (received < r.length) {
        if (!session_.valid(ticket, clock_()) || clock_() - started > 120000) {
            error = TransferError::Interrupted;
            break;
        }
        const auto wanted = static_cast<std::size_t>(std::min<std::uint64_t>(bytes.size(), r.length - received));
        const int n = reader.read(bytes.data(), wanted);
        if (n <= 0 || static_cast<std::size_t>(n) > wanted) {
            error = TransferError::Interrupted;
            break;
        }
        if (!storage_.write(bytes.data(), static_cast<std::size_t>(n))) {
            error = TransferError::Storage;
            break;
        }
        received += static_cast<unsigned>(n);
        session_.progress(ticket, name, received, r.length);
    }
    if (error == TransferError::None && !storage_.seal()) error = TransferError::Storage;
    if (error == TransferError::None && photo) {
        PhotoBytes encoded;
        PhotoError image_error = PhotoError::None;
        if (!storage_.read_staged(encoded))
            error = TransferError::Storage;
        else if (!decode_photo(encoded.data(), encoded.size(), image_error))
            error = image_error == PhotoError::TooLarge ? TransferError::Dimensions : TransferError::Image;
    }
    if (error == TransferError::None) {
        if (!session_.valid(ticket, clock_()))
            error = TransferError::Interrupted;
        else
            error = storage_.commit();
    }
    session_.finished(ticket, area, error);
    if (error != TransferError::None)
        return error_response(error == TransferError::Exists    ? 409
                              : error == TransferError::Storage ? 507
                                                                : 400,
                              error);
    return {201, "{\"ok\":true,\"name\":\"" + json_escape(name) + "\",\"bytes\":" + std::to_string(received) + "}",
            false};
}
}  // namespace aura

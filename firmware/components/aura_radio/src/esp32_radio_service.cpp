#include "esp32_radio_service.hpp"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_pthread.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace aura::firmware {
namespace {

constexpr char kTag[] = "aura_radio";
constexpr std::size_t kMaxEvents = 16;
constexpr std::size_t kMaxSearchBytes = 512U * 1024U;
constexpr std::size_t kMaxHeadBytes = 8192;
constexpr int kMaxRedirects = 3;
constexpr int kConnectTimeoutMs = 6000;
// Short reads let a stop or another station take over within a second.
constexpr int kReadTimeoutMs = 1000;

// The ring of Esp32AudioBackend holds three seconds: stay below it.
RadioPumpLimits device_limits() {
    RadioPumpLimits limits;
    limits.high_water_ms = 2000;
    limits.low_water_ms = 1000;
    limits.max_quiet_reads = 10;  // ten seconds of silence from the server
    limits.max_bytes_before_audio = 512U * 1024U;
    return limits;
}

// Threads that only touch the network and PSRAM keep their stacks in PSRAM.
template <typename Body>
std::thread start_thread(const char* name, std::size_t stack, int priority, Body body) {
    esp_pthread_cfg_t previous = esp_pthread_get_default_config();
    const bool had = esp_pthread_get_cfg(&previous) == ESP_OK;
    esp_pthread_cfg_t config = esp_pthread_get_default_config();
    config.thread_name = name;
    config.stack_size = stack;
    config.prio = priority;
    config.pin_to_core = 0;
    config.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    esp_pthread_set_cfg(&config);
    std::thread thread(std::move(body));
    if (had) {
        esp_pthread_set_cfg(&previous);
    } else {
        const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
        esp_pthread_set_cfg(&defaults);
    }
    return thread;
}

// One directory request (`path`), tried on each radio-browser server in turn.
RadioSearchResult fetch_search_path(const std::string& path) {
    RadioSearchResult result;
    for (const auto& host : radio_browser_hosts()) {
        const std::string url = "https://" + host + path;
        esp_http_client_config_t config{};
        config.url = url.c_str();
        config.timeout_ms = 12000;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.user_agent = "AURA-Radio/1.0";
        config.buffer_size = 2048;
        config.keep_alive_enable = false;
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == nullptr) continue;
        esp_http_client_set_header(client, "Accept", "application/json");
        esp_http_client_set_header(client, "Accept-Encoding", "identity");
        std::string body;
        bool ok = esp_http_client_open(client, 0) == ESP_OK;
        if (ok) {
            const std::int64_t length = esp_http_client_fetch_headers(client);
            ok = length >= 0 && static_cast<std::uint64_t>(length) <= kMaxSearchBytes &&
                 esp_http_client_get_status_code(client) == 200;
            char buffer[1024];
            while (ok) {
                const int read = esp_http_client_read(client, buffer, sizeof(buffer));
                if (read == 0) break;
                if (read < 0 || body.size() + static_cast<std::size_t>(read) > kMaxSearchBytes) {
                    ok = false;
                    break;
                }
                body.append(buffer, static_cast<std::size_t>(read));
            }
            ok = ok && esp_http_client_is_complete_data_received(client);
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (!ok) continue;
        if (!parse_radio_browser_stations(body, result.stations)) {
            result.error_code = "RADIO_DIRECTORY_INVALID";
            return result;
        }
        result.ok = true;
        return result;
    }
    result.error_code = "RADIO_DIRECTORY_UNAVAILABLE";
    return result;
}

// An esp_transport connection, closed and destroyed however the attempt ends.
class Connection final {
public:
    explicit Connection(bool secure) : transport_(secure ? esp_transport_ssl_init() : esp_transport_tcp_init()) {
        if (transport_ && secure) esp_transport_ssl_crt_bundle_attach(transport_, esp_crt_bundle_attach);
    }
    ~Connection() {
        if (!transport_) return;
        if (connected_) esp_transport_close(transport_);
        esp_transport_destroy(transport_);
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool connect(const StreamUrl& url) {
        connected_ = transport_ &&
                     esp_transport_connect(transport_, url.host.c_str(), url.port, kConnectTimeoutMs) == 0;
        return connected_;
    }
    bool write_all(const std::string& data) {
        for (std::size_t sent = 0; sent < data.size();) {
            const int written = esp_transport_write(transport_, data.data() + sent,
                                                    static_cast<int>(data.size() - sent), kConnectTimeoutMs);
            if (written <= 0) return false;
            sent += static_cast<std::size_t>(written);
        }
        return true;
    }
    // ESP-IDF 5.5: 0 is a timeout and -1 a clean close (FIN); here they become
    // the pump's codes.
    int read(std::uint8_t* buffer, int size) {
        const int read = esp_transport_read(transport_, reinterpret_cast<char*>(buffer), size, kReadTimeoutMs);
        if (read > 0) return read;
        if (read == ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT) return kRadioReadTimeout;
        if (read == ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN) return 0;
        return kRadioReadError;
    }

private:
    esp_transport_handle_t transport_{nullptr};
    bool connected_{false};
};

}  // namespace

Esp32RadioService::Esp32RadioService(Esp32AudioBackend& audio) : audio_(audio) {
    worker_ = start_thread("aura_radio", 16384, 4, [this] { run(); });
}

Esp32RadioService::~Esp32RadioService() {
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        shutting_down_ = true;
        command_pending_ = true;
        ++generation_;
    }
    command_ready_.notify_one();
    if (worker_.joinable()) worker_.join();
    if (search_thread_.joinable()) search_thread_.join();
    std::lock_guard<std::mutex> audio_lock(audio_control_mutex_);
    audio_.stream_close();
}

std::uint64_t Esp32RadioService::play(const RadioStation& station) {
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> audio_lock(audio_control_mutex_);
        generation = generation_.fetch_add(1) + 1;
        audio_.stream_close();
    }
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        command_station_ = station;
        command_pending_ = true;
    }
    command_ready_.notify_one();
    return generation;
}

void Esp32RadioService::stop() {
    {
        std::lock_guard<std::mutex> audio_lock(audio_control_mutex_);
        ++generation_;
        audio_.stream_close();
    }
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        command_station_.reset();
        command_pending_ = true;
    }
    command_ready_.notify_one();
}

void Esp32RadioService::set_hold(bool hold) { audio_.stream_hold(hold); }

bool Esp32RadioService::search(std::string query) {
    std::lock_guard<std::mutex> lock(search_mutex_);
    if (searching_) return false;
    if (search_thread_.joinable()) search_thread_.join();  // the last one has finished
    searching_ = true;
    search_result_.reset();
    search_thread_ = start_thread("aura_radio_find", 12288, 2, [this, query = std::move(query)] {
        auto result = search_radio_directory(query, fetch_search_path);
        std::lock_guard<std::mutex> done(search_mutex_);
        search_result_ = std::move(result);
        searching_ = false;
    });
    return true;
}

bool Esp32RadioService::take_search_result(RadioSearchResult& result) {
    std::lock_guard<std::mutex> lock(search_mutex_);
    if (!search_result_) return false;
    result = std::move(*search_result_);
    search_result_.reset();
    return true;
}

bool Esp32RadioService::poll(RadioWorkerEvent& event) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.empty()) return false;
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

void Esp32RadioService::publish(RadioWorkerEvent event) {
    if (!current(event.generation)) return;
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.size() >= kMaxEvents) events_.pop_front();
    events_.push_back(std::move(event));
}

void Esp32RadioService::run() {
    while (true) {
        std::optional<RadioStation> station;
        std::uint64_t generation = 0;
        {
            std::unique_lock<std::mutex> lock(command_mutex_);
            command_ready_.wait(lock, [&] { return command_pending_ || shutting_down_; });
            if (shutting_down_) return;
            command_pending_ = false;
            station = command_station_;
            generation = generation_.load();
        }
        if (station) stream(*station, generation);
    }
}

void Esp32RadioService::stream(const RadioStation& station, std::uint64_t generation) {
    // A live stream that drops (a network blip, a server restart) is joined
    // again with growing pauses before the owner is told; a long stretch of
    // music starts the count again (as in the simulator).
    int attempt = 0;
    while (current(generation)) {
        bool heard_audio = false;
        const auto started = std::chrono::steady_clock::now();
        const auto code = stream_once(station, generation, heard_audio);
        if (!current(generation)) return;
        if (heard_audio && std::chrono::steady_clock::now() - started > std::chrono::seconds(30)) attempt = 0;
        const auto delay = radio_error_retryable(code) ? radio_retry_delay_ms(attempt) : 0U;
        if (delay == 0) {
            {
                std::lock_guard<std::mutex> audio_lock(audio_control_mutex_);
                if (current(generation)) audio_.stream_close();
            }
            ESP_LOGI(kTag, "stream ended: %s", code.c_str());
            publish({RadioWorkerEventType::Failed, generation, code});
            return;
        }
        ++attempt;
        publish({RadioWorkerEventType::Reconnecting, generation, code});
        for (std::uint32_t waited = 0; waited < delay && current(generation); waited += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

std::string Esp32RadioService::stream_once(const RadioStation& station, std::uint64_t generation,
                                           bool& heard_audio) {
    std::string address = station.url;
    for (int hop = 0; hop <= kMaxRedirects; ++hop) {
        StreamUrl url;
        if (!split_stream_url(address, url)) return "RADIO_INVALID_URL";
        Connection connection(url.secure);
        if (!connection.connect(url)) return current(generation) ? "RADIO_NETWORK" : std::string{};
        if (!connection.write_all(stream_request(url))) return "RADIO_NETWORK";
        // The head, bounded; whatever follows it is already stream data.
        std::string head;
        std::size_t head_end = std::string::npos;
        int quiet = 0;
        std::uint8_t chunk[1024];
        while (head_end == std::string::npos && head.size() < kMaxHeadBytes && current(generation)) {
            const int read = connection.read(chunk, sizeof(chunk));
            if (read == kRadioReadTimeout) {
                if (++quiet >= 8) return "RADIO_TIMEOUT";
                continue;
            }
            if (read <= 0) return "RADIO_NETWORK";
            head.append(reinterpret_cast<const char*>(chunk), static_cast<std::size_t>(read));
            head_end = head.find("\r\n\r\n");
        }
        if (!current(generation)) return {};
        if (head_end == std::string::npos) return "RADIO_HTTP";
        const std::string_view head_view(head.data(), head_end + 4);
        std::string location;
        if (parse_stream_redirect(head_view, location)) {
            address = std::move(location);
            continue;
        }
        StreamResponseHead parsed;
        if (!parse_stream_response_head(head_view, parsed)) return "RADIO_HTTP";
        std::string leftover = head.substr(head_end + 4);

        class Sink final : public IRadioAudioSink {
        public:
            Sink(Esp32RadioService& service, std::uint64_t generation) : service_(service), generation_(generation) {}
            bool open(std::uint32_t sample_rate, std::uint8_t channels) override {
                std::lock_guard<std::mutex> audio_lock(service_.audio_control_mutex_);
                return service_.current(generation_) && service_.audio_.stream_open(sample_rate, channels);
            }
            bool push(const std::int16_t* samples, std::size_t frames) override {
                // Not under the lock: a full ring waits for the output, and stop()
                // must not wait for that. A stale push only fills a closed ring.
                return service_.current(generation_) && service_.audio_.stream_push(samples, frames);
            }
            std::uint32_t buffered_ms() const override { return service_.audio_.stream_buffered_ms(); }

        private:
            Esp32RadioService& service_;
            std::uint64_t generation_;
        } sink(*this, generation);
        RadioPumpIo io;
        io.read = [&](std::uint8_t* buffer, int size) -> int {
            if (!leftover.empty()) {
                const auto count = std::min<std::size_t>(leftover.size(), static_cast<std::size_t>(size));
                std::memcpy(buffer, leftover.data(), count);
                leftover.erase(0, count);
                return static_cast<int>(count);
            }
            return connection.read(buffer, size);
        };
        io.current = [this, generation] { return current(generation); };
        io.title = [this, generation](std::string title) {
            publish({RadioWorkerEventType::Title, generation, std::move(title)});
        };
        io.playing = [this, generation] { publish({RadioWorkerEventType::Playing, generation, {}}); };
        io.wait = [](std::uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
        return radio_pump(parsed.metaint, sink, io, device_limits(), heard_audio);
    }
    return "RADIO_HTTP";  // too many redirects
}

}  // namespace aura::firmware

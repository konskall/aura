#pragma once

#include "aura/radio.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace aura::simulator {

class WindowsAudioBackend;

struct RadioSearchResult {
    bool ok{false};
    std::vector<RadioStation> stations;
    std::string error_code;
};

// Reconnecting: the stream dropped and is being joined again (text: why).
enum class RadioWorkerEventType { Playing, Title, Reconnecting, Failed };

struct RadioWorkerEvent {
    RadioWorkerEventType type{RadioWorkerEventType::Failed};
    std::uint64_t generation{0};
    std::string text;
};

// Blocking WinHTTP and MP3 work lives on this worker. It communicates with
// the UI thread through a bounded event queue and feeds only the thread-safe
// continuous channel of WindowsAudioBackend; it never owns an LVGL object.
class WindowsRadioService {
public:
    explicit WindowsRadioService(WindowsAudioBackend& audio);
    ~WindowsRadioService();
    WindowsRadioService(const WindowsRadioService&) = delete;
    WindowsRadioService& operator=(const WindowsRadioService&) = delete;

    std::uint64_t play(const RadioStation& station);
    void stop();
    void set_hold(bool hold);
    std::future<RadioSearchResult> search(std::string query) const;
    bool poll(RadioWorkerEvent& event);
    std::uint64_t generation() const noexcept { return generation_.load(); }

private:
    // Reads one chunk: > 0 bytes, 0 at the end of the stream, or a negative
    // value for a quiet stream (timeout) or a failure.
    using StreamReader = std::function<int(std::uint8_t* buffer, int size)>;

    void run();
    void stream(const RadioStation& station, std::uint64_t generation);
    std::string stream_once(const RadioStation& station, std::uint64_t generation, bool& heard_audio);
    std::string stream_icy(const std::wstring& host, const std::wstring& path, std::uint16_t port,
                           std::uint64_t generation, bool& heard_audio);
    std::string pump(const StreamReader& read, std::size_t metaint, std::uint64_t generation, bool& heard_audio);
    void publish(RadioWorkerEvent event);
    bool current(std::uint64_t generation) const noexcept;
    // The request or socket being read is handed over here, so play() and
    // stop() can cut a blocking read short; whoever closes it first owns it.
    bool adopt_request(void* request, std::uint64_t generation);
    void release_request(void* request);
    bool adopt_socket(std::uintptr_t socket, std::uint64_t generation);
    void release_socket(std::uintptr_t socket);
    void abort_active();

    static constexpr std::uintptr_t kNoSocket = ~static_cast<std::uintptr_t>(0);

    WindowsAudioBackend& audio_;
    mutable std::mutex command_mutex_;
    std::condition_variable command_ready_;
    std::optional<RadioStation> command_station_;
    bool command_pending_{false};
    bool shutting_down_{false};
    std::atomic<std::uint64_t> generation_{0};
    // Serializes stop/play stream teardown with the worker's open/push calls.
    // This prevents a cancelled generation from reopening audio after stop().
    mutable std::mutex audio_control_mutex_;
    std::mutex abort_mutex_;
    void* active_request_{nullptr};
    std::uintptr_t active_socket_{kNoSocket};
    bool winsock_ready_{false};

    std::mutex event_mutex_;
    std::deque<RadioWorkerEvent> events_;
    // Last: the worker starts only after every member above exists.
    std::thread worker_;
};

}  // namespace aura::simulator

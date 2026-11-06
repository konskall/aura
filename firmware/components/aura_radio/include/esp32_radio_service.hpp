#pragma once

#include "aura/radio.hpp"
#include "esp32_audio_backend.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace aura::firmware {

// The device's internet radio, the simulator's WindowsRadioService on the
// board. One worker joins a station's stream: http or https, Shoutcast's
// "ICY 200 OK" too, following up to three redirects. The shared radio_pump
// (aura/radio.hpp) then decodes it into the audio backend's stream, dropped
// streams are joined again with growing pauses, and a directory search runs on
// a short thread of its own. The UI thread polls; nothing here touches LVGL.
class Esp32RadioService {
public:
    explicit Esp32RadioService(Esp32AudioBackend& audio);
    ~Esp32RadioService();
    Esp32RadioService(const Esp32RadioService&) = delete;
    Esp32RadioService& operator=(const Esp32RadioService&) = delete;

    // The generation the events of this station carry.
    std::uint64_t play(const RadioStation& station);
    void stop();
    std::uint64_t generation() const noexcept { return generation_.load(); }
    // Silenced but kept while the microphone is open.
    void set_hold(bool hold);
    // A directory search by name; false while one is still running.
    bool search(std::string query);
    bool take_search_result(RadioSearchResult& result);
    bool poll(RadioWorkerEvent& event);

private:
    void run();
    void stream(const RadioStation& station, std::uint64_t generation);
    std::string stream_once(const RadioStation& station, std::uint64_t generation, bool& heard_audio);
    bool current(std::uint64_t generation) const noexcept { return generation_.load() == generation; }
    void publish(RadioWorkerEvent event);

    Esp32AudioBackend& audio_;
    // stop() closes the output and changes the generation under this lock, so
    // a push can never land on a station that was left.
    std::mutex audio_control_mutex_;
    std::atomic<std::uint64_t> generation_{0};
    std::mutex command_mutex_;
    std::condition_variable command_ready_;
    std::optional<RadioStation> command_station_;
    bool command_pending_{false};
    bool shutting_down_{false};
    std::mutex event_mutex_;
    std::deque<RadioWorkerEvent> events_;
    std::mutex search_mutex_;
    bool searching_{false};
    std::optional<RadioSearchResult> search_result_;
    std::thread search_thread_;
    std::thread worker_;
};

}  // namespace aura::firmware

#pragma once

#include "aura/phone_link.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aura::simulator {

struct WindowsAncsEvent {
    enum class Kind { Link, Upsert, Remove };
    Kind kind{Kind::Link};
    PhoneLinkState link{PhoneLinkState::Unavailable};
    PhoneNotification notification{};
    std::uint32_t uid{0};
    std::string error_code;
};

// Experimental Windows ANCS transport. It opens only paired BLE service
// interfaces, keeps notification contents in memory, and publishes bounded
// events for the simulator UI thread. It never writes notification text to a
// file or console.
class WindowsAncsService {
public:
    WindowsAncsService();
    ~WindowsAncsService();
    WindowsAncsService(const WindowsAncsService&) = delete;
    WindowsAncsService& operator=(const WindowsAncsService&) = delete;

    void start();
    void stop();
    bool poll(WindowsAncsEvent& event);
    bool perform_action(std::uint32_t uid, bool positive);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aura::simulator

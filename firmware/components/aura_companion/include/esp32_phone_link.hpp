#pragma once

#include "aura/phone_link.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace aura::firmware {

struct PhoneEvent {
    enum class Kind { Link, Upsert, Remove };
    Kind kind{Kind::Link};
    PhoneLinkState link{PhoneLinkState::NotPaired};
    PhonePlatform platform{PhonePlatform::Unknown};
    PhoneNotification notification;
    std::uint32_t uid{0};
};

// iPhone notifications over BLE (Apple Notification Center Service). AURA
// advertises with an ANCS solicitation, the owner pairs it in the iPhone's
// Bluetooth settings, and AURA subscribes to ANCS as a GATT client. Bytes are
// interpreted by the shared aura::ancs helpers; events reach the UI loop
// through poll(). Android needs the companion app (docs/phone/ANDROID_COMPANION.md).
// NOT COMPILED, NOT VERIFIED ON HARDWARE.
class Esp32PhoneLink {
public:
    bool start();
    bool available() const noexcept { return started_; }
    void set_pairable(bool pairable);
    void forget();
    void perform_action(std::uint32_t uid, bool positive);
    bool poll(PhoneEvent& event);

    // Called from the NimBLE host task.
    void push(PhoneEvent event);
    static Esp32PhoneLink* instance;

private:
    bool started_{false};
    std::mutex mutex_;
    std::vector<PhoneEvent> events_;
    std::uint32_t dropped_events_{0};
};

}  // namespace aura::firmware

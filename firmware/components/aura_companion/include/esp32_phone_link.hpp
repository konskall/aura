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
// Compiled into the firmware; NOT VERIFIED ON HARDWARE (no phone paired yet).
// The radio runs only when Esp32Companion asks (the owner's switch and a bond,
// 2026-11-19): start() and stop() may follow each other.
class Esp32PhoneLink {
public:
    // `pairable`: advertise for a new phone at once.
    bool start(bool pairable = false);
    // Ends the link and the radio (NimBLE and the controller); start() again later.
    void stop();
    // Whether NimBLE's store in NVS holds a bonded phone, read without the radio.
    static bool has_bond();
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

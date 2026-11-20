#pragma once

#include "aura/clock_services.hpp"
#include "aura/radio.hpp"

#include <cstdint>
#include <optional>

namespace aura {

// An alarm that wakes with a station (the owner, 2026-11-19). While its alert
// rings the platform plays the station and the melody waits; the melody rings
// instead when the device is muted (it always rings), when the station fails,
// or when it is not playing within kStationTimeoutMs. Dismissing or snoozing
// stops the station the alarm started. Platforms call step() every loop.
class AlarmStationPlayer {
public:
    static constexpr std::uint64_t kStationTimeoutMs = 20000;

    struct Decision {
        bool start_station{false};  // play `station` now
        bool stop_station{false};   // stop the radio now (the alarm started it)
        bool melody{true};          // the melody may ring (AlarmRinger)
        AlarmStation station;
    };

    Decision step(const std::optional<ClockAlert>& alert, RadioState radio, bool muted, std::uint64_t now_ms);

private:
    bool active_{false};   // an alert with a station is being handled
    bool started_{false};  // the radio was started for it, and not stopped yet
    bool given_up_{false}; // the melody rings for this alert
    std::uint32_t alert_id_{0};
    ClockAlertKind alert_kind_{ClockAlertKind::Alarm};
    std::uint64_t started_ms_{0};
};

}  // namespace aura

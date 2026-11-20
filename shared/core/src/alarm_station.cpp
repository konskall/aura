#include "aura/alarm_station.hpp"

namespace aura {

AlarmStationPlayer::Decision AlarmStationPlayer::step(const std::optional<ClockAlert>& alert, RadioState radio,
                                                      bool muted, std::uint64_t now_ms) {
    Decision decision;
    const bool with_station = alert && alert->kind == ClockAlertKind::Alarm && !alert->station.url.empty();
    const bool same = active_ && alert && alert->id == alert_id_ && alert->kind == alert_kind_;
    if (!same) {
        // The alert this player handled is gone (dismissed, snoozed) or another
        // one rings: the station it started stops.
        if (started_) decision.stop_station = true;
        active_ = started_ = given_up_ = false;
        if (!with_station) return decision;
        active_ = true;
        alert_id_ = alert->id;
        alert_kind_ = alert->kind;
        if (muted) {
            given_up_ = true;
            return decision;
        }
        started_ = true;
        started_ms_ = now_ms;
        decision.start_station = true;
        decision.station = alert->station;
        decision.melody = false;
        return decision;
    }
    if (given_up_) return decision;
    if (radio == RadioState::Playing) {
        decision.melody = false;
        return decision;
    }
    const bool failed = radio == RadioState::Error || (started_ && now_ms - started_ms_ >= kStationTimeoutMs);
    if (failed) {
        given_up_ = true;
        if (started_ && radio != RadioState::Error) decision.stop_station = true;
        started_ = false;
        return decision;
    }
    decision.melody = false;
    return decision;
}

}  // namespace aura

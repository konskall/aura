#include "aura/device_status.hpp"
#include "aura/motion_model.hpp"
#include "aura/navigation_stack.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

enum class TestRoute { Home, Launcher, Audio, Settings };
}

int main() {
    aura::NavigationStack<TestRoute, 4> navigation(TestRoute::Home);
    navigation.navigate(TestRoute::Launcher);
    navigation.navigate(TestRoute::Audio);
    expect(navigation.current() == TestRoute::Audio, "navigation reaches destination");
    expect(navigation.back() == TestRoute::Launcher, "back restores previous route");
    navigation.navigate(TestRoute::Settings);
    navigation.navigate(TestRoute::Home);
    expect(navigation.current() == TestRoute::Home && navigation.depth() == 0,
           "home clears route history");

    aura::MotionModel motion;
    motion.enter(aura::MotionMode::Idle);
    const auto idle_a = motion.tick(80);
    const auto idle_b = motion.tick(720);
    expect(motion.active() && motion.update_count() == 2, "motion lifecycle counts updates");
    expect(idle_a.scale != idle_b.scale || idle_a.opacity != idle_b.opacity,
           "idle animation changes over time");
    motion.enter(aura::MotionMode::Listening);
    motion.set_amplitude(0.82F);
    const auto listening = motion.tick(33);
    expect(listening.pulse >= 0.8F, "listening follows bounded amplitude");
    motion.set_reduced(true);
    const auto reduced = motion.tick(33);
    expect(reduced.scale == 256, "reduced motion freezes scale");
    motion.leave();
    expect(!motion.active() && motion.mode() == aura::MotionMode::None,
           "motion resources leave cleanly");

    aura::DeviceStatusModel status;
    expect(!status.snapshot().clock_valid && !status.snapshot().power_known,
           "device status starts unavailable");
    status.set_clock(26, -5, aura::StatusOrigin::Live);
    status.set_power(140, true, aura::StatusOrigin::Simulated);
    status.set_connection(false, aura::StatusOrigin::Simulated, 3);
    status.set_bluetooth(true, aura::StatusOrigin::Simulated);
    expect(status.snapshot().hour == 23 && status.snapshot().minute == 0,
           "clock values are bounded");
    expect(status.snapshot().battery_percent == 100 && status.snapshot().charging,
           "power values are bounded and retained");
    expect(status.snapshot().connection_known && !status.snapshot().online,
           "connection state is explicit");
    expect(status.snapshot().wifi_signal_level == 0 && status.snapshot().bluetooth_enabled,
           "device radios expose bounded truthful state");
    status.set_connection(true, aura::StatusOrigin::Live, 9);
    expect(status.snapshot().wifi_signal_level == 3,
           "Wi-Fi signal level is bounded");
    status.set_power_unavailable();
    expect(!status.snapshot().power_known && !status.snapshot().charging,
           "unavailable power never looks measured");
    status.set_clock_unavailable();
    status.set_connection_unavailable();
    status.set_bluetooth_unavailable();
    expect(!status.snapshot().clock_valid && !status.snapshot().connection_known &&
           !status.snapshot().bluetooth_known,
           "unknown clock and radio states remain truthful");

    aura::DeviceStatusModel dated;
    expect(!dated.snapshot().date_valid, "date starts unavailable");
    dated.set_date(2026, 10, 22, 4, aura::StatusOrigin::Live);
    const auto dated_sequence = dated.snapshot().sequence;
    expect(dated.snapshot().date_valid && dated.snapshot().year == 2026 && dated.snapshot().month == 10 &&
           dated.snapshot().day == 22 && dated.snapshot().weekday == 4, "date is retained");
    dated.set_date(2026, 10, 22, 4, aura::StatusOrigin::Live);
    expect(dated.snapshot().sequence == dated_sequence, "an unchanged date does not redraw");
    dated.set_date(2026, 13, 22, 4, aura::StatusOrigin::Live);
    dated.set_date(2026, 10, 0, 4, aura::StatusOrigin::Live);
    dated.set_date(2026, 10, 22, 7, aura::StatusOrigin::Live);
    expect(dated.snapshot().month == 10 && dated.snapshot().day == 22 && dated.snapshot().weekday == 4 &&
           dated.snapshot().sequence == dated_sequence, "an invalid calendar date is rejected");
    dated.set_date_unavailable();
    expect(!dated.snapshot().date_valid && dated.snapshot().sequence == dated_sequence + 1,
           "an unknown date stays truthful");

    std::cout << "ui_recovery_models_test: PASS\n";
    return 0;
}

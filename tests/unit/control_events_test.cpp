#include "aura/control_events.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool require(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

}  // namespace

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("aura-control-events-" + std::to_string(nonce) + ".log");

    {
        std::ofstream output(path, std::ios::binary);
        output << "1\terror\tstale%20event\n";
    }

    aura::simulator::ControlEventReader reader(path.string());
    bool ok = require(reader.poll().empty(), "events that predate the reader must not be replayed");

    {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output << "2\taoraton_incoming\t%CE%93%CE%B5%CE%B9%CE%AC\n";
    }

    const auto events = reader.poll();
    ok &= require(events.size() == 1, "one newly appended event should be returned");
    if (events.size() == 1) {
        ok &= require(events[0].type == "aoraton_incoming", "event type should be preserved");
        ok &= require(events[0].payload == "Γειά", "percent-encoded UTF-8 should be decoded");
    }
    ok &= require(reader.poll().empty(), "a consumed event must not be replayed");

    std::error_code error;
    std::filesystem::remove(path, error);
    ok &= require(!error, "temporary event log should be removable");
    return ok ? 0 : 1;
}

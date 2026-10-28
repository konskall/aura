#include "aura/app_layout.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

// Not assert(): the authoritative suite builds Release, where NDEBUG would
// compile every check away.
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::string layout_text(const std::initializer_list<const char*> codes) {
    std::string text = "AURA_APP_LAYOUT_V1\n";
    for (const auto* code : codes) {
        text += code;
        text += '\n';
    }
    return text;
}

}  // namespace

int main() {
    using namespace aura;

    for (std::size_t index = 0; index < kLauncherAppCount; ++index) {
        const auto app = static_cast<LauncherApp>(index);
        const auto parsed = parse_launcher_app(launcher_app_code(app));
        expect(parsed && *parsed == app, "every app code round-trips");
    }
    expect(!parse_launcher_app("camera") && !parse_launcher_app(""), "unknown codes are rejected");
    expect(std::string(launcher_app_code(LauncherApp::Count)) == "unknown", "the sentinel has no code");

    LauncherLayoutModel model;
    expect(model.snapshot().order == kDefaultLauncherOrder, "the default order comes first");

    // Forward: the moved app lands on the target and the ones between shift left.
    auto sequence = model.snapshot().sequence;
    expect(model.move(0, 5), "a valid move is accepted");
    const auto& moved = model.snapshot().order;
    expect(moved[0] == LauncherApp::Weather && moved[4] == LauncherApp::Translate &&
           moved[5] == LauncherApp::Conversation && moved[6] == LauncherApp::Today,
           "moving forward shifts the apps in between");
    expect(model.snapshot().sequence == sequence + 1, "a move is a change");

    // Backward: the apps in between shift right.
    expect(model.move(5, 0), "moving back is accepted");
    expect(model.snapshot().order == kDefaultLauncherOrder, "moving back restores the order");
    expect(model.move(10, 1), "the last app can move near the front");
    expect(model.snapshot().order[1] == LauncherApp::Organizer &&
           model.snapshot().order[2] == LauncherApp::Weather &&
           model.snapshot().order[10] == LauncherApp::Settings,
           "moving backward shifts the apps in between");

    sequence = model.snapshot().sequence;
    expect(!model.move(3, 3) && !model.move(99, 0) && !model.move(0, kLauncherAppCount),
           "no-op and out-of-range moves are refused");
    expect(model.snapshot().sequence == sequence, "a refused move changes nothing");

    // Every app stays exactly once, whatever the moves.
    for (std::size_t step = 0; step < 40; ++step) model.move((step * 7) % kLauncherAppCount, (step * 3) % kLauncherAppCount);
    auto sorted = model.snapshot().order;
    std::sort(sorted.begin(), sorted.end());
    auto expected = kDefaultLauncherOrder;
    std::sort(expected.begin(), expected.end());
    expect(sorted == expected, "moves never lose or duplicate an app");

    // Persistence.
    const auto saved = model.serialize();
    LauncherLayoutModel restored;
    expect(restored.restore(saved) && restored.snapshot().order == model.snapshot().order,
           "the stored order survives a restart");
    sequence = restored.snapshot().sequence;
    expect(restored.restore(saved) && restored.snapshot().sequence == sequence,
           "restoring the same order is not a change");

    std::string crlf;
    for (const char ch : saved) {
        if (ch == '\n') crlf += '\r';
        crlf += ch;
    }
    LauncherLayoutModel windows_copy;
    expect(windows_copy.restore(crlf + "\r\n") && windows_copy.snapshot().order == model.snapshot().order,
           "CRLF line endings and a trailing blank line are accepted");

    const auto before = restored.snapshot();
    expect(!restored.restore(layout_text({"conversation", "conversation"})), "a duplicate app is rejected");
    expect(!restored.restore(layout_text({"conversation", "weather", "messages", "clock", "notes", "translate",
                                          "today", "radio", "notifications", "settings"})),
           "a missing app is rejected");
    expect(!restored.restore(layout_text({"conversation", "weather", "messages", "clock", "notes", "translate",
                                          "today", "radio", "notifications", "settings", "camera"})),
           "an unknown app is rejected");
    expect(!restored.restore(saved + "organizer\n"), "extra entries are rejected");
    expect(!restored.restore("AURA_APP_LAYOUT_V2\n"), "another version is rejected");
    expect(!restored.restore(""), "an empty store is rejected");
    expect(!restored.restore(std::string(600, 'x')), "an oversized store is rejected");
    expect(restored.snapshot().order == before.order && restored.snapshot().sequence == before.sequence,
           "a rejected store changes nothing");

    sequence = restored.snapshot().sequence;
    restored.reset();
    expect(restored.snapshot().order == kDefaultLauncherOrder && restored.snapshot().sequence == sequence + 1,
           "reset returns to the default order");
    restored.reset();
    expect(restored.snapshot().sequence == sequence + 1, "resetting the default order is not a change");

    std::cout << "aura_app_layout_tests passed\n";
    return 0;
}

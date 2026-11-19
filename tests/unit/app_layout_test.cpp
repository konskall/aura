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
    // The owner's order (2026-11-11), left to right and row by row, with Αρχεία
    // to the right of Φωτογραφίες (2026-11-18): side by side in the three columns
    // and in Γραφίτης's two.
    expect(kDefaultLauncherOrder == std::array<LauncherApp, kLauncherAppCount>{
               LauncherApp::Conversation, LauncherApp::Notes, LauncherApp::Messages,
               LauncherApp::Today, LauncherApp::Weather, LauncherApp::Notifications,
               LauncherApp::Photos, LauncherApp::Files, LauncherApp::Clock,
               LauncherApp::Radio, LauncherApp::Organizer, LauncherApp::Settings},
           "the default order is the owner's");
    const auto photos_at = static_cast<std::size_t>(
        std::find(kDefaultLauncherOrder.begin(), kDefaultLauncherOrder.end(), LauncherApp::Photos) -
        kDefaultLauncherOrder.begin());
    expect(kDefaultLauncherOrder[photos_at + 1] == LauncherApp::Files && photos_at % 3 != 2 && photos_at % 2 == 0,
           "Αρχεία is to the right of Φωτογραφίες on one row, in three columns or two");

    // Forward: the moved app lands on the target and the ones between shift left.
    auto sequence = model.snapshot().sequence;
    expect(model.move(0, 5), "a valid move is accepted");
    const auto& moved = model.snapshot().order;
    expect(moved[0] == LauncherApp::Notes && moved[4] == LauncherApp::Notifications &&
           moved[5] == LauncherApp::Conversation && moved[6] == LauncherApp::Photos,
           "moving forward shifts the apps in between");
    expect(model.snapshot().sequence == sequence + 1, "a move is a change");

    // Backward: the apps in between shift right.
    expect(model.move(5, 0), "moving back is accepted");
    expect(model.snapshot().order == kDefaultLauncherOrder, "moving back restores the order");
    expect(model.move(11, 1), "the last app can move near the front");
    expect(model.snapshot().order[1] == LauncherApp::Settings &&
           model.snapshot().order[2] == LauncherApp::Notes &&
           model.snapshot().order[11] == LauncherApp::Organizer,
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

    // Orders stored before Αρχεία existed. The old default becomes the new one;
    // an order the owner arranged keeps its place, with Αρχεία after Φωτογραφίες.
    LauncherLayoutModel upgraded;
    expect(upgraded.restore(layout_text({"conversation", "notes", "messages", "today", "weather", "notifications",
                                         "clock", "radio", "photos", "organizer", "settings"})) &&
               upgraded.snapshot().order == kDefaultLauncherOrder,
           "the previous default order becomes the new default");
    LauncherLayoutModel arranged;
    expect(arranged.restore(layout_text({"settings", "photos", "conversation", "notes", "messages", "today", "weather",
                                         "notifications", "clock", "radio", "organizer"})),
           "an arranged order from before Αρχεία is accepted");
    expect(arranged.snapshot().order[0] == LauncherApp::Settings && arranged.snapshot().order[1] == LauncherApp::Photos &&
               arranged.snapshot().order[2] == LauncherApp::Files &&
               arranged.snapshot().order[3] == LauncherApp::Conversation &&
               arranged.snapshot().order[11] == LauncherApp::Organizer,
           "Αρχεία joins right after Φωτογραφίες and the rest keep their order");
    LauncherLayoutModel older;
    expect(older.restore(layout_text({"conversation", "weather", "messages", "clock", "notes", "today", "radio",
                                      "notifications", "settings", "organizer"})),
           "an order from before Φωτογραφίες is accepted");
    expect(older.snapshot().order[9] == LauncherApp::Organizer && older.snapshot().order[10] == LauncherApp::Photos &&
               older.snapshot().order[11] == LauncherApp::Files,
           "both join at the end, side by side");

    const auto before = restored.snapshot();
    expect(!restored.restore(layout_text({"conversation", "conversation"})), "a duplicate app is rejected");
    expect(!restored.restore(layout_text({"conversation", "weather", "messages", "clock", "notes", "today",
                                          "radio", "notifications", "settings"})),
           "a missing app is rejected");
    expect(!restored.restore(layout_text({"conversation", "weather", "messages", "clock", "notes", "today",
                                          "radio", "notifications", "settings", "camera"})),
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

#include "aura/app_layout.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace aura {
namespace {

constexpr std::string_view kHeader{"AURA_APP_LAYOUT_V1"};
constexpr std::array<std::string_view, kLauncherAppCount> kCodes{
    "conversation", "weather", "messages", "clock", "notes", "today",
    "radio", "notifications", "settings", "organizer", "photos", "files",
};

// The default order before Αρχεία (2026-11-18): an order stored without a
// change of the owner's becomes the new default.
constexpr std::array<LauncherApp, kLauncherAppCount - 1> kPreviousDefaultOrder{
    LauncherApp::Conversation, LauncherApp::Notes, LauncherApp::Messages,
    LauncherApp::Today, LauncherApp::Weather, LauncherApp::Notifications,
    LauncherApp::Clock, LauncherApp::Radio, LauncherApp::Photos,
    LauncherApp::Organizer, LauncherApp::Settings,
};

std::string_view trim_line(std::string_view value) noexcept {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.remove_suffix(1);
    return value;
}

}  // namespace

const char* launcher_app_code(LauncherApp app) noexcept {
    const auto index = static_cast<std::size_t>(app);
    return index < kCodes.size() ? kCodes[index].data() : "unknown";
}

std::optional<LauncherApp> parse_launcher_app(std::string_view code) noexcept {
    code = trim_line(code);
    for (std::size_t index = 0; index < kCodes.size(); ++index) {
        if (code == kCodes[index]) return static_cast<LauncherApp>(index);
    }
    return std::nullopt;
}

bool LauncherLayoutModel::move(std::size_t from, std::size_t to) noexcept {
    if (from >= kLauncherAppCount || to >= kLauncherAppCount || from == to) return false;
    const auto app = snapshot_.order[from];
    if (from < to) {
        std::move(snapshot_.order.begin() + static_cast<std::ptrdiff_t>(from + 1),
                  snapshot_.order.begin() + static_cast<std::ptrdiff_t>(to + 1),
                  snapshot_.order.begin() + static_cast<std::ptrdiff_t>(from));
    } else {
        std::move_backward(snapshot_.order.begin() + static_cast<std::ptrdiff_t>(to),
                           snapshot_.order.begin() + static_cast<std::ptrdiff_t>(from),
                           snapshot_.order.begin() + static_cast<std::ptrdiff_t>(from + 1));
    }
    snapshot_.order[to] = app;
    ++snapshot_.sequence;
    return true;
}

void LauncherLayoutModel::reset() noexcept {
    if (snapshot_.order == kDefaultLauncherOrder) return;
    snapshot_.order = kDefaultLauncherOrder;
    ++snapshot_.sequence;
}

std::string LauncherLayoutModel::serialize() const {
    std::string value(kHeader);
    value.push_back('\n');
    for (const auto app : snapshot_.order) {
        value += launcher_app_code(app);
        value.push_back('\n');
    }
    return value;
}

bool LauncherLayoutModel::restore(std::string_view data) noexcept {
    if (data.empty() || data.size() > 512) return false;
    const auto next_line = [](std::string_view& remaining) noexcept -> std::optional<std::string_view> {
        if (remaining.empty()) return std::nullopt;
        const auto newline = remaining.find('\n');
        const auto line = trim_line(remaining.substr(0, newline));
        remaining = newline == std::string_view::npos ? std::string_view{} : remaining.substr(newline + 1);
        return line;
    };
    auto remaining = data;
    const auto header = next_line(remaining);
    if (!header || *header != kHeader) return false;
    std::array<LauncherApp, kLauncherAppCount> parsed{};
    std::array<bool, kLauncherAppCount> seen{};
    std::size_t count = 0;
    while (!remaining.empty()) {
        const auto line = next_line(remaining);
        if (!line || line->empty()) continue;
        const auto app = parse_launcher_app(*line);
        if (!app || count >= parsed.size()) return false;
        const auto app_index = static_cast<std::size_t>(*app);
        if (app_index >= seen.size() || seen[app_index]) return false;
        seen[app_index] = true;
        parsed[count++] = *app;
    }
    const auto known = [&seen](LauncherApp app) { return seen[static_cast<std::size_t>(app)]; };
    // V1 orders saved before Photos and Files existed keep their owner's order:
    // Photos joins at the end and Files right after it.
    if (!known(LauncherApp::Photos)) {
        if (known(LauncherApp::Files) || count + 2 != kLauncherAppCount) return false;
        parsed[count++] = LauncherApp::Photos;
        seen[static_cast<std::size_t>(LauncherApp::Photos)] = true;
    }
    if (!known(LauncherApp::Files)) {
        if (count + 1 != kLauncherAppCount) return false;
        const bool previous_default = std::equal(kPreviousDefaultOrder.begin(), kPreviousDefaultOrder.end(), parsed.begin());
        const auto photos = std::find(parsed.begin(), parsed.begin() + static_cast<std::ptrdiff_t>(count), LauncherApp::Photos);
        std::move_backward(photos + 1, parsed.begin() + static_cast<std::ptrdiff_t>(count),
                           parsed.begin() + static_cast<std::ptrdiff_t>(count + 1));
        *(photos + 1) = LauncherApp::Files;
        ++count;
        if (previous_default) parsed = kDefaultLauncherOrder;
    }
    if (count != kLauncherAppCount) return false;
    if (parsed == snapshot_.order) return true;
    snapshot_.order = parsed;
    ++snapshot_.sequence;
    return true;
}

}  // namespace aura

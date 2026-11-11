#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace aura {

enum class LauncherApp : std::uint8_t {
    Conversation,
    Weather,
    Messages,
    Clock,
    Notes,
    Today,
    Radio,
    Notifications,
    Settings,
    Organizer,
    Photos,
    Count,
};

constexpr std::size_t kLauncherAppCount = static_cast<std::size_t>(LauncherApp::Count);

// The owner's order (2026-11-11), three to a row. A stored order is kept.
constexpr std::array<LauncherApp, kLauncherAppCount> kDefaultLauncherOrder{
    LauncherApp::Conversation, LauncherApp::Notes, LauncherApp::Messages,
    LauncherApp::Today, LauncherApp::Weather, LauncherApp::Notifications,
    LauncherApp::Clock, LauncherApp::Radio, LauncherApp::Photos,
    LauncherApp::Organizer, LauncherApp::Settings,
};

struct LauncherLayoutSnapshot {
    std::array<LauncherApp, kLauncherAppCount> order{kDefaultLauncherOrder};
    std::uint64_t sequence{0};
};

class LauncherLayoutModel {
public:
    const LauncherLayoutSnapshot& snapshot() const noexcept { return snapshot_; }
    bool move(std::size_t from, std::size_t to) noexcept;
    void reset() noexcept;
    std::string serialize() const;
    bool restore(std::string_view data) noexcept;

private:
    LauncherLayoutSnapshot snapshot_{};
};

const char* launcher_app_code(LauncherApp app) noexcept;
std::optional<LauncherApp> parse_launcher_app(std::string_view code) noexcept;

}  // namespace aura


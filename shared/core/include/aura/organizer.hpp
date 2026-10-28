#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {

enum class OrganizerKind { Event, Task, Shopping };
enum class OrganizerResult { Ok, Full, Empty, InvalidTime, NotFound };

struct OrganizerEvent {
    std::uint32_t id{0};
    std::int64_t start_minute{0};
    std::uint32_t duration_minutes{60};
    std::string title;
};

struct OrganizerTask {
    std::uint32_t id{0};
    std::int64_t created_minute{0};
    std::int64_t due_minute{0};  // zero means no due date
    bool completed{false};
    std::string text;
};

struct ShoppingItem {
    std::uint32_t id{0};
    bool checked{false};
    std::string text;
};

struct OrganizerSnapshot {
    std::vector<OrganizerEvent> events;       // chronological
    std::vector<OrganizerTask> tasks;         // incomplete first, then newest
    std::vector<ShoppingItem> shopping;       // unchecked first, then newest
    std::uint64_t sequence{0};
};

// A bounded, offline-first personal organizer shared by the simulator and the
// ESP32. The model performs no network or file I/O; platform adapters persist
// its versioned text representation. All display text is sanitized UTF-8.
class OrganizerModel {
public:
    static constexpr std::size_t kMaxEvents = 64;
    static constexpr std::size_t kMaxTasks = 64;
    static constexpr std::size_t kMaxShopping = 64;
    static constexpr std::size_t kMaxTextBytes = 160;
    static constexpr std::size_t kContextBytes = 1800;
    static constexpr std::uint32_t kMaxDurationMinutes = 7U * 24U * 60U;
    // The interface packs an item's id into 30 bits.
    static constexpr std::uint32_t kMaxId = (1U << 30) - 1U;

    OrganizerResult add_event(const std::string& title, std::int64_t start_minute,
                              std::uint32_t duration_minutes = 60, std::uint32_t* id = nullptr);
    OrganizerResult add_task(const std::string& text, std::int64_t created_minute,
                             std::int64_t due_minute = 0, std::uint32_t* id = nullptr);
    OrganizerResult add_shopping(const std::string& text, std::uint32_t* id = nullptr);
    OrganizerResult set_checked(OrganizerKind kind, std::uint32_t id, bool checked);
    OrganizerResult toggle(OrganizerKind kind, std::uint32_t id);
    OrganizerResult remove(OrganizerKind kind, std::uint32_t id);
    // Removes events that ended at or before `minute`, so the bounded agenda
    // never fills up with the past. Returns how many were removed.
    std::size_t prune_events_before(std::int64_t minute);

    std::vector<OrganizerEvent> events_between(std::int64_t first_minute,
                                               std::int64_t last_minute) const;
    std::string context_for(const std::string& question, std::int64_t now_minute) const;

    std::string serialize() const;
    bool restore(const std::string& data);
    void clear();

    const OrganizerSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    void changed() noexcept { ++snapshot_.sequence; }
    void sort_items();
    void compact_ids();

    OrganizerSnapshot snapshot_{};
    std::uint32_t next_id_{1};
};

// The suggested start of a new event: the next full half hour at least 30
// minutes away. Late in the evening that is early tomorrow.
std::int64_t organizer_default_start(std::int64_t now_minute) noexcept;

bool question_mentions_organizer(const std::string& question);
bool question_mentions_shopping(const std::string& question);

}  // namespace aura

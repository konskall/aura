#pragma once

#include "aura/clock_services.hpp"
#include "aura/device_status.hpp"
#include "aura/fuel_prices.hpp"
#include "aura/notes.hpp"
#include "aura/organizer.hpp"
#include "aura/phone_link.hpp"
#include "aura/provider.hpp"
#include "aura/weather.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

// Short-term conversation memory: RAM only, never persisted or logged, and
// forgotten after five quiet minutes or on "new conversation".
class ConversationMemory {
public:
    static constexpr std::size_t kMaxTurns = 4;
    static constexpr std::size_t kMaxTextBytes = 600;
    static constexpr std::uint64_t kExpiryMs = 5U * 60U * 1000U;

    void record(const std::string& question, const std::string& answer, std::uint64_t now_ms);
    // Turns inside the expiry window (oldest first); expired memory is cleared.
    std::vector<ConversationTurn> recent(std::uint64_t now_ms);
    void clear() noexcept;
    bool active(std::uint64_t now_ms) const noexcept;
    std::size_t size() const noexcept { return turns_.size(); }

private:
    std::vector<ConversationTurn> turns_;
    std::uint64_t last_activity_ms_{0};
};

struct DeviceContextInput {
    const DeviceStatusSnapshot* status{};
    const WeatherSnapshot* weather{};
    const FuelPriceSnapshot* fuel{};
    const ClockServicesModel* clock{};
    const NotesModel* notes{};
    const OrganizerModel* organizer{};
    const PhoneNotificationModel* phone{};
    int unread_messages{0};
    std::string question;
};

// Compact device facts for the assistant (time, battery, weather, timers…).
// Notes and phone notifications are added only when the question asks for them.
std::string build_device_context(const DeviceContextInput& input);

// Narrow Greek/English intent detector. It never performs I/O; it only marks
// a turn so a capable gateway may enable its real web-search tool.
bool question_requests_web_search(std::string_view question);

enum class AssistantActionType {
    None,
    Timer,
    Alarm,
    Reminder,
    Note,
    Task,
    Shopping,
    CalendarEvent,
    Volume,
    Brightness,
    Mute,
    Message,
};

// An action the model proposed. It is untrusted: parsed strictly, bounded,
// and executed only after the owner confirms it on the device.
struct AssistantAction {
    AssistantActionType type{AssistantActionType::None};
    int seconds{0};      // Timer
    int hour{0};         // Alarm, Reminder (at)
    int minute{0};
    bool daily{false};   // Alarm
    int in_minutes{0};   // Reminder (relative)
    bool tomorrow{false};
    std::string label;   // Timer, Alarm
    std::string text;    // Reminder, Note, Message
    std::string room;    // Message
    std::int64_t start_minute{0};  // CalendarEvent
    std::uint32_t duration_minutes{60};
    int value{0};        // Volume/Brightness percentage, Mute: 0 or 1
    bool relative{false};  // Volume: `value` is a step ("louder": +10)
};

struct ParsedAnswer {
    std::string text;
    std::optional<AssistantAction> action;
    bool action_rejected{false};
};

// Splits an answer into display text and an optional final
// `ACTION: {json}` line (see bridge/src/turn-prompt.mjs).
ParsedAnswer parse_assistant_answer(const std::string& raw);

// What the answer screen shows: the model's text; `action_question` when an
// action came without words; and `refused_note` under it when the proposed
// action was refused, so the owner never believes it was done. Never the raw
// ACTION line.
std::string answer_display_text(const ParsedAnswer& parsed, const std::string& action_question,
                                const std::string& refused_note);

// When a reminder "at HH:MM" is due: today if still ahead, else tomorrow
// (or tomorrow when asked). Returns a minute_index().
std::int64_t reminder_due_minute(const AssistantAction& action, std::int64_t now_minute) noexcept;

const char* assistant_action_name(AssistantActionType type) noexcept;

}  // namespace aura

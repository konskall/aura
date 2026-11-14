#include "aura/assistant_actions.hpp"

#include <algorithm>
#include <cmath>

#include "aura/text_util.hpp"

namespace aura {

bool execute_assistant_action(const AssistantAction& action, AssistantActionTargets targets) {
    switch (action.type) {
        case AssistantActionType::Timer:
            return targets.clock.start_timer(static_cast<std::uint32_t>(action.seconds), action.label) ==
                   ClockResult::Ok;
        case AssistantActionType::Alarm:
            return targets.clock.add_alarm(action.hour, action.minute, action.daily, action.label) == ClockResult::Ok;
        case AssistantActionType::Reminder:
            if (!targets.clock.snapshot().clock_valid) return false;
            return targets.clock.add_reminder_at(reminder_due_minute(action, targets.clock.snapshot().now_minute),
                                                 action.text) == ClockResult::Ok;
        case AssistantActionType::Note:
            return targets.notes.add_text(action.text, targets.now_minute) == NoteResult::Ok;
        case AssistantActionType::Task:
            return targets.organizer.add_task(action.text, targets.now_minute) == OrganizerResult::Ok;
        case AssistantActionType::Shopping:
            return targets.organizer.add_shopping(action.text) == OrganizerResult::Ok;
        case AssistantActionType::CalendarEvent:
            targets.organizer.prune_events_before(targets.now_minute);
            return targets.organizer.add_event(action.text, action.start_minute, action.duration_minutes) ==
                   OrganizerResult::Ok;
        case AssistantActionType::Volume: {
            // "Louder" and "quieter" step from the volume heard now; louder
            // also ends a mute, as on a phone.
            const int current = static_cast<int>(std::lround(targets.audio.diagnostics().volume * 100.0F));
            const int target = action.relative ? std::clamp(current + action.value, 0, 100) : action.value;
            if (target < 0 || target > 100) return false;
            targets.sound.volume = static_cast<float>(target) / 100.0F;
            targets.audio.set_volume(targets.sound.volume);
            if (action.relative && action.value > 0 && targets.audio.diagnostics().muted) {
                targets.sound.muted = false;
                targets.audio.set_muted(false);
            }
            return true;
        }
        case AssistantActionType::Brightness: {
            if (action.value < 5 || action.value > 100) return false;
            auto settings = targets.display.snapshot().settings;
            settings.active_brightness_percent = action.value;
            targets.display.set_settings(settings);
            return true;
        }
        case AssistantActionType::Mute:
            if (action.value != 0 && action.value != 1) return false;
            targets.sound.muted = action.value != 0;
            targets.audio.set_muted(targets.sound.muted);
            return true;
        case AssistantActionType::Message:
        case AssistantActionType::None:
            break;
    }
    return false;
}

const AoratonRoom* assistant_message_room(const std::vector<AoratonRoom>& rooms, const std::string& wanted) {
    const auto folded = fold_text(wanted);
    if (!folded.empty()) {
        for (const auto& room : rooms) {
            const auto name = fold_text(room.display_name);
            if (name == folded || name.find(folded) != std::string::npos) return &room;
        }
    }
    return rooms.size() == 1 ? &rooms.front() : nullptr;
}

}  // namespace aura

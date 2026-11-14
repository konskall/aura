#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "aura/aoraton.hpp"
#include "aura/assistant_companion.hpp"
#include "aura/audio_service.hpp"
#include "aura/clock_services.hpp"
#include "aura/display_power.hpp"
#include "aura/notes.hpp"
#include "aura/organizer.hpp"
#include "aura/sounds.hpp"

namespace aura {

// What a confirmed assistant action changes: the same models on the device and
// in the simulator. `sound` is the stored copy of the sound settings, kept in
// step with what the audio service is told.
struct AssistantActionTargets {
    ClockServicesModel& clock;
    NotesModel& notes;
    OrganizerModel& organizer;
    DisplayPowerModel& display;
    IAudioService& audio;
    SoundSettings& sound;
    std::int64_t now_minute;  // local minutes, for notes, tasks and reminders
};

// Carries out an action the owner confirmed; false when it could not be done
// (a full list, a value out of range, no clock for a reminder). A message is
// not handled here: the host routes it to a room, or refuses it.
bool execute_assistant_action(const AssistantAction& action, AssistantActionTargets targets);

// The Aoraton room a message by voice is for: the one the owner named
// (accents and case aside, a part of the name is enough) or, with a single
// room, that one. Never a guess: nullptr otherwise.
const AoratonRoom* assistant_message_room(const std::vector<AoratonRoom>& rooms, const std::string& wanted);

}  // namespace aura

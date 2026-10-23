#pragma once

namespace aura::l10n {

// v13 "Companion" copy: clock, notes, translation, today, phone, display,
// software update and the assistant's action cards. Like Strings, a table of
// `const char*` only (tests/unit/ui_assets_test.cpp checks every entry).
struct CompanionStrings {
    // Apps and shared words
    const char* app_clock;
    const char* app_notes;
    const char* app_translate;
    const char* app_today;
    const char* confirm;
    const char* decline;
    const char* done;
    const char* save;
    const char* delete_item;
    const char* delete_again;
    const char* next;
    const char* cancel;
    const char* today;
    const char* tomorrow;

    // Assistant
    const char* transcribing_title;
    const char* transcribing_detail;
    const char* new_conversation;
    const char* memory_format;  // %u earlier questions
    const char* action_question;
    const char* action_timer_format;  // duration text
    const char* action_alarm_format;  // %02d:%02d
    const char* action_reminder_title;
    const char* action_in_minutes_format;  // %d
    const char* action_at_format;  // day word, %02d:%02d
    const char* action_note_title;
    const char* action_done;
    const char* action_failed;
    const char* voice_error_title;
    const char* no_speech_detail;
    const char* stt_unavailable_detail;
    const char* mic_unavailable_detail;
    const char* tts_unavailable;

    // Clock
    const char* clock_empty;
    const char* clock_empty_hint;
    const char* timer;
    const char* alarm;
    const char* reminder;
    const char* paused;
    const char* every_day;
    const char* once;
    const char* alarm_off;
    const char* enabled;
    const char* pause;
    const char* resume;
    const char* minute_one;
    const char* minutes_format;  // %d
    const char* hour_one;
    const char* hours_format;  // %d
    const char* unit_minute;
    const char* unit_minutes;
    const char* unit_hour;
    const char* new_timer;
    const char* new_alarm;
    const char* new_reminder;
    const char* reminder_placeholder;
    const char* reminder_when;
    const char* in_minutes_format;  // %d
    const char* in_hour;
    const char* in_hours_format;  // %d
    const char* tonight_format;  // %02d:%02d
    const char* tomorrow_format;  // %02d:%02d
    const char* day_time_format;  // day word, %02d:%02d
    const char* clock_needs_time;
    const char* clock_full;
    const char* alert_time_up;
    const char* alert_snooze;
    const char* alert_plus_minute;
    const char* alert_stop;
    const char* timer_left_format;  // duration text

    // Notes
    const char* notes_empty;
    const char* notes_empty_hint;
    const char* note_voice;
    const char* note_typed;
    const char* voice_note_title;
    const char* transcript_pending;
    const char* transcript_failed;
    const char* recording_title;
    const char* recording_detail;
    const char* play;
    const char* note_placeholder;
    const char* notes_full;
    const char* note_saved;
    const char* note_needs_voice;

    // Translation: the prompts are shown in each person's own language.
    const char* translate_title;
    const char* translate_needs_voice;
    const char* translate_failed;
    const char* language_codes[6];   // el en de fr it es
    const char* language_names[6];   // endonyms
    const char* translate_tap[6];
    const char* translate_listening[6];
    const char* translate_working[6];

    // Today
    const char* greeting_morning;
    const char* greeting_afternoon;
    const char* greeting_evening;
    const char* greeting_night;
    const char* briefing_empty;
    const char* briefing_weather_detail_format;  // city, max, min, rain
    const char* briefing_reminders_one;
    const char* briefing_reminders_format;  // %d
    const char* briefing_next_format;  // %02d:%02d, text
    const char* briefing_timers_one;
    const char* briefing_timers_format;  // %d
    const char* briefing_messages_one;
    const char* briefing_messages_format;  // %d
    const char* briefing_phone_one;
    const char* briefing_phone_format;  // %d
    const char* read_aloud;

    // Phone
    const char* phone_title;
    const char* phone_not_paired;
    const char* phone_pairing;
    const char* phone_connected;
    const char* phone_connected_format;  // device name
    const char* phone_disconnected;
    const char* phone_unavailable;
    const char* phone_iphone_help;
    const char* phone_android_help;
    const char* phone_pair;
    const char* phone_forget;
    const char* incoming_call;
    const char* call_on_phone;
    const char* answer_call;
    const char* decline_call;
    const char* dismiss_notification;
    const char* phone_notifications;

    // Display and voice settings
    const char* display_title;
    const char* always_on;
    const char* always_on_detail;
    const char* raise_to_wake;
    const char* raise_detail;
    const char* flip_to_mute;
    const char* flip_detail;
    const char* needs_motion_sensor;
    const char* screen_timeout;
    const char* seconds_format;  // %u
    const char* wake_word;
    const char* wake_word_unavailable;
    const char* muted_face_down;

    // Software update
    const char* update_title;
    const char* update_short;
    const char* update_version_format;  // %s
    const char* update_check;
    const char* update_checking;
    const char* update_up_to_date;
    const char* update_available_format;  // version, whole MB, tenths
    const char* update_install;
    const char* update_install_confirm;
    const char* update_downloading_format;  // %d
    const char* update_verifying;
    const char* update_ready;
    const char* update_restart;
    const char* update_failed;
    const char* update_no_source;
    const char* update_block_offline;
    const char* update_block_battery;
    const char* update_demo_note;
};

const CompanionStrings& companion_el_gr() noexcept;
const CompanionStrings& companion_en_us() noexcept;

}  // namespace aura::l10n

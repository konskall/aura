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
    const char* app_radio;
    const char* app_organizer;
    const char* app_layout_title;
    const char* app_layout_pick;
    const char* app_layout_place;
    const char* app_layout_saved;
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

    // Internet radio
    const char* radio_title;
    const char* radio_search;
    const char* radio_search_placeholder;
    const char* radio_favorites;
    const char* radio_results;
    const char* radio_connecting;
    const char* radio_playing;
    const char* radio_stopped;
    const char* radio_stop;
    const char* radio_add_favorite;
    const char* radio_remove_favorite;
    const char* radio_no_results;
    const char* radio_directory_error;
    const char* radio_stream_error;
    const char* radio_demo_title;
    const char* radio_kbps_format;  // %d
    const char* radio_search_retry;
    const char* radio_aod_format;   // station name
    const char* radio_reorder;
    const char* radio_reorder_done;

    // Assistant
    const char* transcribing_title;
    const char* transcribing_detail;
    const char* new_conversation;
    const char* memory_format;  // %u earlier questions
    const char* conversation_history;
    const char* conversation_empty;
    const char* conversation_ask;
    const char* conversation_clear;
    const char* conversation_you;
    const char* conversation_aura;
    const char* device_ai_services;
    const char* assistant_service;
    const char* recognition_service;
    const char* voice_service;
    const char* service_unavailable;
    const char* web_search_unavailable;
    const char* action_question;
    const char* action_timer_format;  // duration text
    const char* action_alarm_format;  // %02d:%02d
    const char* action_reminder_title;
    const char* action_in_minutes_format;  // %d
    const char* action_at_format;  // day word, %02d:%02d
    const char* action_note_title;
    const char* action_task_title;
    const char* action_shopping_title;
    const char* action_volume_title;
    const char* action_brightness_title;
    const char* action_mute_title;
    const char* action_unmute_title;
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
    const char* unit_minutes_short;  // where a row has little room
    const char* unit_hour_short;
    const char* unit_hours_short;
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

    // Calendar, tasks and shopping
    const char* organizer_events;
    const char* organizer_tasks;
    const char* organizer_shopping;
    const char* organizer_events_empty;
    const char* organizer_tasks_empty;
    const char* organizer_shopping_empty;
    const char* organizer_new_event;
    const char* organizer_new_task;
    const char* organizer_new_shopping;
    const char* organizer_event_placeholder;
    const char* organizer_task_placeholder;
    const char* organizer_shopping_placeholder;
    const char* organizer_choose_time;
    const char* organizer_no_time;
    const char* organizer_full;
    const char* organizer_past_time;
    const char* organizer_saved;
    const char* organizer_tap_complete;

    // Deterministic offline assistant replies
    const char* offline_time_format;       // %02d:%02d
    const char* offline_battery_format;    // %d
    const char* offline_battery_charging;  // appended while charging
    const char* offline_status_unknown;
    const char* offline_volume_format;     // %d
    const char* offline_brightness_format; // %d
    const char* offline_muted;
    const char* offline_unmuted;

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
    const char* briefing_namedays_title;
    const char* briefing_namedays_format;  // names
    const char* briefing_no_namedays;
    const char* briefing_next_holiday_format;
    const char* briefing_holiday_today;  // holiday
    const char* briefing_holiday_date_format;  // holiday, day, month, days until
    const char* calendar_date_unavailable;
    const char* calendar_holiday_today_answer;  // holiday
    const char* calendar_next_holiday_answer;  // holiday, day, month
    const char* calendar_nameday_of_answer;  // name, days ("23/4")
    const char* calendar_and;
    const char* briefing_fuel_title;
    const char* briefing_fuel_prices_format;  // unleaded, diesel, LPG
    const char* briefing_fuel_source;
    const char* briefing_fuel_source_date_format;  // date, optional DEMO badge
    const char* briefing_fuel_stale;
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
    const char* aod_new_one;
    const char* aod_new_format;  // %d
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

    // Weather settings
    const char* weather_settings_title;
    const char* weather_location;
    const char* weather_location_detail;
    const char* weather_refresh_frequency;
    const char* weather_refresh_15;
    const char* weather_refresh_30;
    const char* weather_refresh_60;
    const char* weather_refresh_180;
    const char* weather_refresh_note;
    const char* weather_settings_summary_format;  // city, interval

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

    // aoraton.com account (sign-in through the PC browser, room keys)
    const char* aoraton_sign_in;
    const char* aoraton_sign_in_help;
    const char* aoraton_sign_in_waiting;
    const char* aoraton_account_title;
    const char* aoraton_sign_out;
    const char* aoraton_reauth;
    const char* aoraton_reauth_detail;
    const char* aoraton_timeout_title;
    const char* aoraton_timeout_detail;
    const char* aoraton_sign_in_failed;
    const char* aoraton_sign_in_network;
    const char* aoraton_sign_in_unavailable;
    const char* aoraton_live_polling;
    const char* aoraton_room_locked;
    const char* aoraton_keys;
    const char* aoraton_keys_one;
    const char* aoraton_keys_format;  // %u
    const char* aoraton_keys_none;
    const char* aoraton_keys_unlocking;
    const char* aoraton_unlock_title;
    const char* aoraton_unlock_placeholder;
    const char* aoraton_unlock_hint;
    const char* aoraton_unlock;
    const char* aoraton_paste_invite;
    const char* aoraton_paste_hint;
    const char* aoraton_keys_wrong;
    const char* aoraton_keys_not_configured;
    const char* aoraton_keys_not_member;
    const char* aoraton_keys_invalid_invite;
    const char* aoraton_keys_failed;
    const char* aoraton_keys_unlocked_one;
    const char* aoraton_keys_unlocked_format;  // %u
    const char* aoraton_e2ee;

    // Sound and System settings
    const char* sound_volume;
    const char* sound_mute;
    const char* sound_mute_detail;
    const char* sound_chime;
    const char* sound_chime_detail;
    const char* sound_spoken;
    const char* sound_spoken_detail;
    const char* sound_continuous;
    const char* sound_continuous_detail;
    const char* sound_barge_in;
    const char* sound_barge_in_detail;
    const char* sound_voice;
    const char* sound_preview;
    const char* sound_alarm_note;
    const char* voice_settings_title;
    const char* voice_athina;
    const char* voice_athina_detail;
    const char* voice_nestoras;
    const char* voice_nestoras_detail;
    const char* voice_online_note;
    const char* voice_preview;
    const char* voice_preview_phrase;
    const char* system_title;
    const char* system_software;
    const char* system_software_format;  // %s
    const char* system_update;
    const char* system_diagnostics;
    const char* system_diagnostics_detail;
    const char* system_power;
    const char* system_restart;
    const char* system_power_off;
    const char* system_factory_reset;
    const char* system_factory_reset_detail;
    const char* system_restart_question;
    const char* system_restart_detail;
    const char* system_power_off_question;
    const char* system_power_off_detail;
    const char* system_reset_question;
    const char* system_reset_detail;
    const char* system_reset_confirm;
    const char* system_starting;
    const char* system_restarting;
    const char* system_restarting_detail;
    const char* system_resetting;
    const char* system_resetting_detail;
    const char* system_powering_on;
    const char* system_powering_on_detail;
    const char* system_power_off_hint;

    // Interface language. Language names are endonyms; all explanatory copy
    // follows the currently selected interface language.
    const char* language_settings_title;
    const char* language_greek;
    const char* language_english;
    const char* language_greek_detail;
    const char* language_english_detail;
    const char* language_change_note;
};

const CompanionStrings& companion_el_gr() noexcept;
const CompanionStrings& companion_en_us() noexcept;

}  // namespace aura::l10n

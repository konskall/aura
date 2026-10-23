#include "aura/companion_strings.hpp"

#include <array>
#include <cstddef>

namespace aura::l10n {
namespace {

void set(const char* (&target)[6], const std::array<const char*, 6>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) target[index] = values[index];
}

// Endonyms and per-language prompts: the other person reads their own language.
void set_languages(CompanionStrings& s) {
    set(s.language_names, {"Ελληνικά", "English", "Deutsch", "Français", "Italiano", "Español"});
    set(s.translate_tap, {"Πάτα και μίλα", "Tap and speak", "Tippen und sprechen", "Touchez et parlez",
                          "Tocca e parla", "Toca y habla"});
    set(s.translate_listening, {"Ακούω…", "Listening…", "Ich höre zu…", "J’écoute…", "Ascolto…", "Escuchando…"});
    set(s.translate_working, {"Μεταφράζω…", "Translating…", "Übersetze…", "Traduction…", "Traduco…",
                              "Traduciendo…"});
}

}  // namespace

const CompanionStrings& companion_el_gr() noexcept {
    static const CompanionStrings strings = [] {
        CompanionStrings s{};
        s.app_clock = "Ρολόι";
        s.app_notes = "Σημειώσεις";
        s.app_translate = "Μετάφραση";
        s.app_today = "Σήμερα";
        s.confirm = "Επιβεβαίωση";
        s.decline = "Όχι";
        s.done = "Τέλος";
        s.save = "Αποθήκευση";
        s.delete_item = "Διαγραφή";
        s.delete_again = "Πάτα ξανά για διαγραφή";
        s.next = "Επόμενο";
        s.cancel = "Ακύρωση";
        s.today = "Σήμερα";
        s.tomorrow = "Αύριο";

        s.transcribing_title = "Σε άκουσα";
        s.transcribing_detail = "Αναγνώριση ομιλίας…";
        s.new_conversation = "Νέα συζήτηση";
        s.memory_format = "Συνέχεια συζήτησης · %u";
        s.action_question = "Να το κάνω;";
        s.action_timer_format = "Χρονόμετρο %s";
        s.action_alarm_format = "Ξυπνητήρι %02d:%02d";
        s.action_reminder_title = "Υπενθύμιση";
        s.action_in_minutes_format = "Σε %d λεπτά";
        s.action_at_format = "%s %02d:%02d";
        s.action_note_title = "Νέα σημείωση";
        s.action_done = "Έγινε";
        s.action_failed = "Δεν έγινε";
        s.voice_error_title = "Δεν σε άκουσα";
        s.no_speech_detail = "Δεν ακούστηκε ομιλία. Μίλα λίγο πιο κοντά και ξαναδοκίμασε.";
        s.stt_unavailable_detail = "Η αναγνώριση ομιλίας δεν είναι εγκατεστημένη σε αυτή τη συσκευή.";
        s.mic_unavailable_detail = "Το μικρόφωνο δεν είναι διαθέσιμο ή δεν επιτρέπεται η χρήση του.";
        s.tts_unavailable = "Η φωνή δεν είναι διαθέσιμη";

        s.clock_empty = "Δεν υπάρχουν χρονόμετρα ή ξυπνητήρια";
        s.clock_empty_hint = "Πες «βάλε χρονόμετρο 10 λεπτά» ή πάτα +";
        s.timer = "Χρονόμετρο";
        s.alarm = "Ξυπνητήρι";
        s.reminder = "Υπενθύμιση";
        s.paused = "Σε παύση";
        s.every_day = "Κάθε μέρα";
        s.once = "Μία φορά";
        s.alarm_off = "Ανενεργό";
        s.enabled = "Ενεργό";
        s.pause = "Παύση";
        s.resume = "Συνέχεια";
        s.minute_one = "1 λεπτό";
        s.minutes_format = "%d λεπτά";
        s.hour_one = "1 ώρα";
        s.hours_format = "%d ώρες";
        s.unit_minute = "λεπτό";
        s.unit_minutes = "λεπτά";
        s.unit_hour = "ώρα";
        s.new_timer = "Νέο χρονόμετρο";
        s.new_alarm = "Νέο ξυπνητήρι";
        s.new_reminder = "Νέα υπενθύμιση";
        s.reminder_placeholder = "Τι να σου θυμίσω;";
        s.reminder_when = "Πότε;";
        s.in_minutes_format = "Σε %d λεπτά";
        s.in_hour = "Σε 1 ώρα";
        s.in_hours_format = "Σε %d ώρες";
        s.tonight_format = "Απόψε %02d:%02d";
        s.tomorrow_format = "Αύριο %02d:%02d";
        s.day_time_format = "%s %02d:%02d";
        s.clock_needs_time = "Χρειάζεται σωστή ώρα";
        s.clock_full = "Δεν χωράει άλλο";
        s.alert_time_up = "Τέλος χρόνου";
        s.alert_snooze = "Αναβολή 5 λεπτά";
        s.alert_plus_minute = "+1 λεπτό";
        s.alert_stop = "Τέλος";
        s.timer_left_format = "Απομένουν %s";

        s.notes_empty = "Δεν υπάρχουν σημειώσεις";
        s.notes_empty_hint = "Πες «σημείωσε ότι…» ή πάτα Φωνητική";
        s.note_voice = "Φωνητική";
        s.note_typed = "Γραπτή";
        s.voice_note_title = "Φωνητική σημείωση";
        s.transcript_pending = "Μεταγραφή…";
        s.transcript_failed = "Χωρίς κείμενο";
        s.recording_title = "Ηχογράφηση";
        s.recording_detail = "Μίλα · σταματά μόνο του στη σιωπή";
        s.play = "Αναπαραγωγή";
        s.note_placeholder = "Γράψε τη σημείωση";
        s.notes_full = "Γέμισαν οι σημειώσεις";
        s.note_saved = "Η σημείωση αποθηκεύτηκε";
        s.note_needs_voice = "Χρειάζεται μικρόφωνο";

        s.translate_title = "Μετάφραση";
        s.translate_needs_voice = "Η μετάφραση χρειάζεται μικρόφωνο και αναγνώριση ομιλίας";
        s.translate_failed = "Δεν μεταφράστηκε";
        set(s.language_codes, {"ΕΛ", "EN", "DE", "FR", "IT", "ES"});
        set_languages(s);

        s.greeting_morning = "Καλημέρα";
        s.greeting_afternoon = "Καλό απόγευμα";
        s.greeting_evening = "Καλησπέρα";
        s.greeting_night = "Καληνύχτα";
        s.briefing_empty = "Τίποτα προγραμματισμένο για σήμερα";
        s.briefing_weather_detail_format = "%s · %d°/%d° · βροχή %d%%";
        s.briefing_reminders_one = "1 υπενθύμιση σήμερα";
        s.briefing_reminders_format = "%d υπενθυμίσεις σήμερα";
        s.briefing_next_format = "Επόμενη %02d:%02d · %s";
        s.briefing_timers_one = "1 χρονόμετρο σε εξέλιξη";
        s.briefing_timers_format = "%d χρονόμετρα σε εξέλιξη";
        s.briefing_messages_one = "1 αδιάβαστο μήνυμα";
        s.briefing_messages_format = "%d αδιάβαστα μηνύματα";
        s.briefing_phone_one = "1 ειδοποίηση από το κινητό";
        s.briefing_phone_format = "%d ειδοποιήσεις από το κινητό";
        s.read_aloud = "Ανάγνωση";

        s.phone_title = "Κινητό";
        s.phone_not_paired = "Δεν έχει συνδεθεί κινητό";
        s.phone_pairing = "Σύνδεση…";
        s.phone_connected = "Συνδεδεμένο";
        s.phone_connected_format = "Συνδεδεμένο · %s";
        s.phone_disconnected = "Αποσυνδεδεμένο";
        s.phone_unavailable = "Δεν υποστηρίζεται ακόμα σε αυτή τη συσκευή";
        s.phone_iphone_help = "iPhone: Ρυθμίσεις → Bluetooth → AURA";
        s.phone_android_help = "Android: με την εφαρμογή AURA Link (σε σχεδίαση)";
        s.phone_pair = "Σύνδεση κινητού";
        s.phone_forget = "Αποσύνδεση";
        s.incoming_call = "Εισερχόμενη κλήση";
        s.call_on_phone = "Η κλήση απαντιέται στο κινητό";
        s.answer_call = "Απάντηση";
        s.decline_call = "Απόρριψη";
        s.dismiss_notification = "Απόρριψη";
        s.phone_notifications = "Ειδοποιήσεις κινητού";

        s.display_title = "Οθόνη";
        s.always_on = "Πάντα αναμμένη";
        s.always_on_detail = "Αχνό ρολόι όταν δεν τη χρησιμοποιείς";
        s.raise_to_wake = "Ανάβει με σήκωμα";
        s.raise_detail = "Η οθόνη ανάβει όταν τη σηκώνεις";
        s.flip_to_mute = "Σίγαση ανάποδα";
        s.flip_detail = "Γύρισέ τη ανάποδα για ησυχία";
        s.needs_motion_sensor = "Χρειάζεται τον αισθητήρα κίνησης";
        s.screen_timeout = "Σβήσιμο οθόνης";
        s.seconds_format = "Μετά από %u δευτ.";
        s.wake_word = "Λέξη αφύπνισης «Αύρα»";
        s.wake_word_unavailable = "Χρειάζεται εκπαιδευμένο μοντέλο";
        s.muted_face_down = "Σίγαση · η συσκευή είναι ανάποδα";

        s.update_title = "Ενημέρωση λογισμικού";
        s.update_short = "Ενημέρωση";
        s.update_version_format = "AURA %s";
        s.update_check = "Έλεγχος για ενημέρωση";
        s.update_checking = "Έλεγχος…";
        s.update_up_to_date = "Έχεις την τελευταία έκδοση";
        s.update_available_format = "Νέα έκδοση %s · %d,%d MB";
        s.update_install = "Εγκατάσταση";
        s.update_install_confirm = "Η AURA θα κάνει επανεκκίνηση μετά την εγκατάσταση";
        s.update_downloading_format = "Λήψη %d%%";
        s.update_verifying = "Έλεγχος ακεραιότητας…";
        s.update_ready = "Έτοιμη για επανεκκίνηση";
        s.update_restart = "Επανεκκίνηση";
        s.update_failed = "Η ενημέρωση απέτυχε";
        s.update_no_source = "Δεν έχει ρυθμιστεί πηγή ενημερώσεων";
        s.update_block_offline = "Χρειάζεται Wi‑Fi";
        s.update_block_battery = "Χρειάζεται μπαταρία 30 τοις εκατό ή φόρτιση";
        s.update_demo_note = "DEMO · δεν εγκαθίσταται τίποτα";
        return s;
    }();
    return strings;
}

const CompanionStrings& companion_en_us() noexcept {
    static const CompanionStrings strings = [] {
        CompanionStrings s{};
        s.app_clock = "Clock";
        s.app_notes = "Notes";
        s.app_translate = "Translate";
        s.app_today = "Today";
        s.confirm = "Confirm";
        s.decline = "No";
        s.done = "Done";
        s.save = "Save";
        s.delete_item = "Delete";
        s.delete_again = "Tap again to delete";
        s.next = "Next";
        s.cancel = "Cancel";
        s.today = "Today";
        s.tomorrow = "Tomorrow";

        s.transcribing_title = "Got it";
        s.transcribing_detail = "Recognising speech…";
        s.new_conversation = "New chat";
        s.memory_format = "Continuing · %u";
        s.action_question = "Shall I?";
        s.action_timer_format = "Timer %s";
        s.action_alarm_format = "Alarm %02d:%02d";
        s.action_reminder_title = "Reminder";
        s.action_in_minutes_format = "In %d minutes";
        s.action_at_format = "%s %02d:%02d";
        s.action_note_title = "New note";
        s.action_done = "Done";
        s.action_failed = "Couldn't do that";
        s.voice_error_title = "I couldn't hear you";
        s.no_speech_detail = "No speech was heard. Speak a little closer and try again.";
        s.stt_unavailable_detail = "Speech recognition is not installed on this device.";
        s.mic_unavailable_detail = "The microphone is unavailable or not allowed.";
        s.tts_unavailable = "Voice is unavailable";

        s.clock_empty = "No timers or alarms";
        s.clock_empty_hint = "Say “set a timer for 10 minutes” or tap +";
        s.timer = "Timer";
        s.alarm = "Alarm";
        s.reminder = "Reminder";
        s.paused = "Paused";
        s.every_day = "Every day";
        s.once = "Once";
        s.alarm_off = "Off";
        s.enabled = "On";
        s.pause = "Pause";
        s.resume = "Resume";
        s.minute_one = "1 minute";
        s.minutes_format = "%d minutes";
        s.hour_one = "1 hour";
        s.hours_format = "%d hours";
        s.unit_minute = "minute";
        s.unit_minutes = "minutes";
        s.unit_hour = "hour";
        s.new_timer = "New timer";
        s.new_alarm = "New alarm";
        s.new_reminder = "New reminder";
        s.reminder_placeholder = "What should I remind you?";
        s.reminder_when = "When?";
        s.in_minutes_format = "In %d minutes";
        s.in_hour = "In 1 hour";
        s.in_hours_format = "In %d hours";
        s.tonight_format = "Tonight %02d:%02d";
        s.tomorrow_format = "Tomorrow %02d:%02d";
        s.day_time_format = "%s %02d:%02d";
        s.clock_needs_time = "Needs the correct time";
        s.clock_full = "No room for more";
        s.alert_time_up = "Time's up";
        s.alert_snooze = "Snooze 5 minutes";
        s.alert_plus_minute = "+1 minute";
        s.alert_stop = "Stop";
        s.timer_left_format = "%s left";

        s.notes_empty = "No notes yet";
        s.notes_empty_hint = "Say “note that…” or tap Voice";
        s.note_voice = "Voice";
        s.note_typed = "Typed";
        s.voice_note_title = "Voice note";
        s.transcript_pending = "Transcribing…";
        s.transcript_failed = "No text";
        s.recording_title = "Recording";
        s.recording_detail = "Speak · stops after a pause";
        s.play = "Play";
        s.note_placeholder = "Write your note";
        s.notes_full = "Notes are full";
        s.note_saved = "Note saved";
        s.note_needs_voice = "Needs a microphone";

        s.translate_title = "Translate";
        s.translate_needs_voice = "Translation needs a microphone and speech recognition";
        s.translate_failed = "Not translated";
        set(s.language_codes, {"EL", "EN", "DE", "FR", "IT", "ES"});
        set_languages(s);

        s.greeting_morning = "Good morning";
        s.greeting_afternoon = "Good afternoon";
        s.greeting_evening = "Good evening";
        s.greeting_night = "Good night";
        s.briefing_empty = "Nothing planned for today";
        s.briefing_weather_detail_format = "%s · %d°/%d° · rain %d%%";
        s.briefing_reminders_one = "1 reminder today";
        s.briefing_reminders_format = "%d reminders today";
        s.briefing_next_format = "Next %02d:%02d · %s";
        s.briefing_timers_one = "1 timer running";
        s.briefing_timers_format = "%d timers running";
        s.briefing_messages_one = "1 unread message";
        s.briefing_messages_format = "%d unread messages";
        s.briefing_phone_one = "1 phone notification";
        s.briefing_phone_format = "%d phone notifications";
        s.read_aloud = "Read aloud";

        s.phone_title = "Phone";
        s.phone_not_paired = "No phone connected";
        s.phone_pairing = "Pairing…";
        s.phone_connected = "Connected";
        s.phone_connected_format = "Connected · %s";
        s.phone_disconnected = "Disconnected";
        s.phone_unavailable = "Not supported on this device yet";
        s.phone_iphone_help = "iPhone: Settings → Bluetooth → AURA";
        s.phone_android_help = "Android: with the AURA Link app (in design)";
        s.phone_pair = "Pair phone";
        s.phone_forget = "Unpair";
        s.incoming_call = "Incoming call";
        s.call_on_phone = "The call is answered on the phone";
        s.answer_call = "Answer";
        s.decline_call = "Decline";
        s.dismiss_notification = "Dismiss";
        s.phone_notifications = "Phone notifications";

        s.display_title = "Display";
        s.always_on = "Always on";
        s.always_on_detail = "A dim clock when idle";
        s.raise_to_wake = "Raise to wake";
        s.raise_detail = "Lights up when you lift it";
        s.flip_to_mute = "Turn over to mute";
        s.flip_detail = "Turn it face down for quiet";
        s.needs_motion_sensor = "Needs the motion sensor";
        s.screen_timeout = "Screen timeout";
        s.seconds_format = "After %u s";
        s.wake_word = "Wake word “Aura”";
        s.wake_word_unavailable = "Needs a trained model";
        s.muted_face_down = "Muted · the device is face down";

        s.update_title = "Software update";
        s.update_short = "Update";
        s.update_version_format = "AURA %s";
        s.update_check = "Check for updates";
        s.update_checking = "Checking…";
        s.update_up_to_date = "You're up to date";
        s.update_available_format = "Version %s · %d.%d MB";
        s.update_install = "Install";
        s.update_install_confirm = "AURA restarts after installing";
        s.update_downloading_format = "Downloading %d%%";
        s.update_verifying = "Verifying…";
        s.update_ready = "Ready to restart";
        s.update_restart = "Restart";
        s.update_failed = "Update failed";
        s.update_no_source = "No update source configured";
        s.update_block_offline = "Needs Wi‑Fi";
        s.update_block_battery = "Needs 30 percent battery or charging";
        s.update_demo_note = "DEMO · nothing is installed";
        return s;
    }();
    return strings;
}

}  // namespace aura::l10n

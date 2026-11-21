#include "aura/assistant_actions.hpp"
#include "aura/alarm_station.hpp"
#include "aura/assistant_companion.hpp"
#include "aura/battery_history.hpp"
#include "aura/briefing.hpp"
#include "aura/clock_services.hpp"
#include "aura/display_power.hpp"
#include "aura/mock_provider.hpp"
#include "aura/notes.hpp"
#include "aura/ota_update.hpp"
#include "aura/phone_link.hpp"
#include "aura/runtime_metrics.hpp"
#include "aura/sounds.hpp"
#include "aura/text_util.hpp"
#include "aura/voice.hpp"
#include "aura/wake_word.hpp"
#include "aura/xweather.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

aura::LocalDateTime at(int year, int month, int day, int hour, int minute, int weekday = 4) {
    aura::LocalDateTime value;
    value.year = year;
    value.month = month;
    value.day = day;
    value.hour = hour;
    value.minute = minute;
    value.weekday = weekday;
    return value;
}

void text_tests() {
    using namespace aura;
    expect(utf8_truncate("αβγ", 3) == "α", "truncation never splits a Greek letter");
    expect(sanitize_text("  a\x01" "b\tc\n d\xE2\x80\xAE ", 64) == "ab c  d", "control, bidi and spacing are cleaned");
    expect(sanitize_text("x\ny", 64, true) == "x\ny", "newlines survive when asked");
    expect(sanitize_text("\xC0\xAF" "ok", 64) == "ok", "overlong UTF-8 is dropped");
    expect(fold_text("ΣΗΜΕΊΩΣΗ Άλφα") == "σημειωση αλφα", "Greek folding drops case and tonos");
    expect(fold_text("Σημειώσεις") == "σημειωσεισ", "final sigma folds");
    const auto words = folded_words(fold_text("Βάλε «ΣΚΑΪ» 100.3, παρακαλώ; display-mode"));
    expect(words == std::vector<std::string>{"βαλε", "σκαι", "100.3", "παρακαλω", "display", "mode"},
           "words split on punctuation and keep decimal numbers whole");
    expect(!folded_has_word(words, {"play"}) && folded_has_word(words, {"display"}), "a word matches whole");
    expect(folded_has_word_prefix(words, {"παρακαλ"}) && !folded_has_word_prefix(words, {"λαβ", "ισπλ"}),
           "a stem matches only the start of a word");
    expect(folded_contains_phrase(fold_text("Τι ώρα είναι;"), "τι ωρα ειναι") &&
               !folded_contains_phrase(fold_text("κάτι ωραίο"), "τι ωρα"),
           "a phrase matches whole words");
    expect(json_escape("a\"b\n") == "a\\\"b\\n", "JSON escaping");

    const auto object = parse_flat_json(R"({"type":"timer","seconds":600,"ok":true,"label":"Μακ","x":null})");
    expect(object && object->string("type") == std::string("timer") && object->integer("seconds") == 600 &&
           object->boolean("ok") == true && object->string("label") == std::string("Μακ") && object->size() == 5,
           "flat JSON parses strings, integers, booleans and null");
    expect(!parse_flat_json(R"({"a":{"b":1}})"), "nested objects are rejected");
    expect(!parse_flat_json(R"({"a":1,"a":2})"), "duplicate keys are rejected");
    expect(!parse_flat_json(R"({"a":1.5})"), "fractions are rejected");
    expect(!parse_flat_json(R"({"a":1} trailing)"), "trailing garbage is rejected");
    expect(!parse_flat_json(R"({"a":"\ud800"})"), "unpaired surrogates are rejected");

    const auto thursday = at(2026, 10, 22, 7, 30);
    expect(local_date_time_valid(thursday) && !local_date_time_valid(at(2026, 2, 29, 1, 0)), "calendar validity");
    const auto index = minute_index(thursday);
    const auto back = from_minute_index(index);
    expect(back.year == 2026 && back.month == 10 && back.day == 22 && back.hour == 7 && back.minute == 30 &&
           back.weekday == 4, "minute index round-trips with the weekday");
    expect(from_minute_index(minute_index(at(2028, 2, 29, 23, 59))).day == 29, "leap day round-trips");
}

void clock_tests() {
    using namespace aura;
    // A time as the owner chose to read it (2026-11-11).
    expect(clock_text(14, 5, true, InterfaceLanguage::Greek) == "14:05" &&
               clock_text(7, 30, true, InterfaceLanguage::English) == "07:30",
           "24-hour times, as before");
    expect(clock_text(14, 5, false, InterfaceLanguage::Greek) == "2:05 μ.μ." &&
               clock_text(9, 0, false, InterfaceLanguage::Greek) == "9:00 π.μ." &&
               clock_text(14, 5, false, InterfaceLanguage::English) == "2:05 PM",
           "12-hour times say morning or afternoon");
    expect(clock_text(0, 15, false, InterfaceLanguage::English) == "12:15 AM" &&
               clock_text(12, 0, false, InterfaceLanguage::Greek) == "12:00 μ.μ.",
           "midnight and noon are 12");
    // The large clocks' fonts have digits only: the half of the day goes beside them.
    expect(clock_digits(14, 5, true) == "14:05" && clock_digits(14, 5, false) == "2:05" &&
               clock_digits(0, 15, false) == "12:15",
           "a large clock's digits");
    expect(std::string(clock_period(9, InterfaceLanguage::Greek)) == "π.μ." &&
               std::string(clock_period(21, InterfaceLanguage::English)) == "PM",
           "the half of the day");
    ClockServicesModel clock;
    std::uint32_t id = 0;
    expect(clock.start_timer(0, "x") == ClockResult::Invalid, "empty timer is rejected");
    expect(clock.start_timer(90, "Μακαρόνια", &id) == ClockResult::Ok && clock.snapshot().timers.size() == 1,
           "a timer starts");
    const auto sequence = clock.snapshot().sequence;
    clock.tick(400);
    expect(clock.snapshot().sequence == sequence, "sub-second ticks do not redraw");
    clock.tick(700);
    expect(clock.snapshot().sequence > sequence, "a visible second change bumps the sequence");
    clock.pause_timer(id, true);
    clock.tick(200000);
    expect(clock.snapshot().timers.size() == 1 && !clock.snapshot().alert, "a paused timer does not run");
    clock.pause_timer(id, false);
    clock.tick(200000);
    expect(clock.snapshot().timers.empty() && clock.snapshot().alert &&
           clock.snapshot().alert->kind == ClockAlertKind::Timer && clock.snapshot().alert->label == "Μακαρόνια",
           "a finished timer rings");
    expect(clock.snooze_alert(1) == ClockResult::Ok && clock.snapshot().timers.size() == 1 &&
           !clock.snapshot().alert, "snoozing a timer restarts it for a minute");
    clock.cancel_timer(clock.snapshot().timers.front().id);
    for (int index = 0; index < 4; ++index) clock.start_timer(60, "");
    expect(clock.start_timer(60, "") == ClockResult::Full, "at most four timers");

    ClockServicesModel alarms;
    expect(alarms.add_reminder_in(5, "x") == ClockResult::ClockUnavailable, "reminders need a clock");
    expect(alarms.add_alarm(24, 0, false, "") == ClockResult::Invalid, "invalid alarm time");
    alarms.add_alarm(7, 30, false, "Γυμναστήριο");
    alarms.add_alarm(6, 45, true, "");
    expect(alarms.snapshot().alarms.front().hour == 6, "alarms are sorted by time");
    alarms.set_clock(at(2026, 10, 22, 7, 28));
    expect(!alarms.snapshot().alert, "nothing rings early");
    expect(alarms.next_alarm() && alarms.next_alarm()->hour == 7 && alarms.minutes_until(*alarms.next_alarm()) == 2,
           "next alarm is the 07:30 one");
    alarms.set_clock(at(2026, 10, 22, 7, 30));
    expect(alarms.snapshot().alert && alarms.snapshot().alert->kind == ClockAlertKind::Alarm &&
           alarms.snapshot().alert->label == "Γυμναστήριο", "the alarm rings at its minute");
    const auto once = std::find_if(alarms.snapshot().alarms.begin(), alarms.snapshot().alarms.end(),
                                   [](const AlarmItem& item) { return item.hour == 7; });
    expect(once != alarms.snapshot().alarms.end() && !once->enabled, "a one-off alarm disables itself");
    expect(alarms.snooze_alert(5) == ClockResult::Ok && !alarms.snapshot().alert, "snooze silences");
    alarms.set_clock(at(2026, 10, 22, 7, 34));
    expect(!alarms.snapshot().alert, "snooze waits");
    alarms.set_clock(at(2026, 10, 22, 7, 35));
    expect(alarms.snapshot().alert && alarms.snapshot().alert->kind == ClockAlertKind::Alarm, "snoozed alarm rings again");
    alarms.dismiss_alert();
    expect(!alarms.snapshot().alert, "dismiss clears the alert");

    std::uint32_t reminder_id = 0;
    expect(alarms.add_reminder_in(10, "Φάρμακα", &reminder_id) == ClockResult::Ok, "relative reminder");
    expect(alarms.add_reminder_at(minute_index(at(2026, 10, 22, 7, 0)), "past") == ClockResult::Invalid,
           "a reminder in the past is rejected");
    const auto stored = alarms.serialize();
    alarms.set_clock(at(2026, 10, 22, 7, 45));
    expect(alarms.snapshot().alert && alarms.snapshot().alert->kind == ClockAlertKind::Reminder &&
           alarms.snapshot().alert->label == "Φάρμακα" && alarms.snapshot().reminders.empty(), "a reminder fires once");

    ClockServicesModel restored;
    expect(restored.restore(stored) && restored.snapshot().alarms.size() == 2 &&
           restored.snapshot().reminders.size() == 1 && restored.snapshot().reminders.front().text == "Φάρμακα",
           "alarms and reminders survive a restart");
    expect(!restored.restore("garbage"), "an unknown store is refused");
    restored.set_clock(at(2026, 10, 23, 6, 50, 5));
    expect(restored.snapshot().alert && restored.snapshot().alert->kind == ClockAlertKind::Reminder,
           "an overdue reminder still fires after a restart");

    // A snoozed reminder and an alert still waiting for the owner survive a restart.
    ClockServicesModel snoozing;
    snoozing.set_clock(at(2026, 10, 22, 9, 0));
    snoozing.add_reminder_in(1, "Ραντεβού");
    snoozing.set_clock(at(2026, 10, 22, 9, 1));
    expect(snoozing.snooze_alert(5) == ClockResult::Ok && !snoozing.snapshot().alert, "a reminder is snoozed");
    ClockServicesModel after_snooze;
    expect(after_snooze.restore(snoozing.serialize()) && !after_snooze.snapshot().alert, "the snooze is restored");
    after_snooze.set_clock(at(2026, 10, 22, 9, 6));
    expect(after_snooze.snapshot().alert && after_snooze.snapshot().alert->kind == ClockAlertKind::Reminder &&
           after_snooze.snapshot().alert->label == "Ραντεβού", "a snoozed reminder rings after a restart");
    ClockServicesModel unanswered;
    unanswered.set_clock(at(2026, 10, 22, 9, 0));
    unanswered.start_timer(60, "Αυγά");
    unanswered.tick(60000);
    expect(unanswered.snapshot().alert.has_value(), "a timer rings");
    ClockServicesModel after_alert;
    expect(after_alert.restore(unanswered.serialize()) && after_alert.snapshot().alert &&
           after_alert.snapshot().alert->kind == ClockAlertKind::Timer &&
           after_alert.snapshot().alert->label == "Αυγά" && after_alert.snapshot().alert->duration_s == 60,
           "an alert nobody answered rings again after a restart");
    after_alert.dismiss_alert();
    ClockServicesModel after_dismiss;
    expect(after_dismiss.restore(after_alert.serialize()) && !after_dismiss.snapshot().alert,
           "a dismissed alert stays dismissed");

    ClockServicesModel backwards;
    backwards.add_alarm(8, 0, true, "");
    backwards.set_clock(at(2026, 10, 22, 8, 5));
    backwards.set_clock(at(2026, 10, 22, 7, 59));
    backwards.set_clock(at(2026, 10, 22, 8, 0));
    expect(backwards.snapshot().alert.has_value(), "after a clock correction the alarm still rings at its time");
    ClockServicesModel sleepy;
    sleepy.add_alarm(3, 0, true, "");
    sleepy.set_clock(at(2026, 10, 22, 1, 0));
    sleepy.set_clock(at(2026, 10, 22, 9, 0));
    expect(!sleepy.snapshot().alert, "an alarm missed hours ago does not ring late");

    // Daylight saving time in Athens. With the real (UTC) minute beside the wall
    // clock, the model tells a time change from real time passing.
    // 2027-03-28: 03:00 EET becomes 04:00 EEST, so 03:00-03:59 never happen; an
    // alarm in that hour rings as the clock jumps.
    ClockServicesModel spring;
    spring.add_alarm(3, 0, false, "");
    std::int64_t utc = minute_index(at(2027, 3, 28, 0, 59));  // 02:59 EET
    spring.set_clock(at(2027, 3, 28, 2, 59), utc);
    expect(!spring.snapshot().alert, "nothing rings before the clocks go forward");
    spring.set_clock(at(2027, 3, 28, 4, 0), ++utc);
    expect(spring.snapshot().alert && spring.snapshot().alert->hour == 3 && spring.snapshot().alert->minute == 0,
           "an alarm in the hour the clocks skip rings when they go forward");
    // 2026-10-25: 04:00 EEST becomes 03:00 EET, so 03:00-03:59 happen twice; a
    // daily alarm in that hour rings once.
    ClockServicesModel autumn;
    autumn.add_alarm(3, 30, true, "");
    utc = minute_index(at(2026, 10, 25, 0, 29));  // 03:29 EEST
    autumn.set_clock(at(2026, 10, 25, 3, 29), utc);
    autumn.set_clock(at(2026, 10, 25, 3, 30), ++utc);
    expect(autumn.snapshot().alert.has_value(), "the alarm rings the first time 03:30 comes");
    autumn.dismiss_alert();
    for (int minute = 31; minute <= 59; ++minute) autumn.set_clock(at(2026, 10, 25, 3, minute), ++utc);
    for (int minute = 0; minute <= 59; ++minute) autumn.set_clock(at(2026, 10, 25, 3, minute), ++utc);
    expect(!autumn.snapshot().alert, "the hour the clocks repeat does not ring the alarm again");
    for (int minute = 0; minute <= 59; ++minute) autumn.set_clock(at(2026, 10, 25, 4, minute), ++utc);
    expect(!autumn.snapshot().alert, "after the repeated hour the day goes on as usual");
    utc += 22 * 60 + 30;  // 03:29 the next day
    autumn.set_clock(at(2026, 10, 26, 3, 29), utc);
    autumn.set_clock(at(2026, 10, 26, 3, 30), ++utc);
    expect(autumn.snapshot().alert.has_value(), "the daily alarm rings the next day");
    // A time zone three hours to the west repeats three hours of the wall clock:
    // a daily alarm rings at the new 07:00 as well, but not at the change itself.
    ClockServicesModel west;
    west.add_alarm(7, 0, true, "");
    utc = minute_index(at(2026, 11, 2, 4, 59));
    west.set_clock(at(2026, 11, 2, 6, 59), utc);
    west.set_clock(at(2026, 11, 2, 7, 0), ++utc);
    expect(west.snapshot().alert.has_value(), "the alarm rings before the journey");
    west.dismiss_alert();
    west.set_clock(at(2026, 11, 2, 4, 1), ++utc);
    expect(!west.snapshot().alert, "changing the time zone rings nothing at once");
    for (int minute = 2; minute <= 180; ++minute) {  // 04:02 to 07:00
        west.set_clock(at(2026, 11, 2, 4 + minute / 60, minute % 60), ++utc);
    }
    expect(west.snapshot().alert.has_value(), "the daily alarm rings at 07:00 in the new time zone");
    // A real clock correction backwards (the real minute goes back too) still
    // rings the alarm when its time comes again.
    ClockServicesModel corrected;
    corrected.add_alarm(8, 0, true, "");
    utc = minute_index(at(2026, 10, 22, 6, 5));
    corrected.set_clock(at(2026, 10, 22, 8, 5), utc);
    corrected.set_clock(at(2026, 10, 22, 7, 59), utc - 6);
    corrected.set_clock(at(2026, 10, 22, 8, 0), utc - 5);
    expect(corrected.snapshot().alert.has_value(), "after a correction of the real clock the alarm rings at its time");
}

void notes_tests() {
    using namespace aura;
    NotesModel notes;
    std::uint32_t id = 0;
    expect(notes.add_text("   ", 0) == NoteResult::Empty, "an empty note is refused");
    expect(notes.add_text("Αγόρασε γάλα\nκαι ψωμί", minute_index(at(2026, 10, 22, 9, 0)), &id) == NoteResult::Ok,
           "a typed note is saved");
    std::uint32_t voice = 0;
    expect(notes.add_voice("note-2.wav", 4200, 0, &voice) == NoteResult::Ok &&
           notes.snapshot().notes.front().transcript == NoteTranscript::Pending, "a voice note waits for its text");
    expect(notes.add_voice("../etc/passwd", 10, 0) == NoteResult::Empty, "an unsafe audio name is refused");
    notes.set_transcript(voice, "Ραντεβού με τον γιατρό");
    expect(notes.find(voice)->transcript == NoteTranscript::Ready, "the transcript arrives");
    expect(notes.context_for("Τι καιρό κάνει;").empty(), "notes stay private for other questions");
    const auto context = notes.context_for("Τι είχα σημειώσει;");
    expect(context.find("γάλα") != std::string::npos && context.find("γιατρό") != std::string::npos,
           "a notes question sees the notes");
    NotesModel restored;
    expect(restored.restore(notes.serialize()) && restored.snapshot().notes.size() == 2 &&
           restored.snapshot().notes.front().audio_file == "note-2.wav" &&
           restored.snapshot().notes.back().text == "Αγόρασε γάλα\nκαι ψωμί", "notes survive a restart in order");
    std::string removed_file;
    expect(restored.remove(voice, &removed_file) == NoteResult::Ok && removed_file == "note-2.wav",
           "deleting a voice note reports its file");
    expect(question_mentions_notes("ΣΗΜΕΙΏΣΕΙΣ") && !question_mentions_notes("τι ώρα είναι"), "notes intent");

    // Voice-note files stay unique when the clock is unknown or the counter
    // restarts with the device: a new note never overwrites an old one's audio.
    const auto unknown_clock = voice_note_file_name(0, 1, 0x1234abcdU);
    expect(unknown_clock != voice_note_file_name(0, 1, 0x00c0ffeeU), "the random part separates restarts");
    const auto longest = voice_note_file_name(INT64_MAX / 2, UINT32_MAX, UINT32_MAX);
    NotesModel named;
    expect(named.add_voice(unknown_clock, 1000, 0) == NoteResult::Ok &&
           named.add_voice(longest, 1000, 0) == NoteResult::Ok, "generated names are safe note file names");

    // A transcription keeps its note: a spoken question or a cancelled listen
    // in the meantime never drops it.
    NotesModel pending;
    std::uint32_t first_note = 0;
    std::uint32_t second_note = 0;
    pending.add_voice("note-a.wav", 1000, 0, &first_note);
    pending.add_voice("note-b.wav", 1000, 0, &second_note);
    NoteTranscriptions jobs;
    std::promise<TranscriptResult> first_job;
    std::promise<TranscriptResult> second_job;
    expect(jobs.start(first_note, first_job.get_future(), pending) &&
           jobs.start(second_note, second_job.get_future(), pending), "two notes transcribe at once");
    second_job.set_value({true, "Δεύτερη σημείωση", {}});
    jobs.poll(pending);
    expect(pending.find(second_note)->transcript == NoteTranscript::Ready &&
           pending.find(second_note)->text == "Δεύτερη σημείωση" &&
           pending.find(first_note)->transcript == NoteTranscript::Pending, "each result reaches its own note");
    first_job.set_value({false, {}, "STT_FAILED"});
    jobs.poll(pending);
    expect(pending.find(first_note)->transcript == NoteTranscript::Failed && jobs.empty(),
           "a failed transcription is marked on its note");
    std::vector<std::promise<TranscriptResult>> waiting(NoteTranscriptions::kMaxPending + 1);
    std::vector<std::uint32_t> waiting_notes;
    for (std::size_t index = 0; index < waiting.size(); ++index) {
        std::uint32_t id = 0;
        pending.add_voice("note-w" + std::to_string(index) + ".wav", 1000, 0, &id);
        waiting_notes.push_back(id);
    }
    for (std::size_t index = 0; index < NoteTranscriptions::kMaxPending; ++index) {
        jobs.start(waiting_notes[index], waiting[index].get_future(), pending);
    }
    expect(!jobs.start(waiting_notes.back(), waiting.back().get_future(), pending) &&
           pending.find(waiting_notes.back())->transcript == NoteTranscript::Failed,
           "too many at once: the extra note is marked, not left waiting");
    expect(jobs.release().size() == NoteTranscriptions::kMaxPending && jobs.empty(),
           "unfinished jobs can be handed over at shutdown");
}

void assistant_tests() {
    using namespace aura;
    ConversationMemory memory;
    memory.record("Τι καιρό κάνει;", "Ήλιος.", 1000);
    memory.record("Και αύριο;", "Βροχή.", 2000);
    expect(memory.recent(3000).size() == 2, "follow-ups see earlier turns");
    for (int index = 0; index < 6; ++index) memory.record("q", "a", 4000);
    expect(memory.recent(5000).size() == ConversationMemory::kMaxTurns, "memory is bounded");
    expect(memory.recent(5000 + ConversationMemory::kExpiryMs + 1).empty() && memory.size() == 0,
           "quiet minutes forget the conversation");
    expect(question_requests_web_search("Ψάξε για πίτσα στη Σπάρτη") &&
           question_requests_web_search("look up restaurants in Sparta"),
           "explicit Greek and English searches request the web tool");
    expect(!question_requests_web_search("Ψάξε στις σημειώσεις μου το τηλέφωνο") &&
           !question_requests_web_search("Πες μου για τη Σπάρτη"),
           "private collections and ordinary questions do not enable web search");

    auto parsed = parse_assistant_answer("Βάζω χρονόμετρο.\nACTION: {\"type\":\"timer\",\"seconds\":600,\"label\":\"Ζυμαρικά\"}");
    expect(parsed.text == "Βάζω χρονόμετρο." && parsed.action && parsed.action->type == AssistantActionType::Timer &&
           parsed.action->seconds == 600 && parsed.action->label == "Ζυμαρικά", "a timer action is parsed");
    parsed = parse_assistant_answer("OK\n```\nACTION: {\"type\":\"alarm\",\"time\":\"7:05\",\"repeat\":\"daily\"}\n```\n");
    expect(parsed.text == "OK" && parsed.action && parsed.action->hour == 7 && parsed.action->minute == 5 &&
           parsed.action->daily, "a fenced alarm action is parsed and hidden");
    parsed = parse_assistant_answer("**Ζυμαρικά**\n- σε 10 λεπτά\nACTION: {\"type\":\"timer\",\"seconds\":600}");
    expect(parsed.text == "Ζυμαρικά\nσε 10 λεπτά" && parsed.action && parsed.action->seconds == 600,
           "the shown and spoken answer is plain text, and its action still parses");
    parsed = parse_assistant_answer("Done.\nACTION: {\"type\":\"shell\",\"cmd\":\"rm -rf /\"}");
    expect(!parsed.action && parsed.action_rejected && parsed.text == "Done.", "unknown actions are rejected");
    parsed = parse_assistant_answer("ACTION: {\"type\":\"timer\",\"seconds\":600}\nMore text after");
    expect(!parsed.action && parsed.action_rejected && parsed.text == "More text after",
           "an action that is not the last line is not executed");
    parsed = parse_assistant_answer("x\nACTION: {\"type\":\"timer\",\"seconds\":999999}");
    expect(!parsed.action && parsed.action_rejected, "out-of-range timers are rejected");
    parsed = parse_assistant_answer("x\nACTION: {\"type\":\"reminder\",\"time\":\"18:00\",\"day\":\"tomorrow\",\"text\":\"Φάρμακα\"}");
    expect(parsed.action && parsed.action->type == AssistantActionType::Reminder && parsed.action->tomorrow,
           "a reminder for tomorrow");
    parsed = parse_assistant_answer("x\nACTION: {\"type\":\"task\",\"text\":\"Πάρε χαρτί\"}");
    expect(parsed.action && parsed.action->type == AssistantActionType::Task &&
           parsed.action->text == "Πάρε χαρτί", "a task action is parsed");
    parsed = parse_assistant_answer(
        "x\nACTION: {\"type\":\"calendar_event\",\"start\":\"2026-10-28T18:30\",\"duration_minutes\":45,\"text\":\"Οδοντίατρος\"}");
    expect(parsed.action && parsed.action->type == AssistantActionType::CalendarEvent &&
           parsed.action->duration_minutes == 45 &&
           from_minute_index(parsed.action->start_minute).hour == 18, "a calendar action is parsed");
    parsed = parse_assistant_answer(
        "x\nACTION: {\"type\":\"calendar_event\",\"start\":\"2026-02-31T18:30\",\"text\":\"invalid\"}");
    expect(!parsed.action && parsed.action_rejected, "an invalid calendar date is rejected");
    parsed = parse_assistant_answer("x\nACTION: {\"type\":\"reminder\",\"time\":\"18:00\",\"day\":\"tomorrow\",\"text\":\"Φάρμακα\"}");
    const auto now = minute_index(at(2026, 10, 22, 19, 0));
    expect(reminder_due_minute(*parsed.action, now) == minute_index(at(2026, 10, 23, 18, 0)), "tomorrow 18:00");
    AssistantAction today;
    today.hour = 18;
    expect(reminder_due_minute(today, now) == minute_index(at(2026, 10, 23, 18, 0)), "a passed time rolls to tomorrow");
    parsed = parse_assistant_answer("plain answer");
    expect(!parsed.action && !parsed.action_rejected && parsed.text == "plain answer", "plain answers pass through");

    // A refused action says so: the owner never believes something was done.
    parsed = parse_assistant_answer("Βάζω υπενθύμιση.\nACTION: {\"type\":\"reminder\",\"time\":\"25:00\",\"text\":\"x\"}");
    const auto refused = answer_display_text(parsed, "Να το κάνω;", "Δεν έγινε τίποτα.");
    expect(refused.rfind("Βάζω υπενθύμιση.", 0) == 0 && refused.find("Δεν έγινε τίποτα.") != std::string::npos,
           "a refused action is reported under the answer");
    parsed = parse_assistant_answer("ACTION: {\"type\":\"factory_reset\"}");
    expect(answer_display_text(parsed, "Να το κάνω;", "Δεν έγινε τίποτα.") == "Δεν έγινε τίποτα.",
           "the raw ACTION line is never shown");

    // The whole device from the model's line too (the owner, 2026-11-18), as
    // typed values only, each still waiting for the owner's confirmation.
    const auto proposed = [](const char* json) {
        return parse_assistant_answer(std::string("x\nACTION: ") + json).action;
    };
    auto device = proposed("{\"type\":\"volume\",\"value\":40}");
    expect(device && device->type == AssistantActionType::Volume && device->value == 40 && !device->relative,
           "the volume, to a value");
    device = proposed("{\"type\":\"volume\",\"step\":-10}");
    expect(device && device->type == AssistantActionType::Volume && device->value == -10 && device->relative,
           "the volume, by a step");
    expect(!proposed("{\"type\":\"volume\",\"value\":140}") && !proposed("{\"type\":\"volume\"}") &&
               !proposed("{\"type\":\"volume\",\"value\":40,\"step\":10}") &&
               !proposed("{\"type\":\"volume\",\"step\":0}"), "a volume that makes no sense is refused");
    device = proposed("{\"type\":\"brightness\",\"value\":70}");
    expect(device && device->type == AssistantActionType::Brightness && device->value == 70 && !device->relative,
           "the brightness, to a value");
    device = proposed("{\"type\":\"brightness\",\"step\":20}");
    expect(device && device->type == AssistantActionType::Brightness && device->value == 20 && device->relative,
           "the brightness, by a step");
    expect(!proposed("{\"type\":\"brightness\",\"value\":2}"), "a dark screen is refused, as in Οθόνη");
    device = proposed("{\"type\":\"mute\",\"on\":true}");
    expect(device && device->type == AssistantActionType::Mute && device->value == 1, "mute");
    device = proposed("{\"type\":\"mute\",\"on\":false}");
    expect(device && device->type == AssistantActionType::Mute && device->value == 0, "unmute");
    expect(!proposed("{\"type\":\"mute\"}"), "mute says which");
    device = proposed("{\"type\":\"open\",\"app\":\"weather\"}");
    expect(device && device->type == AssistantActionType::OpenApp && device->app == "weather", "open an app");
    expect(!proposed("{\"type\":\"open\",\"app\":\"terminal\"}") && !proposed("{\"type\":\"open\"}"),
           "only the device's own apps open");
    device = proposed("{\"type\":\"restart\"}");
    expect(device && device->type == AssistantActionType::Restart, "restart");
    device = proposed("{\"type\":\"power_off\"}");
    expect(device && device->type == AssistantActionType::PowerOff, "switch off");
    expect(std::string(assistant_action_name(AssistantActionType::OpenApp)) == "open" &&
               std::string(assistant_action_name(AssistantActionType::Restart)) == "restart" &&
               std::string(assistant_action_name(AssistantActionType::PowerOff)) == "power_off",
           "the new actions have names");

    ClockServicesModel action_clock;
    NotesModel action_notes;
    OrganizerModel action_organizer;
    DisplayPowerModel action_display;
    MockAudioService action_audio;
    action_audio.initialize();
    SoundSettings action_sound;
    const AssistantActionTargets targets{action_clock, action_notes, action_organizer, action_display,
                                         action_audio, action_sound, 0};
    auto lit = action_display.snapshot().settings;
    lit.active_brightness_percent = 50;
    action_display.set_settings(lit);
    AssistantAction step;
    step.type = AssistantActionType::Brightness;
    step.relative = true;
    step.value = 20;
    expect(execute_assistant_action(step, targets) &&
               action_display.snapshot().settings.active_brightness_percent == 70, "brighter by a step");
    step.value = 60;
    expect(execute_assistant_action(step, targets) &&
               action_display.snapshot().settings.active_brightness_percent == 100, "no brighter than full");
    step.value = -100;
    expect(execute_assistant_action(step, targets) &&
               action_display.snapshot().settings.active_brightness_percent == 5, "never dark");
    for (const auto host : {AssistantActionType::OpenApp, AssistantActionType::Restart, AssistantActionType::PowerOff}) {
        AssistantAction action;
        action.type = host;
        expect(!execute_assistant_action(action, targets), "the host opens apps, restarts and switches off");
    }
    parsed = parse_assistant_answer("ACTION: {\"type\":\"timer\",\"seconds\":60}");
    expect(answer_display_text(parsed, "Να το κάνω;", "Δεν έγινε τίποτα.") == "Να το κάνω;",
           "an action alone asks for confirmation");
    parsed = parse_assistant_answer("Μόνο κείμενο");
    expect(answer_display_text(parsed, "Να το κάνω;", "Δεν έγινε τίποτα.") == "Μόνο κείμενο",
           "an ordinary answer is shown as it is");

    DeviceStatusModel status;
    status.set_clock(14, 5, StatusOrigin::Live);
    status.set_date(2026, 10, 22, 4, StatusOrigin::Live);
    status.set_power(78, false, StatusOrigin::Simulated);
    ClockServicesModel clock;
    clock.start_timer(600, "Ζυμαρικά");
    NotesModel notes;
    notes.add_text("Κωδικός πόρτας 4321", 0);
    DeviceContextInput input;
    input.status = &status.snapshot();
    input.clock = &clock;
    input.notes = &notes;
    input.question = "Πόση μπαταρία έχω;";
    const auto context = build_device_context(input);
    expect(context.find("14:05, Thursday 2026-10-22") != std::string::npos && context.find("78% (simulated)") !=
           std::string::npos && context.find("10:00 left") != std::string::npos, "device facts are summarised");
    expect(context.find("4321") == std::string::npos, "notes are not shared with unrelated questions");

    // The facts a question asks for come first: a crowded context loses
    // general facts, never the notes the owner asked about.
    ClockServicesModel crowded;
    crowded.set_clock(at(2026, 10, 22, 14, 5));
    for (int index = 0; index < 8; ++index) {
        crowded.add_alarm(6 + index, 15, index % 2 == 0, "Ξυπνητήρι με αρκετά μεγάλη ετικέτα " + std::to_string(index));
    }
    for (int index = 0; index < 5; ++index) {
        crowded.add_reminder_in(30 + index, std::to_string(index) + " υπενθύμιση με πολύ μεγάλο κείμενο για να "
                                "γεμίσει εντελώς το πλαίσιο της συσκευής και να μη χωράει τίποτα άλλο εδώ");
    }
    for (int index = 0; index < 4; ++index) crowded.start_timer(600 + index, "Χρονόμετρο " + std::to_string(index));
    NotesModel many_notes;
    many_notes.add_text("ΠΑΛΙΑ-ΣΗΜΕΙΩΣΗ: ο κωδικός της αποθήκης είναι στο συρτάρι της κουζίνας", 0);
    for (int index = 0; index < 7; ++index) {
        many_notes.add_text("Σημείωση " + std::to_string(index) + ": λίστα με πολλά πράγματα που πρέπει να θυμάμαι "
                            "για το ταξίδι στη Σπάρτη και τη δουλειά της εβδομάδας", 0);
    }
    // A full forecast, as the device always sends one.
    WeatherSnapshot forecast;
    forecast.has_data = true;
    forecast.data.location.name = "Σπάρτη";
    forecast.data.observed_at = "2026-10-22T14:00";
    forecast.data.hourly_count = std::min<std::size_t>(8, forecast.data.hourly.size());
    for (std::size_t index = 0; index < forecast.data.hourly_count; ++index) {
        forecast.data.hourly[index].time = "2026-10-22T" + std::to_string(10 + index) + ":00";
    }
    forecast.data.daily_count = std::min<std::size_t>(5, forecast.data.daily.size());
    for (std::size_t index = 0; index < forecast.data.daily_count; ++index) {
        forecast.data.daily[index].date = "2026-10-2" + std::to_string(2 + index);
    }
    FuelPriceSnapshot fuel;
    fuel.state = FuelPriceState::Ready;
    fuel.data.period_date = "2026-10-19";
    fuel.data.source = "European Commission Weekly Oil Bulletin";
    fuel.data.unleaded95_millieur_per_litre = 1912;
    fuel.data.diesel_millieur_per_litre = 1634;
    DeviceContextInput crowded_input;
    crowded_input.status = &status.snapshot();
    crowded_input.weather = &forecast;
    crowded_input.fuel = &fuel;
    crowded_input.unread_messages = 3;
    crowded_input.clock = &crowded;
    crowded_input.notes = &many_notes;
    crowded_input.question = "Τι έχω στις σημειώσεις μου;";
    const auto crowded_context = build_device_context(crowded_input);
    expect(crowded_context.size() <= 3000 && crowded_context.find("ΠΑΛΙΑ-ΣΗΜΕΙΩΣΗ") != std::string::npos,
           "the asked-for notes survive a crowded context");
    expect(crowded_context.find("local time: 14:05") != std::string::npos, "the time still leads the facts");

    TurnRequest request;
    request.prompt = "Βάλε χρονόμετρο";
    const auto demo = mock_answer(request);
    expect(demo.ok && parse_assistant_answer(demo.text).action, "the demo provider can propose an action");
    request.interface_language = InterfaceLanguage::English;
    request.prompt = "Set a timer";
    const auto english_demo = mock_answer(request);
    expect(english_demo.ok && english_demo.text.find("I can set") != std::string::npos,
           "the demo provider follows the selected English interface language");
    request.prompt = "Tell me about Taygetos";
    const auto english_taygetos = mock_answer(request);
    expect(english_taygetos.ok && english_taygetos.text.find("Taygetos") != std::string::npos &&
               english_taygetos.text.find("Ταΰγετος") == std::string::npos,
           "the Taygetos demo answer follows the selected English interface language");
}

void briefing_tests() {
    using namespace aura;
    DeviceStatusModel status;
    status.set_clock(7, 10, StatusOrigin::Live);
    status.set_date(2026, 10, 22, 4, StatusOrigin::Live);
    ClockServicesModel clock;
    clock.set_clock(at(2026, 10, 22, 7, 10));
    clock.add_alarm(8, 0, false, "");
    clock.add_reminder_in(30, "Φάρμακα");
    BriefingInput input;
    input.status = &status.snapshot();
    input.clock = &clock.snapshot();
    input.unread_messages = 2;
    const auto briefing = build_briefing(input);
    expect(briefing.period == BriefingPeriod::Morning && briefing.date_valid && briefing.items.size() == 4,
           "the morning briefing gathers calendar, alarm, reminders and messages");
    const auto item = [&](BriefingItemKind kind) {
        return std::find_if(briefing.items.begin(), briefing.items.end(),
                            [kind](const BriefingItem& value) { return value.kind == kind; });
    };
    expect(item(BriefingItemKind::NextHoliday) != briefing.items.end() &&
           item(BriefingItemKind::Alarm) != briefing.items.end() &&
           item(BriefingItemKind::Reminders)->count == 1 && item(BriefingItemKind::Messages)->count == 2,
           "briefing contains each expected fact");
    input.language = InterfaceLanguage::English;
    const auto english_briefing = build_briefing(input);
    const auto english_holiday = std::find_if(english_briefing.items.begin(), english_briefing.items.end(),
        [](const BriefingItem& value) { return value.kind == BriefingItemKind::NextHoliday; });
    expect(english_holiday != english_briefing.items.end() && english_holiday->text == "Ohi Day",
           "holiday names follow the selected English interface language");
    const auto today = days_from_civil(2026, 10, 22);
    expect(should_auto_show_briefing(status.snapshot(), today - 1) && !should_auto_show_briefing(status.snapshot(), today),
           "the briefing opens once a morning");
    status.set_clock(13, 0, StatusOrigin::Live);
    expect(!should_auto_show_briefing(status.snapshot(), today - 1), "not in the afternoon");

    // With several timers, "Today" names the one that ends first.
    ClockServicesModel kitchen;
    kitchen.start_timer(60, "Αυγά");
    kitchen.start_timer(900, "Φούρνος");
    BriefingInput timer_input;
    timer_input.clock = &kitchen.snapshot();
    const auto timer_briefing = build_briefing(timer_input);
    const auto timers = std::find_if(timer_briefing.items.begin(), timer_briefing.items.end(),
                                     [](const BriefingItem& value) { return value.kind == BriefingItemKind::Timers; });
    expect(timers != timer_briefing.items.end() && timers->count == 2 && timers->value == 60 && timers->text == "Αυγά",
           "the briefing names the timer that ends first");
}

void motion_tests() {
    using namespace aura;
    ImuGestureDetector detector;
    std::uint32_t t = 0;
    auto feed = [&](float ax, float ay, float az, int samples, ImuGesture* seen) {
        for (int index = 0; index < samples; ++index) {
            t += 20;
            const auto gesture = detector.push({ax, ay, az, t});
            if (gesture != ImuGesture::None && seen && *seen == ImuGesture::None) *seen = gesture;
        }
    };
    // AURA rests on a desk, standing on any edge or lying flat (ImuSample's
    // frame: +y is the top edge, +z out of the screen; a reading is "up").
    // Picking it up wakes it however it is held; the phone-style rule (the
    // screen had to end tilted up towards the face) never fired on the board,
    // held upright or stood on its side (the first flash, 2026-11-14).
    ImuGesture seen = ImuGesture::None;
    // Standing upright on the desk, then lifted (the hand's push) and held a
    // little tilted back.
    feed(0.0F, 1.0F, 0.0F, 50, &seen);
    expect(seen == ImuGesture::None, "standing still on the desk does nothing");
    feed(0.0F, 1.22F, 0.08F, 6, &seen);
    feed(0.0F, 0.97F, 0.25F, 20, &seen);
    expect(seen == ImuGesture::Raise, "picking up the upright device is a raise");
    seen = ImuGesture::None;
    feed(0.0F, 0.97F, 0.25F, 60, &seen);
    expect(seen == ImuGesture::None, "holding still is not another raise");
    // Put down flat (handling it may count), then lying still does nothing.
    feed(0.0F, 0.0F, 1.0F, 50, nullptr);
    feed(0.0F, 0.0F, 1.0F, 100, &seen);
    expect(seen == ImuGesture::None, "lying flat does nothing");
    // Picked up from flat and turned towards the face.
    for (int step = 1; step <= 10; ++step) {
        const float angle = static_cast<float>(step) / 10.0F * 0.9F;
        feed(0.0F, std::sin(angle), std::cos(angle), 1, &seen);
    }
    feed(0.0F, 0.78F, 0.62F, 20, &seen);
    expect(seen == ImuGesture::Raise, "picking up the device lying flat is a raise");
    // Standing on its right edge (the screen turned), then lifted.
    feed(-1.0F, 0.0F, 0.0F, 90, nullptr);
    seen = ImuGesture::None;
    feed(-1.2F, 0.05F, 0.1F, 6, &seen);
    feed(-0.95F, 0.0F, 0.3F, 20, &seen);
    expect(seen == ImuGesture::Raise, "picking it up from any edge is a raise");
    // A knock on the desk shakes it for a sample or two.
    feed(-0.95F, 0.0F, 0.3F, 90, nullptr);
    seen = ImuGesture::None;
    feed(-1.4F, 0.2F, 0.5F, 2, &seen);
    feed(-0.95F, 0.0F, 0.3F, 30, &seen);
    expect(seen == ImuGesture::None, "a knock on the desk is not a raise");
    // Carried around without a pause: one raise, once it has been handled a while.
    feed(0.0F, 1.0F, 0.0F, 50, nullptr);
    int raises = 0;
    for (int step = 0; step < 150; ++step) {
        const float sway = 0.3F * std::sin(static_cast<float>(step) * 0.5F);
        t += 20;
        if (detector.push({sway, 0.9F, 0.3F + sway, t}) == ImuGesture::Raise) ++raises;
    }
    expect(raises == 1, "moving without a pause raises once");
    // At rest flat, then turned face down: that mutes it, never a raise first.
    feed(0.0F, 0.0F, 1.0F, 60, nullptr);
    seen = ImuGesture::None;
    feed(0.0F, 0.0F, -1.0F, 60, &seen);
    expect(seen == ImuGesture::FaceDown && detector.face_down(), "face down after a moment");
    seen = ImuGesture::None;
    feed(0.0F, 0.0F, 1.0F, 30, &seen);
    expect(seen == ImuGesture::FaceUp && !detector.face_down(), "turning it back up");

    DisplayPowerModel display;
    DisplaySettings settings;
    settings.timeout_ms = 1000;
    settings.dim_ms = 500;
    display.set_settings(settings);
    display.user_activity(0);
    display.tick(999);
    expect(display.snapshot().mode == DisplayMode::Active, "active until the timeout");
    display.tick(1000);
    // The first flash (2026-11-14): the always-on clock at 12% was too dark to
    // read on the AMOLED; the owner chose 30%, and 50% for the dimmed screen.
    expect(display.snapshot().mode == DisplayMode::Dimmed && display.snapshot().brightness_percent == 50, "dims");
    display.tick(1500);
    expect(display.snapshot().mode == DisplayMode::AlwaysOn, "then the always-on clock");
    expect(display.snapshot().brightness_percent == 30, "the always-on clock at full brightness setting");

    // Dimming never brightens a screen the owner set darker than the dim level.
    DisplayPowerModel dark;
    DisplaySettings dark_settings = settings;
    dark_settings.active_brightness_percent = 10;
    dark.set_settings(dark_settings);
    dark.user_activity(0);
    dark.tick(1000);
    const int dark_dimmed = dark.snapshot().brightness_percent;
    expect(dark.snapshot().mode == DisplayMode::Dimmed && dark_dimmed > 0 && dark_dimmed < 10,
           "a dark screen dims below its own level");
    dark.tick(1500);
    expect(dark.snapshot().mode == DisplayMode::AlwaysOn && dark.snapshot().brightness_percent > 0 &&
               dark.snapshot().brightness_percent <= dark_dimmed,
           "the always-on clock is no brighter than the dimmed screen");
    dark_settings.active_brightness_percent = 5;
    dark.set_settings(dark_settings);
    expect(dark.snapshot().brightness_percent <= 5, "a darker setting applies to the always-on clock at once");
    expect(display.user_activity(2000) && display.snapshot().mode == DisplayMode::Active,
           "the first touch only wakes the screen");
    expect(!display.user_activity(2100), "later touches act normally");
    display.gesture(ImuGesture::FaceDown, 2200);
    expect(display.snapshot().mode == DisplayMode::Off && display.snapshot().muted_by_flip, "face down mutes and sleeps");
    display.hold_awake(true, 2300);
    expect(display.snapshot().mode == DisplayMode::Off, "an alert does not light a face-down screen");
    display.hold_awake(false, 2400);
    display.gesture(ImuGesture::FaceUp, 2500);
    expect(display.snapshot().mode == DisplayMode::Active && !display.snapshot().muted_by_flip, "face up wakes");
    display.sleep_now(2600);
    display.gesture(ImuGesture::Raise, 2700);
    expect(display.snapshot().mode == DisplayMode::Active, "raise to wake");
    display.hold_awake(true, 2800);
    display.tick(100000);
    expect(display.snapshot().mode == DisplayMode::Active, "held awake while something important runs");
    settings.always_on = false;
    display.set_settings(settings);
    display.hold_awake(false, 100000);
    display.tick(101000);
    display.tick(102000);
    expect(display.snapshot().mode == DisplayMode::Off, "without always-on the screen turns off");
    DisplaySettings persisted;
    persisted.always_on = false;
    persisted.raise_to_wake = false;
    persisted.flip_to_mute = false;
    persisted.active_brightness_percent = 65;
    persisted.timeout_ms = 45000;
    persisted.dim_ms = 8000;
    const auto restored = parse_display_settings(serialize_display_settings(persisted));
    expect(!restored.always_on && !restored.raise_to_wake && !restored.flip_to_mute &&
               restored.active_brightness_percent == 65 &&
               restored.timeout_ms == 45000 && restored.dim_ms == 8000,
           "display settings round-trip through bounded persistence");
    const int x = display.snapshot().shift_x;
    display.set_minute(1);
    display.set_minute(2);
    expect(display.snapshot().shift_x != x || display.snapshot().shift_y != 0, "the always-on clock moves");
}

void phone_tests() {
    using namespace aura;
    const std::uint8_t source[8]{0, 0x1A, 4, 1, 0x78, 0x56, 0x34, 0x12};
    const auto event = ancs::parse_notification_source(source, sizeof(source));
    expect(event && event->event == ancs::EventId::Added && event->uid == 0x12345678U && event->important() &&
           event->positive_action() && event->negative_action() && !event->silent() &&
           ancs::category_from_id(event->category) == PhoneCategory::Social, "ANCS notification source");
    expect(!ancs::parse_notification_source(source, 7), "short packets are rejected");
    const auto command = ancs::get_notification_attributes(0x12345678U);
    expect(command.size() == 16 && command[0] == 0 && command[1] == 0x78 && command[4] == 0x12 && command[5] == 0 &&
           command[6] == 1 && command[7] == 64 && command[15] == 5, "ANCS attribute request layout");
    const auto action = ancs::perform_action(7, false);
    expect(action.size() == 6 && action[0] == 2 && action[1] == 7 && action[5] == 1, "ANCS negative action");

    std::vector<std::uint8_t> response{0, 0x78, 0x56, 0x34, 0x12};
    auto attribute = [&response](std::uint8_t id, const std::string& value) {
        response.push_back(id);
        response.push_back(static_cast<std::uint8_t>(value.size() & 0xFF));
        response.push_back(static_cast<std::uint8_t>(value.size() >> 8));
        response.insert(response.end(), value.begin(), value.end());
    };
    attribute(0, "net.whatsapp.WhatsApp");
    attribute(1, "Μαρία");
    attribute(2, "");
    attribute(3, "Θα αργήσω 10 λεπτά");
    attribute(5, "20261022T091500");
    ancs::AttributeAssembler assembler;
    expect(assembler.push(response.data(), 20) == ancs::AttributeAssembler::Result::NeedMore,
           "a fragment waits for the rest");
    expect(assembler.push(response.data() + 20, response.size() - 20) == ancs::AttributeAssembler::Result::Complete &&
           assembler.uid() == 0x12345678U && assembler.title() == "Μαρία" &&
           assembler.message() == "Θα αργήσω 10 λεπτά" && assembler.date() == "20261022T091500",
           "fragments reassemble");
    expect(ancs::app_display_name(assembler.app_identifier()) == "WhatsApp" &&
           ancs::app_display_name("com.example.Weather") == "Weather", "app names");
    const std::uint8_t bad[]{1, 0, 0, 0, 0};
    assembler.reset();
    expect(assembler.push(bad, sizeof(bad)) == ancs::AttributeAssembler::Result::Invalid, "wrong command is invalid");

    // One notification is fetched at a time; a reply that never comes is given
    // up, so it cannot stall every later notification.
    ancs::FetchScheduler fetches;
    fetches.enqueue(11);
    fetches.enqueue(12);
    fetches.enqueue(11);
    expect(fetches.next(0) == std::optional<std::uint32_t>(11), "the first notification is requested");
    expect(!fetches.next(1000) && fetches.in_flight(), "one request at a time");
    expect(fetches.next(ancs::FetchScheduler::kTimeoutMs + 1) == std::optional<std::uint32_t>(12),
           "a lost reply times out and the queue moves on");
    fetches.finished();
    expect(!fetches.in_flight() && !fetches.next(ancs::FetchScheduler::kTimeoutMs + 2), "a queued duplicate is not fetched twice");
    fetches.enqueue(13);
    fetches.remove(13);
    expect(!fetches.next(10000), "a removed notification is not fetched");

    PhoneNotificationModel phone;
    phone.set_link(PhoneLinkState::Connected, PhonePlatform::Iphone, "iPhone");
    PhoneNotification message;
    message.uid = 1;
    message.category = PhoneCategory::Social;
    message.app = "WhatsApp";
    message.title = "Μαρία";
    message.message = "Θα αργήσω";
    phone.upsert(message);
    PhoneNotification call;
    call.uid = 2;
    call.category = PhoneCategory::IncomingCall;
    call.title = "Γιώργος";
    phone.upsert(call);
    expect(phone.snapshot().notifications.size() == 1 && phone.snapshot().incoming_call, "calls are separate");
    expect(phone.remove(2) && !phone.snapshot().incoming_call, "an ended call disappears");
    expect(phone.context_for("Τι μου έστειλαν στο κινητό;").find("Θα αργήσω") != std::string::npos &&
           phone.context_for("Πόσο κάνει 2+2;").empty(), "phone context only when asked");
    for (std::uint32_t uid = 10; uid < 40; ++uid) {
        message.uid = uid;
        phone.upsert(message);
    }
    expect(phone.snapshot().notifications.size() == PhoneNotificationModel::kMaxNotifications, "bounded list");
    phone.set_link(PhoneLinkState::NotPaired, PhonePlatform::Unknown, "");
    expect(phone.snapshot().notifications.empty(), "unpairing forgets the phone's notifications");
}

// Stands in for the platform's ECDSA check: it records what it was asked to
// verify and answers as told.
struct FakeUpdateVerifier final : aura::IUpdateSignatureVerifier {
    bool key{true};
    bool valid{true};
    mutable std::string message;
    mutable std::uint8_t first_byte{};
    bool configured() const override { return key; }
    bool verify(const std::string& signed_message, const std::array<std::uint8_t, 64>& signature) const override {
        message = signed_message;
        first_byte = signature[0];
        return key && valid;
    }
};

// The owner's Bluetooth switch (2026-11-19): on, the radio runs only for a
// phone (off when not needed, for the battery); off, never; and the choice
// survives a restart.
void bluetooth_tests() {
    using namespace aura;
    expect(BluetoothSettings{}.enabled, "Bluetooth is allowed by default");
    const BluetoothSettings on{true};
    const BluetoothSettings off{false};
    expect(!bluetooth_should_run(on, false, false), "allowed, but off with no phone paired or being paired");
    expect(bluetooth_should_run(on, true, false), "on for a paired phone");
    expect(bluetooth_should_run(on, false, true), "on while a phone is being paired");
    expect(!bluetooth_should_run(off, true, true), "switched off: never");
    expect(parse_bluetooth_settings(serialize_bluetooth_settings(off)) == off &&
               parse_bluetooth_settings(serialize_bluetooth_settings(on)) == on,
           "the choice round-trips through its store");
    expect(parse_bluetooth_settings("") == BluetoothSettings{} &&
               parse_bluetooth_settings("aura-bluetooth 1\nenabled=2\n") == BluetoothSettings{} &&
               parse_bluetooth_settings("other 1\nenabled=0\n") == BluetoothSettings{},
           "a missing or bad store keeps the default");
    PhoneNotificationModel phone;
    expect(phone.snapshot().bluetooth_enabled && !phone.snapshot().bluetooth_on, "allowed and not running at first");
    const auto before = phone.snapshot().sequence;
    phone.set_bluetooth(false, false);
    expect(!phone.snapshot().bluetooth_enabled && !phone.snapshot().bluetooth_on && phone.snapshot().sequence != before,
           "the phone's snapshot shows the switch and the radio");
    const auto same = phone.snapshot().sequence;
    phone.set_bluetooth(false, false);
    expect(phone.snapshot().sequence == same, "the same state again is no change");
}

void ota_tests() {
    using namespace aura;
    expect(compare_versions("1.10.0", "1.9.9") > 0 && compare_versions("1.0.0", "1.0.0") == 0 &&
           compare_versions("bad", "1.0.0") < 0 && !version_valid("1.0") && version_valid("13.0.0"), "versions");
    const std::string hash(64, 'a');
    const std::string signature = "ab" + std::string(126, '0');
    std::string error;
    FakeUpdateVerifier verifier;
    auto manifest = parse_update_manifest(
        "{\"version\":\"13.1.0\",\"url\":\"https://updates.example/aura.bin\",\"size\":1234567,\"sha256\":\"" + hash +
        "\",\"notes\":\"Νέα\",\"signature\":\"" + signature + "\"}", verifier, &error);
    expect(manifest && manifest->size_bytes == 1234567 && manifest->notes == "Νέα", "a valid manifest");
    expect(verifier.message == "AURA-OTA-1\n13.1.0\n1234567\n" + hash + "\n" && verifier.first_byte == 0xAB,
           "the owner's signature is checked over the version, the size and the SHA-256");
    expect(!parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"http://x/a.bin\",\"size\":1,\"sha256\":\"" + hash +
                                  "\",\"signature\":\"" + signature + "\"}", verifier, &error) &&
           error == "MANIFEST_BAD_URL", "plain HTTP is refused");
    expect(!parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"https://x/a.bin\",\"size\":99999999,\"sha256\":\"" +
                                  hash + "\",\"signature\":\"" + signature + "\"}", verifier, &error) &&
           error == "MANIFEST_BAD_SIZE", "oversized images are refused");
    expect(parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"https://x/a.bin\",\"size\":7340032,\"sha256\":\"" +
                                 hash + "\",\"signature\":\"" + signature + "\"}", verifier, &error).has_value(),
           "an image that fills one 7 MB app slot is accepted");
    expect(!parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"https://x/a.bin\",\"size\":7340033,\"sha256\":\"" +
                                  hash + "\",\"signature\":\"" + signature + "\"}", verifier, &error) &&
           error == "MANIFEST_BAD_SIZE", "one byte more than the slot is refused");
    const std::string unsigned_manifest = "{\"version\":\"13.1.0\",\"url\":\"https://x/a.bin\",\"size\":1,\"sha256\":\"" +
                                          hash + "\"}";
    expect(!parse_update_manifest(unsigned_manifest, verifier, &error) && error == "MANIFEST_UNSIGNED",
           "an update without the owner's signature is refused");
    expect(!parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"https://x/a.bin\",\"size\":1,\"sha256\":\"" +
                                  hash + "\",\"signature\":\"xyz\"}", verifier, &error) && error == "MANIFEST_UNSIGNED",
           "a malformed signature is no signature");
    verifier.valid = false;
    expect(!parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"https://x/a.bin\",\"size\":1,\"sha256\":\"" +
                                  hash + "\",\"signature\":\"" + signature + "\"}", verifier, &error) &&
           error == "MANIFEST_BAD_SIGNATURE", "a signature by another key, or for other bytes, is refused");
    verifier.key = false;
    expect(!parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"https://x/a.bin\",\"size\":1,\"sha256\":\"" +
                                  hash + "\",\"signature\":\"" + signature + "\"}", verifier, &error) &&
           error == "UPDATE_KEY_MISSING", "a build without the owner's public key installs nothing");
    verifier.key = true;
    verifier.valid = true;
    const std::string upper_hash(64, 'A');
    expect(parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"https://x/a.bin\",\"size\":1,\"sha256\":\"" +
                                 upper_hash + "\",\"signature\":\"" + signature + "\"}", verifier, &error) &&
           verifier.message == "AURA-OTA-1\n13.1.0\n1\n" + hash + "\n", "the signed SHA-256 is in lower case");

    OtaUpdateModel ota("13.0.0");
    expect(!ota.begin_check(), "no source, no check");
    ota.set_source(true, true);
    expect(ota.begin_check(), "check starts");
    ota.check_finished(manifest);
    expect(ota.snapshot().state == UpdateState::Available, "a newer version is offered");
    expect(ota.can_install(false, 80, true, false) == UpdateBlock::Offline &&
           ota.can_install(true, 20, true, false) == UpdateBlock::LowBattery &&
           ota.can_install(true, 20, true, true) == UpdateBlock::None &&
           ota.can_install(true, 0, false, false) == UpdateBlock::LowBattery, "install policy");
    expect(ota.begin_download(), "download starts");
    ota.set_progress(0.5F);
    ota.begin_verify();
    ota.ready_to_restart();
    expect(ota.snapshot().state == UpdateState::ReadyToRestart, "the update is ready");
    OtaUpdateModel same("13.1.0");
    same.set_source(true, false);
    same.begin_check();
    same.check_finished(manifest);
    expect(same.snapshot().state == UpdateState::UpToDate, "the same version is up to date");
}

// Speech-like sound: low-passed noise in syllables of 150 ms with 60 ms gaps,
// and a 400 ms pause every two seconds.
std::vector<std::int16_t> syllables(std::size_t count, double rms, std::uint32_t seed) {
    std::vector<std::int16_t> output(count);
    double smooth = 0.0;
    std::vector<double> raw(count);
    double energy = 0.0;
    std::size_t sounding = 0;
    for (std::size_t index = 0; index < count; ++index) {
        seed = seed * 1103515245U + 12345U;
        smooth = 0.85 * smooth + 0.15 * (static_cast<double>((seed >> 16) & 0x7FFFU) - 16384.0);
        const std::size_t in_phrase = index % 32000U;
        const bool on = in_phrase < 25600U && (in_phrase % 3360U) < 2400U;
        raw[index] = on ? smooth : 0.0;
        if (on) {
            energy += smooth * smooth;
            ++sounding;
        }
    }
    const double scale = sounding ? rms / std::sqrt(energy / static_cast<double>(sounding)) : 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        output[index] = static_cast<std::int16_t>(std::clamp(raw[index] * scale, -32767.0, 32767.0));
    }
    return output;
}

// The board's echo (2026-11-16, measured): the microphone hears the speaker
// about 2.2 times as loud as the reference slot (24 dB and 12 dB of gain), a
// little late, with two reflections, the speaker's clipping above `knee` and a
// tail an 8 ms filter cannot hold (`tail`).
std::vector<std::int16_t> board_echo(const std::vector<std::int16_t>& reference, double knee, double tail,
                                     std::uint32_t seed) {
    std::vector<std::int16_t> output(reference.size());
    double decay = 0.0;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const auto at = [&](std::size_t delay) { return index >= delay ? static_cast<double>(reference[index - delay]) : 0.0; };
        double echo = 2.2 * at(6) + 0.5 * at(21) - 0.25 * at(47);
        echo = knee * std::tanh(echo / knee);
        decay = 0.995 * decay + tail * at(160);
        seed = seed * 1103515245U + 12345U;
        const double noise = (static_cast<double>((seed >> 16) & 0x7FFFU) - 16384.0) / 16384.0 * 15.0;
        output[index] = static_cast<std::int16_t>(std::clamp(echo + decay + noise, -32767.0, 32767.0));
    }
    return output;
}

void echo_reference_gate_tests() {
    using namespace aura;
    static constexpr std::size_t kBlock = 480;  // the companion drains about 30 ms at a time
    const auto run = [](EchoReferenceGate& gate, const std::vector<std::int16_t>& microphone,
                        const std::vector<std::int16_t>& reference, std::size_t& when) {
        for (std::size_t at = 0; at < microphone.size(); at += kBlock) {
            const auto count = std::min(kBlock, microphone.size() - at);
            if (gate.observe(microphone.data() + at, reference.data() + at, count)) {
                when = at + count;
                return true;
            }
        }
        return false;
    };
    const std::size_t eight_seconds = 16000U * 8U;
    const auto over = [](const std::vector<std::int16_t>& echo, const std::vector<std::int16_t>& owner) {
        std::vector<std::int16_t> mixed(echo);
        for (std::size_t index = 16000U * 3U; index < mixed.size(); ++index) {
            mixed[index] = static_cast<std::int16_t>(std::clamp(static_cast<int>(mixed[index]) + owner[index], -32767, 32767));
        }
        return mixed;
    };
    std::size_t when = 0;

    // A clean echo: cancelled by more than 15 dB; a voice softer than the
    // echo itself still interrupts it.
    const auto reference = syllables(eight_seconds, 300.0, 7U);
    const auto echo = board_echo(reference, 9000.0, 0.004, 11U);
    EchoReferenceGate quiet_owner;
    expect(!run(quiet_owner, echo, reference, when), "the device's own voice, cancelled, never interrupts it");
    expect(quiet_owner.cancelled_db() > 15.0F, "a clean echo is cancelled by more than 15 dB");
    EchoReferenceGate soft_listening;
    expect(run(soft_listening, over(echo, syllables(eight_seconds, 250.0, 29U)), reference, when) &&
               when > 16000U * 3U && when < 16000U * 4U,
           "a voice softer than the echo interrupts it within a second");

    // A loud voice through a clipping speaker, with a longer tail: about
    // 12 dB is cancelled, and the rest must not sound like the owner; the
    // owner speaking up over it does, soon.
    const auto loud_reference = syllables(eight_seconds, 600.0, 7U);
    const auto harsh = board_echo(loud_reference, 2500.0, 0.012, 11U);
    EchoReferenceGate harsh_quiet;
    expect(!run(harsh_quiet, harsh, loud_reference, when) && harsh_quiet.cancelled_db() > 10.0F,
           "a loud, clipped echo is cancelled by more than 10 dB, and never interrupts");
    const auto owner = syllables(eight_seconds, 700.0, 23U);
    EchoReferenceGate listening;
    expect(run(listening, over(harsh, owner), loud_reference, when) && when > 16000U * 3U &&
               when < 16000U * 3U + 9600U,
           "the owner speaking over the answer interrupts it within 0.6 s");
    // For the log: how far the loudest sound over the answer rose above the
    // echo left over, against the margin that interrupts.
    expect(harsh_quiet.loudest() > 0.0F && harsh_quiet.loudest() < EchoReferenceGate::kOwnerMargin &&
               listening.loudest() >= EchoReferenceGate::kOwnerMargin,
           "the gate tells how close a sound came to interrupting");
    const auto echo_for_glitches = harsh;

    // The ES7210 starts with a quarter second of noise in every slot, and again
    // when the speaker starts while it listens (measured on the board): never
    // the owner.
    // The same settling swing in every slot, clipping at first, for 250 ms.
    std::vector<std::int16_t> glitched_microphone(echo_for_glitches);
    std::vector<std::int16_t> glitched_reference(loud_reference);
    for (const std::size_t start : {std::size_t{0}, std::size_t{16000U * 4U}}) {
        for (std::size_t index = start; index < start + 4000U; ++index) {
            const double age = static_cast<double>(index - start);
            const double swing = 60000.0 * std::exp(-age / 900.0) * std::sin(age * 2.0 * 3.14159265 * 35.0 / 16000.0);
            const auto value = static_cast<std::int16_t>(std::clamp(swing, -32767.0, 32767.0));
            glitched_microphone[index] = value;
            glitched_reference[index] = value;
        }
    }
    EchoReferenceGate glitching;
    expect(!run(glitching, glitched_microphone, glitched_reference, when),
           "the ES7210's start and restart noise does not interrupt");

    // No reference at all (the speaker silent): nothing is learnt, nothing interrupts.
    EchoReferenceGate silent;
    const std::vector<std::int16_t> nothing(16000U * 2U, 0);
    const auto room = syllables(16000U * 2U, 600.0, 31U);
    expect(!run(silent, room, nothing, when), "without the device's voice the gate does not decide");
}

void voice_tests() {
    using namespace aura;
    const auto rms = [](const std::vector<std::int16_t>& samples) {
        double energy = 0.0;
        for (const auto sample : samples) energy += static_cast<double>(sample) * sample;
        return samples.empty() ? 0.0 : std::sqrt(energy / static_cast<double>(samples.size()));
    };
    VoiceFrontEnd front_end;
    front_end.reset();
    std::vector<std::int16_t> room_noise(320);
    for (std::size_t index = 0; index < room_noise.size(); ++index) {
        room_noise[index] = static_cast<std::int16_t>(180.0 * std::sin(static_cast<double>(index) * 0.71));
    }
    std::vector<std::int16_t> processed;
    for (int index = 0; index < 30; ++index) front_end.process(room_noise.data(), room_noise.size(), processed);
    expect(rms(processed) < rms(room_noise) * 0.65, "stationary room noise is suppressed");
    std::vector<std::int16_t> quiet_voice(320);
    for (std::size_t index = 0; index < quiet_voice.size(); ++index) {
        quiet_voice[index] = static_cast<std::int16_t>(900.0 * std::sin(static_cast<double>(index) * 0.19));
    }
    for (int index = 0; index < 20; ++index) front_end.process(quiet_voice.data(), quiet_voice.size(), processed);
    expect(front_end.diagnostics().gain > 1.2F && front_end.diagnostics().speech_probability > 0.5F,
           "bounded AGC raises a quiet voice after noise calibration");

    AcousticEchoCanceller canceller;
    std::vector<std::int16_t> reference(1600);
    std::vector<std::int16_t> echoed_voice(1600);
    for (std::size_t index = 0; index < reference.size(); ++index) {
        reference[index] = static_cast<std::int16_t>(5000.0 * std::sin(static_cast<double>(index) * 0.12));
        echoed_voice[index] = static_cast<std::int16_t>(0.65 * reference[index] +
            1500.0 * std::sin(static_cast<double>(index) * 0.31));
    }
    expect(canceller.process(echoed_voice.data(), reference.data(), reference.size(), processed) &&
           canceller.reference_active() && canceller.suppression() > 0.5F && rms(processed) < rms(echoed_voice),
           "render-reference echo is measurably suppressed");
    canceller.reset();
    expect(!canceller.process(echoed_voice.data(), nullptr, echoed_voice.size(), processed) &&
           processed == echoed_voice, "AEC stays inactive without a real playback reference");

    // The real delay between rendering and the microphone is unknown: the
    // canceller finds it within the reference window instead of assuming one.
    std::vector<std::int16_t> rendered(1600 + 4000);
    std::uint32_t noise = 12345;
    double smooth = 0.0;
    for (auto& sample : rendered) {
        // Low-passed noise: speech-like, with most energy below 1 kHz.
        noise = noise * 1103515245U + 12345U;
        smooth = 0.85 * smooth + 0.15 * (static_cast<double>((noise >> 16) & 0x7FFFU) - 16384.0);
        sample = static_cast<std::int16_t>(std::clamp(smooth * 0.9, -12000.0, 12000.0));
    }
    const std::size_t true_offset = 2500;
    std::vector<std::int16_t> delayed_echo(1600);
    for (std::size_t index = 0; index < delayed_echo.size(); ++index) {
        delayed_echo[index] = static_cast<std::int16_t>(0.6 * rendered[true_offset + index] +
            900.0 * std::sin(static_cast<double>(index) * 0.31));
    }
    AcousticEchoCanceller searching;
    searching.process(delayed_echo.data(), rendered.data(), delayed_echo.size(), 4000, processed);
    searching.process(delayed_echo.data(), rendered.data(), delayed_echo.size(), 4000, processed);
    const auto found = searching.delay_offset();
    expect(found + 2 >= true_offset && found <= true_offset + 2, "the echo delay is found");
    expect(searching.reference_active() && searching.suppression() > 0.5F && rms(processed) < rms(delayed_echo) * 0.6,
           "a delayed echo is suppressed");

    VoiceRecorder recorder;
    recorder.start();
    std::vector<std::int16_t> quiet(1600, 40);
    for (int index = 0; index < 5; ++index) recorder.append(quiet.data(), quiet.size());
    expect(!recorder.speech_detected() && !recorder.should_stop(), "silence is not speech");
    std::vector<std::int16_t> voice(1600);
    for (std::size_t index = 0; index < voice.size(); ++index) {
        voice[index] = static_cast<std::int16_t>(6000.0 * std::sin(static_cast<double>(index) * 0.2));
    }
    for (int index = 0; index < 10; ++index) recorder.append(voice.data(), voice.size());
    expect(recorder.speech_detected() && recorder.level() > 0.5F, "speech raises the level");
    for (int index = 0; index < 13; ++index) recorder.append(quiet.data(), quiet.size());
    expect(recorder.should_stop() && !recorder.no_speech(), "a pause after speech ends the recording");
    const auto wav = recorder.finish_wav();
    PcmClip clip;
    expect(parse_wav(wav.data(), wav.size(), clip) && clip.format.sample_rate == 16000 &&
           clip.samples.size() == 28U * 1600U, "the recording is a valid 16 kHz WAV");
    recorder.start();
    for (int index = 0; index < 61; ++index) recorder.append(quiet.data(), quiet.size());
    expect(recorder.no_speech() && recorder.should_stop(), "no speech at all stops after six seconds");

    // A dictated note may pause to think and may last half a minute.
    VoiceRecorder dictation;
    dictation.start(VoiceRecorder::Mode::Dictation);
    for (int index = 0; index < 10; ++index) dictation.append(voice.data(), voice.size());
    for (int index = 0; index < 20; ++index) dictation.append(quiet.data(), quiet.size());
    expect(!dictation.should_stop(), "a two-second pause does not end a dictated note");
    for (int index = 0; index < 16; ++index) dictation.append(quiet.data(), quiet.size());
    expect(dictation.should_stop(), "a long pause ends it");
    dictation.start(VoiceRecorder::Mode::Dictation);
    for (int index = 0; index < 320; ++index) dictation.append(voice.data(), voice.size());
    expect(dictation.should_stop() && dictation.elapsed_ms() == VoiceRecorder::kDictationMaxMs &&
           VoiceRecorder::kDictationMaxMs == 30000U, "a dictated note lasts up to 30 seconds");
    recorder.start();
    for (int index = 0; index < 200; ++index) recorder.append(voice.data(), voice.size());
    expect(recorder.elapsed_ms() == VoiceRecorder::kMaxMs, "a spoken question still stops at 15 seconds");

    // Speech recognition gets the voice as it was heard (2026-11-16, the
    // owner: «κάνει αρκετά λάθη στην αναγνώριση»): no gain chosen every 20 ms
    // that clips a loud syllable, no gate that thins a soft one. The level and
    // the pauses still come from the front end.
    VoiceRecorder faithful;
    faithful.start();
    std::vector<std::int16_t> soft(1600);
    std::vector<std::int16_t> loud(1600);
    for (std::size_t index = 0; index < soft.size(); ++index) {
        soft[index] = static_cast<std::int16_t>(300.0 * std::sin(static_cast<double>(index) * 0.2));
        loud[index] = static_cast<std::int16_t>(20000.0 * std::sin(static_cast<double>(index) * 0.2));
    }
    for (int index = 0; index < 5; ++index) faithful.append(soft.data(), soft.size());
    for (int index = 0; index < 5; ++index) faithful.append(loud.data(), loud.size());
    const auto faithful_wav = faithful.finish_wav();
    PcmClip heard;
    expect(parse_wav(faithful_wav.data(), faithful_wav.size(), heard) && heard.samples.size() == 10U * 1600U,
           "the recording holds every sample");
    const auto peak_of = [&heard](std::size_t from, std::size_t to) {
        int peak = 0;
        for (std::size_t index = from; index < to; ++index) peak = std::max(peak, std::abs(static_cast<int>(heard.samples[index])));
        return peak;
    };
    expect(peak_of(1600, 8000) >= 280 && peak_of(1600, 8000) <= 320, "a soft voice keeps its level");
    expect(peak_of(9600, 16000) >= 19000 && peak_of(9600, 16000) <= 20400, "a loud voice is neither raised nor clipped");
    const std::uint8_t garbage[48]{};
    expect(!parse_wav(garbage, sizeof(garbage), clip), "garbage is not a WAV");

    BargeInGate gate;
    std::vector<std::int16_t> speaker_bleed(1600, 420);
    for (int index = 0; index < 7; ++index) {
        expect(!gate.observe(speaker_bleed.data(), speaker_bleed.size(), 0.35F),
               "speaker calibration does not interrupt playback");
    }
    expect(gate.calibrated(), "barge-in gate calibrates against playback bleed");
    std::vector<std::int16_t> interruption(1600);
    for (std::size_t index = 0; index < interruption.size(); ++index) {
        interruption[index] = static_cast<std::int16_t>(6500.0 * std::sin(static_cast<double>(index) * 0.17));
    }
    expect(!gate.observe(interruption.data(), interruption.size(), 0.35F),
           "one short sound cannot interrupt an answer");
    expect(!gate.observe(interruption.data(), interruption.size(), 0.35F),
           "barge-in requires sustained speech");
    expect(gate.observe(interruption.data(), interruption.size(), 0.35F),
           "sustained speech triggers barge-in");

    // Talking during calibration must not make a later interruption need a shout.
    BargeInGate talked_over;
    std::vector<std::int16_t> quiet_bleed(320, 300);
    std::vector<std::int16_t> loud_voice(320);
    std::vector<std::int16_t> normal_voice(320);
    for (std::size_t index = 0; index < loud_voice.size(); ++index) {
        loud_voice[index] = static_cast<std::int16_t>(6000.0 * std::sin(static_cast<double>(index) * 0.17));
        normal_voice[index] = static_cast<std::int16_t>(3000.0 * std::sin(static_cast<double>(index) * 0.17));
    }
    for (int index = 0; index < 18; ++index) talked_over.observe(quiet_bleed.data(), quiet_bleed.size(), 0.3F);
    for (int index = 0; index < 15; ++index) talked_over.observe(loud_voice.data(), loud_voice.size(), 0.3F);
    expect(talked_over.calibrated(), "calibrated after 650 ms");
    bool triggered = false;
    for (int index = 0; index < 25 && !triggered; ++index) {
        triggered = talked_over.observe(normal_voice.data(), normal_voice.size(), 0.3F);
    }
    expect(triggered, "speech during calibration does not raise the barge-in bar");

    echo_reference_gate_tests();
}

}  // namespace

void sound_tests() {
    using namespace aura;
    // A dragged volume slider is written once it rests, not on every step.
    SettingSettle settle(500);
    expect(!settle.settled("v=51", 10) && !settle.settled("v=52", 20) && !settle.settled("v=53", 400),
           "a value that keeps changing waits");
    expect(!settle.settled("v=53", 899) && settle.settled("v=53", 900), "it is written once it rests");
    const auto peak = [](const PcmClip& clip) {
        int loudest = 0;
        for (const auto sample : clip.samples) loudest = std::max(loudest, std::abs(static_cast<int>(sample)));
        return loudest;
    };
    const auto milliseconds = [](const PcmClip& clip) {
        return static_cast<std::uint32_t>(clip.samples.size() * 1000 / kSoundSampleRate);
    };

    const auto chime = make_message_chime();
    expect(chime.format.sample_rate == kSoundSampleRate && chime.format.channels == 1 &&
           chime.format.bits_per_sample == 16, "chime is 16 kHz mono PCM");
    expect(milliseconds(chime) >= 250 && milliseconds(chime) <= 600, "chime is short");
    expect(peak(chime) > 4000 && peak(chime) < 14000, "chime is audible but soft");
    expect(std::abs(chime.samples.front()) < 200 && std::abs(chime.samples.back()) < 200,
           "chime starts and ends without a click");

    const auto quiet = make_alarm_melody(alarm_loudness(0));
    const auto full = make_alarm_melody(alarm_loudness(60000));
    expect(milliseconds(full) >= 800 && milliseconds(full) + 500 <= kAlarmCycleMs,
           "one alarm pass leaves a pause in its cycle");
    expect(peak(full) > 26000 && peak(full) < 31500, "full alarm is loud without clipping");
    expect(std::abs(peak(quiet) * 10 - peak(full) * 3) < peak(full) / 10, "the alarm starts at 30 %");
    expect(peak(make_alarm_melody(4.0F)) == peak(full) && peak(make_alarm_melody(-1.0F)) == 0,
           "alarm loudness is clamped");
    expect(alarm_loudness(0) < alarm_loudness(15000) && alarm_loudness(15000) < alarm_loudness(30000) &&
           alarm_loudness(30000) == 1.0F && alarm_loudness(600000) == 1.0F, "alarm loudness rises to full in 30 s");

    // One ringer for both platforms. The melody is synthesised once; each pass
    // only scales it, so ringing does not stall the UI thread every cycle.
    const auto same_samples = [](const PcmClip& left, const PcmClip& right) {
        if (left.samples.size() != right.samples.size()) return false;
        for (std::size_t index = 0; index < left.samples.size(); ++index) {
            if (std::abs(left.samples[index] - right.samples[index]) > 1) return false;
        }
        return true;
    };
    AlarmRinger ringer;
    expect(ringer.next(false, false, false, 1000) == nullptr, "nothing rings without an alert");
    const auto* first_pass = ringer.next(true, false, false, 5000);
    expect(first_pass && same_samples(*first_pass, make_alarm_melody(alarm_loudness(0))),
           "an alert rings at once, at 30 %");
    expect(ringer.next(true, false, false, 6000) == nullptr, "the next pass waits for its cycle");
    expect(ringer.next(true, false, true, 7100) == nullptr, "never over another clip");
    const auto* louder_pass = ringer.next(true, false, false, 7200);
    expect(louder_pass && same_samples(*louder_pass, make_alarm_melody(alarm_loudness(2200))),
           "each pass is a little louder");
    expect(ringer.next(true, true, false, 20000) == nullptr, "face down silences the alert");
    expect(ringer.next(false, false, false, 21000) == nullptr, "the alert ends");
    const auto* next_alert = ringer.next(true, false, false, 22000);
    expect(next_alert && same_samples(*next_alert, make_alarm_melody(alarm_loudness(0))),
           "the next alert starts quietly again");

    // The slider and the mute switch change only the audio service: what is
    // stored follows it.
    SoundSettings owner;
    owner.message_chime = false;
    AudioDiagnostics heard;
    heard.volume = 0.2F;
    heard.muted = true;
    const auto stored = current_sound_settings(owner, heard);
    expect(std::fabs(stored.volume - 0.2F) < 0.001F && stored.muted && !stored.message_chime,
           "volume and mute are stored as the audio service holds them");
    expect(SoundSettings{} == SoundSettings{} && !(stored == owner) && stored != owner,
           "settings compare as values, so an unchanged choice is not written again");

    MessageChimeGate gate;
    expect(!gate.observe({"a1"}, 1000, false), "notifications already there at start-up do not chime");
    expect(gate.observe({"a1", "a2"}, 2000, false), "a new notification chimes");
    expect(!gate.observe({"a1", "a2"}, 9000, false), "the same notifications do not chime again");
    expect(!gate.observe({"a2"}, 10000, false), "reading a room is not a new message");
    expect(!gate.observe({"a2", "a3"}, 11000, true), "no chime while quiet");
    expect(!gate.observe({"a2", "a3"}, 16000, false), "a message seen while quiet does not chime later");
    expect(gate.observe({"a2", "a3", "p7"}, 20000, false), "a phone notification chimes too");
    expect(!gate.observe({"a2", "a3", "p7", "a4"}, 21000, false), "a burst chimes at most once every few seconds");
    expect(gate.observe({"a5"}, 30000, false), "the next message after the pause chimes again");

    SoundSettings chosen;
    chosen.volume = 0.8F;
    chosen.muted = true;
    chosen.message_chime = false;
    chosen.spoken_replies = false;
    chosen.continuous_conversation = false;
    chosen.barge_in = false;
    chosen.assistant_voice = AssistantVoice::Athina;
    const auto restored = parse_sound_settings(serialize_sound_settings(chosen));
    expect(std::fabs(restored.volume - 0.8F) < 0.01F && restored.muted && !restored.message_chime &&
           !restored.spoken_replies && !restored.continuous_conversation && !restored.barge_in &&
           restored.assistant_voice == AssistantVoice::Athina,
           "sound settings and the selected voice survive a restart");
    const SoundSettings defaults;
    const auto garbage = parse_sound_settings("aura-sound 1\nvolume=7\nmuted=yes\nchime=0\nother=1\n");
    expect(garbage.volume == defaults.volume && !garbage.muted && !garbage.message_chime && garbage.spoken_replies &&
           garbage.continuous_conversation && garbage.barge_in,
           "bad sound values keep their defaults");
    const auto foreign = parse_sound_settings("volume=0.1\n");
    expect(foreign.volume == defaults.volume && foreign.message_chime, "a file without the header is ignored");
}

// The device's main loop and processor follow the screen: full speed while it
// is lit, slower and at a lower clock once it is dark.
// The numbers the first boot on a board prints: frame and loop times, Synapse,
// internal RAM and PSRAM.
void metrics_tests() {
    using namespace aura;
    DurationStats stats;
    expect(stats.count() == 0 && stats.mean_us() == 0 && stats.max_us() == 0, "no samples, no numbers");
    stats.add(10000);
    stats.add(30000);
    stats.add(20000);
    expect(stats.count() == 3 && stats.mean_us() == 20000 && stats.max_us() == 30000, "count, mean and maximum");
    stats.reset();
    expect(stats.count() == 0 && stats.max_us() == 0, "a new window starts empty");
    RuntimeMetrics metrics;
    metrics.uptime_ms = 12345;
    metrics.frame.add(12300);
    metrics.frame.add(40000);
    // The first flash (2026-11-14): how much of a frame waits for the panel's
    // transfer, and how many pixels a frame sends.
    metrics.flush_wait.add(4000);
    metrics.flush_wait.add(6000);
    metrics.flushed_pixels = 2 * 115200;
    metrics.loop.add(1200);
    metrics.presence_frame_ms = 180;
    metrics.presence_reduced = true;
    metrics.presence_fps = 1.84;
    metrics.internal_free = 120 * 1024;
    metrics.internal_min_free = 95 * 1024;
    metrics.internal_largest = 60 * 1024;
    metrics.psram_free = 6100 * 1024;
    metrics.psram_min_free = 5900 * 1024;
    metrics.psram_largest = 4000 * 1024;
    const auto line = format_runtime_metrics(metrics);
    expect(line == "t=12.3 s | frame avg 26.1 max 40.0 ms n=2 wait 5.0 ms 115 kpx | loop avg 1.2 max 1.2 ms n=1 | "
                   "synapse 180 ms reduced 1.8 fps | internal free 120 min 95 largest 60 KiB | "
                   "psram free 6100 min 5900 largest 4000 KiB",
           "one readable line per report");
}

void power_tests() {
    using namespace aura;
    expect(host_loop_interval_ms(DisplayMode::Active) == 33 && host_loop_interval_ms(DisplayMode::Dimmed) == 33,
           "a lit screen keeps the 30 Hz loop");
    expect(host_loop_interval_ms(DisplayMode::AlwaysOn) == 100, "the always-on clock needs the loop at 10 Hz");
    expect(host_loop_interval_ms(DisplayMode::Off) == 250, "a dark screen needs the loop at 4 Hz");
    expect(needs_full_speed(DisplayMode::Active) && needs_full_speed(DisplayMode::Dimmed),
           "a lit screen keeps the processor at full speed");
    expect(!needs_full_speed(DisplayMode::AlwaysOn) && !needs_full_speed(DisplayMode::Off),
           "a dark screen lets the processor slow down");
    // Hybrid message sync (the owner's choice, 2026-11-06).
    expect(realtime_sync_allowed(DisplayMode::Active, false) && realtime_sync_allowed(DisplayMode::Dimmed, false),
           "a lit screen keeps realtime messages on battery");
    expect(!realtime_sync_allowed(DisplayMode::AlwaysOn, false) && !realtime_sync_allowed(DisplayMode::Off, false),
           "a dark screen on battery falls back to the five-minute check");
    expect(realtime_sync_allowed(DisplayMode::Off, true) && realtime_sync_allowed(DisplayMode::AlwaysOn, true),
           "on USB power realtime stays on");
}

// An alarm that wakes with a station (the owner, 2026-11-19): the alarm keeps
// its station through a restart, the ringing alert carries it, and the
// player starts it, rings the melody when it does not play, and stops it.
void alarm_station_tests() {
    using namespace aura;
    ClockServicesModel clock;
    std::uint32_t id = 0;
    expect(clock.add_alarm(7, 0, true, "", &id) == ClockResult::Ok, "an alarm");
    const AlarmStation kosmos{"uuid-kosmos", "DEMO Κόσμος", "https://example.invalid/kosmos.mp3"};
    expect(clock.snapshot().alarms.front().station.url.empty(), "the melody by default");
    expect(clock.set_alarm_station(id, kosmos) == ClockResult::Ok &&
               clock.snapshot().alarms.front().station.url == kosmos.url,
           "it can wake with a station");
    expect(clock.set_alarm_station(id + 99, kosmos) == ClockResult::NotFound, "only an alarm that exists");
    ClockServicesModel restored;
    expect(restored.restore(clock.serialize()) && restored.snapshot().alarms.size() == 1 &&
               restored.snapshot().alarms.front().station.name == kosmos.name &&
               restored.snapshot().alarms.front().station.id == kosmos.id,
           "the station survives a restart");
    ClockServicesModel older;
    expect(older.restore("aura-clock 1\nN\t5\nA\t3\t6\t30\t1\t1\tΔουλειά\n") && older.snapshot().alarms.size() == 1 &&
               older.snapshot().alarms.front().station.url.empty() && older.snapshot().alarms.front().label == "Δουλειά",
           "an older store still reads, with the melody");
    clock.set_clock(at(2026, 11, 20, 7, 0, 5));
    expect(clock.snapshot().alert && clock.snapshot().alert->station.url == kosmos.url,
           "the ringing alert carries the station");
    ClockServicesModel after_restart;
    expect(after_restart.restore(clock.serialize()), "a ringing alarm is stored");
    after_restart.set_clock(at(2026, 11, 20, 7, 1, 5));
    expect(after_restart.snapshot().alert && after_restart.snapshot().alert->station.url == kosmos.url,
           "and rings with its station after a restart");
    expect(clock.set_alarm_station(id, std::nullopt) == ClockResult::Ok &&
               clock.snapshot().alarms.front().station.url.empty(),
           "and back to the melody");

    ClockAlert ringing;
    ringing.kind = ClockAlertKind::Alarm;
    ringing.id = 7;
    ringing.station = kosmos;
    AlarmStationPlayer player;
    auto decision = player.step(ringing, RadioState::Idle, false, 1000);
    expect(decision.start_station && decision.station.url == kosmos.url && !decision.melody && !decision.stop_station,
           "the station starts and the melody waits");
    decision = player.step(ringing, RadioState::Connecting, false, 5000);
    expect(!decision.start_station && !decision.melody, "while it connects, no melody and no second start");
    decision = player.step(ringing, RadioState::Playing, false, 8000);
    expect(!decision.melody && !decision.start_station, "playing: the station is the alarm");
    decision = player.step(std::nullopt, RadioState::Playing, false, 20000);
    expect(decision.stop_station && !decision.start_station, "dismissed or snoozed: the station it started stops");
    decision = player.step(std::nullopt, RadioState::Idle, false, 21000);
    expect(!decision.stop_station && !decision.start_station, "once");

    AlarmStationPlayer late;
    late.step(ringing, RadioState::Idle, false, 0);
    expect(!late.step(ringing, RadioState::Connecting, false, AlarmStationPlayer::kStationTimeoutMs - 1).melody,
           "still connecting: still waiting");
    decision = late.step(ringing, RadioState::Connecting, false, AlarmStationPlayer::kStationTimeoutMs);
    expect(decision.melody && decision.stop_station, "not playing within the wait: stopped, and the melody rings");
    expect(late.step(ringing, RadioState::Idle, false, AlarmStationPlayer::kStationTimeoutMs + 100).melody &&
               !late.step(ringing, RadioState::Idle, false, AlarmStationPlayer::kStationTimeoutMs + 200).start_station,
           "the melody goes on, and the station is not tried again for this alert");

    AlarmStationPlayer failing;
    failing.step(ringing, RadioState::Idle, false, 0);
    expect(failing.step(ringing, RadioState::Error, false, 3000).melody, "a station that fails: the melody at once");

    AlarmStationPlayer muted;
    decision = muted.step(ringing, RadioState::Idle, true, 0);
    expect(!decision.start_station && decision.melody, "muted: the melody, which always rings");

    AlarmStationPlayer plain;
    ClockAlert melody = ringing;
    melody.station = {};
    decision = plain.step(melody, RadioState::Idle, false, 0);
    expect(!decision.start_station && decision.melody, "an alarm without a station: the melody");
    ClockAlert timer = ringing;
    timer.kind = ClockAlertKind::Timer;
    AlarmStationPlayer timers;
    expect(!timers.step(timer, RadioState::Idle, false, 0).start_station, "a timer never starts the radio");
}

// Xweather (the owner's keys, 2026-11-19): rain in the next hour minute by
// minute, official alerts and lightning near, merged into the weather; checks
// only when they can matter, and one notice per rain, storm or alert.
void xweather_tests() {
    using namespace aura;
    expect(xweather_minutely_path(37.98, 23.73) == "conditions/37.980,23.730?filter=minutelyprecip&plimit=60" &&
               xweather_alerts_path(37.98, 23.73) == "alerts/37.980,23.730?limit=10" &&
               xweather_lightning_path(37.98, 23.73) ==
                   "lightning/closest?p=37.980,23.730&radius=50km&from=-15minutes&to=now&limit=1&sort=dt:-1",
           "the three requests, the place to three decimals");
    expect(xweather_url("forecasts/1,2?filter=1hr", "id1", "s2") ==
               "https://data.api.xweather.com/forecasts/1,2?filter=1hr&client_id=id1&client_secret=s2" &&
               xweather_url("alerts/1,2", "id1", "s2") == "https://data.api.xweather.com/alerts/1,2?client_id=id1&client_secret=s2",
           "the keys ride as query parameters");

    // Rain in 7 minutes for 3 minutes, peaking at 3.2 mm/h.
    std::string periods;
    for (int minute = 0; minute < 60; ++minute) {
        const char* rate = minute == 7 ? "0.4" : minute == 8 ? "3.2" : minute == 9 ? "1.1" : "0";
        if (!periods.empty()) periods += ",";
        periods += std::string("{\"timestamp\":") + std::to_string(1795123800 + minute * 60) +
                   ",\"precipRateMM\":" + rate + ",\"precipMM\":0}";
    }
    XweatherNowcast now;
    expect(parse_xweather_minutely("{\"success\":true,\"error\":null,\"response\":[{\"periods\":[" + periods + "]}]}", now) &&
               now.rain_ok && now.rain_in_minutes == 7 && now.rain_minutes == 3 && now.rain_peak_mm_h > 3.1F,
           "rain in seven minutes, for three, at most 3.2 mm/h");
    XweatherNowcast dry;
    expect(parse_xweather_minutely("{\"success\":true,\"response\":[{\"periods\":[{\"precipRateMM\":0}]}]}", dry) &&
               dry.rain_ok && dry.rain_in_minutes == -1,
           "no rain in the hour");
    XweatherNowcast broken;
    expect(!parse_xweather_minutely("{\"success\":false,\"error\":{\"code\":\"invalid_client\"}}", broken) &&
               !broken.rain_ok && !parse_xweather_minutely("not json", broken),
           "an error or a broken answer is no nowcast");

    expect(parse_xweather_alerts("{\"success\":true,\"error\":{\"code\":\"warn_no_data\"},\"response\":[]}", now) &&
               now.alerts_ok && now.alert_count == 0 && now.highest == WeatherAlertLevel::None,
           "no alerts here");
    expect(parse_xweather_alerts(
               "{\"success\":true,\"response\":[{\"details\":{\"type\":\"AW.RA.MN\",\"name\":\"DEMO RAIN\"}},"
               "{\"details\":{\"type\":\"AW.TS.SV\",\"name\":\"DEMO THUNDERSTORMS\"}}]}", now) &&
               now.alert_count == 2 && now.highest == WeatherAlertLevel::Severe && now.alert_name == "DEMO THUNDERSTORMS",
           "two alerts here, the worst severe, with its name");

    expect(parse_xweather_lightning(
               "{\"success\":true,\"response\":[{\"ob\":{\"timestamp\":1795123500},"
               "\"relativeTo\":{\"distanceKM\":12.4,\"bearing\":225}}]}", 1795123800, now) &&
               now.lightning_ok && now.lightning_near && now.lightning_km > 12.3F && now.lightning_age_minutes == 5 &&
               now.lightning_bearing_deg == 225,
           "a strike 12 km south-west five minutes ago");
    XweatherNowcast calm;
    expect(parse_xweather_lightning("{\"success\":true,\"error\":{\"code\":\"warn_no_data\"},\"response\":[]}",
                                    1795123800, calm) && calm.lightning_ok && !calm.lightning_near,
           "no lightning near");

    WeatherData data;
    data.condition = WeatherCondition::Cloudy;
    apply_xweather(data, now);
    expect(data.hazards.xweather_available && data.hazards.nowcast_available && data.hazards.rain_in_minutes == 7 &&
               data.hazards.alerts_available && data.hazards.active_alert_count == 2 &&
               data.hazards.highest_alert == WeatherAlertLevel::Severe && data.hazards.lightning_observed &&
               data.condition == WeatherCondition::Thunderstorm,
           "merged: a storm within 15 km shows as one");
    WeatherData raining;
    raining.condition = WeatherCondition::Cloudy;
    XweatherNowcast falling;
    falling.rain_ok = true;
    falling.rain_in_minutes = 0;
    falling.rain_minutes = 30;
    falling.rain_peak_mm_h = 4.0F;
    apply_xweather(raining, falling);
    expect(raining.condition == WeatherCondition::Rain, "rain falling now shows as rain");

    WeatherData plain;
    plain.hourly_count = 3;
    for (std::size_t index = 0; index < 3; ++index) plain.hourly[index].rain_probability_percent = 5;
    plain.condition = WeatherCondition::Clear;
    expect(!xweather_rain_plausible(plain) && !xweather_storm_plausible(plain), "a dry hour asks nothing");
    plain.hourly[2].rain_probability_percent = 40;
    expect(xweather_rain_plausible(plain) && !xweather_storm_plausible(plain), "a likely shower asks for rain");
    plain.hourly[1].condition = WeatherCondition::Thunderstorm;
    expect(xweather_storm_plausible(plain), "a likely storm asks for lightning too");

    WeatherNoticeGate gate;
    WeatherHazards hazards;
    hazards.nowcast_available = true;
    hazards.rain_in_minutes = 20;
    auto notice = gate.observe(hazards, 1000);
    expect(notice && notice->kind == WeatherNotice::Kind::Rain && notice->minutes == 20, "rain in twenty minutes: a notice");
    hazards.rain_in_minutes = 10;
    expect(!gate.observe(hazards, 1010), "the same rain: no second notice");
    hazards.rain_in_minutes = -1;
    gate.observe(hazards, 1100);
    hazards.rain_in_minutes = 5;
    expect(gate.observe(hazards, 1200).has_value(), "a new rain after a dry spell: a new notice");
    hazards.lightning_available = hazards.lightning_observed = true;
    hazards.nearest_lightning_km = 8.0F;
    notice = gate.observe(hazards, 1210);
    expect(notice && notice->kind == WeatherNotice::Kind::Storm && notice->km < 9.0F, "a storm near: a notice");
    expect(!gate.observe(hazards, 1230), "not again within the hour");
    hazards.alerts_available = true;
    hazards.active_alert_count = 1;
    hazards.highest_alert = WeatherAlertLevel::Moderate;
    hazards.alert_name = "DEMO ALERT";
    notice = gate.observe(hazards, 1240);
    expect(notice && notice->kind == WeatherNotice::Kind::Alert && notice->name == "DEMO ALERT", "an alert: a notice");
    expect(!gate.observe(hazards, 1250), "once per alert");
    hazards.highest_alert = WeatherAlertLevel::Minor;
    hazards.alert_name = "DEMO MINOR";
    expect(!gate.observe(hazards, 1300) || gate.observe(hazards, 1300)->kind != WeatherNotice::Kind::Alert,
           "a minor alert is no notice");

    // Which requests are due: alerts each hour; rain each 15 minutes when
    // plausible; lightning each 15 minutes when a storm is.
    XweatherScheduler scheduler;
    WeatherData quiet;
    quiet.condition = WeatherCondition::Clear;
    auto due = scheduler.due(quiet, 0);
    expect(due.alerts && !due.rain && !due.lightning, "a dry, calm hour: alerts only");
    due = scheduler.due(quiet, XweatherScheduler::kAlertsEveryMs - 1);
    expect(!due.alerts && !due.rain && !due.lightning, "and not again within the hour");
    quiet.condition = WeatherCondition::Thunderstorm;
    due = scheduler.due(quiet, XweatherScheduler::kAlertsEveryMs);
    expect(due.alerts && due.rain && due.lightning, "a storm: all three");
    due = scheduler.due(quiet, XweatherScheduler::kAlertsEveryMs + XweatherScheduler::kRainEveryMs - 1);
    expect(!due.rain && !due.lightning, "not again within fifteen minutes");
    due = scheduler.due(quiet, XweatherScheduler::kAlertsEveryMs + XweatherScheduler::kRainEveryMs);
    expect(due.rain && due.lightning && !due.alerts, "then rain and lightning again");
    scheduler.reset();
    expect(scheduler.due(quiet, XweatherScheduler::kAlertsEveryMs + XweatherScheduler::kRainEveryMs + 1).alerts,
           "a new place asks at once");

    WeatherData with_rain;
    with_rain.hazards.nowcast_available = true;
    with_rain.hazards.rain_in_minutes = 61;
    expect(!with_rain.valid(), "rain beyond the hour is not valid");
}

// How long the battery lasts (the owner, 2026-11-19): a sample every five
// minutes, the drain since the USB went out, the time left at that rate, and
// the card's battery-log.csv.
void battery_history_tests() {
    using namespace aura;
    constexpr std::uint64_t kMinute = 60000;
    BatteryHistory history;
    const auto reading = [](std::uint64_t at_ms, int percent, bool usb) {
        BatterySample sample;
        sample.at_ms = at_ms;
        sample.unix_seconds = 1795125600 + static_cast<std::int64_t>(at_ms / 1000);  // 2026-11-20 00:00 local
        sample.percent = percent;
        sample.battery_mv = 3700 + percent * 5;
        sample.external_power = usb;
        sample.charging = usb && percent < 100;
        sample.screen_on = true;
        sample.wifi_on = true;
        return sample;
    };
    expect(history.add(reading(0, 100, true)), "the first reading is kept");
    expect(!history.add(reading(5000, 100, true)), "readings within five minutes are not");
    expect(!history.drain_percent_per_hour() && !history.minutes_left(), "on USB: no estimate");
    expect(history.add(reading(6000, 100, false)), "the USB going out is kept at once");
    // 6 % an hour: one percent every ten minutes.
    for (std::uint64_t minute = 5; minute <= 25; minute += 5) history.add(reading(6000 + minute * kMinute, 100 - static_cast<int>(minute / 10), false));
    expect(!history.drain_percent_per_hour(), "under half an hour on battery: still measuring");
    expect(history.minutes_on_battery() == 25, "and it says for how long it has measured");
    for (std::uint64_t minute = 30; minute <= 60; minute += 5) history.add(reading(6000 + minute * kMinute, 100 - static_cast<int>(minute / 10), false));
    const auto drain = history.drain_percent_per_hour();
    expect(drain && *drain > 5.0F && *drain < 7.0F, "about 6 % an hour after an hour");
    const auto left = history.minutes_left();
    expect(left && *left > 840 && *left < 1100, "94 % at 6 %/h: about fifteen and a half hours left");
    expect(history.add(reading(6000 + 61 * kMinute, 94, true)), "the USB coming back is kept at once");
    expect(!history.drain_percent_per_hour() && history.minutes_on_battery() == -1, "and ends the estimate");
    const auto csv = history.csv();
    expect(csv.rfind("time_utc,percent,battery_mv,usb,charging,screen,bluetooth,wifi\n", 0) == 0,
           "the log starts with its columns");
    expect(csv.find("\n2026-11-19T22:00:00Z,100,4200,1,0,1,0,1\n") != std::string::npos,
           "one line a sample, in UTC");
    for (int sample = 0; sample < 400; ++sample) history.add(reading((200 + sample * 5) * kMinute, 50, false));
    expect(history.samples().size() == BatteryHistory::kMaxSamples, "a day of samples at most");
}

// The owner's first discharge was lost (2026-11-20): the log lives in RAM and
// starts again at every boot, so the first write after the battery ran out
// replaced it. The first write of a boot now keeps the old log as
// battery-log-previous.csv.
void battery_log_keep_tests() {
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "aura_battery_log_keep_test";
    fs::remove_all(folder);
    fs::create_directories(folder);
    const auto put = [&](const char* name, const char* text) { std::ofstream(folder / name, std::ios::binary) << text; };
    const auto get = [&](const char* name) {
        std::ifstream file(folder / name, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    const std::string directory = folder.string();
    expect(std::string(aura::kBatteryLogFile) == "battery-log.csv" &&
               std::string(aura::kBatteryLogPreviousFile) == "battery-log-previous.csv",
           "the two logs' names");

    put("battery-log.csv", "the discharge\n");
    put("battery-log-previous.csv", "an older one\n");
    expect(aura::keep_previous_battery_log(directory), "a log on the card is kept");
    expect(get("battery-log-previous.csv") == "the discharge\n", "as the previous one, replacing an older one");
    expect(!fs::exists(folder / "battery-log.csv"), "and the new boot's log starts empty");

    expect(!aura::keep_previous_battery_log(directory), "with no log yet, nothing to keep");
    expect(get("battery-log-previous.csv") == "the discharge\n", "and the previous one stays");

    put("battery-log.csv.bak", "a write cut short\n");
    expect(aura::keep_previous_battery_log(directory), "a write cut short left only its backup");
    expect(get("battery-log-previous.csv") == "a write cut short\n" && !fs::exists(folder / "battery-log.csv.bak"),
           "which is kept the same way");

    fs::remove_all(folder);
    fs::create_directories(folder);
    expect(!aura::keep_previous_battery_log(directory) && fs::is_empty(folder), "an empty card stays empty");
    fs::remove_all(folder);
}

// «Αύρα» without a touch (the owner, 2026-11-19; the first model, 2026-11-20):
// the device does what microWakeWord's own test did, so that test's cutoffs
// (tools/wakeword/README.md) hold on the device.
void wake_word_tests() {
    using namespace aura;
    // Off until the owner turns it on: an open microphone, and its battery.
    const WakeWordSettings defaults;
    expect(!defaults.enabled && defaults.sensitivity == WakeWordSensitivity::Normal, "off, normal sensitivity");
    for (const auto sensitivity : {WakeWordSensitivity::Low, WakeWordSensitivity::Normal, WakeWordSensitivity::High}) {
        WakeWordSettings settings;
        settings.enabled = true;
        settings.sensitivity = sensitivity;
        const auto back = parse_wake_word_settings(serialize_wake_word_settings(settings));
        expect(back.enabled && back.sensitivity == sensitivity, "the settings round-trip");
    }
    const auto garbage = parse_wake_word_settings("enabled=1\nsensitivity=loud\n");
    expect(!garbage.enabled && garbage.sensitivity == WakeWordSensitivity::Normal, "a text that is not ours: defaults");
    const auto odd = parse_wake_word_settings("aura-wake-word 1\nenabled=1\nsensitivity=loud\n");
    expect(odd.enabled && odd.sensitivity == WakeWordSensitivity::Normal, "an unknown sensitivity: normal");

    // The final test's table for Low 0.99 and Normal 0.98. High is 0.75 (the
    // owner's choice, 2026-11-21): the owner's tries from a distance came to
    // 0.58-0.85 and woke at none of the old 0.96.
    expect(wake_word_cutoff(WakeWordSensitivity::Low) == 0.99F && wake_word_cutoff(WakeWordSensitivity::Normal) == 0.98F &&
               wake_word_cutoff(WakeWordSensitivity::High) == 0.75F,
           "each sensitivity's cutoff");

    // Features as in training (x 0.0390625), then the input tensor's own
    // quantization, cut toward zero as numpy's astype(int8) cuts.
    constexpr float kScale = 0.10196078568696976F;
    constexpr int kZero = -128;
    expect(quantize_wake_feature(0, kScale, kZero) == -128, "silence is the lowest input");
    expect(quantize_wake_feature(100, kScale, kZero) == -89, "100: 3.906/0.102 = 38.3, -89.7 cut toward zero");
    expect(quantize_wake_feature(400, kScale, kZero) == 25, "400: 25.25 cut to 25");
    expect(quantize_wake_feature(667, kScale, kZero) == 127 && quantize_wake_feature(65535, kScale, kZero) == 127,
           "the loudest clamp to 127");

    // The detector: the mean of the last five outputs over the cutoff, after a
    // cooldown of 75 means at the start and after each detection.
    WakeWordDetector detector;
    detector.set_cutoff(0.98F);
    int first = 0;
    for (int push = 1; push <= 200 && first == 0; ++push) {
        if (detector.push(255)) first = push;
    }
    expect(first == 79, "sure from the start: heard at the 75th mean, the 79th output");
    int second = 0;
    for (int push = 80; push <= 300 && second == 0; ++push) {
        if (detector.push(255)) second = push;
    }
    expect(second == 154, "and once more only after another 75 means");
    expect(detector.take_peak() > 0.99F && detector.take_peak() == 0.0F, "the peak mean, once");

    detector.reset();
    detector.set_cutoff(200.0F / 255.0F);
    bool heard = false;
    for (int push = 0; push < 120; ++push) heard = detector.push(200) || heard;
    expect(!heard, "a mean exactly at the cutoff is not over it");
    expect(detector.push(205), "a little over it is");

    detector.reset();
    detector.set_cutoff(0.98F);
    heard = false;
    for (int push = 0; push < 300; ++push) heard = detector.push(push % 5 == 0 ? 255 : 10) || heard;
    expect(!heard, "one sure output among unsure ones is not a word");
    detector.reset();
    for (int push = 0; push < 78; ++push) heard = detector.push(255) || heard;
    expect(!heard, "after a reset the cooldown starts again");

    // A try that came near without waking, for the device's log (the owner,
    // 2026-11-20, had to shout from a metre): a rise of the mean over 0.5 that
    // falls back under 0.2 without a word gives its highest mean, once.
    detector.reset();
    detector.set_cutoff(0.98F);
    for (int push = 0; push < 100; ++push) detector.push(0);
    expect(detector.take_near_miss() == 0.0F, "quiet: nothing came near");
    for (int push = 0; push < 10; ++push) detector.push(200);
    expect(detector.take_near_miss() == 0.0F, "a rise still under way is not yet a miss");
    for (int push = 0; push < 10; ++push) detector.push(0);
    const float miss = detector.take_near_miss();
    expect(miss > 0.78F && miss < 0.79F, "it fell back without a word: its highest mean (200/255)");
    expect(detector.take_near_miss() == 0.0F, "once");
    for (int push = 0; push < 10; ++push) detector.push(100);
    for (int push = 0; push < 10; ++push) detector.push(0);
    expect(detector.take_near_miss() == 0.0F, "a rise under 0.5 is not near");
    heard = false;
    for (int push = 0; push < 10; ++push) heard = detector.push(255) || heard;
    for (int push = 0; push < 10; ++push) detector.push(0);
    expect(heard && detector.take_near_miss() == 0.0F, "a word, and its tail, are not a miss");
    for (int push = 0; push < 10; ++push) detector.push(150);
    for (int push = 0; push < 10; ++push) detector.push(0);
    const float after = detector.take_near_miss();
    expect(after > 0.58F && after < 0.59F, "the next near try counts again (150/255)");
    // A new listen starts clean: a rise under way when the last one stopped is
    // not a miss of the next.
    for (int push = 0; push < 10; ++push) detector.push(200);
    detector.reset();
    for (int push = 0; push < 10; ++push) detector.push(0);
    expect(detector.take_near_miss() == 0.0F, "a reset forgets a rise under way");
}

// The screen turns with the device (the owner's request on the first flash,
// 2026-11-14): the edge nearest the ground becomes the bottom.
void orientation_tests() {
    using namespace aura;
    OrientationTracker tracker;
    std::uint32_t t = 0;
    int changes = 0;
    auto hold = [&](float ax, float ay, float az, std::uint32_t ms) {
        for (std::uint32_t spent = 0; spent < ms; spent += 20) {
            t += 20;
            if (tracker.push({ax, ay, az, t})) ++changes;
        }
    };
    expect(tracker.rotation() == ScreenRotation::Bottom, "the screen starts upright");
    hold(0.0F, 1.0F, 0.0F, 1000);
    expect(tracker.rotation() == ScreenRotation::Bottom && changes == 0, "standing upright keeps it upright");

    // Turned clockwise: the right edge is down, "up" points to the left edge.
    hold(-1.0F, 0.0F, 0.0F, 400);
    expect(tracker.rotation() == ScreenRotation::Bottom, "a turn must settle before the screen follows");
    hold(-1.0F, 0.0F, 0.0F, 400);
    expect(tracker.rotation() == ScreenRotation::Right && changes == 1, "then the right edge is the bottom");

    // Near the diagonal the screen holds; well past it, it follows.
    const float rad = 3.14159265F / 180.0F;
    hold(-std::sin(40.0F * rad), std::cos(40.0F * rad), 0.0F, 2000);
    expect(tracker.rotation() == ScreenRotation::Right, "5 degrees past the diagonal is not a turn");
    hold(-std::sin(20.0F * rad), std::cos(20.0F * rad), 0.0F, 1000);
    expect(tracker.rotation() == ScreenRotation::Bottom && changes == 2, "well past it the screen turns back");

    // Lying flat, or shaken, the screen keeps its rotation.
    hold(-1.0F, 0.0F, 0.0F, 1000);
    expect(tracker.rotation() == ScreenRotation::Right && changes == 3, "turned again");
    hold(0.05F, 0.1F, 1.0F, 2000);
    expect(tracker.rotation() == ScreenRotation::Right, "lying flat keeps the last rotation");
    hold(0.0F, -2.0F, 0.3F, 1000);
    expect(tracker.rotation() == ScreenRotation::Right, "a shake is not a turn");

    // Upside down, and the left edge down.
    hold(0.0F, -1.0F, 0.0F, 1000);
    expect(tracker.rotation() == ScreenRotation::Top, "the top edge down");
    hold(1.0F, 0.0F, 0.0F, 1000);
    expect(tracker.rotation() == ScreenRotation::Left, "the left edge down");

    // Locked, it never turns; unlocked, it follows again.
    tracker.set_locked(true);
    hold(0.0F, 1.0F, 0.0F, 2000);
    expect(tracker.rotation() == ScreenRotation::Left, "a locked screen keeps its rotation");
    tracker.set_locked(false);
    hold(0.0F, 1.0F, 0.0F, 1000);
    expect(tracker.rotation() == ScreenRotation::Bottom, "unlocked, it follows the device again");

    // A touch on the panel lands on the content drawn under it.
    auto touched = [](ScreenRotation rotation, int x, int y) {
        rotate_touch_point(rotation, 480, x, y);
        return std::make_pair(x, y);
    };
    expect(touched(ScreenRotation::Bottom, 10, 20) == std::make_pair(10, 20), "upright: unchanged");
    expect(touched(ScreenRotation::Top, 10, 20) == std::make_pair(469, 459), "upside down: both reversed");
    // Right edge down: the content's top runs along the panel's left edge.
    expect(touched(ScreenRotation::Right, 0, 0) == std::make_pair(479, 0), "right: the panel's corner");
    expect(touched(ScreenRotation::Right, 470, 100) == std::make_pair(379, 470), "right: the content's bottom");
    // Left edge down: the content's top runs along the panel's right edge.
    expect(touched(ScreenRotation::Left, 0, 0) == std::make_pair(0, 479), "left: the panel's corner");
    expect(touched(ScreenRotation::Left, 10, 100) == std::make_pair(100, 469), "left: the content's bottom");

    // The lock is a display setting and survives a restart.
    DisplaySettings locked;
    locked.rotation_locked = true;
    expect(parse_display_settings(serialize_display_settings(locked)).rotation_locked &&
               !parse_display_settings(serialize_display_settings(DisplaySettings{})).rotation_locked,
           "the rotation lock round-trips");
}

int main() {
    orientation_tests();
    sound_tests();
    power_tests();
    battery_history_tests();
    battery_log_keep_tests();
    wake_word_tests();
    alarm_station_tests();
    xweather_tests();
    metrics_tests();
    text_tests();
    clock_tests();
    notes_tests();
    assistant_tests();
    briefing_tests();
    motion_tests();
    phone_tests();
    bluetooth_tests();
    ota_tests();
    voice_tests();
    std::cout << "companion_models_test: PASS\n";
    return 0;
}

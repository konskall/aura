#include "aura/assistant_companion.hpp"
#include "aura/briefing.hpp"
#include "aura/clock_services.hpp"
#include "aura/display_power.hpp"
#include "aura/mock_provider.hpp"
#include "aura/notes.hpp"
#include "aura/ota_update.hpp"
#include "aura/phone_link.hpp"
#include "aura/sounds.hpp"
#include "aura/text_util.hpp"
#include "aura/translation.hpp"
#include "aura/voice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
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
    parsed = parse_assistant_answer("ACTION: {\"type\":\"volume\",\"value\":40}");
    expect(answer_display_text(parsed, "Να το κάνω;", "Δεν έγινε τίποτα.") == "Δεν έγινε τίποτα.",
           "the raw ACTION line is never shown");
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

void translation_tests() {
    using namespace aura;
    TranslationModel model;
    const auto id = model.begin(TranslateSide::Mine);
    model.set_source(id, "Πού είναι ο σταθμός;");
    model.complete(id, "Where is the station?");
    expect(model.find(id)->status == TranslationStatus::Ready, "a translation completes");
    expect(model.source_language(TranslateSide::Theirs) == "en" && model.target_language(TranslateSide::Theirs) == "el",
           "the other side speaks the other language");
    expect(!model.set_their_language("xx") && model.set_their_language("de") && model.snapshot().entries.empty(),
           "changing the language starts a new conversation");
    for (int index = 0; index < 9; ++index) model.begin(TranslateSide::Theirs);
    expect(model.snapshot().entries.size() == TranslationModel::kMaxEntries, "translation history is bounded");
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
    ImuGesture seen = ImuGesture::None;
    feed(0.0F, -1.0F, 0.0F, 25, &seen);  // hanging at the side
    for (int step = 0; step < 10; ++step) {  // lift and turn towards the face
        const float angle = static_cast<float>(step) / 10.0F * 1.1F;
        t += 20;
        const auto gesture = detector.push({0.25F, -std::cos(angle) + 0.3F, std::sin(angle) + 0.2F, t});
        if (gesture != ImuGesture::None && seen == ImuGesture::None) seen = gesture;
    }
    feed(0.0F, -0.45F, 0.89F, 20, &seen);  // settled, screen towards the viewer
    expect(seen == ImuGesture::Raise, "lifting the device is a raise");
    seen = ImuGesture::None;
    feed(0.0F, -0.45F, 0.89F, 50, &seen);
    expect(seen == ImuGesture::None, "holding still is not another raise");
    feed(0.0F, 0.0F, 1.0F, 30, &seen);  // flat on a table
    expect(seen == ImuGesture::None, "lying flat does nothing");
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
    expect(display.snapshot().mode == DisplayMode::Dimmed && display.snapshot().brightness_percent == 30, "dims");
    display.tick(1500);
    expect(display.snapshot().mode == DisplayMode::AlwaysOn, "then the always-on clock");
    expect(display.snapshot().brightness_percent == 12, "the always-on clock at full brightness setting");

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

void ota_tests() {
    using namespace aura;
    expect(compare_versions("1.10.0", "1.9.9") > 0 && compare_versions("1.0.0", "1.0.0") == 0 &&
           compare_versions("bad", "1.0.0") < 0 && !version_valid("1.0") && version_valid("13.0.0"), "versions");
    const std::string hash(64, 'a');
    std::string error;
    auto manifest = parse_update_manifest(
        "{\"version\":\"13.1.0\",\"url\":\"https://updates.example/aura.bin\",\"size\":1234567,\"sha256\":\"" + hash +
        "\",\"notes\":\"Νέα\"}", &error);
    expect(manifest && manifest->size_bytes == 1234567 && manifest->notes == "Νέα", "a valid manifest");
    expect(!parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"http://x/a.bin\",\"size\":1,\"sha256\":\"" + hash +
                                  "\"}", &error) && error == "MANIFEST_BAD_URL", "plain HTTP is refused");
    expect(!parse_update_manifest("{\"version\":\"13.1.0\",\"url\":\"https://x/a.bin\",\"size\":99999999,\"sha256\":\"" +
                                  hash + "\"}", &error) && error == "MANIFEST_BAD_SIZE", "oversized images are refused");

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

int main() {
    sound_tests();
    text_tests();
    clock_tests();
    notes_tests();
    assistant_tests();
    briefing_tests();
    translation_tests();
    motion_tests();
    phone_tests();
    ota_tests();
    voice_tests();
    std::cout << "companion_models_test: PASS\n";
    return 0;
}

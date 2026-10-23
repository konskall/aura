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
#include <cstdlib>
#include <iostream>
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
    const auto now = minute_index(at(2026, 10, 22, 19, 0));
    expect(reminder_due_minute(*parsed.action, now) == minute_index(at(2026, 10, 23, 18, 0)), "tomorrow 18:00");
    AssistantAction today;
    today.hour = 18;
    expect(reminder_due_minute(today, now) == minute_index(at(2026, 10, 23, 18, 0)), "a passed time rolls to tomorrow");
    parsed = parse_assistant_answer("plain answer");
    expect(!parsed.action && !parsed.action_rejected && parsed.text == "plain answer", "plain answers pass through");

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

    TurnRequest request;
    request.prompt = "Βάλε χρονόμετρο";
    const auto demo = mock_answer(request);
    expect(demo.ok && parse_assistant_answer(demo.text).action, "the demo provider can propose an action");
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
    expect(briefing.period == BriefingPeriod::Morning && briefing.date_valid && briefing.items.size() == 3,
           "the morning briefing gathers alarm, reminders and messages");
    expect(briefing.items[0].kind == BriefingItemKind::Alarm && briefing.items[1].count == 1 &&
           briefing.items[2].count == 2, "briefing order");
    const auto today = days_from_civil(2026, 10, 22);
    expect(should_auto_show_briefing(status.snapshot(), today - 1) && !should_auto_show_briefing(status.snapshot(), today),
           "the briefing opens once a morning");
    status.set_clock(13, 0, StatusOrigin::Live);
    expect(!should_auto_show_briefing(status.snapshot(), today - 1), "not in the afternoon");
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
    const std::uint8_t garbage[48]{};
    expect(!parse_wav(garbage, sizeof(garbage), clip), "garbage is not a WAV");
}

}  // namespace

void sound_tests() {
    using namespace aura;
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

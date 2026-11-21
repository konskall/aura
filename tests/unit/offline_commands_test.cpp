#include "aura/offline_commands.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "offline command test failed: " << message << '\n';
    std::exit(1);
}
}

int main() {
    using namespace aura;
    expect(parse_offline_command("Τι ώρα είναι;").kind == OfflineCommandKind::ShowTime, "Greek time");
    expect(parse_offline_command("What is the battery level?").kind == OfflineCommandKind::ShowBattery,
           "English battery");
    expect(parse_offline_command("Άνοιξε τη λίστα αγορών").kind == OfflineCommandKind::OpenShopping,
           "open shopping");
    const auto timer = parse_offline_command("Βάλε χρονόμετρο 10 λεπτά");
    expect(timer.has_action && timer.action.type == AssistantActionType::Timer && timer.action.seconds == 600,
           "bounded timer");
    const auto alarm = parse_offline_command("Βάλε ξυπνητήρι 07:30 κάθε μέρα");
    expect(alarm.has_action && alarm.action.type == AssistantActionType::Alarm && alarm.action.hour == 7 &&
           alarm.action.minute == 30 && alarm.action.daily, "daily alarm");
    const auto shopping = parse_offline_command("Αγόρασε γάλα");
    expect(shopping.has_action && shopping.action.type == AssistantActionType::Shopping &&
           shopping.action.text == "γάλα", "shopping action");
    const auto volume = parse_offline_command("Βάλε ένταση 55");
    expect(volume.has_action && volume.action.type == AssistantActionType::Volume &&
           volume.action.value == 55, "confirmed volume");
    const auto brightness = parse_offline_command("Βάλε φωτεινότητα 70");
    expect(brightness.has_action && brightness.action.type == AssistantActionType::Brightness &&
           brightness.action.value == 70, "confirmed brightness");
    const auto mute = parse_offline_command("σίγαση");
    expect(mute.has_action && mute.action.type == AssistantActionType::Mute && mute.action.value == 1,
           "confirmed mute");
    expect(parse_offline_command("μίλησέ μου για την ιστορία").kind == OfflineCommandKind::None,
           "ambiguous falls through");

    // Timer lengths: digits win, and number words count only when whole.
    const auto timer_seconds = [](const char* text) {
        const auto command = parse_offline_command(text);
        return command.has_action && command.action.type == AssistantActionType::Timer ? command.action.seconds : -1;
    };
    expect(timer_seconds("Βάλε χρονόμετρο τριάντα λεπτά") == 1800, "τριάντα is 30, not τρία");
    expect(timer_seconds("Βάλε χρονόμετρο δεκαπέντε λεπτά") == 900, "δεκαπέντε is 15, not πέντε");
    expect(timer_seconds("Βάλε χρονόμετρο 20 λεπτά για εμένα") == 1200, "digits win over 'ένα' inside 'εμένα'");
    expect(timer_seconds("Set a timer for 10 minutes for the phone") == 600, "'phone' holds no 'one'");
    expect(timer_seconds("Βάλε χρονόμετρο είκοσι πέντε λεπτά") == 1500, "compound Greek numbers add up");
    expect(timer_seconds("Βάλε χρονόμετρο μισή ώρα") == 1800, "half an hour");
    expect(timer_seconds("Βάλε χρονόμετρο 10 λεπτά, ωραία") == 600, "'ωραία' is not an hour");
    expect(timer_seconds("Start a timer for two hours") == 7200, "English hours");

    // Local answers only for the plain question, not every sentence with its words.
    expect(parse_offline_command("Τι ώρα ξεκινά η ταινία;").kind == OfflineCommandKind::None,
           "a film's start time goes to the assistant");
    expect(parse_offline_command("Πόση ώρα θέλει το αυγό;").kind == OfflineCommandKind::None,
           "a cooking time goes to the assistant");
    expect(parse_offline_command("Τι ώρα είναι στη Νέα Υόρκη;").kind == OfflineCommandKind::None,
           "another city's time goes to the assistant");
    expect(parse_offline_command("Αύρα, τι ώρα είναι τώρα;").kind == OfflineCommandKind::ShowTime,
           "the local time with filler words");
    expect(parse_offline_command("What time is it?").kind == OfflineCommandKind::ShowTime, "English time");
    expect(parse_offline_command("Πόση μπαταρία έχει το κινητό μου;").kind == OfflineCommandKind::None,
           "the phone's battery is not the device's");

    // Settings change only on a request, never on a sentence that mentions them.
    expect(!parse_offline_command("Τι κάνει η σίγαση;").has_action, "a question about mute does not mute");
    expect(!parse_offline_command("Είδα μια ταινία χωρίς ήχο").has_action, "a story without sound does not mute");
    const auto polite_mute = parse_offline_command("Κάνε σίγαση παρακαλώ");
    expect(polite_mute.has_action && polite_mute.action.type == AssistantActionType::Mute &&
               polite_mute.action.value == 1, "a mute request mutes");
    const auto unmute = parse_offline_command("Βγάλε τη σίγαση");
    expect(unmute.has_action && unmute.action.type == AssistantActionType::Mute && unmute.action.value == 0,
           "unmute");
    expect(!parse_offline_command("Ποια ήταν η ένταση του σεισμού 5 ρίχτερ;").has_action,
           "an earthquake's magnitude is not a volume");
    const auto bare_volume = parse_offline_command("ένταση στο 40");
    expect(bare_volume.has_action && bare_volume.action.type == AssistantActionType::Volume &&
               bare_volume.action.value == 40, "a bare volume request");
    expect(!parse_offline_command("buy me a coffee").has_action, "'buy me a coffee' is not a shopping item");
    const auto buy = parse_offline_command("buy milk");
    expect(buy.has_action && buy.action.type == AssistantActionType::Shopping && buy.action.text == "milk",
           "an English shopping item");
    expect(parse_offline_command("Show me the shower settings").kind == OfflineCommandKind::None,
           "'shower' is not a show command target");

    // Greek past tense is written like a command; a question is never one.
    expect(!parse_offline_command("Σημείωσε γκολ ο Ολυμπιακός;").has_action, "a football question is not a note");
    expect(!parse_offline_command("Αγόρασε η Μαρία τα εισιτήρια;").has_action,
           "a question about Maria is not a shopping item");
    expect(!parse_offline_command("Buy milk?").has_action, "an English question is not an item either");
    const auto dictated = parse_offline_command("Σημείωσε να πάρω γάλα");
    expect(dictated.has_action && dictated.action.type == AssistantActionType::Note &&
           dictated.action.text == "να πάρω γάλα", "a dictated note still works");

    // Louder and quieter without a number change the volume by a step.
    const auto relative_volume = [](const char* text) {
        const auto command = parse_offline_command(text);
        return command.has_action && command.action.type == AssistantActionType::Volume && command.action.relative ?
            command.action.value : 0;
    };
    expect(relative_volume("Δυνάμωσε την ένταση") == 10, "turn it up");
    expect(relative_volume("Χαμήλωσε λίγο τον ήχο") == -10, "turn it down");
    expect(relative_volume("πιο δυνατά") == 10 && relative_volume("Πιο σιγά παρακαλώ") == -10, "louder, quieter");
    expect(relative_volume("Turn the volume up") == 10 && relative_volume("lower the volume") == -10 &&
           relative_volume("louder") == 10, "English steps");
    expect(!parse_offline_command("Χαμήλωσε τη θερμοκρασία").has_action, "the heating is not the volume");
    const auto absolute = parse_offline_command("Χαμήλωσε την ένταση στο 20");
    expect(absolute.has_action && absolute.action.type == AssistantActionType::Volume && !absolute.action.relative &&
           absolute.action.value == 20, "a number still sets the volume");

    // The device by voice (the owner, 2026-11-18): said outright, it is a command;
    // a sentence that only mentions it goes to the assistant.
    const auto kind = [](const char* text) { return parse_offline_command(text).kind; };
    expect(kind("Κάνε επανεκκίνηση") == OfflineCommandKind::Restart, "restart");
    expect(kind("Κάνε επανεκκίνηση στη συσκευή παρακαλώ.") == OfflineCommandKind::Restart, "restart the device");
    expect(kind("Μπορείς να κάνεις επανεκκίνηση;") == OfflineCommandKind::Restart, "a polite restart");
    expect(kind("Restart the device") == OfflineCommandKind::Restart && kind("Reboot") == OfflineCommandKind::Restart,
           "restart in English");
    expect(kind("Τι κάνει η επανεκκίνηση;") == OfflineCommandKind::None, "a question about restarting");
    expect(kind("Πώς κάνω επανεκκίνηση στο κινητό μου;") == OfflineCommandKind::None, "the phone's restart");
    expect(kind("Restart the timer") == OfflineCommandKind::None, "restarting a timer is not the device");
    expect(kind("Απενεργοποιήσου") == OfflineCommandKind::PowerOff, "switch yourself off");
    expect(kind("Κλείσε τη συσκευή") == OfflineCommandKind::PowerOff, "close the device");
    expect(kind("Σβήσε τη συσκευή τώρα") == OfflineCommandKind::PowerOff, "put the device out");
    expect(kind("Turn off the device") == OfflineCommandKind::PowerOff, "turn off the device");
    expect(kind("Shut down") == OfflineCommandKind::PowerOff && kind("Power off") == OfflineCommandKind::PowerOff,
           "power off in English");
    expect(kind("Κλείσε την πόρτα") == OfflineCommandKind::None, "a door is not the device");
    expect(kind("Κλείσε το φως") == OfflineCommandKind::None, "nor a light");
    expect(kind("Turn off") == OfflineCommandKind::None, "turn off what?");
    expect(kind("Κλείσε") == OfflineCommandKind::None && kind("Σβήσε το") == OfflineCommandKind::None,
           "close what?");
    expect(kind("Η συσκευή") == OfflineCommandKind::None && kind("The device") == OfflineCommandKind::None,
           "the device's name alone asks for nothing");
    expect(kind("Πώς απενεργοποιώ τη συσκευή;") == OfflineCommandKind::None, "a question about switching off");

    const auto relative_brightness = [](const char* text) {
        const auto command = parse_offline_command(text);
        return command.has_action && command.action.type == AssistantActionType::Brightness &&
                       command.action.relative ? command.action.value : 0;
    };
    expect(relative_brightness("Αύξησε τη φωτεινότητα") == 20, "brighter");
    expect(relative_brightness("Μείωσε λίγο τη φωτεινότητα") == -20, "dimmer");
    expect(relative_brightness("Κάνε την οθόνη πιο φωτεινή") == 20, "a brighter screen");
    expect(relative_brightness("Make the screen brighter") == 20 && relative_brightness("Lower the brightness") == -20,
           "brightness steps in English");
    const auto brightness_to = parse_offline_command("Αύξησε τη φωτεινότητα στο 70");
    expect(brightness_to.has_action && brightness_to.action.type == AssistantActionType::Brightness &&
               !brightness_to.action.relative && brightness_to.action.value == 70, "a number sets the brightness");
    expect(!parse_offline_command("Αύξησε τη φωτεινότητα στο 200").has_action, "a brightness out of range is no step");
    expect(!parse_offline_command("Τι φωτεινότητα έχει ο ήλιος;").has_action, "the sun's brightness is a question");
    const auto volume_to = parse_offline_command("Μείωσε την ένταση στο 30");
    expect(volume_to.has_action && volume_to.action.type == AssistantActionType::Volume &&
               !volume_to.action.relative && volume_to.action.value == 30, "μείωσε with a number");

    expect(kind("Άνοιξε τον καιρό") == OfflineCommandKind::OpenWeather, "the weather");
    expect(kind("Άνοιξε τις ρυθμίσεις") == OfflineCommandKind::OpenSettings, "settings");
    expect(kind("Άνοιξε το Σήμερα") == OfflineCommandKind::OpenToday, "today");
    expect(kind("Άνοιξε τα μηνύματα") == OfflineCommandKind::OpenMessages, "messages");
    expect(kind("Δείξε μου τις φωτογραφίες") == OfflineCommandKind::OpenPhotos, "photos");
    expect(kind("Πήγαινε στην αρχική") == OfflineCommandKind::OpenHome, "home");
    expect(kind("Άνοιξε το μενού") == OfflineCommandKind::OpenApps, "the menu");
    expect(kind("Άνοιξε το wifi") == OfflineCommandKind::OpenWifi && kind("Open Wi-Fi") == OfflineCommandKind::OpenWifi,
           "wifi");
    expect(kind("Άνοιξε τη μεταφορά αρχείων") == OfflineCommandKind::OpenFiles, "file transfer");
    expect(kind("Δείξε την μπαταρία") == OfflineCommandKind::OpenBattery, "the battery screen");
    expect(kind("Δείξε μου τα μηνύματα που έστειλε η Μαρία") == OfflineCommandKind::None,
           "a question about messages goes to the assistant");

    // Φακός (the owner, 2026-11-21: «η Αύρα πρέπει να μπορεί να ανάψει το
    // φακό»): white, red, and off, said outright; anything more is the assistant's.
    expect(kind("Άναψε τον φακό") == OfflineCommandKind::OpenFlashlight &&
               kind("Αύρα, άναψε το φακό") == OfflineCommandKind::OpenFlashlight &&
               kind("Άνοιξε τον φακό") == OfflineCommandKind::OpenFlashlight &&
               kind("Φακός") == OfflineCommandKind::OpenFlashlight &&
               kind("Μπορείς να ανάψεις τον φακό;") == OfflineCommandKind::OpenFlashlight &&
               kind("Turn on the flashlight") == OfflineCommandKind::OpenFlashlight &&
               kind("Torch on") == OfflineCommandKind::OpenFlashlight,
           "the flashlight, white");
    expect(kind("Κόκκινο φως") == OfflineCommandKind::OpenRedLight &&
               kind("Άναψε κόκκινο φως") == OfflineCommandKind::OpenRedLight &&
               kind("Άναψε τον φακό στο κόκκινο") == OfflineCommandKind::OpenRedLight &&
               kind("Red light") == OfflineCommandKind::OpenRedLight,
           "the flashlight, red");
    expect(kind("Σβήσε τον φακό") == OfflineCommandKind::OpenHome && kind("Κλείσε τον φακό") == OfflineCommandKind::OpenHome &&
               kind("Σβήσε το κόκκινο φως") == OfflineCommandKind::OpenHome &&
               kind("Turn off the flashlight") == OfflineCommandKind::OpenHome,
           "off: Home, never the device switched off");
    expect(kind("Πού έβαλα τον φακό μου;") == OfflineCommandKind::None &&
               kind("Πώς μαγειρεύω φακές;") == OfflineCommandKind::None &&
               kind("Τι σημαίνει το κόκκινο φως στο φανάρι;") == OfflineCommandKind::None &&
               kind("Άναψε το φως") == OfflineCommandKind::None,
           "a sentence about a flashlight, lentils, a traffic light or the room's light is the assistant's");
    expect(std::string(offline_command_app(OfflineCommandKind::OpenFlashlight)) == "flashlight" &&
               std::string(offline_command_app(OfflineCommandKind::OpenRedLight)) == "red_light",
           "the flashlight's two apps");

    expect(std::string(offline_command_app(OfflineCommandKind::OpenWeather)) == "weather" &&
               std::string(offline_command_app(OfflineCommandKind::OpenToday)) == "today" &&
               offline_command_app(OfflineCommandKind::Restart) == nullptr, "each opening command names its app");
    for (const auto opening : {OfflineCommandKind::OpenClock, OfflineCommandKind::OpenNotes,
                               OfflineCommandKind::OpenCalendar, OfflineCommandKind::OpenTasks,
                               OfflineCommandKind::OpenShopping, OfflineCommandKind::OpenRadio,
                               OfflineCommandKind::OpenWeather, OfflineCommandKind::OpenToday,
                               OfflineCommandKind::OpenSettings, OfflineCommandKind::OpenMessages,
                               OfflineCommandKind::OpenPhotos, OfflineCommandKind::OpenHome,
                               OfflineCommandKind::OpenApps, OfflineCommandKind::OpenBattery,
                               OfflineCommandKind::OpenWifi, OfflineCommandKind::OpenFiles,
                               OfflineCommandKind::OpenFlashlight, OfflineCommandKind::OpenRedLight}) {
        const char* app = offline_command_app(opening);
        expect(app && assistant_app_known(app), "each opening command names a known app");
    }

    const auto alarm_at = [](const char* text, int hour, int minute) {
        const auto command = parse_offline_command(text);
        return command.has_action && command.action.type == AssistantActionType::Alarm &&
               command.action.hour == hour && command.action.minute == minute;
    };
    expect(alarm_at("Βάλε ξυπνητήρι στις 7", 7, 0), "an alarm at 7");
    expect(alarm_at("Βάλε ξυπνητήρι στις 7.30", 7, 30), "an alarm at 7.30");
    expect(alarm_at("Βάλε ένα ξυπνητήρι στις εφτά και μισή", 7, 30), "half past, the article is no hour");
    expect(alarm_at("Βάλε ξυπνητήρι στις οχτώ και τέταρτο", 8, 15), "quarter past");
    expect(alarm_at("Βάλε ξυπνητήρι στις εφτά παρά τέταρτο", 6, 45), "quarter to");
    expect(alarm_at("Βάλε ξυπνητήρι στις 8 το βράδυ", 20, 0), "in the evening");
    expect(alarm_at("Βάλε ξυπνητήρι στις 6:45 το πρωί", 6, 45), "in the morning");
    expect(alarm_at("Set an alarm for 7 pm", 19, 0) && alarm_at("Set an alarm at 6:30 am", 6, 30), "am and pm");
    expect(alarm_at("Ξύπνησέ με στις 7", 7, 0), "wake me at 7");
    expect(!parse_offline_command("Βάλε ξυπνητήρι").has_action, "no time goes to the assistant");
    expect(!parse_offline_command("Βάλε ξυπνητήρι στις 25").has_action, "no such hour");
    std::cout << "offline command tests passed\n";
}

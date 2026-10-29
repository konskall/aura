#include "aura/offline_commands.hpp"

#include <cstdlib>
#include <iostream>

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
    std::cout << "offline command tests passed\n";
}

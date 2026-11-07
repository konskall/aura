#include "aura/transfer_strings.hpp"
namespace aura::l10n {
const TransferStrings& transfer_strings(InterfaceLanguage language) {
    static const TransferStrings el{
        "Μεταφορά", "Μεταφορά αρχείων", "Στείλε αρχεία από κινητό ή υπολογιστή στο ίδιο Wi-Fi.",
        "Ενεργοποίηση", "Διακοπή πρόσβασης", "Εκκίνηση…", "Σκάναρε το QR", "Κωδικός", "Λήγει σε %u:%02u", "Αρχεία: %u",
        "Έτοιμη για μεταφορά", "Λήψη αρχείου…", "Η πρόσβαση έληξε", "Πολλές προσπάθειες", "Η μεταφορά δεν ξεκίνησε",
        "Πρόσβαση από αυτό το PC", "Έλεγξε τη σύνδεση Wi-Fi.", "Έλεγξε την κάρτα και τον ελεύθερο χώρο.",
        "Υπάρχει ήδη αυτό το αρχείο.", "Η εικόνα δεν υποστηρίζεται ή είναι πολύ μεγάλη.", "Η μεταφορά διακόπηκε.",
        "Προσωρινή πρόσβαση · έμπιστο Wi-Fi", "Άνοιγμα στον browser",
        "Σκάναρε με το κινητό για σύνδεση", "Ή από υπολογιστή", "Το κινητό θέλει το ίδιο Wi-Fi με τη συσκευή."
    };
    static const TransferStrings en{
        "Transfer", "File transfer", "Send files from your phone or computer on the same Wi-Fi.",
        "Enable access", "Stop access", "Starting…", "Scan the QR code", "Code", "Ends in %u:%02u", "Files: %u",
        "Ready to receive", "Receiving file…", "Access expired", "Too many attempts", "Could not start transfer",
        "Access from this PC", "Check the Wi-Fi connection.", "Check the card and available space.",
        "This file already exists.", "Unsupported or oversized image.", "Transfer interrupted.",
        "Temporary access · trusted Wi-Fi", "Open in browser",
        "Scan with your phone to sign in", "Or from a PC", "The phone must use the same Wi-Fi."
    };
    return language == InterfaceLanguage::English ? en : el;
}
}

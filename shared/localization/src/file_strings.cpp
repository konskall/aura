#include "aura/file_strings.hpp"
namespace aura::l10n {
const FileStrings& file_strings(InterfaceLanguage language) {
    static const FileStrings el{
        "Αρχεία", "Κάρτα SD", "%s ελεύθερα από %s", "Δεν βρέθηκε κάρτα SD", "Βάλε μια κάρτα microSD και ξαναδοκίμασε.",
        "Φόρτωση…", "Ο φάκελος είναι άδειος", "Ο φάκελος δεν διαβάστηκε. Δοκίμασε ξανά.", "Εμφανίζονται τα πρώτα %u",
        "Σελίδα %u / %u", "Φάκελος", "Τοποθεσία", "Μέγεθος", "Τροποποίηση", "Είδος", "Εικόνα", "Ήχος", "Έγγραφο", "Αρχείο",
        "Αρχείο της συσκευής: δεν διαγράφεται από εδώ.",
        "Άνοιγμα", "Διαγραφή", "Διαγραφή", "Το αρχείο θα διαγραφεί οριστικά από την κάρτα.",
        "Ο φάκελος και ό,τι περιέχει θα διαγραφούν οριστικά από την κάρτα.", "Διαγραφή", "Ακύρωση", "Διαγράφηκε",
        "Η διαγραφή απέτυχε. Δοκίμασε ξανά.", "Διαγραφή…", "Ανανέωση", ",",
        "Μετονομασία", "Νέο όνομα", "Μετονομάστηκε", "Δεν άλλαξε το όνομα. Μήπως υπάρχει ήδη;", "Προβολή",
        "Ανοίγει…", "Δεν ανοίγει αυτό το αρχείο.", "Αναπαραγωγή", "Διακοπή"
    };
    static const FileStrings en{
        "Files", "SD card", "%s free of %s", "No SD card found", "Insert a microSD card and try again.",
        "Loading…", "This folder is empty", "Could not read this folder. Try again.", "Showing the first %u",
        "Page %u / %u", "Folder", "Location", "Size", "Modified", "Type", "Image", "Audio", "Document", "File",
        "A device file: it cannot be deleted here.",
        "Open", "Delete", "Delete", "This file will be permanently deleted from the card.",
        "This folder and everything in it will be permanently deleted from the card.", "Delete", "Cancel", "Deleted",
        "Could not delete it. Try again.", "Deleting…", "Refresh", ".",
        "Rename", "New name", "Renamed", "The name did not change. Is it taken?", "View",
        "Opening…", "This file does not open.", "Play", "Stop"
    };
    return language == InterfaceLanguage::English ? en : el;
}
}  // namespace aura::l10n

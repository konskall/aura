#include "aura/photo_strings.hpp"
namespace aura::l10n {
const PhotoStrings& photo_strings(InterfaceLanguage language) {
    static const PhotoStrings el{
        "Φωτογραφίες", "Παρουσίαση", "Φόρτωση…", "Δεν υπάρχουν φωτογραφίες", "Η κάρτα δεν είναι διαθέσιμη",
        "Δεν ανοίγει αυτή η εικόνα. Δοκίμασε άλλη φωτογραφία.",
        "Η εικόνα είναι πολύ μεγάλη. Αποθήκευσέ τη σε μικρότερο μέγεθος.", "Δεν αποθηκεύτηκε η ρύθμιση.",
        "Στείλε JPG/PNG από το κινητό ή τον υπολογιστή σου μέσω Wi-Fi.",
        "Στείλε JPG/PNG από το κινητό ή τον υπολογιστή σου μέσω Wi-Fi.",
        "Ανανέωση", "Παρουσίαση", "Αλλαγή εικόνας", "%u δευτ.", "1 λεπτό", "Στο ρολόι",
        "Φωτογραφία στο φόντο", "Χωρίς φωτογραφία", "Οι φωτογραφίες μένουν στη συσκευή.",
        "%u από %u", "Προηγούμενη", "Επόμενη", "Σελίδα %u / %u", "Χρήση αυτής",
        "Διαγραφή φωτογραφίας", "Διαγραφή", "Η φωτογραφία θα διαγραφεί οριστικά από τη συσκευή.",
        "Διαγραφή", "Ακύρωση", "Η διαγραφή απέτυχε. Δοκίμασε ξανά.", "Διαγραφή…"
    };
    static const PhotoStrings en{
        "Photos", "Slideshow", "Loading…", "No photos yet", "Memory card unavailable",
        "This image could not be opened. Try another photo.",
        "This image is too large. Save a smaller copy.", "Could not save this setting.",
        "Send JPG/PNG files from your phone or computer over Wi-Fi.",
        "Send JPG/PNG files from your phone or computer over Wi-Fi.",
        "Refresh", "Slideshow", "Change image", "%u sec", "1 minute", "On the clock",
        "Photo background", "No photo background", "Your photos stay on the device.",
        "%u of %u", "Previous", "Next", "Page %u / %u", "Use this photo",
        "Delete photo", "Delete", "This photo will be permanently deleted from the device.",
        "Delete", "Cancel", "Could not delete the photo. Try again.", "Deleting…"
    };
    return language == InterfaceLanguage::English ? en : el;
}
}

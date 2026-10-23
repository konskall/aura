#include "aura/mock_provider.hpp"

#include "aura/text_util.hpp"

#include <chrono>
#include <thread>

namespace aura {

AssistantReply mock_answer(const TurnRequest& request) {
    const auto& prompt = request.prompt;
    if (request.mode == TurnMode::Translate) {
        return AssistantReply{true, "DEMO [" + request.target_language + "] " + prompt, {}};
    }
    const auto folded = fold_text(prompt);
    // Actions use the same ACTION line a live provider is asked for, so the
    // confirmation flow can be exercised without a network.
    if (folded_contains_any(folded, {"χρονομετρ", "timer"})) {
        return AssistantReply{true,
            "DEMO • Μπορώ να βάλω χρονόμετρο 5 λεπτών.\n"
            "ACTION: {\"type\":\"timer\",\"seconds\":300,\"label\":\"DEMO\"}", {}};
    }
    if (folded_contains_any(folded, {"ξυπνητηρ", "alarm"})) {
        return AssistantReply{true,
            "DEMO • Μπορώ να βάλω ξυπνητήρι για τις 07:30.\n"
            "ACTION: {\"type\":\"alarm\",\"time\":\"07:30\",\"repeat\":\"once\",\"label\":\"DEMO\"}", {}};
    }
    if (folded_contains_any(folded, {"θυμισε", "υπενθυμισ", "remind"})) {
        return AssistantReply{true,
            "DEMO • Θα σου το θυμίσω σε 30 λεπτά.\n"
            "ACTION: {\"type\":\"reminder\",\"in_minutes\":30,\"text\":\"DEMO υπενθύμιση\"}", {}};
    }
    if (folded_contains_any(folded, {"σημειωσε", "κρατα σημειωση", "write down"})) {
        return AssistantReply{true,
            "DEMO • Να το κρατήσω στις σημειώσεις;\n"
            "ACTION: {\"type\":\"note\",\"text\":\"DEMO σημείωση\"}", {}};
    }
    if (folded_contains_any(folded, {"καιρ", "βρεξ"})) {
        return AssistantReply{true,
            "DEMO • Δεν υπάρχει ενεργή ζωντανή πηγή καιρού. Το δείγμα δείχνει 21°C, ασθενή άνεμο και καθαρό ουρανό. Για πραγματικά δεδομένα χρειάζεται ρητά ρυθμισμένος πάροχος με πηγή και χρονική σήμανση.", {}};
    }
    if (folded_contains_any(folded, {"ταυγετο"})) {
        return AssistantReply{true,
            "Ο Ταΰγετος είναι η επιβλητική οροσειρά της νότιας Πελοποννήσου. Η ψηλότερη κορυφή του είναι ο Προφήτης Ηλίας. Αυτή είναι γενική, μη ζωντανή πληροφορία από το deterministic Demo Mode.", {}};
    }
    if (!request.history.empty()) {
        return AssistantReply{true,
            "DEMO • Συνεχίζω τη συζήτησή μας (" + std::to_string(request.history.size()) +
            " προηγούμενες ερωτήσεις). Η απάντηση δημιουργήθηκε τοπικά από το deterministic mock.", {}};
    }
    return AssistantReply{true,
        "DEMO • Σε άκουσα. Η απάντηση δημιουργήθηκε τοπικά από το deterministic mock, χωρίς δίκτυο, συνδρομή ή πρόσβαση σε προσωπικά δεδομένα.", {}};
}

std::future<AssistantReply> MockProvider::start_turn(TurnRequest request) {
    const auto generation = ++*generation_;
    const auto delay = delay_ms_;
    return std::async(std::launch::async, [current = generation_, generation, delay, request = std::move(request)] {
        if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if (current->load() != generation) return AssistantReply{false, {}, "CANCELLED"};
        return mock_answer(request);
    });
}

void MockProvider::cancel() {
    ++*generation_;
}

}  // namespace aura

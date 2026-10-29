#include "aura/mock_provider.hpp"

#include "aura/assistant_stream.hpp"
#include "aura/text_util.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace aura {

AssistantReply mock_answer(const TurnRequest& request) {
    const auto& prompt = request.prompt;
    if (request.mode == TurnMode::Translate) {
        return AssistantReply{true, "DEMO [" + request.target_language + "] " + prompt, {}};
    }
    const bool english = request.interface_language == InterfaceLanguage::English;
    const auto folded = fold_text(prompt);
    // Actions use the same ACTION line a live provider is asked for, so the
    // confirmation flow can be exercised without a network.
    if (folded_contains_any(folded, {"χρονομετρ", "timer"})) {
        if (english) return AssistantReply{true,
            "DEMO • I can set a five-minute timer.\n"
            "ACTION: {\"type\":\"timer\",\"seconds\":300,\"label\":\"DEMO\"}", {}};
        return AssistantReply{true,
            "DEMO • Μπορώ να βάλω χρονόμετρο 5 λεπτών.\n"
            "ACTION: {\"type\":\"timer\",\"seconds\":300,\"label\":\"DEMO\"}", {}};
    }
    if (folded_contains_any(folded, {"ξυπνητηρ", "alarm"})) {
        if (english) return AssistantReply{true,
            "DEMO • I can set an alarm for 07:30.\n"
            "ACTION: {\"type\":\"alarm\",\"time\":\"07:30\",\"repeat\":\"once\",\"label\":\"DEMO\"}", {}};
        return AssistantReply{true,
            "DEMO • Μπορώ να βάλω ξυπνητήρι για τις 07:30.\n"
            "ACTION: {\"type\":\"alarm\",\"time\":\"07:30\",\"repeat\":\"once\",\"label\":\"DEMO\"}", {}};
    }
    if (folded_contains_any(folded, {"θυμισε", "υπενθυμισ", "remind"})) {
        if (english) return AssistantReply{true,
            "DEMO • I can remind you in 30 minutes.\n"
            "ACTION: {\"type\":\"reminder\",\"in_minutes\":30,\"text\":\"DEMO reminder\"}", {}};
        return AssistantReply{true,
            "DEMO • Θα σου το θυμίσω σε 30 λεπτά.\n"
            "ACTION: {\"type\":\"reminder\",\"in_minutes\":30,\"text\":\"DEMO υπενθύμιση\"}", {}};
    }
    if (folded_contains_any(folded, {"σημειωσε", "κρατα σημειωση", "write down"})) {
        if (english) return AssistantReply{true,
            "DEMO • Should I save that in Notes?\n"
            "ACTION: {\"type\":\"note\",\"text\":\"DEMO note\"}", {}};
        return AssistantReply{true,
            "DEMO • Να το κρατήσω στις σημειώσεις;\n"
            "ACTION: {\"type\":\"note\",\"text\":\"DEMO σημείωση\"}", {}};
    }
    if (folded_contains_any(folded, {"καιρ", "βρεξ", "weather", "rain"})) {
        if (english) return AssistantReply{true,
            "DEMO • No live weather source is active. The sample shows 21°C, light wind and clear skies. Real data needs an explicitly configured provider with source and timestamp.", {}};
        return AssistantReply{true,
            "DEMO • Δεν υπάρχει ενεργή ζωντανή πηγή καιρού. Το δείγμα δείχνει 21°C, ασθενή άνεμο και καθαρό ουρανό. Για πραγματικά δεδομένα χρειάζεται ρητά ρυθμισμένος πάροχος με πηγή και χρονική σήμανση.", {}};
    }
    if (folded_contains_any(folded, {"ταυγετο", "taygetos"})) {
        if (english) return AssistantReply{true,
            "DEMO · Taygetos is the prominent mountain range of the southern Peloponnese. Its highest summit is Profitis Ilias. This is general, non-live information from deterministic Demo Mode.", {}};
        return AssistantReply{true,
            "Ο Ταΰγετος είναι η επιβλητική οροσειρά της νότιας Πελοποννήσου. Η ψηλότερη κορυφή του είναι ο Προφήτης Ηλίας. Αυτή είναι γενική, μη ζωντανή πληροφορία από τη λειτουργία DEMO.", {}};
    }
    if (!request.history.empty()) {
        if (english) return AssistantReply{true,
            "DEMO • Continuing our conversation (" + std::to_string(request.history.size()) +
            " earlier questions). This answer was created locally by the deterministic mock.", {}};
        return AssistantReply{true,
            "DEMO • Συνεχίζω τη συζήτησή μας (" + std::to_string(request.history.size()) +
            " προηγούμενες ερωτήσεις). Η απάντηση δημιουργήθηκε τοπικά από το deterministic mock.", {}};
    }
    if (english) return AssistantReply{true,
        "DEMO • I heard you. This answer was created locally by the deterministic mock, without network, subscription or access to personal data.", {}};
    return AssistantReply{true,
        "DEMO • Σε άκουσα. Η απάντηση δημιουργήθηκε τοπικά από το deterministic mock, χωρίς δίκτυο, συνδρομή ή πρόσβαση σε προσωπικά δεδομένα.", {}};
}

std::future<AssistantReply> MockProvider::start_turn(TurnRequest request) {
    const auto generation = ++*generation_;
    const auto delay = delay_ms_;
    return std::async(std::launch::async, [current = generation_, generation, delay, request = std::move(request)] {
        const auto reply = mock_answer(request);
        if (!request.stream) {
            if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            if (current->load() != generation) return AssistantReply{false, {}, "CANCELLED"};
            return reply;
        }
        // Deterministic fake token delivery exercises the same bounded mailbox
        // as a live SSE provider while remaining explicitly labelled DEMO.
        std::size_t offset = 0;
        const auto pause = std::chrono::milliseconds(delay == 0 ? 0 : std::max<std::uint32_t>(12, delay / 24));
        while (offset < reply.text.size()) {
            if (current->load() != generation) return AssistantReply{false, {}, "CANCELLED"};
            auto end = reply.text.find(' ', offset + 1);
            if (end == std::string::npos) end = reply.text.size();
            else ++end;  // retain the separator in the raw cumulative stream
            request.stream->publish_delta(std::string_view(reply.text).substr(offset, end - offset));
            offset = end;
            if (pause.count() > 0) std::this_thread::sleep_for(pause);
        }
        if (current->load() != generation) return AssistantReply{false, {}, "CANCELLED"};
        return reply;
    });
}

void MockProvider::cancel() {
    ++*generation_;
}

}  // namespace aura

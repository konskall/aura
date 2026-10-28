#include "aura/assistant_stream.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace aura;

    expect(first_complete_sentence("Η θερμοκρασία είναι 21.5 βαθμοί.").value_or("") ==
               "Η θερμοκρασία είναι 21.5 βαθμοί.",
           "decimal point is not a sentence boundary");
    expect(first_complete_sentence("Θα βρέξει σήμερα; Ναι.").value_or("") == "Θα βρέξει σήμερα;",
           "Greek question mark ends the first sentence");
    expect(!first_complete_sentence("ACTION: {\"type\":\"timer\"}"),
           "an action line is never sent to early speech");

    AssistantStreamMailbox mailbox;
    AssistantStreamSnapshot update;
    mailbox.publish_delta("Καλησπέρα ");
    expect(mailbox.take_after(0, update), "first partial is available");
    const auto first_sequence = update.sequence;
    expect(update.text == "Καλησπέρα" && update.first_sentence.empty(), "partial is sanitized and incomplete");
    mailbox.publish_delta("σου. Πώς είσαι;");
    expect(mailbox.take_after(first_sequence, update), "new cumulative partial is available");
    expect(update.text == "Καλησπέρα σου. Πώς είσαι;", "split chunks retain whitespace");
    expect(update.first_sentence == "Καλησπέρα σου.", "first complete sentence is stable");
    const auto complete_sequence = update.sequence;
    expect(!mailbox.take_after(complete_sequence, update), "mailbox coalesces when unchanged");

    AssistantStreamMailbox action;
    action.publish_delta("Θα το ετοιμάσω.\nACTION: {\"type\":\"timer\"");
    expect(action.take_after(0, update) && update.text == "Θα το ετοιμάσω.",
           "an incomplete action is hidden from streamed display text");

    AssistantStreamMailbox bounded;
    bounded.publish_delta(std::string(AssistantStreamMailbox::kMaxRawBytes + 100, 'x'));
    expect(bounded.take_after(0, update) && update.truncated &&
               update.text.size() <= AssistantStreamMailbox::kMaxTextBytes,
           "untrusted output stays bounded");

    AssistantStreamMailbox decoded_mailbox;
    AssistantStreamDecoder decoder(&decoded_mailbox);
    expect(decoder.push("{\"type\":\"delta\",\"text\":\"Γειά "), "split NDJSON prefix accepted");
    expect(decoder.push("σου.\"}\n{\"type\":\"done\",\"text\":\"Γειά σου.\"}\n"),
           "split UTF-8 NDJSON suffix accepted");
    expect(decoder.finish() && decoder.final_text() == "Γειά σου.", "authoritative final text decoded");
    expect(decoded_mailbox.take_after(0, update) && update.first_sentence == "Γειά σου.",
           "wire deltas feed the shared first-sentence mailbox");

    AssistantStreamDecoder invalid;
    expect(!invalid.push("{\"type\":\"unknown\"}\n"), "unknown wire event fails closed");

    // One network read may carry many short lines; only a single line is bounded.
    std::string many_lines;
    while (many_lines.size() < AssistantStreamDecoder::kMaxLineBytes * 2) {
        many_lines += "{\"type\":\"delta\",\"text\":\"λέξη \"}\n";
    }
    many_lines += "{\"type\":\"done\",\"text\":\"τέλος\"}\n";
    AssistantStreamDecoder large_read;
    expect(large_read.push(many_lines) && large_read.finish() && large_read.final_text() == "τέλος",
           "a large read of short lines is accepted");
    AssistantStreamDecoder endless;
    expect(!endless.push(std::string(AssistantStreamDecoder::kMaxLineBytes + 1, 'x')),
           "one oversized line still fails closed");

    // Early speech never starts with an abbreviation or a list number.
    expect(first_complete_sentence("Πάρε π.χ. γάλα και ψωμί. Μετά").value_or("") == "Πάρε π.χ. γάλα και ψωμί.",
           "π.χ. does not end a sentence");
    expect(first_complete_sentence("Φρούτα, λαχανικά κ.λπ. κάνουν καλό. Ναι").value_or("") ==
               "Φρούτα, λαχανικά κ.λπ. κάνουν καλό.",
           "κ.λπ. does not end a sentence");
    expect(first_complete_sentence("1. Άνοιξε το ρολόι. 2. Πάτα +.").value_or("") == "1. Άνοιξε το ρολόι.",
           "a list number is not a sentence");
    expect(first_complete_sentence("Ναι. Θα βρέξει.").value_or("") == "Ναι.", "a short sentence still ends");
    expect(first_complete_sentence("Η απάντηση είναι 42. Τέλος.").value_or("") == "Η απάντηση είναι 42.",
           "a number at the end of a sentence still ends it");

    // What is left to say after the early first sentence: never that sentence again.
    expect(unspoken_remainder("Καλησπέρα σου. Πώς είσαι;", "Καλησπέρα σου.") == "Πώς είσαι;",
           "the rest after the spoken sentence");
    expect(unspoken_remainder("Καλησπέρα  σου.\n\nΠώς είσαι;", "Καλησπέρα σου.") == "Πώς είσαι;",
           "spacing differences do not repeat the first sentence");
    expect(unspoken_remainder("**Καλησπέρα** σου. Πώς είσαι;", "Καλησπέρα σου.") == "Πώς είσαι;",
           "markup differences do not repeat the first sentence");
    expect(unspoken_remainder("Καλησπέρα σου.", "Καλησπέρα σου.").empty(), "nothing left after a one-sentence answer");
    expect(unspoken_remainder("Γεια. Άλλη απάντηση.", "Καλησπέρα σου.") == "Γεια. Άλλη απάντηση.",
           "a different final answer is spoken whole");

    // Cleaning is incremental: small deltas, even ones that split a Greek
    // letter's bytes, give exactly the text of one large delta.
    std::string long_answer;
    for (int index = 0; index < 400; ++index) long_answer += "Καλημέρα\tκόσμε! ";
    AssistantStreamMailbox whole;
    AssistantStreamMailbox pieces;
    whole.publish_delta(long_answer);
    for (std::size_t at = 0; at < long_answer.size(); at += 3) {
        pieces.publish_delta(std::string_view(long_answer).substr(at, 3));
    }
    AssistantStreamSnapshot whole_update;
    AssistantStreamSnapshot pieces_update;
    expect(whole.take_after(0, whole_update) && pieces.take_after(0, pieces_update) &&
               whole_update.text == pieces_update.text && whole_update.first_sentence == pieces_update.first_sentence,
           "small deltas give the same text as one");

    std::cout << "assistant_stream_test: PASS\n";
    return 0;
}

#include "aura/aoraton_inco.hpp"
#include "aura/json_reader.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace aura;

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

AoratonIncoCandidate asking() {
    AoratonIncoCandidate candidate;
    candidate.sent_here = true;
    candidate.row_id = "7b0c3a52-1111-4222-8333-944455556666";
    candidate.uid = "user-1";
    candidate.type = "text";
    candidate.text = "@inco τι ώρα είναι;";
    candidate.room_ai_enabled = true;
    return candidate;
}

AoratonIncoRow row(const char* uid, const char* username, const std::string& text, const char* type = "text") {
    AoratonIncoRow value;
    value.id = "r";
    value.uid = uid;
    value.username = username;
    value.type = type;
    value.text = text;
    return value;
}

std::size_t code_points(const std::string& text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) count += (byte & 0xC0) != 0x80;
    return count;
}

void mention_tests() {
    for (const char* text : {"inco", "@inco", "Inco, τι ώρα είναι;", "INCO", "ρώτα τον @inco.", "@inco τι ώρα είναι;",
                             "  hey inco  ", "(inco)", "inco?"}) {
        expect(aoraton_mentions_inco(text), text);
    }
    for (const char* text : {"incoming", "income", "incognito", "Incoming call", "inco2", "xinco", "@incognito", "",
                             "INCOME tax", "τίποτα"}) {
        expect(!aoraton_mentions_inco(text), text);
    }
}

void answer_rule_tests() {
    expect(aoraton_inco_should_answer(asking(), true, false, false), "own fresh mention in an inco room is answered");
    auto other_device = asking();
    other_device.sent_here = false;
    expect(!aoraton_inco_should_answer(other_device, true, false, false),
           "a message from another device or user is answered by that device, never here");
    auto unsaved = asking();
    unsaved.row_id.clear();
    expect(!aoraton_inco_should_answer(unsaved, true, false, false), "only a stored row is answered");
    auto from_inco = asking();
    from_inco.uid = kAoratonIncoUid;
    expect(!aoraton_inco_should_answer(from_inco, true, false, false), "inco never answers itself");
    auto room_off = asking();
    room_off.room_ai_enabled = false;
    expect(!aoraton_inco_should_answer(room_off, true, false, false), "rooms without inco are not answered");
    auto system = asking();
    system.type = "system";
    expect(!aoraton_inco_should_answer(system, true, false, false), "system rows are not answered");
    auto blank = asking();
    blank.text = "   ";
    expect(!aoraton_inco_should_answer(blank, true, false, false), "empty text is not answered");
    auto plain = asking();
    plain.text = "incoming τηλεφώνημα";
    expect(!aoraton_inco_should_answer(plain, true, false, false), "a message not addressed to inco is not answered");
    auto reply = plain;
    reply.reply_to_inco = true;
    expect(aoraton_inco_should_answer(reply, true, false, false), "a reply to inco is answered");
    expect(!aoraton_inco_should_answer(asking(), false, false, false), "off until the owner enables it");
    expect(!aoraton_inco_should_answer(asking(), true, true, false), "one answer at a time");
    expect(!aoraton_inco_should_answer(asking(), true, false, true), "quiet for the day after the quota");
}

void history_tests() {
    std::vector<AoratonIncoRow> rows;
    rows.push_back(row("u-old", "Old", "πολύ παλιό"));
    for (int index = 0; index < 26; ++index) rows.push_back(row("user-1", "Owner", "μήνυμα " + std::to_string(index)));
    rows.push_back(row("user-1", "Owner", "joined", "system"));
    rows.push_back(row("u-maria", "Maria", "", "image"));
    auto poll = row("u-maria", "Maria", "", "poll");
    poll.has_poll = true;
    poll.poll_question = "Πού;";
    poll.poll_options = {"a", "b", "c"};
    rows.push_back(poll);
    auto bare_poll = poll;
    bare_poll.poll_options.clear();
    rows.push_back(bare_poll);
    rows.push_back(row(kAoratonIncoUid, "inco", "Είναι 01:15."));
    auto reply = row("u-maria", "Maria", "ευχαριστώ");
    reply.reply_to_username = "inco";
    rows.push_back(reply);
    rows.push_back(row("user-1", "Owner", std::string(800, 'x') + "αβγ"));
    std::string greek;
    for (int index = 0; index < 750; ++index) greek += "λ";
    rows.push_back(row("user-1", "Owner", greek));
    rows.push_back(row("user-1", "Owner", "@inco τι ώρα είναι;"));

    const auto turns = aoraton_inco_turns(rows);
    expect(turns.size() == 24, "at most 24 turns");
    expect(turns.back().text == "Owner: @inco τι ώρα είναι;" && !turns.back().model, "the question is the last turn");
    expect(code_points(turns[turns.size() - 2].text) == 7 + 700, "greek text cut to 700 characters, not bytes");
    expect(turns[turns.size() - 3].text == "Owner: " + std::string(700, 'x'), "long text cut to 700 characters");
    expect(turns[turns.size() - 4].text == "Maria (reply to inco): ευχαριστώ", "a reply names who it answers");
    expect(turns[turns.size() - 5].model && turns[turns.size() - 5].text == "Είναι 01:15.",
           "inco's own rows are model turns without a name");
    expect(turns[turns.size() - 6].text == "Maria: [poll: Πού;]", "a poll without options");
    expect(turns[turns.size() - 7].text == "Maria: [poll: Πού; \xE2\x80\x94 options: a, b, c]", "a poll and its options");
    for (const auto& turn : turns) {
        expect(turn.text.find("joined") == std::string::npos, "system rows are left out");
        expect(turn.text != "Maria: ", "bare attachments are left out");
        expect(turn.text.find("πολύ παλιό") == std::string::npos, "only the newest turns");
    }
}

void plain_text_tests() {
    expect(aoraton_inco_plain_text("# Καιρός") == "Καιρός", "heading");
    expect(aoraton_inco_plain_text("   ### Sub") == "Sub", "indented heading");
    expect(aoraton_inco_plain_text("    # code block") == "# code block", "four spaces are not a heading");
    expect(aoraton_inco_plain_text("####### seven") == "####### seven", "seven # are not a heading");
    expect(aoraton_inco_plain_text("#hashtag") == "#hashtag", "a hashtag stays");
    expect(aoraton_inco_plain_text("* ένα\n  - δύο\n+ τρία") == "• ένα\n  • δύο\n• τρία", "bullets keep their indent");
    expect(aoraton_inco_plain_text("δες [εδώ](https://example.com/a) τώρα") == "δες εδώ τώρα", "link text only");
    expect(aoraton_inco_plain_text("**Αθήνα**: 18°C και __υγρασία__") == "Αθήνα: 18°C και υγρασία", "bold");
    expect(aoraton_inco_plain_text("είναι *πολύ* ζεστά") == "είναι πολύ ζεστά", "italic");
    expect(aoraton_inco_plain_text("2 * 3 = 6") == "2 * 3 = 6", "arithmetic stays");
    expect(aoraton_inco_plain_text("a * b * c") == "a * b * c", "spaced asterisks stay");
    expect(aoraton_inco_plain_text("τρέξε `ls -la`") == "τρέξε ls -la", "code");
    expect(aoraton_inco_plain_text("snake_case_name") == "snake_case_name", "snake case stays");
    expect(aoraton_inco_plain_text("  \n**Γεια**\n  ") == "Γεια", "trimmed");
    expect(aoraton_inco_plain_text("# ") == "# ", "an answer that would come out empty is kept");
    expect(aoraton_inco_plain_text("## Τίτλος\n* **Σήμερα**: βροχή\n* [Πηγή](https://x.y)") ==
               "Τίτλος\n• Σήμερα: βροχή\n• Πηγή",
           "a typical model answer");
}

void time_tests() {
    // 2026-10-23T22:15:00Z
    const std::int64_t when = 1792793700;
    expect(aoraton_inco_client_date_time(when, 180) == "Saturday, October 24, 2026 at 01:15 AM GMT+3",
           "the browser's local date and time");
    expect(aoraton_inco_client_date_time(when, 330) == "Saturday, October 24, 2026 at 03:45 AM GMT+5:30",
           "half-hour zone");
    expect(aoraton_inco_client_date_time(when, -240) == "Friday, October 23, 2026 at 06:15 PM GMT-4", "west of UTC");
    expect(aoraton_inco_client_date_time(when, 0) == "Friday, October 23, 2026 at 10:15 PM UTC", "UTC");
    expect(aoraton_inco_client_date_time(when + 6300, 0) == "Saturday, October 24, 2026 at 12:00 AM UTC", "midnight");
    expect(aoraton_inco_client_date_time(when - 36000, 0) == "Friday, October 23, 2026 at 12:15 PM UTC", "noon");
    expect(aoraton_inco_local_day(when, 180) == "2026-10-24" && aoraton_inco_local_day(when, 0) == "2026-10-23",
           "local day for the quota");
    expect(aoraton_inco_prefix("αβγδ", 2) == "αβ" && aoraton_inco_prefix("abc", 10) == "abc", "prefix in characters");
}

void request_tests() {
    const std::vector<AoratonIncoTurn> turns{{false, "Owner: γεια \"σου\"\nνέα γραμμή"}, {true, "Γεια!"}};
    JsonValue body;
    expect(parse_json(aoraton_inco_request_body("v2.room", turns, "@inco γεια", "Owner",
                                                "Saturday, October 24, 2026 at 01:15 AM GMT+3",
                                                AoratonIncoLocation::Unasked, "Αθήνα"),
                      body),
           "request is JSON");
    expect(body.string_at("roomKey") == "v2.room" && body.string_at("triggerText") == "@inco γεια" &&
               body.string_at("triggerUsername") == "Owner" &&
               body.string_at("clientDateTime") == "Saturday, October 24, 2026 at 01:15 AM GMT+3",
           "request fields");
    const auto* history = body.find("history");
    expect(history && history->items().size() == 2 && history->items()[0].string_at("role") == "user" &&
               history->items()[0].string_at("text") == "Owner: γεια \"σου\"\nνέα γραμμή" &&
               history->items()[1].string_at("role") == "model",
           "history turns survive escaping");
    expect(!body.find("roomName") && !body.find("maxChars") && !body.find("locationCity") &&
               !body.find("locationDenied"),
           "no room name, no LoRa budget, no location until asked");
    JsonValue city;
    expect(parse_json(aoraton_inco_request_body("v2.room", turns, "q", "Owner", "t", AoratonIncoLocation::City, "Αθήνα"),
                      city) &&
               city.string_at("locationCity") == "Αθήνα" && !city.find("locationDenied"),
           "second pass with the known city");
    JsonValue denied;
    expect(parse_json(aoraton_inco_request_body("v2.room", turns, "q", "Owner", "t", AoratonIncoLocation::Denied, ""),
                      denied) &&
               denied.find("locationDenied") && denied.find("locationDenied")->as_bool(false) &&
               !denied.find("locationCity"),
           "second pass without a city");
}

void reply_tests() {
    AoratonIncoReply reply;
    expect(aoraton_parse_inco_reply(
               R"json({"text":"Είναι **01:15**.","sources":[{"title":"Ώρα","uri":"https://time.example/a"},)json"
               R"json({"title":"x","uri":"javascript:alert(1)"},{"title":"y"}]})json",
               reply),
           "text reply parses");
    expect(reply.kind == AoratonIncoReplyKind::Text && reply.text == "Είναι **01:15**." && reply.sources.size() == 1 &&
               reply.sources[0].title == "Ώρα" && reply.sources[0].uri == "https://time.example/a",
           "only https sources are kept");
    expect(aoraton_parse_inco_reply(R"({"needLocation":true})", reply) &&
               reply.kind == AoratonIncoReplyKind::NeedLocation,
           "location request");
    expect(aoraton_parse_inco_reply(R"({"quota":true})", reply) && reply.kind == AoratonIncoReplyKind::Quota, "quota");
    expect(aoraton_parse_inco_reply(R"({"text":"","sources":[]})", reply) && reply.kind == AoratonIncoReplyKind::Empty,
           "empty text writes nothing");
    expect(!aoraton_parse_inco_reply("[]", reply) && !aoraton_parse_inco_reply("not json", reply), "malformed");
    expect(!aoraton_parse_inco_reply("{\"text\":\"\xFF\"}", reply), "invalid UTF-8 is refused");
}

void avatar_tests() {
    expect(aoraton_inco_avatar("https://cdn.example/inco.png") == "https://cdn.example/inco.png", "https avatar");
    expect(aoraton_inco_avatar("https://cdn.example/a@b.png") == "https://cdn.example/a@b.png", "@ in the path");
    for (const char* value : {"", "http://cdn.example/a.png", "https://user:pw@cdn.example/a.png",
                              "https://user@cdn.example/a.png", "javascript:alert(1)", "data:image/png;base64,AA",
                              "https://cdn.example/a b.png", "https://"}) {
        expect(aoraton_inco_avatar(value) == kAoratonIncoDefaultAvatar, value);
    }
}

void insert_tests() {
    AoratonIncoQuote quote{"7b0c3a52-1111-4222-8333-944455556666", "Owner", "v2:quote", false};
    JsonValue body;
    expect(parse_json(aoraton_inco_insert_body("v2.room", "https://aoraton.com/inco-avatar.png", "v2:answer", &quote,
                                               {{"Ώρα", "https://time.example/a"}}),
                      body),
           "insert is JSON");
    expect(body.string_at("room_key") == "v2.room" && body.string_at("uid") == kAoratonIncoUid &&
               body.string_at("username") == "inco" &&
               body.string_at("avatar_url") == "https://aoraton.com/inco-avatar.png" &&
               body.string_at("text") == "v2:answer" && body.string_at("type") == "text",
           "inco row fields");
    expect(!body.find("client_msg_id") && !body.find("roomName"), "no client_msg_id");
    const auto* reply_to = body.find("reply_to");
    expect(reply_to && reply_to->string_at("id") == quote.id && reply_to->string_at("username") == "Owner" &&
               reply_to->string_at("text") == "v2:quote" && reply_to->find("isAttachment") &&
               reply_to->find("isAttachment")->type() == JsonType::Bool &&
               !reply_to->find("isAttachment")->as_bool(true),
           "reply quotes the encrypted question");
    const auto* sources = body.find("grounding_metadata");
    expect(sources && sources->items().size() == 1 && sources->items()[0].string_at("title") == "Ώρα" &&
               sources->items()[0].string_at("uri") == "https://time.example/a",
           "sources stored as grounding metadata");
    JsonValue empty;
    expect(parse_json(aoraton_inco_insert_body("v2.room", "a", "v2:x", &quote, {}), empty) &&
               empty.find("grounding_metadata") && empty.find("grounding_metadata")->type() == JsonType::Array &&
               empty.find("grounding_metadata")->items().empty(),
           "no sources: an empty array");
    JsonValue quota;
    expect(parse_json(aoraton_inco_insert_body("v2.room", "a", "v2:quota", nullptr, {}), quota) &&
               !quota.find("reply_to") && !quota.find("grounding_metadata") && quota.string_at("username") == "inco",
           "the quota notice quotes nothing");
}

}  // namespace

int main() {
    mention_tests();
    answer_rule_tests();
    history_tests();
    plain_text_tests();
    time_tests();
    request_tests();
    reply_tests();
    avatar_tests();
    insert_tests();
    std::cout << "aoraton inco tests passed\n";
    return 0;
}

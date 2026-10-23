#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

// inco, aoraton's room assistant, as the web client runs it: the device that
// SENT a message mentioning inco calls the `inco-ai` edge function, encrypts
// the answer with the room key and inserts it itself under inco's reserved
// uid. These are pure functions; the worker supplies the network and the keys.

inline constexpr char kAoratonIncoUid[] = "00000000-0000-0000-0000-000000000000";
inline constexpr char kAoratonIncoDefaultAvatar[] = "https://aoraton.com/inco-avatar.png";
inline constexpr char kAoratonIncoQuotaText[] =
    "\xE2\x9A\xA0\xEF\xB8\x8F I have exhausted my daily question quota. I will be available again tomorrow!";

// "inco" as a whole word, optionally @-mentioned: inco, @inco, "Inco," match;
// incoming, income and incognito do not.
bool aoraton_mentions_inco(std::string_view text);

// A message AURA has just written, as far as answering it is concerned.
struct AoratonIncoCandidate {
    bool sent_here{false};   // inserted by this process, and the insert returned its row
    std::string row_id;      // that stored row
    std::string uid;
    std::string type;
    std::string text;        // decrypted
    bool reply_to_inco{false};
    bool room_ai_enabled{false};
};

// Exactly one responder: only the device that sent the message answers it,
// never inco's own rows, only where the room enables inco, one at a time.
bool aoraton_inco_should_answer(const AoratonIncoCandidate& candidate, bool enabled, bool busy,
                                bool quota_exhausted);

// A decrypted row of the room, for the conversation sent along with the question.
struct AoratonIncoRow {
    std::string id;
    std::string uid;
    std::string username;
    std::string type;
    std::string text;
    std::string reply_to_username;  // empty when not a reply
    bool has_poll{false};
    std::string poll_question;
    std::vector<std::string> poll_options;
};

struct AoratonIncoTurn {
    bool model{false};  // inco's own message
    std::string text;
};

// The last 24 rows with something to say (no system rows, no bare
// attachments), each cut to 700 characters, oldest first.
std::vector<AoratonIncoTurn> aoraton_inco_turns(const std::vector<AoratonIncoRow>& rows);

// Plain text for the chat bubble: headings, bullets, links, bold, italic and
// code marks are dropped; an answer that would come out empty is kept as is.
std::string aoraton_inco_plain_text(const std::string& raw);

// The first `count` characters (Unicode code points) of UTF-8 text.
std::string aoraton_inco_prefix(std::string_view text, std::size_t count);

// The device's local time as the browser writes it for inco, e.g.
// "Saturday, October 24, 2026 at 01:15 AM GMT+3".
std::string aoraton_inco_client_date_time(std::int64_t utc_unix, int utc_offset_minutes);
// The local calendar day, "2026-10-24", for the daily quota.
std::string aoraton_inco_local_day(std::int64_t utc_unix, int utc_offset_minutes);

enum class AoratonIncoLocation { Unasked, City, Denied };

// The `inco-ai` request. Never carries the room name (the room is end-to-end
// encrypted) nor a LoRa reply budget.
std::string aoraton_inco_request_body(const std::string& room_key,
                                      const std::vector<AoratonIncoTurn>& turns,
                                      const std::string& trigger_text,
                                      const std::string& trigger_username,
                                      const std::string& client_date_time,
                                      AoratonIncoLocation location,
                                      const std::string& city);

struct AoratonIncoSource {
    std::string title;
    std::string uri;
};

enum class AoratonIncoReplyKind { Text, Empty, NeedLocation, Quota };

struct AoratonIncoReply {
    AoratonIncoReplyKind kind{AoratonIncoReplyKind::Empty};
    std::string text;
    std::vector<AoratonIncoSource> sources;  // https only, bounded
};

// A 200 answer of `inco-ai`; false when it is not the documented shape.
bool aoraton_parse_inco_reply(std::string_view json, AoratonIncoReply& reply);

// The room's custom inco avatar when it is an https URL without credentials,
// otherwise inco's default.
std::string aoraton_inco_avatar(const std::string& room_avatar);

// The quoted question inside an answer row.
struct AoratonIncoQuote {
    std::string id;
    std::string username;
    std::string wire;  // the first 100 characters, encrypted
    bool is_attachment{false};
};

// The inco row insert. `quote` is null for the quota notice, which (as on the
// web) quotes nothing and carries no sources. No client_msg_id.
std::string aoraton_inco_insert_body(const std::string& room_key,
                                     const std::string& avatar_url,
                                     const std::string& wire,
                                     const AoratonIncoQuote* quote,
                                     const std::vector<AoratonIncoSource>& sources);

}  // namespace aura

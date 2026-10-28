#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

// An internet radio station from radio-browser.info (an open, community
// directory). Only MP3 streams are kept: the device decodes MP3 itself.
struct RadioStation {
    std::string id;    // radio-browser stationuuid
    std::string name;
    std::string url;   // last known stream URL
    int bitrate{0};    // kbit/s, 0 when unknown
    std::string aliases;  // folded Greek spellings the assistant may hear
};

enum class RadioState { Idle, Connecting, Playing, Error };

enum class RadioIntentKind { None, Open, PlayStation, Stop };

struct RadioIntent {
    RadioIntentKind kind{RadioIntentKind::None};
    bool explicit_radio{false};
};

struct RadioSnapshot {
    RadioState state{RadioState::Idle};
    RadioStation station;      // the current or last station
    std::string title;         // the stream's "now playing" text, if it sends one
    std::string error_code;
    std::vector<RadioStation> favorites;
    std::vector<RadioStation> results;  // the last search
    bool searching{false};
    std::string query;
    std::string search_error;
    std::uint64_t sequence{0};
};

// What the radio shows and remembers. The platform streams and decodes; this
// model owns the state, the favourites and the rules.
class RadioModel {
public:
    static constexpr std::size_t kMaxFavorites = 24;
    static constexpr std::size_t kMaxResults = 30;

    RadioModel();

    void begin_connect(const RadioStation& station);
    void set_playing();
    void set_title(const std::string& title);
    void fail(const std::string& code);
    void stop();

    // Adds or removes; false when the list is full.
    bool toggle_favorite(const RadioStation& station);
    bool is_favorite(const std::string& id) const;
    // Moves one favourite by one position. Order is persisted by the existing
    // versioned favourites store.
    bool move_favorite(std::size_t index, int direction);

    void begin_search(const std::string& query);
    void search_finished(std::vector<RadioStation> results, const std::string& error);

    // A station the owner named ("βάλε Σφαίρα", "Kiss"): favourites first,
    // then the last search. Null when nothing matches.
    const RadioStation* find_spoken(std::string_view spoken) const;

    // Favourites survive restarts; the default list returns when the file is empty.
    std::string serialize_favorites() const;
    bool restore_favorites(const std::string& data);

    const RadioSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    void changed() noexcept { ++snapshot_.sequence; }

    RadioSnapshot snapshot_{};
};

// Popular Greek MP3 stations, checked against radio-browser on 2026-10-24.
std::vector<RadioStation> greek_default_stations();

// radio-browser JSON (an array of stations) to MP3 stations with http(s) URLs.
bool parse_radio_browser_stations(std::string_view json, std::vector<RadioStation>& out,
                                  std::size_t max_count = RadioModel::kMaxResults);
// The search path for Greek MP3 stations whose name contains `query`.
std::string radio_browser_search_path(std::string_view query, std::size_t limit = RadioModel::kMaxResults);
// The radio-browser servers to try, in order.
std::vector<std::string> radio_browser_hosts();

// Local, deterministic command recognition. The complete utterance is kept so
// RadioModel::find_spoken can match a station without sending a control action
// through the assistant/model path.
RadioIntent analyze_radio_intent(std::string_view text);

// Splits an ICY stream ("Icy-MetaData: 1") into audio bytes and the
// StreamTitle it announces every `metaint` bytes. metaint 0 means no metadata.
class IcyStreamReader {
public:
    explicit IcyStreamReader(std::size_t metaint = 0) : metaint_(metaint) {}
    void feed(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& audio);
    // The newest title since the last call, if one arrived.
    bool take_title(std::string& title);

private:
    void finish_metadata();

    std::size_t metaint_;
    std::size_t audio_left_{0};
    std::size_t meta_left_{0};
    bool in_meta_{false};
    bool started_{false};
    std::string meta_;
    std::string title_;
    bool has_title_{false};
};

// The head of a stream response: "HTTP/1.x 200" or Shoutcast's "ICY 200 OK"
// (which WinHTTP rejects), and its icy-metaint header. False for any other
// status, a malformed head or an absurd metadata interval.
struct StreamResponseHead {
    int status{0};
    std::size_t metaint{0};
};
bool parse_stream_response_head(std::string_view head, StreamResponseHead& out);

// Whether a dropped stream is worth joining again: a network blip or a server
// restart is; a bad URL or a stream that is not MP3 is not.
bool radio_error_retryable(std::string_view code) noexcept;
// The pause before retry `attempt` (0, 1, ...): 1, 2, 4 then 8 seconds; 0
// means give up and tell the owner.
std::uint32_t radio_retry_delay_ms(int attempt) noexcept;

const char* radio_state_name(RadioState state) noexcept;

}  // namespace aura

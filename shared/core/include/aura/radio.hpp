#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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
    // What "βάλε ραδιόφωνο" plays: the station heard last, else the first
    // favourite. Null when there is neither.
    const RadioStation* station_to_resume() const;

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
// The search path for MP3 stations whose name contains `query`: Greek ones,
// or from every country when `worldwide` (the fallback when none is Greek).
std::string radio_browser_search_path(std::string_view query, std::size_t limit = RadioModel::kMaxResults,
                                      bool worldwide = false);
// The radio-browser servers to try, in order.
std::vector<std::string> radio_browser_hosts();

struct RadioSearchResult {
    bool ok{false};
    std::vector<RadioStation> stations;
    std::string error_code;
};

// A directory search by name: Greek stations first and, when the directory
// answers but has no Greek one, the same name worldwide ("BBC Radio 1").
// `fetch` asks the directory for one search path.
RadioSearchResult search_radio_directory(std::string_view query,
                                         const std::function<RadioSearchResult(const std::string&)>& fetch);

// Local, deterministic command recognition. The complete utterance is kept so
// RadioModel::find_spoken can match a station without sending a control action
// through the assistant/model path.
RadioIntent analyze_radio_intent(std::string_view text);

enum class RadioRequestKind { None, Open, Play, Stop };

struct RadioRequest {
    RadioRequestKind kind{RadioRequestKind::None};
    const RadioStation* station{nullptr};  // Play's: in the model, valid until it changes
};

// What a question asks of the radio, decided the same way by the simulator and
// the device: stop it, play a station (the one named, or for "βάλε ραδιόφωνο"
// the last one heard), or only show it. None leaves the question to the
// assistant. A timer, reminder or setting comes first: "βάλε υπενθύμιση ...
// ραδιόφωνο" is a reminder.
RadioRequest radio_request(std::string_view question, const RadioModel& model);

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

// A station's address, split for a socket of the device's own.
struct StreamUrl {
    bool secure{false};
    std::string host;
    std::uint16_t port{0};
    std::string path;  // with its query, "/" at least; never the #fragment
};
// http(s)://host[:port][/path], printable ASCII, at most 2,048 characters. No
// user name and no IPv6 literal: stream addresses do not use them.
bool split_stream_url(std::string_view url, StreamUrl& out);
// The device's request: HTTP/1.0, which Shoutcast v1 answers with "ICY 200
// OK", asking for the ICY titles; the port appears in Host when not the default.
std::string stream_request(const StreamUrl& url);

// A stream server's move: "HTTP/1.x 301|302|303|307|308" with an absolute
// http(s) Location (at most 2,048 printable characters). Anything else,
// including a relative Location, is not followed.
bool parse_stream_redirect(std::string_view head, std::string& location);

// What a radio worker tells the UI thread, on every platform. Reconnecting:
// the stream dropped and is being joined again (text: why).
enum class RadioWorkerEventType { Playing, Title, Reconnecting, Failed };

struct RadioWorkerEvent {
    RadioWorkerEventType type{RadioWorkerEventType::Failed};
    std::uint64_t generation{0};
    std::string text;
};

// The platform's music output (the simulator's SDL channel, the device's
// I2S stream). false: the station changed meanwhile, or the output refused.
class IRadioAudioSink {
public:
    virtual ~IRadioAudioSink() = default;
    virtual bool open(std::uint32_t sample_rate, std::uint8_t channels) = 0;
    virtual bool push(const std::int16_t* samples, std::size_t frames) = 0;
    virtual std::uint32_t buffered_ms() const = 0;
};

// A read that waited without data, and one that failed.
constexpr int kRadioReadTimeout = -1;
constexpr int kRadioReadError = -2;

struct RadioPumpLimits {
    std::uint32_t high_water_ms{3500};  // stop reading above this much audio queued
    std::uint32_t low_water_ms{1800};   // and go on below this
    int max_quiet_reads{4};             // timed-out reads in a row before RADIO_TIMEOUT
    std::size_t max_bytes_before_audio{512U * 1024U};  // then the stream is not MP3
};

struct RadioPumpIo {
    // > 0 bytes read, 0 at the end of the stream, kRadioReadTimeout or kRadioReadError.
    std::function<int(std::uint8_t* buffer, int size)> read;
    std::function<bool()> current;  // still the station asked for
    std::function<void(std::string title)> title;
    std::function<void()> playing;  // the first audio reached the output
    std::function<void(std::uint32_t ms)> wait;  // while the output drains
};

// One connection's stream, the same on every platform: the ICY titles are
// split off, the MP3 is decoded and the output is fed between the water
// marks. Returns why it ended (RADIO_TIMEOUT, RADIO_NETWORK,
// RADIO_STREAM_ENDED, RADIO_DECODE, RADIO_AUDIO), or "" when the station
// changed. Its buffers are on the heap: the device's worker stack is small.
std::string radio_pump(std::size_t metaint, IRadioAudioSink& sink, const RadioPumpIo& io,
                       const RadioPumpLimits& limits, bool& heard_audio);

// Whether a dropped stream is worth joining again: a network blip or a server
// restart is; a bad URL or a stream that is not MP3 is not.
bool radio_error_retryable(std::string_view code) noexcept;
// The pause before retry `attempt` (0, 1, ...): 1, 2, 4 then 8 seconds; 0
// means give up and tell the owner.
std::uint32_t radio_retry_delay_ms(int attempt) noexcept;

const char* radio_state_name(RadioState state) noexcept;

}  // namespace aura

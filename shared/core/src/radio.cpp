#include "aura/radio.hpp"

#include "aura/json_reader.hpp"
#include "aura/text_util.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace aura {
namespace {

constexpr std::size_t kMaxNameBytes = 80;
constexpr std::size_t kMaxUrlBytes = 400;
constexpr std::size_t kMaxTitleBytes = 160;
constexpr std::size_t kMaxQueryBytes = 60;
constexpr std::size_t kMaxIcyMetadata = 16 * 255;

bool stream_url(const std::string& url) {
    return url.size() <= kMaxUrlBytes && (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) &&
           url.find_first_of(" \t\r\n\"<>") == std::string::npos;
}

// Words of a request that name no station: articles, politeness, the request
// verbs and "radio"/"station" themselves ("βάλε μου λίγο τη Σφαίρα").
bool request_filler(const std::string& word) {
    static constexpr std::string_view kFillers[] = {
        "ο", "η", "οι", "το", "τον", "την", "τη", "τα", "του", "της", "στο", "στον", "στην", "στη", "σε",
        "ενα", "εναν", "μια", "μου", "μας", "λιγο", "παρακαλω", "να", "θα", "θελω", "ηθελα", "μπορεισ",
        "ακουσω", "ακουσουμε", "βαλε", "βαλεισ", "παιξε", "παιξεισ", "ανοιξε", "ανοιξεισ", "ραδιο",
        "ραντιο", "σταθμο", "σταθμοσ", "σταθμου", "φμ", "the", "a", "an", "to", "on", "some", "please",
        "radio", "station", "fm", "me", "my", "i", "want", "would", "like", "listen", "could", "can",
        "you", "put", "play", "open", "let", "lets", "us"};
    if (word.rfind("ραδιοφων", 0) == 0) return true;
    for (const auto filler : kFillers) {
        if (word == filler) return true;
    }
    return false;
}

bool numeric_word(const std::string& word) {
    return !word.empty() && std::all_of(word.begin(), word.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || ch == '.' || ch == ',';
    });
}

// The words a station answers to: its name and its spoken aliases.
std::vector<std::string> station_words(const RadioStation& station) {
    auto words = folded_words(fold_text(station.name));
    std::string alias;
    std::istringstream aliases(station.aliases);
    while (std::getline(aliases, alias, '|')) {
        for (auto& word : folded_words(fold_text(alias))) words.push_back(std::move(word));
    }
    return words;
}

// How well the request words name `station`. Every word must belong to the
// station, so "βάλε υπενθύμιση για το τρίτο" names nothing, and one of them
// must be a name rather than a frequency. 0 for no match; otherwise the bytes
// matched, so "τρίτο πρόγραμμα" prefers ΕΡΤ Τρίτο to ΕΡΤ Πρώτο Πρόγραμμα.
std::size_t match_score(const std::vector<std::string>& request, const RadioStation& station) {
    if (request.empty()) return 0;
    const auto words = station_words(station);
    std::size_t score = 0;
    bool named = false;
    for (const auto& word : request) {
        if (std::find(words.begin(), words.end(), word) == words.end()) return 0;
        score += word.size();
        named = named || (!numeric_word(word) && word.size() >= 3);
    }
    return named ? score : 0;
}

std::string url_encode(std::string_view text) {
    static const char* hex = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char ch : text) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' ||
            ch == '_' || ch == '.' || ch == '~') {
            encoded += static_cast<char>(ch);
        } else {
            encoded += '%';
            encoded += hex[ch >> 4];
            encoded += hex[ch & 0x0F];
        }
    }
    return encoded;
}

}  // namespace

std::vector<RadioStation> greek_default_stations() {
    return {
        {"ab044907-7d8f-43ad-bb09-8431207aaf5a", "ΕΡΤ Πρώτο Πρόγραμμα", "https://radiostreaming.ert.gr/ert-proto", 256,
         "πρωτο προγραμμα|ερτ ειδησεις"},
        {"961d3712-0601-11e8-ae97-52543be04c81", "ΕΡΤ Τρίτο 102 FM", "http://radiostreaming.ert.gr/ert-102fm", 192,
         "τριτο προγραμμα"},
        {"b4489557-58f3-4971-bc3a-c57c9a6b497d", "ΕΡΤ Kosmos", "https://radiostreaming.ert.gr/ert-kosmos", 256, "κοσμος"},
        {"605b2521-e764-42ba-8be0-473fc096a3b3", "Zeppelin 106.7", "https://radiostreaming.ert.gr/ert-zeppelin", 192,
         "ζεπελιν"},
        {"2d9dfa25-7379-460b-8cd0-fe163af26ebe", "SKAI 100.3", "https://skai.live24.gr/skai1003", 192, "σκαι|σκάι"},
        {"9611d448-0601-11e8-ae97-52543be04c81", "Sfera 102.2", "http://sfera.live24.gr/sfera4132", 128, "σφαιρα"},
        {"d0b872a1-1015-4e6c-8e29-36e00ba9667a", "Kiss 92.9", "https://kissfm.live24.gr/kissfmathens", 192, "κισ|κις"},
        {"961f9a79-0601-11e8-ae97-52543be04c81", "Real FM 97.8", "http://netradio.live24.gr/realfm", 192, "ρεαλ|ριαλ"},
        {"b7e57990-b3ad-44a9-9458-2f8f58dcbde2", "Rock FM 96.9", "https://az10.yesstreaming.net:8060/radio.mp3", 320,
         "ροκ"},
        {"960c2c57-0601-11e8-ae97-52543be04c81", "Love Radio 97.5", "http://netradio.live24.gr/loveradio-1000", 192,
         "λαβ|λοβ"},
        {"94ab0296-5b88-49e6-b33a-a1823a3f6b2d", "Athens Deejay 95.2", "http://netradio.live24.gr/athensdeejay", 192,
         "ντιτζει|ντι τζει"},
        {"a115daa5-3640-430e-8890-8fa2ae065743", "Easy 97.2", "https://easy972.live24.gr/easy972", 128, "ιζι"},
        {"b0907d78-8536-4327-9a26-d1a2c70d464f", "Pepper 96.6", "http://pepper966.live24.gr/pepper9660", 192, "πεπερ"},
        {"8f174c53-5547-44d2-bb92-75a985cdb5ef", "Parapolitika 90.1", "https://netradio.live24.gr/athinaradio", 128,
         "παραπολιτικα"},
        {"96185f35-0601-11e8-ae97-52543be04c81", "Sport FM 94.6", "http://netradio.live24.gr/sportfm7712", 128,
         "σπορ"},
    };
}

std::vector<std::string> radio_browser_hosts() {
    return {"de1.api.radio-browser.info", "de2.api.radio-browser.info", "fi1.api.radio-browser.info"};
}

RadioIntent analyze_radio_intent(std::string_view text) {
    const auto folded = fold_text(text);
    const auto words = folded_words(folded);
    const bool radio = folded_has_word_prefix(words, {"ραδιοφων"}) || folded_has_word(words, {"radio", "ραδιο", "ραντιο"});
    const bool stop = folded_has_word_prefix(words, {"σταματ"}) ||
                      folded_has_word(words, {"κλεισε", "κλεισεισ", "σβησε", "σβησεισ", "stop"}) ||
                      folded_contains_phrase(folded, "turn off");
    if (radio && stop) return {RadioIntentKind::Stop, true};
    const bool play = folded_has_word(words, {"βαλε", "βαλεισ", "παιξε", "παιξεισ", "ανοιξε", "ανοιξεισ", "play", "open"}) ||
                      folded_contains_phrase(folded, "put on") || folded_contains_phrase(folded, "turn on");
    if (play) return {RadioIntentKind::PlayStation, radio};
    // Without a request verb only a bare "ραδιόφωνο (παρακαλώ)" is a command; a
    // sentence or question about the radio belongs to the assistant.
    if (radio && std::all_of(words.begin(), words.end(), request_filler)) return {RadioIntentKind::Open, true};
    return {};
}

bool parse_stream_response_head(std::string_view head, StreamResponseHead& out) {
    out = {};
    const auto line_end = head.find("\r\n");
    const auto status_line = head.substr(0, line_end);
    std::string_view status;
    if (status_line.rfind("ICY ", 0) == 0) status = status_line.substr(4);
    else if (status_line.rfind("HTTP/1.0 ", 0) == 0 || status_line.rfind("HTTP/1.1 ", 0) == 0) status = status_line.substr(9);
    else return false;
    if (status.substr(0, 3) != "200" || (status.size() > 3 && status[3] != ' ')) return false;
    out.status = 200;
    if (line_end == std::string_view::npos) return true;
    std::size_t position = line_end + 2;
    while (position < head.size()) {
        const auto end = head.find("\r\n", position);
        const auto line = head.substr(position, end == std::string_view::npos ? std::string_view::npos : end - position);
        if (line.empty()) break;
        const auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string name(line.substr(0, colon));
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
            });
            if (name == "icy-metaint") {
                auto value = line.substr(colon + 1);
                while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
                if (value.empty()) return false;
                std::uint64_t parsed = 0;
                for (const char ch : value) {
                    if (ch < '0' || ch > '9') return false;
                    parsed = parsed * 10U + static_cast<std::uint64_t>(ch - '0');
                    if (parsed > 16U * 1024U * 1024U) return false;
                }
                out.metaint = static_cast<std::size_t>(parsed);
            }
        }
        if (end == std::string_view::npos) break;
        position = end + 2;
    }
    return true;
}

bool radio_error_retryable(std::string_view code) noexcept {
    return code == "RADIO_NETWORK" || code == "RADIO_STREAM_ENDED" || code == "RADIO_TIMEOUT";
}

std::uint32_t radio_retry_delay_ms(int attempt) noexcept {
    return attempt >= 0 && attempt < 4 ? 1000U << attempt : 0U;
}

RadioSearchResult search_radio_directory(std::string_view query,
                                         const std::function<RadioSearchResult(const std::string&)>& fetch) {
    auto result = fetch(radio_browser_search_path(query));
    if (result.ok && result.stations.empty()) {
        result = fetch(radio_browser_search_path(query, RadioModel::kMaxResults, true));
    }
    return result;
}

std::string radio_browser_search_path(std::string_view query, std::size_t limit, bool worldwide) {
    const auto clean = utf8_truncate(sanitize_text(query, kMaxQueryBytes * 2), kMaxQueryBytes);
    char tail[112];
    std::snprintf(tail, sizeof(tail), "%s&codec=MP3&hidebroken=true&order=clickcount&reverse=true&limit=%u",
                  worldwide ? "" : "&countrycode=GR",
                  static_cast<unsigned>(std::min<std::size_t>(limit, RadioModel::kMaxResults)));
    return "/json/stations/search?name=" + url_encode(clean) + tail;
}

bool parse_radio_browser_stations(std::string_view json, std::vector<RadioStation>& out, std::size_t max_count) {
    out.clear();
    JsonValue root;
    JsonLimits limits;
    limits.max_bytes = 512 * 1024;
    limits.max_nodes = 32768;
    if (!parse_json(json, root, limits) || root.type() != JsonType::Array) return false;
    for (const auto& item : root.items()) {
        if (out.size() >= max_count) break;
        if (item.type() != JsonType::Object) continue;
        if (fold_text(item.string_at("codec")) != "mp3") continue;
        RadioStation station;
        station.id = utf8_truncate(item.string_at("stationuuid"), 40);
        station.name = utf8_truncate(sanitize_text(item.string_at("name"), kMaxNameBytes * 2), kMaxNameBytes);
        station.url = item.string_at("url_resolved");
        if (!stream_url(station.url)) station.url = item.string_at("url");
        if (const auto* bitrate = item.find("bitrate")) {
            station.bitrate = static_cast<int>(std::clamp<std::int64_t>(bitrate->as_int(0), 0, 2000));
        }
        if (station.id.empty() || station.name.empty() || !stream_url(station.url)) continue;
        const bool duplicate = std::any_of(out.begin(), out.end(), [&](const RadioStation& known) {
            return known.id == station.id || known.url == station.url;
        });
        if (!duplicate) out.push_back(std::move(station));
    }
    return true;
}

RadioModel::RadioModel() {
    snapshot_.favorites = greek_default_stations();
    changed();
}

void RadioModel::begin_connect(const RadioStation& station) {
    snapshot_.state = RadioState::Connecting;
    snapshot_.station = station;
    snapshot_.title.clear();
    snapshot_.error_code.clear();
    changed();
}

void RadioModel::set_playing() {
    if (snapshot_.state != RadioState::Connecting) return;
    snapshot_.state = RadioState::Playing;
    changed();
}

void RadioModel::set_title(const std::string& title) {
    auto clean = utf8_truncate(sanitize_text(title, kMaxTitleBytes * 2), kMaxTitleBytes);
    if (snapshot_.state == RadioState::Idle || clean == snapshot_.title) return;
    snapshot_.title = std::move(clean);
    changed();
}

void RadioModel::fail(const std::string& code) {
    if (snapshot_.state == RadioState::Idle) return;
    snapshot_.state = RadioState::Error;
    snapshot_.error_code = code.empty() ? "RADIO_FAILED" : utf8_truncate(code, 40);
    snapshot_.title.clear();
    changed();
}

void RadioModel::stop() {
    if (snapshot_.state == RadioState::Idle) return;
    snapshot_.state = RadioState::Idle;
    snapshot_.title.clear();
    snapshot_.error_code.clear();
    changed();
}

bool RadioModel::is_favorite(const std::string& id) const {
    return std::any_of(snapshot_.favorites.begin(), snapshot_.favorites.end(),
                       [&](const RadioStation& station) { return station.id == id; });
}

bool RadioModel::toggle_favorite(const RadioStation& station) {
    auto& favorites = snapshot_.favorites;
    const auto found = std::find_if(favorites.begin(), favorites.end(),
                                    [&](const RadioStation& item) { return item.id == station.id; });
    if (found != favorites.end()) {
        favorites.erase(found);
    } else {
        if (favorites.size() >= kMaxFavorites || station.id.empty() || !stream_url(station.url)) return false;
        favorites.push_back(station);
    }
    changed();
    return true;
}

bool RadioModel::move_favorite(std::size_t index, int direction) {
    auto& favorites = snapshot_.favorites;
    if (index >= favorites.size() || (direction != -1 && direction != 1)) return false;
    if ((direction < 0 && index == 0) || (direction > 0 && index + 1 >= favorites.size())) return false;
    const auto target = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(index) + direction);
    std::swap(favorites[index], favorites[target]);
    changed();
    return true;
}

void RadioModel::begin_search(const std::string& query) {
    snapshot_.query = utf8_truncate(sanitize_text(query, kMaxQueryBytes * 2), kMaxQueryBytes);
    snapshot_.searching = true;
    snapshot_.search_error.clear();
    changed();
}

void RadioModel::search_finished(std::vector<RadioStation> results, const std::string& error) {
    if (results.size() > kMaxResults) results.resize(kMaxResults);
    snapshot_.results = std::move(results);
    snapshot_.searching = false;
    snapshot_.search_error = error;
    changed();
}

const RadioStation* RadioModel::find_spoken(std::string_view spoken) const {
    std::vector<std::string> request;
    for (auto& word : folded_words(fold_text(spoken))) {
        if (!request_filler(word)) request.push_back(std::move(word));
    }
    const RadioStation* best = nullptr;
    std::size_t best_score = 0;
    for (const auto* list : {&snapshot_.favorites, &snapshot_.results}) {
        for (const auto& station : *list) {
            const auto score = match_score(request, station);
            if (score > best_score) {
                best = &station;
                best_score = score;
            }
        }
    }
    return best;
}

const RadioStation* RadioModel::station_to_resume() const {
    if (!snapshot_.station.id.empty() && stream_url(snapshot_.station.url)) return &snapshot_.station;
    return snapshot_.favorites.empty() ? nullptr : &snapshot_.favorites.front();
}

std::string RadioModel::serialize_favorites() const {
    std::string text = "aura-radio 1\n";
    for (const auto& station : snapshot_.favorites) {
        char bitrate[16];
        std::snprintf(bitrate, sizeof(bitrate), "%d", station.bitrate);
        text += station.id + '\t' + bitrate + '\t' + station.url + '\t' + station.name + '\t' + station.aliases + '\n';
    }
    return text;
}

bool RadioModel::restore_favorites(const std::string& data) {
    std::istringstream input(data);
    std::string line;
    if (!std::getline(input, line) || line != "aura-radio 1") return false;
    std::vector<RadioStation> favorites;
    while (std::getline(input, line) && favorites.size() < kMaxFavorites) {
        std::vector<std::string> fields;
        std::istringstream row(line);
        std::string field;
        while (std::getline(row, field, '\t')) fields.push_back(field);
        if (fields.size() < 4) continue;
        RadioStation station;
        station.id = utf8_truncate(fields[0], 40);
        station.bitrate = std::clamp(std::atoi(fields[1].c_str()), 0, 2000);
        station.url = fields[2];
        station.name = utf8_truncate(sanitize_text(fields[3], kMaxNameBytes * 2), kMaxNameBytes);
        if (fields.size() > 4) station.aliases = utf8_truncate(sanitize_text(fields[4], 200), 200);
        if (station.id.empty() || station.name.empty() || !stream_url(station.url)) continue;
        favorites.push_back(std::move(station));
    }
    snapshot_.favorites = std::move(favorites);
    changed();
    return true;
}

void IcyStreamReader::feed(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& audio) {
    if (metaint_ == 0) {
        audio.insert(audio.end(), data, data + size);
        return;
    }
    if (!started_) {
        audio_left_ = metaint_;
        started_ = true;
    }
    std::size_t position = 0;
    while (position < size) {
        if (!in_meta_) {
            if (audio_left_ == 0) {
                // The length byte: metadata follows in 16-byte blocks.
                meta_left_ = static_cast<std::size_t>(data[position++]) * 16U;
                meta_.clear();
                in_meta_ = meta_left_ > 0;
                if (!in_meta_) audio_left_ = metaint_;
                continue;
            }
            const auto take = std::min(audio_left_, size - position);
            audio.insert(audio.end(), data + position, data + position + take);
            audio_left_ -= take;
            position += take;
        } else {
            const auto take = std::min(meta_left_, size - position);
            if (meta_.size() + take <= kMaxIcyMetadata) meta_.append(reinterpret_cast<const char*>(data + position), take);
            meta_left_ -= take;
            position += take;
            if (meta_left_ == 0) {
                finish_metadata();
                in_meta_ = false;
                audio_left_ = metaint_;
            }
        }
    }
}

void IcyStreamReader::finish_metadata() {
    const std::string key = "StreamTitle='";
    const auto start = meta_.find(key);
    if (start == std::string::npos) return;
    const auto begin = start + key.size();
    auto end = meta_.find("';", begin);
    if (end == std::string::npos) end = meta_.find('\'', begin);
    if (end == std::string::npos) return;
    title_ = utf8_truncate(sanitize_text(meta_.substr(begin, end - begin), kMaxTitleBytes * 2), kMaxTitleBytes);
    has_title_ = true;
}

bool IcyStreamReader::take_title(std::string& title) {
    if (!has_title_) return false;
    title = title_;
    has_title_ = false;
    return true;
}

const char* radio_state_name(RadioState state) noexcept {
    switch (state) {
        case RadioState::Idle: return "IDLE";
        case RadioState::Connecting: return "CONNECTING";
        case RadioState::Playing: return "PLAYING";
        case RadioState::Error: return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace aura

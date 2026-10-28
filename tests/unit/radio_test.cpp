#include "aura/mp3_decoder.hpp"
#include "aura/radio.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// A silent MPEG-1 Layer III frame: 128 kbit/s, 44.1 kHz, stereo, empty side
// information. 144 * 128000 / 44100 = 417 bytes.
std::vector<std::uint8_t> silent_frames(int count) {
    std::vector<std::uint8_t> bytes;
    for (int index = 0; index < count; ++index) {
        std::vector<std::uint8_t> frame(417, 0);
        frame[0] = 0xFF;
        frame[1] = 0xFB;
        frame[2] = 0x90;
        frame[3] = 0x00;
        bytes.insert(bytes.end(), frame.begin(), frame.end());
    }
    return bytes;
}

void station_tests() {
    using namespace aura;
    const auto defaults = greek_default_stations();
    expect(defaults.size() >= 12, "a useful default list");
    for (const auto& station : defaults) {
        expect(!station.id.empty() && !station.name.empty() && station.url.rfind("http", 0) == 0,
               "every default station has an id, a name and an http(s) URL");
    }

    const std::string json = R"json([
      {"stationuuid":"a1","name":"Test FM 99.1","url":"http://a/x","url_resolved":"https://a/stream","codec":"MP3","bitrate":192},
      {"stationuuid":"a2","name":"AAC Only","url_resolved":"https://b/stream","codec":"AAC+","bitrate":64},
      {"stationuuid":"a3","name":"Bad URL","url_resolved":"ftp://c/stream","url":"javascript:x","codec":"MP3"},
      {"stationuuid":"a4","name":"Copy","url_resolved":"https://a/stream","codec":"mp3"},
      {"stationuuid":"","name":"No id","url_resolved":"https://d/stream","codec":"MP3"},
      "junk"
    ])json";
    std::vector<RadioStation> parsed;
    expect(parse_radio_browser_stations(json, parsed), "radio-browser JSON parses");
    expect(parsed.size() == 1 && parsed[0].id == "a1" && parsed[0].url == "https://a/stream" &&
               parsed[0].bitrate == 192,
           "only MP3 stations with an id and an http(s) URL, without duplicates");
    expect(!parse_radio_browser_stations("{\"not\":\"an array\"}", parsed) && parsed.empty(),
           "an object is not a station list");

    const auto path = radio_browser_search_path("Σφαίρα & co");
    expect(path.rfind("/json/stations/search?name=%CE%A3%CF%86", 0) == 0, "the query is percent-encoded UTF-8");
    expect(path.find("countrycode=GR") != std::string::npos && path.find("codec=MP3") != std::string::npos &&
               path.find("%26") != std::string::npos,
           "Greek MP3 stations only, and '&' cannot inject a parameter");

    expect(analyze_radio_intent("Άνοιξε το ραδιόφωνο").kind == RadioIntentKind::PlayStation,
           "Greek open-radio intent");
    expect(analyze_radio_intent("Βάλε ραδιόφωνο Sfera").kind == RadioIntentKind::PlayStation,
           "Greek station intent");
    expect(analyze_radio_intent("Σταμάτα το ραδιόφωνο").kind == RadioIntentKind::Stop,
           "Greek stop-radio intent");
    expect(analyze_radio_intent("Πες μου για τον Ταΰγετο").kind == RadioIntentKind::None,
           "unrelated Greek text is not a radio command");
    expect(analyze_radio_intent("Show the display settings").kind == RadioIntentKind::None,
           "'display' is not 'play'");
    expect(analyze_radio_intent("Reopen the notes").kind == RadioIntentKind::None, "'reopen' is not 'open'");
    expect(analyze_radio_intent("Πες μου την ιστορία του ραδιοφώνου").kind == RadioIntentKind::None,
           "a sentence about radio is not a command");
    expect(analyze_radio_intent("Τι είναι το ραδιόφωνο;").kind == RadioIntentKind::None,
           "a question about radio is not a command");
    expect(analyze_radio_intent("Ραδιόφωνο").kind == RadioIntentKind::Open, "the word alone opens the radio");
    const auto polite = analyze_radio_intent("Μπορείς να βάλεις ραδιόφωνο;");
    expect(polite.kind == RadioIntentKind::PlayStation && polite.explicit_radio,
           "a polite question still asks for the radio");
}

void model_tests() {
    using namespace aura;
    RadioModel model;
    const auto& snap = model.snapshot();
    expect(snap.favorites.size() == greek_default_stations().size(), "the defaults are the first favourites");

    const auto* sfera = model.find_spoken("Βάλε τη Σφαίρα");
    expect(sfera && sfera->name.find("Sfera") == 0, "a Greek spelling finds the station");
    const auto* kiss = model.find_spoken("play kiss fm please");
    expect(kiss && kiss->name.find("Kiss") == 0, "a Latin name finds the station");
    expect(model.find_spoken("σταμάτα το ραδιόφωνο") == nullptr, "a stop request names no station");
    expect(model.find_spoken("92.9") == nullptr, "a frequency alone is not a name");
    expect(model.find_spoken("βάλε υπενθύμιση για την παραλαβή") == nullptr, "'παραλαβή' is not Love Radio");
    expect(model.find_spoken("βάλε στο πρόγραμμα ραντεβού") == nullptr,
           "a calendar sentence is not ΕΡΤ Πρώτο Πρόγραμμα");
    expect(model.find_spoken("βάλε υπενθύμιση για το τρίτο") == nullptr, "an ordinal is not ΕΡΤ Τρίτο");
    expect(model.find_spoken("βάλε ένα κοντσέρτο") == nullptr, "'κοντσέρτο' does not name ΕΡΤ");
    const auto* third = model.find_spoken("βάλε το τρίτο πρόγραμμα");
    expect(third && third->name.find("ΕΡΤ Τρίτο") == 0, "an alias still finds ΕΡΤ Τρίτο");
    const auto* love = model.find_spoken("βάλε λαβ ράντιο");
    expect(love && love->name.find("Love") == 0, "a spoken alias still finds Love Radio");
    const auto* skai = model.find_spoken("θέλω να ακούσω ΣΚΑΪ 100.3");
    expect(skai && skai->name.find("SKAI") == 0, "a name with its frequency finds the station");

    const auto before = snap.sequence;
    model.begin_connect(*kiss);
    expect(snap.state == RadioState::Connecting && snap.station.name.find("Kiss") == 0 && snap.sequence > before,
           "connecting shows the station");
    model.set_title("Artist - Song\x01");
    expect(snap.title == "Artist - Song", "the title is cleaned");
    model.set_playing();
    expect(snap.state == RadioState::Playing, "playing");
    model.fail("STREAM_ENDED");
    expect(snap.state == RadioState::Error && snap.error_code == "STREAM_ENDED" && snap.title.empty(),
           "a failure says why");
    model.stop();
    expect(snap.state == RadioState::Idle && snap.error_code.empty(), "stop clears the error");
    model.set_playing();
    expect(snap.state == RadioState::Idle, "nothing plays without a connection first");

    RadioStation extra{"x1", "Extra 1", "https://e/1", 128, {}};
    expect(model.toggle_favorite(extra) && model.is_favorite("x1"), "a favourite is added");
    const auto second_id = model.snapshot().favorites[1].id;
    expect(model.move_favorite(1, -1) && model.snapshot().favorites[0].id == second_id,
           "a favourite moves up");
    expect(!model.move_favorite(0, -1) && !model.move_favorite(99, 1),
           "reorder refuses an edge and an invalid index");
    const auto saved = model.serialize_favorites();
    expect(model.toggle_favorite(extra) && !model.is_favorite("x1"), "and removed");
    RadioModel restored;
    expect(restored.restore_favorites(saved) && restored.is_favorite("x1") &&
               restored.snapshot().favorites[0].id == second_id &&
               restored.snapshot().favorites.size() == greek_default_stations().size() + 1,
           "favourites and their order survive a restart");
    expect(!restored.restore_favorites("not a radio file") && restored.is_favorite("x1"),
           "a foreign file changes nothing");
    expect(restored.restore_favorites("aura-radio 1\nid\t0\tjavascript:x\tBad\t\n") &&
               restored.snapshot().favorites.empty(),
           "a stored bad URL is dropped");

    model.begin_search("rock");
    expect(snap.searching && snap.query == "rock", "searching");
    model.search_finished({{"r1", "Classic Rock Greece", "https://r/1", 128, {}}}, {});
    expect(!snap.searching && snap.results.size() == 1, "results arrive");
    const auto* found = model.find_spoken("βάλε classic rock");
    expect(found != nullptr, "a search result can be named too");
}

void icy_tests() {
    using namespace aura;
    // Shoutcast v1 answers "ICY 200 OK"; the stream head is read either way.
    StreamResponseHead head;
    expect(parse_stream_response_head("ICY 200 OK\r\nicy-name: Test\r\nIcy-MetaInt: 8192\r\n\r\n", head) &&
               head.status == 200 && head.metaint == 8192,
           "Shoutcast's ICY status is accepted with its metadata interval");
    expect(parse_stream_response_head("HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n", head) &&
               head.metaint == 0,
           "HTTP 200 without metadata");
    expect(!parse_stream_response_head("HTTP/1.1 404 Not Found\r\n\r\n", head), "an error status is refused");
    expect(!parse_stream_response_head("garbage", head), "garbage is refused");
    expect(!parse_stream_response_head("ICY 200 OK\r\nicy-metaint: 99999999999\r\n\r\n", head),
           "an absurd metadata interval is refused");

    // A dropped stream is joined again a few times; a bad station is not.
    expect(radio_error_retryable("RADIO_STREAM_ENDED") && radio_error_retryable("RADIO_NETWORK") &&
               !radio_error_retryable("RADIO_INVALID_URL") && !radio_error_retryable("RADIO_DECODE"),
           "only network failures are retried");
    expect(radio_retry_delay_ms(0) == 1000 && radio_retry_delay_ms(3) == 8000 && radio_retry_delay_ms(4) == 0,
           "retries back off, then give up");

    // metaint 4: four audio bytes, a length byte, metadata, then audio again.
    std::string meta = "StreamTitle='Νίκος - Τραγούδι';";
    meta.resize(48, '\0');  // UTF-8 Greek: 44 bytes, three 16-byte blocks
    std::vector<std::uint8_t> stream = {1, 2, 3, 4, 3};
    stream.insert(stream.end(), meta.begin(), meta.end());
    stream.insert(stream.end(), {5, 6, 7, 8, 0, 9, 10});
    IcyStreamReader reader(4);
    std::vector<std::uint8_t> audio;
    // Odd chunk sizes cross every boundary.
    for (std::size_t position = 0; position < stream.size(); position += 3) {
        const auto take = std::min<std::size_t>(3, stream.size() - position);
        reader.feed(stream.data() + position, take, audio);
    }
    expect(audio == std::vector<std::uint8_t>({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), "metadata is removed from the audio");
    std::string title;
    expect(reader.take_title(title) && title == "Νίκος - Τραγούδι", "the title is read");
    expect(!reader.take_title(title), "a title is handed over once");

    IcyStreamReader plain(0);
    audio.clear();
    plain.feed(stream.data(), stream.size(), audio);
    expect(audio.size() == stream.size(), "without metaint every byte is audio");
}

void mp3_tests() {
    using namespace aura;
    const auto bytes = silent_frames(12);
    Mp3StreamDecoder decoder;
    Mp3Pcm pcm;
    // Arrives in uneven pieces, as from a socket.
    for (std::size_t position = 0; position < bytes.size(); position += 700) {
        decoder.feed(bytes.data() + position, std::min<std::size_t>(700, bytes.size() - position));
        decoder.decode(pcm);
    }
    decoder.decode(pcm, 1152 * 64, true);
    expect(pcm.sample_rate == 44100 && pcm.channels == 2, "44.1 kHz stereo");
    expect(pcm.samples.size() == 1152U * 2U * 12U, "every frame decodes, whatever the chunk size");
    bool silent = true;
    for (const auto sample : pcm.samples) silent = silent && sample == 0;
    expect(silent, "silence decodes to silence");

    Mp3StreamDecoder junk;
    std::vector<std::uint8_t> noise(200000);
    for (std::size_t index = 0; index < noise.size(); ++index) noise[index] = static_cast<std::uint8_t>(index * 37 + 11);
    Mp3Pcm none;
    junk.feed(noise.data(), noise.size());
    junk.decode(none);
    expect(none.samples.size() < 1152U * 4U, "noise is not music");
}

}  // namespace

int main() {
    station_tests();
    model_tests();
    icy_tests();
    mp3_tests();
    if (failures == 0) std::cout << "aura_radio_tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

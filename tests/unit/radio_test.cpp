#include "aura/mp3_decoder.hpp"
#include "aura/radio.hpp"

#include <algorithm>
#include <functional>
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
    const auto worldwide = radio_browser_search_path("BBC Radio", RadioModel::kMaxResults, true);
    expect(worldwide.find("countrycode") == std::string::npos && worldwide.find("codec=MP3") != std::string::npos,
           "the worldwide search drops only the country");

    // A name with no Greek station is looked up worldwide; a Greek hit or a
    // directory failure is asked once.
    std::vector<std::string> asked;
    const auto found = search_radio_directory("BBC Radio 1", [&](const std::string& path) {
        asked.push_back(path);
        RadioSearchResult reply;
        reply.ok = true;
        if (path.find("countrycode=GR") == std::string::npos) reply.stations.push_back(greek_default_stations().front());
        return reply;
    });
    expect(found.ok && found.stations.size() == 1 && asked.size() == 2 &&
               asked[0].find("countrycode=GR") != std::string::npos &&
               asked[1].find("countrycode") == std::string::npos,
           "no Greek station: the same name is searched worldwide");
    asked.clear();
    const auto greek = search_radio_directory("Sfera", [&](const std::string& path) {
        asked.push_back(path);
        RadioSearchResult reply;
        reply.ok = true;
        reply.stations.push_back(greek_default_stations().front());
        return reply;
    });
    expect(greek.ok && greek.stations.size() == 1 && asked.size() == 1, "a Greek hit is asked once");
    asked.clear();
    const auto failed = search_radio_directory("Sfera", [&](const std::string& path) {
        asked.push_back(path);
        RadioSearchResult reply;
        reply.error_code = "RADIO_DIRECTORY_UNAVAILABLE";
        return reply;
    });
    expect(!failed.ok && failed.error_code == "RADIO_DIRECTORY_UNAVAILABLE" && asked.size() == 1,
           "a directory failure is not asked again worldwide");

    // "Βάλε ραδιόφωνο" plays: the station heard last, else the first favourite.
    RadioModel resume;
    expect(resume.station_to_resume() && resume.station_to_resume()->id == resume.snapshot().favorites.front().id,
           "a first radio request plays the first favourite");
    const auto heard = resume.snapshot().favorites.at(2);
    resume.begin_connect(heard);
    resume.stop();
    expect(resume.station_to_resume() && resume.station_to_resume()->id == heard.id,
           "afterwards it plays the station heard last");
    RadioModel empty;
    empty.restore_favorites("aura-radio 1\n");
    expect(!empty.station_to_resume(), "nothing to resume without stations");

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


// The platform's music output, stood in: it drains `wait` milliseconds at a time.
struct FakeSink final : aura::IRadioAudioSink {
    std::uint32_t rate{0};
    std::uint8_t channels{0};
    std::size_t frames{0};
    std::uint32_t buffered{0};
    std::uint32_t peak{0};
    bool refuse{false};
    bool open(std::uint32_t sample_rate, std::uint8_t channel_count) override {
        if (refuse) return false;
        rate = sample_rate;
        channels = channel_count;
        return true;
    }
    bool push(const std::int16_t* samples, std::size_t count) override {
        if (!samples || refuse) return false;
        frames += count;
        buffered += static_cast<std::uint32_t>(count * 1000U / rate);
        peak = std::max(peak, buffered);
        return true;
    }
    std::uint32_t buffered_ms() const override { return buffered; }
};

// An ICY stream of `count` silent frames with a title every `metaint` bytes.
std::vector<std::uint8_t> icy_stream(int count, std::size_t metaint, const std::string& title) {
    const auto audio = silent_frames(count);
    std::string meta = "StreamTitle='" + title + "';";
    meta.resize((meta.size() + 15) / 16 * 16, '\0');
    std::vector<std::uint8_t> stream;
    for (std::size_t at = 0; at < audio.size(); at += metaint) {
        const auto take = std::min(metaint, audio.size() - at);
        stream.insert(stream.end(), audio.begin() + static_cast<std::ptrdiff_t>(at),
                      audio.begin() + static_cast<std::ptrdiff_t>(at + take));
        if (take == metaint) {
            stream.push_back(static_cast<std::uint8_t>(meta.size() / 16));
            stream.insert(stream.end(), meta.begin(), meta.end());
        }
    }
    return stream;
}

void pump_tests() {
    using namespace aura;
    RadioPumpLimits limits;
    limits.high_water_ms = 300;
    limits.low_water_ms = 100;
    limits.max_quiet_reads = 3;
    limits.max_bytes_before_audio = 64U * 1024U;

    {
        // A whole stream: titles go to the owner, MP3 to the output, which is
        // kept between the water marks; the end of the stream ends the pump.
        const auto stream = icy_stream(40, 1000, "Νίκος - Τραγούδι");
        std::size_t at = 0;
        FakeSink sink;
        std::vector<std::string> titles;
        int playing = 0;
        int waits = 0;
        RadioPumpIo io;
        io.read = [&](std::uint8_t* buffer, int size) {
            const auto take = std::min<std::size_t>({stream.size() - at, static_cast<std::size_t>(size), 777});
            std::copy_n(stream.data() + at, take, buffer);
            at += take;
            return static_cast<int>(take);
        };
        io.current = [] { return true; };
        io.title = [&](std::string title) { titles.push_back(std::move(title)); };
        io.playing = [&] { ++playing; };
        io.wait = [&](std::uint32_t ms) {
            ++waits;
            sink.buffered = sink.buffered > ms ? sink.buffered - ms : 0;
        };
        bool heard = false;
        const auto code = radio_pump(1000, sink, io, limits, heard);
        expect(code == "RADIO_STREAM_ENDED" && heard && playing == 1, "the end of a heard stream is reported once");
        expect(!titles.empty() && titles.front() == "Νίκος - Τραγούδι", "the title reaches the owner");
        expect(sink.rate == 44100 && sink.channels == 2, "the output opens at the stream's format");
        // The decoder holds back two of the largest frames (2,882 bytes) for more
        // data; at the end of a live stream they are not drained (about 0.2 s).
        expect(sink.frames == 1152U * 34U, "every frame reaches the output, but the few the decoder holds back");
        expect(waits > 0 && sink.peak <= limits.high_water_ms + 1152U * 8U * 1000U / 44100U,
               "the output is kept near the high water mark");
    }

    const auto run = [&](const std::function<int(std::uint8_t*, int)>& read, FakeSink& sink, bool& heard,
                         const std::function<bool()>& current = [] { return true; }) {
        RadioPumpIo io;
        io.read = read;
        io.current = current;
        io.title = [](std::string) {};
        io.playing = [] {};
        io.wait = [&](std::uint32_t ms) { sink.buffered = sink.buffered > ms ? sink.buffered - ms : 0; };
        return radio_pump(0, sink, io, limits, heard);
    };
    {
        FakeSink sink;
        bool heard = false;
        std::size_t sent = 0;
        const auto code = run([&](std::uint8_t* buffer, int size) {
            for (int index = 0; index < size; ++index) buffer[index] = static_cast<std::uint8_t>(sent + index * 37 + 11);
            sent += static_cast<std::size_t>(size);
            return size;
        }, sink, heard);
        expect(code == "RADIO_DECODE" && !heard && sink.frames == 0, "a stream that is not MP3 is refused");
    }
    {
        FakeSink sink;
        bool heard = false;
        int reads = 0;
        expect(run([&](std::uint8_t*, int) { ++reads; return kRadioReadTimeout; }, sink, heard) == "RADIO_TIMEOUT" &&
                   reads == 3,
               "a silent server ends the attempt after the quiet reads");
        expect(run([](std::uint8_t*, int) { return kRadioReadError; }, sink, heard) == "RADIO_NETWORK",
               "a broken connection is a network error");
        expect(run([](std::uint8_t*, int) { return 0; }, sink, heard) == "RADIO_DECODE",
               "a stream that ends before any audio is not playable");
    }
    {
        // Another station was chosen: the pump stops without a code and feeds nothing more.
        const auto stream = silent_frames(40);
        std::size_t at = 0;
        bool current = true;
        FakeSink sink;
        bool heard = false;
        const auto code = run([&](std::uint8_t* buffer, int size) {
            const auto take = std::min<std::size_t>({stream.size() - at, static_cast<std::size_t>(size), 4000});
            std::copy_n(stream.data() + at, take, buffer);
            at += take;
            if (at > 6000) current = false;
            return static_cast<int>(take);
        }, sink, heard, [&] { return current; });
        const auto frames = sink.frames;
        expect(code.empty() && frames < 1152U * 40U, "a changed station ends the pump quietly");
    }
    {
        FakeSink sink;
        sink.refuse = true;
        const auto stream = silent_frames(20);
        std::size_t at = 0;
        bool heard = false;
        const auto code = run([&](std::uint8_t* buffer, int size) {
            const auto take = std::min<std::size_t>(stream.size() - at, static_cast<std::size_t>(size));
            std::copy_n(stream.data() + at, take, buffer);
            at += take;
            return static_cast<int>(take);
        }, sink, heard);
        expect(code == "RADIO_AUDIO" && !heard, "an output that refuses the audio is reported");
    }

    std::string location;
    expect(parse_stream_redirect("HTTP/1.1 302 Found\r\nServer: x\r\nlocation:  https://cdn.example/live.mp3 \r\n\r\n",
                                 location) &&
               location == "https://cdn.example/live.mp3",
           "a redirect gives its new address");
    expect(parse_stream_redirect("HTTP/1.0 301 Moved\r\nLocation: http://other.example:8000/;\r\n\r\n", location) &&
               location == "http://other.example:8000/;",
           "a permanent move too");
    expect(!parse_stream_redirect("HTTP/1.1 200 OK\r\nLocation: https://x.example/\r\n\r\n", location) &&
               !parse_stream_redirect("ICY 200 OK\r\n\r\n", location) &&
               !parse_stream_redirect("HTTP/1.1 302 Found\r\nLocation: /relative\r\n\r\n", location) &&
               !parse_stream_redirect("HTTP/1.1 302 Found\r\nLocation: ftp://x.example/\r\n\r\n", location) &&
               !parse_stream_redirect("HTTP/1.1 302 Found\r\n\r\n", location),
           "anything else is not a redirect to follow");
}


void url_tests() {
    using namespace aura;
    StreamUrl url;
    expect(split_stream_url("http://stream.example.gr:8000/live.mp3", url) && !url.secure &&
               url.host == "stream.example.gr" && url.port == 8000 && url.path == "/live.mp3",
           "an http stream with its port");
    expect(split_stream_url("https://cdn.example/radio?quality=high#player", url) && url.secure && url.port == 443 &&
               url.host == "cdn.example" && url.path == "/radio?quality=high",
           "https, the default port, the query kept and the fragment dropped");
    expect(split_stream_url("http://host.example", url) && url.port == 80 && url.path == "/", "a bare host asks for /");
    for (const char* bad : {"ftp://host.example/", "http://", "http://host:/x", "http://host:70000/",
                            "http://user@host.example/", "http://[::1]/stream", "http://host example/",
                            "http://host.example/\x7f"}) {
        expect(!split_stream_url(bad, url), "an address the device does not open");
    }
    expect(!split_stream_url("http://h.example/" + std::string(2100, 'a'), url), "a bounded address");

    expect(split_stream_url("http://stream.example.gr:8000/live.mp3", url) &&
               stream_request(url) ==
                   "GET /live.mp3 HTTP/1.0\r\nHost: stream.example.gr:8000\r\nUser-Agent: AURA-Radio/1.0\r\n"
                   "Accept: audio/mpeg,*/*;q=0.5\r\nIcy-MetaData: 1\r\nConnection: close\r\n\r\n",
           "HTTP/1.0 with ICY titles asked for, and the port in Host");
    expect(split_stream_url("https://cdn.example/radio", url) &&
               stream_request(url).find("\r\nHost: cdn.example\r\n") != std::string::npos,
           "no default port in Host");
}

}  // namespace

int main() {
    station_tests();
    model_tests();
    icy_tests();
    mp3_tests();
    pump_tests();
    url_tests();
    if (failures == 0) std::cout << "aura_radio_tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

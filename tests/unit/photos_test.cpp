#include "aura/photos.hpp"
#include "aura/app_layout.hpp"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace aura;
namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
std::vector<std::uint8_t> fixture(const char* name) {
    std::ifstream in(std::string(AURA_PHOTO_FIXTURES) + "/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
struct Files : IPhotoStorage {
    std::vector<std::uint8_t> bytes = fixture("DEMO-image.png");
    std::string saved;
    bool available{true}, fail_save{false}, fail_delete{false};
    std::string deleted;
    PhotoError scan(std::vector<std::string>& names) override {
        names = {"DEMO-b.jpg", "../bad.jpg", "DEMO-a.png", "DEMO-a.png"};
        return available ? PhotoError::None : PhotoError::Storage;
    }
    PhotoError read(const std::string&, PhotoBytes& out) override {
        if (!out.resize(bytes.size())) return PhotoError::TooLarge;
        std::copy(bytes.begin(), bytes.end(), out.data());
        return available ? PhotoError::None : PhotoError::Read;
    }
    std::string read_settings() override { return saved; }
    bool save_settings(const std::string& text) override {
        if (fail_save) return false;
        saved = text;
        return true;
    }
    bool erase(const std::string& name) override {
        if (fail_delete) return false;
        deleted = name;
        return true;
    }
};
}  // namespace
int main() {
    {
        // A photo in a conversation shows as a thumbnail: the average of the
        // pixels it covers, the aspect kept, within the bubble's edge.
        PhotoError error = PhotoError::None;
        const auto bytes = fixture("DEMO-large-bars.jpg");
        const auto image = decode_photo(bytes, error);
        expect(image && error == PhotoError::None, "the fixture decodes");
        const auto thumb = photo_thumbnail(*image, 216);
        expect(thumb && thumb->width <= 216 && thumb->height <= 216 &&
                   (thumb->width == 216 || thumb->height == 216),
               "a thumbnail fills 216 px on its long side");
        const double source_ratio = static_cast<double>(image->width) / image->height;
        const double thumb_ratio = static_cast<double>(thumb->width) / thumb->height;
        expect(thumb_ratio > source_ratio * 0.97 && thumb_ratio < source_ratio * 1.03, "the aspect is kept");
        const auto mean = [](const PhotoImage& picture) {
            double sum = 0;
            for (std::size_t index = 0; index < picture.pixels.size(); ++index)
                sum += static_cast<double>((picture.pixels[index] >> 11) & 31);
            return sum / static_cast<double>(picture.pixels.size());
        };
        expect(std::abs(mean(*thumb) - mean(*image)) < 1.0, "it averages the pixels it covers");
        const auto small = photo_thumbnail(*thumb, 400);
        expect(small && small->width == thumb->width && small->height == thumb->height,
               "a picture already within the edge is never enlarged");
    }
    expect(photo_filename_valid("DEMO Αθήνα.JPG") && photo_filename_valid("DEMO-image.png"),
           "Greek and uppercase extensions supported");
    for (const auto* name :
         {"../x.jpg", "a/b.png", "a\\b.jpg", "C:x.png", ".hidden.jpg", "bad.txt", "bad\n.jpg", "bad.jpg "})
        expect(!photo_filename_valid(name), "unsafe filename rejected");
    PhotoSettings s{30, "DEMO Αθήνα.png"}, restored;
    expect(parse_photo_settings(serialize_photo_settings(s), restored) && restored.interval_seconds == 30 &&
               restored.ambient_name == s.ambient_name,
           "settings roundtrip");
    for (const auto* invalid : {"{}", "{\"version\":1,\"seconds\":2,\"ambient\":\"\"}",
                                "{\"version\":1,\"seconds\":10,\"ambient\":\"../a.jpg\"}"})
        expect(!parse_photo_settings(invalid, restored), "invalid preference rejected");
    PhotoError error;
    for (const auto* name : {"DEMO-color-bars.jpg", "DEMO-color-bars.png"}) {
        const auto bars = decode_photo(fixture(name), error);
        expect(bars && bars->width == 192 && bars->height == 64, "color fixture decodes");
        const auto red = bars->pixels[32 * 192 + 32], blue = bars->pixels[32 * 192 + 160];
        expect((red >> 11) > 26 && (red & 31) < 5 && (blue >> 11) < 5 && (blue & 31) > 26,
               "JPEG and PNG preserve red and blue channels");
    }
    for (const auto* name : {"DEMO-baseline.jpg", "DEMO-progressive.jpg", "DEMO-image.png"}) {
        const auto image = decode_photo(fixture(name), error);
        expect(image && error == PhotoError::None && image->width == 96 && image->height == 48 &&
                   image->pixels.size() == 96 * 48,
               "actual JPG/PNG decode");
    }
    const auto rotated = decode_photo(fixture("DEMO-rotated.jpg"), error);
    expect(rotated && rotated->width == 48 && rotated->height == 96, "EXIF orientation honored");
    const auto large = decode_photo(fixture("DEMO-large.jpg"), error);
    expect(large && large->width == 480 && large->height <= 480 && large->pixels.size() <= 480 * 480,
           "large baseline JPEG downscaled with bounded output");
    const auto transparent = decode_photo(fixture("DEMO-transparent.png"), error);
    expect(transparent && transparent->pixels[0] == 0, "PNG alpha composited over black");
    expect(!decode_photo({1, 2, 3, 4}, error) && error == PhotoError::Format, "corrupt image rejected");
    expect(!decode_photo(std::vector<std::uint8_t>(kMaxPhotoFileBytes + 1), error) && error == PhotoError::TooLarge,
           "file byte limit enforced");
    expect(!decode_photo(fixture("DEMO-tall.png"), error) && error == PhotoError::TooLarge,
           "compressed PNG resolution is independent of file byte size");
    const auto large_bars = decode_photo(fixture("DEMO-large-bars.jpg"), error);
    expect(large_bars && large_bars->width == 480 && large_bars->height == 240 &&
               (large_bars->pixels[120 * 480 + 40] >> 11) > 26 && (large_bars->pixels[120 * 480 + 440] & 31) > 26,
           "large JPEG reduction preserves spatial content and channels");
    PhotoMailbox mailbox;
    PhotoModel model(mailbox);
    Files files;
    PhotoWorker worker(mailbox, files);
    worker.initialize();
    model.poll(0, false);
    expect(model.refresh() && worker.step(), "scan scheduled");
    model.poll(0, false);
    expect(model.snapshot().names.size() == 2 && model.snapshot().names[0] == "DEMO-a.png",
           "catalog filtered sorted deduplicated");
    expect(model.select(0) && worker.step(), "load scheduled");
    model.poll(0, true);
    expect(model.snapshot().image && model.set_ambient(true) && model.set_interval(5),
           "ambient and slideshow interval selected");
    while (worker.step()) {
    }
    model.poll(0, true);
    expect(!model.snapshot().saving && model.snapshot().ambient && parse_photo_settings(files.saved, restored) &&
               restored.ambient_name == "DEMO-a.png",
           "preferences saved, ambient retained");
    model.set_playing(true);
    for (unsigned i = 0; i < 4; ++i)
        model.poll(1000, true);
    expect(model.snapshot().selected == 0 && !model.snapshot().loading, "slideshow waits full interval");
    model.poll(1000, true);
    expect(model.snapshot().selected == 1 && model.snapshot().loading, "slideshow advances exactly on interval");
    worker.step();
    model.poll(0, true);
    expect(model.snapshot().ambient != model.snapshot().image, "browsing does not replace chosen ambient");
    model.poll(0, false);
    expect(!model.snapshot().playing, "leaving viewer stops slideshow");
    expect(model.select(0) && model.select(1), "two generations submitted");
    worker.step();
    model.poll(0, true);
    expect(model.snapshot().loading, "superseded load discarded");
    worker.step();
    model.poll(0, true);
    expect(model.snapshot().image && model.snapshot().selected == 1, "newest load applied");
    files.fail_save = true;
    model.set_interval(60);
    worker.step();
    model.poll(0, true);
    expect(model.snapshot().error == PhotoError::Save, "storage failures are visible");
    files.bytes = {0, 0};
    model.advance(1);
    worker.step();
    model.poll(0, true);
    expect(!model.snapshot().image && model.snapshot().error == PhotoError::Format,
           "bad image does not display previous pixels");
    PhotoMailbox bounded;
    for (int i = 0; i < 4; ++i)
        expect(bounded.submit({}), "bounded command accepted");
    expect(!bounded.submit({}), "fifth queued command rejected");
    expect(bounded.pending(), "accepted preferences remain visible to platform shutdown flush");
    files.available = false;
    expect(model.refresh() && worker.step(), "missing card refresh submitted");
    model.poll(0, false);
    expect(model.snapshot().names.empty() && !model.snapshot().ambient && model.snapshot().error == PhotoError::Storage,
           "card loss clears catalog and cached ambient");
    PhotoBytes limited;
    expect(!limited.resize(kMaxPhotoFileBytes + 1) && limited.empty(),
           "large buffers reject over-limit allocation explicitly");
    files.fail_save = false;
    for (unsigned interval : {5U, 10U, 30U, 60U})
        expect(model.set_interval(interval), "fill pending preference queue");
    model.reset();
    while (worker.step()) {
    }
    model.poll(0, false);
    expect(
        parse_photo_settings(files.saved, restored) && restored.ambient_name.empty() && restored.interval_seconds == 10,
        "factory reset replaces pending old preferences with defaults");
    files.available = true;
    files.bytes = fixture("DEMO-image.png");
    expect(model.refresh() && worker.step(), "reload for delete checks");
    model.poll(0, false);
    expect(!model.erase("../DEMO-a.png") && !model.erase("not-in-catalog.png"),
           "delete requires an exact catalog basename");
    model.select(0);
    worker.step();
    model.poll(0, true);
    model.set_ambient(true);
    while (worker.step()) {
    }
    model.poll(0, true);
    files.fail_delete = true;
    expect(model.erase("DEMO-a.png") && files.deleted.empty(),
           "confirmed deletion queues work rather than performing UI-thread I/O");
    expect(!model.select(1), "selection waits for pending deletion");
    worker.step();
    model.poll(0, false);
    expect(
        model.snapshot().names.size() == 2 && model.snapshot().ambient && model.snapshot().error == PhotoError::Delete,
        "failed deletion preserves file and ambient");
    files.fail_delete = false;
    model.erase("DEMO-a.png");
    worker.step();
    model.poll(0, false);
    expect(files.deleted == "DEMO-a.png" && model.snapshot().names.size() == 1 && !model.snapshot().ambient &&
               model.snapshot().settings.ambient_name.empty(),
           "deleting ambient clears retained pixels and preference");
    while (worker.step()) {
    }
    model.poll(0, false);
    expect(parse_photo_settings(files.saved, restored) && restored.ambient_name.empty(),
           "ambient deletion preference survives restart");
    LauncherLayoutModel layout;
    expect(
        layout.restore(
            "AURA_APP_LAYOUT_V1\nweather\nconversation\nmessages\nclock\nnotes\ntoday\nradio\nnotifications\nsettings\norganizer\n") &&
            layout.snapshot().order.front() == LauncherApp::Weather &&
            layout.snapshot().order.back() == LauncherApp::Photos,
        "existing owner launcher order migrates with Photos appended");
    std::cout << "Photo decoder, model, preference and migration checks passed\n";
}

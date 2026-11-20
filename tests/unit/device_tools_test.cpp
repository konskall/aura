// The owner's tools of 2026-11-18: the conversation's history, the Αρχεία app
// and zooming a photo with two fingers.
#include "aura/conversation_log.hpp"
#include "aura/file_browser.hpp"
#include "aura/photo_zoom.hpp"
#include "aura/photos.hpp"

#include <cmath>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

void expect(bool value, const char* message) {
    if (value) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

bool near(float left, float right, float tolerance = 0.01F) { return std::fabs(left - right) <= tolerance; }

void conversation_log_tests() {
    using aura::ConversationLog;
    ConversationLog log;
    expect(log.turns().empty(), "a new log is empty");
    log.add("Τι καιρό θα κάνει;\nΑύριο;", "Βροχή\nκαι κρύο \\ πολύ.\n\nΚαληνύχτα", 29000000);
    expect(log.turns().size() == 1 && log.turns().front().minute == 29000000, "a turn is kept with its time");
    const auto before = log.sequence();
    log.add("", "μόνο απάντηση", 1);
    log.add("μόνο ερώτηση", "   ", 1);
    expect(log.turns().size() == 1 && log.sequence() == before, "half a turn is not kept");

    ConversationLog restored;
    expect(restored.restore(log.serialize()), "the stored text comes back");
    expect(restored.turns().size() == 1 && restored.turns().front().question == "Τι καιρό θα κάνει;\nΑύριο;" &&
               restored.turns().front().answer == "Βροχή\nκαι κρύο \\ πολύ.\n\nΚαληνύχτα" &&
               restored.turns().front().minute == 29000000,
           "line breaks and backslashes survive storing");

    for (int index = 0; index < 50; ++index) log.add("ερώτηση " + std::to_string(index), "απάντηση", index + 1);
    expect(log.turns().size() == ConversationLog::kMaxTurns, "the log is bounded");
    expect(log.turns().back().question == "ερώτηση 49" && log.turns().front().question == "ερώτηση 10",
           "the oldest turns go first");
    log.add(std::string(ConversationLog::kMaxQuestionBytes * 2, 'q'), std::string(ConversationLog::kMaxAnswerBytes * 2, 'a'), 9);
    expect(log.turns().back().question.size() == ConversationLog::kMaxQuestionBytes &&
               log.turns().back().answer.size() == ConversationLog::kMaxAnswerBytes,
           "a long answer is cut to the bound");

    auto kept = log.turns();
    expect(!log.restore("something else\n") && !log.restore("") && log.turns().size() == kept.size(),
           "another file leaves the log as it was");
    expect(!log.restore("AURA_CONVERSATIONS_V1\nnot a number\tq\ta\n") && log.turns().size() == kept.size(),
           "a damaged line leaves the log as it was");
    expect(!log.restore("AURA_CONVERSATIONS_V1\n5\tq\ta\\x\n") && log.turns().size() == kept.size(),
           "a broken escape leaves the log as it was");
    log.clear();
    expect(log.turns().empty(), "clear empties it");
}

class FakeStorage final : public aura::IFileStorage {
public:
    std::map<std::string, std::vector<aura::FileEntry>> folders;
    std::vector<std::string> erased;
    aura::FileBrowserError list_error{aura::FileBrowserError::None};
    aura::FileBrowserError list(const std::string& path, std::vector<aura::FileEntry>& entries, bool& truncated) override {
        if (list_error != aura::FileBrowserError::None) return list_error;
        const auto found = folders.find(path);
        if (found == folders.end()) return aura::FileBrowserError::NotFound;
        for (auto entry : found->second) aura::file_list_add(entries, std::move(entry), truncated);
        return aura::FileBrowserError::None;
    }
    aura::FileBrowserError erase(const std::string& path) override {
        erased.push_back(path);
        return aura::FileBrowserError::None;
    }
    std::vector<std::pair<std::string, std::string>> renamed;
    aura::FileBrowserError rename(const std::string& path, const std::string& new_name) override {
        renamed.emplace_back(path, new_name);
        return aura::FileBrowserError::None;
    }
    std::map<std::string, std::vector<std::uint8_t>> contents;
    std::size_t last_read_limit{0};
    aura::FileBrowserError read(const std::string& path, std::size_t max_bytes,
                                std::vector<std::uint8_t>& bytes) override {
        last_read_limit = max_bytes;
        const auto found = contents.find(path);
        if (found == contents.end()) return aura::FileBrowserError::NotFound;
        bytes.assign(found->second.begin(),
                     found->second.begin() + static_cast<std::ptrdiff_t>(std::min(max_bytes, found->second.size())));
        return aura::FileBrowserError::None;
    }
    bool space(std::uint64_t& total, std::uint64_t& free) override {
        total = 1000;
        free = 250;
        return true;
    }
};

void file_browser_tests() {
    using namespace aura;
    expect(file_path_valid("") && file_path_valid("aura/photos/a b.jpg") && file_path_valid("Μουσική/τραγούδι.mp3"),
           "ordinary paths are valid");
    for (const char* bad : {"/etc", "aura/", "aura//photos", "..", "aura/../secret", "./a", "a\\b", "a/.", "a\nb"}) {
        expect(!file_path_valid(bad), "a path outside the card is refused");
    }
    expect(!file_path_valid(std::string(kMaxFilePathBytes + 1, 'a')), "a very long path is refused");
    expect(file_path_join("", "a.txt") == "a.txt" && file_path_join("aura/photos", "a.jpg") == "aura/photos/a.jpg",
           "paths join");
    expect(file_path_parent("aura/photos/a.jpg") == "aura/photos" && file_path_parent("aura").empty() &&
               file_path_parent("").empty(),
           "the parent folder");
    expect(file_path_leaf("aura/photos/a.jpg") == "a.jpg" && file_path_leaf("a.jpg") == "a.jpg", "the last name");
    expect(file_path_display("aura/photos") == "aura / photos" && file_path_display("").empty(), "how a place reads");

    expect(file_kind("A.JPG", false) == FileKind::Image && file_kind("τραγούδι.MP3", false) == FileKind::Audio &&
               file_kind("note.txt", false) == FileKind::Document && file_kind("data.bin", false) == FileKind::Other &&
               file_kind("photo.jpg", true) == FileKind::Folder && file_kind(".jpg", false) == FileKind::Other,
           "kinds by extension");

    for (const char* mine : {"Μουσική", "Μουσική/a.mp3", "report.pdf", "aura/photos/a.jpg", "aura/files/b.pdf",
                             "aura/files/folder/c.txt"}) {
        expect(!file_path_protected(mine), "what the owner put on the card can be deleted");
    }
    for (const char* system : {"", "aura", "aura/clock.txt", "aura/notes", "aura/notes/n1.wav", "aura/photos", "aura/files",
                               "aura/.cache/aoraton/x", "aura/.transfer/upload.part", "aura/photos-settings.json"}) {
        expect(file_path_protected(system), "the device's own files are never deleted here");
    }

    expect(file_size_text(0, ",") == "0 B" && file_size_text(850, ",") == "850 B", "bytes");
    expect(file_size_text(1536, ",") == "1,5 KB" && file_size_text(1536, ".") == "1.5 KB", "the language's separator");
    expect(file_size_text(1024ULL * 1024ULL, ",") == "1 MB", "a whole number has no decimals");
    expect(file_size_text(250ULL * 1024ULL * 1024ULL, ",") == "250 MB", "large numbers are whole");
    expect(file_size_text(1023ULL * 1024ULL * 1023ULL / 1000ULL, ",").find("MB") != std::string::npos, "rounding stays in range");
    expect(file_size_text(5ULL * 1024ULL * 1024ULL * 1024ULL, ",") == "5 GB", "gigabytes");

    std::vector<FileEntry> entries;
    bool truncated = false;
    file_list_add(entries, {"zeta.txt", false, 1, 0}, truncated);
    file_list_add(entries, {"Alpha", true, 0, 0}, truncated);
    file_list_add(entries, {"beta.txt", false, 1, 0}, truncated);
    file_list_add(entries, {"gamma", true, 0, 0}, truncated);
    expect(entries[0].name == "Alpha" && entries[1].name == "gamma" && entries[2].name == "beta.txt" &&
               entries[3].name == "zeta.txt" && !truncated,
           "folders first, then by name");
    entries.clear();
    for (std::size_t index = 0; index < kMaxFileEntries + 5; ++index) {
        char name[16];
        std::snprintf(name, sizeof(name), "f%04zu", kMaxFileEntries + 5 - index);
        file_list_add(entries, {name, false, 1, 0}, truncated);
    }
    expect(entries.size() == kMaxFileEntries && truncated && entries.front().name == "f0001",
           "a large folder keeps its first entries in order");

    FileMailbox mailbox;
    FakeStorage storage;
    storage.folders[""] = {{"aura", true, 0, 0}, {"report.pdf", false, 4096, 5}};
    storage.folders["aura"] = {{"clock.txt", false, 10, 0}};
    FileWorker worker(mailbox, storage);
    FileBrowserModel model(mailbox);
    expect(model.open("") && model.snapshot().loading, "opening a folder reads it");
    expect(!model.open("../outside"), "a folder outside the card is not opened");
    worker.step();
    model.poll();
    expect(!model.snapshot().loading && model.snapshot().entries.size() == 2 && model.snapshot().space_known &&
               model.snapshot().free_bytes == 250,
           "its entries and the free space arrive");

    // A slow result for a folder no longer open never replaces the one shown.
    expect(model.open("aura") && model.open(""), "two folders in a row");
    worker.step();
    worker.step();
    model.poll();
    expect(model.snapshot().path.empty() && model.snapshot().entries.size() == 2, "the last folder asked for wins");

    expect(model.open("aura"), "into a folder");
    worker.step();
    model.poll();
    expect(model.snapshot().path == "aura" && model.snapshot().entries.front().name == "clock.txt", "the folder's entries");
    expect(model.open_parent() && model.snapshot().path.empty(), "back up one folder");
    worker.step();
    model.poll();
    expect(!model.open_parent(), "the root has no parent");

    expect(!model.erase("aura/clock.txt") && model.snapshot().error == FileBrowserError::Protected && storage.erased.empty(),
           "the model refuses the device's own files");
    expect(model.erase("report.pdf") && model.snapshot().deleting, "a confirmed deletion starts");
    expect(!model.erase("other.pdf"), "one deletion at a time");
    worker.step();
    model.poll();
    expect(storage.erased == std::vector<std::string>{"report.pdf"} && !model.snapshot().deleting &&
               model.snapshot().erased == 1 && model.snapshot().loading,
           "the file is deleted, counted, and the folder read again");
    worker.step();
    model.poll();
    expect(!model.snapshot().loading, "the folder is shown again");

    // The worker refuses on its own what the model would.
    FileJob forced;
    forced.kind = FileJobKind::Erase;
    forced.path = "aura/clock.txt";
    expect(mailbox.submit(forced), "a job reaches the worker");
    worker.step();
    FileResult refused;
    expect(mailbox.consume(refused) && refused.error == FileBrowserError::Protected &&
               storage.erased == std::vector<std::string>{"report.pdf"},
           "the worker never deletes the device's own files");

    storage.list_error = FileBrowserError::NoCard;
    expect(model.refresh(), "reading again");
    worker.step();
    model.poll();
    expect(model.snapshot().error == FileBrowserError::NoCard && model.snapshot().entries.empty(), "no card says so");

    expect(model.open("aura") && model.snapshot().loading, "a folder is asked for");
    model.unavailable(FileBrowserError::NoCard);
    expect(model.snapshot().error == FileBrowserError::NoCard && !model.snapshot().loading &&
               model.snapshot().entries.empty(), "no card, without waiting for a worker");

    model.seed_demo();
    expect(model.snapshot().demo && model.snapshot().entries.size() == 3 &&
               model.snapshot().entries[1].name.find("DEMO") != std::string::npos,
           "sample entries say DEMO");
}

// Αρχεία: renaming and a look inside (the owner, 2026-11-19). A file or folder
// the owner put there takes a valid new name in its own folder; a picture is
// decoded for a look, a text file shows its first lines.
void file_rename_preview_tests() {
    using namespace aura;
    FileMailbox mailbox;
    FakeStorage storage;
    storage.folders[""] = {{"report.pdf", false, 4096, 5}, {"notes.txt", false, 20, 5}, {"photo.jpg", false, 1350, 5}};
    FileWorker worker(mailbox, storage);
    FileBrowserModel model(mailbox);
    model.open("");
    worker.step();
    model.poll();

    expect(model.rename("report.pdf", "Έκθεση 2026.pdf") && model.snapshot().renaming, "a rename starts");
    expect(!model.rename("notes.txt", "other.txt"), "one rename at a time");
    worker.step();
    model.poll();
    expect(storage.renamed.size() == 1 && storage.renamed.front().first == "report.pdf" &&
               storage.renamed.front().second == "Έκθεση 2026.pdf" && !model.snapshot().renaming &&
               model.snapshot().renamed == 1 && model.snapshot().loading,
           "the card renames it and the folder is read again");
    worker.step();
    model.poll();
    for (const char* bad : {"", "a/b.txt", "..", ".", "a\nb", "con\\x"}) {
        expect(!model.rename("notes.txt", bad), "an invalid name is refused");
    }
    expect(!model.rename("notes.txt", "notes.txt"), "the same name is no rename");
    expect(!model.rename("aura/clock.txt", "x.txt") && model.snapshot().error == FileBrowserError::Protected,
           "the device's own files keep their names");
    FileJob forced;
    forced.kind = FileJobKind::Rename;
    forced.path = "aura/clock.txt";
    forced.new_name = "x.txt";
    expect(mailbox.submit(forced), "a forced rename reaches the worker");
    worker.step();
    FileResult refused;
    expect(mailbox.consume(refused) && refused.error == FileBrowserError::Protected && storage.renamed.size() == 1,
           "and the worker refuses it on its own");

    const std::string words = "Γεια σου\nδεύτερη γραμμή\n";
    storage.contents["notes.txt"] = std::vector<std::uint8_t>(words.begin(), words.end());
    expect(model.preview("notes.txt") && model.snapshot().previewing, "a text file is read for a look");
    worker.step();
    model.poll();
    expect(!model.snapshot().previewing && model.snapshot().preview_path == "notes.txt" &&
               model.snapshot().preview_text == words && !model.snapshot().preview_image &&
               storage.last_read_limit == kMaxFilePreviewTextBytes,
           "its first lines, with their line breaks");

    std::ifstream fixture(std::string(AURA_PHOTO_FIXTURES) + "/DEMO-baseline.jpg", std::ios::binary);
    storage.contents["photo.jpg"] = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(fixture), {});
    expect(!storage.contents["photo.jpg"].empty(), "the picture's bytes");
    expect(model.preview("photo.jpg"), "a picture is decoded for a look");
    worker.step();
    model.poll();
    expect(model.snapshot().preview_path == "photo.jpg" && model.snapshot().preview_image &&
               model.snapshot().preview_image->width > 0 && model.snapshot().preview_text.empty() &&
               model.snapshot().preview_error == FileBrowserError::None,
           "and shown");

    storage.contents["broken.jpg"] = {1, 2, 3};
    expect(model.preview("broken.jpg"), "a damaged picture is tried");
    worker.step();
    model.poll();
    expect(model.snapshot().preview_error == FileBrowserError::Read && !model.snapshot().preview_image,
           "and says it cannot be read");
    expect(!model.preview("report.pdf") && model.snapshot().preview_error == FileBrowserError::Unsupported,
           "a kind without a look says so at once");
}

void photo_zoom_tests() {
    using aura::PhotoZoom;
    PhotoZoom zoom;
    zoom.set_frame(480, 320, 480, 480);
    expect(near(zoom.scale(), 1.0F) && !zoom.zoomed() && near(zoom.center_x(), 240) && near(zoom.center_y(), 160),
           "a picture starts whole and centred");

    // Two fingers apart, around the frame's centre: twice as near as the picture.
    zoom.pinch(140, 240, 340, 240);
    zoom.pinch(40, 240, 440, 240);
    expect(zoom.pinching() && near(zoom.zoom(), 2.0F) && zoom.zoomed(), "fingers twice as far apart: twice the zoom");
    expect(near(zoom.center_x(), 240) && near(zoom.center_y(), 160), "the point between them stays");
    zoom.pinch_end();
    expect(!zoom.pinching() && near(zoom.zoom(), 2.0F), "lifting keeps the zoom");

    // Dragging the zoomed picture stops at its edges.
    zoom.pan(-100, 0);
    expect(near(zoom.center_x(), 290), "a drag moves the picture under the finger");
    zoom.pan(-1000, -1000);
    expect(near(zoom.center_x(), 360) && near(zoom.center_y(), 200), "the edges stop at the frame's edges");
    zoom.pan(2000, 0);
    expect(near(zoom.center_x(), 120), "and the left edge");

    // A little zoom: the picture's height still fits inside the square frame.
    zoom.reset();
    zoom.pinch(140, 240, 340, 240);
    zoom.pinch(120, 240, 360, 240);
    zoom.pinch_end();
    expect(near(zoom.zoom(), 1.2F), "a little zoom");
    zoom.pan(0, -500);
    expect(near(zoom.center_y(), 160), "a side smaller than the frame stays centred");

    // A pinch around a point off the centre keeps that point under the fingers.
    zoom.reset();
    zoom.pinch(40, 40, 120, 40);
    const float picked_x = 80.0F;  // the frame's x between the fingers = the picture's x at scale 1
    zoom.pinch(0, 40, 160, 40);
    expect(near(zoom.zoom(), 2.0F), "a pinch off centre zooms the same");
    const float shown_x = (picked_x - zoom.center_x()) * zoom.scale() + 240.0F;
    expect(near(shown_x, 80.0F, 0.5F), "the point between the fingers stays under them");

    zoom.pinch(0, 40, 2000, 40);
    expect(near(zoom.zoom(), PhotoZoom::kMaxZoom), "the zoom has a ceiling");
    zoom.pinch_end();
    zoom.pinch(100, 100, 300, 100);
    zoom.pinch(195, 100, 205, 100);
    expect(zoom.zoom() >= 1.0F, "fingers together never shrink below the whole picture");
    zoom.pinch_end();

    zoom.reset();
    zoom.pinch(100, 100, 200, 100);
    zoom.pinch(100, 100, 203, 100);
    zoom.pinch_end();
    expect(!zoom.zoomed() && near(zoom.center_x(), 240), "barely zoomed goes back to whole");

    zoom.reset();
    zoom.pan(50, 50);
    expect(near(zoom.center_x(), 240), "a whole picture does not move");
    zoom.pinch(100, 240, 110, 240);
    expect(!zoom.pinching() && near(zoom.zoom(), 1.0F), "two touches too close are not a pinch");

    zoom.pinch(100, 100, 300, 100);
    zoom.pinch(0, 100, 400, 100);
    zoom.set_frame(480, 320, 480, 268);
    expect(!zoom.zoomed() && near(zoom.scale(), 268.0F / 320.0F), "another frame starts whole");
    zoom.set_frame(480, 320, 480, 268);
    zoom.pinch(100, 100, 300, 100);
    zoom.pinch(0, 100, 400, 100);
    zoom.set_frame(480, 320, 480, 268);
    expect(zoom.zoomed(), "the same frame keeps the zoom");

    // The frame's pixels: four quadrants of colour, a whole picture and a zoomed one.
    aura::PhotoImage image;
    image.width = 4;
    image.height = 4;
    expect(image.pixels.resize(16), "the picture's pixels");
    const std::uint16_t colours[4]{0xF800, 0x07E0, 0x001F, 0xFFFF};
    for (unsigned y = 0; y < 4; ++y) {
        for (unsigned x = 0; x < 4; ++x) image.pixels[y * 4 + x] = colours[(y / 2) * 2 + x / 2];
    }
    PhotoZoom view;
    view.set_frame(4, 4, 64, 64);
    std::vector<std::uint16_t> frame(64 * 64, 0x1234);
    const std::size_t last = frame.size() - 1;
    aura::photo_zoom_render(image, view, frame.data(), false);
    expect(frame[0] == 0xF800 && frame[63] == 0x07E0 && frame[last - 63] == 0x001F && frame[last] == 0xFFFF,
           "the whole picture fills the frame");
    view.pinch(20, 32, 44, 32);
    view.pinch(8, 32, 56, 32);
    expect(near(view.zoom(), 2.0F), "zoomed in twice");
    aura::photo_zoom_render(image, view, frame.data(), false);
    expect(frame[0] == 0xF800 && frame[last] == 0xFFFF, "the zoomed picture shows the centre");
    view.pan(100, 100);
    aura::photo_zoom_render(image, view, frame.data(), false);
    expect(frame[0] == 0xF800 && frame[last] == 0xF800, "dragged to the corner, one colour fills the frame");

    PhotoZoom wide;
    wide.set_frame(4, 4, 8, 4);  // a square picture in a wide frame
    std::vector<std::uint16_t> strip(32, 0x1234);
    aura::photo_zoom_render(image, wide, strip.data(), false);
    expect(strip[0] == 0 && strip[7] == 0 && strip[2] == 0xF800 && strip[5] == 0x07E0,
           "black around a picture narrower than the frame");

    aura::photo_zoom_render(image, view, frame.data(), true);
    view.reset();
    aura::photo_zoom_render(image, view, frame.data(), true);
    expect(frame[0] == 0xF800 && frame[last] == 0xFFFF, "a smooth picture keeps its corners");
    const std::uint16_t middle = frame[30 * 64 + 30];
    const unsigned red = middle >> 11U;
    expect(red > 0U && red < 31U, "a smooth picture blends between its pixels");
    aura::photo_zoom_render(image, view, nullptr, true);  // nothing to draw into: nothing happens
}

}  // namespace

int main() {
    conversation_log_tests();
    file_browser_tests();
    file_rename_preview_tests();
    photo_zoom_tests();
    std::cout << "Device tools tests passed\n";
    return 0;
}

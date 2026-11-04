#include "aura/photo_runtime.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
#include <windows.h>
namespace {
void expect(bool v, const char* m) {
    if (!v) {
        std::cerr << m << '\n';
        std::exit(1);
    }
}
}  // namespace
int main() {
    namespace fs = std::filesystem;
    const auto parent = fs::absolute(fs::current_path() / "artifacts").lexically_normal();
    const auto dir = parent / ("AURA-photo-storage-test-" + std::to_string(GetCurrentProcessId()));
    expect(dir.parent_path() == parent, "synthetic root bounded to workspace artifacts");
    fs::create_directories(dir / "photos");
    const std::string name = "DEMO Ελλάδα.png";
    const auto image = dir / "photos" / fs::u8path(name);
    fs::copy_file(fs::path(AURA_PHOTO_FIXTURES) / "DEMO-image.png", image, fs::copy_options::overwrite_existing);
    {
        std::ofstream out(dir / "photos" / "ignored.txt");
        out << "DEMO";
    }
    fs::create_directories(dir / "photos" / "DEMO-directory.jpg");
    aura::simulator::DesktopPhotoStorage storage(dir / "photos");
    std::vector<std::string> names;
    expect(storage.scan(names) == aura::PhotoError::None && names.size() == 1 && names[0] == name,
           "actual directory scanning and Greek filename");
    aura::PhotoBytes bytes;
    expect(storage.read(name, bytes) == aura::PhotoError::None && !bytes.empty(), "actual Unicode Windows read");
    expect(storage.read("../ignored.png", bytes) == aura::PhotoError::Read, "traversal rejected by adapter");
    expect(storage.save_settings(aura::serialize_photo_settings({30, name})), "atomic local settings writer");
    {
        aura::simulator::PhotoRuntime runtime(dir.u8string(), false);
        for (int i = 0; i < 200 && !runtime.model().snapshot().ambient; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            runtime.model().poll(0, false);
        }
        expect(runtime.model().snapshot().ambient && runtime.model().snapshot().settings.interval_seconds == 30,
               "fresh worker restores ambient image and interval from disk");
        expect(runtime.model().refresh(), "scan enqueued");
        for (int i = 0; i < 200 && runtime.model().snapshot().scanning; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            runtime.model().poll(0, false);
        }
        expect(runtime.model().snapshot().names.size() == 1 && runtime.model().select(0),
               "worker catalog delivered to owner thread");
        for (int i = 0; i < 200 && runtime.model().snapshot().loading; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            runtime.model().poll(0, true);
        }
        expect(runtime.model().snapshot().image != nullptr, "worker decoded actual disk image");
        runtime.model().reset();
    }
    expect(fs::exists(image), "reset preserves the owner's photo files");
    aura::PhotoSettings restored;
    expect(aura::parse_photo_settings(storage.read_settings(), restored) && restored.ambient_name.empty() &&
               restored.interval_seconds == 10,
           "reset preferences flushed during worker teardown");
    expect(!storage.erase("../ignored.png") && !storage.erase("DEMO-directory.jpg") &&
               fs::is_directory(dir / "photos" / "DEMO-directory.jpg"),
           "native photo deletion excludes traversal and directories");
    expect(storage.erase(name) && !fs::exists(image) && fs::exists(dir / "photos" / "ignored.txt"),
           "native deletion removes only the confirmed Unicode photo");
    // Only exact known synthetic paths are removed. No recursive traversal.
    fs::remove(image);
    fs::remove(dir / "photos" / "ignored.txt");
    fs::remove(dir / "photos" / "DEMO-directory.jpg");
    fs::remove(dir / "photos");
    fs::remove(dir / "photos-settings.json");
    fs::remove(dir / "photos-settings.json.tmp");
    fs::remove(dir);
    std::cout << "Desktop photo storage, Unicode paths, threaded load and restart persistence passed\n";
}

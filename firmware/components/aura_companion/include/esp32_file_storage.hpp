#pragma once

#include "aura/file_browser.hpp"
#include "esp32_microphone.hpp"

namespace aura::firmware {

// The Αρχεία app on the microSD card: a card path below /sdcard, read and
// deleted on the card's own worker (Esp32Companion's storage task).
class Esp32FileStorage final : public IFileStorage {
public:
    explicit Esp32FileStorage(const Esp32CompanionStore& store) : store_(store) {}
    FileBrowserError list(const std::string& path, std::vector<FileEntry>& entries, bool& truncated) override;
    FileBrowserError erase(const std::string& path) override;
    FileBrowserError rename(const std::string& path, const std::string& new_name) override;
    FileBrowserError read(const std::string& path, std::size_t max_bytes, std::vector<std::uint8_t>& bytes) override;
    bool space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) override;

private:
    const Esp32CompanionStore& store_;
};

}  // namespace aura::firmware

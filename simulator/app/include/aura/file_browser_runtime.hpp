#pragma once

#include "aura/file_browser.hpp"

#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace aura::simulator {

// The simulator's memory card: its store folder is the card's "aura" folder,
// so the Αρχεία app shows there what the device shows on its card.
class DesktopFileStorage final : public IFileStorage {
public:
    // The PC file behind a card path ("aura/..."); false for any other.
    bool local_path(const std::string& path, std::filesystem::path& out) const { return resolve(path, out); }
    explicit DesktopFileStorage(std::filesystem::path root) : root_(std::move(root)) {}
    FileBrowserError list(const std::string& path, std::vector<FileEntry>& entries, bool& truncated) override;
    FileBrowserError erase(const std::string& path) override;
    FileBrowserError rename(const std::string& path, const std::string& new_name) override;
    FileBrowserError read(const std::string& path, std::size_t max_bytes, std::vector<std::uint8_t>& bytes) override;
    bool space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) override;

private:
    bool resolve(const std::string& path, std::filesystem::path& out) const;
    std::filesystem::path root_;
};

// Deterministic runs: a fixed card of labelled DEMO files, kept in memory.
class DemoFileStorage final : public IFileStorage {
public:
    DemoFileStorage();
    FileBrowserError list(const std::string& path, std::vector<FileEntry>& entries, bool& truncated) override;
    FileBrowserError erase(const std::string& path) override;
    FileBrowserError rename(const std::string& path, const std::string& new_name) override;
    FileBrowserError read(const std::string& path, std::size_t max_bytes, std::vector<std::uint8_t>& bytes) override;
    bool space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) override;

private:
    std::map<std::string, std::vector<FileEntry>> folders_;
    std::map<std::string, std::vector<std::uint8_t>> contents_;  // DEMO files a look can read
};

class FileBrowserRuntime {
public:
    FileBrowserRuntime(const std::string& store_dir, bool deterministic);
    ~FileBrowserRuntime();
    FileBrowserModel& model() noexcept { return model_; }
    // A card file as the radio's player opens it (file:// and the PC path); empty
    // in deterministic runs, whose card is DEMO.
    std::string file_url(const std::string& path) const {
        std::filesystem::path local;
        if (deterministic_ || !file_path_valid(path) || !storage_.local_path(path, local)) return {};
        return "file://" + local.u8string();
    }
    // On the UI thread: finished work reaches the model (and, in a
    // deterministic run, the work itself is done here).
    void poll();

private:
    DesktopFileStorage storage_;
    DemoFileStorage demo_;
    FileMailbox mailbox_;
    FileBrowserModel model_{mailbox_};
    FileWorker demo_worker_{mailbox_, demo_};
    bool deterministic_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

}  // namespace aura::simulator

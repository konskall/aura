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
    explicit DesktopFileStorage(std::filesystem::path root) : root_(std::move(root)) {}
    FileBrowserError list(const std::string& path, std::vector<FileEntry>& entries, bool& truncated) override;
    FileBrowserError erase(const std::string& path) override;
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
    bool space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) override;

private:
    std::map<std::string, std::vector<FileEntry>> folders_;
};

class FileBrowserRuntime {
public:
    FileBrowserRuntime(const std::string& store_dir, bool deterministic);
    ~FileBrowserRuntime();
    FileBrowserModel& model() noexcept { return model_; }
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

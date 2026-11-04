#pragma once
#include "aura/photos.hpp"
#include <atomic>
#include <filesystem>
#include <thread>

namespace aura::simulator {
class DesktopPhotoStorage final : public IPhotoStorage {
public:
    explicit DesktopPhotoStorage(std::filesystem::path root) : root_(std::move(root)) {}
    PhotoError scan(std::vector<std::string>& names) override;
    PhotoError read(const std::string& name, PhotoBytes& bytes) override;
    std::string read_settings() override;
    bool save_settings(const std::string& text) override;
    bool erase(const std::string& name) override;

private:
    bool ready(bool create);
    std::filesystem::path root_;
};
class PhotoRuntime {
public:
    PhotoRuntime(const std::string& store_dir, bool deterministic);
    ~PhotoRuntime();
    PhotoModel& model() { return model_; }

private:
    DesktopPhotoStorage storage_;
    PhotoMailbox mailbox_;
    PhotoModel model_{mailbox_, true};
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};
}  // namespace aura::simulator

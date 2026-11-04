#pragma once
#include "aura/photos.hpp"
#include "esp32_microphone.hpp"
namespace aura::firmware {
class Esp32PhotoStorage final : public IPhotoStorage {
public:
    explicit Esp32PhotoStorage(Esp32CompanionStore& store) : store_(store) {}
    PhotoError scan(std::vector<std::string>& names) override;
    PhotoError read(const std::string& name, PhotoBytes& bytes) override;
    std::string read_settings() override { return store_.read("photos-settings.json"); }
    bool save_settings(const std::string& text) override { return store_.write("photos-settings.json", text); }
    bool erase(const std::string& name) override;

private:
    Esp32CompanionStore& store_;
};
}  // namespace aura::firmware

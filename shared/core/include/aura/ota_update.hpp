#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace aura {

enum class UpdateState { Idle, Checking, UpToDate, Available, Downloading, Verifying, ReadyToRestart, Failed };

struct UpdateManifest {
    std::string version;
    std::string url;
    std::uint32_t size_bytes{0};
    std::string sha256;
    std::string notes;
};

struct UpdateSnapshot {
    UpdateState state{UpdateState::Idle};
    std::string current_version;
    std::optional<UpdateManifest> available;
    float progress{0.0F};
    std::string error_code;
    // A configured update source exists (firmware: manifest URL; simulator: DEMO).
    bool source_configured{false};
    bool demo_source{false};
    std::uint64_t sequence{0};
};

enum class UpdateBlock { None, NoUpdate, Busy, Offline, LowBattery };

// Over-the-air update flow. The platform downloads and writes the image
// (firmware: esp_https_ota into the inactive OTA slot); this model owns the
// rules: a newer signed-off version, Wi-Fi, enough battery and an explicit
// confirmation before anything is written.
class OtaUpdateModel {
public:
    static constexpr std::uint32_t kMaxImageBytes = 0x680000;  // one OTA slot
    static constexpr int kMinBatteryPercent = 30;

    explicit OtaUpdateModel(std::string current_version = "0.0.0");

    void set_source(bool configured, bool demo);
    bool begin_check();
    void check_finished(const std::optional<UpdateManifest>& manifest);
    void check_failed(const std::string& code);
    UpdateBlock can_install(bool online, int battery_percent, bool battery_known, bool charging) const;
    bool begin_download();
    void set_progress(float progress);
    void begin_verify();
    void ready_to_restart();
    void fail(const std::string& code);
    void reset();

    const UpdateSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    void changed() noexcept { ++snapshot_.sequence; }
    UpdateSnapshot snapshot_{};
};

// The install rule shared by the model and the UI.
UpdateBlock update_block(const UpdateSnapshot& snapshot, bool online, int battery_percent, bool battery_known,
                         bool charging) noexcept;

// Strict manifest: {"version":"1.4.0","url":"https://...","size":123,
// "sha256":"<64 hex>","notes":"..."}; anything else is rejected.
std::optional<UpdateManifest> parse_update_manifest(const std::string& json, std::string* error = nullptr);
bool version_valid(const std::string& version) noexcept;
// <0, 0, >0 like strcmp; invalid versions compare as older.
int compare_versions(const std::string& left, const std::string& right) noexcept;
const char* update_state_name(UpdateState state) noexcept;

}  // namespace aura

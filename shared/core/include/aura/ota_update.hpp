#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace aura {

enum class UpdateState { Idle, Checking, UpToDate, Available, Downloading, Verifying, ReadyToRestart, Failed };

// Where a device looks for updates unless its NVS names another source: the
// manifest attached to the newest GitHub release of this repository
// (scripts/update-signing.mjs release prepares one).
inline constexpr char kUpdateManifestUrl[] =
    "https://github.com/konskall/aura/releases/latest/download/aura-manifest.json";

struct UpdateManifest {
    std::string version;
    std::string url;
    std::uint32_t size_bytes{0};
    std::string sha256;
    std::string notes;
    // The owner's ECDSA P-256 signature (r || s) of update_signing_message().
    std::array<std::uint8_t, 64> signature{};
};

// What the owner's key signs for an update: its version, size and SHA-256
// (lower case). HTTPS and the image's SHA-256 show where the bytes came from
// and that they arrived whole; the signature shows the owner released them,
// so an update server someone else controls cannot install its own firmware.
// A newer version is required as well, so an old signed image cannot return.
std::string update_signing_message(const UpdateManifest& manifest);

// Checks the owner's signature with the public key the build was made with
// (firmware: mbedTLS; simulator: Windows BCrypt).
class IUpdateSignatureVerifier {
public:
    virtual ~IUpdateSignatureVerifier() = default;
    // Whether the build has the owner's public key; without it nothing installs.
    virtual bool configured() const = 0;
    // Whether `signature` signs SHA-256(`message`) with the owner's key.
    virtual bool verify(const std::string& message, const std::array<std::uint8_t, 64>& signature) const = 0;
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
    static constexpr std::uint32_t kMaxImageBytes = 0x700000;  // one OTA slot (firmware/partitions.csv)
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
// "sha256":"<64 hex>","notes":"...","signature":"<128 hex>"}, signed by the
// owner's key; anything else is rejected (scripts/update-signing.mjs signs).
std::optional<UpdateManifest> parse_update_manifest(const std::string& json, const IUpdateSignatureVerifier& verifier,
                                                    std::string* error = nullptr);
bool version_valid(const std::string& version) noexcept;
// <0, 0, >0 like strcmp; invalid versions compare as older.
int compare_versions(const std::string& left, const std::string& right) noexcept;
const char* update_state_name(UpdateState state) noexcept;

}  // namespace aura

#include "aura/ota_update.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace aura {
namespace {

bool parse_version(const std::string& version, std::array<long, 3>& parts) noexcept {
    std::size_t position = 0;
    for (std::size_t index = 0; index < 3; ++index) {
        if (position >= version.size() || version[position] < '0' || version[position] > '9') return false;
        long value = 0;
        int digits = 0;
        while (position < version.size() && version[position] >= '0' && version[position] <= '9') {
            if (++digits > 6) return false;
            value = value * 10 + (version[position] - '0');
            ++position;
        }
        parts[index] = value;
        if (index < 2) {
            if (position >= version.size() || version[position] != '.') return false;
            ++position;
        }
    }
    return position == version.size();
}

bool is_hex64(const std::string& value) noexcept {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    });
}

}  // namespace

bool version_valid(const std::string& version) noexcept {
    std::array<long, 3> parts{};
    return parse_version(version, parts);
}

int compare_versions(const std::string& left, const std::string& right) noexcept {
    std::array<long, 3> a{};
    std::array<long, 3> b{};
    const bool left_ok = parse_version(left, a);
    const bool right_ok = parse_version(right, b);
    if (!left_ok || !right_ok) return left_ok == right_ok ? 0 : (left_ok ? 1 : -1);
    for (std::size_t index = 0; index < 3; ++index) {
        if (a[index] != b[index]) return a[index] < b[index] ? -1 : 1;
    }
    return 0;
}

std::optional<UpdateManifest> parse_update_manifest(const std::string& json, std::string* error) {
    auto reject = [error](const char* code) -> std::optional<UpdateManifest> {
        if (error) *error = code;
        return std::nullopt;
    };
    const auto object = parse_flat_json(json, 2048);
    if (!object) return reject("MANIFEST_INVALID_JSON");
    const auto version = object->string("version");
    const auto url = object->string("url");
    const auto size = object->integer("size");
    const auto sha256 = object->string("sha256");
    if (!version || !version_valid(*version)) return reject("MANIFEST_BAD_VERSION");
    // Only HTTPS; the firmware also pins the server certificate bundle.
    if (!url || url->rfind("https://", 0) != 0 || url->size() > 256 ||
        url->find_first_of(" \t\r\n\"<>\\") != std::string::npos) {
        return reject("MANIFEST_BAD_URL");
    }
    if (!size || *size <= 0 || *size > static_cast<std::int64_t>(OtaUpdateModel::kMaxImageBytes)) {
        return reject("MANIFEST_BAD_SIZE");
    }
    if (!sha256 || !is_hex64(*sha256)) return reject("MANIFEST_BAD_HASH");
    UpdateManifest manifest;
    manifest.version = *version;
    manifest.url = *url;
    manifest.size_bytes = static_cast<std::uint32_t>(*size);
    manifest.sha256 = *sha256;
    if (const auto notes = object->string("notes")) manifest.notes = sanitize_text(*notes, 300, true);
    return manifest;
}

OtaUpdateModel::OtaUpdateModel(std::string current_version) {
    snapshot_.current_version = std::move(current_version);
}

void OtaUpdateModel::set_source(bool configured, bool demo) {
    if (snapshot_.source_configured == configured && snapshot_.demo_source == demo) return;
    snapshot_.source_configured = configured;
    snapshot_.demo_source = demo;
    changed();
}

bool OtaUpdateModel::begin_check() {
    if (!snapshot_.source_configured) return false;
    const auto state = snapshot_.state;
    if (state == UpdateState::Checking || state == UpdateState::Downloading || state == UpdateState::Verifying ||
        state == UpdateState::ReadyToRestart) {
        return false;
    }
    snapshot_.state = UpdateState::Checking;
    snapshot_.error_code.clear();
    changed();
    return true;
}

void OtaUpdateModel::check_finished(const std::optional<UpdateManifest>& manifest) {
    if (snapshot_.state != UpdateState::Checking) return;
    if (manifest && compare_versions(manifest->version, snapshot_.current_version) > 0) {
        snapshot_.available = manifest;
        snapshot_.state = UpdateState::Available;
    } else {
        snapshot_.available.reset();
        snapshot_.state = UpdateState::UpToDate;
    }
    changed();
}

void OtaUpdateModel::check_failed(const std::string& code) {
    if (snapshot_.state != UpdateState::Checking) return;
    snapshot_.state = UpdateState::Failed;
    snapshot_.error_code = sanitize_text(code, 48);
    changed();
}

UpdateBlock OtaUpdateModel::can_install(bool online, int battery_percent, bool battery_known, bool charging) const {
    return update_block(snapshot_, online, battery_percent, battery_known, charging);
}

UpdateBlock update_block(const UpdateSnapshot& snapshot, bool online, int battery_percent, bool battery_known,
                         bool charging) noexcept {
    if (snapshot.state != UpdateState::Available || !snapshot.available) {
        return snapshot.state == UpdateState::Downloading || snapshot.state == UpdateState::Verifying ?
            UpdateBlock::Busy : UpdateBlock::NoUpdate;
    }
    if (!online) return UpdateBlock::Offline;
    // An unknown battery level is treated as low unless the device charges.
    if (!charging && (!battery_known || battery_percent < OtaUpdateModel::kMinBatteryPercent)) {
        return UpdateBlock::LowBattery;
    }
    return UpdateBlock::None;
}

bool OtaUpdateModel::begin_download() {
    if (snapshot_.state != UpdateState::Available || !snapshot_.available) return false;
    snapshot_.state = UpdateState::Downloading;
    snapshot_.progress = 0.0F;
    changed();
    return true;
}

void OtaUpdateModel::set_progress(float progress) {
    if (snapshot_.state != UpdateState::Downloading) return;
    const float clamped = std::clamp(progress, 0.0F, 1.0F);
    // Whole percent steps: the screen does not need more redraws.
    if (static_cast<int>(clamped * 100.0F) == static_cast<int>(snapshot_.progress * 100.0F)) return;
    snapshot_.progress = clamped;
    changed();
}

void OtaUpdateModel::begin_verify() {
    if (snapshot_.state != UpdateState::Downloading) return;
    snapshot_.state = UpdateState::Verifying;
    snapshot_.progress = 1.0F;
    changed();
}

void OtaUpdateModel::ready_to_restart() {
    if (snapshot_.state != UpdateState::Verifying) return;
    snapshot_.state = UpdateState::ReadyToRestart;
    changed();
}

void OtaUpdateModel::fail(const std::string& code) {
    snapshot_.state = UpdateState::Failed;
    snapshot_.error_code = sanitize_text(code, 48);
    snapshot_.progress = 0.0F;
    changed();
}

void OtaUpdateModel::reset() {
    snapshot_.state = UpdateState::Idle;
    snapshot_.available.reset();
    snapshot_.progress = 0.0F;
    snapshot_.error_code.clear();
    changed();
}

const char* update_state_name(UpdateState state) noexcept {
    switch (state) {
        case UpdateState::Idle: return "IDLE";
        case UpdateState::Checking: return "CHECKING";
        case UpdateState::UpToDate: return "UP_TO_DATE";
        case UpdateState::Available: return "AVAILABLE";
        case UpdateState::Downloading: return "DOWNLOADING";
        case UpdateState::Verifying: return "VERIFYING";
        case UpdateState::ReadyToRestart: return "READY_TO_RESTART";
        case UpdateState::Failed: return "FAILED";
    }
    return "UNKNOWN";
}

}  // namespace aura

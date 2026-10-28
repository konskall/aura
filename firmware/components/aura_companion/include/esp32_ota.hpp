#pragma once

#include "aura/ota_update.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace aura::firmware {

struct OtaEvent {
    enum class Kind { CheckDone, CheckFailed, Progress, Verifying, Ready, Failed };
    Kind kind{Kind::CheckFailed};
    std::optional<UpdateManifest> manifest;
    float progress{0.0F};
    std::string error_code;
};

// Over-the-air updates: an HTTPS manifest (NVS "aura_ota"/"manifest_url",
// empty by default), then a streamed download into the inactive OTA slot
// with a SHA-256 check against the manifest before the boot partition
// changes. Rollback is on: a new image that does not confirm itself after
// boot (confirm_running_image) is reverted by the bootloader.
// NOT COMPILED, NOT VERIFIED ON HARDWARE.
class Esp32Ota {
public:
    bool initialize();
    bool configured() const noexcept { return !manifest_url_.empty(); }
    std::string running_version() const;
    bool request_check();
    bool request_install(const UpdateManifest& manifest);
    bool poll(OtaEvent& event);
    void restart();
    // Marks the running image good once the UI has run for a while.
    static void confirm_running_image();

private:
    static void check_task(void* argument);
    static void install_task(void* argument);
    const char* install(const UpdateManifest& manifest);
    void push(OtaEvent event);

    std::string manifest_url_;
    UpdateManifest pending_;
    std::atomic<bool> busy_{false};
    std::mutex mutex_;
    std::vector<OtaEvent> events_;
};

}  // namespace aura::firmware

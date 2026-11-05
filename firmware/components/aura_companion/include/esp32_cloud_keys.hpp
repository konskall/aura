#pragma once

#include <atomic>
#include <cstdint>

#include "aura/cloud_keys.hpp"

namespace aura::firmware {

// The assistant's keys in NVS (namespace "aura_cloud"), which is encrypted on
// every build (CONFIG_NVS_ENCRYPTION). They arrive from the owner's PC over the
// code-protected transfer session and are never logged. NOT VERIFIED ON HARDWARE.
class Esp32CloudKeyStore final : public ICloudKeySink {
public:
    // The keys kept, or empty ones.
    CloudKeys load() const;
    bool store(const CloudKeys& keys) override;
    // Changes with every store, so the main loop can start the assistant with new keys.
    std::uint32_t generation() const noexcept { return generation_.load(); }

private:
    std::atomic<std::uint32_t> generation_{0};
};

}  // namespace aura::firmware

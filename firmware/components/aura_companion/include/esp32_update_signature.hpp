#pragma once

#include "aura/ota_update.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace aura::firmware {

// The owner's update signature, checked with mbedTLS: ECDSA P-256 over SHA-256,
// with the public key as 130 hex characters (04 || X || Y) from
// CONFIG_AURA_OTA_PUBLIC_KEY. NOT VERIFIED ON HARDWARE.
class Esp32UpdateSignatureVerifier final : public IUpdateSignatureVerifier {
public:
    explicit Esp32UpdateSignatureVerifier(const char* public_key_hex);

    bool configured() const override { return configured_; }
    bool verify(const std::string& message, const std::array<std::uint8_t, 64>& signature) const override;

private:
    std::array<std::uint8_t, 65> point_{};  // 04 || X || Y
    bool configured_{false};
};

}  // namespace aura::firmware

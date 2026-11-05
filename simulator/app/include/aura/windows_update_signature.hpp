#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "aura/ota_update.hpp"

namespace aura::simulator {

// The owner's update signature, checked with Windows BCrypt: ECDSA P-256 over
// SHA-256, with the public key as 130 hex characters (04 || X || Y), as
// scripts/update-signing.mjs prints it.
class WindowsUpdateSignatureVerifier final : public IUpdateSignatureVerifier {
public:
    explicit WindowsUpdateSignatureVerifier(const std::string& public_key_hex);

    bool configured() const override { return configured_; }
    bool verify(const std::string& message, const std::array<std::uint8_t, 64>& signature) const override;

private:
    std::array<std::uint8_t, 64> point_{};  // X || Y
    bool configured_{false};
};

}  // namespace aura::simulator

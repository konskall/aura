#pragma once

#include "aura/aoraton_crypto.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura::firmware {

class Esp32AoratonCryptoV2 final : public IAoratonCryptoV2 {
public:
    bool encrypt_text(const std::string& plaintext,
                      const AoratonRoomKey& room_key,
                      std::string& wire,
                      AoratonCryptoError& error) override;
    bool decrypt_text(const std::string& wire,
                      const AoratonRoomKey& room_key,
                      std::string& plaintext,
                      AoratonCryptoError& error) override;
    bool pin_verifier(const std::string& room_name,
                      const std::string& pin,
                      std::string& verifier) override;
    bool room_key_verifier(const AoratonRoomKey& room_key,
                           std::string& verifier) override;
    bool safety_code_material(const AoratonRoomKey& room_key,
                              std::string& material) override;

private:
    static bool derive_subkey(const AoratonRoomKey& room_key,
                              const char* info,
                              std::array<std::uint8_t, 32>& key);
    static bool encrypt_gcm(const std::array<std::uint8_t, 32>& key,
                            const std::uint8_t* iv,
                            const std::string& plaintext,
                            std::vector<std::uint8_t>& ciphertext_and_tag);
    static bool decrypt_gcm(const std::array<std::uint8_t, 32>& key,
                            const AoratonV2Envelope& envelope,
                            std::vector<std::uint8_t>& plaintext,
                            bool& authentication_failed);
};

}  // namespace aura::firmware

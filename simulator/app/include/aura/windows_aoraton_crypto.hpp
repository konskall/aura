#pragma once

#include "aura/aoraton_account.hpp"
#include "aura/aoraton_crypto.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aura {

class WindowsAoratonCryptoV2 final : public IAoratonCryptoV2 {
    friend class WindowsAoratonAccountCrypto;

public:
    using RandomSource = std::function<bool(std::uint8_t*, std::size_t)>;

    explicit WindowsAoratonCryptoV2(RandomSource random_source = {});

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
    bool fill_random(std::uint8_t* destination, std::size_t size) const;
    static bool sha256(const std::uint8_t* data,
                       std::size_t size,
                       std::array<std::uint8_t, 32>& digest);
    static bool hmac_sha256(const std::uint8_t* secret,
                            std::size_t secret_size,
                            const std::uint8_t* data,
                            std::size_t data_size,
                            std::array<std::uint8_t, 32>& digest);
    static bool derive_subkey(const AoratonRoomKey& room_key,
                              const char* info,
                              std::array<std::uint8_t, 32>& subkey);
    static bool aes_gcm_encrypt(const std::array<std::uint8_t, 32>& key,
                                const std::uint8_t* iv,
                                const std::uint8_t* plaintext,
                                std::size_t plaintext_size,
                                std::vector<std::uint8_t>& ciphertext_and_tag);
    static bool aes_gcm_decrypt(const std::array<std::uint8_t, 32>& key,
                                const std::uint8_t* iv,
                                const std::vector<std::uint8_t>& ciphertext_and_tag,
                                std::vector<std::uint8_t>& plaintext,
                                bool& authentication_failed);

    RandomSource random_source_;
};

// Account-level primitives (PKCE, the key vault) over the same CNG providers.
class WindowsAoratonAccountCrypto final : public IAoratonAccountCrypto {
public:
    bool random_bytes(std::uint8_t* destination, std::size_t size) override;
    bool sha256(const std::uint8_t* data, std::size_t size, std::array<std::uint8_t, 32>& digest) override;
    bool pbkdf2_sha256(const std::string& passphrase,
                       const std::vector<std::uint8_t>& salt,
                       std::uint32_t iterations,
                       std::array<std::uint8_t, 32>& key) override;
    bool aes_gcm_open(const std::array<std::uint8_t, 32>& key,
                      const std::string& wire,
                      std::string& plaintext) override;
};

}  // namespace aura

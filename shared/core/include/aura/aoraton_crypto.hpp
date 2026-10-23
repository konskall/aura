#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {

constexpr std::size_t kAoratonRoomKeyBytes = 32;
constexpr std::size_t kAoratonGcmIvBytes = 12;
constexpr std::size_t kAoratonGcmTagBytes = 16;
// The audited Aoraton database guard caps messages.text at 2048 characters.
// 1500 plaintext bytes always fit after v2 framing and base64url expansion.
constexpr std::size_t kAoratonMaxPlaintextBytes = 1500;
constexpr std::size_t kAoratonMaxEnvelopeChars = 2048;

enum class AoratonCryptoError {
    None,
    EmptyPlaintext,
    PlaintextTooLarge,
    InvalidUtf8,
    InvalidEnvelope,
    UnsupportedVersion,
    InvalidBase64Url,
    InvalidIvLength,
    TruncatedCiphertext,
    CiphertextTooLarge,
    AuthenticationFailed,
    RandomFailure,
    PlatformFailure,
};

class AoratonRoomKey final {
public:
    AoratonRoomKey() = default;
    explicit AoratonRoomKey(const std::array<std::uint8_t, kAoratonRoomKeyBytes>& bytes);
    ~AoratonRoomKey();

    AoratonRoomKey(const AoratonRoomKey&) = delete;
    AoratonRoomKey& operator=(const AoratonRoomKey&) = delete;
    AoratonRoomKey(AoratonRoomKey&& other) noexcept;
    AoratonRoomKey& operator=(AoratonRoomKey&& other) noexcept;

    const std::uint8_t* data() const noexcept { return bytes_.data(); }
    std::size_t size() const noexcept { return bytes_.size(); }
    bool is_present() const noexcept { return present_; }
    void clear() noexcept;

private:
    std::array<std::uint8_t, kAoratonRoomKeyBytes> bytes_{};
    bool present_{false};
};

struct AoratonV2Envelope {
    std::array<std::uint8_t, kAoratonGcmIvBytes> iv{};
    std::vector<std::uint8_t> ciphertext_and_tag;
};

bool aoraton_is_valid_utf8(const std::string& value) noexcept;
std::string aoraton_base64url_encode(const std::uint8_t* data, std::size_t size);
bool aoraton_base64url_decode(const std::string& encoded, std::vector<std::uint8_t>& decoded) noexcept;
bool aoraton_parse_v2_envelope(const std::string& wire,
                               AoratonV2Envelope& envelope,
                               AoratonCryptoError& error) noexcept;
std::string aoraton_serialize_v2_envelope(const std::uint8_t* iv,
                                         const std::vector<std::uint8_t>& ciphertext_and_tag);
const char* aoraton_crypto_error_name(AoratonCryptoError error) noexcept;

class IAoratonCryptoV2 {
public:
    virtual ~IAoratonCryptoV2() = default;

    virtual bool encrypt_text(const std::string& plaintext,
                              const AoratonRoomKey& room_key,
                              std::string& wire,
                              AoratonCryptoError& error) = 0;
    virtual bool decrypt_text(const std::string& wire,
                              const AoratonRoomKey& room_key,
                              std::string& plaintext,
                              AoratonCryptoError& error) = 0;
    virtual bool pin_verifier(const std::string& room_name,
                              const std::string& pin,
                              std::string& verifier) = 0;
    virtual bool room_key_verifier(const AoratonRoomKey& room_key,
                                   std::string& verifier) = 0;
    virtual bool safety_code_material(const AoratonRoomKey& room_key,
                                      std::string& material) = 0;
};

}  // namespace aura

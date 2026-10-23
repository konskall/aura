#include "aura/esp32_aoraton_crypto.hpp"

#include "esp_random.h"
#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <utility>

namespace aura::firmware {
namespace {

constexpr char kMessageInfo[] = "inco/e2ee/v2/msg";
constexpr char kAuthInfo[] = "inco/e2ee/v2/auth";
constexpr char kSafetyInfo[] = "inco/e2ee/v2/safety";

std::string trim_ascii(std::string value) {
    const auto whitespace = [](unsigned char character) { return std::isspace(character) != 0; };
    while (!value.empty() && whitespace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && whitespace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string normalized_room_name(std::string value) {
    value = trim_ascii(std::move(value));
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character + ('a' - 'A'));
    }
    return value;
}

void clear_vector(std::vector<std::uint8_t>& value) {
    if (!value.empty()) mbedtls_platform_zeroize(value.data(), value.size());
    value.clear();
}

}  // namespace

bool Esp32AoratonCryptoV2::derive_subkey(const AoratonRoomKey& room_key,
                                         const char* info,
                                         std::array<std::uint8_t, 32>& key) {
    if (!room_key.is_present() || !info) return false;
    const mbedtls_md_info_t* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!sha256) return false;
    std::array<std::uint8_t, 32> salt{};
    return mbedtls_hkdf(sha256, salt.data(), salt.size(), room_key.data(), room_key.size(),
                        reinterpret_cast<const std::uint8_t*>(info), std::strlen(info),
                        key.data(), key.size()) == 0;
}

bool Esp32AoratonCryptoV2::encrypt_gcm(const std::array<std::uint8_t, 32>& key,
                                       const std::uint8_t* iv,
                                       const std::string& plaintext,
                                       std::vector<std::uint8_t>& ciphertext_and_tag) {
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    if (mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key.data(), 256) != 0) {
        mbedtls_gcm_free(&context);
        return false;
    }
    ciphertext_and_tag.assign(plaintext.size() + kAoratonGcmTagBytes, 0);
    const int result = mbedtls_gcm_crypt_and_tag(
        &context, MBEDTLS_GCM_ENCRYPT, plaintext.size(), iv, kAoratonGcmIvBytes,
        nullptr, 0, reinterpret_cast<const std::uint8_t*>(plaintext.data()),
        ciphertext_and_tag.data(), kAoratonGcmTagBytes,
        ciphertext_and_tag.data() + plaintext.size());
    mbedtls_gcm_free(&context);
    if (result != 0) clear_vector(ciphertext_and_tag);
    return result == 0;
}

bool Esp32AoratonCryptoV2::decrypt_gcm(const std::array<std::uint8_t, 32>& key,
                                       const AoratonV2Envelope& envelope,
                                       std::vector<std::uint8_t>& plaintext,
                                       bool& authentication_failed) {
    authentication_failed = false;
    if (envelope.ciphertext_and_tag.size() < kAoratonGcmTagBytes) return false;
    const std::size_t ciphertext_size = envelope.ciphertext_and_tag.size() - kAoratonGcmTagBytes;
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    if (mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key.data(), 256) != 0) {
        mbedtls_gcm_free(&context);
        return false;
    }
    plaintext.assign(ciphertext_size, 0);
    const int result = mbedtls_gcm_auth_decrypt(
        &context, ciphertext_size, envelope.iv.data(), envelope.iv.size(), nullptr, 0,
        envelope.ciphertext_and_tag.data() + ciphertext_size, kAoratonGcmTagBytes,
        envelope.ciphertext_and_tag.data(), plaintext.data());
    mbedtls_gcm_free(&context);
    if (result != 0) {
        authentication_failed = result == MBEDTLS_ERR_GCM_AUTH_FAILED;
        clear_vector(plaintext);
    }
    return result == 0;
}

bool Esp32AoratonCryptoV2::encrypt_text(const std::string& plaintext,
                                        const AoratonRoomKey& room_key,
                                        std::string& wire,
                                        AoratonCryptoError& error) {
    wire.clear();
    if (plaintext.empty()) { error = AoratonCryptoError::EmptyPlaintext; return false; }
    if (plaintext.size() > kAoratonMaxPlaintextBytes) { error = AoratonCryptoError::PlaintextTooLarge; return false; }
    if (!aoraton_is_valid_utf8(plaintext)) { error = AoratonCryptoError::InvalidUtf8; return false; }
    std::array<std::uint8_t, 32> key{};
    if (!derive_subkey(room_key, kMessageInfo, key)) { error = AoratonCryptoError::PlatformFailure; return false; }
    std::array<std::uint8_t, kAoratonGcmIvBytes> iv{};
    esp_fill_random(iv.data(), iv.size());
    std::vector<std::uint8_t> encrypted;
    const bool ok = encrypt_gcm(key, iv.data(), plaintext, encrypted);
    mbedtls_platform_zeroize(key.data(), key.size());
    if (!ok) { error = AoratonCryptoError::PlatformFailure; return false; }
    wire = aoraton_serialize_v2_envelope(iv.data(), encrypted);
    clear_vector(encrypted);
    mbedtls_platform_zeroize(iv.data(), iv.size());
    if (wire.size() > kAoratonMaxEnvelopeChars) {
        wire.clear();
        error = AoratonCryptoError::CiphertextTooLarge;
        return false;
    }
    error = AoratonCryptoError::None;
    return true;
}

bool Esp32AoratonCryptoV2::decrypt_text(const std::string& wire,
                                        const AoratonRoomKey& room_key,
                                        std::string& plaintext,
                                        AoratonCryptoError& error) {
    plaintext.clear();
    AoratonV2Envelope envelope;
    if (!aoraton_parse_v2_envelope(wire, envelope, error)) return false;
    std::array<std::uint8_t, 32> key{};
    if (!derive_subkey(room_key, kMessageInfo, key)) { error = AoratonCryptoError::PlatformFailure; return false; }
    std::vector<std::uint8_t> decoded;
    bool authentication_failed = false;
    const bool ok = decrypt_gcm(key, envelope, decoded, authentication_failed);
    mbedtls_platform_zeroize(key.data(), key.size());
    clear_vector(envelope.ciphertext_and_tag);
    if (!ok) {
        error = authentication_failed ? AoratonCryptoError::AuthenticationFailed : AoratonCryptoError::PlatformFailure;
        return false;
    }
    plaintext.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    clear_vector(decoded);
    if (plaintext.size() > kAoratonMaxPlaintextBytes || !aoraton_is_valid_utf8(plaintext)) {
        if (!plaintext.empty()) mbedtls_platform_zeroize(&plaintext[0], plaintext.size());
        plaintext.clear();
        error = AoratonCryptoError::InvalidUtf8;
        return false;
    }
    error = AoratonCryptoError::None;
    return true;
}

bool Esp32AoratonCryptoV2::pin_verifier(const std::string& room_name,
                                        const std::string& pin,
                                        std::string& verifier) {
    const std::string input = std::string("inco/e2ee/v2/pin|") + normalized_room_name(room_name) + "|" + trim_ascii(pin);
    std::array<std::uint8_t, 32> digest{};
    if (mbedtls_sha256(reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), digest.data(), 0) != 0) return false;
    verifier = aoraton_base64url_encode(digest.data(), digest.size());
    mbedtls_platform_zeroize(digest.data(), digest.size());
    return true;
}

bool Esp32AoratonCryptoV2::room_key_verifier(const AoratonRoomKey& room_key,
                                             std::string& verifier) {
    std::array<std::uint8_t, 32> digest{};
    if (!derive_subkey(room_key, kAuthInfo, digest)) return false;
    verifier = aoraton_base64url_encode(digest.data(), digest.size());
    mbedtls_platform_zeroize(digest.data(), digest.size());
    return true;
}

bool Esp32AoratonCryptoV2::safety_code_material(const AoratonRoomKey& room_key,
                                                std::string& material) {
    std::array<std::uint8_t, 32> digest{};
    if (!derive_subkey(room_key, kSafetyInfo, digest)) return false;
    material = aoraton_base64url_encode(digest.data(), digest.size());
    mbedtls_platform_zeroize(digest.data(), digest.size());
    return true;
}

}  // namespace aura::firmware

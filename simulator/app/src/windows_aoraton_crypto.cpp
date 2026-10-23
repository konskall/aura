#include "aura/windows_aoraton_crypto.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <utility>

namespace aura {
namespace {

constexpr char kMessageInfo[] = "inco/e2ee/v2/msg";
constexpr char kAuthInfo[] = "inco/e2ee/v2/auth";
constexpr char kSafetyInfo[] = "inco/e2ee/v2/safety";

bool fits_ulong(std::size_t value) noexcept {
    return value <= static_cast<std::size_t>(std::numeric_limits<ULONG>::max());
}

void clear_bytes(void* pointer, std::size_t size) noexcept {
    if (pointer && size) SecureZeroMemory(pointer, size);
}

class AlgorithmHandle final {
public:
    ~AlgorithmHandle() { if (value_) BCryptCloseAlgorithmProvider(value_, 0); }
    BCRYPT_ALG_HANDLE* out() noexcept { return &value_; }
    operator BCRYPT_ALG_HANDLE() const noexcept { return value_; }
private:
    BCRYPT_ALG_HANDLE value_{nullptr};
};

class HashHandle final {
public:
    ~HashHandle() { if (value_) BCryptDestroyHash(value_); }
    BCRYPT_HASH_HANDLE* out() noexcept { return &value_; }
    operator BCRYPT_HASH_HANDLE() const noexcept { return value_; }
private:
    BCRYPT_HASH_HANDLE value_{nullptr};
};

class KeyHandle final {
public:
    ~KeyHandle() { if (value_) BCryptDestroyKey(value_); }
    BCRYPT_KEY_HANDLE* out() noexcept { return &value_; }
    operator BCRYPT_KEY_HANDLE() const noexcept { return value_; }
private:
    BCRYPT_KEY_HANDLE value_{nullptr};
};

bool property_ulong(BCRYPT_HANDLE handle, LPCWSTR name, ULONG& value) {
    ULONG received = 0;
    return BCryptGetProperty(handle, name, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                             &received, 0) >= 0 && received == sizeof(value);
}

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

}  // namespace

WindowsAoratonCryptoV2::WindowsAoratonCryptoV2(RandomSource random_source)
    : random_source_(std::move(random_source)) {}

bool WindowsAoratonCryptoV2::fill_random(std::uint8_t* destination, std::size_t size) const {
    if (random_source_) return random_source_(destination, size);
    if (!fits_ulong(size)) return false;
    return BCryptGenRandom(nullptr, destination, static_cast<ULONG>(size),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

bool WindowsAoratonCryptoV2::sha256(const std::uint8_t* data,
                                    std::size_t size,
                                    std::array<std::uint8_t, 32>& digest) {
    if (!fits_ulong(size)) return false;
    AlgorithmHandle algorithm;
    if (BCryptOpenAlgorithmProvider(algorithm.out(), BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    ULONG object_size = 0;
    ULONG hash_size = 0;
    if (!property_ulong(algorithm, BCRYPT_OBJECT_LENGTH, object_size) ||
        !property_ulong(algorithm, BCRYPT_HASH_LENGTH, hash_size) || hash_size != digest.size()) return false;
    std::vector<std::uint8_t> object(object_size);
    HashHandle hash;
    if (BCryptCreateHash(algorithm, hash.out(), object.data(), object_size, nullptr, 0, 0) < 0 ||
        (size > 0 && BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) < 0) ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        clear_bytes(object.data(), object.size());
        return false;
    }
    clear_bytes(object.data(), object.size());
    return true;
}

bool WindowsAoratonCryptoV2::hmac_sha256(const std::uint8_t* secret,
                                         std::size_t secret_size,
                                         const std::uint8_t* data,
                                         std::size_t data_size,
                                         std::array<std::uint8_t, 32>& digest) {
    if (!fits_ulong(secret_size) || !fits_ulong(data_size)) return false;
    AlgorithmHandle algorithm;
    if (BCryptOpenAlgorithmProvider(algorithm.out(), BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return false;
    ULONG object_size = 0;
    ULONG hash_size = 0;
    if (!property_ulong(algorithm, BCRYPT_OBJECT_LENGTH, object_size) ||
        !property_ulong(algorithm, BCRYPT_HASH_LENGTH, hash_size) || hash_size != digest.size()) return false;
    std::vector<std::uint8_t> object(object_size);
    HashHandle hash;
    if (BCryptCreateHash(algorithm, hash.out(), object.data(), object_size,
                         const_cast<PUCHAR>(secret), static_cast<ULONG>(secret_size), 0) < 0 ||
        (data_size > 0 && BCryptHashData(hash, const_cast<PUCHAR>(data),
                                         static_cast<ULONG>(data_size), 0) < 0) ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        clear_bytes(object.data(), object.size());
        return false;
    }
    clear_bytes(object.data(), object.size());
    return true;
}

bool WindowsAoratonCryptoV2::derive_subkey(const AoratonRoomKey& room_key,
                                           const char* info,
                                           std::array<std::uint8_t, 32>& subkey) {
    if (!room_key.is_present() || !info) return false;
    std::array<std::uint8_t, 32> zero_salt{};
    std::array<std::uint8_t, 32> pseudorandom_key{};
    if (!hmac_sha256(zero_salt.data(), zero_salt.size(), room_key.data(), room_key.size(),
                     pseudorandom_key)) return false;
    const std::size_t info_size = std::strlen(info);
    std::vector<std::uint8_t> expand(info_size + 1);
    std::memcpy(expand.data(), info, info_size);
    expand.back() = 0x01;
    const bool ok = hmac_sha256(pseudorandom_key.data(), pseudorandom_key.size(),
                                expand.data(), expand.size(), subkey);
    clear_bytes(pseudorandom_key.data(), pseudorandom_key.size());
    clear_bytes(expand.data(), expand.size());
    return ok;
}

bool WindowsAoratonCryptoV2::aes_gcm_encrypt(
    const std::array<std::uint8_t, 32>& key,
    const std::uint8_t* iv,
    const std::uint8_t* plaintext,
    std::size_t plaintext_size,
    std::vector<std::uint8_t>& ciphertext_and_tag) {
    if (!fits_ulong(plaintext_size)) return false;
    AlgorithmHandle algorithm;
    if (BCryptOpenAlgorithmProvider(algorithm.out(), BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) return false;
    if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0) return false;
    ULONG object_size = 0;
    if (!property_ulong(algorithm, BCRYPT_OBJECT_LENGTH, object_size)) return false;
    std::vector<std::uint8_t> key_object(object_size);
    KeyHandle key_handle;
    if (BCryptGenerateSymmetricKey(algorithm, key_handle.out(), key_object.data(), object_size,
                                   const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) < 0) {
        clear_bytes(key_object.data(), key_object.size());
        return false;
    }

    std::vector<std::uint8_t> ciphertext(plaintext_size);
    std::array<std::uint8_t, kAoratonGcmTagBytes> tag{};
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authentication;
    BCRYPT_INIT_AUTH_MODE_INFO(authentication);
    authentication.pbNonce = const_cast<PUCHAR>(iv);
    authentication.cbNonce = static_cast<ULONG>(kAoratonGcmIvBytes);
    authentication.pbTag = tag.data();
    authentication.cbTag = static_cast<ULONG>(tag.size());
    ULONG written = 0;
    const NTSTATUS status = BCryptEncrypt(
        key_handle, const_cast<PUCHAR>(plaintext), static_cast<ULONG>(plaintext_size),
        &authentication, nullptr, 0, ciphertext.empty() ? nullptr : ciphertext.data(),
        static_cast<ULONG>(ciphertext.size()), &written, 0);
    clear_bytes(key_object.data(), key_object.size());
    if (status < 0 || written != plaintext_size) {
        clear_bytes(ciphertext.data(), ciphertext.size());
        clear_bytes(tag.data(), tag.size());
        return false;
    }
    ciphertext_and_tag = std::move(ciphertext);
    ciphertext_and_tag.insert(ciphertext_and_tag.end(), tag.begin(), tag.end());
    clear_bytes(tag.data(), tag.size());
    return true;
}

bool WindowsAoratonCryptoV2::aes_gcm_decrypt(
    const std::array<std::uint8_t, 32>& key,
    const std::uint8_t* iv,
    const std::vector<std::uint8_t>& ciphertext_and_tag,
    std::vector<std::uint8_t>& plaintext,
    bool& authentication_failed) {
    authentication_failed = false;
    if (ciphertext_and_tag.size() < kAoratonGcmTagBytes || !fits_ulong(ciphertext_and_tag.size())) return false;
    const std::size_t ciphertext_size = ciphertext_and_tag.size() - kAoratonGcmTagBytes;
    AlgorithmHandle algorithm;
    if (BCryptOpenAlgorithmProvider(algorithm.out(), BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) return false;
    if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0) return false;
    ULONG object_size = 0;
    if (!property_ulong(algorithm, BCRYPT_OBJECT_LENGTH, object_size)) return false;
    std::vector<std::uint8_t> key_object(object_size);
    KeyHandle key_handle;
    if (BCryptGenerateSymmetricKey(algorithm, key_handle.out(), key_object.data(), object_size,
                                   const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) < 0) {
        clear_bytes(key_object.data(), key_object.size());
        return false;
    }

    plaintext.assign(ciphertext_size, 0);
    std::array<std::uint8_t, kAoratonGcmTagBytes> tag{};
    std::copy(ciphertext_and_tag.end() - static_cast<std::ptrdiff_t>(tag.size()),
              ciphertext_and_tag.end(), tag.begin());
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authentication;
    BCRYPT_INIT_AUTH_MODE_INFO(authentication);
    authentication.pbNonce = const_cast<PUCHAR>(iv);
    authentication.cbNonce = static_cast<ULONG>(kAoratonGcmIvBytes);
    authentication.pbTag = tag.data();
    authentication.cbTag = static_cast<ULONG>(tag.size());
    ULONG written = 0;
    const NTSTATUS status = BCryptDecrypt(
        key_handle, const_cast<PUCHAR>(ciphertext_and_tag.data()),
        static_cast<ULONG>(ciphertext_size), &authentication, nullptr, 0,
        plaintext.empty() ? nullptr : plaintext.data(), static_cast<ULONG>(plaintext.size()),
        &written, 0);
    clear_bytes(key_object.data(), key_object.size());
    clear_bytes(tag.data(), tag.size());
    if (status < 0 || written != ciphertext_size) {
        authentication_failed = status == static_cast<NTSTATUS>(0xC000A002L);
        clear_bytes(plaintext.data(), plaintext.size());
        plaintext.clear();
        return false;
    }
    return true;
}

bool WindowsAoratonCryptoV2::encrypt_text(const std::string& plaintext,
                                          const AoratonRoomKey& room_key,
                                          std::string& wire,
                                          AoratonCryptoError& error) {
    wire.clear();
    if (plaintext.empty()) {
        error = AoratonCryptoError::EmptyPlaintext;
        return false;
    }
    if (plaintext.size() > kAoratonMaxPlaintextBytes) {
        error = AoratonCryptoError::PlaintextTooLarge;
        return false;
    }
    if (!aoraton_is_valid_utf8(plaintext)) {
        error = AoratonCryptoError::InvalidUtf8;
        return false;
    }
    if (!room_key.is_present()) {
        error = AoratonCryptoError::PlatformFailure;
        return false;
    }
    std::array<std::uint8_t, 32> key{};
    std::array<std::uint8_t, kAoratonGcmIvBytes> iv{};
    if (!derive_subkey(room_key, kMessageInfo, key)) {
        error = AoratonCryptoError::PlatformFailure;
        return false;
    }
    if (!fill_random(iv.data(), iv.size())) {
        clear_bytes(key.data(), key.size());
        error = AoratonCryptoError::RandomFailure;
        return false;
    }
    std::vector<std::uint8_t> encrypted;
    const bool ok = aes_gcm_encrypt(key, iv.data(),
                                    reinterpret_cast<const std::uint8_t*>(plaintext.data()),
                                    plaintext.size(), encrypted);
    clear_bytes(key.data(), key.size());
    if (!ok) {
        error = AoratonCryptoError::PlatformFailure;
        return false;
    }
    wire = aoraton_serialize_v2_envelope(iv.data(), encrypted);
    clear_bytes(encrypted.data(), encrypted.size());
    clear_bytes(iv.data(), iv.size());
    if (wire.size() > kAoratonMaxEnvelopeChars) {
        wire.clear();
        error = AoratonCryptoError::CiphertextTooLarge;
        return false;
    }
    error = AoratonCryptoError::None;
    return true;
}

bool WindowsAoratonCryptoV2::decrypt_text(const std::string& wire,
                                          const AoratonRoomKey& room_key,
                                          std::string& plaintext,
                                          AoratonCryptoError& error) {
    plaintext.clear();
    AoratonV2Envelope envelope;
    if (!aoraton_parse_v2_envelope(wire, envelope, error)) return false;
    if (!room_key.is_present()) {
        error = AoratonCryptoError::PlatformFailure;
        return false;
    }
    std::array<std::uint8_t, 32> key{};
    if (!derive_subkey(room_key, kMessageInfo, key)) {
        error = AoratonCryptoError::PlatformFailure;
        return false;
    }
    std::vector<std::uint8_t> decoded;
    bool authentication_failed = false;
    const bool ok = aes_gcm_decrypt(key, envelope.iv.data(), envelope.ciphertext_and_tag,
                                    decoded, authentication_failed);
    clear_bytes(key.data(), key.size());
    clear_bytes(envelope.ciphertext_and_tag.data(), envelope.ciphertext_and_tag.size());
    if (!ok) {
        error = authentication_failed ? AoratonCryptoError::AuthenticationFailed
                                      : AoratonCryptoError::PlatformFailure;
        return false;
    }
    plaintext.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    clear_bytes(decoded.data(), decoded.size());
    if (plaintext.size() > kAoratonMaxPlaintextBytes) {
        clear_bytes(&plaintext[0], plaintext.size());
        plaintext.clear();
        error = AoratonCryptoError::PlaintextTooLarge;
        return false;
    }
    if (!aoraton_is_valid_utf8(plaintext)) {
        clear_bytes(&plaintext[0], plaintext.size());
        plaintext.clear();
        error = AoratonCryptoError::InvalidUtf8;
        return false;
    }
    error = AoratonCryptoError::None;
    return true;
}

bool WindowsAoratonCryptoV2::pin_verifier(const std::string& room_name,
                                          const std::string& pin,
                                          std::string& verifier) {
    const std::string input = std::string("inco/e2ee/v2/pin|") + normalized_room_name(room_name) + "|" + trim_ascii(pin);
    std::array<std::uint8_t, 32> digest{};
    if (!sha256(reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), digest)) return false;
    verifier = aoraton_base64url_encode(digest.data(), digest.size());
    clear_bytes(digest.data(), digest.size());
    return true;
}

bool WindowsAoratonCryptoV2::room_key_verifier(const AoratonRoomKey& room_key,
                                               std::string& verifier) {
    std::array<std::uint8_t, 32> digest{};
    if (!derive_subkey(room_key, kAuthInfo, digest)) return false;
    verifier = aoraton_base64url_encode(digest.data(), digest.size());
    clear_bytes(digest.data(), digest.size());
    return true;
}

bool WindowsAoratonCryptoV2::safety_code_material(const AoratonRoomKey& room_key,
                                                  std::string& material) {
    std::array<std::uint8_t, 32> digest{};
    if (!derive_subkey(room_key, kSafetyInfo, digest)) return false;
    material = aoraton_base64url_encode(digest.data(), digest.size());
    clear_bytes(digest.data(), digest.size());
    return true;
}

bool WindowsAoratonAccountCrypto::random_bytes(std::uint8_t* destination, std::size_t size) {
    return destination && fits_ulong(size) &&
           BCryptGenRandom(nullptr, destination, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

bool WindowsAoratonAccountCrypto::sha256(const std::uint8_t* data,
                                         std::size_t size,
                                         std::array<std::uint8_t, 32>& digest) {
    return WindowsAoratonCryptoV2::sha256(data, size, digest);
}

bool WindowsAoratonAccountCrypto::pbkdf2_sha256(const std::string& passphrase,
                                                const std::vector<std::uint8_t>& salt,
                                                std::uint32_t iterations,
                                                std::array<std::uint8_t, 32>& key) {
    if (iterations == 0 || !fits_ulong(passphrase.size()) || !fits_ulong(salt.size())) return false;
    AlgorithmHandle algorithm;
    if (BCryptOpenAlgorithmProvider(algorithm.out(), BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return false;
    return BCryptDeriveKeyPBKDF2(algorithm,
                                 reinterpret_cast<PUCHAR>(const_cast<char*>(passphrase.data())),
                                 static_cast<ULONG>(passphrase.size()),
                                 const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
                                 iterations, key.data(), static_cast<ULONG>(key.size()), 0) >= 0;
}

bool WindowsAoratonAccountCrypto::aes_gcm_open(const std::array<std::uint8_t, 32>& key,
                                               const std::string& wire,
                                               std::string& plaintext) {
    plaintext.clear();
    AoratonV2Envelope envelope;
    AoratonCryptoError error = AoratonCryptoError::None;
    if (!aoraton_parse_v2_envelope(wire, envelope, error)) return false;
    std::vector<std::uint8_t> bytes;
    bool authentication_failed = false;
    if (!WindowsAoratonCryptoV2::aes_gcm_decrypt(key, envelope.iv.data(), envelope.ciphertext_and_tag, bytes,
                                                 authentication_failed)) {
        clear_bytes(bytes.data(), bytes.size());
        return false;
    }
    plaintext.assign(bytes.begin(), bytes.end());
    clear_bytes(bytes.data(), bytes.size());
    return true;
}

}  // namespace aura

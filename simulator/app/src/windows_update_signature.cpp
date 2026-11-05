#include "aura/windows_update_signature.hpp"

// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on

#include <cstring>
#include <vector>

namespace aura::simulator {
namespace {

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool sha256(const std::string& message, std::array<std::uint8_t, 32>& digest) {
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                      reinterpret_cast<PUCHAR>(const_cast<char*>(message.data())),
                      static_cast<ULONG>(message.size()), digest.data(), static_cast<ULONG>(digest.size())) == 0;
}

}  // namespace

WindowsUpdateSignatureVerifier::WindowsUpdateSignatureVerifier(const std::string& public_key_hex) {
    // An uncompressed point: 04, then X and Y of 32 bytes each.
    if (public_key_hex.size() != 130 || public_key_hex.compare(0, 2, "04") != 0) return;
    for (std::size_t index = 0; index < point_.size(); ++index) {
        const int high = hex_value(public_key_hex[2 + index * 2]);
        const int low = hex_value(public_key_hex[3 + index * 2]);
        if (high < 0 || low < 0) return;
        point_[index] = static_cast<std::uint8_t>(high * 16 + low);
    }
    configured_ = true;
}

bool WindowsUpdateSignatureVerifier::verify(const std::string& message,
                                            const std::array<std::uint8_t, 64>& signature) const {
    if (!configured_) return false;
    std::array<std::uint8_t, 32> digest{};
    if (!sha256(message, digest)) return false;
    std::vector<std::uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + point_.size());
    BCRYPT_ECCKEY_BLOB header{};
    header.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header.cbKey = 32;
    std::memcpy(blob.data(), &header, sizeof(header));
    std::memcpy(blob.data() + sizeof(header), point_.data(), point_.size());
    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptImportKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(),
                            static_cast<ULONG>(blob.size()), 0) != 0) {
        return false;
    }
    // BCrypt takes the signature as r || s (IEEE P1363), as the manifest carries it.
    const bool valid = BCryptVerifySignature(key, nullptr, digest.data(), static_cast<ULONG>(digest.size()),
                                             const_cast<PUCHAR>(signature.data()),
                                             static_cast<ULONG>(signature.size()), 0) == 0;
    BCryptDestroyKey(key);
    return valid;
}

}  // namespace aura::simulator

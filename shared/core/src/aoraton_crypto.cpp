#include "aura/aoraton_crypto.hpp"

#include <algorithm>
#include <cstring>

namespace aura {
namespace {

void secure_clear(void* pointer, std::size_t size) noexcept {
    volatile auto* bytes = static_cast<volatile std::uint8_t*>(pointer);
    while (size-- > 0) *bytes++ = 0;
}

int base64url_value(char value) noexcept {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '-') return 62;
    if (value == '_') return 63;
    return -1;
}

}  // namespace

AoratonRoomKey::AoratonRoomKey(
    const std::array<std::uint8_t, kAoratonRoomKeyBytes>& bytes)
    : bytes_(bytes), present_(true) {}

AoratonRoomKey::~AoratonRoomKey() { clear(); }

AoratonRoomKey::AoratonRoomKey(AoratonRoomKey&& other) noexcept
    : bytes_(other.bytes_), present_(other.present_) {
    other.clear();
}

AoratonRoomKey& AoratonRoomKey::operator=(AoratonRoomKey&& other) noexcept {
    if (this == &other) return *this;
    clear();
    bytes_ = other.bytes_;
    present_ = other.present_;
    other.clear();
    return *this;
}

void AoratonRoomKey::clear() noexcept {
    secure_clear(bytes_.data(), bytes_.size());
    present_ = false;
}

bool aoraton_is_valid_utf8(const std::string& value) noexcept {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(value.data());
    std::size_t index = 0;
    while (index < value.size()) {
        const std::uint8_t first = bytes[index++];
        if (first <= 0x7f) continue;

        std::uint32_t codepoint = 0;
        std::size_t trailing = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1f;
            trailing = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0f;
            trailing = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07;
            trailing = 3;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (index + trailing > value.size()) return false;
        for (std::size_t offset = 0; offset < trailing; ++offset) {
            const std::uint8_t next = bytes[index++];
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

std::string aoraton_base64url_encode(const std::uint8_t* data, std::size_t size) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    result.reserve((size * 4 + 2) / 3);
    for (std::size_t index = 0; index < size; index += 3) {
        const std::uint32_t a = data[index];
        const std::uint32_t b = index + 1 < size ? data[index + 1] : 0;
        const std::uint32_t c = index + 2 < size ? data[index + 2] : 0;
        const std::uint32_t block = (a << 16) | (b << 8) | c;
        result.push_back(alphabet[(block >> 18) & 0x3f]);
        result.push_back(alphabet[(block >> 12) & 0x3f]);
        if (index + 1 < size) result.push_back(alphabet[(block >> 6) & 0x3f]);
        if (index + 2 < size) result.push_back(alphabet[block & 0x3f]);
    }
    return result;
}

bool aoraton_base64url_decode(const std::string& encoded,
                              std::vector<std::uint8_t>& decoded) noexcept {
    decoded.clear();
    if (encoded.empty() || encoded.size() % 4 == 1) return false;
    decoded.reserve(encoded.size() * 3 / 4);
    std::uint32_t buffer = 0;
    unsigned int bits = 0;
    for (const char byte : encoded) {
        const int value = base64url_value(byte);
        if (value < 0) {
            decoded.clear();
            return false;
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
        }
    }
    // Reject non-canonical encodings whose unused low bits are non-zero.
    if (bits != 0 && (buffer & ((1u << bits) - 1u)) != 0) {
        decoded.clear();
        return false;
    }
    return true;
}

bool aoraton_parse_v2_envelope(const std::string& wire,
                               AoratonV2Envelope& envelope,
                               AoratonCryptoError& error) noexcept {
    envelope = {};
    if (wire.size() > kAoratonMaxEnvelopeChars) {
        error = AoratonCryptoError::CiphertextTooLarge;
        return false;
    }
    const std::size_t first = wire.find(':');
    const std::size_t second = first == std::string::npos ? std::string::npos : wire.find(':', first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        wire.find(':', second + 1) != std::string::npos) {
        error = AoratonCryptoError::InvalidEnvelope;
        return false;
    }
    if (wire.substr(0, first) != "v2") {
        error = AoratonCryptoError::UnsupportedVersion;
        return false;
    }
    std::vector<std::uint8_t> iv;
    if (!aoraton_base64url_decode(wire.substr(first + 1, second - first - 1), iv) ||
        !aoraton_base64url_decode(wire.substr(second + 1), envelope.ciphertext_and_tag)) {
        error = AoratonCryptoError::InvalidBase64Url;
        return false;
    }
    if (iv.size() != kAoratonGcmIvBytes) {
        error = AoratonCryptoError::InvalidIvLength;
        return false;
    }
    if (envelope.ciphertext_and_tag.size() < kAoratonGcmTagBytes) {
        error = AoratonCryptoError::TruncatedCiphertext;
        return false;
    }
    std::copy(iv.begin(), iv.end(), envelope.iv.begin());
    error = AoratonCryptoError::None;
    return true;
}

std::string aoraton_serialize_v2_envelope(
    const std::uint8_t* iv,
    const std::vector<std::uint8_t>& ciphertext_and_tag) {
    return std::string("v2:") + aoraton_base64url_encode(iv, kAoratonGcmIvBytes) + ":" +
           aoraton_base64url_encode(ciphertext_and_tag.data(), ciphertext_and_tag.size());
}

const char* aoraton_crypto_error_name(AoratonCryptoError error) noexcept {
    switch (error) {
        case AoratonCryptoError::None: return "NONE";
        case AoratonCryptoError::EmptyPlaintext: return "EMPTY_PLAINTEXT";
        case AoratonCryptoError::PlaintextTooLarge: return "PLAINTEXT_TOO_LARGE";
        case AoratonCryptoError::InvalidUtf8: return "INVALID_UTF8";
        case AoratonCryptoError::InvalidEnvelope: return "INVALID_ENVELOPE";
        case AoratonCryptoError::UnsupportedVersion: return "UNSUPPORTED_VERSION";
        case AoratonCryptoError::InvalidBase64Url: return "INVALID_BASE64URL";
        case AoratonCryptoError::InvalidIvLength: return "INVALID_IV_LENGTH";
        case AoratonCryptoError::TruncatedCiphertext: return "TRUNCATED_CIPHERTEXT";
        case AoratonCryptoError::CiphertextTooLarge: return "CIPHERTEXT_TOO_LARGE";
        case AoratonCryptoError::AuthenticationFailed: return "AUTHENTICATION_FAILED";
        case AoratonCryptoError::RandomFailure: return "RANDOM_FAILURE";
        case AoratonCryptoError::PlatformFailure: return "PLATFORM_FAILURE";
    }
    return "UNKNOWN";
}

}  // namespace aura

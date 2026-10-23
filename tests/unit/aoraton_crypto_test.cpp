#include "aura/aoraton_crypto.hpp"
#include "aura/windows_aoraton_crypto.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

aura::AoratonRoomKey sequential_key(std::uint8_t offset = 0) {
    std::array<std::uint8_t, aura::kAoratonRoomKeyBytes> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(index + offset);
    }
    return aura::AoratonRoomKey(bytes);
}

}  // namespace

int main() {
    using namespace aura;

    const std::array<std::uint8_t, kAoratonGcmIvBytes> fixed_iv{
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab};
    WindowsAoratonCryptoV2 deterministic([&](std::uint8_t* destination, std::size_t size) {
        if (size != fixed_iv.size()) return false;
        std::copy(fixed_iv.begin(), fixed_iv.end(), destination);
        return true;
    });
    auto key = sequential_key();
    const std::string plaintext = u8"\u0393\u03b5\u03b9\u03b1 \u03c3\u03bf\u03c5 AURA \U0001f44b\n\u03ba\u03b1\u03c6\u03ad e\u0301";
    const std::string browser_wire =
        "v2:oKGio6Slpqeoqaqr:88QR8avMSZBFI1Q9FKfJPRK1uILPjmc1reJIqJwRxRnHrPg52Jyb8axi5UAP8Jz4byJFW00W";

    std::string wire;
    AoratonCryptoError error = AoratonCryptoError::PlatformFailure;
    expect(deterministic.encrypt_text(plaintext, key, wire, error), "device encrypts WebCrypto fixture text");
    expect(error == AoratonCryptoError::None, "successful encryption clears error");
    expect(wire == browser_wire, "device encryption exactly matches browser WebCrypto vector");

    std::string decoded;
    expect(deterministic.decrypt_text(browser_wire, key, decoded, error), "device decrypts browser WebCrypto vector");
    expect(decoded == plaintext, "Greek emoji multiline combining text round-trips");

    std::string verifier;
    expect(deterministic.room_key_verifier(key, verifier), "room-key verifier derives");
    expect(verifier == "s8zFw9nR0eZliujVhttf-njLfgjKxicQ-MExUTGIlwo", "auth HKDF context matches browser");
    expect(deterministic.safety_code_material(key, verifier), "safety material derives");
    expect(verifier == "gBVf77cc-b0WQ3R768_v6ZmRhsxiNDQ9dKSSJeyAyK4", "safety HKDF context matches browser");
    expect(deterministic.pin_verifier("  Family Room  ", " 0420 ", verifier), "PIN verifier hashes");
    expect(verifier == "xx1mQSkgcocp0iVpzLGdxjJzKot3WAB4U2FgUMCh5m8", "PIN lower/trim behavior matches browser");

    AoratonV2Envelope envelope;
    expect(aoraton_parse_v2_envelope(browser_wire, envelope, error), "valid envelope parses");
    expect(envelope.iv == fixed_iv, "parsed IV is exact");
    expect(envelope.ciphertext_and_tag.size() == plaintext.size() + kAoratonGcmTagBytes,
           "WebCrypto tag is appended to ciphertext");

    std::string tampered = browser_wire;
    tampered.back() = tampered.back() == 'A' ? 'B' : 'A';
    expect(!deterministic.decrypt_text(tampered, key, decoded, error), "tampered tag is rejected");
    expect(error == AoratonCryptoError::AuthenticationFailed, "tampered tag reports authentication failure");
    auto wrong_key = sequential_key(1);
    expect(!deterministic.decrypt_text(browser_wire, wrong_key, decoded, error), "wrong key is rejected");
    expect(error == AoratonCryptoError::AuthenticationFailed, "wrong key reports authentication failure");

    expect(!aoraton_parse_v2_envelope("v1:oKGio6Slpqeoqaqr:AAAA", envelope, error) &&
               error == AoratonCryptoError::UnsupportedVersion,
           "legacy or unknown version is never silently downgraded");
    expect(!aoraton_parse_v2_envelope("v2:AAAA:AAAA", envelope, error) &&
               error == AoratonCryptoError::InvalidIvLength,
           "wrong IV length is rejected");
    expect(!aoraton_parse_v2_envelope("v2:oKGio6Slpqeoqaqr:AA=A", envelope, error) &&
               error == AoratonCryptoError::InvalidBase64Url,
           "padded or invalid base64url is rejected");
    expect(!aoraton_parse_v2_envelope("v2:oKGio6Slpqeoqaqr:AAAA", envelope, error) &&
               error == AoratonCryptoError::TruncatedCiphertext,
           "truncated tag is rejected");
    expect(!aoraton_parse_v2_envelope("v2:oKGio6Slpqeoqaqr:AAAA:extra", envelope, error) &&
               error == AoratonCryptoError::InvalidEnvelope,
           "extra envelope fields are rejected");

    const std::string invalid_utf8("\xc0\xaf", 2);
    expect(!deterministic.encrypt_text(invalid_utf8, key, wire, error) &&
               error == AoratonCryptoError::InvalidUtf8,
           "invalid UTF-8 is rejected before encryption");
    expect(!deterministic.encrypt_text("", key, wire, error) &&
               error == AoratonCryptoError::EmptyPlaintext,
           "empty text is not sent");
    expect(!deterministic.encrypt_text(std::string(kAoratonMaxPlaintextBytes + 1, 'x'), key, wire, error) &&
               error == AoratonCryptoError::PlaintextTooLarge,
           "oversize plaintext is bounded");
    const std::string maximum(kAoratonMaxPlaintextBytes, 'x');
    expect(deterministic.encrypt_text(maximum, key, wire, error), "maximum plaintext encrypts");
    expect(wire.size() <= kAoratonMaxEnvelopeChars, "maximum plaintext fits audited database envelope cap");
    expect(deterministic.decrypt_text(wire, key, decoded, error) && decoded == maximum,
           "maximum plaintext decrypts");

    WindowsAoratonCryptoV2 production_random;
    std::string first;
    std::string second;
    expect(production_random.encrypt_text("nonce check", key, first, error), "CNG random encryption one");
    expect(production_random.encrypt_text("nonce check", key, second, error), "CNG random encryption two");
    expect(first != second, "CSPRNG produces a fresh IV per encryption");
    expect(production_random.decrypt_text(first, key, decoded, error) && decoded == "nonce check",
           "production random ciphertext decrypts");

    std::cout << "aoraton_crypto_test: PASS\n";
    return 0;
}

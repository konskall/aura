#include "aura/aoraton_self_test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {
namespace {

constexpr std::array<std::uint8_t, 32> kSha256Abc = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
constexpr std::array<std::uint8_t, 32> kPbkdf2One = {
    0x12, 0x0f, 0xb6, 0xcf, 0xfc, 0xf8, 0xb3, 0x2c, 0x43, 0xe7, 0x22, 0x52, 0x56, 0xc4, 0xf8, 0x37,
    0xa8, 0x65, 0x48, 0xc9, 0x2c, 0xcc, 0x35, 0x48, 0x08, 0x05, 0x98, 0x7c, 0xb7, 0x0b, 0xe1, 0x7b};
constexpr std::array<std::uint8_t, 32> kPbkdf2Many = {
    0xc5, 0xe4, 0x78, 0xd5, 0x92, 0x88, 0xc8, 0x41, 0xaa, 0x53, 0x0d, 0xb6, 0x84, 0x5c, 0x4c, 0x8d,
    0x96, 0x28, 0x93, 0xa0, 0x01, 0xce, 0x4e, 0x11, 0xa4, 0x96, 0x38, 0x73, 0xaa, 0x98, 0x13, 0x4a};
// AES-256-GCM under the key 00..1f with the iv a0..ab, no AAD: the vault's form.
constexpr char kVaultWire[] = "v2:oKGio6Slpqeoqaqr:j3YfQmi9Y8oOEaqwbx-jtV3aaLaiHDYoZvsTfr0n_abUEzY";
constexpr char kVaultPlaintext[] = "inco-vault-check-v1";
// tests/fixtures/aoraton_e2ee_v2_vectors.json (synthetic, room key 00..1f).
constexpr char kMessageWire[] =
    "v2:oKGio6Slpqeoqaqr:88QR8avMSZBFI1Q9FKfJPRK1uILPjmc1reJIqJwRxRnHrPg52Jyb8axi5UAP8Jz4byJFW00W";
constexpr std::uint8_t kMessagePlaintext[] = {
    0xce, 0x93, 0xce, 0xb5, 0xce, 0xb9, 0xce, 0xb1, 0x20, 0xcf, 0x83, 0xce, 0xbf, 0xcf, 0x85, 0x20, 0x41, 0x55,
    0x52, 0x41, 0x20, 0xf0, 0x9f, 0x91, 0x8b, 0x0a, 0xce, 0xba, 0xce, 0xb1, 0xcf, 0x86, 0xce, 0xad, 0x20, 0x65,
    0xcc, 0x81};
constexpr char kRoomKeyVerifier[] = "s8zFw9nR0eZliujVhttf-njLfgjKxicQ-MExUTGIlwo";

std::array<std::uint8_t, 32> counting_key() {
    std::array<std::uint8_t, 32> key{};
    for (std::size_t index = 0; index < key.size(); ++index) key[index] = static_cast<std::uint8_t>(index);
    return key;
}

bool random_ok(IAoratonAccountCrypto& account) {
    std::array<std::uint8_t, 32> first{};
    std::array<std::uint8_t, 32> second{};
    if (!account.random_bytes(first.data(), first.size()) || !account.random_bytes(second.data(), second.size()))
        return false;
    std::size_t repeats = 0;
    for (std::size_t index = 1; index < first.size(); ++index) repeats += first[index] == first[index - 1] ? 1U : 0U;
    return first != second && repeats < first.size() / 2;
}

bool pbkdf2_ok(IAoratonAccountCrypto& account, std::uint32_t rounds, const std::array<std::uint8_t, 32>& expected) {
    const std::vector<std::uint8_t> salt = {'s', 'a', 'l', 't'};
    std::array<std::uint8_t, 32> key{};
    return account.pbkdf2_sha256("password", salt, rounds, key) && key == expected;
}

}  // namespace

AoratonSelfTestResult aoraton_crypto_self_test(IAoratonAccountCrypto& account, IAoratonCryptoV2& message) {
    if (!random_ok(account)) return AoratonSelfTestResult::RandomFailed;

    std::array<std::uint8_t, 32> digest{};
    const std::uint8_t abc[] = {'a', 'b', 'c'};
    if (!account.sha256(abc, sizeof(abc), digest) || digest != kSha256Abc) return AoratonSelfTestResult::Sha256Failed;

    if (!pbkdf2_ok(account, 1, kPbkdf2One) || !pbkdf2_ok(account, 4096, kPbkdf2Many))
        return AoratonSelfTestResult::Pbkdf2Failed;

    const auto key = counting_key();
    std::string opened;
    std::string tampered = kVaultWire;
    tampered[tampered.size() - 2] = tampered[tampered.size() - 2] == 'A' ? 'B' : 'A';
    std::string refused;
    if (!account.aes_gcm_open(key, kVaultWire, opened) || opened != kVaultPlaintext ||
        account.aes_gcm_open(key, tampered, refused))
        return AoratonSelfTestResult::VaultOpenFailed;

    const AoratonRoomKey room_key(key);
    std::string text;
    AoratonCryptoError error = AoratonCryptoError::None;
    const std::string expected(reinterpret_cast<const char*>(kMessagePlaintext), sizeof(kMessagePlaintext));
    std::string wire;
    std::string round_trip;
    if (!message.decrypt_text(kMessageWire, room_key, text, error) || text != expected ||
        !message.encrypt_text(expected, room_key, wire, error) || wire == kMessageWire ||
        !message.decrypt_text(wire, room_key, round_trip, error) || round_trip != expected)
        return AoratonSelfTestResult::MessageFailed;

    std::string verifier;
    if (!message.room_key_verifier(room_key, verifier) || verifier != kRoomKeyVerifier)
        return AoratonSelfTestResult::VerifierFailed;
    return AoratonSelfTestResult::Passed;
}

const char* aoraton_self_test_name(AoratonSelfTestResult result) noexcept {
    switch (result) {
        case AoratonSelfTestResult::Passed: return "PASSED";
        case AoratonSelfTestResult::RandomFailed: return "RANDOM_FAILED";
        case AoratonSelfTestResult::Sha256Failed: return "SHA256_FAILED";
        case AoratonSelfTestResult::Pbkdf2Failed: return "PBKDF2_FAILED";
        case AoratonSelfTestResult::VaultOpenFailed: return "VAULT_OPEN_FAILED";
        case AoratonSelfTestResult::MessageFailed: return "MESSAGE_FAILED";
        case AoratonSelfTestResult::VerifierFailed: return "VERIFIER_FAILED";
    }
    return "UNKNOWN";
}

}  // namespace aura

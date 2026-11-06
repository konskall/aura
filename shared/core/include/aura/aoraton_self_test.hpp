#pragma once

#include "aura/aoraton_account.hpp"
#include "aura/aoraton_crypto.hpp"

namespace aura {

enum class AoratonSelfTestResult {
    Passed,
    RandomFailed,
    Sha256Failed,
    Pbkdf2Failed,
    VaultOpenFailed,
    MessageFailed,
    VerifierFailed,
};

// Known answers for a platform's crypto adapters: SHA-256 ("abc"), PBKDF2-
// HMAC-SHA256 ("password"/"salt", 1 and 4096 rounds), the vault's AES-GCM
// opening (and a refused tamper), the clean-room E2EE v2 vector and its room-
// key verifier, and a random source that does not repeat itself. The device
// runs it before its account worker starts; the PC tests run it against the
// Windows adapters, which keeps the vectors honest. It reports a code only.
AoratonSelfTestResult aoraton_crypto_self_test(IAoratonAccountCrypto& account, IAoratonCryptoV2& message);
const char* aoraton_self_test_name(AoratonSelfTestResult result) noexcept;

}  // namespace aura

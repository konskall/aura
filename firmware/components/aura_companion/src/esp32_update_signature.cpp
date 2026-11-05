#include "esp32_update_signature.hpp"

#include <cstring>

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"

namespace aura::firmware {
namespace {

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

}  // namespace

Esp32UpdateSignatureVerifier::Esp32UpdateSignatureVerifier(const char* public_key_hex) {
    if (!public_key_hex || std::strlen(public_key_hex) != point_.size() * 2) return;
    for (std::size_t index = 0; index < point_.size(); ++index) {
        const int high = hex_value(public_key_hex[index * 2]);
        const int low = hex_value(public_key_hex[index * 2 + 1]);
        if (high < 0 || low < 0) return;
        point_[index] = static_cast<std::uint8_t>(high * 16 + low);
    }
    configured_ = point_[0] == 0x04;
}

bool Esp32UpdateSignatureVerifier::verify(const std::string& message,
                                          const std::array<std::uint8_t, 64>& signature) const {
    if (!configured_) return false;
    unsigned char digest[32];
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(message.data()), message.size(), digest, 0) != 0) {
        return false;
    }
    mbedtls_ecp_group group;
    mbedtls_ecp_point key;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&key);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    // The manifest carries the signature as r || s, 32 bytes each.
    const bool valid = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                       mbedtls_ecp_point_read_binary(&group, &key, point_.data(), point_.size()) == 0 &&
                       mbedtls_ecp_check_pubkey(&group, &key) == 0 &&
                       mbedtls_mpi_read_binary(&r, signature.data(), 32) == 0 &&
                       mbedtls_mpi_read_binary(&s, signature.data() + 32, 32) == 0 &&
                       mbedtls_ecdsa_verify(&group, digest, sizeof(digest), &key, &r, &s) == 0;
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&key);
    mbedtls_ecp_group_free(&group);
    return valid;
}

}  // namespace aura::firmware

// The simulator's check of the owner's update signature (Windows BCrypt), on a
// fixed P-256 vector made with Node's crypto; the throwaway private key that
// signed it was never stored.
#include "aura/ota_update.hpp"
#include "aura/windows_update_signature.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::array<std::uint8_t, 64> signature_from_hex(const std::string& hex) {
    std::array<std::uint8_t, 64> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(std::stoi(hex.substr(index * 2, 2), nullptr, 16));
    }
    return bytes;
}

constexpr const char* kKey =
    "04d8ca13d58db69528178295cfd64023343e36ed57971de9f5a02b19969590724152cf9e5e8cc211dc07168f1a96c23b32cd6d3afe7a2aecc87b07015ec370dfbe";
constexpr const char* kOtherKey =
    "0488b9a4ec2c860fe45b7cbf2645f3472a3af4851db4d6907526eac2b58e5ea2b0f28e2329807dd15c460f643e0be9c1918a2bddbd3546e26ac5f5077e57781dae";
constexpr const char* kSignature =
    "ed35b6c80bd33d25180eaf184c4ec673c6771742c6ab4a45b81d9e3d61101a522f7a0797744a01502ea77cde3052351788651385dbecdab0b10348192d132b55";

// With a manifest file and a public key, prints what the device would decide
// (OK or the parser's error code) for tests/interop/run_update_signing_test.mjs.
int check_manifest_file(const char* path, const char* public_key_hex) {
    std::ifstream file(path, std::ios::binary);
    const std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const aura::simulator::WindowsUpdateSignatureVerifier verifier(public_key_hex);
    std::string error;
    const auto manifest = aura::parse_update_manifest(body, verifier, &error);
    std::cout << (manifest ? "OK" : error) << '\n';
    return manifest ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3) return check_manifest_file(argv[1], argv[2]);
    aura::UpdateManifest manifest;
    manifest.version = "13.1.0";
    manifest.url = "https://updates.example/aura.bin";
    manifest.size_bytes = 1234567;
    manifest.sha256 = std::string(64, 'a');
    manifest.signature = signature_from_hex(kSignature);
    const std::string message = aura::update_signing_message(manifest);
    expect(message == "AURA-OTA-1\n13.1.0\n1234567\n" + std::string(64, 'a') + "\n",
           "the owner signs the version, the size and the SHA-256");

    const aura::simulator::WindowsUpdateSignatureVerifier verifier(kKey);
    expect(verifier.configured(), "the build's public key is read");
    expect(verifier.verify(message, manifest.signature), "the owner's signature verifies");
    auto newer = manifest;
    newer.version = "13.2.0";
    expect(!verifier.verify(aura::update_signing_message(newer), manifest.signature),
           "a signature for one version does not cover another");
    auto damaged = manifest.signature;
    damaged[5] ^= 0x01;
    expect(!verifier.verify(message, damaged), "a damaged signature is refused");
    const aura::simulator::WindowsUpdateSignatureVerifier other(kOtherKey);
    expect(other.configured() && !other.verify(message, manifest.signature), "another key's signature is refused");
    const aura::simulator::WindowsUpdateSignatureVerifier none("");
    expect(!none.configured() && !none.verify(message, manifest.signature), "a build without a key refuses everything");
    const aura::simulator::WindowsUpdateSignatureVerifier malformed("04zz");
    expect(!malformed.configured(), "a malformed key is no key");
    std::cout << "update_signature_test: PASS\n";
    return 0;
}

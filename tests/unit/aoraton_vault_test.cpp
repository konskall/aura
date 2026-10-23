#include "aura/windows_aoraton_vault.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<unsigned char> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool contains(const std::vector<unsigned char>& haystack, const std::string& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

}  // namespace

int main() {
    using namespace aura::simulator;

    std::wstring temporary(32768, L'\0');
    const DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
    expect(length > 0 && length < temporary.size(), "Windows temp path is available");
    temporary.resize(length);
    const std::filesystem::path directory = std::filesystem::path(temporary) /
        (L"AURA-vault-test-" + std::to_wstring(GetCurrentProcessId()));
    expect(directory.filename().wstring().find(L"AURA-vault-test-") == 0, "test cleanup target is narrowly scoped");
    std::error_code cleanup_error;
    std::filesystem::remove_all(directory, cleanup_error);
    expect(!cleanup_error, "stale test directory cleanup succeeds");

    const std::filesystem::path path = directory / L"vault.dpapi";
    WindowsAoratonVault vault(path);
    AoratonVaultSecrets secrets;
    secrets.access_token = "synthetic-access-token-DO-NOT-LOG";
    secrets.refresh_token = "synthetic-refresh-token-ROTATE";
    AoratonVaultRoomKey room;
    room.room_id = "v2.synthetic-test-room";
    for (std::size_t index = 0; index < room.key.size(); ++index) room.key[index] = static_cast<std::uint8_t>(index + 1);
    secrets.room_keys.push_back(std::move(room));

    expect(vault.store(secrets), "DPAPI vault stores synthetic secrets atomically");
    expect(std::filesystem::exists(path), "vault file exists");
    const auto encrypted = read_all(path);
    expect(!contains(encrypted, secrets.access_token), "access token is not plaintext on disk");
    expect(!contains(encrypted, secrets.refresh_token), "refresh token is not plaintext on disk");
    expect(!contains(encrypted, secrets.room_keys.front().room_id), "room identity is inside protected blob");

    AoratonVaultSecrets loaded;
    expect(vault.load(loaded), "same Windows user decrypts DPAPI vault");
    expect(loaded.access_token == secrets.access_token && loaded.refresh_token == secrets.refresh_token,
           "session material round-trips exactly");
    expect(loaded.room_keys.size() == 1 && loaded.room_keys.front().room_id == secrets.room_keys.front().room_id,
           "room record round-trips");
    expect(loaded.room_keys.front().key == secrets.room_keys.front().key, "256-bit room key round-trips");

    secrets.access_token = "rotated-access-token";
    secrets.refresh_token = "rotated-refresh-token";
    expect(vault.store(secrets), "token rotation replaces vault atomically");
    loaded.clear();
    expect(vault.load(loaded) && loaded.access_token == "rotated-access-token",
           "latest rotated token wins");

    auto corrupted = read_all(path);
    expect(corrupted.size() > 20, "vault has authenticated ciphertext");
    // Corrupt the authenticated container's declared protected-blob length.
    // This deterministic structural negative is independent of opaque DPAPI
    // implementation fields that Windows may legitimately ignore.
    corrupted[8] ^= 0x01;
    {
        HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        expect(output != INVALID_HANDLE_VALUE, "test opens hidden vault for controlled corruption");
        DWORD written = 0;
        expect(WriteFile(output, corrupted.data(), static_cast<DWORD>(corrupted.size()), &written, nullptr) &&
                   written == corrupted.size(),
               "test writes controlled corruption");
        expect(SetEndOfFile(output), "test preserves exact corrupted file length");
        CloseHandle(output);
    }
    loaded.clear();
    expect(!vault.load(loaded), "corrupted vault container is rejected");
    expect(loaded.access_token.empty() && loaded.room_keys.empty(), "failed load returns no partial secrets");

    expect(vault.erase(), "unpair erases the local vault");
    expect(!std::filesystem::exists(path), "vault file is gone after erase");
    std::filesystem::remove_all(directory, cleanup_error);
    expect(!cleanup_error, "narrow test directory cleanup succeeds");

    std::cout << "aoraton_vault_test: PASS\n";
    return 0;
}

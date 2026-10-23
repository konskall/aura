#include "aura/windows_aoraton_vault.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dpapi.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace aura::simulator {
namespace {

constexpr std::size_t kMaxTokenBytes = 16 * 1024;
constexpr std::size_t kMaxRoomIdBytes = 128;
constexpr std::size_t kMaxRooms = 64;
constexpr std::size_t kMaxVaultBytes = 128 * 1024;
constexpr char kPlainMagic[] = "ARV1";
constexpr char kOuterMagic[] = "AURADP11";
constexpr char kEntropy[] = "AURA-Aoraton-v11-current-user-vault";

void clear_memory(void* pointer, std::size_t size) noexcept {
    if (pointer && size) SecureZeroMemory(pointer, size);
}

void clear_string(std::string& value) noexcept {
    if (!value.empty()) clear_memory(&value[0], value.size());
    value.clear();
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 24));
}

bool take_u32(const std::vector<std::uint8_t>& input, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > input.size()) return false;
    value = static_cast<std::uint32_t>(input[offset]) |
            (static_cast<std::uint32_t>(input[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(input[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(input[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool append_string(std::vector<std::uint8_t>& output, const std::string& value, std::size_t maximum) {
    if (value.size() > maximum || value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    append_u32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

bool take_string(const std::vector<std::uint8_t>& input,
                 std::size_t& offset,
                 std::size_t maximum,
                 std::string& value) {
    std::uint32_t size = 0;
    if (!take_u32(input, offset, size) || size > maximum || offset + size > input.size()) return false;
    value.assign(reinterpret_cast<const char*>(input.data() + offset), size);
    offset += size;
    return true;
}

bool serialize(const AoratonVaultSecrets& secrets, std::vector<std::uint8_t>& output) {
    if (secrets.room_keys.size() > kMaxRooms) return false;
    output.clear();
    // Reserve the exact size up front: vector growth would free intermediate
    // buffers holding tokens/keys without zeroizing them.
    std::size_t expected = sizeof(kPlainMagic) - 1 + 4 + secrets.access_token.size() +
                           4 + secrets.refresh_token.size() + 4;
    for (const auto& room : secrets.room_keys) expected += 4 + room.room_id.size() + room.key.size();
    output.reserve(expected);
    output.insert(output.end(), kPlainMagic, kPlainMagic + sizeof(kPlainMagic) - 1);
    if (!append_string(output, secrets.access_token, kMaxTokenBytes) ||
        !append_string(output, secrets.refresh_token, kMaxTokenBytes)) return false;
    append_u32(output, static_cast<std::uint32_t>(secrets.room_keys.size()));
    for (const auto& room : secrets.room_keys) {
        if (room.room_id.empty() || !append_string(output, room.room_id, kMaxRoomIdBytes)) return false;
        output.insert(output.end(), room.key.begin(), room.key.end());
    }
    return output.size() <= kMaxVaultBytes;
}

bool deserialize(const std::vector<std::uint8_t>& input, AoratonVaultSecrets& secrets) {
    if (input.size() < sizeof(kPlainMagic) - 1 || input.size() > kMaxVaultBytes ||
        std::memcmp(input.data(), kPlainMagic, sizeof(kPlainMagic) - 1) != 0) return false;
    AoratonVaultSecrets parsed;
    std::size_t offset = sizeof(kPlainMagic) - 1;
    if (!take_string(input, offset, kMaxTokenBytes, parsed.access_token) ||
        !take_string(input, offset, kMaxTokenBytes, parsed.refresh_token)) return false;
    std::uint32_t count = 0;
    if (!take_u32(input, offset, count) || count > kMaxRooms) return false;
    parsed.room_keys.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        AoratonVaultRoomKey room;
        if (!take_string(input, offset, kMaxRoomIdBytes, room.room_id) || room.room_id.empty() ||
            offset + room.key.size() > input.size()) return false;
        std::copy_n(input.data() + offset, room.key.size(), room.key.begin());
        offset += room.key.size();
        parsed.room_keys.push_back(std::move(room));
    }
    if (offset != input.size()) return false;
    secrets = std::move(parsed);
    return true;
}

DATA_BLOB entropy_blob() {
    DATA_BLOB blob{};
    blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
    blob.cbData = static_cast<DWORD>(sizeof(kEntropy) - 1);
    return blob;
}

bool write_atomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code error;
    if (!path.has_parent_path() || !std::filesystem::create_directories(path.parent_path(), error) && error) return false;
    const std::filesystem::path temporary = path.wstring() + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool wrote = bytes.size() <= std::numeric_limits<DWORD>::max() &&
                       WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                       written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!wrote || !MoveFileExW(temporary.c_str(), path.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

}  // namespace

AoratonVaultRoomKey::~AoratonVaultRoomKey() { clear(); }

AoratonVaultRoomKey::AoratonVaultRoomKey(AoratonVaultRoomKey&& other) noexcept
    : room_id(std::move(other.room_id)), key(other.key) {
    other.clear();
}

AoratonVaultRoomKey& AoratonVaultRoomKey::operator=(AoratonVaultRoomKey&& other) noexcept {
    if (this == &other) return *this;
    clear();
    room_id = std::move(other.room_id);
    key = other.key;
    other.clear();
    return *this;
}

void AoratonVaultRoomKey::clear() noexcept {
    clear_memory(key.data(), key.size());
    room_id.clear();
}

AoratonVaultSecrets::~AoratonVaultSecrets() { clear(); }

AoratonVaultSecrets::AoratonVaultSecrets(AoratonVaultSecrets&& other) noexcept
    : access_token(std::move(other.access_token)),
      refresh_token(std::move(other.refresh_token)),
      room_keys(std::move(other.room_keys)) {
    other.clear();
}

AoratonVaultSecrets& AoratonVaultSecrets::operator=(AoratonVaultSecrets&& other) noexcept {
    if (this == &other) return *this;
    clear();
    access_token = std::move(other.access_token);
    refresh_token = std::move(other.refresh_token);
    room_keys = std::move(other.room_keys);
    other.clear();
    return *this;
}

void AoratonVaultSecrets::clear() noexcept {
    clear_string(access_token);
    clear_string(refresh_token);
    room_keys.clear();
}

WindowsAoratonVault::WindowsAoratonVault(std::filesystem::path path) : path_(std::move(path)) {}

std::filesystem::path WindowsAoratonVault::default_path() {
    std::wstring value(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) return {};
    value.resize(length);
    return std::filesystem::path(value) / L"AURA" / L"aoraton-v11.dpapi";
}

bool WindowsAoratonVault::store(const AoratonVaultSecrets& secrets) const {
    if (path_.empty()) return false;
    std::vector<std::uint8_t> plain;
    if (!serialize(secrets, plain)) return false;
    DATA_BLOB input{static_cast<DWORD>(plain.size()), plain.data()};
    DATA_BLOB entropy = entropy_blob();
    DATA_BLOB encrypted{};
    const BOOL protected_ok = CryptProtectData(&input, L"AURA Aoraton v11", &entropy, nullptr, nullptr,
                                               CRYPTPROTECT_UI_FORBIDDEN, &encrypted);
    clear_memory(plain.data(), plain.size());
    if (!protected_ok || !encrypted.pbData || encrypted.cbData == 0 || encrypted.cbData > kMaxVaultBytes) {
        if (encrypted.pbData) LocalFree(encrypted.pbData);
        return false;
    }
    std::vector<std::uint8_t> outer;
    outer.reserve(sizeof(kOuterMagic) - 1 + 4 + encrypted.cbData);
    outer.insert(outer.end(), kOuterMagic, kOuterMagic + sizeof(kOuterMagic) - 1);
    append_u32(outer, encrypted.cbData);
    outer.insert(outer.end(), encrypted.pbData, encrypted.pbData + encrypted.cbData);
    clear_memory(encrypted.pbData, encrypted.cbData);
    LocalFree(encrypted.pbData);
    const bool ok = write_atomic(path_, outer);
    clear_memory(outer.data(), outer.size());
    return ok;
}

bool WindowsAoratonVault::load(AoratonVaultSecrets& secrets) const {
    secrets.clear();
    if (path_.empty()) return false;
    std::ifstream file(path_, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff size = file.tellg();
    if (size <= static_cast<std::streamoff>(sizeof(kOuterMagic) + 4) ||
        size > static_cast<std::streamoff>(kMaxVaultBytes)) return false;
    file.seekg(0);
    std::vector<std::uint8_t> outer(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(outer.data()), size)) return false;
    if (std::memcmp(outer.data(), kOuterMagic, sizeof(kOuterMagic) - 1) != 0) return false;
    std::size_t offset = sizeof(kOuterMagic) - 1;
    std::uint32_t encrypted_size = 0;
    if (!take_u32(outer, offset, encrypted_size) || encrypted_size == 0 ||
        offset + encrypted_size != outer.size()) return false;
    DATA_BLOB input{encrypted_size, outer.data() + offset};
    DATA_BLOB entropy = entropy_blob();
    DATA_BLOB plain_blob{};
    LPWSTR description = nullptr;
    const BOOL unprotected = CryptUnprotectData(&input, &description, &entropy, nullptr, nullptr,
                                                CRYPTPROTECT_UI_FORBIDDEN, &plain_blob);
    if (description) LocalFree(description);
    clear_memory(outer.data(), outer.size());
    if (!unprotected || !plain_blob.pbData || plain_blob.cbData == 0 || plain_blob.cbData > kMaxVaultBytes) {
        if (plain_blob.pbData) LocalFree(plain_blob.pbData);
        return false;
    }
    std::vector<std::uint8_t> plain(plain_blob.pbData, plain_blob.pbData + plain_blob.cbData);
    clear_memory(plain_blob.pbData, plain_blob.cbData);
    LocalFree(plain_blob.pbData);
    const bool ok = deserialize(plain, secrets);
    clear_memory(plain.data(), plain.size());
    if (!ok) secrets.clear();
    return ok;
}

bool WindowsAoratonVault::erase() const {
    if (path_.empty()) return false;
    if (DeleteFileW(path_.c_str())) return true;
    return GetLastError() == ERROR_FILE_NOT_FOUND;
}

}  // namespace aura::simulator

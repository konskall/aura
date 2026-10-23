#pragma once

#include "aura/aoraton_crypto.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aura::simulator {

struct AoratonVaultRoomKey {
    AoratonVaultRoomKey() = default;
    ~AoratonVaultRoomKey();
    AoratonVaultRoomKey(const AoratonVaultRoomKey&) = delete;
    AoratonVaultRoomKey& operator=(const AoratonVaultRoomKey&) = delete;
    AoratonVaultRoomKey(AoratonVaultRoomKey&& other) noexcept;
    AoratonVaultRoomKey& operator=(AoratonVaultRoomKey&& other) noexcept;

    std::string room_id;
    std::array<std::uint8_t, kAoratonRoomKeyBytes> key{};

    void clear() noexcept;
};

struct AoratonVaultSecrets {
    AoratonVaultSecrets() = default;
    ~AoratonVaultSecrets();
    AoratonVaultSecrets(const AoratonVaultSecrets&) = delete;
    AoratonVaultSecrets& operator=(const AoratonVaultSecrets&) = delete;
    AoratonVaultSecrets(AoratonVaultSecrets&& other) noexcept;
    AoratonVaultSecrets& operator=(AoratonVaultSecrets&& other) noexcept;

    std::string access_token;
    std::string refresh_token;
    std::vector<AoratonVaultRoomKey> room_keys;

    void clear() noexcept;
};

class WindowsAoratonVault final {
public:
    explicit WindowsAoratonVault(std::filesystem::path path);

    static std::filesystem::path default_path();
    const std::filesystem::path& path() const noexcept { return path_; }
    bool store(const AoratonVaultSecrets& secrets) const;
    bool load(AoratonVaultSecrets& secrets) const;
    bool erase() const;

private:
    std::filesystem::path path_;
};

}  // namespace aura::simulator

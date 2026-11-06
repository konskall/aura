#pragma once

#include "aura/aoraton_attachment_cache.hpp"

#include <filesystem>

namespace aura::simulator {

// The simulator's attachment cache folder, beside the account's vault
// (%LOCALAPPDATA%\AURA\aoraton-cache): `<SHA-256>.enc` files, each written to a
// temporary file first and renamed into place.
class WindowsAoratonCacheFiles final : public IAoratonCacheFiles {
public:
    explicit WindowsAoratonCacheFiles(std::filesystem::path folder);

    bool list(std::vector<AoratonCacheEntry>& entries) override;
    bool read(const std::string& name, std::size_t max_bytes, std::string& data) override;
    bool write(const std::string& name, const std::string& data) override;
    void remove(const std::string& name) override;

private:
    std::filesystem::path file(const std::string& name) const;

    std::filesystem::path folder_;
};

}  // namespace aura::simulator

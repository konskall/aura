#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {

// Encrypted Aoraton attachments kept as they arrived: the AES-GCM bytes the
// public `attachments` bucket serves to anyone. A photo or voice message opened
// again, or whose thumbnail is needed again, is then read from the device's
// microSD card instead of downloaded. No key, thumbnail or decrypted byte is
// ever written; each file is named by the SHA-256 of its address.
class IAoratonAttachmentCache {
public:
    virtual ~IAoratonAttachmentCache() = default;
    // The file's sealed bytes when kept and at most `max_bytes`.
    virtual bool load(const std::string& name, std::size_t max_bytes, std::string& sealed) = 0;
    virtual void store(const std::string& name, const std::string& sealed) = 0;
    // A copy that no longer opens.
    virtual void erase(const std::string& name) = 0;
    // Signing out.
    virtual void clear() = 0;
};

struct AoratonCacheEntry {
    std::string name;
    std::uint64_t size{0};
    std::int64_t modified{0};  // seconds since the epoch: after a restart the oldest go first
};

// The platform's folder of `<name>.enc` files: `/sdcard/aura/.cache/aoraton` on
// the device, `%LOCALAPPDATA%\AURA\aoraton-cache` in the simulator.
class IAoratonCacheFiles {
public:
    virtual ~IAoratonCacheFiles() = default;
    // false when there is nowhere to keep files (no card): the cache is then off.
    virtual bool list(std::vector<AoratonCacheEntry>& entries) = 0;
    virtual bool read(const std::string& name, std::size_t max_bytes, std::string& data) = 0;
    // Whole or not at all.
    virtual bool write(const std::string& name, const std::string& data) = 0;
    virtual void remove(const std::string& name) = 0;
};

// A SHA-256 in lower-case hex: the only names the cache reads or writes.
bool aoraton_cache_name_valid(const std::string& name);

// The cache's rules over the platform's files: at most `max_bytes` in all, the
// least recently used file out first. Used from the account worker's thread.
class AoratonAttachmentCache final : public IAoratonAttachmentCache {
public:
    static constexpr std::uint64_t kDefaultMaxBytes = 128ULL * 1024 * 1024;

    explicit AoratonAttachmentCache(IAoratonCacheFiles& files, std::uint64_t max_bytes = kDefaultMaxBytes);

    bool load(const std::string& name, std::size_t max_bytes, std::string& sealed) override;
    void store(const std::string& name, const std::string& sealed) override;
    void erase(const std::string& name) override;
    void clear() override;
    std::uint64_t total_bytes() const noexcept { return total_; }

private:
    bool ready();
    void forget(std::size_t index);
    void trim();

    IAoratonCacheFiles& files_;
    std::uint64_t max_bytes_;
    bool listed_{false};
    bool available_{false};
    std::vector<AoratonCacheEntry> entries_;  // least recently used first
    std::uint64_t total_{0};
};

}  // namespace aura

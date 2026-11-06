#include "aura/aoraton_attachment_cache.hpp"

#include <algorithm>
#include <utility>

namespace aura {

bool aoraton_cache_name_valid(const std::string& name) {
    return name.size() == 64 && name.find_first_not_of("0123456789abcdef") == std::string::npos;
}

AoratonAttachmentCache::AoratonAttachmentCache(IAoratonCacheFiles& files, std::uint64_t max_bytes)
    : files_(files), max_bytes_(max_bytes) {}

// The folder is listed once, at first use: the card is mounted before the
// account starts, and a card inserted later is used after a restart.
bool AoratonAttachmentCache::ready() {
    if (listed_) return available_;
    listed_ = true;
    std::vector<AoratonCacheEntry> found;
    available_ = files_.list(found);
    if (!available_) return false;
    for (auto& entry : found) {
        if (!aoraton_cache_name_valid(entry.name)) continue;
        total_ += entry.size;
        entries_.push_back(std::move(entry));
    }
    std::stable_sort(entries_.begin(), entries_.end(), [](const AoratonCacheEntry& left, const AoratonCacheEntry& right) {
        return left.modified < right.modified;
    });
    trim();
    return true;
}

void AoratonAttachmentCache::forget(std::size_t index) {
    total_ -= entries_[index].size;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
}

void AoratonAttachmentCache::trim() {
    while (total_ > max_bytes_ && !entries_.empty()) {
        files_.remove(entries_.front().name);
        forget(0);
    }
}

bool AoratonAttachmentCache::load(const std::string& name, std::size_t max_bytes, std::string& sealed) {
    sealed.clear();
    if (!aoraton_cache_name_valid(name) || !ready()) return false;
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [&](const AoratonCacheEntry& entry) { return entry.name == name; });
    if (found == entries_.end() || found->size > max_bytes) return false;
    const auto index = static_cast<std::size_t>(found - entries_.begin());
    if (!files_.read(name, max_bytes, sealed)) {
        sealed.clear();
        files_.remove(name);
        forget(index);
        return false;
    }
    // Opening a file counts as using it.
    auto entry = std::move(entries_[index]);
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    entries_.push_back(std::move(entry));
    return true;
}

void AoratonAttachmentCache::store(const std::string& name, const std::string& sealed) {
    if (!aoraton_cache_name_valid(name) || sealed.empty() || sealed.size() > max_bytes_ || !ready()) return;
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [&](const AoratonCacheEntry& entry) { return entry.name == name; });
    if (found != entries_.end()) forget(static_cast<std::size_t>(found - entries_.begin()));
    if (!files_.write(name, sealed)) {
        files_.remove(name);
        return;
    }
    entries_.push_back({name, sealed.size(), 0});
    total_ += sealed.size();
    trim();
}

void AoratonAttachmentCache::erase(const std::string& name) {
    if (!aoraton_cache_name_valid(name) || !ready()) return;
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [&](const AoratonCacheEntry& entry) { return entry.name == name; });
    files_.remove(name);
    if (found != entries_.end()) forget(static_cast<std::size_t>(found - entries_.begin()));
}

void AoratonAttachmentCache::clear() {
    if (!ready()) return;
    for (const auto& entry : entries_) files_.remove(entry.name);
    entries_.clear();
    total_ = 0;
}

}  // namespace aura

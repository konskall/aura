#include "esp32_file_storage.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace aura::firmware {
namespace {

constexpr const char* kCard = "/sdcard";
// A folder of tens of thousands of files is not read to its end.
constexpr std::size_t kMaxInspected = 2048;

std::string card_path(const std::string& path) { return path.empty() ? std::string(kCard) : std::string(kCard) + "/" + path; }

std::int64_t local_minute(std::time_t when) {
    std::tm local{};
    if (when <= 0 || !localtime_r(&when, &local) || local.tm_year < 100) return 0;
    LocalDateTime time;
    time.year = local.tm_year + 1900;
    time.month = local.tm_mon + 1;
    time.day = local.tm_mday;
    time.hour = local.tm_hour;
    time.minute = local.tm_min;
    return minute_index(time);
}

// Names first, then the deletions: removing while reading moves a FAT folder's cursor.
bool erase_tree(const std::string& path, std::size_t depth, std::size_t& count) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) return false;
    if (++count > kMaxFileEraseEntries) return false;
    if (!S_ISDIR(info.st_mode)) return std::remove(path.c_str()) == 0;
    if (depth >= kMaxFileEraseDepth) return false;
    DIR* folder = opendir(path.c_str());
    if (!folder) return false;
    std::vector<std::string> names;
    while (const auto* entry = readdir(folder)) {
        const std::string name(entry->d_name);
        if (name != "." && name != "..") names.push_back(name);
        if (names.size() > kMaxFileEraseEntries) break;
    }
    closedir(folder);
    for (const auto& name : names) {
        if (!erase_tree(path + "/" + name, depth + 1, count)) return false;
    }
    return rmdir(path.c_str()) == 0;
}

}  // namespace

FileBrowserError Esp32FileStorage::list(const std::string& path, std::vector<FileEntry>& entries, bool& truncated) {
    if (!store_.mounted()) return FileBrowserError::NoCard;
    const auto folder = card_path(path);
    DIR* directory = opendir(folder.c_str());
    if (!directory) return errno == ENOENT ? FileBrowserError::NotFound : FileBrowserError::Read;
    std::size_t inspected = 0;
    while (const auto* entry = readdir(directory)) {
        if (++inspected > kMaxInspected) {
            truncated = true;
            break;
        }
        const std::string name(entry->d_name);
        if (name == "." || name == ".." || !file_name_valid(name)) continue;
        struct stat info {};
        if (stat((folder + "/" + name).c_str(), &info) != 0) continue;
        FileEntry item;
        item.name = name;
        item.folder = S_ISDIR(info.st_mode);
        item.bytes = item.folder ? 0U : static_cast<std::uint64_t>(info.st_size);
        item.modified_minute = local_minute(info.st_mtime);
        file_list_add(entries, std::move(item), truncated);
    }
    closedir(directory);
    return FileBrowserError::None;
}

FileBrowserError Esp32FileStorage::erase(const std::string& path) {
    if (!store_.mounted()) return FileBrowserError::NoCard;
    if (path.empty()) return FileBrowserError::Protected;
    std::size_t count = 0;
    return erase_tree(card_path(path), 0, count) ? FileBrowserError::None : FileBrowserError::Delete;
}

FileBrowserError Esp32FileStorage::rename(const std::string& path, const std::string& new_name) {
    if (!store_.mounted()) return FileBrowserError::NoCard;
    if (path.empty()) return FileBrowserError::Protected;
    const std::string from = card_path(path);
    const std::string to = card_path(file_path_join(file_path_parent(path), new_name));
    struct stat existing {};
    if (stat(from.c_str(), &existing) != 0) return FileBrowserError::NotFound;
    if (stat(to.c_str(), &existing) == 0) return FileBrowserError::Rename;  // never over another
    return std::rename(from.c_str(), to.c_str()) == 0 ? FileBrowserError::None : FileBrowserError::Rename;
}

FileBrowserError Esp32FileStorage::read(const std::string& path, std::size_t max_bytes,
                                        std::vector<std::uint8_t>& bytes) {
    if (!store_.mounted()) return FileBrowserError::NoCard;
    std::FILE* file = std::fopen(card_path(path).c_str(), "rb");
    if (!file) return errno == ENOENT ? FileBrowserError::NotFound : FileBrowserError::Read;
    bytes.clear();
    std::uint8_t chunk[2048];
    while (bytes.size() < max_bytes) {
        const std::size_t want = std::min(sizeof(chunk), max_bytes - bytes.size());
        const std::size_t got = std::fread(chunk, 1, want, file);
        bytes.insert(bytes.end(), chunk, chunk + got);
        if (got < want) break;
    }
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    return failed ? FileBrowserError::Read : FileBrowserError::None;
}

bool Esp32FileStorage::space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) {
    return store_.space(total_bytes, free_bytes);
}

}  // namespace aura::firmware

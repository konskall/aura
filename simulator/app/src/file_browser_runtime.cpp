#include "aura/file_browser_runtime.hpp"

#include "aura/text_util.hpp"

#include <fstream>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <system_error>

namespace aura::simulator {
namespace {

constexpr std::string_view kCardFolder{"aura"};

bool redirected(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

// The local minute a file was last written; 0 when Windows cannot say.
std::int64_t modified_minute(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    FILETIME local{};
    SYSTEMTIME when{};
    if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &when)) return 0;
    LocalDateTime time;
    time.year = when.wYear;
    time.month = when.wMonth;
    time.day = when.wDay;
    time.hour = when.wHour;
    time.minute = when.wMinute;
    return minute_index(time);
}

}  // namespace

bool DesktopFileStorage::resolve(const std::string& path, std::filesystem::path& out) const {
    const auto slash = path.find('/');
    if (std::string_view(path).substr(0, slash) != kCardFolder) return false;
    out = root_;
    if (slash != std::string::npos) out /= std::filesystem::u8path(path.substr(slash + 1));
    return true;
}

FileBrowserError DesktopFileStorage::list(const std::string& path, std::vector<FileEntry>& entries, bool& truncated) {
    if (path.empty()) {
        // The card's root holds the device's folder.
        file_list_add(entries, {std::string(kCardFolder), true, 0, 0}, truncated);
        return FileBrowserError::None;
    }
    std::filesystem::path folder;
    if (!resolve(path, folder)) return FileBrowserError::NotFound;
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error) || error || redirected(folder)) return FileBrowserError::NotFound;
    std::size_t inspected = 0;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end && inspected < 4096;
         it.increment(error), ++inspected) {
        const auto name = it->path().filename().u8string();
        if (!file_name_valid(name) || redirected(it->path())) continue;
        std::error_code item_error;
        const bool is_folder = it->is_directory(item_error);
        if (item_error) continue;
        const auto bytes = is_folder ? 0 : it->file_size(item_error);
        file_list_add(entries, {name, is_folder, item_error ? 0 : bytes, modified_minute(it->path())}, truncated);
    }
    return error ? FileBrowserError::Read : FileBrowserError::None;
}

FileBrowserError DesktopFileStorage::erase(const std::string& path) {
    std::filesystem::path target;
    if (!resolve(path, target) || redirected(target)) return FileBrowserError::NotFound;
    std::error_code error;
    if (std::filesystem::is_directory(target, error)) {
        std::size_t count = 0;
        for (std::filesystem::recursive_directory_iterator it(target, error), end; !error && it != end;
             it.increment(error)) {
            if (redirected(it->path()) || ++count > kMaxFileEraseEntries) return FileBrowserError::Delete;
        }
        if (error) return FileBrowserError::Delete;
    }
    std::filesystem::remove_all(target, error);
    return error ? FileBrowserError::Delete : FileBrowserError::None;
}

FileBrowserError DesktopFileStorage::rename(const std::string& path, const std::string& new_name) {
    std::filesystem::path from;
    if (!resolve(path, from) || redirected(from) || path == kCardFolder) return FileBrowserError::NotFound;
    const auto to = from.parent_path() / std::filesystem::u8path(new_name);
    std::error_code error;
    if (!std::filesystem::exists(from, error) || error) return FileBrowserError::NotFound;
    if (std::filesystem::exists(to, error) || error) return FileBrowserError::Rename;  // never over another
    std::filesystem::rename(from, to, error);
    return error ? FileBrowserError::Rename : FileBrowserError::None;
}

FileBrowserError DesktopFileStorage::read(const std::string& path, std::size_t max_bytes,
                                          std::vector<std::uint8_t>& bytes) {
    std::filesystem::path target;
    if (!resolve(path, target) || redirected(target)) return FileBrowserError::NotFound;
    std::ifstream input(target, std::ios::binary);
    if (!input) return FileBrowserError::NotFound;
    bytes.assign(max_bytes, 0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(max_bytes));
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    return input.bad() ? FileBrowserError::Read : FileBrowserError::None;
}

bool DesktopFileStorage::space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) {
    std::error_code error;
    const auto info = std::filesystem::space(root_, error);
    if (error) return false;
    total_bytes = info.capacity;
    free_bytes = info.available;
    return true;
}

DemoFileStorage::DemoFileStorage() {
    const std::int64_t minute = days_from_civil(2026, 11, 2) * 1440 + 9 * 60;
    folders_[""] = {{"aura", true, 0, 0}, {"DEMO Μουσική", true, 0, 0}, {"DEMO.txt", false, 2048, minute}};
    folders_["aura"] = {{"photos", true, 0, 0}, {"files", true, 0, 0}, {"clock.txt", false, 512, minute}};
    folders_["aura/photos"] = {{"DEMO ήλιος.jpg", false, 524288, minute}};
    folders_["aura/files"] = {};
    folders_["DEMO Μουσική"] = {{"DEMO ρυθμός.mp3", false, 3145728, minute}};
    const std::string words = "DEMO\nΣημειώσεις της κάρτας.\nΔεύτερη γραμμή.\n";
    contents_["DEMO.txt"] = std::vector<std::uint8_t>(words.begin(), words.end());
}

FileBrowserError DemoFileStorage::list(const std::string& path, std::vector<FileEntry>& entries, bool& truncated) {
    const auto found = folders_.find(path);
    if (found == folders_.end()) return FileBrowserError::NotFound;
    for (auto entry : found->second) file_list_add(entries, std::move(entry), truncated);
    return FileBrowserError::None;
}

FileBrowserError DemoFileStorage::erase(const std::string& path) {
    auto parent = folders_.find(file_path_parent(path));
    if (parent == folders_.end()) return FileBrowserError::NotFound;
    const auto leaf = file_path_leaf(path);
    auto& entries = parent->second;
    const auto before = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const FileEntry& entry) { return entry.name == leaf; }),
                  entries.end());
    if (entries.size() == before) return FileBrowserError::NotFound;
    folders_.erase(path);
    return FileBrowserError::None;
}

FileBrowserError DemoFileStorage::rename(const std::string& path, const std::string& new_name) {
    auto parent = folders_.find(file_path_parent(path));
    if (parent == folders_.end()) return FileBrowserError::NotFound;
    const auto leaf = file_path_leaf(path);
    auto& entries = parent->second;
    for (const auto& entry : entries) {
        if (entry.name == new_name) return FileBrowserError::Rename;
    }
    for (auto& entry : entries) {
        if (entry.name != leaf) continue;
        entry.name = new_name;
        const auto renamed = file_path_join(file_path_parent(path), new_name);
        if (auto folder = folders_.find(path); folder != folders_.end()) {
            auto moved = std::move(folder->second);
            folders_.erase(folder);
            folders_[renamed] = std::move(moved);
        }
        if (auto content = contents_.find(path); content != contents_.end()) {
            auto moved = std::move(content->second);
            contents_.erase(content);
            contents_[renamed] = std::move(moved);
        }
        return FileBrowserError::None;
    }
    return FileBrowserError::NotFound;
}

FileBrowserError DemoFileStorage::read(const std::string& path, std::size_t max_bytes,
                                       std::vector<std::uint8_t>& bytes) {
    const auto found = contents_.find(path);
    if (found == contents_.end()) return FileBrowserError::Read;
    bytes.assign(found->second.begin(),
                 found->second.begin() + static_cast<std::ptrdiff_t>(std::min(max_bytes, found->second.size())));
    return FileBrowserError::None;
}

bool DemoFileStorage::space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) {
    total_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;
    free_bytes = 24ULL * 1024ULL * 1024ULL * 1024ULL;
    return true;
}

FileBrowserRuntime::FileBrowserRuntime(const std::string& store_dir, bool deterministic)
    : storage_(std::filesystem::u8path(store_dir)), deterministic_(deterministic) {
    if (deterministic_ || store_dir.empty()) return;
    thread_ = std::thread([this] {
        FileWorker worker(mailbox_, storage_);
        while (!stopping_) {
            if (!worker.step()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
}

FileBrowserRuntime::~FileBrowserRuntime() {
    stopping_ = true;
    if (thread_.joinable()) thread_.join();
}

void FileBrowserRuntime::poll() {
    if (deterministic_) {
        while (demo_worker_.step()) {
        }
    }
    model_.poll();
}

}  // namespace aura::simulator

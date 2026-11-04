#include "aura/photo_runtime.hpp"
#include <chrono>
#include <fstream>
#include <windows.h>

namespace aura::simulator {
namespace {
bool regular_read(const std::filesystem::path& path, PhotoBytes& bytes, std::size_t limit) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    const bool valid = GetFileInformationByHandle(file, &info) && GetFileSizeEx(file, &size) &&
                       !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                       size.QuadPart >= 0 && static_cast<std::uint64_t>(size.QuadPart) <= limit;
    bool ok = false;
    if (valid) {
        if (!bytes.resize(static_cast<std::size_t>(size.QuadPart))) {
            CloseHandle(file);
            return false;
        }
        DWORD read = 0;
        ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    }
    CloseHandle(file);
    if (!ok) bytes.clear();
    return ok;
}
}  // namespace
bool DesktopPhotoStorage::ready(bool create) {
    if (root_.empty()) return false;
    std::error_code ec;
    // Reject redirected directories, including parent junctions.
    for (auto p = std::filesystem::absolute(root_, ec).lexically_normal(); !p.empty();) {
        const auto attributes = GetFileAttributesW(p.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (!create || (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND))
                return false;
        } else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            return false;
        auto parent = p.parent_path();
        if (parent == p) break;
        p = std::move(parent);
    }
    if (create && !ec) std::filesystem::create_directories(root_, ec);
    return !ec && std::filesystem::is_directory(root_, ec) && !ec;
}
PhotoError DesktopPhotoStorage::scan(std::vector<std::string>& names) {
    names.clear();
    if (!ready(true)) return PhotoError::Storage;
    std::error_code ec;
    std::size_t inspected = 0;
    for (std::filesystem::directory_iterator it(root_, ec), end; !ec && it != end && inspected < 2048;
         it.increment(ec), ++inspected) {
        const auto attr = GetFileAttributesW(it->path().c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        const auto name = it->path().filename().u8string();
        if (photo_filename_valid(name) && names.size() < kMaxPhotos) names.push_back(name);
    }
    return ec ? PhotoError::Storage : PhotoError::None;
}
PhotoError DesktopPhotoStorage::read(const std::string& name, PhotoBytes& bytes) {
    if (!photo_filename_valid(name) || !ready(false)) return PhotoError::Read;
    const auto path = root_ / std::filesystem::u8path(name);
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (!ec && size > kMaxPhotoFileBytes) return PhotoError::TooLarge;
    return regular_read(path, bytes, kMaxPhotoFileBytes) ? PhotoError::None : PhotoError::Read;
}
std::string DesktopPhotoStorage::read_settings() {
    if (!ready(true)) return {};
    PhotoBytes bytes;
    if (!regular_read(root_.parent_path() / "photos-settings.json", bytes, 512)) return {};
    return bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
bool DesktopPhotoStorage::erase(const std::string& name) {
    if (!photo_filename_valid(name) || !ready(false)) return false;
    const auto path = root_ / std::filesystem::u8path(name);
    HANDLE file =
        CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_DISPOSITION_INFO disposition{TRUE};
    const bool ok = GetFileInformationByHandle(file, &info) &&
                    !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                    SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
    CloseHandle(file);
    return ok;
}
bool DesktopPhotoStorage::save_settings(const std::string& text) {
    if (!ready(true) || text.empty() || text.size() > 512) return false;
    const auto path = root_.parent_path() / "photos-settings.json";
    auto temp = path;
    temp += ".tmp";
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        return false;
    HANDLE file =
        CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    DWORD written = 0;
    const bool ok = GetFileInformationByHandle(file, &info) &&
                    !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                    SetFilePointer(file, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER && SetEndOfFile(file) &&
                    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                    written == text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) return false;
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
PhotoRuntime::PhotoRuntime(const std::string& dir, bool deterministic)
    : storage_(std::filesystem::u8path(dir) / "photos") {
    if (!deterministic && !dir.empty())
        thread_ = std::thread([this] {
            PhotoWorker worker(mailbox_, storage_);
            worker.initialize();
            while (!stopping_) {
                if (!worker.step()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            // Flush already accepted preferences on normal shutdown, bounded to four jobs.
            for (unsigned i = 0; i < 4 && worker.step(); ++i) {
            }
        });
}
PhotoRuntime::~PhotoRuntime() {
    stopping_ = true;
    if (thread_.joinable()) thread_.join();
}
}  // namespace aura::simulator

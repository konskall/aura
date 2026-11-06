#include "aura/windows_aoraton_cache_files.hpp"

#include <chrono>
#include <fstream>
#include <system_error>
#include <utility>

namespace aura::simulator {
namespace {

constexpr const char* kExtension = ".enc";

}  // namespace

WindowsAoratonCacheFiles::WindowsAoratonCacheFiles(std::filesystem::path folder) : folder_(std::move(folder)) {}

std::filesystem::path WindowsAoratonCacheFiles::file(const std::string& name) const {
    return folder_ / (name + kExtension);
}

bool WindowsAoratonCacheFiles::list(std::vector<AoratonCacheEntry>& entries) {
    entries.clear();
    if (folder_.empty()) return false;
    std::error_code error;
    if (!std::filesystem::exists(folder_, error)) return !error;  // nothing kept yet
    std::filesystem::directory_iterator it(folder_, error);
    if (error) return false;
    // File times to seconds since the epoch (C++17 has no clock_cast): only
    // their order matters.
    const auto file_now = std::filesystem::file_time_type::clock::now();
    const auto system_now = std::chrono::system_clock::now();
    for (const auto end = std::filesystem::directory_iterator(); it != end; it.increment(error)) {
        if (error) return false;
        const auto& path = it->path();
        // Compared as wide text: a stray file's name may not be ASCII.
        if (path.extension().wstring() != L".enc") continue;
        const std::wstring wide = path.stem().wstring();
        std::string stem;
        for (const wchar_t letter : wide) {
            if (letter > 0x7F) break;
            stem.push_back(static_cast<char>(letter));
        }
        if (stem.size() != wide.size() || !aoraton_cache_name_valid(stem) || !it->is_regular_file(error)) continue;
        const auto size = it->file_size(error);
        if (error) continue;
        const auto written = it->last_write_time(error);
        if (error) continue;
        const auto since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
            (system_now + std::chrono::duration_cast<std::chrono::system_clock::duration>(written - file_now))
                .time_since_epoch());
        entries.push_back({stem, size, static_cast<std::int64_t>(since_epoch.count())});
    }
    return true;
}

bool WindowsAoratonCacheFiles::read(const std::string& name, std::size_t max_bytes, std::string& data) {
    data.clear();
    if (!aoraton_cache_name_valid(name)) return false;
    std::error_code error;
    const auto size = std::filesystem::file_size(file(name), error);
    if (error || size > max_bytes) return false;
    std::ifstream input(file(name), std::ios::binary);
    if (!input) return false;
    data.resize(static_cast<std::size_t>(size));
    if (!input.read(data.data(), static_cast<std::streamsize>(data.size())) ||
        input.peek() != std::char_traits<char>::eof()) {
        data.clear();
        return false;
    }
    return true;
}

bool WindowsAoratonCacheFiles::write(const std::string& name, const std::string& data) {
    if (!aoraton_cache_name_valid(name) || folder_.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(folder_, error);
    if (error) return false;
    const auto temporary = folder_ / (name + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.write(data.data(), static_cast<std::streamsize>(data.size())) || !output.flush()) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    // A copy being replaced goes first: rename may not overwrite on Windows.
    std::filesystem::remove(file(name), error);
    std::filesystem::rename(temporary, file(name), error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

void WindowsAoratonCacheFiles::remove(const std::string& name) {
    if (!aoraton_cache_name_valid(name)) return;
    std::error_code error;
    std::filesystem::remove(file(name), error);
}

}  // namespace aura::simulator

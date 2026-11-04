#include "esp32_photo_storage.hpp"
#include "sdkconfig.h"
#if !defined(CONFIG_FATFS_LFN_HEAP) || !defined(CONFIG_FATFS_API_ENCODING_UTF_8)
#error "The photo library requires FATFS long filenames and UTF-8; use firmware/sdkconfig.defaults"
#endif
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
namespace aura::firmware {
namespace {
constexpr const char* kPhotos = "/sdcard/aura/photos";
}
PhotoError Esp32PhotoStorage::scan(std::vector<std::string>& names) {
    names.clear();
    mkdir(kPhotos, 0775);
    DIR* dir = opendir(kPhotos);
    if (!dir) return PhotoError::Storage;
    std::size_t inspected = 0;
    while (const auto* entry = readdir(dir)) {
        if (++inspected > 2048) break;
        const std::string name(entry->d_name);
        if (!photo_filename_valid(name) || names.size() >= kMaxPhotos) continue;
        struct stat info{};
        if (stat((std::string(kPhotos) + "/" + name).c_str(), &info) == 0 && S_ISREG(info.st_mode))
            names.push_back(name);
    }
    closedir(dir);
    return PhotoError::None;
}
PhotoError Esp32PhotoStorage::read(const std::string& name, PhotoBytes& bytes) {
    if (!photo_filename_valid(name)) return PhotoError::Read;
    FILE* file = std::fopen((std::string(kPhotos) + "/" + name).c_str(), "rb");
    if (!file) return PhotoError::Read;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return PhotoError::Read;
    }
    const auto size = std::ftell(file);
    if (size < 0 || static_cast<unsigned long>(size) > kMaxPhotoFileBytes) {
        std::fclose(file);
        return PhotoError::TooLarge;
    }
    std::rewind(file);
    if (!bytes.resize(static_cast<std::size_t>(size))) {
        std::fclose(file);
        return PhotoError::TooLarge;
    }
    const bool ok = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    if (!ok) bytes.clear();
    return ok ? PhotoError::None : PhotoError::Read;
}
bool Esp32PhotoStorage::erase(const std::string& name) {
    if (!photo_filename_valid(name)) return false;
    const auto path = std::string(kPhotos) + "/" + name;
    struct stat info{};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && std::remove(path.c_str()) == 0;
}
}  // namespace aura::firmware

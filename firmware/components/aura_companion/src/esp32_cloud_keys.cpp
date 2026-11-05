#include "esp32_cloud_keys.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "nvs.h"

namespace aura::firmware {
namespace {

constexpr const char* kNamespace = "aura_cloud";

std::string read_key(nvs_handle_t handle, const char* name) {
    std::size_t size = 0;
    if (nvs_get_str(handle, name, nullptr, &size) != ESP_OK || size == 0 || size > 512) return {};
    std::vector<char> value(size, '\0');
    if (nvs_get_str(handle, name, value.data(), &size) != ESP_OK) return {};
    std::string text(value.data());
    std::fill(value.begin(), value.end(), '\0');
    return text;
}

bool write_key(nvs_handle_t handle, const char* name, const std::string& value) {
    if (value.empty()) {
        const esp_err_t erased = nvs_erase_key(handle, name);
        return erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND;
    }
    return nvs_set_str(handle, name, value.c_str()) == ESP_OK;
}

}  // namespace

CloudKeys Esp32CloudKeyStore::load() const {
    CloudKeys keys;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return keys;
    keys.groq_api_key = read_key(handle, "groq");
    keys.azure_speech_key = read_key(handle, "azure");
    keys.azure_speech_region = read_key(handle, "region");
    nvs_close(handle);
    if (!cloud_keys_valid(keys)) wipe_cloud_keys(keys);
    return keys;
}

bool Esp32CloudKeyStore::store(const CloudKeys& keys) {
    if (!cloud_keys_valid(keys)) return false;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool ok = write_key(handle, "groq", keys.groq_api_key) && write_key(handle, "azure", keys.azure_speech_key) &&
                    write_key(handle, "region", keys.azure_speech_region) && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (ok) generation_.fetch_add(1);
    return ok;
}

}  // namespace aura::firmware

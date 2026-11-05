// Firmware settings the shared code relies on. The simulator cannot see them, so
// a wrong value would first show on the board: these checks read the ESP-IDF
// defaults files the firmware is configured from.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ++failures;
        std::cerr << "FAIL: cannot read " << path << '\n';
        return {};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// Whether `text` has `line` as one whole line (CR LF or LF endings).
bool has_line(const std::string& text, const std::string& line) {
    std::istringstream lines(text);
    std::string current;
    while (std::getline(lines, current)) {
        if (!current.empty() && current.back() == '\r') current.pop_back();
        if (current == line) return true;
    }
    return false;
}

// Whether any line sets `key` (as "key=..." or "# key is not set").
bool mentions(const std::string& text, const std::string& key) {
    return text.find(key + "=") != std::string::npos || text.find("# " + key + " is not set") != std::string::npos;
}

void check_lvgl_memory(const std::string& defaults) {
    // LVGL's own pool is a fixed 64 KiB on the device; the transient layers of a
    // transformed widget alone take 28-427 KiB, so an allocation failed there and
    // LVGL silently skipped the widget. Through the C library, LVGL allocates from
    // the system heap: small blocks stay in internal RAM and blocks larger than
    // CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL go to PSRAM.
    expect(has_line(defaults, "CONFIG_LV_USE_CLIB_MALLOC=y"), "LVGL allocates through the C library (heap + PSRAM)");
    expect(!has_line(defaults, "CONFIG_LV_USE_BUILTIN_MALLOC=y"), "LVGL's fixed builtin pool is not selected");
    expect(!mentions(defaults, "CONFIG_LV_MEM_SIZE"), "no LVGL pool size remains configured");
    expect(has_line(defaults, "CONFIG_SPIRAM_USE_MALLOC=y"), "malloc() can return PSRAM");
}

void check_power(const std::string& defaults) {
    // Dynamic frequency scaling: the firmware holds the processor at full speed
    // while the screen is lit and lets it drop when it is dark (app_main).
    expect(has_line(defaults, "CONFIG_PM_ENABLE=y"), "power management is on");
}

void check_security(const std::string& defaults, const std::string& production) {
    // Development boards: NVS (the Wi-Fi password, Aoraton keys) is encrypted with
    // keys derived by the HMAC peripheral from a key the first boot burns into
    // eFuse block 5; flashing and debugging stay open.
    expect(has_line(defaults, "CONFIG_NVS_ENCRYPTION=y"), "NVS is encrypted");
    expect(has_line(defaults, "CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y"), "NVS keys come from the HMAC peripheral");
    expect(has_line(defaults, "CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=5"), "the HMAC key lives in eFuse block 5");
    expect(!has_line(defaults, "CONFIG_SECURE_BOOT=y") && !has_line(defaults, "CONFIG_SECURE_FLASH_ENC_ENABLED=y"),
           "a development board is never locked: secure boot and flash encryption stay in production");
    // Production locks the board for good, on purpose.
    expect(has_line(production, "CONFIG_SECURE_BOOT=y") && has_line(production, "CONFIG_SECURE_BOOT_V2_ENABLED=y"),
           "production boots only signed images");
    expect(has_line(production, "CONFIG_SECURE_FLASH_ENC_ENABLED=y") &&
               has_line(production, "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y"),
           "production encrypts the flash in release mode");
    expect(has_line(production, "CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y"),
           "production keeps NVS keys in the flash-encrypted nvs_keys partition");
}

}  // namespace

int main() {
    const std::string root = AURA_REPO_DIR;
    const std::string defaults = read_file(root + "/firmware/sdkconfig.defaults");
    check_lvgl_memory(defaults);
    check_power(defaults);
    check_security(defaults, read_file(root + "/firmware/sdkconfig.production.defaults"));
    if (failures != 0) {
        std::cerr << failures << " firmware configuration check(s) failed\n";
        return 1;
    }
    std::cout << "firmware configuration checks passed\n";
    return 0;
}

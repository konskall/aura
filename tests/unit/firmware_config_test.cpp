// Firmware settings the shared code relies on. The simulator cannot see them, so
// a wrong value would first show on the board: these checks read the ESP-IDF
// defaults files the firmware is configured from.
#include "aura/ota_update.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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
    // the system heap, which puts them in PSRAM (check_internal_ram).
    expect(has_line(defaults, "CONFIG_LV_USE_CLIB_MALLOC=y"), "LVGL allocates through the C library (heap + PSRAM)");
    expect(!has_line(defaults, "CONFIG_LV_USE_BUILTIN_MALLOC=y"), "LVGL's fixed builtin pool is not selected");
    expect(!mentions(defaults, "CONFIG_LV_MEM_SIZE"), "no LVGL pool size remains configured");
    expect(has_line(defaults, "CONFIG_SPIRAM_USE_MALLOC=y"), "malloc() can return PSRAM");
}

// The first flash (2026-11-14): the board ran out of internal RAM at boot, and a
// task stack, a mutex and the Bluetooth controller could not be allocated.
// malloc() takes PSRAM first, whatever the size; 64 KiB stay for allocations
// that must be internal or DMA-capable; Wi-Fi's buffers live in PSRAM and its
// code out of IRAM, which shares internal RAM with the heap.
void check_internal_ram(const std::string& defaults) {
    expect(has_line(defaults, "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0"), "malloc() never prefers internal RAM");
    expect(has_line(defaults, "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536"),
           "64 KiB of internal RAM stay for stacks, FreeRTOS objects and DMA");
    expect(has_line(defaults, "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y"), "Wi-Fi and lwIP buffers in PSRAM");
    expect(has_line(defaults, "CONFIG_ESP_WIFI_IRAM_OPT=n") && has_line(defaults, "CONFIG_ESP_WIFI_RX_IRAM_OPT=n"),
           "Wi-Fi's code stays out of IRAM");
    expect(has_line(defaults, "CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288"), "the UI loop's 12 KiB stack");
}

// The BSP 2.0.1's bsp_display_lock() returns esp_lv_adapter_lock()'s esp_err_t
// as a bool: false exactly when the lock was taken. On the first flash the UI
// was never built and the screen stayed white. The firmware takes LVGL's lock
// through aura_board_display_lock() only.
void check_display_lock(const std::string& root) {
    for (const char* file : {"/firmware/main/app_main.cpp",
                             "/firmware/components/aura_companion/src/esp32_companion.cpp",
                             "/firmware/components/aura_board/src/aura_display.c"}) {
        const std::string text = read_file(root + file);
        expect(text.find("bsp_display_lock(") == std::string::npos,
               std::string(file) + ": no bsp_display_lock(), whose result is inverted");
    }
}

// Two seconds of white at every start (the owner, 2026-11-14): the BSP's
// sequence lit the panel at full brightness with whatever its memory held, and
// LVGL then drew its default near-white screen until the UI was built. The
// panel now starts dark (its sequence without 29h, the brightness at 0); the
// brand's start-up screen is drawn before any service starts, and only then
// does the panel light.
// The text without its // and /* */ comments: a check that a call is gone
// must not see the comments that tell its story.
std::string without_comments(const std::string& text) {
    std::string code;
    for (std::size_t index = 0; index < text.size();) {
        if (text.compare(index, 2, "//") == 0) {
            index = text.find('\n', index);
            if (index == std::string::npos) break;
        } else if (text.compare(index, 2, "/*") == 0) {
            const auto end = text.find("*/", index + 2);
            index = end == std::string::npos ? text.size() : end + 2;
        } else {
            code += text[index++];
        }
    }
    return code;
}

void check_dark_start(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string display =
        without_comments(read_file(root + "/firmware/components/aura_board/src/aura_display.c"));
    const auto table = display.find("kPanelStart[] = {");
    const std::string start = table == none ? std::string{} : display.substr(table, display.find("};", table) - table);
    expect(start.find("{0x11, ") != none && start.find("{0x51, (uint8_t[]){0x00}, 1, 0}") != none,
           "the panel wakes at brightness 0");
    expect(!start.empty() && start.find("{0x29") == none, "the panel's start leaves it switched off");
    expect(display.find("bsp_display_new(") == none && display.find("bsp_display_brightness") == none,
           "the BSP's lit start is not used");
    const std::string companion =
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_companion.cpp"));
    expect(companion.find("bsp_display_brightness") == none,
           "the brightness goes through aura_board_display_set_brightness(), the panel AURA started");
    const std::string main = read_file(root + "/firmware/main/app_main.cpp");
    const auto shown = main.find("ui.show_startup(display)");
    const auto lit = main.find("aura_board_display_reveal(");
    const auto audio = main.find("audio.initialize()");
    const auto created = main.find("ui.create(display");
    expect(shown != none && lit != none && audio != none && created != none && shown < lit && lit < audio &&
               audio < created,
           "the start-up screen is drawn, then the panel lit, before the services start and the UI is built");
}

// Synapse's middle rows go to the panel straight (LVGL spent 47-72 ms on each
// of its frames, 2026-11-14): from two strips of internal RAM the panel's DMA
// reads, taken once the services have started and before the UI is built.
void check_synapse_panel_output(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string main = without_comments(read_file(root + "/firmware/main/app_main.cpp"));
    const auto worker = main.find("start_frame_worker();\n");
    const auto helper = main.find("start_frame_helper();\n");
    const auto output = main.find("start_synapse_output();");
    const auto created = main.find("ui.create(display");
    expect(worker != none && helper != none && output != none && created != none && worker < helper &&
               helper < output && output < created,
           "Synapse's panel output starts after its worker, its helper and the services, before the UI");
    expect(main.find("index < 2 ? (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) : MALLOC_CAP_SPIRAM") != none &&
               main.find("aura::ui::set_synapse_output(&synapse_panel)") != none,
           "the worker's strips are internal RAM the panel's DMA reads, the helper's PSRAM");
    // The helper draws on the second core, below LVGL's task there.
    const auto helper_start = main.find("void start_frame_helper()");
    const auto helper_started = main.find("aura::ui::FrameWorker::start_helper();", helper_start);
    const std::string helper_config = helper_start == none || helper_started == none
                                          ? std::string{}
                                          : main.substr(helper_start, helper_started - helper_start);
    expect(helper_config.find("helper.pin_to_core = 1;") != none && helper_config.find("helper.prio = 1;") != none,
           "Synapse's helper draws on the second core, below LVGL");
    expect(main.find("return aura_board_display_lock(") != none && main.find("aura_board_display_draw(") != none,
           "Synapse's strips go to the panel under LVGL's lock");
    const std::string display =
        without_comments(read_file(root + "/firmware/components/aura_board/src/aura_display.c"));
    expect(display.find("esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, pixels)") != none,
           "the strips go to the panel AURA started");
    // The loop shows a drawn frame and begins the next without waiting out
    // its 33 ms (it drew 6.9 frames a second from 85 ms ones, 2026-11-15):
    // the worker wakes it, and it sleeps no longer than Synapse's rest, while
    // the screen is lit.
    const auto woken = main.find("aura::ui::FrameWorker::shared()->set_finished(wake_loop, xTaskGetCurrentTaskHandle());");
    expect(woken != none && worker < woken && woken < created && main.find("xTaskNotifyGive(") != none,
           "Synapse's worker wakes the loop when a frame is drawn");
    const auto lit = main.find("if (aura::needs_full_speed(mode)) {\n            const std::uint32_t due = ui.next_tick_ms();");
    const auto counted = main.find("sleep_ms = std::min(interval, due > spent ? due - spent : 0U);", lit);
    expect(lit != none && counted != none && main.find("ticked = xTaskGetTickCount();\n            ui.tick(elapsed_ms);") != none &&
               main.find("ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep_ms));") != none &&
               main.find("vTaskDelay(pdMS_TO_TICKS(aura::host_loop_interval_ms(mode)));") == none,
           "the loop sleeps until Synapse's next tick, less the pass since it, or a frame drawn, while lit");
}

void check_transfer_server(const std::string& root) {
    // With its two connections taken (the browser keeps idle ones open), ESP-IDF's
    // server stops listening unless it may close the least recently used one: the
    // login's request (a new connection) then waited forever (the first flash,
    // 2026-11-14; esp_http_server/src/httpd_main.c).
    const std::string text = read_file(root + "/firmware/components/aura_companion/src/esp32_file_transfer.cpp");
    expect(text.find("config.lru_purge_enable = true;") != std::string::npos,
           "the file transfer server makes room for a new connection");
    // Downloads (the owner's request, 2026-11-14) stream through the server's
    // chunks; a cut one ends with the connection, never with HTTP's last chunk.
    expect(text.find("protocol.handle(r, reader, &writer)") != std::string::npos &&
               text.find("written_ == announced_ ? httpd_resp_send_chunk(request_, nullptr, 0) : ESP_FAIL") !=
                   std::string::npos,
           "the device streams downloads and never ends a cut one as complete");
}

// Ήχος → «Ακρόαση φωνής» speaks the preview phrase in the chosen voice. The UI
// disables the button when no one handles it, and the firmware never did: on
// the board it could not be pressed (the owner, 2026-11-16).
void check_voice_preview(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string main = without_comments(read_file(root + "/firmware/main/app_main.cpp"));
    const auto handled = main.find("callbacks.on_voice_preview = ");
    const auto created = main.find("ui.create(display");
    expect(handled != none && created != none && handled < created,
           "the device handles «Ακρόαση φωνής» before the UI is built");
    expect(main.find(".voice_preview_phrase") != none && main.find("assistant_voice_id(voice)") != none,
           "the preview speaks the preview phrase in the voice it was pressed for");
}

// LVGL's pixels live in PSRAM, which the SPI driver cannot send: it allocated a
// buffer of internal RAM for every transfer, and with the microphone open or a
// TLS connection starting there was none («Draw bitmap failed: ESP_ERR_NO_MEM»,
// the screen stood still, 2026-11-16). They go through two strips of internal
// RAM taken once at start; the microphone's capture task keeps its stack in
// PSRAM.
void check_panel_memory(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string display =
        without_comments(read_file(root + "/firmware/components/aura_board/src/aura_display.c"));
    const auto taken = display.find("heap_caps_malloc(s_bounce_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)");
    const auto used = display.find("esp_lv_adapter_set_draw_bitmap_callbacks(display, &draw, NULL)");
    expect(taken != none && used != none && taken < used,
           "LVGL's pixels reach the panel through strips of internal RAM taken once at start");
    expect(display.find("esp_ptr_dma_capable(color_map)") != none,
           "pixels already in internal RAM (Synapse's strips) go to the panel as they are");
    const std::string microphone =
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_microphone.cpp"));
    expect(microphone.find("xTaskCreateWithCaps(&Esp32Microphone::task, \"aura_mic\"") != none &&
               microphone.find("MALLOC_CAP_SPIRAM") != none && microphone.find("vTaskDeleteWithCaps(nullptr)") != none,
           "the microphone's capture task keeps its stack in PSRAM");
}

// A question's recording reached Groq at 20-58 KB/s: one TCP window of 5760
// bytes a round trip, and with Bluetooth on the Wi-Fi radio must doze, so a
// round trip took 100-250 ms (2026-11-16). 16 KB in flight; lwIP's buffers live
// in PSRAM (CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP).
void check_upload_window(const std::string& defaults) {
    expect(has_line(defaults, "CONFIG_LWIP_TCP_SND_BUF_DEFAULT=16384"), "an upload keeps 16 KB in flight");
}

void check_network_name(const std::string& defaults) {
    // The name the router lists for AURA (DHCP option 12); ESP-IDF's default is
    // "espressif" (the owner's request on the first flash, 2026-11-14). Host
    // names allow only letters, digits and hyphens, so no spaces.
    expect(has_line(defaults, "CONFIG_LWIP_LOCAL_HOSTNAME=\"Aura-Ai-Assistant\""),
           "the router lists AURA as Aura-Ai-Assistant, not espressif");
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

struct Partition {
    std::string name;
    std::uint32_t offset{0};
    std::uint32_t size{0};
    bool app{false};
};

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

// The rows of an ESP-IDF partition table: name, type, subtype, offset, size.
std::vector<Partition> partitions(const std::string& table) {
    std::vector<Partition> rows;
    std::istringstream lines(table);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cells;
        std::istringstream fields(line);
        std::string cell;
        while (std::getline(fields, cell, ',')) cells.push_back(trim(cell));
        if (cells.size() < 5) {
            expect(false, "a partition row has five columns: " + line);
            continue;
        }
        Partition row;
        row.name = cells[0];
        row.app = cells[1] == "app";
        row.offset = static_cast<std::uint32_t>(std::stoul(cells[3], nullptr, 0));
        row.size = static_cast<std::uint32_t>(std::stoul(cells[4], nullptr, 0));
        rows.push_back(row);
    }
    return rows;
}

void check_partitions(const std::string& defaults, const std::string& development, const std::string& production) {
    // Two 7 MB app slots on the 16 MB flash (2026-11-06, the owner's choice):
    // the firmware had only 10% of 6.5 MB slots free. Everything the owner
    // keeps (photos, notes, transfers) lives on the microSD card, so the
    // internal `storage` area is a small reserve.
    expect(has_line(defaults, "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y"), "the flash is 16 MB");
    constexpr std::uint32_t kSlot = 0x700000;
    expect(aura::OtaUpdateModel::kMaxImageBytes == kSlot, "an update may fill one app slot, and no more");
    const std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> fixed = {
        {"ota_0", {0x20000, kSlot}},
        {"ota_1", {0x720000, kSlot}},
        {"storage", {0xE20000, 0x1D0000}},
        {"coredump", {0xFF0000, 0x10000}},
    };
    for (const auto* table : {&development, &production}) {
        const char* which = table == &development ? "development" : "production";
        const auto rows = partitions(*table);
        std::uint32_t end = 0;
        std::size_t found = 0;
        for (const auto& row : rows) {
            expect(row.offset >= end, std::string(which) + ": " + row.name + " does not overlap the one before");
            end = row.offset + row.size;
            if (row.app) expect(row.offset % 0x10000 == 0, std::string(which) + ": " + row.name + " is 64 KiB aligned");
            const auto expected = fixed.find(row.name);
            if (expected == fixed.end()) continue;
            ++found;
            expect(row.offset == expected->second.first && row.size == expected->second.second,
                   std::string(which) + ": " + row.name + " has the agreed offset and size");
        }
        expect(found == fixed.size(), std::string(which) + ": both app slots, storage and coredump exist");
        expect(end <= 0x1000000, std::string(which) + ": the table fits the 16 MB flash");
    }
}

}  // namespace

int main() {
    const std::string root = AURA_REPO_DIR;
    const std::string defaults = read_file(root + "/firmware/sdkconfig.defaults");
    check_lvgl_memory(defaults);
    check_internal_ram(defaults);
    check_display_lock(root);
    check_dark_start(root);
    check_synapse_panel_output(root);
    check_voice_preview(root);
    check_panel_memory(root);
    check_upload_window(defaults);
    check_network_name(defaults);
    check_transfer_server(root);
    check_power(defaults);
    check_security(defaults, read_file(root + "/firmware/sdkconfig.production.defaults"));
    check_partitions(defaults, read_file(root + "/firmware/partitions.csv"),
                     read_file(root + "/firmware/partitions.production.csv"));
    if (failures != 0) {
        std::cerr << failures << " firmware configuration check(s) failed\n";
        return 1;
    }
    std::cout << "firmware configuration checks passed\n";
    return 0;
}

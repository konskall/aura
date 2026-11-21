// Firmware settings the shared code relies on. The simulator cannot see them, so
// a wrong value would first show on the board: these checks read the ESP-IDF
// defaults files the firmware is configured from.
#include "aura/ota_update.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    // The page downloads through /api/download, a path the server never
    // registered: ESP-IDF answered 404 and the page said there was no connection,
    // while sending to the device worked (the owner, 2026-11-18).
    const std::string code = without_comments(text);
    for (const char* path : {"/api/session", "/api/upload", "/api/files", "/api/download", "/api/delete", "/api/keys"}) {
        expect(code.find(std::string("uri.uri = \"") + path + "\";") != std::string::npos,
               std::string("the file transfer server registers ") + path);
    }
    // One path more than the server has room for refuses the whole transfer.
    const auto count = [&code](const std::string& needle) {
        std::size_t found = 0;
        for (auto at = code.find(needle); at != std::string::npos; at = code.find(needle, at + 1)) ++found;
        return found;
    };
    const auto setting = [&code](const std::string& name) -> unsigned long {
        const auto at = code.find(name);
        return at == std::string::npos ? 0UL : std::strtoul(code.c_str() + at + name.size(), nullptr, 10);
    };
    expect(setting("config.max_uri_handlers = ") >= 1 + count("\"/api/") + count("\"/aoraton/"),
           "the file transfer server has room for every path it registers");
    // lwIP resends a lost packet after 1.5 s, then 3 s (CONFIG_LWIP_TCP_RTO_TIME):
    // a send that gives up within a second cuts the download at the first one.
    expect(setting("config.send_wait_timeout = ") >= 5, "a download outlasts a lost packet");
}

// The device by voice (the owner, 2026-11-18): the owner's own words, matched on
// the device, are done at once; what the model proposes waits for a tap.
void check_device_commands(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string main = without_comments(read_file(root + "/firmware/main/app_main.cpp"));
    const auto local = main.find("assistant.turn->set_local_request(");
    const auto parsed = main.find("aura::parse_offline_command(question)", local);
    expect(local != none && parsed != none, "the owner's words for the device are matched on the device");
    expect(main.find("companion.system_action(aura::SystemAction::Restart)", parsed) != none &&
               main.find("companion.system_action(aura::SystemAction::PowerOff)", parsed) != none,
           "the device restarts and switches off when told to");
    expect(main.find("ui.open_app(app)", parsed) != none, "the device opens the app it is told to");
    expect(main.find("companion.perform_action(command.action)", parsed) != none,
           "the volume, the brightness and the alarms are set when the owner says so");
    expect(main.find("companion.propose_action(action);") != none, "what the model proposes is only proposed");
    const std::string companion =
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_companion.cpp"));
    expect(companion.find("active_ui_->open_app(action.app)") != none &&
               companion.find("case AssistantActionType::Restart: system_action(SystemAction::Restart)") != none &&
               companion.find("case AssistantActionType::PowerOff: system_action(SystemAction::PowerOff)") != none,
           "a confirmed proposal reaches the screens and the board's power");
}

// While the device speaks the microphone's last second is kept for the words that
// interrupt it. Moving those 32 KB at every pass emptied the data cache, and the
// animation's frames took a third longer (measured on the board, 2026-11-18).
void check_barge_words(const std::string& root) {
    const std::string companion =
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_companion.cpp"));
    expect(companion.find("barge_words_.erase(") == std::string::npos &&
               companion.find("barge_words_next_ = (barge_words_next_ + 1U) % kWordsKept;") != std::string::npos,
           "the words over the answer are kept in a ring, never moved");
}

// The menus' movement on the board (the owner, 2026-11-18): a whole-screen fade
// took ten frames of 150-190 ms to open a screen.
void check_view_fade(const std::string& root) {
    const std::string main = without_comments(read_file(root + "/firmware/main/app_main.cpp"));
    const auto off = main.find("ui.set_view_fade(false);");
    const auto created = main.find("ui.create(display");
    expect(off != std::string::npos && created != std::string::npos && off < created,
           "screens open at once on the board, without a fade");
}

// The owner's requests of 2026-11-18 on the board: a sound for Aoraton's
// messages too, two fingers from the touch controller, the Αρχεία app's worker,
// the conversation kept on the card, and the sign-in screen kept awake.
void check_owner_tools(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string companion =
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_companion.cpp"));
    const auto chime = companion.find("message_chime_.observe(");
    const auto aoraton_ids = companion.find("aoraton_->snapshot().notifications");
    expect(aoraton_ids != none && chime != none && aoraton_ids < chime &&
               companion.find("ids.push_back(\"a\" + item.message_id)") != none,
           "a new Aoraton message chimes on the device, not only a phone notification");
    expect(companion.find("chime_aoraton_sequence_") != none, "the chime looks again when Aoraton's messages change");
    const std::string display =
        without_comments(read_file(root + "/firmware/components/aura_board/src/aura_display.c"));
    expect(display.find("touch_cfg.callbacks.custom_touch_read = read_two_fingers;") != none &&
               display.find("kTouchBytes = 15") != none,
           "the touch controller is read for two fingers");
    // Φακός (the owner, 2026-11-21): while it shows, the panel at full
    // brightness and the screen kept awake; face down it goes dark as ever,
    // and the owner's brightness comes back after it.
    expect(companion.find("const bool flashlight = active_ui_ && active_ui_->view() == UiView::Flashlight;") != none &&
               companion.find("const bool busy = flashlight ||") != none &&
               companion.find("flashlight && display_.snapshot().mode == DisplayMode::Active ? 100 : "
                              "display_.snapshot().brightness_percent") != none,
           "the flashlight keeps the screen awake at full brightness");
    // Without LVGL's gesture recognition the adapter asks for one finger
    // (MAX_TOUCH_POINTS_V9 is 1), and asking esp_lcd_touch for no more kept
    // the second from the photo (the owner, 2026-11-20: the zoom did nothing).
    expect(display.find("esp_lcd_touch_get_data(touch, points, count, max_count)") == none &&
               display.find("esp_lcd_touch_get_data(touch, both, &found, 2)") != none,
           "both fingers are read whatever the adapter asks for");
    const std::string main = without_comments(read_file(root + "/firmware/main/app_main.cpp"));
    expect(main.find("aura_board_touch_points(finger_x, finger_y)") != none &&
               main.find("ui.set_touch_points(points") != none,
           "the fingers reach the UI");
    const auto task = companion.find("void Esp32Companion::storage_task()");
    const auto worker = companion.find("file_worker_.step();", task);
    const auto queue = companion.find("xQueueReceive(storage_jobs_", task);
    expect(task != none && worker != none && queue != none && worker < queue,
           "the card's own worker reads and deletes for Αρχεία");
    expect(companion.find("\"conversations.txt\", conversation_log_.serialize()") != none,
           "the conversation is stored on the card");
    const std::string store =
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_microphone.cpp"));
    const auto reset = store.find("void Esp32CompanionStore::factory_reset()");
    expect(reset != none && store.find("\"conversations.txt\"", reset) != none,
           "a factory reset deletes the stored conversation");
    expect(main.find("assistant.turn->set_turn_recorded(") != none, "each answer reaches the history");
    expect(companion.find("UiView::AoratonSignIn") != none, "the screen stays on while the sign-in waits for the phone");
}

// The full check of 2026-11-19. An update whose connection drops reads nothing
// for ever: esp_http_client_read() gives 0 both for "nothing yet" and for a
// connection the server closed early, so without a deadline the install task
// waited for good and the screen stayed at its last percent.
void check_update_stall(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string ota = without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_ota.cpp"));
    const auto loop = ota.find("const char* Esp32Ota::install(");
    const auto zero = ota.find("if (read == 0)", loop);
    const auto deadline = ota.find("> kDownloadStallUs", zero);
    const auto next = ota.find("continue;", zero);
    expect(loop != none && zero != none && deadline != none && deadline < next,
           "an update that receives nothing for a while ends instead of waiting for ever");
    expect(ota.find("last_data_us = esp_timer_get_time();", zero) != none,
           "the deadline starts again with every piece of the image");
}

// System task stacks measured on the board (2026-11-19, the least ever free):
// the event task, which runs the Wi-Fi handlers (the saved network read at
// start; a new one written to NVS once it connects, then SNTP started), kept
// 604 of its 2304 bytes before the write ever ran; the inter-core tasks 384 and
// 472 of 1280.
void check_task_stacks(const std::string& defaults) {
    expect(has_line(defaults, "CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096"),
           "the event task has room for the Wi-Fi handlers' NVS writes");
    expect(has_line(defaults, "CONFIG_ESP_IPC_TASK_STACK_SIZE=1536"), "the inter-core tasks have some room left");
}

// Per-sample sums on the board stay in integers: the ESP32-S3 computes doubles
// in software, and the microphone and the echo check ran them for every sample.
void check_integer_sums(const std::string& root) {
    const std::string microphone =
        read_file(root + "/firmware/components/aura_companion/include/esp32_microphone.hpp") +
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_microphone.cpp"));
    expect(microphone.find("std::int64_t voice{0};") != std::string::npos &&
               microphone.find("static_cast<double>(voice) * voice") == std::string::npos,
           "the microphone's levels add whole samples, without doubles");
    const std::string companion =
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_companion.cpp"));
    expect(companion.find("level.energy += static_cast<std::int64_t>(sample) * sample;") != std::string::npos,
           "a question's level adds whole samples, without doubles");
}

// Σήμερα on the board (the owner, 2026-11-18): the read-aloud button needs a
// handler to exist at all, and the fuel prices a source of the device's own.
void check_today(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string main = without_comments(read_file(root + "/firmware/main/app_main.cpp"));
    const auto created = main.find("ui.create(display");
    const auto speak = main.find("callbacks.on_speak = ");
    const auto stop = main.find("callbacks.on_speak_stop = ");
    expect(speak != none && stop != none && created != none && speak < created && stop < created,
           "the device handles the read-aloud button before the UI is built");
    expect(main.find("assistant.turn->read_aloud(text)") != none &&
               main.find("companion.set_speaking(assistant.turn->speaking())") != none,
           "the button reads aloud and turns into a stop button while the voice is on");
    expect(main.find("fuel_backend.request()") != none && main.find("fuel_backend.poll(fuel_prices)") != none &&
               main.find("ui.apply_fuel(fuel.snapshot())") != none,
           "the week's fuel prices reach Σήμερα");
    expect(main.find("input.fuel = &fuel.snapshot();") != none, "and the assistant's facts");
    const std::string component = read_file(root + "/firmware/components/aura_weather/CMakeLists.txt");
    expect(component.find("src/esp32_fuel_backend.cpp") != none, "the fuel backend is built");
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

// The board wires the speaker's signal back into the ES7210 (MIC3), and the
// microphone hears the device as loud as the owner: barge-in cut answers by
// itself (2026-11-16). Measured on the board: four TDM slots, 0 the speaker's
// signal, 1 and 3 the microphones; ~250 ms of noise in every slot after each
// start; 30 dB clipped speech near the device. The clips go out in 32-bit
// samples so the shared I2S frame is 64 bits both ways.
void check_echo_reference(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string microphone =
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_microphone.cpp"));
    const std::string header =
        without_comments(read_file(root + "/firmware/components/aura_companion/include/esp32_microphone.hpp"));
    const auto gain = microphone.find("esp_codec_dev_set_in_gain(codec, kVoiceGainDb)");
    const auto reference = microphone.find("enable_reference(codec)");
    expect(microphone.find("info.channel = kSlots") != none && gain != none && reference != none && gain < reference,
           "the microphone opens four slots, the speaker's signal among them");
    expect(microphone.find("kReferenceSlot = 0") != none && microphone.find("kVoiceSlot = 1") != none &&
               microphone.find("kInterfaceReg, kTdm") != none,
           "slot 0 is the speaker's signal and slot 1 the voice, in TDM");
    // 24 dB heard a voice a metre away at -50 dBFS, under the recorder's floor
    // and the wake word's training (the owner had to shout, 2026-11-20); close
    // speech peaked at -20 dBFS, so 33 dB leaves it room. While AURA speaks, 18.
    expect(header.find("kVoiceGainDb = 33.0F") != none && header.find("kMonitorGainDb = 18.0F") != none,
           "listening at 33 dB; while AURA speaks, 18");
    expect(header.find("kSettleMs = 300") != none && microphone.find("16U * kSettleMs") != none,
           "the ES7210's first 300 ms after a start are dropped");
    const std::string audio =
        without_comments(read_file(root + "/firmware/components/aura_audio/src/esp32_audio_backend.cpp"));
    expect(audio.find("sample_info.bits_per_sample = 32") != none &&
               audio.find("esp_codec_dev_write(codec_, wide_, count * sizeof(std::int32_t))") != none,
           "a clip goes out in 32-bit samples: a 64-bit I2S frame, as the microphone's");
    const std::string companion =
        without_comments(read_file(root + "/firmware/components/aura_companion/src/esp32_companion.cpp"));
    expect(companion.find("barge_gate_->observe(samples_.data(), reference_.data(), samples_.size())") != none,
           "barge-in takes the speaker's signal out of the microphone");
    expect(companion.find("microphone_.discard()") != none &&
               companion.find("recorder_.append(words.data(), words.size())") != none,
           "a question after an answer keeps the open microphone, and a barge-in's words");
    const std::string main = without_comments(read_file(root + "/firmware/main/app_main.cpp"));
    const auto listen = main.find("static const auto start_listening");
    const auto question = main.find("companion.begin_question()", listen);
    expect(listen != none && question != none &&
               main.substr(listen, question - listen).find("set_barge_monitor(false)") == none,
           "listening does not close the barge-in monitor's microphone first");
}

// The voice is asked for while the answer is still being written. Behind the
// answer on one worker it came seconds late («με το που έρχεται το μήνυμα αργεί
// να παίξει ο ήχος», the owner, 2026-11-16): it has a thread and a connection
// of its own, the thread's stack in PSRAM like the first's.
void check_voice_worker(const std::string& root) {
    constexpr auto none = std::string::npos;
    const std::string main = without_comments(read_file(root + "/firmware/main/app_main.cpp"));
    expect(main.find("voice_worker = start_cloud_worker(\"aura_voice\")") != none &&
               main.find("cloud_worker = start_cloud_worker(\"aura_cloud\")") != none,
           "the voice has a worker of its own");
    expect(main.find("worker.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") != none,
           "both workers keep their stacks in PSRAM");
    const auto speech = main.find("make_unique<aura::CloudSpeechService>(cloud_https, *cloud_worker, voice_https,");
    expect(speech != none && main.find("*voice_worker, keys);", speech) != none,
           "the voice goes over its own connection");
    expect(main.find("!cloud_worker || !voice_worker") != none, "the assistant waits for both workers");
    // It plays as it arrives, from the first tenths of a second: read outside
    // the backend's lock, waiting while the rest is on its way, and dropped when
    // the voice is stopped.
    const std::string audio =
        without_comments(read_file(root + "/firmware/components/aura_audio/src/esp32_audio_backend.cpp"));
    const std::string header =
        without_comments(read_file(root + "/firmware/components/aura_audio/include/esp32_audio_backend.hpp"));
    expect(audio.find("(clip.samples.empty() && !clip.feed)") != none &&
               header.find("!clip_.samples.empty() || clip_.feed || !pending_wav_path_.empty()") != none,
           "the speaker takes a voice still arriving");
    const auto voice_loop = audio.find("feed->read(fed, chunk.data(), kChunkSamples, ended)");
    expect(voice_loop != none && audio.rfind("xSemaphoreGive(mutex_);", voice_loop) != none &&
               audio.find("if (ended) break;", voice_loop) != none,
           "it reads the voice without holding the backend's lock, to the voice's end");
    const auto stop = audio.find("void Esp32AudioBackend::stop()");
    const auto stop_end = audio.find("void Esp32AudioBackend::set_volume", stop);
    expect(stop != none && audio.substr(stop, stop_end - stop).find("clip_.feed.reset()") != none,
           "stopping the voice drops it");
    // The connections open while the owner speaks, with a HEAD that asks for
    // nothing, unless one served a request moments ago.
    const std::string https =
        without_comments(read_file(root + "/firmware/components/aura_network/src/esp32_https_client.cpp"));
    const auto warm = https.find("void Esp32HttpsClient::warm(const HttpsRequest& request)");
    expect(warm != none && https.find("candidate.used_ms < kFreshMs", warm) != none &&
               https.find("send(request, {}, true)", warm) != none,
           "a connection is opened ahead of a request, once");
    expect(https.find("head ? HTTP_METHOD_HEAD : HTTP_METHOD_POST") != none &&
               https.find("if (head) return finish(nullptr, true);") != none,
           "a HEAD reads no body, and its connection serves the next request");
    // The speaker opens first: the microphone's start kept the voice waiting.
    const auto monitor = main.find("companion.set_barge_monitor(");
    expect(monitor != none && main.find("companion.barge_monitoring()", monitor) != none &&
               main.find("aura::AudioState::Playing", monitor) != none,
           "barge-in's microphone opens once the voice is sounding");
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
    check_echo_reference(root);
    check_voice_worker(root);
    check_upload_window(defaults);
    check_network_name(defaults);
    check_transfer_server(root);
    check_device_commands(root);
    check_today(root);
    check_barge_words(root);
    check_view_fade(root);
    check_owner_tools(root);
    check_update_stall(root);
    check_integer_sums(root);
    check_task_stacks(defaults);
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

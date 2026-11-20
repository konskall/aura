// The display bring-up of the Waveshare ESP32-S3-Touch-AMOLED-2.16 BSP 2.0.1
// (bsp_display_start(), bsp_display_lcd_init() and bsp_display_indev_init()),
// step for step, with one change: LVGL's draw buffers are `draw_rows` rows high
// instead of 50. LVGL draws the screen one buffer-high band at a time, and the
// header's glass can blur only what its band holds, so one band must hold the
// whole capsule (rows 8-87; aura::ui::tokens::DEVICE_DRAW_ROWS).
//
// The SPI driver cannot send from PSRAM: it copies each transfer into a
// temporary buffer in internal RAM first, as large as the transfer. The BSP
// allows a whole frame per transfer, so a 100-row band took 96 KB of internal
// RAM, and on the first flash (2026-11-14) the board ran out of it at boot.
// Transfers are therefore kPanelTransferRows rows; the panel IO queues three,
// so at most about 17 KB is in flight. (psram_dma_direct, the DMA reading PSRAM
// itself, underflows at the panel's 40 MHz: tried on the board, frames failed.)
//
// The panel itself starts as the BSP's bsp_display_new() starts it, except that
// it stays dark: the BSP's sequence lit it at full brightness with whatever its
// memory held, and LVGL then drew its default near-white screen until the UI was
// built, two seconds of white at every start (the owner, 2026-11-14). Here the
// panel lights only once AURA's own first screen is in its memory
// (aura_board_display_reveal).
#include "aura_display.h"

#include <stdint.h>
#include <string.h>

// In the BSP's own order: its display header needs esp_err_t declared first.
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "driver/spi_master.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_lv_adapter.h"
#include "bsp/esp32_s3_touch_amoled_2_16.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "aura_display";

// Rows per SPI transfer (960 bytes each); see the top of this file.
enum { kPanelTransferRows = 6 };

// The BSP 2.0.1's start-up sequence for the CO5300 (esp32_s3_touch_amoled_2_16.c,
// lcd_init_cmds), with two changes: the brightness (51h) starts at 0, not FFh,
// and the display-on command (29h, and the 600 ms after it) is left out, so the
// panel stays dark until aura_board_display_reveal() sends it.
static const co5300_lcd_init_cmd_t kPanelStart[] = {
    {0x11, (uint8_t[]){0x00}, 0, 600},  // sleep out
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0x00}, 1, 0},  // brightness 0 (the BSP: FFh)
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x36, (uint8_t[]){0xA0}, 1, 0},
};

// bsp_display_new() step for step, with kPanelStart and without its display-on.
// The BSP's own panel handle stays empty, so its brightness functions are not
// used: aura_board_display_set_brightness() replaces them.
static esp_err_t new_dark_panel(int max_transfer_sz, esp_lcd_panel_handle_t* ret_panel,
                                esp_lcd_panel_io_handle_t* ret_io) {
    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(BSP_LCD_PCLK, BSP_LCD_DATA0, BSP_LCD_DATA1,
                                                              BSP_LCD_DATA2, BSP_LCD_DATA3, max_transfer_sz);
    esp_err_t result = spi_bus_initialize(BSP_LCD_SPI_NUM, &bus, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) return result;
    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, NULL, NULL);
    io_config.trans_queue_depth = 3;
    static const co5300_vendor_config_t vendor = {
        .init_cmds = kPanelStart,
        .init_cmds_size = sizeof(kPanelStart) / sizeof(kPanelStart[0]),
        .flags = {.use_qspi_interface = 1},
    };
    esp_lcd_panel_io_handle_t io = NULL;
    result = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, &io);
    if (result != ESP_OK) return result;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .vendor_config = (void*)&vendor,
    };
    esp_lcd_panel_handle_t panel = NULL;
    result = esp_lcd_new_panel_co5300(io, &panel_config, &panel);
    if (result != ESP_OK) return result;
    if ((result = esp_lcd_panel_reset(panel)) != ESP_OK || (result = esp_lcd_panel_init(panel)) != ESP_OK) {
        return result;
    }
    *ret_panel = panel;
    *ret_io = io;
    return ESP_OK;
}

// LVGL's pixels live in PSRAM, which the SPI driver cannot send: for every
// transfer it allocated a buffer of internal RAM as large as the transfer, and
// with the microphone open or a TLS connection starting there was none left
// («Draw bitmap failed: ESP_ERR_NO_MEM», 188 times in a minute, the screen
// standing still; 2026-11-16). The pixels now go through two strips of internal
// RAM taken once at start, kPanelTransferRows rows each: one is filled while the
// other is sent. The panel IO waits for the transfers in flight before each
// command it sends, so a strip is free again once the draw after its own has
// begun. Each draw is copied whole before it returns, so LVGL may reuse its
// buffer however early the panel IO reports a transfer done.
static uint8_t* s_bounce[2] = {NULL, NULL};
static size_t s_bounce_bytes = 0;
static int s_bounce_next = 0;

static esp_err_t bounce_draw_bitmap(lv_display_t* disp, esp_lcd_panel_handle_t panel, int x_start, int y_start,
                                    int x_end, int y_end, const void* color_map, void* user_ctx) {
    (void)disp;
    (void)user_ctx;
    const size_t row_bytes = (size_t)(x_end - x_start) * (BSP_LCD_BITS_PER_PIXEL / 8);
    // Whole rows, an even number: the panel takes areas from an even row.
    const int rows_per_strip = row_bytes == 0 ? 0 : (int)(s_bounce_bytes / row_bytes) & ~1;
    if (s_bounce[0] == NULL || rows_per_strip < 2 || esp_ptr_dma_capable(color_map)) {
        return esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, color_map);
    }
    const uint8_t* source = (const uint8_t*)color_map;
    for (int y = y_start; y < y_end; y += rows_per_strip) {
        const int rows = y_end - y < rows_per_strip ? y_end - y : rows_per_strip;
        uint8_t* strip = s_bounce[s_bounce_next];
        s_bounce_next ^= 1;
        memcpy(strip, source, (size_t)rows * row_bytes);
        const esp_err_t result = esp_lcd_panel_draw_bitmap(panel, x_start, y, x_end, y + rows, strip);
        if (result != ESP_OK) return result;
        source += (size_t)rows * row_bytes;
    }
    return ESP_OK;
}

// The CO5300 takes areas that start on even and end on odd coordinates (as the
// BSP's rounder does).
static void round_to_panel(lv_event_t* event) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(event);
    const uint16_t x1 = (uint16_t)area->x1;
    const uint16_t x2 = (uint16_t)area->x2;
    const uint16_t y1 = (uint16_t)area->y1;
    const uint16_t y2 = (uint16_t)area->y2;
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

// The screen's rotation (the owner's request on the first flash, 2026-11-14).
// The adapter does not rotate an "OTHER" panel, so the panel itself turns
// (MADCTL, as the BSP's bsp_display_rotation_set() writes it) and each touch
// is turned back into the content's coordinates.
static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static lv_indev_read_cb_t s_touch_read = NULL;
static volatile int s_rotation = 0;
// Lit by aura_board_display_reveal(); until then a brightness waits for it.
static bool s_revealed = false;
static int s_brightness_percent = 100;

// MADCTL per rotation: the start-up sequence's 0xA0 upright, then the BSP's
// own values (0x00, 0x60, 0xC0, 0xA0 for its 0/90/180/270). 0x60 is the half
// turn; which of 0xC0 and 0x00 puts the content's bottom on the right edge
// needs the board (NEEDS HARDWARE).
static const uint8_t kMadctl[4] = {0xA0, 0xC0, 0x60, 0x00};

// Two fingers (the owner's zoom in a photo, 2026-11-18). The CST9217 reports
// them, but its component reads one (its CST9217_MAX_TOUCH_POINTS is 1): this
// read takes the same register and layout, 5 bytes for the first finger, the
// finger count at 5 and 0xAB at 6, then 5 bytes for the second finger, and
// leaves them in the component's data, so the touch library still does the
// mirroring and swapping as configured.
enum { kTouchDataRegister = 0xD000, kTouchAck = 0xAB, kTouchPressed = 0x06, kTouchBytes = 15 };
static int32_t s_finger_x[2];
static int32_t s_finger_y[2];
static int s_fingers = 0;

static esp_err_t read_two_fingers(esp_lcd_touch_handle_t touch, esp_lcd_touch_point_data_t* points, uint8_t* count,
                                  uint8_t max_count, void* context) {
    (void)context;
    uint8_t data[kTouchBytes] = {0};
    const uint8_t low = kTouchDataRegister & 0xFF;
    esp_err_t result = esp_lcd_panel_io_tx_param(touch->io, kTouchDataRegister >> 8, &low, 1);
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2));
        result = esp_lcd_panel_io_rx_param(touch->io, -1, data, sizeof(data));
    }
    if (result == ESP_OK && data[6] == kTouchAck) {
        const uint8_t reported = data[5] & 0x7F;
        portENTER_CRITICAL(&touch->data.lock);
        touch->data.points = 0;
        for (uint8_t finger = 0; finger < 2 && finger < reported; ++finger) {
            const uint8_t* place = &data[finger * 5 + (finger ? 2 : 0)];
            if ((place[0] & 0x0F) != kTouchPressed) continue;
            const uint8_t index = touch->data.points++;
            touch->data.coords[index].x = (uint16_t)((place[1] << 4) | (place[3] >> 4));
            touch->data.coords[index].y = (uint16_t)((place[2] << 4) | (place[3] & 0x0F));
        }
        portEXIT_CRITICAL(&touch->data.lock);
    }
    // A read that failed keeps the fingers last seen, as the component does.
    *count = 0;
    const esp_err_t coordinates = esp_lcd_touch_get_data(touch, points, count, max_count);
    s_fingers = *count > 2 ? 2 : *count;
    for (int finger = 0; finger < s_fingers; ++finger) {
        s_finger_x[finger] = points[finger].x;
        s_finger_y[finger] = points[finger].y;
    }
    return coordinates;
}

int aura_board_touch_points(int32_t x[2], int32_t y[2]) {
    for (int finger = 0; finger < s_fingers; ++finger) {
        x[finger] = s_finger_x[finger];
        y[finger] = s_finger_y[finger];
        aura_board_rotate_touch(s_rotation, BSP_LCD_H_RES, &x[finger], &y[finger]);
    }
    return s_fingers;
}

static void rotated_touch_read(lv_indev_t* indev, lv_indev_data_t* data) {
    s_touch_read(indev, data);
    int32_t x = data->point.x;
    int32_t y = data->point.y;
    aura_board_rotate_touch(s_rotation, BSP_LCD_H_RES, &x, &y);
    data->point.x = x;
    data->point.y = y;
}

// The touch controller, as the BSP's bsp_touch_new() starts it, but its I2C
// channel is made once and kept: a controller that does not answer at the
// start is asked again later (aura_board_touch_retry) without a new channel
// each time. AURA stopped with no screen when it did not answer (F-4 of
// 2026-11-19). Its reset is GPIO40, apart from the panel's GPIO39, so asking
// again never resets the panel.
static lv_display_t* s_display = NULL;
static esp_lcd_panel_io_handle_t s_touch_io = NULL;
static bool s_touch_ready = false;
static unsigned s_touch_swap_xy = 0;
static unsigned s_touch_mirror_x = 0;
static unsigned s_touch_mirror_y = 0;

static bool start_touch(void) {
    if (s_touch_ready) return true;
    if (s_display == NULL) return false;
    if (s_touch_io == NULL) {
        if (bsp_i2c_init() != ESP_OK) return false;
        esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
        io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;
        if (esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &io_config, &s_touch_io) != ESP_OK) {
            s_touch_io = NULL;
            return false;
        }
    }
    const esp_lcd_touch_config_t touch_config = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_LCD_TOUCH_RST,
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = s_touch_swap_xy, .mirror_x = s_touch_mirror_x, .mirror_y = s_touch_mirror_y},
    };
    esp_lcd_touch_handle_t touch = NULL;
    if (esp_lcd_touch_new_i2c_cst9217(s_touch_io, &touch_config, &touch) != ESP_OK || touch == NULL) return false;
    esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_display, touch);
    touch_cfg.callbacks.custom_touch_read = read_two_fingers;
    lv_indev_t* touch_input = esp_lv_adapter_register_touch(&touch_cfg);
    if (touch_input == NULL) {
        esp_lcd_touch_del(touch);
        return false;
    }
    // Touches reach LVGL through the rotation (aura_board_display_set_rotation).
    s_touch_read = lv_indev_get_read_cb(touch_input);
    if (s_touch_read != NULL) lv_indev_set_read_cb(touch_input, rotated_touch_read);
    s_touch_ready = true;
    return true;
}

bool aura_board_touch_ready(void) { return s_touch_ready; }

bool aura_board_touch_retry(void) {
    if (s_touch_ready) return true;
    if (!aura_board_display_lock(-1)) return false;
    const bool ready = start_touch();
    aura_board_display_unlock();
    if (ready) ESP_LOGI(TAG, "the touch controller answered");
    return ready;
}

void aura_board_display_set_rotation(int rotation) {
    if (s_panel_io == NULL || rotation < 0 || rotation > 3 || rotation == s_rotation) return;
    const uint8_t madctl = kMadctl[rotation];
    // A QSPI write (0x02) of command 0x36, the memory access control.
    const int command = (0x02 << 24) | (0x36 << 8);
    if (esp_lcd_panel_io_tx_param(s_panel_io, command, &madctl, 1) != ESP_OK) return;
    s_rotation = rotation;
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    lv_obj_invalidate(lv_layer_sys());
    ESP_LOGI(TAG, "rotation %d (MADCTL 0x%02X)", rotation, madctl);
}

bool aura_board_display_lock(int32_t timeout_ms) { return esp_lv_adapter_lock(timeout_ms) == ESP_OK; }

void aura_board_display_unlock(void) { esp_lv_adapter_unlock(); }

bool aura_board_display_draw(int x1, int y1, int x2, int y2, const void* pixels) {
    return s_panel != NULL && pixels != NULL && esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, pixels) == ESP_OK;
}

// As the BSP's bsp_display_brightness_set(): a QSPI write (0x02) of 51h.
static bool send_brightness(int percent) {
    const uint8_t level = (uint8_t)(percent * 255 / 100);
    const int command = (0x02 << 24) | (0x51 << 8);
    return esp_lcd_panel_io_tx_param(s_panel_io, command, &level, 1) == ESP_OK;
}

bool aura_board_display_set_brightness(int percent) {
    if (s_panel_io == NULL || percent < 0 || percent > 100) return false;
    s_brightness_percent = percent;
    return !s_revealed || send_brightness(percent);
}

void aura_board_display_reveal(int brightness_percent) {
    if (s_panel == NULL || !aura_board_display_lock(-1)) return;
    if (!s_revealed) {
        // The whole screen drawn and sent now. The panel IO sends a command only
        // after the pixels queued before it, so the panel lights on this frame.
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(NULL);
        if (esp_lcd_panel_disp_on_off(s_panel, true) == ESP_OK) s_revealed = true;
        else ESP_LOGE(TAG, "the panel did not switch on");
    }
    aura_board_display_set_brightness(brightness_percent);
    aura_board_display_unlock();
}

lv_display_t* aura_board_display_start(int draw_rows) {
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {
            .swap_xy = 1,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };
    if (draw_rows <= 0 || draw_rows > BSP_LCD_V_RES) {
        ESP_LOGE(TAG, "invalid draw rows %d", draw_rows);
        return NULL;
    }
    // The LVGL task runs the touch callbacks, which build whole screens: 16 KiB
    // instead of the adapter's 8 KiB (internal RAM, measured on the first boot).
    cfg.lv_adapter_cfg.task_stack_size = 16 * 1024;
    // On the second core: the audio task and the radio's decoder run on the
    // first at priorities 5 and 4, and LVGL's task (priority 6) drawing a 70-140 ms
    // frame there starved them; the radio stuttered while the screen moved
    // (the first flash, 2026-11-14). Synapse's worker draws on the first core,
    // below them all (app_main's start_frame_worker()).
    cfg.lv_adapter_cfg.task_core_id = 1;
    if (esp_lv_adapter_init(&cfg.lv_adapter_cfg) != ESP_OK) return NULL;

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    if (new_dark_panel(BSP_LCD_H_RES * kPanelTransferRows * BSP_LCD_BITS_PER_PIXEL / 8, &panel, &io) != ESP_OK) {
        return NULL;
    }
    // The two strips LVGL's pixels go through (bounce_draw_bitmap), before the
    // services take the internal RAM.
    s_bounce_bytes = BSP_LCD_H_RES * kPanelTransferRows * BSP_LCD_BITS_PER_PIXEL / 8;
    for (int i = 0; i < 2; ++i) s_bounce[i] = heap_caps_malloc(s_bounce_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_bounce[0] == NULL || s_bounce[1] == NULL) {
        heap_caps_free(s_bounce[0]);
        heap_caps_free(s_bounce[1]);
        s_bounce[0] = s_bounce[1] = NULL;
        ESP_LOGW(TAG, "no internal RAM for the panel's strips: LVGL's pixels go through the SPI driver's buffers");
    }

    const esp_lv_adapter_display_config_t display_cfg = {
        .panel = panel,
        .panel_io = io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = cfg.rotation,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            .buffer_height = (uint16_t)draw_rows,
            .use_psram = true,
            .enable_ppa_accel = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = cfg.tear_avoid_mode,
    };
    lv_display_t* display = esp_lv_adapter_register_display(&display_cfg);
    if (display == NULL) return NULL;
    lv_display_add_event_cb(display, round_to_panel, LV_EVENT_INVALIDATE_AREA, NULL);
    if (s_bounce[0] != NULL) {
        const esp_lv_adapter_draw_bitmap_callbacks_t draw = {.custom_draw_bitmap = bounce_draw_bitmap};
        if (esp_lv_adapter_set_draw_bitmap_callbacks(display, &draw, NULL) != ESP_OK) {
            ESP_LOGW(TAG, "the panel's strips were not taken: LVGL's pixels go through the SPI driver's buffers");
        }
    }

    s_display = display;
    s_touch_swap_xy = cfg.touch_flags.swap_xy;
    s_touch_mirror_x = cfg.touch_flags.mirror_x;
    s_touch_mirror_y = cfg.touch_flags.mirror_y;
    if (!start_touch()) ESP_LOGW(TAG, "the touch controller did not answer: AURA starts without it");
    s_panel_io = io;
    s_panel = panel;

    // Black, not the default theme's near-white, should LVGL draw before AURA's
    // first screen (the panel is still dark then, but its memory keeps it).
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    if (esp_lv_adapter_start() != ESP_OK) return NULL;
    ESP_LOGI(TAG, "LVGL draws %d rows at a time", draw_rows);
    return display;
}

// The display bring-up of the Waveshare ESP32-S3-Touch-AMOLED-2.16 BSP 2.0.1
// (bsp_display_start(), bsp_display_lcd_init() and bsp_display_indev_init()),
// step for step, with one change: LVGL's draw buffers are `draw_rows` rows high
// instead of 50. LVGL draws the screen one buffer-high band at a time, and the
// header's glass can blur only what its band holds, so one band must hold the
// whole capsule (rows 8-87; aura::ui::tokens::DEVICE_DRAW_ROWS).
#include "aura_display.h"

#include <stdint.h>

// In the BSP's own order: its display header needs esp_err_t declared first.
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lv_adapter.h"
#include "bsp/esp32_s3_touch_amoled_2_16.h"
#include "bsp/display.h"
#include "bsp/touch.h"

static const char* TAG = "aura_display";

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
    if (esp_lv_adapter_init(&cfg.lv_adapter_cfg) != ESP_OK) return NULL;

    const bsp_display_config_t panel_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * BSP_LCD_BITS_PER_PIXEL / 8,
    };
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    if (bsp_display_new(&panel_cfg, &panel, &io) != ESP_OK) return NULL;

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

    esp_lcd_touch_handle_t touch = NULL;
    if (bsp_touch_new(&cfg, &touch) != ESP_OK || touch == NULL) return NULL;
    const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch);
    if (esp_lv_adapter_register_touch(&touch_cfg) == NULL) return NULL;

    if (bsp_display_brightness_init() != ESP_OK) return NULL;
    if (esp_lv_adapter_start() != ESP_OK) return NULL;
    ESP_LOGI(TAG, "LVGL draws %d rows at a time", draw_rows);
    return display;
}

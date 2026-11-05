#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the panel, touch and LVGL as the BSP's bsp_display_start() does, except
// that LVGL draws `draw_rows` rows at a time into its two PSRAM draw buffers
// (the BSP fixes 50). Returns NULL if any step fails.
lv_display_t* aura_board_display_start(int draw_rows);

#ifdef __cplusplus
}
#endif

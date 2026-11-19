#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the panel, touch and LVGL as the BSP's bsp_display_start() does, except
// that LVGL draws `draw_rows` rows at a time into its two PSRAM draw buffers
// (the BSP fixes 50), and the panel stays dark until aura_board_display_reveal().
// Returns NULL if any step fails.
lv_display_t* aura_board_display_start(int draw_rows);

// Draws the whole screen now and then lights the panel at `brightness_percent`:
// the first thing it shows is AURA's own screen, never what its memory held nor
// LVGL's default near-white screen (two seconds of white at every start, the
// owner, 2026-11-14). Takes the display lock itself; later calls only set the
// brightness.
void aura_board_display_reveal(int brightness_percent);

// The panel's brightness, 0-100 %; before aura_board_display_reveal() it is
// kept for the reveal. Under the display lock, as every other panel command.
bool aura_board_display_set_brightness(int percent);

// LVGL's lock, waited for up to `timeout_ms` (negative: for ever); true when
// taken. Use these, never bsp_display_lock(): the BSP 2.0.1 returns
// esp_lv_adapter_lock()'s esp_err_t as a bool, so it says false exactly when
// the lock was taken, and it passes 0 on as "do not wait" although its header
// promises "block indefinitely" (found on the first flash, 2026-11-14).
bool aura_board_display_lock(int32_t timeout_ms);
void aura_board_display_unlock(void);

// Pixels to the panel straight, past LVGL: columns [x1, x2) and rows [y1, y2)
// of the screen as LVGL lays it out (the panel turns with it), RGB565 with the
// high byte first, in memory the SPI's DMA reads (internal RAM). Windows start
// on even and end on odd coordinates. Under the display lock. The memory may
// be written again once the next panel command has gone: the panel IO sends
// one only after the pixels queued before it. Synapse's strips (app_main).
bool aura_board_display_draw(int x1, int y1, int x2, int y2, const void* pixels);

// The screen's rotation, aura::ScreenRotation's order: 0 upright (as built), 1
// the right edge, 2 the top, 3 the left edge is the content's bottom. The panel
// turns, touches follow, and the whole screen is drawn again. Under the
// display lock.
void aura_board_display_set_rotation(int rotation);
// A touch at (*x, *y) on the upright panel of side `size`, to the point of the
// rotated content under it (aura::rotate_touch_point).
void aura_board_rotate_touch(int rotation, int size, int32_t* x, int32_t* y);
// The fingers on the glass at the last read (0 to 2), in the rotated content's
// coordinates: x[i], y[i] for each. Under the display lock.
int aura_board_touch_points(int32_t x[2], int32_t y[2]);

#ifdef __cplusplus
}
#endif

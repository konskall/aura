// The C side of the screen's rotation (aura_display.c) uses the shared,
// unit-tested touch mapping.
#include "aura_display.h"

#include "aura/display_power.hpp"

extern "C" void aura_board_rotate_touch(int rotation, int size, int32_t* x, int32_t* y) {
    if (rotation < 0 || rotation > 3) return;
    int px = static_cast<int>(*x);
    int py = static_cast<int>(*y);
    aura::rotate_touch_point(static_cast<aura::ScreenRotation>(rotation), size, px, py);
    *x = px;
    *y = py;
}

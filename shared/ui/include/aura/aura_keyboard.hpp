#pragma once

#include "lvgl.h"

namespace aura::ui {

enum class KeyboardProfile {
    Text,      // Greek first, Latin, numbers/symbols and the Greek accent key
    Password,  // Latin, numbers and ASCII symbols only (WPA passphrases are ASCII)
};

// Creates the shared on-screen keyboard bound to `textarea`. Layout switching,
// one-shot/locked shift and the Greek tonos/diaeresis key are handled here;
// character entry, backspace and OK (LV_EVENT_READY on the text area) use
// LVGL's keyboard behaviour. Pressed keys show an enlarged pop-over because a
// key on the 2.16" panel is only about 3.5 mm wide.
lv_obj_t* create_keyboard(lv_obj_t* parent, lv_obj_t* textarea, KeyboardProfile profile);

}  // namespace aura::ui

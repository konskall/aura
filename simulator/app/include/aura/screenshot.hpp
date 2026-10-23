#pragma once

#include <string>

#include "lvgl.h"

namespace aura::simulator {
bool save_display_bmp(lv_display_t* display, const std::string& path);
}


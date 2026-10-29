#pragma once

#include "aura/aura_icons.hpp"
#include "lvgl.h"

// Material Symbols Rounded (Apache 2.0). nullptr = use the Lucide asset.
const lv_image_dsc_t* aura_icon_material(aura::ui::IconRole role) noexcept;

#pragma once

#include <cstdint>

#include "aura/appearance.hpp"
#include "lvgl.h"

namespace aura::ui {

// One appearance's colours. The names are the v12 tokens; their meaning is the
// same in every appearance (blue = primary, ice/lilac/magenta = listening,
// thinking, speaking, mint/amber/coral = success, warning, danger).
struct Palette {
    std::uint32_t canvas;
    std::uint32_t surface;
    std::uint32_t surface_high;
    std::uint32_t surface_glass;
    std::uint32_t separator;
    std::uint32_t danger_surface;
    std::uint32_t text;
    std::uint32_t text_secondary;
    std::uint32_t text_tertiary;
    std::uint32_t blue;
    std::uint32_t primary_end;  // second stop of a gradient primary; == blue when flat
    std::uint32_t on_primary;   // label colour on a primary button
    std::uint32_t indigo;
    std::uint32_t ice;
    std::uint32_t lilac;
    std::uint32_t magenta;
    std::uint32_t mint;
    std::uint32_t amber;
    std::uint32_t pink;
    std::uint32_t coral;
    std::uint32_t toggle_on;  // a switch's track when on; its knob is the text colour
};

// Fonts for the type roles of aura_theme.hpp. `display` and `clock` keep
// Halo's 72 and 96 px metrics in every look (the shared screens are laid out
// for them); the signature screens use `numerals_small`, `numerals_large` and
// `hero`. `hero` may be null: the large numerals are used instead.
struct FontSet {
    const lv_font_t* caption;
    const lv_font_t* body;
    const lv_font_t* headline;
    const lv_font_t* title;
    const lv_font_t* display;
    const lv_font_t* clock;
    const lv_font_t* numerals_small;
    const lv_font_t* numerals_large;
    const lv_font_t* hero;
};

enum class RowStyle : std::uint8_t { Platter, Glass, Hairline };
enum class WellStyle : std::uint8_t { TintedCircle, HairlineCircle, Chip, Squircle };

// How the shared components draw themselves in one appearance.
struct Metrics {
    RowStyle rows;
    WellStyle wells;
    bool gradient_primary;
    bool outline_secondary;
    int platter_radius;
    int button_radius;
    int hero_center_y;  // the presence centre on Home and in the assistant states
};

const Palette& palette_for(Appearance appearance) noexcept;
const FontSet& fonts_for(Appearance appearance) noexcept;
const Metrics& metrics_for(Appearance appearance) noexcept;

// UI thread only: copies the palette into tokens:: and selects the fonts,
// icons and metrics the components read.
void set_active_appearance(Appearance appearance) noexcept;
Appearance active_appearance() noexcept;
const Metrics& active_metrics() noexcept;

// WCAG 2.x contrast ratio of two opaque sRGB colours (1.0 to 21.0).
double contrast_ratio(std::uint32_t foreground, std::uint32_t background) noexcept;

}  // namespace aura::ui

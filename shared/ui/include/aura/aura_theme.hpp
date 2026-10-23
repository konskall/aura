#pragma once

#include <cstdint>

#include "lvgl.h"

namespace aura::ui {

// v12 "Halo" tokens (docs/design/AURA_V12_HALO_DESIGN.md). The panel is a
// 2.16" 480x480 AMOLED at ~12.4 px/mm, so sizes follow watch-class
// legibility: text >= 22 px, controls >= 64 px, primary targets 80+ px.
namespace tokens {
struct Rect {
    int x;
    int y;
    int width;
    int height;
};

// True black: AMOLED pixels are off, and content floats on graphite platters.
constexpr std::uint32_t canvas = 0x000000;
constexpr std::uint32_t surface = 0x121822;
constexpr std::uint32_t surface_high = 0x1B2330;
constexpr std::uint32_t surface_glass = 0x222C3B;
constexpr std::uint32_t separator = 0x2B3546;
constexpr std::uint32_t danger_surface = 0x3A141B;
constexpr std::uint32_t text = 0xF5F8FC;
constexpr std::uint32_t text_secondary = 0xA9B5C7;
constexpr std::uint32_t text_tertiary = 0x748197;
constexpr std::uint32_t ice = 0x25D6FF;
constexpr std::uint32_t blue = 0x178BFF;
constexpr std::uint32_t indigo = 0x4C52FF;
constexpr std::uint32_t lilac = 0x8A45FF;
constexpr std::uint32_t magenta = 0xDE58FB;
constexpr std::uint32_t mint = 0x35ECC3;
constexpr std::uint32_t amber = 0xFFCB3F;
constexpr std::uint32_t pink = 0xFF579E;
constexpr std::uint32_t coral = 0xFF6B78;

constexpr int display_width = 480;
constexpr int display_height = 480;
constexpr int GUTTER = 24;
constexpr int CONTENT_WIDTH = display_width - GUTTER * 2;
// Home keeps its status glyphs in this band; inner screens carry the time in
// their header instead.
constexpr int STATUS_BAR_HEIGHT = 64;
constexpr int HEADER_HEIGHT = 96;
constexpr int ROW_HEIGHT = 88;
constexpr int ROW_PITCH = 96;
constexpr int BUTTON_HEIGHT = 80;
constexpr int CHIP_HEIGHT = 64;
constexpr int HEADER_BUTTON = 64;
constexpr Rect CONTENT_SAFE_RECT{GUTTER, HEADER_HEIGHT, CONTENT_WIDTH, display_height - HEADER_HEIGHT};
// The orb and its halo ring sit at the same place on Home and in every
// assistant state, so starting to talk never moves the presence.
constexpr Rect HERO_RECT{140, 160, 200, 200};
constexpr Rect MERCURY_RECT{152, 172, 176, 176};
constexpr int HALO_RADIUS = 96;
constexpr Rect BOTTOM_ACTION_SAFE_RECT{GUTTER, 376, CONTENT_WIDTH, 88};

constexpr int radius_small = 14;
constexpr int radius_medium = 24;
constexpr int radius_large = 28;
constexpr int touch = 64;
constexpr int micro_ms = 120;
constexpr int content_ms = 200;
constexpr int sheet_ms = 300;
}  // namespace tokens

enum class ButtonTone { Primary, Secondary, Quiet, Danger };

lv_color_t color(std::uint32_t value);
// Type scale: IBM Plex Sans (Light subsets for numbers), Space Grotesk wordmark.
const lv_font_t* font_caption();   // Regular 22: metadata, hints
const lv_font_t* font_body();      // Regular 26: reading text
const lv_font_t* font_headline();  // Medium 28: row titles, buttons, keys
const lv_font_t* font_title();     // SemiBold 32: screen and state titles
const lv_font_t* font_display();   // Light 72: numbers only (falls back to title)
const lv_font_t* font_clock();     // Light 96: digits and ':' only
const lv_font_t* font_brand();     // Space Grotesk 26: "AURA" only

void reset(lv_obj_t* object);
// A borderless graphite platter; `selected` adds an accent hairline.
void style_platter(lv_obj_t* object, std::uint32_t accent = tokens::blue,
                   int radius = tokens::radius_large, bool selected = false);
void style_scrollbar(lv_obj_t* object);
lv_obj_t* make_label(lv_obj_t* parent, const char* text,
                     std::uint32_t text_color = tokens::text,
                     const lv_font_t* font = nullptr);
// An 80 px pill when the caller keeps the default height.
lv_obj_t* make_button(lv_obj_t* parent, const char* text, ButtonTone tone,
                      const lv_font_t* font = nullptr);
lv_obj_t* make_tag(lv_obj_t* parent, const char* text, std::uint32_t accent);
void set_button_disabled(lv_obj_t* button, bool disabled);

}  // namespace aura::ui

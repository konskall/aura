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

// Colours of the active appearance (aura_appearance.hpp). They start as Halo,
// the v12 palette, and change only through set_active_appearance() on the UI
// thread. True black: AMOLED pixels are off, and content floats on platters.
inline std::uint32_t canvas = 0x000000;
inline std::uint32_t surface = 0x121822;
inline std::uint32_t surface_high = 0x1B2330;
inline std::uint32_t surface_glass = 0x222C3B;
inline std::uint32_t separator = 0x2B3546;
inline std::uint32_t danger_surface = 0x3A141B;
inline std::uint32_t text = 0xF5F8FC;
inline std::uint32_t text_secondary = 0xA9B5C7;
inline std::uint32_t text_tertiary = 0x748197;
inline std::uint32_t ice = 0x25D6FF;
inline std::uint32_t blue = 0x178BFF;
inline std::uint32_t primary_end = 0x178BFF;  // second stop of a gradient primary
inline std::uint32_t on_primary = 0xF5F8FC;   // label colour on a primary button
inline std::uint32_t indigo = 0x4C52FF;
inline std::uint32_t lilac = 0x8A45FF;
inline std::uint32_t magenta = 0xDE58FB;
inline std::uint32_t mint = 0x35ECC3;
inline std::uint32_t amber = 0xFFCB3F;
inline std::uint32_t pink = 0xFF579E;
inline std::uint32_t coral = 0xFF6B78;
inline std::uint32_t toggle_on = 0x178BFF;  // a switch's track when on

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
// Signature screens only (aura_ui_<look>.inl): their larger numerals.
const lv_font_t* font_numerals_small();
const lv_font_t* font_numerals_large();
const lv_font_t* font_hero();      // the largest numerals (Home clock); the large numerals unless set
const lv_font_t* font_brand();     // Space Grotesk 26: "AURA" only
// The text roles above come from the active appearance's font set.

// Σέλας's glass: a vertical gradient with a faint light edge (11 %).
inline constexpr std::uint32_t kGlassTop = 0x1A1F34;
inline constexpr std::uint32_t kGlassBottom = 0x0D1020;
inline constexpr std::uint32_t kGlassEdge = 0xAAB4FF;
inline constexpr lv_opa_t kGlassEdgeOpacity = 28;

void reset(lv_obj_t* object);
// Σέλας's glass on any surface: the gradient, its light edge and `radius`.
void style_glass(lv_obj_t* object, int radius);
// A soft light field (Σέλας): one shared 32 x 32 A8 disc scaled to `radius`
// and tinted. Only for regions that do not animate; never a target.
lv_obj_t* light_field(lv_obj_t* parent, int center_x, int center_y, int radius, std::uint32_t tint,
                      lv_opa_t opacity);
// Whether `object` is a light field (an image of the shared disc).
bool is_light_field(lv_obj_t* object);
// A borderless graphite platter; `selected` adds an accent hairline.
void style_platter(lv_obj_t* object, std::uint32_t accent = tokens::blue,
                   int radius = tokens::radius_large, bool selected = false);
// A list row: a platter in Halo and Graphite, glass in Aurora, an open line
// with one hairline underneath in Marble.
void style_row(lv_obj_t* object, std::uint32_t accent = tokens::blue, bool selected = false);
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

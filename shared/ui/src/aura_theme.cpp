#include "aura/aura_theme.hpp"

#include <cstdint>

#include "aura/aura_appearance.hpp"

LV_FONT_DECLARE(aura_font_space_medium_26);

namespace aura::ui {

namespace {
const FontSet& fonts() { return fonts_for(active_appearance()); }

// `rgb` at `percent` brightness (for pressed states).
std::uint32_t shade(std::uint32_t rgb, int percent) {
    const auto channel = [&](int shift) {
        return ((static_cast<int>((rgb >> shift) & 0xFFU) * percent / 100) & 0xFF) << shift;
    };
    return static_cast<std::uint32_t>(channel(16) | channel(8) | channel(0));
}
}  // namespace

lv_color_t color(std::uint32_t value) { return lv_color_hex(value); }
const lv_font_t* font_caption() { return fonts().caption; }
const lv_font_t* font_body() { return fonts().body; }
const lv_font_t* font_headline() { return fonts().headline; }
const lv_font_t* font_title() { return fonts().title; }
const lv_font_t* font_display() { return fonts().display; }
const lv_font_t* font_clock() { return fonts().clock; }
const lv_font_t* font_numerals_small() { return fonts().numerals_small; }
const lv_font_t* font_numerals_large() { return fonts().numerals_large; }
const lv_font_t* font_hero() { return fonts().hero ? fonts().hero : fonts().numerals_large; }
const lv_font_t* font_brand() { return &aura_font_space_medium_26; }

void reset(lv_obj_t* object) {
    lv_obj_remove_style_all(object);
    lv_obj_set_clickable(object, false);
    lv_obj_set_scrollable(object, false);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

// No shadows anywhere: they are the most expensive LVGL primitive on the
// ESP32-S3 and read as dated on true black.
void style_platter(lv_obj_t* object, std::uint32_t accent, int radius, bool selected) {
    const auto rows = active_metrics().rows;
    lv_obj_set_style_bg_color(object, color(selected ? tokens::surface_glass : tokens::surface), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, selected ? 2 : 0, 0);
    lv_obj_set_style_border_color(object, color(accent), 0);
    lv_obj_set_style_border_opa(object, LV_OPA_70, 0);
    if (rows == RowStyle::Hairline) {
        // Marble: hairline outlines instead of filled boxes.
        lv_obj_set_style_bg_opa(object, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(object, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(object, color(selected ? accent : tokens::separator), 0);
        lv_obj_set_style_border_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_STATE_PRESSED);
    } else if (rows == RowStyle::Glass) {
        // Aurora: dark glass, lighter at the top, with a faint light edge.
        lv_obj_set_style_bg_color(object, color(selected ? tokens::surface_glass : tokens::surface_high), 0);
        lv_obj_set_style_bg_grad_color(object, color(tokens::surface), 0);
        lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(object, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(object, color(selected ? accent : 0xAAB4FF), 0);
        lv_obj_set_style_border_opa(object, selected ? LV_OPA_70 : 28, 0);
    }
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_pad_all(object, 16, 0);
    lv_obj_set_style_bg_color(object, color(tokens::surface_high), LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(object, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(object, -3, LV_STATE_PRESSED);
}

void style_row(lv_obj_t* object, std::uint32_t accent, bool selected) {
    style_platter(object, accent, active_metrics().platter_radius, selected);
    if (active_metrics().rows != RowStyle::Hairline) return;
    // Marble rows are open lines: one hairline underneath, no box.
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_bg_color(object, color(tokens::surface_glass), 0);
    lv_obj_set_style_bg_opa(object, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(object, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, color(tokens::separator), 0);
}

void style_scrollbar(lv_obj_t* object) {
    lv_obj_set_style_width(object, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(object, color(tokens::text_tertiary), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(object, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(object, 4, LV_PART_SCROLLBAR);
}

lv_obj_t* make_label(lv_obj_t* parent, const char* text, std::uint32_t text_color,
                     const lv_font_t* font) {
    auto* value = lv_label_create(parent);
    lv_label_set_text(value, text ? text : "");
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(value, color(text_color), 0);
    lv_obj_set_style_text_font(value, font ? font : font_body(), 0);
    lv_obj_set_style_text_line_space(value, 4, 0);
    lv_obj_set_clickable(value, false);
    return value;
}

lv_obj_t* make_button(lv_obj_t* parent, const char* text, ButtonTone tone, const lv_font_t* font) {
    auto* value = lv_button_create(parent);
    std::uint32_t background = tokens::surface_high;
    std::uint32_t foreground = tokens::text;
    std::uint32_t pressed = tokens::surface_glass;
    const auto& metrics = active_metrics();
    if (tone == ButtonTone::Primary) {
        background = tokens::blue;
        foreground = tokens::on_primary;
        // Halo keeps its exact pressed blue; the other looks darken their primary.
        pressed = active_appearance() == Appearance::Halo ? 0x0F6AD0 : shade(tokens::blue, 82);
    } else if (tone == ButtonTone::Danger) {
        background = tokens::danger_surface;
        foreground = tokens::coral;
        pressed = 0x52202A;
    } else if (tone == ButtonTone::Quiet) {
        background = tokens::surface;
        foreground = tokens::text_secondary;
    }
    lv_obj_set_height(value, tokens::BUTTON_HEIGHT);
    lv_obj_set_style_bg_color(value, color(background), 0);
    lv_obj_set_style_bg_color(value, color(pressed), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(value, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(value, 0, 0);
    lv_obj_set_style_radius(value, metrics.button_radius, 0);
    lv_obj_set_style_shadow_width(value, 0, 0);
    if (tone == ButtonTone::Primary && metrics.gradient_primary) {
        lv_obj_set_style_bg_grad_color(value, color(tokens::primary_end), 0);
        lv_obj_set_style_bg_grad_dir(value, LV_GRAD_DIR_HOR, 0);
    }
    if (tone == ButtonTone::Secondary && metrics.outline_secondary) {
        // Marble: the secondary action is a hairline outline.
        lv_obj_set_style_bg_opa(value, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(value, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(value, 2, 0);
        lv_obj_set_style_border_color(value, color(tokens::text_tertiary), 0);
        lv_obj_set_style_border_opa(value, LV_OPA_60, 0);
    }
    lv_obj_set_style_pad_hor(value, 16, 0);
    lv_obj_set_style_transform_width(value, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(value, -3, LV_STATE_PRESSED);
    auto* title = make_label(value, text, foreground, font ? font : font_headline());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(title, lv_pct(100), 0);
    lv_obj_center(title);
    return value;
}

lv_obj_t* make_tag(lv_obj_t* parent, const char* text, std::uint32_t accent) {
    auto* value = make_label(parent, text, accent, font_caption());
    lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_bg_color(value, color(accent), 0);
    lv_obj_set_style_bg_opa(value, LV_OPA_20, 0);
    lv_obj_set_style_radius(value, tokens::radius_small, 0);
    lv_obj_set_style_pad_hor(value, 12, 0);
    lv_obj_set_style_pad_ver(value, 3, 0);
    return value;
}

void set_button_disabled(lv_obj_t* button, bool disabled) {
    if (disabled) lv_obj_add_state(button, LV_STATE_DISABLED);
    else lv_obj_remove_state(button, LV_STATE_DISABLED);
    lv_obj_set_style_opa(button, disabled ? LV_OPA_40 : LV_OPA_COVER, 0);
}

}  // namespace aura::ui

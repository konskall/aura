#include "aura/aura_theme.hpp"

LV_FONT_DECLARE(aura_font_plex_regular_22);
LV_FONT_DECLARE(aura_font_plex_regular_26);
LV_FONT_DECLARE(aura_font_plex_medium_28);
LV_FONT_DECLARE(aura_font_plex_semibold_32);
LV_FONT_DECLARE(aura_font_plex_light_72);
LV_FONT_DECLARE(aura_font_plex_light_96);
LV_FONT_DECLARE(aura_font_space_medium_26);

namespace aura::ui {

lv_color_t color(std::uint32_t value) { return lv_color_hex(value); }
const lv_font_t* font_caption() { return &aura_font_plex_regular_22; }
const lv_font_t* font_body() { return &aura_font_plex_regular_26; }
const lv_font_t* font_headline() { return &aura_font_plex_medium_28; }
const lv_font_t* font_title() { return &aura_font_plex_semibold_32; }
const lv_font_t* font_display() { return &aura_font_plex_light_72; }
const lv_font_t* font_clock() { return &aura_font_plex_light_96; }
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
    lv_obj_set_style_bg_color(object, color(selected ? tokens::surface_glass : tokens::surface), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, selected ? 2 : 0, 0);
    lv_obj_set_style_border_color(object, color(accent), 0);
    lv_obj_set_style_border_opa(object, LV_OPA_70, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_pad_all(object, 16, 0);
    lv_obj_set_style_bg_color(object, color(tokens::surface_high), LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(object, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(object, -3, LV_STATE_PRESSED);
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
    if (tone == ButtonTone::Primary) {
        background = tokens::blue;
        pressed = 0x0F6AD0;
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
    lv_obj_set_style_radius(value, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(value, 0, 0);
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

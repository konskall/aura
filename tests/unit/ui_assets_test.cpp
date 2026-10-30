#include "aura/aura_appearance.hpp"
#include "aura/aura_icons.hpp"
#include "aura/aura_aurora_view.hpp"
#include "aura/aura_mercury_view.hpp"
#include "aura/aura_pearl_view.hpp"
#include "aura/aura_stars_view.hpp"
#include "aura/aura_presence.hpp"
#include "aura/aura_theme.hpp"
#include "aura/strings.hpp"
#include "aura/visual_strings.hpp"
#include "aura/companion_strings.hpp"

#include <array>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string_view>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Strings/VisualStrings are positional aggregates of `const char*`. Every
// field must be initialized in both languages and every character must exist
// in the UI fonts, or the device would show an empty box.
template <typename Table>
void expect_complete_table(const Table& table, const char* name, const lv_font_t* font = nullptr) {
    static_assert(sizeof(Table) % sizeof(const char*) == 0, "string table holds only pointers");
    const lv_font_t* drawn = font ? font : aura::ui::font_caption();
    const auto* fields = reinterpret_cast<const char* const*>(&table);
    for (std::size_t index = 0; index < sizeof(Table) / sizeof(const char*); ++index) {
        if (fields[index] == nullptr) {
            std::cerr << "FAILED: " << name << " field " << index << " is missing\n";
            std::exit(1);
        }
        const std::string_view text(fields[index]);
        for (std::size_t offset = 0; offset < text.size();) {
            const auto lead = static_cast<unsigned char>(text[offset]);
            std::uint32_t codepoint = lead;
            std::size_t length = 1;
            if (lead >= 0xF0) { codepoint = lead & 0x07U; length = 4; }
            else if (lead >= 0xE0) { codepoint = lead & 0x0FU; length = 3; }
            else if (lead >= 0xC0) { codepoint = lead & 0x1FU; length = 2; }
            for (std::size_t next = 1; next < length && offset + next < text.size(); ++next) {
                codepoint = (codepoint << 6U) | (static_cast<unsigned char>(text[offset + next]) & 0x3FU);
            }
            offset += length;
            if (codepoint < 0x20) continue;
            lv_font_glyph_dsc_t glyph{};
            // The Plex set must draw everything itself; an appearance font may
            // leave a rare glyph (arrows, some dashes) to its Plex fallback.
            if (!lv_font_get_glyph_dsc(drawn, &glyph, codepoint, 0) || (!font && glyph.resolved_font != drawn)) {
                std::cerr << "FAILED: " << name << " field " << index << " uses U+" << std::hex << codepoint
                          << std::dec << " which the UI fonts cannot draw: " << text << '\n';
                std::exit(1);
            }
        }
    }
}

template <typename Table>
void expect_no_greek_copy(const Table& table, const char* name,
                          std::initializer_list<const char*> intentional_greek = {}) {
    const auto* fields = reinterpret_cast<const char* const*>(&table);
    for (std::size_t index = 0; index < sizeof(Table) / sizeof(const char*); ++index) {
        const std::string_view text(fields[index]);
        for (std::size_t offset = 0; offset < text.size();) {
            const auto lead = static_cast<unsigned char>(text[offset]);
            std::uint32_t codepoint = lead;
            std::size_t length = 1;
            if (lead >= 0xF0) { codepoint = lead & 0x07U; length = 4; }
            else if (lead >= 0xE0) { codepoint = lead & 0x0FU; length = 3; }
            else if (lead >= 0xC0) { codepoint = lead & 0x1FU; length = 2; }
            for (std::size_t next = 1; next < length && offset + next < text.size(); ++next) {
                codepoint = (codepoint << 6U) | (static_cast<unsigned char>(text[offset + next]) & 0x3FU);
            }
            offset += length;
            if (codepoint >= 0x0370 && codepoint <= 0x03FF) {
                bool allowed = false;
                for (const auto* value : intentional_greek) allowed = allowed || value == fields[index];
                if (allowed) continue;
                std::cerr << "FAILED: " << name << " field " << index
                          << " contains untranslated Greek copy: " << text << '\n';
                std::exit(1);
            }
        }
    }
}

void expect_glyphs(const lv_font_t* font, const std::array<std::uint32_t, 33>& codepoints) {
    for (const auto codepoint : codepoints) {
        lv_font_glyph_dsc_t glyph{};
        if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.resolved_font != font) {
            std::cerr << "FAILED: missing UI glyph U+" << std::hex << codepoint << '\n';
            std::exit(1);
        }
    }
}
}

// Every appearance draws all copy in its own text fonts, and its numeral fonts
// carry the digits and symbols themselves (not through their fallback).
void check_appearance_fonts() {
    using aura::Appearance;
    namespace ui = aura::ui;
    const auto& plex = ui::fonts_for(Appearance::Halo);
    for (const auto appearance : {Appearance::Marble, Appearance::Aurora, Appearance::Graphite}) {
        const auto& fonts = ui::fonts_for(appearance);
        expect(fonts.caption != plex.caption && fonts.body != plex.body && fonts.headline != plex.headline &&
                   fonts.title != plex.title && fonts.display != plex.display && fonts.clock != plex.clock,
               "Marble, Aurora and Graphite have their own fonts");
    }
    expect(ui::fonts_for(Appearance::Graphite).hero != nullptr &&
               ui::fonts_for(Appearance::Graphite).hero != ui::fonts_for(Appearance::Graphite).numerals_large,
           "Graphite has its own Home clock numerals");
    for (const auto appearance : aura::kAppearances) {
        const auto& fonts = ui::fonts_for(appearance);
        // The 48 shared screens were laid out for Halo's 72 and 96 px numerals:
        // every look keeps those two roles at Halo's height; the larger sizes of
        // the signature screens are numerals_small and numerals_large.
        expect(lv_font_get_line_height(fonts.display) <= lv_font_get_line_height(plex.display) * 11 / 10 &&
                   lv_font_get_line_height(fonts.clock) <= lv_font_get_line_height(plex.clock) * 11 / 10,
               "the shared numeral roles stay within Halo's line height");
        expect(fonts.numerals_small != nullptr && fonts.numerals_large != nullptr,
               "every look has its signature numerals");
        for (const auto* font : {fonts.caption, fonts.body, fonts.headline, fonts.title}) {
            expect_complete_table(aura::l10n::el_gr(), "Strings el_gr", font);
            expect_complete_table(aura::l10n::en_us(), "Strings en_us", font);
            expect_complete_table(aura::l10n::visual_el_gr(), "VisualStrings el_gr", font);
            expect_complete_table(aura::l10n::visual_en_us(), "VisualStrings en_us", font);
            expect_complete_table(aura::l10n::companion_el_gr(), "CompanionStrings el_gr", font);
            expect_complete_table(aura::l10n::companion_en_us(), "CompanionStrings en_us", font);
        }
        auto carries = [](const lv_font_t* font, std::initializer_list<std::uint32_t> codepoints) {
            for (const auto codepoint : codepoints) {
                lv_font_glyph_dsc_t glyph{};
                if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.resolved_font != font) return false;
            }
            return true;
        };
        // Every numeral the UI prints (no copy uses U+2212, which some faces lack).
        for (const auto* font : {fonts.display, fonts.numerals_small}) {
            expect(carries(font, {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', 0xB0, '%', '+', '-', '.',
                                  '/', 'C', ' '}),
                   "the display numerals carry every digit and symbol themselves");
        }
        for (const auto* font : {fonts.clock, fonts.numerals_large, fonts.hero ? fonts.hero : fonts.numerals_large}) {
            expect(carries(font, {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', '-', ' '}),
                   "the clock numerals carry the digits themselves");
            // The new looks also print the weather temperature with them.
            expect(appearance == Appearance::Halo || font == fonts.clock || carries(font, {0xB0}),
                   "the signature numerals carry the degree sign");
        }
    }
}

// Every icon role draws in every appearance: Halo keeps Lucide, the others use
// their own family with at most eight Lucide stand-ins.
void check_appearance_icons() {
    using aura::Appearance;
    namespace ui = aura::ui;
    for (const auto appearance : aura::kAppearances) {
        std::size_t stand_ins = 0;
        for (std::size_t index = 0; index < ui::icon_role_count(); ++index) {
            const auto role = static_cast<ui::IconRole>(index);
            const auto* icon = ui::icon_asset(role, appearance);
            expect(icon != nullptr && icon->header.cf == LV_COLOR_FORMAT_A8 && icon->header.w == 64 &&
                       icon->header.h == 64 && icon->data != nullptr,
                   "every icon role draws as a 64 px A8 image in every appearance");
            const bool stand_in = ui::icon_is_fallback(role, appearance);
            const bool lucide = icon == ui::icon_asset(role, Appearance::Halo);
            if (appearance == Appearance::Halo) {
                expect(!stand_in, "Halo draws Lucide as its own family");
            } else {
                expect(stand_in == lucide, "a stand-in is exactly the Lucide asset, and only a stand-in");
                stand_ins += stand_in ? 1U : 0U;
            }
        }
        expect(stand_ins <= 8U, "each family draws all but at most eight roles itself");
    }
    ui::set_active_appearance(Appearance::Graphite);
    expect(ui::icon_asset(ui::IconRole::Home) == ui::icon_asset(ui::IconRole::Home, Appearance::Graphite),
           "icons follow the active appearance");
    ui::set_active_appearance(Appearance::Halo);
    expect(ui::icon_asset(ui::IconRole::Palette) != nullptr, "Settings > Appearance has its palette icon");
}

// Palettes, fonts and metrics of the four appearances (Settings > Appearance).
void check_appearance_registry() {
    using aura::Appearance;
    namespace ui = aura::ui;

    const auto& halo = ui::palette_for(Appearance::Halo);
    expect(halo.canvas == 0x000000 && halo.surface == 0x121822 && halo.surface_high == 0x1B2330 &&
               halo.surface_glass == 0x222C3B && halo.separator == 0x2B3546 && halo.danger_surface == 0x3A141B &&
               halo.text == 0xF5F8FC && halo.text_secondary == 0xA9B5C7 && halo.text_tertiary == 0x748197 &&
               halo.blue == 0x178BFF && halo.indigo == 0x4C52FF && halo.ice == 0x25D6FF && halo.lilac == 0x8A45FF &&
               halo.magenta == 0xDE58FB && halo.mint == 0x35ECC3 && halo.amber == 0xFFCB3F &&
               halo.pink == 0xFF579E && halo.coral == 0xFF6B78,
           "Halo keeps the v12 palette");
    expect(halo.primary_end == halo.blue && halo.on_primary == halo.text,
           "Halo's primary stays flat blue with today's label colour");

    const auto& marble = ui::palette_for(Appearance::Marble);
    expect(marble.text == 0xF2EDE4 && marble.blue == 0xF2EDE4 && marble.on_primary == 0x0B0A09 &&
               marble.indigo == 0xDFC08C && marble.separator == 0x2B2723 && marble.text_tertiary == 0x877D71,
           "Marble palette follows the spec");
    const auto& aurora = ui::palette_for(Appearance::Aurora);
    expect(aurora.blue == 0x3A7BFF && aurora.primary_end == 0xC35CF0 && aurora.text == 0xF3F5FF &&
               aurora.ice == 0x38E1C9 && aurora.on_primary == 0xFFFFFF,
           "Aurora palette follows the spec");
    const auto& graphite = ui::palette_for(Appearance::Graphite);
    expect(graphite.text == 0xFFFFFF && graphite.text_tertiary == 0x737985 && graphite.blue == 0xFFFFFF &&
               graphite.on_primary == 0x000000 && graphite.amber == 0xFFB547,
           "Graphite palette follows the spec");

    expect(ui::contrast_ratio(0xFFFFFF, 0x000000) > 20.9 && ui::contrast_ratio(0x777777, 0x777777) == 1.0,
           "contrast follows WCAG 2.x");
    for (const auto appearance : aura::kAppearances) {
        const auto& palette = ui::palette_for(appearance);
        expect(ui::contrast_ratio(palette.text, palette.canvas) >= 7.0 &&
                   ui::contrast_ratio(palette.text, palette.surface) >= 7.0,
               "primary text reads at 7:1 on canvas and surface");
        expect(ui::contrast_ratio(palette.text_secondary, palette.canvas) >= 4.5 &&
                   ui::contrast_ratio(palette.text_tertiary, palette.canvas) >= 4.5,
               "secondary and tertiary text read at 4.5:1");
        expect(ui::contrast_ratio(palette.on_primary, palette.blue) >= 3.0 &&
                   ui::contrast_ratio(palette.on_primary, palette.primary_end) >= 3.0,
               "button labels read at 3:1 on both primary stops");
    }

    ui::set_active_appearance(Appearance::Marble);
    expect(ui::active_appearance() == Appearance::Marble && ui::tokens::text == 0xF2EDE4 &&
               ui::tokens::on_primary == 0x0B0A09,
           "activating Marble swaps the colour tokens");
    ui::set_active_appearance(Appearance::Halo);
    expect(ui::tokens::text == 0xF5F8FC && ui::tokens::blue == 0x178BFF, "Halo restores its tokens");

    for (const auto appearance : aura::kAppearances) {
        ui::set_active_appearance(appearance);
        const auto& fonts = ui::fonts_for(appearance);
        expect(ui::font_caption() == fonts.caption && ui::font_body() == fonts.body &&
                   ui::font_headline() == fonts.headline && ui::font_title() == fonts.title &&
                   ui::font_display() == fonts.display && ui::font_clock() == fonts.clock,
               "font accessors follow the active appearance");
        expect(ui::font_numerals_small() == fonts.numerals_small && ui::font_numerals_large() == fonts.numerals_large,
               "signature numerals follow the active appearance");
        expect(ui::font_hero() == (fonts.hero ? fonts.hero : fonts.numerals_large),
               "the hero numerals fall back to the large signature numerals");
        expect(&ui::active_metrics() == &ui::metrics_for(appearance), "component metrics follow the active appearance");
    }
    ui::set_active_appearance(Appearance::Halo);
    expect(ui::metrics_for(Appearance::Halo).rows == ui::RowStyle::Platter &&
               ui::metrics_for(Appearance::Halo).platter_radius == 28 &&
               ui::metrics_for(Appearance::Halo).hero_center_y == 260,
           "Halo metrics describe today's components");
}

// A headless 480 x 480 LVGL display for the checks that create objects.
void ensure_lvgl() {
    static bool ready = false;
    if (ready) return;
    ready = true;
    lv_init();
    static std::uint8_t draw_buffer[480 * 48 * 2];
    auto* display = lv_display_create(480, 480);
    lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t* target, const lv_area_t*, std::uint8_t*) {
        lv_display_flush_ready(target);
    });
}

bool same_color(lv_color_t value, std::uint32_t hex) { return (lv_color_to_u32(value) & 0xFFFFFFU) == hex; }

// The shared components draw each appearance's kit: rows, platters and buttons.
void check_component_kit() {
    using aura::Appearance;
    namespace ui = aura::ui;
    ensure_lvgl();
    auto* screen = lv_screen_active();
    expect(ui::palette_for(Appearance::Halo).toggle_on == 0x178BFF &&
               ui::palette_for(Appearance::Marble).toggle_on == 0xDFC08C &&
               ui::palette_for(Appearance::Aurora).toggle_on == 0x3A7BFF &&
               ui::palette_for(Appearance::Graphite).toggle_on == 0x3DDC97,
           "a switch that is on shows a colour its knob stands out from");
    for (const auto appearance : aura::kAppearances) {
        ui::set_active_appearance(appearance);
        const auto& palette = ui::palette_for(appearance);
        const auto& metrics = ui::metrics_for(appearance);
        expect(ui::tokens::toggle_on == palette.toggle_on, "the toggle colour follows the appearance");

        auto* primary = ui::make_button(screen, "Α", ui::ButtonTone::Primary);
        expect(same_color(lv_obj_get_style_bg_color(primary, LV_PART_MAIN), palette.blue) &&
                   same_color(lv_obj_get_style_text_color(lv_obj_get_child(primary, 0), LV_PART_MAIN), palette.on_primary) &&
                   lv_obj_get_style_radius(primary, LV_PART_MAIN) == metrics.button_radius,
               "a primary button uses the look's primary, label colour and radius");
        expect((lv_obj_get_style_bg_grad_dir(primary, LV_PART_MAIN) == LV_GRAD_DIR_HOR) == metrics.gradient_primary &&
                   (!metrics.gradient_primary ||
                    same_color(lv_obj_get_style_bg_grad_color(primary, LV_PART_MAIN), palette.primary_end)),
               "Aurora's primary runs a horizontal gradient to its second stop");
        auto* secondary = ui::make_button(screen, "Β", ui::ButtonTone::Secondary);
        expect(metrics.outline_secondary ? lv_obj_get_style_bg_opa(secondary, LV_PART_MAIN) == LV_OPA_TRANSP &&
                                               lv_obj_get_style_border_width(secondary, LV_PART_MAIN) >= 1
                                         : lv_obj_get_style_bg_opa(secondary, LV_PART_MAIN) == LV_OPA_COVER,
               "Marble's secondary button is an outline, the others are filled");

        auto* row = lv_obj_create(screen);
        ui::reset(row);
        ui::style_row(row, palette.ice, false);
        auto* platter = lv_obj_create(screen);
        ui::reset(platter);
        ui::style_platter(platter, palette.ice);
        if (metrics.rows == ui::RowStyle::Hairline) {
            expect(lv_obj_get_style_bg_opa(row, LV_PART_MAIN) == LV_OPA_TRANSP &&
                       lv_obj_get_style_border_side(row, LV_PART_MAIN) == LV_BORDER_SIDE_BOTTOM &&
                       lv_obj_get_style_border_width(row, LV_PART_MAIN) == 1 &&
                       same_color(lv_obj_get_style_border_color(row, LV_PART_MAIN), palette.separator),
                   "Marble rows are open, with one hairline underneath");
            expect(lv_obj_get_style_bg_opa(platter, LV_PART_MAIN) == LV_OPA_TRANSP &&
                       lv_obj_get_style_border_side(platter, LV_PART_MAIN) == LV_BORDER_SIDE_FULL &&
                       lv_obj_get_style_border_width(platter, LV_PART_MAIN) >= 1,
                   "Marble platters are hairline outlines");
        } else if (metrics.rows == ui::RowStyle::Glass) {
            expect(lv_obj_get_style_bg_grad_dir(row, LV_PART_MAIN) == LV_GRAD_DIR_VER &&
                       lv_obj_get_style_bg_grad_dir(platter, LV_PART_MAIN) == LV_GRAD_DIR_VER &&
                       lv_obj_get_style_border_width(platter, LV_PART_MAIN) == 1,
                   "Aurora rows and platters are glass: a vertical gradient with a light edge");
            expect(same_color(lv_obj_get_style_bg_color(row, LV_PART_MAIN), 0x1A1F34) &&
                       same_color(lv_obj_get_style_bg_grad_color(row, LV_PART_MAIN), 0x0D1020) &&
                       same_color(lv_obj_get_style_border_color(row, LV_PART_MAIN), 0xAAB4FF) &&
                       lv_obj_get_style_border_opa(row, LV_PART_MAIN) == 28,
                   "Aurora's glass runs from 0x1A1F34 to 0x0D1020 with an 11 % 0xAAB4FF edge");
        } else {
            expect(lv_obj_get_style_bg_opa(row, LV_PART_MAIN) == LV_OPA_COVER &&
                       same_color(lv_obj_get_style_bg_color(row, LV_PART_MAIN), palette.surface) &&
                       lv_obj_get_style_bg_grad_dir(row, LV_PART_MAIN) == LV_GRAD_DIR_NONE,
                   "Halo and Graphite rows are solid platters");
        }
        lv_obj_clean(screen);
    }
    ui::set_active_appearance(Appearance::Halo);

    // Σέλας's light fields: one small shared soft disc, scaled and tinted.
    auto* field = ui::light_field(screen, 300, 120, 150, 0x7B5CFF, 90);
    const auto* source = static_cast<const lv_image_dsc_t*>(lv_image_get_src(field));
    lv_obj_update_layout(field);
    expect(source && source->header.cf == LV_COLOR_FORMAT_A8 && source->header.w == 32 && source->header.h == 32,
           "a light field draws one shared 32 x 32 A8 disc (1 KB of internal RAM)");
    expect(lv_obj_get_width(field) == 300 && lv_obj_get_height(field) == 300 && lv_obj_get_x(field) == 150 &&
               lv_obj_get_y(field) == -30 && !lv_obj_is_clickable(field) &&
               same_color(lv_obj_get_style_image_recolor(field, LV_PART_MAIN), 0x7B5CFF) &&
               lv_obj_get_style_image_opa(field, LV_PART_MAIN) == 90,
           "a light field is centred, sized by its radius, tinted and never a target");
    auto* second = ui::light_field(screen, 100, 100, 40, 0x38E1C9, 60);
    expect(lv_image_get_src(second) == lv_image_get_src(field), "every light field shares the one disc");
    auto* plain = lv_image_create(screen);
    expect(ui::is_light_field(field) && ui::is_light_field(second) && !ui::is_light_field(plain) &&
               !ui::is_light_field(screen),
           "a light field is recognised by its disc, and nothing else is");
    lv_obj_clean(screen);
}

// Every appearance has an assistant presence that animates in all four states,
// freezes for Reduced Motion and detaches safely.
void check_presences() {
    using aura::ui::PresenceState;
    ensure_lvgl();
    for (const auto appearance : aura::kAppearances) {
        auto presence = aura::ui::make_presence(appearance);
        expect(presence != nullptr, "every appearance has a presence");
        auto* object = presence->attach(lv_screen_active(), 240, 260);
        expect(object != nullptr && presence->object() == object, "a presence attaches one object");
        for (const auto state : {PresenceState::Idle, PresenceState::Listening, PresenceState::Thinking,
                                 PresenceState::Speaking}) {
            presence->set_state(state);
            presence->set_level(0.8F);
            const auto before = presence->frame_count();
            for (int step = 0; step < 4; ++step) presence->tick(66);
            expect(presence->frame_count() > before, "the presence animates in every state");
        }
        presence->set_reduced_motion(true);
        const auto frozen = presence->frame_count();
        for (int step = 0; step < 4; ++step) presence->tick(66);
        expect(presence->frame_count() == frozen, "Reduced Motion freezes the presence");
        presence->detach();
        presence->detach();
        expect(presence->object() == nullptr, "a detached presence holds no object");
        lv_obj_clean(lv_screen_active());
    }
    expect(aura::ui::make_presence(aura::Appearance::Halo)->buffer_bytes() == 92928U,
           "Halo keeps Mercury's 92,928-byte buffer");
}

// Μάρμαρο's pearl: a sphere in Mercury's buffer size plus rings drawn by LVGL.
void check_pearl_presence() {
    using aura::ui::PresenceState;
    ensure_lvgl();
    auto* screen = lv_screen_active();
    {
        auto presence = aura::ui::make_presence(aura::Appearance::Marble);
        presence->attach(screen, 240, 272);
        expect(lv_obj_get_child_count(screen) > 1, "Marble's presence is the pearl with its rings, not Mercury");
        presence->detach();
        lv_obj_clean(screen);
    }

    aura::ui::AuraPearlView pearl;
    expect(pearl.buffer_bytes() == 92928U, "the pearl's sphere uses a 176 x 176 RGB565 + A8 buffer");
    expect(aura::ui::AuraPearlView::frame_interval_ms == 66, "the pearl animates at about 15 FPS");
    expect(pearl.attach(screen, 240, 272) == pearl.object() && pearl.object() != nullptr,
           "the pearl attaches its sphere image");
    for (const auto state : {PresenceState::Idle, PresenceState::Listening, PresenceState::Thinking,
                             PresenceState::Speaking}) {
        pearl.set_state(state);
        const auto before = pearl.frame_count();
        for (int step = 0; step < 3; ++step) pearl.tick(66);
        expect(pearl.frame_count() > before, "the pearl animates in every state");
    }

    // Listening: the ring around the pearl follows the voice.
    pearl.set_state(PresenceState::Listening);
    pearl.set_level(0.0F);
    for (int step = 0; step < 10; ++step) pearl.tick(66);
    const int quiet = pearl.ring_radius();
    pearl.set_level(1.0F);
    for (int step = 0; step < 10; ++step) pearl.tick(66);
    const int loud = pearl.ring_radius();
    expect(quiet > 80 && loud > quiet, "a louder voice widens the listening ring");
    expect(loud <= 205, "the rings stay within 205 px of the centre");

    pearl.set_reduced_motion(true);
    const auto frozen = pearl.frame_count();
    for (int step = 0; step < 4; ++step) pearl.tick(66);
    expect(pearl.frame_count() == frozen, "Reduced Motion freezes the pearl");
    pearl.set_reduced_motion(false);

    // Detached, it holds nothing of the cleaned screen; attached again, it moves.
    pearl.detach();
    lv_obj_clean(screen);
    expect(pearl.object() == nullptr && !pearl.tick(66), "a detached pearl forgets its objects");
    pearl.attach(screen, 240, 272);
    const auto again = pearl.frame_count();
    for (int step = 0; step < 3; ++step) pearl.tick(66);
    expect(pearl.object() != nullptr && pearl.frame_count() > again, "the pearl attaches a second time");
    pearl.detach();
    lv_obj_clean(screen);
}

// Σέλας's ribbons: a 432 x 200 band of additive light and a haze behind it.
void check_aurora_presence() {
    using aura::ui::PresenceState;
    ensure_lvgl();
    auto* screen = lv_screen_active();
    expect(aura::ui::make_presence(aura::Appearance::Aurora)->buffer_bytes() == 259200U,
           "Σέλας's presence is the ribbons' 432 x 200 RGB565 + A8 band");

    auto aurora = std::make_unique<aura::ui::AuraAuroraView>();
    expect(aura::ui::AuraAuroraView::frame_interval_ms == 66, "the ribbons animate at about 15 FPS");
    expect(aurora->attach(screen, 240, 234) == aurora->object() && aurora->object() != nullptr,
           "the ribbons attach their band");
    const auto* band = static_cast<const lv_image_dsc_t*>(lv_image_get_src(aurora->object()));
    const auto lit = [&] {
        const auto* alpha = band->data + 432 * 200 * 2;
        int count = 0;
        for (int index = 0; index < 432 * 200; ++index) count += alpha[index] > 24 ? 1 : 0;
        return count;
    };
    for (const auto state : {PresenceState::Idle, PresenceState::Listening, PresenceState::Thinking,
                             PresenceState::Speaking}) {
        aurora->set_state(state);
        aurora->set_level(0.6F);
        const auto before = aurora->frame_count();
        for (int step = 0; step < 3; ++step) aurora->tick(66);
        expect(aurora->frame_count() > before && lit() > 1000, "the ribbons move and shine in every state");
    }

    // Listening: a louder voice makes the ribbons sway further.
    aurora->set_state(PresenceState::Listening);
    aurora->set_level(0.0F);
    for (int step = 0; step < 10; ++step) aurora->tick(66);
    const float quiet = aurora->amplitude();
    aurora->set_level(1.0F);
    for (int step = 0; step < 10; ++step) aurora->tick(66);
    const float loud = aurora->amplitude();
    expect(quiet > 0.0F && loud > quiet, "a louder voice makes the ribbons sway further");

    // Thinking: within a second the ribbons have tied into the loop.
    aurora->set_state(PresenceState::Thinking);
    for (int step = 0; step < 16; ++step) aurora->tick(66);
    expect(aurora->loop_share() >= 0.95F && lit() > 1000, "within a second the ribbons tie into the loop");

    aurora->set_reduced_motion(true);
    const auto frozen = aurora->frame_count();
    for (int step = 0; step < 4; ++step) aurora->tick(66);
    expect(aurora->frame_count() == frozen, "Reduced Motion freezes the ribbons");
    aurora->set_reduced_motion(false);

    aurora->detach();
    lv_obj_clean(screen);
    expect(aurora->object() == nullptr && !aurora->tick(66), "detached ribbons forget their objects");
    aurora->attach(screen, 240, 234);
    const auto again = aurora->frame_count();
    for (int step = 0; step < 3; ++step) aurora->tick(66);
    expect(aurora->object() != nullptr && aurora->frame_count() > again, "the ribbons attach a second time");
    aurora->detach();
    lv_obj_clean(screen);
}

// Γραφίτης's Αστερισμός: 300 points on a turning sphere in a 280 x 280 canvas.
void check_stars_presence() {
    using aura::ui::PresenceState;
    ensure_lvgl();
    auto* screen = lv_screen_active();
    expect(aura::ui::make_presence(aura::Appearance::Graphite)->buffer_bytes() == 209088U,
           "Γραφίτης's presence is the 264 x 264 RGB565 + A8 constellation");

    auto stars = std::make_unique<aura::ui::AuraStarsView>();
    expect(aura::ui::AuraStarsView::frame_interval_ms == 66, "the constellation animates at about 15 FPS");
    expect(stars->attach(screen, 240, 260) == stars->object() && stars->object() != nullptr,
           "the constellation attaches its canvas");
    const auto* canvas = static_cast<const lv_image_dsc_t*>(lv_image_get_src(stars->object()));
    constexpr int side = aura::ui::AuraStarsView::size;
    const auto lit = [&] {
        const auto* alpha = canvas->data + side * side * 2;
        int count = 0;
        for (int index = 0; index < side * side; ++index) count += alpha[index] > 24 ? 1 : 0;
        return count;
    };
    for (const auto state : {PresenceState::Idle, PresenceState::Listening, PresenceState::Thinking,
                             PresenceState::Speaking}) {
        stars->set_state(state);
        stars->set_level(0.6F);
        const auto before = stars->frame_count();
        for (int step = 0; step < 3; ++step) stars->tick(66);
        expect(stars->frame_count() > before && lit() > 800, "the constellation turns and shines in every state");
    }

    // Listening: a louder voice lifts the points further from the sphere.
    stars->set_state(PresenceState::Listening);
    stars->set_level(0.0F);
    for (int step = 0; step < 10; ++step) stars->tick(66);
    const float quiet = stars->swell();
    stars->set_level(1.0F);
    for (int step = 0; step < 10; ++step) stars->tick(66);
    const float loud = stars->swell();
    expect(loud > quiet, "a louder voice lifts the points further");

    // At full voice, listening or speaking, no point reaches the canvas's edge:
    // the swell is never cut.
    const auto edge_lit = [&] {
        const auto* alpha = canvas->data + side * side * 2;
        int count = 0;
        for (int index = 0; index < side; ++index) {
            count += (alpha[index] != 0) + (alpha[(side - 1) * side + index] != 0) +
                     (alpha[index * side] != 0) + (alpha[index * side + side - 1] != 0);
        }
        return count;
    };
    int edge = 0;
    for (const auto state : {PresenceState::Listening, PresenceState::Speaking}) {
        stars->set_state(state);
        stars->set_level(1.0F);
        for (int step = 0; step < 120; ++step) {
            stars->tick(66);
            edge += edge_lit();
        }
    }
    expect(edge == 0, "a loud voice never reaches the canvas's edge");

    // Thinking: a bright band sweeps the sphere.
    stars->set_state(PresenceState::Thinking);
    for (int step = 0; step < 6; ++step) stars->tick(66);
    const float band = stars->band();
    for (int step = 0; step < 6; ++step) stars->tick(66);
    expect(stars->band() != band, "the thinking band moves");

    stars->set_reduced_motion(true);
    const auto frozen = stars->frame_count();
    for (int step = 0; step < 4; ++step) stars->tick(66);
    expect(stars->frame_count() == frozen, "Reduced Motion freezes the constellation");
    stars->set_reduced_motion(false);

    stars->detach();
    lv_obj_clean(screen);
    expect(stars->object() == nullptr && !stars->tick(66), "a detached constellation forgets its objects");
    stars->attach(screen, 240, 260);
    const auto again = stars->frame_count();
    for (int step = 0; step < 3; ++step) stars->tick(66);
    expect(stars->object() != nullptr && stars->frame_count() > again, "the constellation attaches a second time");
    stars->detach();
    lv_obj_clean(screen);
}

int main() {
    expect(aura::ui::tokens::HEADER_HEIGHT == 96 && aura::ui::tokens::BUTTON_HEIGHT >= 80 &&
               aura::ui::tokens::ROW_HEIGHT >= 80 && aura::ui::tokens::HEADER_BUTTON >= 64,
           "watch-class header, rows and primary targets on the 12.4 px/mm panel");
    expect(aura::ui::tokens::CONTENT_SAFE_RECT.x >= aura::ui::tokens::GUTTER,
           "content safe rect respects horizontal gutters");
    expect(aura::ui::tokens::HERO_RECT.y >= aura::ui::tokens::STATUS_BAR_HEIGHT,
           "Mercury hero begins below the status bar");
    expect(aura::ui::tokens::MERCURY_RECT.x >= aura::ui::tokens::HERO_RECT.x &&
           aura::ui::tokens::MERCURY_RECT.y >= aura::ui::tokens::HERO_RECT.y &&
           aura::ui::tokens::MERCURY_RECT.x + aura::ui::tokens::MERCURY_RECT.width <=
               aura::ui::tokens::HERO_RECT.x + aura::ui::tokens::HERO_RECT.width &&
           aura::ui::tokens::MERCURY_RECT.y + aura::ui::tokens::MERCURY_RECT.height <=
               aura::ui::tokens::HERO_RECT.y + aura::ui::tokens::HERO_RECT.height,
           "Mercury pixels remain inside the hero safe rect");
    expect(aura::ui::tokens::BOTTOM_ACTION_SAFE_RECT.y >=
               aura::ui::tokens::HERO_RECT.y + aura::ui::tokens::HERO_RECT.height,
           "bottom actions do not overlap the hero safe rect");

    expect(aura::ui::icon_role_count() >= 40, "semantic icon registry covers the product");
    for (std::size_t index = 0; index < aura::ui::icon_role_count(); ++index) {
        const auto role = static_cast<aura::ui::IconRole>(index);
        const auto* icon = aura::ui::icon_asset(role);
        expect(icon != nullptr, "every icon role resolves");
        expect(icon->header.cf == LV_COLOR_FORMAT_A8, "icons use tintable A8 assets");
        expect(icon->header.w == 64 && icon->header.h == 64, "icons share a 64px source grid");
        expect(icon->data != nullptr && icon->data_size >= 64U * 64U, "icon bitmap is present");
        expect(aura::ui::icon_role_name(role)[0] != '\0', "icon role has a manifest name");
    }

    constexpr std::array<std::uint32_t, 33> required{
        0x0041, 0x005A, 0x0061, 0x007A, 0x00B0, 0x00C1, 0x00E1, 0x0301,
        0x0386, 0x0388, 0x0389, 0x038A, 0x038C, 0x038E, 0x038F,
        0x03AC, 0x03AD, 0x03AE, 0x03AF, 0x03CC, 0x03CD, 0x03CE,
        0x03CA, 0x03CB, 0x0390, 0x03B0, 0x03C2, 0x03C9,
        0x2014, 0x2022, 0x2026, 0x20AC, 0x2122,
    };
    // Every text size carries the full Greek/Latin UI range.
    const std::array<const lv_font_t*, 4> ui_fonts{
        aura::ui::font_caption(), aura::ui::font_body(), aura::ui::font_headline(), aura::ui::font_title(),
    };
    for (const auto* font : ui_fonts) expect_glyphs(font, required);

    // The Light numeric subsets draw every character their formats print
    // ("%.0f°C", "%d%%", "204 681", "%02d:%02d") without the fallback font.
    constexpr std::array<std::uint32_t, 20> display_glyphs{
        0x0020, 0x0025, 0x002B, 0x002D, 0x002E, 0x002F, 0x0030, 0x0031, 0x0032, 0x0033,
        0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x0043, 0x00B0, 0x2212,
    };
    for (const auto codepoint : display_glyphs) {
        lv_font_glyph_dsc_t glyph{};
        expect(lv_font_get_glyph_dsc(aura::ui::font_display(), &glyph, codepoint, 0) &&
                   glyph.resolved_font == aura::ui::font_display(),
               "IBM Plex Light 72 numeric glyph exists");
    }
    constexpr std::array<std::uint32_t, 13> clock_glyphs{
        0x0020, 0x002D, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A,
    };
    for (const auto codepoint : clock_glyphs) {
        lv_font_glyph_dsc_t glyph{};
        expect(lv_font_get_glyph_dsc(aura::ui::font_clock(), &glyph, codepoint, 0) &&
                   glyph.resolved_font == aura::ui::font_clock(),
               "IBM Plex Light 96 clock glyph exists");
    }
    // Anything outside a subset falls back to the title font, never to a box.
    lv_font_glyph_dsc_t fallback_glyph{};
    expect(lv_font_get_glyph_dsc(aura::ui::font_display(), &fallback_glyph, 0x2014, 0) &&
               fallback_glyph.resolved_font == aura::ui::font_title(),
           "numeric subsets fall back to the title font");

    constexpr std::array<std::uint32_t, 4> brand{0x0041, 0x0055, 0x0052, 0x0020};
    for (const auto codepoint : brand) {
        lv_font_glyph_dsc_t glyph{};
        expect(lv_font_get_glyph_dsc(aura::ui::font_brand(), &glyph, codepoint, 0),
               "Space Grotesk AURA wordmark glyph exists");
    }

    expect_complete_table(aura::l10n::el_gr(), "Strings el_gr");
    expect_complete_table(aura::l10n::en_us(), "Strings en_us");
    expect_complete_table(aura::l10n::visual_el_gr(), "VisualStrings el_gr");
    expect_complete_table(aura::l10n::visual_en_us(), "VisualStrings en_us");
    expect_complete_table(aura::l10n::companion_el_gr(), "CompanionStrings el_gr");
    expect_complete_table(aura::l10n::companion_en_us(), "CompanionStrings en_us");
    expect_no_greek_copy(aura::l10n::en_us(), "Strings en_us");
    const auto& visual_en = aura::l10n::visual_en_us();
    expect_no_greek_copy(visual_en, "VisualStrings en_us",
        {visual_en.typography_sample_1, visual_en.typography_sample_2, visual_en.typography_sample_3,
         visual_en.typography_sample_4, visual_en.typography_sample_5, visual_en.typography_sample_6,
         visual_en.typography_sample_7, visual_en.typography_sample_8});
    expect(std::string_view(aura::l10n::companion_en_us().system_power_off_hint) ==
               "Press and hold the screen to power on" &&
           std::string_view(aura::l10n::companion_en_us().language_change_note) ==
               "The change applies immediately across AURA.",
           "English companion and language settings copy is populated");

    check_appearance_registry();
    check_appearance_fonts();
    check_appearance_icons();
    check_component_kit();
    check_presences();
    check_pearl_presence();
    check_aurora_presence();
    check_stars_presence();

    aura::ui::AuraMercuryView mercury;
    expect(mercury.buffer_bytes() == static_cast<std::size_t>(176 * 176 * 3),
           "Mercury uses bounded RGB565+A8 storage");
    expect(aura::ui::AuraMercuryView::frame_interval_ms == 66,
           "Mercury animation targets approximately 15 FPS");

    std::cout << "ui_assets_test: PASS\n";
    return 0;
}

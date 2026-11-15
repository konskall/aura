#include "aura/aura_appearance.hpp"
#include "aura/aura_frame_worker.hpp"
#include "aura/aura_icons.hpp"
#include "aura/aura_aurora_view.hpp"
#include "aura/aura_mercury_view.hpp"
#include "aura/aura_pearl_view.hpp"
#include "aura/aura_stars_view.hpp"
#include "aura/aura_presence.hpp"
#include "aura/aura_theme.hpp"
#include "aura/aura_ui.hpp"
#include "aura/strings.hpp"
#include "aura/visual_strings.hpp"
#include "aura/companion_strings.hpp"
#include "aura/photo_strings.hpp"
#include "aura/transfer_strings.hpp"
#include "aura_brand_assets.hpp"
#include "src/image/lv_image_decoder_private.h"  // lv_image_decoder_dsc_t, to decode the compressed icons

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void check_aura_logo_asset(const lv_image_dsc_t& asset, int width, int height) {
    expect(asset.header.magic == LV_IMAGE_HEADER_MAGIC && asset.header.cf == LV_COLOR_FORMAT_ARGB8888,
           "Aura logo uses the project's full-colour LVGL format");
    expect(asset.header.w == width && asset.header.h == height &&
               asset.header.stride == static_cast<std::uint32_t>(width * 4),
           "Aura logo keeps its approved aspect ratio at the required size");
    expect(asset.data && asset.data_size == static_cast<std::uint32_t>(width * height * 4),
           "Aura logo is a bounded native image asset");

    bool transparent = false;
    bool partial_alpha = false;
    bool opaque = false;
    bool cyan = false;
    bool blue = false;
    bool violet = false;
    for (std::uint32_t offset = 0; offset < asset.data_size; offset += 4) {
        const auto b = asset.data[offset];
        const auto g = asset.data[offset + 1];
        const auto r = asset.data[offset + 2];
        const auto a = asset.data[offset + 3];
        transparent = transparent || a == 0;
        partial_alpha = partial_alpha || (a > 0 && a < 255);
        opaque = opaque || a == 255;
        if (a > 160) {
            cyan = cyan || (g > r + 20 && b > r + 20);
            blue = blue || (b > r + 20 && b > g + 10);
            violet = violet || (r > g + 15 && b > g + 20);
        }
    }
    const auto last_alpha = asset.data[asset.data_size - 1];
    expect(asset.data[3] == 0 && last_alpha == 0 && transparent && partial_alpha && opaque,
           "Aura logo background, corners and antialiasing preserve transparency");
    expect(cyan && blue && violet, "Aura logo preserves the approved cyan-blue-violet gradient");
}

// Strings/VisualStrings are positional aggregates of `const char*`. Every
// field must be initialized in both languages and every character must exist
// in the UI fonts, or the device would show an empty box.
template <typename Table>
void expect_complete_table(const Table& table, const char* name, const lv_font_t* font = nullptr);

// The printf conversions in `text`, in order, each followed by a space ("%%" is
// literal text, not a conversion).
std::string printf_conversions(const char* text) {
    std::string conversions;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor != '%') continue;
        ++cursor;
        if (*cursor == '%') continue;
        std::string conversion = "%";
        while (*cursor != '\0' && std::strchr("-+ #0123456789.*hlLzjt", *cursor) != nullptr) conversion += *cursor++;
        if (*cursor == '\0') {
            conversions += conversion + ' ';
            break;
        }
        conversions += conversion + *cursor + ' ';
    }
    return conversions;
}

// Every copy has the same printf conversions, in the same order, in both languages.
template <typename Table>
void expect_matching_formats(const Table& greek, const Table& english, const char* name) {
    static_assert(sizeof(Table) % sizeof(const char*) == 0, "string table holds only pointers");
    const auto* greek_fields = reinterpret_cast<const char* const*>(&greek);
    const auto* english_fields = reinterpret_cast<const char* const*>(&english);
    for (std::size_t index = 0; index < sizeof(Table) / sizeof(const char*); ++index) {
        const auto greek_conversions = printf_conversions(greek_fields[index]);
        const auto english_conversions = printf_conversions(english_fields[index]);
        if (greek_conversions != english_conversions) {
            std::cerr << "FAILED: " << name << " field " << index << ": Greek {" << greek_conversions
                      << "} against English {" << english_conversions << "}\n";
            std::exit(1);
        }
    }
}

template <typename Table>
void expect_complete_table(const Table& table, const char* name, const lv_font_t* font) {
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
        // The always-on clock fills the panel (the owner's request on the first
        // flash, 2026-11-14): larger than the 96 px clock, and the widest time,
        // "08:08", still within 420 px of the 480 px screen.
        expect(fonts.ambient_clock != nullptr &&
                   carries(fonts.ambient_clock, {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', ' '}),
               "the always-on clock carries the digits and the colon itself");
        if (fonts.ambient_clock) {
            expect(lv_font_get_line_height(fonts.ambient_clock) > lv_font_get_line_height(fonts.clock),
                   "the always-on clock is larger than the ordinary clock");
            const std::int32_t widest = 4 * lv_font_get_glyph_width(fonts.ambient_clock, '8', '8') +
                                        lv_font_get_glyph_width(fonts.ambient_clock, ':', '0');
            expect(widest <= 420, "the widest always-on time fits the screen with a margin");
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

// The icons are stored compressed with LVGL's RLE. Uncompressed, the four
// families were 1.6 MB of flash (101 images of 64 x 64 bytes each). Each one
// still decodes to a 64 px A8 image; the pointer screenshots check its pixels.
void ensure_lvgl();
void check_icon_compression() {
    namespace ui = aura::ui;
    ensure_lvgl();
    std::vector<const lv_image_dsc_t*> icons;
    for (const auto appearance : aura::kAppearances) {
        for (std::size_t index = 0; index < ui::icon_role_count(); ++index) {
            const auto* icon = ui::icon_asset(static_cast<ui::IconRole>(index), appearance);
            if (icon && std::find(icons.begin(), icons.end(), icon) == icons.end()) icons.push_back(icon);
        }
    }
    std::size_t stored = 0;
    bool compressed = true;
    bool decodes = true;
    for (const auto* icon : icons) {
        stored += icon->data_size;
        compressed = compressed && (icon->header.flags & LV_IMAGE_FLAGS_COMPRESSED) != 0;
        lv_image_decoder_dsc_t decoder{};
        if (lv_image_decoder_open(&decoder, icon, nullptr) != LV_RESULT_OK) {
            decodes = false;
            continue;
        }
        decodes = decodes && decoder.decoded != nullptr && decoder.decoded->header.w == 64 &&
                  decoder.decoded->header.h == 64 && decoder.decoded->header.cf == LV_COLOR_FORMAT_A8;        lv_image_decoder_close(&decoder);
    }
    expect(icons.size() >= 300U, "the four families' icons are all counted");
    expect(compressed, "every icon is stored compressed");
    expect(decodes, "every compressed icon decodes to a 64 px A8 image");
    expect(stored < 384U * 1024U, "the icons of all four appearances take under 384 KB of flash");
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
               ui::metrics_for(Appearance::Halo).hero_center_y == 246,
           "Halo metrics describe today's components");
    // The voice screens' presence sits in the middle of the free band between
    // the state caption (it ends near y = 100) and the action row (y = 380),
    // so Μάρμαρο's ring clears the buttons (the owner's report, 2026-11-14).
    expect(ui::metrics_for(Appearance::Marble).stage_center_y == 240,
           "Μάρμαρο centres its pearl between the caption and the buttons");
    for (const auto appearance : aura::kAppearances) {
        const int stage = ui::metrics_for(appearance).stage_center_y;
        expect(stage >= 230 && stage <= 250, "every look keeps its voice presence near the free band's centre");
    }
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
    expect(aura::ui::make_presence(aura::Appearance::Halo)->buffer_bytes() == 362880U,
           "Halo uses the bounded 480 x 252 Synapse frame");
}

// Μάρμαρο's pearl: a sphere in its historical buffer size plus LVGL rings.
void check_pearl_presence() {
    using aura::ui::PresenceState;
    ensure_lvgl();
    auto* screen = lv_screen_active();
    {
        auto presence = aura::ui::make_presence(aura::Appearance::Marble);
        presence->attach(screen, 240, 272);
        expect(lv_obj_get_child_count(screen) > 1, "Marble's presence is the pearl with its own rings");
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

// Synapse's luminance (Rec. 709) at one frame pixel, in either frame format:
// RGB888 (blue, green, red in memory) or RGB565.
float synapse_luminance(const lv_image_dsc_t& frame, int x, int y) {
    const auto index = static_cast<std::size_t>(y) * frame.header.w + static_cast<std::size_t>(x);
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    if (frame.header.cf == LV_COLOR_FORMAT_RGB888) {
        const auto* pixel = frame.data + index * 3;
        blue = pixel[0];
        green = pixel[1];
        red = pixel[2];
    } else {
        const auto packed = reinterpret_cast<const std::uint16_t*>(frame.data)[index];
        red = static_cast<float>((packed >> 11U) & 0x1FU) * 255.0F / 31.0F;
        green = static_cast<float>((packed >> 5U) & 0x3FU) * 255.0F / 63.0F;
        blue = static_cast<float>(packed & 0x1FU) * 255.0F / 31.0F;
    }
    return red * 0.2126F + green * 0.7152F + blue * 0.0722F;
}

// The approved browser presentation (artifacts/halo-synapse.html), rendered at
// t = 6 s with each state's values settled and a voice amplitude of 0.5 while
// listening and speaking: mean luminance per radial band (0-15, 15-40, 40-80,
// 80-130 and 130-240 px from the centre) and quadrant (top left, top right,
// bottom left, bottom right), counting rows within 100 px of the centre only.
constexpr float kSynapseReference[4][5][4] = {
    {{204.7F, 225.6F, 226.1F, 210.5F}, {91.6F, 91.6F, 116.5F, 80.2F}, {67.2F, 83.8F, 63.9F, 87.8F},
     {45.5F, 38.6F, 29.0F, 45.8F}, {1.9F, 2.3F, 1.5F, 2.8F}},
    {{223.1F, 239.1F, 226.5F, 224.1F}, {78.2F, 95.0F, 69.7F, 116.9F}, {48.4F, 48.5F, 59.5F, 101.0F},
     {31.4F, 68.4F, 75.3F, 45.2F}, {8.1F, 6.8F, 6.4F, 5.8F}},
    {{202.1F, 225.3F, 228.1F, 207.5F}, {92.4F, 118.4F, 99.9F, 102.6F}, {49.4F, 62.9F, 71.6F, 65.0F},
     {39.7F, 62.7F, 50.1F, 46.1F}, {3.5F, 9.3F, 3.3F, 4.5F}},
    {{225.1F, 239.9F, 237.9F, 220.0F}, {82.1F, 95.3F, 137.1F, 76.2F}, {43.3F, 35.2F, 69.6F, 55.2F},
     {49.4F, 39.1F, 51.2F, 58.8F}, {15.2F, 12.2F, 13.2F, 9.1F}},
};

// A clock for the Synapse checks: each reading advances it by `synapse_draw_ms`,
// and Synapse reads it once before and once after drawing a frame, so every
// frame takes exactly that long to draw.
std::uint32_t synapse_clock_ms = 0;
std::uint32_t synapse_draw_ms = 0;
std::uint32_t synapse_test_clock() noexcept {
    const auto now = synapse_clock_ms;
    synapse_clock_ms += synapse_draw_ms;
    return now;
}
// The checks' own Synapse: it draws every frame instantly (a real clock on a
// busy machine could make one frame slow and switch it to the reduced frame).
std::unique_ptr<aura::ui::AuraMercuryView> instant_synapse() {
    synapse_draw_ms = 0;
    return std::make_unique<aura::ui::AuraMercuryView>(synapse_test_clock, nullptr);
}

void check_synapse() {
    using aura::ui::AuraMercuryView;
    using aura::ui::PresenceState;
    ensure_lvgl();
    auto* screen = lv_screen_active();

    // The approved presentation at 1:1 scale: the full width of the screen,
    // between the assistant captions (above y = 120) and the controls (y >= 372).
    expect(AuraMercuryView::width == 480 && AuraMercuryView::height == 252,
           "Synapse draws the approved presentation at 1:1 in a 480 x 252 frame");
    expect(AuraMercuryView::frame_interval_ms == 50,
           "Synapse animation targets 20 FPS for presentation-grade motion");

    auto* caption = lv_label_create(screen);
    auto synapse = instant_synapse();
    expect(synapse->buffer_bytes() == static_cast<std::size_t>(480 * 252 * 3),
           "Synapse keeps one bounded 480 x 252 x 3 byte frame");
    auto* synapse_object = synapse->attach(screen, 240, 246);
    lv_obj_update_layout(screen);
    lv_area_t area{};
    lv_obj_get_coords(synapse_object, &area);
    expect(area.x1 == 0 && area.x2 == 479 && area.y1 == 120 && area.y2 == 371,
           "Synapse spans the screen width between the captions and the controls");
    expect(lv_obj_get_index(synapse_object) == 0 && lv_obj_get_index(caption) == 1,
           "Synapse lies behind every label and control, even ones made before it");
    const auto* synapse_source = static_cast<const lv_image_dsc_t*>(lv_image_get_src(synapse_object));
    expect(synapse_source && synapse_source->header.w == 480 && synapse_source->header.h == 252 &&
               (synapse_source->header.cf == LV_COLOR_FORMAT_RGB888 ||
                synapse_source->header.cf == LV_COLOR_FORMAT_RGB565),
           "Synapse is an opaque light field on the black canvas, without an alpha plane");
    synapse->detach();
    lv_obj_clean(screen);

    // Every state matches the approved presentation's light, band by band.
    const PresenceState states[] = {PresenceState::Idle, PresenceState::Listening, PresenceState::Thinking,
                                    PresenceState::Speaking};
    const char* names[] = {"idle", "listening", "thinking", "speaking"};
    for (int state = 0; state < 4; ++state) {
        auto view = instant_synapse();
        view->set_state(states[state]);
        view->set_level(state == 1 || state == 3 ? 0.5F : 0.0F);
        auto* image = view->attach(screen, 240, 246);
        for (int step = 0; step < 120; ++step) view->tick(50);
        const auto& frame = *static_cast<const lv_image_dsc_t*>(lv_image_get_src(image));
        std::array<std::array<float, 4>, 5> totals{};
        std::array<std::array<int, 4>, 5> counts{};
        constexpr float bands[] = {0.0F, 15.0F, 40.0F, 80.0F, 130.0F, 240.0F};
        for (int y = 0; y < AuraMercuryView::height; ++y) {
            for (int x = 0; x < AuraMercuryView::width; ++x) {
                const float dx = static_cast<float>(x) + 0.5F - 240.0F;
                const float dy = static_cast<float>(y) + 0.5F - 126.0F;
                if (std::abs(dy) > 100.0F) continue;
                const float distance = std::hypot(dx, dy);
                int band = 0;
                while (band < 5 && distance >= bands[band + 1]) ++band;
                if (band == 5) continue;
                const int quadrant = (dx >= 0.0F ? 1 : 0) + (dy >= 0.0F ? 2 : 0);
                totals[static_cast<std::size_t>(band)][static_cast<std::size_t>(quadrant)] +=
                    synapse_luminance(frame, x, y);
                ++counts[static_cast<std::size_t>(band)][static_cast<std::size_t>(quadrant)];
            }
        }
        for (int band = 0; band < 5; ++band) {
            for (int quadrant = 0; quadrant < 4; ++quadrant) {
                const float mean = totals[static_cast<std::size_t>(band)][static_cast<std::size_t>(quadrant)] /
                                   static_cast<float>(counts[static_cast<std::size_t>(band)]
                                                            [static_cast<std::size_t>(quadrant)]);
                const float reference = kSynapseReference[state][band][quadrant];
                if (std::abs(mean - reference) > reference * 0.15F + 4.0F) {
                    std::cerr << "Synapse " << names[state] << " band " << band << " quadrant " << quadrant
                              << ": " << mean << " against " << reference << '\n';
                    expect(false, "Synapse matches the approved presentation's light in every state");
                }
            }
        }
        view->detach();
        lv_obj_clean(screen);
    }

    // Distinct states, and the listening lattice follows the voice level.
    auto moving = instant_synapse();
    moving->attach(screen, 240, 246);
    std::array<std::uint32_t, 4> signatures{};
    for (int state = 0; state < 4; ++state) {
        moving->set_state(states[state]);
        moving->set_level(0.72F);
        for (int step = 0; step < 8; ++step) moving->tick(50);
        signatures[static_cast<std::size_t>(state)] = moving->frame_signature();
    }
    expect(signatures[0] != signatures[1] && signatures[1] != signatures[2] && signatures[2] != signatures[3] &&
               signatures[0] != signatures[3],
           "every assistant state has a distinct Synapse frame");
    moving->set_state(PresenceState::Listening);
    moving->set_level(0.0F);
    for (int step = 0; step < 7; ++step) moving->tick(50);
    const auto quiet_signature = moving->frame_signature();
    moving->set_level(1.0F);
    for (int step = 0; step < 7; ++step) moving->tick(50);
    expect(moving->frame_signature() != quiet_signature, "the listening lattice follows the voice level");
    moving->set_reduced_motion(true);
    const auto frozen = moving->frame_count();
    for (int step = 0; step < 4; ++step) moving->tick(50);
    expect(moving->frame_count() == frozen, "Reduced Motion freezes Synapse");
    moving->detach();
    lv_obj_clean(screen);
}


// The mean difference, per colour channel, of two Synapse frames.
float synapse_difference(const lv_image_dsc_t& left, const lv_image_dsc_t& right) {
    float total = 0.0F;
    for (int y = 0; y < aura::ui::AuraMercuryView::height; ++y) {
        for (int x = 0; x < aura::ui::AuraMercuryView::width; ++x) {
            total += std::abs(synapse_luminance(left, x, y) - synapse_luminance(right, x, y));
        }
    }
    return total / static_cast<float>(aura::ui::AuraMercuryView::width * aura::ui::AuraMercuryView::height);
}

// How sharp a frame is: the mean difference in light between neighbouring
// pixels, across and down. Blurred lines spread it out.
float synapse_sharpness(const lv_image_dsc_t& frame) {
    float total = 0.0F;
    for (int y = 0; y + 1 < aura::ui::AuraMercuryView::height; ++y) {
        for (int x = 0; x + 1 < aura::ui::AuraMercuryView::width; ++x) {
            const float here = synapse_luminance(frame, x, y);
            total += std::abs(here - synapse_luminance(frame, x + 1, y)) +
                     std::abs(here - synapse_luminance(frame, x, y + 1));
        }
    }
    return total / static_cast<float>((aura::ui::AuraMercuryView::width - 1) * (aura::ui::AuraMercuryView::height - 1));
}

// Synapse never takes more than a third of the time it draws in, on any device:
// a slower one draws fewer, then lighter frames, and with a worker the UI thread
// does not draw at all.
void check_synapse_pacing() {
    using aura::ui::AuraMercuryView;
    using aura::ui::FrameWorker;
    using aura::ui::SynapseQuality;
    ensure_lvgl();
    auto* screen = lv_screen_active();

    // Idle time after a frame: the rest of its 50 ms, and at least twice the drawing.
    expect(AuraMercuryView::paced_wait(0) == 50 && AuraMercuryView::paced_wait(10) == 40,
           "a fast frame keeps Synapse at 20 FPS");
    expect(AuraMercuryView::paced_wait(30) == 60, "a 30 ms frame leaves twice its drawing time idle");
    expect(AuraMercuryView::paced_wait(150) == 300,
           "a slow frame leaves twice its drawing time idle, with no frame-rate floor that would starve the UI");
    expect(AuraMercuryView::paced_wait(5000) == 2000, "an extreme stall still lets Synapse move every 3 s");
    // On a worker the UI thread never draws: the board's worker draws below
    // every other task of its core, so Synapse may draw four fifths of the
    // time there. Waiting as long as it drew halved the board's frames (2.3 a
    // second at ~205 ms, 2026-11-15).
    expect(AuraMercuryView::worker_wait(0) == 50 && AuraMercuryView::worker_wait(30) == 20 &&
               AuraMercuryView::worker_wait(150) == 37 && AuraMercuryView::worker_wait(5000) == 250,
           "on a worker, Synapse draws at most four fifths of the time");
    expect(AuraMercuryView::next_quality(SynapseQuality::Full, AuraMercuryView::reduce_above_ms) ==
               SynapseQuality::Full &&
               AuraMercuryView::next_quality(SynapseQuality::Full, AuraMercuryView::reduce_above_ms + 1) ==
               SynapseQuality::Reduced,
           "a full frame slower than the limit switches Synapse to the reduced frame");
    expect(AuraMercuryView::next_quality(SynapseQuality::Reduced, AuraMercuryView::restore_below_ms) ==
               SynapseQuality::Reduced &&
               AuraMercuryView::next_quality(SynapseQuality::Reduced, AuraMercuryView::restore_below_ms - 1) ==
               SynapseQuality::Full,
           "a reduced frame well inside the limit restores the full frame");
    expect(AuraMercuryView::restore_below_ms * 2 < AuraMercuryView::reduce_above_ms,
           "the two limits are far enough apart that the quality does not flap");

    // The firmware's loop: it sleeps 33 ms between ticks, and each tick reports the
    // time since the previous one, drawing on the same thread included.
    synapse_draw_ms = 150;  // the ESP32-S3 estimate for one frame
    auto slow = std::make_unique<AuraMercuryView>(synapse_test_clock, nullptr);
    slow->attach(screen, 240, 246);
    const auto first_frame = slow->frame_count();
    std::uint64_t total_ms = 0;
    std::uint64_t drawing_ms = 0;
    std::uint32_t elapsed_ms = 33;
    for (int tick = 0; tick < 600; ++tick) {
        const auto frames = slow->frame_count();
        slow->tick(elapsed_ms);
        const std::uint32_t drew_ms = slow->frame_count() > frames ? synapse_draw_ms : 0;
        drawing_ms += drew_ms;
        elapsed_ms = 33 + drew_ms;
        total_ms += elapsed_ms;
    }
    const double duty = static_cast<double>(drawing_ms) / static_cast<double>(total_ms);
    if (duty > 0.34) std::cerr << "Synapse drew for " << duty * 100.0 << " % of the time\n";
    expect(duty <= 0.34, "drawing Synapse takes at most a third of the time on a slow device");
    expect(slow->frame_count() >= first_frame + 20, "a slow device still animates Synapse");
    expect(slow->quality() == SynapseQuality::Reduced, "a device too slow for the full frame draws the reduced one");
    slow->detach();
    lv_obj_clean(screen);

    // The reduced frame keeps the look for less work. Between the two limits
    // each view keeps the quality it has.
    synapse_draw_ms = 60;
    auto full = std::make_unique<AuraMercuryView>(synapse_test_clock, nullptr);
    auto reduced = std::make_unique<AuraMercuryView>(synapse_test_clock, nullptr);
    reduced->set_quality(SynapseQuality::Reduced);
    auto* full_image = full->attach(screen, 240, 246);
    auto* reduced_image = reduced->attach(screen, 240, 246);
    for (int step = 0; step < 20; ++step) {
        full->tick(50);
        reduced->tick(50);
    }
    const float difference = synapse_difference(*static_cast<const lv_image_dsc_t*>(lv_image_get_src(full_image)),
                                                 *static_cast<const lv_image_dsc_t*>(lv_image_get_src(reduced_image)));
    if (difference > 3.0F) std::cerr << "reduced Synapse differs by " << difference << " per pixel\n";
    expect(full->quality() == SynapseQuality::Full && reduced->quality() == SynapseQuality::Reduced,
           "a frame time between the two limits keeps the quality");
    expect(difference <= 3.0F, "the reduced frame looks like the full one");
    if (reduced->frame_work() * 3 > full->frame_work() * 2) {
        std::cerr << "reduced Synapse work " << reduced->frame_work() << " against " << full->frame_work() << '\n';
    }
    expect(reduced->frame_work() * 3 <= full->frame_work() * 2,
           "the reduced frame takes at most two thirds of the work");
    full->detach();
    reduced->detach();
    lv_obj_clean(screen);

    // With a worker, the caller only hands frames over: the worker draws them and
    // the next tick shows the finished one.
    FrameWorker worker;
    auto async = std::make_unique<AuraMercuryView>(nullptr, &worker);
    expect(async->buffer_bytes() == static_cast<std::size_t>(2 * 480 * 252 * 3),
           "a view with a worker shows one frame while the worker draws the next");
    auto* async_image = async->attach(screen, 240, 246);
    const auto attached_frames = async->frame_count();
    const auto* shown_before = static_cast<const lv_image_dsc_t*>(lv_image_get_src(async_image))->data;
    async->tick(50);
    expect(async->frame_count() == attached_frames, "the tick that starts a frame does not draw it");
    worker.wait_idle();
    async->tick(1);
    const auto* shown_after = static_cast<const lv_image_dsc_t*>(lv_image_get_src(async_image))->data;
    expect(async->frame_count() == attached_frames + 1 && shown_after != shown_before,
           "the next tick shows the frame the worker drew");
    async->tick(50);
    async.reset();  // while the worker may still draw: the view waits for it
    expect(!worker.busy(), "a view never leaves the worker drawing into freed memory");
    lv_obj_clean(screen);

    // A return to a Synapse screen, with a worker: the last frame shows at once
    // and the worker draws the next. On the board a frame takes ~330 ms, and
    // drawing it in attach() held the display lock and froze touch on every
    // return (the first flash, 2026-11-14).
    auto returning = std::make_unique<AuraMercuryView>(nullptr, &worker);
    returning->attach(screen, 240, 246);
    expect(returning->frame_count() == 1, "the first visit draws its frame at once, never blank");
    returning->detach();
    lv_obj_clean(screen);
    returning->attach(screen, 240, 246);
    expect(returning->frame_count() == 0, "a return shows the last frame and does not draw on the caller's thread");
    worker.wait_idle();
    returning->tick(1);
    expect(returning->frame_count() == 1, "the next tick shows the frame the worker drew");
    returning.reset();
    expect(!worker.busy(), "a returned view never leaves the worker drawing into freed memory");
    lv_obj_clean(screen);
}

// The board's loop sleeps between its passes (33 ms and its own work): Synapse
// waited there for its frame to be seen, and again for the next to begin,
// drawing 6.9 frames a second from 85 ms frames (measured 2026-11-15). The
// worker now says when it has drawn one, and Synapse when it wants its tick.
void check_synapse_next_tick() {
    using aura::ui::AuraMercuryView;
    using aura::ui::FrameWorker;
    using aura::ui::PresenceView;
    ensure_lvgl();
    auto* screen = lv_screen_active();

    FrameWorker worker;
    std::atomic<int> finished{0};
    worker.set_finished([](void* count) { static_cast<std::atomic<int>*>(count)->fetch_add(1); }, &finished);
    expect(worker.submit([](void*) {}, nullptr), "the worker takes a job");
    for (int wait = 0; wait < 1000 && finished.load() == 0; ++wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(finished.load() == 1 && !worker.busy(), "the worker says when it has finished a job, once it is free");

    auto view = std::make_unique<AuraMercuryView>(nullptr, &worker);
    expect(view->next_tick_ms() == PresenceView::no_tick, "a view not shown wants no tick");
    view->attach(screen, 240, 246);
    expect(view->next_tick_ms() == 0, "after a visit's first frame the next begins on the first tick");
    view->tick(0);
    worker.wait_idle();
    for (int wait = 0; wait < 1000 && finished.load() < 2; ++wait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(view->next_tick_ms() == 0, "a frame the worker has drawn wants its tick at once");
    const auto frames = view->frame_count();
    view->tick(0);
    const std::uint32_t waiting = view->next_tick_ms();
    expect(view->frame_count() == frames + 1 && waiting > 0 && waiting != PresenceView::no_tick,
           "that tick shows it, and the next frame waits out its own rest");
    view->tick(waiting - 1);
    expect(view->next_tick_ms() == 1, "the wait counts down with the ticks");
    view->tick(1);
    expect(view->frame_count() == frames + 1, "the tick that ends the wait begins the next frame");
    worker.wait_idle();
    view->set_reduced_motion(true);
    expect(view->next_tick_ms() == PresenceView::no_tick, "with reduced motion Synapse wants no tick");
    view.reset();
    lv_obj_clean(screen);

    // Without a worker (the simulator) the UI's own 33 ms set the pace.
    auto alone = std::make_unique<AuraMercuryView>(nullptr, nullptr);
    alone->attach(screen, 240, 246);
    expect(alone->next_tick_ms() == PresenceView::no_tick, "a view drawing on the UI thread asks for no early tick");
    alone.reset();
    lv_obj_clean(screen);
}

// The board's Synapse (the owner's request after the first flash, 2026-11-14:
// it took ~340 ms a frame there): the light gathered at half resolution and
// scaled up smoothly, without the orbit traces and the web between the paths,
// and with half the sparks. It keeps the presentation's picture, a little
// softer, for under 30 % of the work.
void check_synapse_device_profile() {
    using aura::ui::AuraMercuryView;
    using aura::ui::PresenceState;
    using aura::ui::SynapseProfile;
    ensure_lvgl();
    auto* screen = lv_screen_active();

    expect(instant_synapse()->profile() == SynapseProfile::Presentation &&
               std::make_unique<AuraMercuryView>()->profile() == SynapseProfile::Presentation,
           "the simulator draws the approved presentation in full");

    const PresenceState states[] = {PresenceState::Idle, PresenceState::Listening, PresenceState::Thinking,
                                    PresenceState::Speaking};
    const char* names[] = {"idle", "listening", "thinking", "speaking"};
    for (int state = 0; state < 4; ++state) {
        auto presentation = instant_synapse();
        auto device = instant_synapse();
        device->set_profile(SynapseProfile::Device);
        expect(device->profile() == SynapseProfile::Device, "the board's profile can be chosen");
        for (auto* view : {presentation.get(), device.get()}) {
            view->set_state(states[state]);
            view->set_level(state == 1 || state == 3 ? 0.5F : 0.0F);
        }
        auto* presentation_image = presentation->attach(screen, 240, 246);
        auto* device_image = device->attach(screen, 240, 246);
        for (int step = 0; step < 120; ++step) {
            presentation->tick(50);
            device->tick(50);
        }
        const auto& shown = *static_cast<const lv_image_dsc_t*>(lv_image_get_src(device_image));
        const auto& full = *static_cast<const lv_image_dsc_t*>(lv_image_get_src(presentation_image));
        expect(shown.header.w == 480 && shown.header.h == 252 && shown.header.cf == LV_COLOR_FORMAT_RGB565,
               "the device frame fills the same 480 x 252 frame, in the board's RGB565 on every display");
        // The presentation's light, band by band, with room for the softer
        // scale-up and the layers left out.
        std::array<std::array<float, 4>, 5> totals{};
        std::array<std::array<int, 4>, 5> counts{};
        constexpr float bands[] = {0.0F, 15.0F, 40.0F, 80.0F, 130.0F, 240.0F};
        for (int y = 0; y < AuraMercuryView::height; ++y) {
            for (int x = 0; x < AuraMercuryView::width; ++x) {
                const float dx = static_cast<float>(x) + 0.5F - 240.0F;
                const float dy = static_cast<float>(y) + 0.5F - 126.0F;
                if (std::abs(dy) > 100.0F) continue;
                const float distance = std::hypot(dx, dy);
                int band = 0;
                while (band < 5 && distance >= bands[band + 1]) ++band;
                if (band == 5) continue;
                const int quadrant = (dx >= 0.0F ? 1 : 0) + (dy >= 0.0F ? 2 : 0);
                totals[static_cast<std::size_t>(band)][static_cast<std::size_t>(quadrant)] +=
                    synapse_luminance(shown, x, y);
                ++counts[static_cast<std::size_t>(band)][static_cast<std::size_t>(quadrant)];
            }
        }
        for (int band = 0; band < 5; ++band) {
            for (int quadrant = 0; quadrant < 4; ++quadrant) {
                const float mean = totals[static_cast<std::size_t>(band)][static_cast<std::size_t>(quadrant)] /
                                   static_cast<float>(counts[static_cast<std::size_t>(band)]
                                                            [static_cast<std::size_t>(quadrant)]);
                const float reference = kSynapseReference[state][band][quadrant];
                if (std::abs(mean - reference) > reference * 0.25F + 6.0F) {
                    std::cerr << "device Synapse " << names[state] << " band " << band << " quadrant " << quadrant
                              << ": " << mean << " against " << reference << '\n';
                    expect(false, "the device frame keeps the presentation's light in every state");
                }
            }
        }
        // Measured: 3.9 (idle) to 5.3 (speaking) levels, mostly the sharp
        // lines one pixel across and the RGB565 dither; a misplaced or missing
        // layer differs far more.
        const float difference = synapse_difference(shown, full);
        if (difference > 6.5F) std::cerr << "device Synapse " << names[state] << " differs by " << difference << '\n';
        expect(difference <= 6.5F, "the device frame looks like the presentation");
        // Its lines are as sharp as the presentation's (the owner found the
        // first device frame, gathered at half resolution, blurred: it had
        // ~55 % of the presentation's sharpness; this one 127-134 %, its
        // dither included).
        const float sharpness = synapse_sharpness(shown);
        const float reference_sharpness = synapse_sharpness(full);
        if (sharpness < reference_sharpness * 0.9F) {
            std::cerr << "device Synapse " << names[state] << " sharpness " << sharpness << " against "
                      << reference_sharpness << '\n';
        }
        expect(sharpness >= reference_sharpness * 0.9F, "the device frame's lines are not blurred");
        // Every pixel at full resolution, the lattice's halo from every fourth
        // step: 47-50 % of the presentation's pixels (measured 2026-11-15),
        // all of them in fast memory on the board. The presentation's whole
        // lattice would be 100 %.
        if (device->frame_work() * 10 > presentation->frame_work() * 6) {
            std::cerr << "device Synapse work " << device->frame_work() << " against "
                      << presentation->frame_work() << '\n';
        }
        expect(device->frame_work() * 10 <= presentation->frame_work() * 6,
               "the device frame takes under 60 % of the presentation's work");
        presentation->detach();
        device->detach();
        lv_obj_clean(screen);
    }

    // It still answers the voice: the resonance ticks are the voice meter.
    auto meter = instant_synapse();
    meter->set_profile(SynapseProfile::Device);
    meter->set_state(PresenceState::Listening);
    meter->attach(screen, 240, 246);
    for (int step = 0; step < 7; ++step) meter->tick(50);
    const auto quiet = meter->frame_signature();
    meter->set_level(1.0F);
    for (int step = 0; step < 7; ++step) meter->tick(50);
    expect(meter->frame_signature() != quiet, "the device frame follows the voice level");
    meter->detach();
    lv_obj_clean(screen);
}

// The board's square root inside the device frame's loops (its sqrtf runs from
// flash): close enough that no pixel's light moves by a level.
void check_synapse_estimated_root() {
    using aura::ui::AuraMercuryView;
    expect(AuraMercuryView::estimated_root(0.0F) == 0.0F && AuraMercuryView::estimated_root(-4.0F) == 0.0F,
           "the board's square root of nothing is nothing");
    float worst = 0.0F;
    for (float value = 1e-4F; value < 3.0e5F; value *= 1.0137F) {
        const float exact = std::sqrt(value);
        worst = std::max(worst, std::abs(AuraMercuryView::estimated_root(value) - exact) / exact);
    }
    if (worst > 1e-5F) std::cerr << "estimated root: relative error " << worst << '\n';
    expect(worst <= 1e-5F, "the board's square root lies within 1e-5 of the root");
}

// The board's reciprocal and inverse root inside the device frame's loops (it
// divides floats in a call into ROM): no pixel's light moves by a level.
void check_synapse_estimated_reciprocal() {
    using aura::ui::AuraMercuryView;
    float worst = 0.0F;
    for (float value = 1e-6F; value < 1e6F; value *= 1.0137F) {
        for (const float sign : {1.0F, -1.0F}) {
            const float exact = 1.0F / (sign * value);
            worst = std::max(worst, std::abs(AuraMercuryView::estimated_reciprocal(sign * value) - exact) /
                                        std::abs(exact));
        }
    }
    if (worst > 1e-6F) std::cerr << "estimated reciprocal: relative error " << worst << '\n';
    expect(worst <= 1e-6F, "the board's reciprocal lies within 1e-6 of it, of either sign");
    float worst_root = 0.0F;
    for (float value = 1e-4F; value < 3.0e5F; value *= 1.0137F) {
        const float exact = 1.0F / std::sqrt(value);
        worst_root = std::max(worst_root, std::abs(AuraMercuryView::estimated_inverse_root(value) - exact) / exact);
    }
    if (worst_root > 1e-5F) std::cerr << "estimated inverse root: relative error " << worst_root << '\n';
    expect(worst_root <= 1e-5F && AuraMercuryView::estimated_inverse_root(0.0F) == 0.0F &&
               AuraMercuryView::estimated_inverse_root(-4.0F) == 0.0F,
           "the board's inverse root lies within 1e-5 of it, and nothing for nothing");
}

// The device frame drawn six rows at a time (for the panel) is exactly the
// frame drawn in one piece: no shape is lost where two strips meet, so no line
// shows a seam every six rows.
void check_synapse_strips_seamless() {
    using aura::ui::AuraMercuryView;
    using aura::ui::PresenceState;
    using aura::ui::SynapseProfile;
    ensure_lvgl();
    auto* screen = lv_screen_active();
    for (const auto state : {PresenceState::Idle, PresenceState::Listening, PresenceState::Speaking}) {
        auto strips = instant_synapse();
        auto whole = instant_synapse();
        whole->set_strip_height(AuraMercuryView::height);
        for (auto* view : {strips.get(), whole.get()}) {
            view->set_profile(SynapseProfile::Device);
            view->set_state(state);
            view->set_level(0.7F);
        }
        auto* strips_image = strips->attach(screen, 240, 246);
        auto* whole_image = whole->attach(screen, 240, 246);
        const auto& strips_frame = *static_cast<const lv_image_dsc_t*>(lv_image_get_src(strips_image));
        const auto& whole_frame = *static_cast<const lv_image_dsc_t*>(lv_image_get_src(whole_image));
        for (int step = 0; step < 12; ++step) {
            strips->tick(50);
            whole->tick(50);
            expect(std::memcmp(strips_frame.data, whole_frame.data, strips_frame.data_size) == 0 &&
                       strips->frame_work() == whole->frame_work(),
                   "the device frame drawn in six-row strips is exactly the frame drawn whole");
        }
        strips->detach();
        whole->detach();
        lv_obj_clean(screen);
    }
}

// A panel for Synapse's direct output: it keeps every strip sent, and can hold
// the worker inside lock() until the check lets it go on.
class FakePanel final : public aura::ui::SynapseOutput {
public:
    struct Strip {
        int left;
        int top;
        int columns;
        int rows;
        bool locked;
        std::vector<std::uint8_t> pixels;
    };

    void* strip(int index, std::size_t bytes) override {
        auto& memory = memory_[static_cast<std::size_t>(index & 3)];
        if (memory.size() < bytes) memory.resize(bytes);
        return memory.data();
    }
    bool lock(std::uint32_t) override {
        {
            std::unique_lock<std::mutex> guard(gate_);
            waiting_ = true;
            changed_.notify_all();
            changed_.wait(guard, [this] { return open_; });
            waiting_ = false;
        }
        // LVGL's lock: one sender at a time (the worker and its helper both send).
        exclusive_.lock();
        locked_ = true;
        return true;
    }
    void unlock() override {
        locked_ = false;
        exclusive_.unlock();
    }
    bool send(int left, int top, int columns, int rows, const void* pixels) override {
        const auto* bytes = static_cast<const std::uint8_t*>(pixels);
        const auto size = static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows) * 2U;
        sent_.push_back({left, top, columns, rows, locked_, std::vector<std::uint8_t>(bytes, bytes + size)});
        return true;
    }
    // The next lock() waits until open(); held() returns once the worker waits there.
    void hold() {
        std::lock_guard<std::mutex> guard(gate_);
        open_ = false;
    }
    void held() {
        std::unique_lock<std::mutex> guard(gate_);
        changed_.wait(guard, [this] { return waiting_; });
    }
    void open() {
        {
            std::lock_guard<std::mutex> guard(gate_);
            open_ = true;
        }
        changed_.notify_all();
    }
    // Read once the worker is idle.
    const std::vector<Strip>& sent() const { return sent_; }

private:
    std::array<std::vector<std::uint8_t>, 4> memory_;
    std::vector<Strip> sent_;
    std::mutex exclusive_;
    std::mutex gate_;
    std::condition_variable changed_;
    bool open_{true};
    bool waiting_{false};
    bool locked_{false};
};

// One frame on the worker: Synapse hands it over, the worker draws it, the next
// tick shows it, and LVGL draws what was invalidated.
void synapse_worker_frame(aura::ui::AuraMercuryView& view, aura::ui::FrameWorker& worker) {
    view.tick(50);
    worker.wait_idle();
    view.tick(0);
    lv_refr_now(nullptr);
}

// Whether the last of `frames` frames on the worker went to the panel straight
// (the first after a change waits for LVGL to draw it).
bool synapse_goes_straight(aura::ui::AuraMercuryView& view, aura::ui::FrameWorker& worker, int frames = 2) {
    for (int step = 1; step < frames; ++step) synapse_worker_frame(view, worker);
    const auto direct = view.direct_frames();
    synapse_worker_frame(view, worker);
    return view.direct_frames() == direct + 1;
}

// The strips of the last direct frame: its middle rows, each once, under the
// lock, and exactly the frame shown with each pixel's high byte first (a
// worker and its helper send theirs in turns).
bool synapse_strips_match(const FakePanel& panel, std::size_t first, const lv_image_dsc_t& frame, int top) {
    using aura::ui::AuraMercuryView;
    constexpr int strips = (AuraMercuryView::direct_bottom - AuraMercuryView::direct_top) / AuraMercuryView::strip_rows;
    if (panel.sent().size() != first + strips) return false;
    std::vector<const FakePanel::Strip*> sent;
    for (std::size_t index = first; index < panel.sent().size(); ++index) sent.push_back(&panel.sent()[index]);
    std::sort(sent.begin(), sent.end(), [](const auto* left, const auto* right) { return left->top < right->top; });
    const auto* pixels = reinterpret_cast<const std::uint16_t*>(frame.data);
    for (int index = 0; index < strips; ++index) {
        const auto& strip = *sent[static_cast<std::size_t>(index)];
        const int row = AuraMercuryView::direct_top + index * AuraMercuryView::strip_rows;
        if (strip.left != 0 || strip.top != top + row || strip.columns != AuraMercuryView::width ||
            strip.rows != AuraMercuryView::strip_rows || !strip.locked) {
            return false;
        }
        for (int y = 0; y < AuraMercuryView::strip_rows; ++y) {
            for (int x = 0; x < AuraMercuryView::width; ++x) {
                const std::uint16_t shown = pixels[static_cast<std::size_t>((row + y) * AuraMercuryView::width + x)];
                const auto at = static_cast<std::size_t>((y * AuraMercuryView::width + x) * 2);
                if (strip.pixels[at] != (shown >> 8) || strip.pixels[at + 1] != (shown & 0xFFU)) return false;
            }
        }
    }
    return true;
}

// The board's Synapse sends the middle rows of each frame to the panel itself,
// strip by strip while it draws them, where nothing LVGL draws lies over them:
// LVGL then draws only the band's top and bottom rows (it spent 47-72 ms on
// each of Synapse's frames, 2026-11-14). Anything LVGL draws over those rows
// keeps the whole frame for LVGL.
void check_synapse_direct_output() {
    using aura::ui::AuraMercuryView;
    using aura::ui::SynapseProfile;
    ensure_lvgl();
    auto* screen = lv_screen_active();
    FakePanel panel;
    aura::ui::set_synapse_output(&panel);
    aura::ui::FrameWorker worker;
    synapse_draw_ms = 0;
    auto view = std::make_unique<AuraMercuryView>(synapse_test_clock, &worker);
    view->set_profile(SynapseProfile::Device);
    view->set_state(aura::ui::PresenceState::Speaking);
    view->set_level(0.5F);
    auto* image = view->attach(screen, 240, 246);
    // The same frames drawn on the ticking thread, which never sends: drawn
    // behind the first, so it never lies over it.
    auto plain = instant_synapse();
    plain->set_profile(SynapseProfile::Device);
    plain->set_state(aura::ui::PresenceState::Speaking);
    plain->set_level(0.5F);
    auto* plain_image = plain->attach(screen, 240, 246);
    lv_refr_now(nullptr);
    const auto& frame = *static_cast<const lv_image_dsc_t*>(lv_image_get_src(image));
    const auto& plain_frame = *static_cast<const lv_image_dsc_t*>(lv_image_get_src(plain_image));

    // The first frame on the worker goes to LVGL whole (Synapse was just
    // shown); after it LVGL has drawn the band, and the rows go straight.
    synapse_worker_frame(*view, worker);
    plain->tick(50);
    lv_refr_now(nullptr);
    expect(panel.sent().empty() && view->direct_frames() == 0, "a frame just shown goes to LVGL whole");
    for (int step = 0; step < 3; ++step) {
        const std::size_t before = panel.sent().size();
        synapse_worker_frame(*view, worker);
        expect(synapse_strips_match(panel, before, frame, 120),
               "Synapse sends its middle rows to the panel, strip by strip, exactly as shown");
        // The plain frame's own redraw lies under the rows too: LVGL draws it
        // before the next frame goes.
        plain->tick(50);
        lv_refr_now(nullptr);
    }
    expect(view->direct_frames() == 3, "every frame after the first goes to the panel straight");
    expect(std::memcmp(frame.data, plain_frame.data, frame.data_size) == 0,
           "the frame shown is the same whether its rows went to the panel or not");

    // A label over those rows keeps the frames for LVGL; above them it does not.
    auto* label = lv_label_create(screen);
    lv_label_set_text(label, "AURA");
    lv_obj_set_pos(label, 100, 220);
    auto before = panel.sent().size();
    expect(!synapse_goes_straight(*view, worker, 3) && panel.sent().size() == before,
           "a label over the middle rows keeps Synapse's frames for LVGL");
    lv_obj_set_pos(label, 100, 124);
    expect(synapse_goes_straight(*view, worker), "a label over the band's top lets the middle rows go straight");
    lv_obj_set_pos(label, 100, 220);
    lv_obj_set_hidden(label, true);
    expect(synapse_goes_straight(*view, worker), "a hidden label lies over nothing");
    lv_obj_delete(label);

    // Home's talk button lies over the halo: clear until it is pressed.
    auto* talk = lv_button_create(screen);
    lv_obj_remove_style_all(talk);
    lv_obj_set_style_bg_opa(talk, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(talk, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_size(talk, 224, 224);
    lv_obj_set_pos(talk, 128, 148);
    expect(synapse_goes_straight(*view, worker), "a clear button over the halo lets the rows go straight");
    lv_obj_add_state(talk, LV_STATE_PRESSED);
    before = panel.sent().size();
    expect(!synapse_goes_straight(*view, worker, 3) && panel.sent().size() == before,
           "the pressed button's veil keeps the frames for LVGL");
    lv_obj_remove_state(talk, LV_STATE_PRESSED);
    expect(synapse_goes_straight(*view, worker), "released, the rows go straight again");

    // Invalidated while the worker draws: the rest of the frame goes to LVGL.
    panel.hold();
    before = panel.sent().size();
    const auto direct = view->direct_frames();
    view->tick(50);
    panel.held();
    lv_obj_invalidate(talk);
    panel.open();
    worker.wait_idle();
    view->tick(0);
    lv_refr_now(nullptr);
    expect(panel.sent().size() == before && view->direct_frames() == direct,
           "LVGL invalidating the rows during a frame keeps that frame for LVGL");
    lv_obj_delete(talk);
    expect(synapse_goes_straight(*view, worker), "the next frames go straight again");

    // Leaving the screen stops a frame mid-way: nothing more reaches the panel.
    panel.hold();
    before = panel.sent().size();
    view->tick(50);
    panel.held();
    view->detach();
    plain->detach();
    lv_obj_clean(screen);
    panel.open();
    worker.wait_idle();
    expect(panel.sent().size() == before, "a frame drawn while the screen changes sends nothing");
    view.reset();
    aura::ui::set_synapse_output(nullptr);
}

// With a helper (the board's other core), the worker draws the even strips and
// the helper the odd ones: the same frame, and the same rows to the panel.
void check_synapse_helper_shares_strips() {
    using aura::ui::AuraMercuryView;
    using aura::ui::SynapseProfile;
    ensure_lvgl();
    auto* screen = lv_screen_active();
    FakePanel panel;
    aura::ui::set_synapse_output(&panel);
    aura::ui::FrameWorker worker;
    aura::ui::FrameWorker helper;
    synapse_draw_ms = 0;
    auto shared = std::make_unique<AuraMercuryView>(synapse_test_clock, &worker, &helper);
    auto alone = instant_synapse();
    alone->set_strip_height(AuraMercuryView::height);
    for (auto* view : {shared.get(), alone.get()}) {
        view->set_profile(SynapseProfile::Device);
        view->set_state(aura::ui::PresenceState::Speaking);
        view->set_level(0.6F);
    }
    auto* shared_image = shared->attach(screen, 240, 246);
    auto* alone_image = alone->attach(screen, 240, 246);  // behind the other: it never lies over it
    lv_refr_now(nullptr);
    const auto& shared_frame = *static_cast<const lv_image_dsc_t*>(lv_image_get_src(shared_image));
    const auto& alone_frame = *static_cast<const lv_image_dsc_t*>(lv_image_get_src(alone_image));
    std::size_t before = 0;
    for (int step = 0; step < 4; ++step) {
        before = panel.sent().size();
        synapse_worker_frame(*shared, worker);
        alone->tick(50);
        lv_refr_now(nullptr);
        expect(std::memcmp(shared_frame.data, alone_frame.data, shared_frame.data_size) == 0,
               "with a helper the device frame is exactly the frame drawn whole");
    }
    expect(shared->direct_frames() >= 2 && synapse_strips_match(panel, before, shared_frame, 120),
           "with a helper the middle rows still go to the panel, each once and exactly as shown");
    shared->detach();
    alone->detach();
    lv_obj_clean(screen);
    shared.reset();
    aura::ui::set_synapse_output(nullptr);
}

// On Home, Halo's layout leaves those rows clear: the date line lies over the
// band's top, the hint under its bottom, and the talk button is clear.
void check_synapse_direct_on_home() {
    using aura::ui::AuraMercuryView;
    ensure_lvgl();
    auto* previous = lv_display_get_default();
    static std::uint8_t buffer[480 * 48 * 2];
    auto* display = lv_display_create(480, 480);
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t* target, const lv_area_t*, std::uint8_t*) {
        lv_display_flush_ready(target);
    });
    lv_display_set_default(display);
    auto owner = std::make_unique<aura::AuraUi>();
    auto& face = *owner;
    face.create(display, aura::AuraUiCallbacks{});
    aura::DeviceStatusSnapshot status;
    status.clock_valid = true;
    status.hour = 19;
    status.minute = 30;
    status.date_valid = true;
    status.year = 2026;
    status.month = 11;
    status.day = 15;
    status.weekday = 0;
    face.apply_status(status);
    face.show(aura::UiView::Home);
    lv_refr_now(display);

    // Halo's own Synapse draws the presentation here: the board's takes its place.
    lv_obj_t* halo = nullptr;
    const std::function<void(lv_obj_t*)> find = [&](lv_obj_t* object) {
        for (std::uint32_t index = 0; index < lv_obj_get_child_count(object) && !halo; ++index) {
            auto* child = lv_obj_get_child(object, static_cast<std::int32_t>(index));
            if (lv_obj_check_type(child, &lv_image_class) && lv_obj_get_width(child) == AuraMercuryView::width &&
                lv_obj_get_height(child) == AuraMercuryView::height) {
                halo = child;
            }
            find(child);
        }
    };
    find(face.root());
    expect(halo != nullptr, "Home shows Halo's Synapse");
    FakePanel panel;
    aura::ui::set_synapse_output(&panel);
    aura::ui::FrameWorker worker;
    auto view = std::make_unique<AuraMercuryView>(synapse_test_clock, &worker);
    view->set_profile(aura::ui::SynapseProfile::Device);
    if (halo) {
        lv_obj_set_hidden(halo, true);
        view->attach(lv_obj_get_parent(halo), 240, 246);
        lv_refr_now(display);
        for (int step = 0; step < 3; ++step) synapse_worker_frame(*view, worker);
    }
    expect(view->direct_frames() == 2 && !panel.sent().empty() && panel.sent().front().top == 150,
           "on Home, Synapse's middle rows go to the panel straight");
    // So do the assistant's screens, once they have faded in: the title and
    // caption lie above the band, the buttons below it. Back on a screen, the
    // worker's frame shows first and the next waits for LVGL to draw the band.
    for (const auto state : {aura::AssistantState::Listening, aura::AssistantState::Thinking}) {
        view->detach();
        aura::AssistantSnapshot snapshot;
        snapshot.state = state;
        snapshot.sequence = static_cast<std::uint64_t>(state) + 10U;
        face.apply(snapshot);
        for (int step = 0; step < 10; ++step) {
            lv_tick_inc(50);
            lv_timer_handler();
        }
        halo = nullptr;
        find(face.root());
        expect(halo != nullptr, "the assistant's screen shows Halo's Synapse");
        lv_obj_set_hidden(halo, true);
        view->attach(lv_obj_get_parent(halo), 240, 246);
        lv_refr_now(display);
        expect(synapse_goes_straight(*view, worker, 3), "on the assistant's screens the middle rows go straight");
    }
    view->detach();
    view.reset();
    aura::ui::set_synapse_output(nullptr);
    lv_screen_load(lv_obj_create(nullptr));
    lv_obj_delete(face.root());
    owner.reset();
    lv_display_set_default(previous);
    lv_display_delete(display);
    aura::ui::set_active_appearance(aura::Appearance::Halo);
}

// The firmware builds the UI, and Halo's Synapse with it, before it starts the
// shared worker (app_main's static AuraUi). The view looks the worker up when
// it is shown: on the first flash it kept none, drew every ~340 ms frame on the
// UI thread with the display locked, and froze the whole board. Runs last: the
// shared worker, once started, serves every later view in this process.
void check_synapse_finds_shared_worker() {
    using aura::ui::AuraMercuryView;
    using aura::ui::FrameWorker;
    ensure_lvgl();
    auto* screen = lv_screen_active();
    expect(FrameWorker::shared() == nullptr, "no shared worker before the platform starts one");
    auto view = std::make_unique<AuraMercuryView>();  // as AuraUi makes it, before the worker
    FrameWorker::start_shared();
    view->attach(screen, 240, 246);
    expect(view->buffer_bytes() == static_cast<std::size_t>(2 * 480 * 252 * 3),
           "a view made before the shared worker started draws on it, with a second frame");
    const auto attached = view->frame_count();
    view->tick(50);
    expect(view->frame_count() == attached, "the UI thread hands the frame to the worker, it does not draw it");
    FrameWorker::shared()->wait_idle();
    view->tick(1);
    expect(view->frame_count() == attached + 1, "the next tick shows the frame the worker drew");
    view.reset();
    expect(!FrameWorker::shared()->busy(), "the view never leaves the worker drawing into freed memory");
    lv_obj_clean(screen);
}

// A header floats on a glass capsule, and what scrolls under it shows through, blurred.
void check_header_glass() {
    namespace ui = aura::ui;
    ensure_lvgl();
    const auto& rect = ui::tokens::HEADER_GLASS_RECT;
    expect(rect.x == 8 && rect.y == 8 && rect.width == 464 && rect.height == 80,
           "the header glass lies 8 px inside the 96 px header band");
    for (const auto appearance : aura::kAppearances) {
        ui::set_active_appearance(appearance);
        auto* screen = lv_obj_create(nullptr);
        ui::reset(screen);
        lv_obj_set_size(screen, 480, 480);
        auto* glass = ui::make_header_glass(screen);
        lv_obj_update_layout(screen);
        lv_area_t area{};
        lv_obj_get_coords(glass, &area);
        expect(area.x1 == rect.x && area.y1 == rect.y && lv_area_get_width(&area) == rect.width &&
                   lv_area_get_height(&area) == rect.height,
               "the header glass covers HEADER_GLASS_RECT");
        expect(lv_obj_get_style_radius(glass, LV_PART_MAIN) == LV_RADIUS_CIRCLE, "the header glass is a capsule");
        expect(lv_obj_get_style_blur_backdrop(glass, LV_PART_MAIN) &&
                   lv_obj_get_style_blur_radius(glass, LV_PART_MAIN) == 18,
               "the header glass blurs what lies under it");
        expect(lv_obj_get_style_bg_main_opa(glass, LV_PART_MAIN) == 176 &&
                   lv_obj_get_style_bg_grad_opa(glass, LV_PART_MAIN) == 136 &&
                   lv_obj_get_style_bg_grad_dir(glass, LV_PART_MAIN) == LV_GRAD_DIR_VER &&
                   same_color(lv_obj_get_style_bg_color(glass, LV_PART_MAIN), ui::tokens::surface_glass) &&
                   same_color(lv_obj_get_style_bg_grad_color(glass, LV_PART_MAIN), ui::tokens::surface),
               "the header glass is the look's surfaces, translucent and clearer towards its foot");
        expect(lv_obj_get_style_border_width(glass, LV_PART_MAIN) == 1 &&
                   lv_obj_get_style_border_opa(glass, LV_PART_MAIN) == 40 &&
                   same_color(lv_obj_get_style_border_color(glass, LV_PART_MAIN), ui::tokens::text),
               "the header glass has a faint light edge");
        expect(lv_obj_get_style_shadow_width(glass, LV_PART_MAIN) == 0,
               "the header glass casts no shadow (none anywhere: costly on the ESP32-S3)");
        bool band = lv_obj_is_clickable(glass);
        for (const lv_point_t point : {lv_point_t{0, 0}, lv_point_t{479, 0}, lv_point_t{0, 95}, lv_point_t{479, 95},
                                       lv_point_t{240, 92}, lv_point_t{4, 48}}) {
            band = band && lv_obj_hit_test(glass, &point);
        }
        const lv_point_t below{240, 96};
        expect(band && !lv_obj_hit_test(glass, &below),
               "taps anywhere in the header band, and only there, stop at the glass");
        expect(ui::is_header_glass(glass) && !ui::is_header_glass(screen), "the header glass is recognisable");
        lv_obj_delete(screen);
    }
    ui::set_active_appearance(aura::Appearance::Halo);
}

// The device's LVGL draws the screen in bands of DEVICE_DRAW_ROWS rows, rounded to
// even starts and odd ends for the panel, and the glass can blur only what its band
// holds. Drawn that way, the capsule must look as it does when the screen is drawn whole.
std::vector<std::uint8_t> glass_frame(480 * 480 * 2);

void check_header_glass_bands() {
    namespace ui = aura::ui;
    ensure_lvgl();
    auto* previous = lv_display_get_default();
    auto* display = lv_display_create(480, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, [](lv_display_t* target, const lv_area_t* area, std::uint8_t* pixels) {
        const auto width = static_cast<std::size_t>(lv_area_get_width(area));
        for (int y = area->y1; y <= area->y2; ++y) {
            std::memcpy(&glass_frame[(static_cast<std::size_t>(y) * 480 + static_cast<std::size_t>(area->x1)) * 2],
                        pixels + static_cast<std::size_t>(y - area->y1) * width * 2, width * 2);
        }
        lv_display_flush_ready(target);
    });
    lv_display_add_event_cb(
        display,
        [](lv_event_t* event) {
            auto* area = static_cast<lv_area_t*>(lv_event_get_param(event));
            area->x1 &= ~1;
            area->y1 &= ~1;
            area->x2 |= 1;
            area->y2 |= 1;
        },
        LV_EVENT_INVALIDATE_AREA, nullptr);
    lv_display_set_default(display);
    ui::set_active_appearance(aura::Appearance::Halo);

    // Bright discs and a white bar across the band edges, as the launcher's icons
    // and names pass under the header.
    auto* screen = lv_obj_create(nullptr);
    ui::reset(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    constexpr std::array<std::uint32_t, 6> tints{0x8B5CF6, 0xF5B83D, 0x2DD4A7, 0xFF5470, 0x3A7BFF, 0xFFFFFF};
    for (std::size_t index = 0; index < tints.size(); ++index) {
        auto* disc = lv_obj_create(screen);
        ui::reset(disc);
        lv_obj_set_size(disc, 88, 88);
        lv_obj_set_pos(disc, 12 + static_cast<int>(index) * 76, 6 + static_cast<int>(index % 3) * 20);
        lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(disc, ui::color(tints[index]), 0);
        lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    }
    auto* bar = lv_obj_create(screen);
    ui::reset(bar);
    lv_obj_set_size(bar, 300, 10);
    lv_obj_set_pos(bar, 90, 46);
    lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    ui::make_header_glass(screen);
    lv_screen_load(screen);

    std::vector<std::uint8_t> whole_buffer(480 * 480 * 2);
    std::vector<std::uint8_t> band_buffer(static_cast<std::size_t>(480) * ui::tokens::DEVICE_DRAW_ROWS * 2);
    const auto render = [&](std::vector<std::uint8_t>& buffer) {
        lv_display_set_buffers(display, buffer.data(), nullptr, static_cast<std::uint32_t>(buffer.size()),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_obj_invalidate(screen);
        lv_refr_now(display);
        return glass_frame;
    };
    const auto whole = render(whole_buffer);
    const auto banded = render(band_buffer);

    // The largest difference of one RGB565 channel (in its own steps) inside the capsule.
    const auto& rect = ui::tokens::HEADER_GLASS_RECT;
    int worst = 0;
    int worst_row = -1;
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
        for (int x = rect.x; x < rect.x + rect.width; ++x) {
            const auto at = (static_cast<std::size_t>(y) * 480 + static_cast<std::size_t>(x)) * 2;
            const auto a = static_cast<int>(whole[at] | (whole[at + 1] << 8));
            const auto b = static_cast<int>(banded[at] | (banded[at + 1] << 8));
            const int difference = std::max({std::abs((a >> 11) - (b >> 11)), std::abs(((a >> 5) & 63) - ((b >> 5) & 63)),
                                             std::abs((a & 31) - (b & 31))});
            if (difference > worst) {
                worst = difference;
                worst_row = y;
            }
        }
    }
    std::cout << "header glass in " << ui::tokens::DEVICE_DRAW_ROWS << "-row bands: largest channel difference "
              << worst << " (row " << worst_row << ")\n";
    expect(worst <= 1, "drawn in the device's bands, the header glass looks as when the screen is drawn whole");

    lv_display_set_default(previous);
    lv_display_delete(display);
}

// The objects of `type` under `root` that are shown (no hidden object above them).
void shown_of(lv_obj_t* root, const lv_obj_class_t* type, std::vector<lv_obj_t*>& found) {
    if (lv_obj_is_hidden(root)) return;
    if (lv_obj_check_type(root, type)) found.push_back(root);
    for (std::uint32_t index = 0; index < lv_obj_get_child_count(root); ++index) {
        shown_of(lv_obj_get_child(root, static_cast<int32_t>(index)), type, found);
    }
}

std::vector<lv_obj_t*> shown_of(lv_obj_t* root, const lv_obj_class_t* type) {
    std::vector<lv_obj_t*> found;
    shown_of(root, type, found);
    return found;
}

bool shows_text(lv_obj_t* root, const char* text) {
    for (auto* label : shown_of(root, &lv_label_class)) {
        if (std::strcmp(lv_label_get_text(label), text) == 0) return true;
    }
    return false;
}

bool shows_wordmark(lv_obj_t* root) {
    for (auto* image : shown_of(root, &lv_image_class)) {
        if (lv_image_get_src(image) == &aura_brand_aura_hero) return true;
    }
    return false;
}

// The device's own start (the owner, 2026-11-14: two seconds of white at every
// start). Before create() the brand's screen shows: no words, since the
// language is still on the card, and the brand's colours whatever the look the
// card holds. The platform moves its bar; create() fills it, then Home shows.
// A restart the owner confirms on the device holds its screen until the board
// restarts, never Home.
void check_startup_screen() {
    using aura::UiView;
    namespace ui = aura::ui;
    ensure_lvgl();
    auto* previous = lv_display_get_default();
    static std::uint8_t buffer[480 * 48 * 2];
    auto* display = lv_display_create(480, 480);
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t* target, const lv_area_t*, std::uint8_t*) {
        lv_display_flush_ready(target);
    });
    lv_display_set_default(display);
    const auto& halo = ui::palette_for(aura::Appearance::Halo);
    const auto brand_bar = [&halo](lv_obj_t* bar) {
        return same_color(lv_obj_get_style_bg_color(bar, LV_PART_MAIN), halo.surface_high) &&
               same_color(lv_obj_get_style_bg_color(bar, LV_PART_INDICATOR), ui::tokens::brand_start) &&
               same_color(lv_obj_get_style_bg_grad_color(bar, LV_PART_INDICATOR), ui::tokens::brand_end) &&
               lv_obj_get_style_bg_grad_dir(bar, LV_PART_INDICATOR) == LV_GRAD_DIR_HOR;
    };

    auto owner = std::make_unique<aura::AuraUi>();
    auto& face = *owner;
    face.set_appearance(aura::Appearance::Marble);  // a look the card may hold
    face.show_startup(display);
    expect(face.view() == UiView::Boot && face.root() != nullptr && lv_screen_active() == face.root(),
           "the start-up screen shows before the UI is created");
    auto bars = shown_of(face.root(), &lv_bar_class);
    expect(bars.size() == 1 && lv_bar_get_value(bars.front()) == 0, "the start-up screen has one bar, empty");
    expect(shown_of(face.root(), &lv_label_class).empty(),
           "the start-up screen has no words: the language is still on the card");
    expect(shows_wordmark(face.root()), "the start-up screen shows the AURA wordmark");
    expect(bars.size() == 1 && brand_bar(bars.front()),
           "the start-up bar fills in the wordmark's gradient on Halo's track, whatever the look");
    face.set_boot_progress(40);
    bars = shown_of(face.root(), &lv_bar_class);
    expect(bars.size() == 1 && lv_bar_get_value(bars.front()) == 40, "the platform moves the start-up bar");
    face.set_boot_progress(140);
    bars = shown_of(face.root(), &lv_bar_class);
    expect(bars.size() == 1 && lv_bar_get_value(bars.front()) == 100, "the bar never passes full");
    face.set_boot_progress(40);
    // The card's language and look arrive before create(): the screen stays as it was.
    face.set_language(aura::InterfaceLanguage::English);
    face.set_appearance(aura::Appearance::Graphite);
    bars = shown_of(face.root(), &lv_bar_class);
    expect(face.view() == UiView::Boot && shown_of(face.root(), &lv_label_class).empty() &&
               shows_wordmark(face.root()) && bars.size() == 1 && lv_bar_get_value(bars.front()) == 40 &&
               brand_bar(bars.front()),
           "the language and the look from the card leave the start-up screen as it was");
    face.show(UiView::Settings);
    expect(face.view() == UiView::Boot, "nothing opens over the start-up screen");

    face.create(display, aura::AuraUiCallbacks{});
    expect(face.view() == UiView::Boot, "create() first fills the start-up bar");
    int last = 40;
    bool rising = true;
    for (int step = 0; step < 40 && face.view() == UiView::Boot; ++step) {
        face.tick(50);
        bars = shown_of(face.root(), &lv_bar_class);
        if (face.view() == UiView::Boot && bars.size() == 1) {
            const auto value = static_cast<int>(lv_bar_get_value(bars.front()));
            rising = rising && value >= last;
            last = value;
        }
    }
    expect(face.view() == UiView::Home, "then Home, within two seconds");
    expect(rising && last >= 90, "the bar fills from where the platform left it");

    // A restart confirmed on the device: its screen until the board restarts.
    face.show_system_progress(aura::SystemAction::Restart);
    expect(face.view() == UiView::Boot &&
               shows_text(face.root(), aura::l10n::companion_en_us().system_restarting),
           "a confirmed restart shows its screen, in the card's language");
    bars = shown_of(face.root(), &lv_bar_class);
    expect(bars.size() == 1 && lv_bar_get_value(bars.front()) == 0, "the restart bar starts empty");
    for (int step = 0; step < 100; ++step) face.tick(100);
    expect(face.view() == UiView::Boot, "the restart screen stays until the board restarts, never Home");
    face.set_boot_progress(70);
    bars = shown_of(face.root(), &lv_bar_class);
    expect(bars.size() == 1 && lv_bar_get_value(bars.front()) == 70, "the platform moves the restart bar");
    face.show(UiView::Settings);
    expect(face.view() == UiView::Boot, "nothing opens over the restart screen");

    // The screen's objects go before the UI that handles their events.
    lv_screen_load(lv_obj_create(nullptr));
    lv_obj_delete(face.root());
    owner.reset();
    lv_display_set_default(previous);
    lv_display_delete(display);
    ui::set_active_appearance(aura::Appearance::Halo);
}

// The row (a label's parent) whose title is `title`.
lv_obj_t* row_titled(lv_obj_t* root, const char* title) {
    for (auto* label : shown_of(root, &lv_label_class)) {
        if (std::strcmp(lv_label_get_text(label), title) == 0) return lv_obj_get_parent(label);
    }
    return nullptr;
}

bool row_switch_on(lv_obj_t* row) {
    if (!row) return false;
    const auto switches = shown_of(row, &lv_switch_class);
    return switches.size() == 1 && lv_obj_has_state(switches.front(), LV_STATE_CHECKED);
}

// Ρυθμίσεις → Οθόνη on the board (the owner, 2026-11-14): the rotation lock's
// switch never moved, since a change of the lock alone did not draw the screen
// again (the lock itself did change), and Reduced motion opened Settings, where
// it lived before the settings were grouped. Both stay on Οθόνη and show the change.
void check_display_settings_follow() {
    using aura::UiView;
    ensure_lvgl();
    auto* previous = lv_display_get_default();
    static std::uint8_t buffer[480 * 48 * 2];
    auto* display = lv_display_create(480, 480);
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t* target, const lv_area_t*, std::uint8_t*) {
        lv_display_flush_ready(target);
    });
    lv_display_set_default(display);

    auto owner = std::make_unique<aura::AuraUi>();
    auto& face = *owner;
    aura::DisplaySnapshot display_state;
    aura::AuraUiCallbacks callbacks;
    callbacks.on_display_settings = [&display_state](const aura::DisplaySettings& settings) {
        display_state.settings = settings;  // as the platform keeps it, and reports it back
        ++display_state.sequence;
    };
    face.create(display, std::move(callbacks));
    aura::CompanionCapabilities capabilities;
    capabilities.imu = true;
    face.set_capabilities(capabilities);
    display_state.sequence = 1;
    face.apply_display(display_state);
    face.show(UiView::DisplaySettings);
    const auto& words = aura::l10n::companion_el_gr();
    auto* lock_row = row_titled(face.root(), words.rotation_lock_title);
    expect(lock_row && !row_switch_on(lock_row), "the rotation lock starts off, its title whole");
    if (lock_row) lv_obj_send_event(lock_row, LV_EVENT_CLICKED, nullptr);
    expect(display_state.settings.rotation_locked, "a tap on the rotation lock locks the rotation");
    face.apply_display(display_state);
    expect(face.view() == UiView::DisplaySettings &&
               row_switch_on(row_titled(face.root(), words.rotation_lock_title)),
           "the rotation lock's switch shows the lock");

    face.tick(200);  // past the double-tap guard
    auto* motion_row = row_titled(face.root(), aura::l10n::visual_el_gr().reduced_motion);
    const bool reduced_before = face.reduced_motion();
    if (motion_row) lv_obj_send_event(motion_row, LV_EVENT_CLICKED, nullptr);
    expect(motion_row && face.reduced_motion() != reduced_before, "a tap on Reduced motion changes it");
    expect(face.view() == UiView::DisplaySettings, "Reduced motion stays on Οθόνη, where its row is");
    expect(row_switch_on(row_titled(face.root(), aura::l10n::visual_el_gr().reduced_motion)) == face.reduced_motion(),
           "its switch shows the change");

    lv_screen_load(lv_obj_create(nullptr));
    lv_obj_delete(face.root());
    owner.reset();
    lv_display_set_default(previous);
    lv_display_delete(display);
    aura::ui::set_active_appearance(aura::Appearance::Halo);
}

int main() {
    check_aura_logo_asset(aura_brand_aura_compact, 144, 53);
    check_aura_logo_asset(aura_brand_aura_hero, 176, 65);

    expect(aura::ui::tokens::HEADER_HEIGHT == 96 && aura::ui::tokens::BUTTON_HEIGHT >= 80 &&
               aura::ui::tokens::ROW_HEIGHT >= 80 && aura::ui::tokens::HEADER_BUTTON >= 64,
           "watch-class header, rows and primary targets on the 12.4 px/mm panel");
    expect(aura::ui::tokens::CONTENT_SAFE_RECT.x >= aura::ui::tokens::GUTTER,
           "content safe rect respects horizontal gutters");
    expect(aura::ui::tokens::HERO_RECT.y >= aura::ui::tokens::STATUS_BAR_HEIGHT,
           "the presence hero begins below the status bar");
    expect(aura::ui::tokens::SYNAPSE_RECT.x >= aura::ui::tokens::HERO_RECT.x &&
           aura::ui::tokens::SYNAPSE_RECT.y >= aura::ui::tokens::HERO_RECT.y &&
           aura::ui::tokens::SYNAPSE_RECT.x + aura::ui::tokens::SYNAPSE_RECT.width <=
               aura::ui::tokens::HERO_RECT.x + aura::ui::tokens::HERO_RECT.width &&
           aura::ui::tokens::SYNAPSE_RECT.y + aura::ui::tokens::SYNAPSE_RECT.height <=
               aura::ui::tokens::HERO_RECT.y + aura::ui::tokens::HERO_RECT.height,
           "Synapse pixels remain inside the hero safe rect");
    expect(aura::ui::tokens::COMPACT_PRESENCE_TARGET == 176,
           "Marble and Aurora keep their compact Home presence target");
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
        expect(icon->data != nullptr && icon->data_size > 12U, "icon bitmap is present");
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
    // The code fills a copy's placeholders the same way in either language, so
    // a translation must keep them, in order: a mismatch prints garbage or crashes.
    expect(printf_conversions("%d of %s, 100%% %02u %.1f") == "%d %s %02u %.1f ",
           "printf conversions are read in order, without %%");
    expect(printf_conversions("%d %s") != printf_conversions("%s %d"), "swapped placeholders differ");
    expect_matching_formats(aura::l10n::el_gr(), aura::l10n::en_us(), "Strings");
    expect_matching_formats(aura::l10n::visual_el_gr(), aura::l10n::visual_en_us(), "VisualStrings");
    expect_matching_formats(aura::l10n::companion_el_gr(), aura::l10n::companion_en_us(), "CompanionStrings");
    expect_matching_formats(aura::l10n::photo_strings(aura::InterfaceLanguage::Greek),
                            aura::l10n::photo_strings(aura::InterfaceLanguage::English), "PhotoStrings");
    expect_matching_formats(aura::l10n::transfer_strings(aura::InterfaceLanguage::Greek),
                            aura::l10n::transfer_strings(aura::InterfaceLanguage::English), "TransferStrings");
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
    check_icon_compression();
    check_component_kit();
    check_presences();
    check_pearl_presence();
    check_aurora_presence();
    check_stars_presence();

    check_synapse();
    check_synapse_pacing();
    check_synapse_device_profile();
    check_synapse_estimated_root();
    check_synapse_estimated_reciprocal();
    check_synapse_next_tick();
    check_synapse_strips_seamless();
    check_synapse_direct_output();
    check_synapse_helper_shares_strips();
    check_synapse_direct_on_home();
    check_header_glass();
    check_header_glass_bands();
    check_startup_screen();
    check_display_settings_follow();
    check_synapse_finds_shared_worker();  // last: it starts the process's shared worker

    std::cout << "ui_assets_test: PASS\n";
    return 0;
}

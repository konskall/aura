#include "aura/aura_icons.hpp"
#include "aura/aura_mercury_view.hpp"
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
void expect_complete_table(const Table& table, const char* name) {
    static_assert(sizeof(Table) % sizeof(const char*) == 0, "string table holds only pointers");
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
            if (!lv_font_get_glyph_dsc(aura::ui::font_caption(), &glyph, codepoint, 0) ||
                glyph.resolved_font != aura::ui::font_caption()) {
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

    aura::ui::AuraMercuryView mercury;
    expect(mercury.buffer_bytes() == static_cast<std::size_t>(176 * 176 * 3),
           "Mercury uses bounded RGB565+A8 storage");
    expect(aura::ui::AuraMercuryView::frame_interval_ms == 66,
           "Mercury animation targets approximately 15 FPS");

    std::cout << "ui_assets_test: PASS\n";
    return 0;
}

#include "aura/aura_appearance.hpp"

#include <cmath>
#include <cstddef>

#include "aura/aura_theme.hpp"

LV_FONT_DECLARE(aura_font_plex_regular_22);
LV_FONT_DECLARE(aura_font_plex_regular_26);
LV_FONT_DECLARE(aura_font_plex_medium_28);
LV_FONT_DECLARE(aura_font_plex_semibold_32);
LV_FONT_DECLARE(aura_font_plex_light_72);
LV_FONT_DECLARE(aura_font_plex_light_96);
LV_FONT_DECLARE(aura_font_commissioner_regular_22);
LV_FONT_DECLARE(aura_font_commissioner_regular_26);
LV_FONT_DECLARE(aura_font_commissioner_medium_28);
LV_FONT_DECLARE(aura_font_notoserif_regular_32);
LV_FONT_DECLARE(aura_font_notoserif_light_84);
LV_FONT_DECLARE(aura_font_notoserif_extralight_128);
LV_FONT_DECLARE(aura_font_manrope_medium_22);
LV_FONT_DECLARE(aura_font_manrope_medium_26);
LV_FONT_DECLARE(aura_font_manrope_semibold_28);
LV_FONT_DECLARE(aura_font_manrope_bold_32);
LV_FONT_DECLARE(aura_font_manrope_light_80);
LV_FONT_DECLARE(aura_font_manrope_extralight_116);
LV_FONT_DECLARE(aura_font_robotoflex_medium_22);
LV_FONT_DECLARE(aura_font_robotoflex_regular_26);
LV_FONT_DECLARE(aura_font_robotoflex_semibold_cond_28);
LV_FONT_DECLARE(aura_font_robotoflex_semibold_32);
LV_FONT_DECLARE(aura_font_robotoflex_light_cond_64);
LV_FONT_DECLARE(aura_font_robotoflex_light_cond_116);
LV_FONT_DECLARE(aura_font_robotoflex_light_cond_204);
LV_FONT_DECLARE(aura_font_notoserif_light_72);
LV_FONT_DECLARE(aura_font_notoserif_extralight_96);
LV_FONT_DECLARE(aura_font_manrope_light_72);
LV_FONT_DECLARE(aura_font_manrope_extralight_96);
LV_FONT_DECLARE(aura_font_robotoflex_light_cond_96);

namespace aura::ui {
namespace {

// docs/superpowers/specs/2026-10-29-aura-appearances-design.md, "Tokens".
// Halo keeps the v12 values exactly; its button label stays the text colour.
constexpr Palette kPalettes[] = {
    {0x000000, 0x121822, 0x1B2330, 0x222C3B, 0x2B3546, 0x3A141B, 0xF5F8FC, 0xA9B5C7, 0x748197,
     0x178BFF, 0x178BFF, 0xF5F8FC, 0x4C52FF, 0x25D6FF, 0x8A45FF, 0xDE58FB, 0x35ECC3, 0xFFCB3F, 0xFF579E, 0xFF6B78, 0x178BFF},
    {0x000000, 0x110F0D, 0x1A1714, 0x24201B, 0x2B2723, 0x2A1512, 0xF2EDE4, 0xB7AEA1, 0x877D71,
     0xF2EDE4, 0xF2EDE4, 0x0B0A09, 0xDFC08C, 0x7FE3EC, 0xB39CFF, 0xF0A9C0, 0xA8C9A4, 0xE9B872, 0xE58A7B, 0xE58A7B, 0xDFC08C},
    {0x000000, 0x11152A, 0x1C2236, 0x252C46, 0x2A3150, 0x3A1430, 0xF3F5FF, 0xA9B1CC, 0x6F7797,
     0x3A7BFF, 0xC35CF0, 0xFFFFFF, 0x7B5CFF, 0x38E1C9, 0x9D6BFF, 0xFF6FB1, 0x43D9A3, 0xFFC266, 0xFF6FB1, 0xFF6B78, 0x3A7BFF},
    {0x000000, 0x141519, 0x1D1F24, 0x26292F, 0x2B2E34, 0x3A141B, 0xFFFFFF, 0xA3A8B2, 0x737985,
     0xFFFFFF, 0xFFFFFF, 0x000000, 0xA98CFF, 0x5CC8FF, 0xA98CFF, 0xFF5D7D, 0x3DDC97, 0xFFB547, 0xFF5D7D, 0xFF5D7D, 0x3DDC97},
};

// docs/superpowers/specs/2026-10-29-aura-appearances-design.md, "Type roles".
const FontSet kFonts[] = {
    {&aura_font_plex_regular_22, &aura_font_plex_regular_26, &aura_font_plex_medium_28,
     &aura_font_plex_semibold_32, &aura_font_plex_light_72, &aura_font_plex_light_96,
     &aura_font_plex_light_72, &aura_font_plex_light_96, nullptr},
    {&aura_font_commissioner_regular_22, &aura_font_commissioner_regular_26, &aura_font_commissioner_medium_28,
     &aura_font_notoserif_regular_32, &aura_font_notoserif_light_72, &aura_font_notoserif_extralight_96,
     &aura_font_notoserif_light_84, &aura_font_notoserif_extralight_128, nullptr},
    {&aura_font_manrope_medium_22, &aura_font_manrope_medium_26, &aura_font_manrope_semibold_28,
     &aura_font_manrope_bold_32, &aura_font_manrope_light_72, &aura_font_manrope_extralight_96,
     &aura_font_manrope_light_80, &aura_font_manrope_extralight_116, nullptr},
    {&aura_font_robotoflex_medium_22, &aura_font_robotoflex_regular_26, &aura_font_robotoflex_semibold_cond_28,
     &aura_font_robotoflex_semibold_32, &aura_font_robotoflex_light_cond_64, &aura_font_robotoflex_light_cond_96,
     &aura_font_robotoflex_light_cond_64, &aura_font_robotoflex_light_cond_116, &aura_font_robotoflex_light_cond_204},
};

// The component kit of each look (spec, "Phases" 3-5). Every hero centre stays
// at Halo's until its look's signature screens place the presence.
constexpr Metrics kMetrics[] = {
    {RowStyle::Platter, WellStyle::TintedCircle, false, false, 28, LV_RADIUS_CIRCLE, 260},
    {RowStyle::Hairline, WellStyle::HairlineCircle, false, true, 24, LV_RADIUS_CIRCLE, 272},
    {RowStyle::Glass, WellStyle::Chip, true, false, 28, LV_RADIUS_CIRCLE, 260},
    {RowStyle::Platter, WellStyle::Squircle, false, false, 24, 22, 260},
};

Appearance g_active = Appearance::Halo;

std::size_t index_of(Appearance appearance) noexcept {
    const auto index = static_cast<std::size_t>(appearance);
    return index < sizeof(kPalettes) / sizeof(kPalettes[0]) ? index : 0;
}

double channel(std::uint32_t value) noexcept {
    const double srgb = static_cast<double>(value & 0xFFU) / 255.0;
    return srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}

double luminance(std::uint32_t rgb) noexcept {
    return 0.2126 * channel(rgb >> 16) + 0.7152 * channel(rgb >> 8) + 0.0722 * channel(rgb);
}

}  // namespace

const Palette& palette_for(Appearance appearance) noexcept { return kPalettes[index_of(appearance)]; }
const FontSet& fonts_for(Appearance appearance) noexcept { return kFonts[index_of(appearance)]; }
const Metrics& metrics_for(Appearance appearance) noexcept { return kMetrics[index_of(appearance)]; }

void set_active_appearance(Appearance appearance) noexcept {
    g_active = kAppearances[index_of(appearance)];
    const auto& palette = palette_for(g_active);
    tokens::canvas = palette.canvas;
    tokens::surface = palette.surface;
    tokens::surface_high = palette.surface_high;
    tokens::surface_glass = palette.surface_glass;
    tokens::separator = palette.separator;
    tokens::danger_surface = palette.danger_surface;
    tokens::text = palette.text;
    tokens::text_secondary = palette.text_secondary;
    tokens::text_tertiary = palette.text_tertiary;
    tokens::blue = palette.blue;
    tokens::primary_end = palette.primary_end;
    tokens::on_primary = palette.on_primary;
    tokens::indigo = palette.indigo;
    tokens::ice = palette.ice;
    tokens::lilac = palette.lilac;
    tokens::magenta = palette.magenta;
    tokens::mint = palette.mint;
    tokens::amber = palette.amber;
    tokens::pink = palette.pink;
    tokens::coral = palette.coral;
    tokens::toggle_on = palette.toggle_on;
}

Appearance active_appearance() noexcept { return g_active; }
const Metrics& active_metrics() noexcept { return metrics_for(g_active); }

double contrast_ratio(std::uint32_t foreground, std::uint32_t background) noexcept {
    const double first = luminance(foreground);
    const double second = luminance(background);
    const double lighter = first > second ? first : second;
    const double darker = first > second ? second : first;
    return (lighter + 0.05) / (darker + 0.05);
}

}  // namespace aura::ui

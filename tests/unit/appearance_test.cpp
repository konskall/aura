#include "aura/appearance.hpp"
#include "aura/display_power.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}  // namespace

int main() {
    using namespace aura;

    // appearance_ids_round_trip
    for (const auto appearance : kAppearances) {
        expect(appearance_from_id(appearance_id(appearance)) == appearance, "every appearance id round-trips");
    }
    expect(std::string(appearance_id(Appearance::Halo)) == "halo", "Halo is stored as halo");
    expect(std::string(appearance_id(Appearance::Marble)) == "marble", "Marble is stored as marble");
    expect(std::string(appearance_id(Appearance::Aurora)) == "aurora", "Aurora is stored as aurora");
    expect(std::string(appearance_id(Appearance::Graphite)) == "graphite", "Graphite is stored as graphite");

    // unknown_id_is_halo
    expect(appearance_from_id("neon") == Appearance::Halo, "an unknown id is Halo");
    expect(appearance_from_id("") == Appearance::Halo, "an empty id is Halo");
    expect(appearance_from_id("HALO ") == Appearance::Halo, "a mangled id is Halo");
    expect(appearance_from_id("Marble") == Appearance::Halo, "ids are exact, not case-folded");

    // display_settings_keep_appearance
    DisplaySettings settings;
    expect(settings.appearance == Appearance::Halo, "Halo is the default");
    settings.appearance = Appearance::Graphite;
    settings.always_on = false;
    const auto text = serialize_display_settings(settings);
    expect(text.find("appearance=graphite\n") != std::string::npos, "the appearance is written to display.txt");
    const auto parsed = parse_display_settings(text);
    expect(parsed.appearance == Appearance::Graphite, "the appearance is read back");
    expect(!parsed.always_on, "the other settings survive beside it");

    // old_display_file_is_halo
    const auto old = parse_display_settings("aura-display 1\nalways_on=0\n");
    expect(old.appearance == Appearance::Halo && !old.always_on, "a file from an older build keeps Halo and its keys");
    const auto unknown = parse_display_settings("aura-display 1\nalways_on=0\nappearance=neon\ntimeout_ms=60000\n");
    expect(unknown.appearance == Appearance::Halo, "an unknown stored appearance falls back to Halo");
    expect(!unknown.always_on && unknown.timeout_ms == 60000, "an unknown appearance keeps the other keys");
    return 0;
}

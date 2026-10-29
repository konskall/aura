#include "aura/appearance.hpp"

namespace aura {

const char* appearance_id(Appearance appearance) noexcept {
    switch (appearance) {
        case Appearance::Marble: return "marble";
        case Appearance::Aurora: return "aurora";
        case Appearance::Graphite: return "graphite";
        case Appearance::Halo: break;
    }
    return "halo";
}

Appearance appearance_from_id(std::string_view id) noexcept {
    for (const auto appearance : kAppearances) {
        if (id == appearance_id(appearance)) return appearance;
    }
    return Appearance::Halo;
}

}  // namespace aura

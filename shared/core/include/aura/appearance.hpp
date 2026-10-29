#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace aura {

// The look of the whole device, chosen in Settings > Appearance. Halo is the
// v12/v13 design and stays the default.
enum class Appearance : std::uint8_t { Halo, Marble, Aurora, Graphite };

inline constexpr std::array<Appearance, 4> kAppearances{Appearance::Halo, Appearance::Marble,
                                                        Appearance::Aurora, Appearance::Graphite};

// Stable ids stored in display.txt: "halo", "marble", "aurora", "graphite".
const char* appearance_id(Appearance appearance) noexcept;
// Anything that is not exactly one of the ids is Halo.
Appearance appearance_from_id(std::string_view id) noexcept;

}  // namespace aura

#include "aura/aura_presence.hpp"

#include "aura/aura_mercury_view.hpp"
#include "aura/aura_pearl_view.hpp"

namespace aura::ui {

// Mercury stands in for every appearance until its own presence exists.
std::unique_ptr<PresenceView> make_presence(Appearance appearance) {
    if (appearance == Appearance::Marble) return std::make_unique<AuraPearlView>();
    return std::make_unique<AuraMercuryView>();
}

}  // namespace aura::ui

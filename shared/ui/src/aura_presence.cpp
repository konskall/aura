#include "aura/aura_presence.hpp"

#include "aura/aura_aurora_view.hpp"
#include "aura/aura_mercury_view.hpp"
#include "aura/aura_pearl_view.hpp"
#include "aura/aura_stars_view.hpp"

namespace aura::ui {

// Each appearance's own presence: Mercury (Άλως), the pearl (Μάρμαρο), the
// ribbons (Σέλας) and the constellation (Γραφίτης).
std::unique_ptr<PresenceView> make_presence(Appearance appearance) {
    if (appearance == Appearance::Marble) return std::make_unique<AuraPearlView>();
    if (appearance == Appearance::Aurora) return std::make_unique<AuraAuroraView>();
    if (appearance == Appearance::Graphite) return std::make_unique<AuraStarsView>();
    return std::make_unique<AuraMercuryView>();
}

}  // namespace aura::ui

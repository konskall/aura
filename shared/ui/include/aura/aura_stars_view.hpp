#pragma once

#include <array>
#include <cstdint>

#include "aura/aura_presence.hpp"
#include "lvgl.h"

namespace aura::ui {

// Γραφίτης's presence, Αστερισμός: a sphere of 300 points of light turning
// slowly. Its surface lifts with the voice while listening, a bright band
// sweeps it while thinking and waves run from the centre while speaking. The
// points are stamped with additive light into a 280 x 280 canvas; the glow is
// a light field behind the screen's content. Reference: the Stars concept in
// docs/design/themes/presence-reference.js.
class AuraStarsView final : public PresenceView {
public:
    static constexpr int size = 280;
    static constexpr std::size_t point_count = 300;
    static constexpr std::uint32_t frame_interval_ms = 66;

    AuraStarsView();

    lv_obj_t* attach(lv_obj_t* parent, int center_x, int center_y) override;
    void detach() noexcept override;
    void set_state(PresenceState state) noexcept override;
    void set_level(float level) noexcept override;
    void set_reduced_motion(bool reduced) noexcept override;
    bool tick(std::uint32_t elapsed_ms) override;

    lv_obj_t* object() const noexcept override { return image_; }
    std::uint64_t frame_count() const noexcept override { return frame_count_; }
    std::size_t buffer_bytes() const noexcept override { return buffer_.size(); }
    // How far the voice lifted the points in the last frame (share of the
    // radius), and where the thinking band is (-1 .. 1).
    float swell() const noexcept { return swell_; }
    float band() const noexcept { return band_; }

private:
    struct Point {
        float x;
        float y;
        float z;
        float seed;
    };

    void advance(float seconds) noexcept;
    void render();
    void stamp(float x, float y, float radius, float red, float green, float blue) noexcept;
    void update_glow();

    // While drawing, a pixel's three bytes hold its added red, green and blue;
    // a last pass turns them into the RGB565 colour and the A8 alpha.
    alignas(4) std::array<std::uint8_t, size * size * 3> buffer_{};
    std::array<Point, point_count> points_{};
    lv_image_dsc_t descriptor_{};
    lv_obj_t* image_{};
    lv_obj_t* glow_{};
    int glow_opacity_{-1};
    std::uint32_t glow_color_{};
    std::array<float, 4> weights_{1.0F, 0.0F, 0.0F, 0.0F};  // idle, listening, thinking, speaking
    float seconds_{};
    float turn_{};
    float level_{};
    float swell_{};
    float band_{};
    PresenceState state_{PresenceState::Idle};
    std::uint32_t accumulator_ms_{};
    std::uint64_t frame_count_{};
    bool reduced_motion_{};
    bool attached_before_{};
};

}  // namespace aura::ui

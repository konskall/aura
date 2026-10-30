#pragma once

#include <array>
#include <cstdint>

#include "aura/aura_presence.hpp"
#include "lvgl.h"

namespace aura::ui {

// Σέλας's presence: three ribbons of light. They wave gently while waiting,
// follow the voice while listening, tie into a loop with a light travelling
// round it while thinking and dance with the voice while speaking. The
// ribbons are drawn per pixel into a 432 x 200 band with additive light; their
// haze is a light field behind the screen's content. Reference: the Aurora
// concept in docs/design/themes/presence-reference.js.
class AuraAuroraView final : public PresenceView {
public:
    static constexpr int width = 432;
    static constexpr int height = 200;
    static constexpr std::uint32_t frame_interval_ms = 66;

    AuraAuroraView();

    lv_obj_t* attach(lv_obj_t* parent, int center_x, int center_y) override;
    void detach() noexcept override;
    void set_state(PresenceState state) noexcept override;
    void set_level(float level) noexcept override;
    void set_reduced_motion(bool reduced) noexcept override;
    bool tick(std::uint32_t elapsed_ms) override;

    lv_obj_t* object() const noexcept override { return image_; }
    std::uint64_t frame_count() const noexcept override { return frame_count_; }
    std::size_t buffer_bytes() const noexcept override { return buffer_.size(); }
    // How far the ribbons sway (px) and how far they have tied into the loop (0..1).
    float amplitude() const noexcept { return amplitude_; }
    float loop_share() const noexcept { return weights_[2]; }

private:
    void advance(float seconds) noexcept;
    void render();
    void wave(float drive, float strength);
    void stamp(float drive, float strength);
    void add_light(int x, int y, float red, float green, float blue) noexcept;
    void soft_dot(float x, float y, float radius, bool squared, float red, float green, float blue,
                  float peak) noexcept;
    void update_haze();

    // While drawing, a pixel's three bytes hold its added red, green and blue;
    // a last pass turns them into the RGB565 colour and the A8 alpha.
    alignas(4) std::array<std::uint8_t, width * height * 3> buffer_{};
    lv_image_dsc_t descriptor_{};
    lv_obj_t* image_{};
    lv_obj_t* haze_{};
    int haze_opacity_{-1};
    std::uint32_t haze_color_{};
    std::array<float, 3> phases_{};
    std::array<float, 4> weights_{1.0F, 0.0F, 0.0F, 0.0F};  // idle, listening, thinking, speaking
    float seconds_{};
    float level_{};
    float amplitude_{};
    PresenceState state_{PresenceState::Idle};
    std::uint32_t accumulator_ms_{};
    std::uint64_t frame_count_{};
    bool reduced_motion_{};
    bool attached_before_{};
};

}  // namespace aura::ui

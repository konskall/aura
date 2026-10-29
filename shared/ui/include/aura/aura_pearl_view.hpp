#pragma once

#include <array>
#include <cstdint>

#include "aura/aura_presence.hpp"
#include "lvgl.h"

namespace aura::ui {

// Μάρμαρο's presence: a pearl that breathes while it waits, sends out ripples
// as it listens, turns light inside as it thinks and pulses with the voice.
// The sphere is drawn per pixel like Mercury; its glow, rings and thinking
// comet are LVGL objects it owns, behind the screen's content. Reference:
// the Pearl concept in docs/design/themes/presence-reference.js.
class AuraPearlView final : public PresenceView {
public:
    static constexpr int size = 176;
    static constexpr std::uint32_t frame_interval_ms = 66;

    AuraPearlView();

    lv_obj_t* attach(lv_obj_t* parent, int center_x, int center_y) override;
    void detach() noexcept override;
    void set_state(PresenceState state) noexcept override;
    void set_level(float level) noexcept override;
    void set_reduced_motion(bool reduced) noexcept override;
    bool tick(std::uint32_t elapsed_ms) override;

    lv_obj_t* object() const noexcept override { return image_; }
    std::uint64_t frame_count() const noexcept override { return frame_count_; }
    std::size_t buffer_bytes() const noexcept override { return buffer_.size(); }
    // The listening ring's radius in px; 0 while it is hidden.
    int ring_radius() const noexcept { return ring_.radius; }

private:
    static constexpr std::size_t kRipples = 6;
    static constexpr std::size_t kCometSegments = 6;

    // An arc and the values last given to it, so an unchanged ring is not redrawn.
    struct Ring {
        lv_obj_t* arc{};
        int radius{};
        int width{};
        int opacity{};
        std::uint32_t color{};
        int start{-1};
        int end{-1};
    };

    void advance(float seconds) noexcept;
    void render();
    void update_objects();
    Ring make_ring(lv_obj_t* parent, bool rounded, std::uint32_t& depth);
    void show_ring(Ring& ring, float radius, float width, float opacity, std::uint32_t color, int start = 0,
                   int end = 360);

    alignas(4) std::array<std::uint8_t, size * size * 3> buffer_{};
    lv_image_dsc_t descriptor_{};
    lv_obj_t* image_{};
    lv_obj_t* glow_{};
    int glow_opacity_{-1};
    std::uint32_t glow_color_{};
    Ring ring_{};
    std::array<Ring, kRipples> ripples_{};
    std::array<Ring, kCometSegments> comet_{};
    std::array<float, kRipples> ripple_born_{};  // seconds; negative while free
    std::array<float, 4> weights_{1.0F, 0.0F, 0.0F, 0.0F};  // idle, listening, thinking, speaking
    int center_x_{};
    int center_y_{};
    float seconds_{};
    float level_{};
    float radius_{80.0F};
    float previous_drive_{};
    float last_ripple_{-9.0F};
    PresenceState state_{PresenceState::Idle};
    std::uint32_t accumulator_ms_{};
    std::uint64_t frame_count_{};
    bool reduced_motion_{};
    bool attached_before_{};
};

}  // namespace aura::ui

#pragma once

#include <array>
#include <cstdint>

#include "aura/aura_presence.hpp"
#include "lvgl.h"

namespace aura::ui {

using MercuryState = PresenceState;

// Halo's presence, Synapse: a centred reactor inside eight symmetric kinetic
// light paths. The historical type name is kept so the public AuraUi API and
// existing simulator/firmware composition stay source-compatible.
class AuraMercuryView final : public PresenceView {
public:
    static constexpr int size = 252;
    static constexpr std::uint32_t frame_interval_ms = 50;
    // The wait before the next frame after one that took `render_ms` to draw.
    static std::uint32_t paced_interval(std::uint32_t render_ms) noexcept;

    AuraMercuryView();

    lv_obj_t* attach(lv_obj_t* parent, int center_x, int center_y) override;
    void detach() noexcept override;
    void set_state(MercuryState state) noexcept override;
    void set_level(float level) noexcept override;
    void set_reduced_motion(bool reduced) noexcept override;
    bool tick(std::uint32_t elapsed_ms) override;

    std::uint64_t frame_count() const noexcept override { return frame_count_; }
    std::size_t buffer_bytes() const noexcept override { return buffer_.size(); }
    lv_obj_t* object() const noexcept override { return image_; }
    int reactor_x() const noexcept { return size / 2; }
    int reactor_y() const noexcept { return size / 2; }
    int lit_center_x() const noexcept { return lit_center_x_; }
    int lit_center_y() const noexcept { return lit_center_y_; }
    int lit_extent() const noexcept { return lit_extent_; }
    std::uint32_t frame_signature() const noexcept;

private:
    struct Point {
        float x;
        float y;
        float z;
    };

    void advance(float seconds) noexcept;
    void render();
    Point path_point(int path, float u) const noexcept;
    void add_light(int x, int y, float red, float green, float blue) noexcept;
    void soft_dot(float x, float y, float radius, float red, float green, float blue, float peak) noexcept;
    void light_line(const Point& from, const Point& to, float radius, float red, float green, float blue,
                    float peak) noexcept;
    void overlay_dot(float x, float y, float radius, float red, float green, float blue, float opacity) noexcept;
    void overlay_line(const Point& from, const Point& to, float radius, float red, float green, float blue,
                      float opacity) noexcept;
    void clear_core(float radius) noexcept;
    void draw_atmosphere(float drive);
    void draw_orbits(float drive);
    void draw_paths(float drive);
    void draw_reactor(float drive);
    void draw_foreground_paths(float drive);
    void finalize();
    void update_glow();

    alignas(4) std::array<std::uint8_t, size * size * 3> buffer_{};
    lv_image_dsc_t descriptor_{};
    lv_obj_t* image_{};
    lv_obj_t* glow_{};
    int glow_opacity_{-1};
    std::uint32_t glow_color_{};
    std::array<float, 4> weights_{1.0F, 0.0F, 0.0F, 0.0F};  // idle, listening, thinking, speaking
    MercuryState state_{MercuryState::Idle};
    float level_{};
    float seconds_{};
    float phase_{};
    float speed_{0.32F};
    float spread_{0.14F};
    float weave_{0.26F};
    float wave_{0.16F};
    float drive_{};
    std::uint32_t accumulator_ms_{};
    std::uint32_t interval_ms_{frame_interval_ms};  // paced by the last frame's cost
    std::uint64_t frame_count_{};
    int lit_center_x_{size / 2};
    int lit_center_y_{size / 2};
    int lit_extent_{};
    bool reduced_motion_{};
    bool attached_before_{};
};

}  // namespace aura::ui

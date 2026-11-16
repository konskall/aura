#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "aura/aura_device_frame.hpp"
#include "aura/aura_frame_worker.hpp"
#include "aura/aura_presence.hpp"
#include "lvgl.h"

namespace aura::ui {

// Μάρμαρο's presence: a pearl that breathes while it waits, sends out ripples
// as it listens, turns light inside as it thinks and pulses with the voice.
// The sphere is drawn per pixel like Mercury; its glow, rings and thinking
// comet are LVGL objects it owns, behind the screen's content. Reference:
// the Pearl concept in docs/design/themes/presence-reference.js. On the board
// (PresenceProfile::Device) the glow, the rings, the comet and the sphere are
// one opaque frame, as far as the rings reach, drawn on the worker.
class AuraPearlView final : public PresenceView {
public:
    static constexpr int size = 176;
    static constexpr std::uint32_t frame_interval_ms = 66;
    // The board's frame: the farthest ripple (205 px), its stroke and edge.
    static constexpr int device_reach = 208;

    // The platform's profile, drawn on its shared worker once one is started.
    AuraPearlView();
    // A profile, and a worker to draw the device frames on (none: the
    // caller's thread).
    AuraPearlView(PresenceProfile profile, FrameWorker* worker);
    // Waits for a frame on the worker: it reads the rings and rows below.
    ~AuraPearlView() override;

    lv_obj_t* attach(lv_obj_t* parent, int center_x, int center_y) override;
    void detach() noexcept override;
    void set_state(PresenceState state) noexcept override;
    void set_level(float level) noexcept override;
    void set_reduced_motion(bool reduced) noexcept override;
    bool tick(std::uint32_t elapsed_ms) override;
    std::uint32_t next_tick_ms() const noexcept override;

    lv_obj_t* object() const noexcept override { return image_; }
    std::uint64_t frame_count() const noexcept override { return device_ ? device_->frame_count() : frame_count_; }
    std::size_t buffer_bytes() const noexcept override;
    std::uint32_t last_frame_ms() const noexcept override { return device_ ? device_->last_frame_ms() : 0U; }
    // The listening ring's radius in px; 0 while it is hidden.
    int ring_radius() const noexcept;
    PresenceProfile profile() const noexcept { return profile_; }
    // The board's frame (null in the presentation); the tests redraw it whole.
    DeviceFrame* device_frame() const noexcept { return device_.get(); }

private:
    static constexpr std::size_t kRipples = 6;
    static constexpr std::size_t kCometSegments = 6;
    // Back to front: the ripples, the listening ring, the comet.
    static constexpr std::size_t kRings = kRipples + 1 + kCometSegments;

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
    // A ring of the board's frame, as LVGL's arc draws one: its outer radius
    // ((2 reach + stroke) / 2), stroke, opacity (0..255), colour, and angles in
    // degrees clockwise from 3 o'clock.
    struct RingShape {
        bool visible{};
        int reach{};
        int radius{};
        int stroke{};
        int opacity{};
        std::uint32_t color{};
        int start{};
        int end{360};
        bool rounded{};
        // A segment's rounded ends: their centres on the middle of the stroke.
        float start_x{};
        float start_y{};
        float end_x{};
        float end_y{};
        // A segment under half a turn: its ends' directions, for a pixel's
        // side of each, and the pixels it can reach.
        bool narrow{};
        float start_cos{};
        float start_sin{};
        float end_cos{};
        float end_sin{};
        FrameRect box{};
        bool operator==(const RingShape& other) const noexcept {
            return visible == other.visible && reach == other.reach && radius == other.radius &&
                   stroke == other.stroke && opacity == other.opacity && color == other.color &&
                   start == other.start && end == other.end && rounded == other.rounded;
        }
        bool operator!=(const RingShape& other) const noexcept { return !(*this == other); }
    };
    // The sphere's lights for one frame.
    struct Light {
        float x;
        float y;
        float inverse_radius_squared;
        float red;  // already times its strength
        float green;
        float blue;
    };
    struct Sphere {
        std::array<Light, 6> lights{};
        std::size_t count{};
        float radius{};
        float highlight_x{};
        float highlight_y{};
        float highlight_inverse{};
        float highlight_strength{};
        float light_x{};
        float light_y{};
        float body_inverse{};
        float outside{};
        float rim_start{};
    };

    void advance(float seconds) noexcept;
    Sphere sphere() const noexcept;
    void render();
    void update_objects();
    template <typename Place>
    void each_ring(std::uint32_t glow, Place&& place);
    Ring make_ring(lv_obj_t* parent, bool rounded, std::uint32_t& depth);
    void show_ring(Ring& ring, float radius, float width, float opacity, std::uint32_t color, int start = 0,
                   int end = 360);
    // The board: the frame's glow, rings and sphere, then its drawing on the worker.
    lv_obj_t* attach_device(lv_obj_t* parent, int center_x, int center_y);
    void prepare_device();
    bool start_device_frame();
    static RingShape ring_shape(float radius, float width, float opacity, std::uint32_t color, int start, int end,
                                bool rounded) noexcept;
    static void begin_device(void* view, const FrameRect& redraw);
    static void draw_device(void* view, int lane, std::uint16_t* pixels, const FrameRect& area,
                            const FrameRect& rect) noexcept;
    void lay_ring(const FrameRow& row, const RingShape& ring) const noexcept;
    void lay_sphere(const FrameRow& row, int lane) const noexcept;
    // The sphere's light on a grid of every other pixel from its centre (the
    // light is smooth; its rim is worked out at every pixel): grid row `row`,
    // red, green and blue at each of its columns.
    static constexpr int kGridColumns = size / 2 + 3;
    static constexpr int kGridFirst = -(size / 4) - 1;
    void shade_grid_row(int row, float* out) const noexcept;
    const float* grid_row(int lane, int row) const noexcept;

    std::unique_ptr<std::uint8_t[]> buffer_;
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

    PresenceProfile profile_;
    bool shared_worker_{};
    std::unique_ptr<DeviceFrame> device_;
    Sphere sphere_{};
    FrameLight glow_frame_{};
    std::array<RingShape, kRings> rings_{};
    std::vector<FrameLight> absorbed_;
    // Each lane's row, and the two grid rows it last worked out (each frame
    // starts afresh).
    std::array<std::unique_ptr<std::uint8_t[]>, DeviceFrame::lanes> rows_;
    mutable std::array<std::array<std::array<float, 3 * kGridColumns>, 2>, DeviceFrame::lanes> grid_rows_{};
    mutable std::array<std::array<int, 2>, DeviceFrame::lanes> grid_index_{};
};

}  // namespace aura::ui

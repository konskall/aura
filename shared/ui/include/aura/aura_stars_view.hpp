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

// Γραφίτης's presence, Αστερισμός: a sphere of 300 points of light turning
// slowly. Its surface lifts with the voice while listening, a bright band
// sweeps it while thinking and waves run from the centre while speaking. The
// points are stamped with additive light into a 264 x 264 canvas that fits
// between the assistant screens' caption and their buttons; the glow is a
// light field behind the screen's content. Reference: the Stars concept in
// docs/design/themes/presence-reference.js. On the board (PresenceProfile::
// Device) the glow and the points are one opaque frame, the glow's square,
// drawn on the worker.
class AuraStarsView final : public PresenceView {
public:
    static constexpr int size = 264;
    static constexpr std::size_t point_count = 300;
    static constexpr std::uint32_t frame_interval_ms = 66;
    // The glow: a light field 1.9 times the sphere's radius.
    static constexpr int glow_radius = 186;

    // The platform's profile, drawn on its shared worker once one is started.
    AuraStarsView();
    // A profile, and a worker to draw the device frames on (none: the
    // caller's thread).
    AuraStarsView(PresenceProfile profile, FrameWorker* worker);
    // Waits for a frame on the worker: it reads the dots and strips below.
    ~AuraStarsView() override;

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
    // How far the voice lifted the points in the last frame (share of the
    // radius), and where the thinking band is (-1 .. 1).
    float swell() const noexcept { return swell_; }
    float band() const noexcept { return band_; }
    PresenceProfile profile() const noexcept { return profile_; }
    // The board's frame (null in the presentation); the tests redraw it whole.
    DeviceFrame* device_frame() const noexcept { return device_.get(); }

private:
    struct Point {
        float x;
        float y;
        float z;
        float seed;
    };
    // One point's light, in canvas pixels: where it lies, and the square it
    // reaches (its pixel and reach), worked out once a frame.
    struct Dot {
        float x;
        float y;
        float radius;
        float red;
        float green;
        float blue;
        int column;
        int row;
        int reach;
    };
    static constexpr int kStripRows = 8;

    void advance(float seconds) noexcept;
    void render();
    template <typename Stamp>
    void each_point(Stamp&& stamp);
    void stamp(float x, float y, float radius, float red, float green, float blue) noexcept;
    void update_glow();
    // The board: the frame's points and glow, then its drawing on the worker.
    lv_obj_t* attach_device(lv_obj_t* parent, int center_x, int center_y);
    void prepare_device();
    bool start_device_frame();
    FrameLight glow_light() const noexcept;
    static void draw_device(void* view, int lane, std::uint16_t* pixels, const FrameRect& area,
                            const FrameRect& rect) noexcept;

    // While drawing, a pixel's three bytes hold its added red, green and blue;
    // a last pass turns them into the RGB565 colour and the A8 alpha.
    std::unique_ptr<std::uint8_t[]> buffer_;
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

    PresenceProfile profile_;
    bool shared_worker_{};
    std::unique_ptr<DeviceFrame> device_;
    int center_x_{};
    int center_y_{};
    std::vector<Dot> dots_;
    FrameLight glow_frame_{};
    std::vector<FrameLight> absorbed_;
    // Each lane's row and strip of light.
    std::array<std::unique_ptr<std::uint8_t[]>, DeviceFrame::lanes> rows_;
    std::array<std::unique_ptr<std::uint8_t[]>, DeviceFrame::lanes> lights_;
};

}  // namespace aura::ui

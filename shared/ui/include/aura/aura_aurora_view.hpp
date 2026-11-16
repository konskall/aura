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

// Σέλας's presence: three ribbons of light. They wave gently while waiting,
// follow the voice while listening, tie into a loop with a light travelling
// round it while thinking and dance with the voice while speaking. The
// ribbons are drawn per pixel into a 432 x 200 band with additive light; their
// haze is a light field behind the screen's content. Reference: the Aurora
// concept in docs/design/themes/presence-reference.js. On the board
// (PresenceProfile::Device) the haze reaches past the screen's edges, so the
// haze, the light fields under it and the ribbons are one opaque frame of the
// whole screen, drawn on the worker.
class AuraAuroraView final : public PresenceView {
public:
    static constexpr int width = 432;
    static constexpr int height = 200;
    static constexpr std::uint32_t frame_interval_ms = 66;
    // The haze: a light field of 270 px around the band's centre.
    static constexpr int haze_radius = 270;

    // The platform's profile, drawn on its shared worker once one is started.
    AuraAuroraView();
    // A profile, and a worker to draw the device frames on (none: the
    // caller's thread).
    AuraAuroraView(PresenceProfile profile, FrameWorker* worker);
    // Waits for a frame on the worker: it reads the curves and strips below.
    ~AuraAuroraView() override;

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
    // How far the ribbons sway (px) and how far they have tied into the loop (0..1).
    float amplitude() const noexcept { return amplitude_; }
    float loop_share() const noexcept { return weights_[2]; }
    PresenceProfile profile() const noexcept { return profile_; }
    // The board's frame (null in the presentation); the tests redraw it whole.
    DeviceFrame* device_frame() const noexcept { return device_.get(); }

private:
    // One soft dot of the loop (band pixels), as soft_dot() lays it, with the
    // square it reaches (its pixel and reach) worked out once a frame.
    struct Dot {
        float x;
        float y;
        float radius;
        float inverse;
        float red;
        float green;
        float blue;
        float peak;
        bool squared;
        int column;
        int row;
        int reach;
    };
    // What a device frame draws, set on the UI thread before the worker reads it.
    struct Job {
        std::array<float, 4> weights{};
        std::array<float, 3> phases{};
        float amplitude{};
        float seconds{};
        float drive{};
        float strength{};
        bool loop{};
    };
    static constexpr int kStripRows = 8;

    void advance(float seconds) noexcept;
    void render();
    void wave(float drive, float strength);
    void stamp(float drive, float strength);
    void add_light(int x, int y, float red, float green, float blue) noexcept;
    void soft_dot(float x, float y, float radius, bool squared, float red, float green, float blue,
                  float peak) noexcept;
    void update_haze();
    // The board: the frame's haze, light fields and ribbons, then its drawing on the worker.
    lv_obj_t* attach_device(lv_obj_t* parent, int center_x, int center_y);
    void prepare_device();
    bool start_device_frame();
    FrameLight haze_light() const noexcept;
    void device_curves() noexcept;
    void device_dots();
    void bucket_dots();
    static void begin_device(void* view, const FrameRect& redraw);
    static void draw_device(void* view, int lane, std::uint16_t* pixels, const FrameRect& area,
                            const FrameRect& rect) noexcept;
    void light_strip(std::uint8_t* light, int top, int bottom, int first, int last) const noexcept;

    // While drawing, a pixel's three bytes hold its added red, green and blue;
    // a last pass turns them into the RGB565 colour and the A8 alpha.
    std::unique_ptr<std::uint8_t[]> buffer_;
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

    PresenceProfile profile_;
    bool shared_worker_{};
    std::unique_ptr<DeviceFrame> device_;
    int center_x_{};
    int center_y_{};
    bool placed_{};
    Job job_{};
    FrameLight haze_frame_{};
    std::vector<FrameLight> absorbed_;
    // Per band column: where it lies along the ribbons, its fade, and the
    // ribbons' envelope there and a column on (the presentation's curve()).
    std::unique_ptr<float[]> along_;
    std::unique_ptr<float[]> next_;
    std::unique_ptr<float[]> fade_;
    std::unique_ptr<float[]> envelope_;
    std::unique_ptr<float[]> next_envelope_;
    // Per ribbon and band column, on the worker: the curve's height and the
    // share of a pixel across it.
    std::unique_ptr<float[]> curve_;
    std::unique_ptr<float[]> across_;
    // ...and the band rows its layers light there.
    std::unique_ptr<std::int16_t[]> tops_;
    std::unique_ptr<std::int16_t[]> bottoms_;
    // The four layers' light by the distance across a curve, 1/16 px a step
    // (the reach is at most 24 px): worked out once a frame.
    static constexpr int kLayerSteps = 402;
    std::array<float, kLayerSteps> layers_{};
    std::vector<Dot> dots_;
    // The dots each eight band rows hold (indices, in drawing order).
    static constexpr int kBuckets = (height + kStripRows - 1) / kStripRows;
    std::vector<std::uint16_t> bucket_dots_;
    std::array<int, kBuckets + 1> bucket_starts_{};
    // Each lane's row and strip of light.
    std::array<std::unique_ptr<std::uint8_t[]>, DeviceFrame::lanes> rows_;
    std::array<std::unique_ptr<std::uint8_t[]>, DeviceFrame::lanes> lights_;
};

}  // namespace aura::ui

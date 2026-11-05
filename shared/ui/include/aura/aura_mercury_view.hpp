#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "aura/aura_frame_worker.hpp"
#include "aura/aura_presence.hpp"
#include "lvgl.h"

namespace aura::ui {

using MercuryState = PresenceState;

// How much of the presentation Synapse draws.
enum class SynapseQuality : std::uint8_t {
    Full,     // the approved presentation
    Reduced,  // the lattice from fewer steps: about 70 % of the time, nearly the same light
};

// Halo's presence, Synapse: the approved browser presentation
// (artifacts/halo-synapse.html) drawn natively at 1:1 scale. Eight
// point-reflected kinetic light paths surround a luminous reactor, with orbital
// traces, resonance ticks, wavefronts and particles; every element adds light,
// as the presentation's 'lighter' canvas does. The frame spans the screen width
// and the band between the assistant captions and the controls, and lies behind
// every label and control. The historical type name keeps the public AuraUi API
// and the simulator/firmware composition source-compatible.
class AuraMercuryView final : public PresenceView {
public:
    static constexpr int width = 480;
    static constexpr int height = 252;
    static constexpr std::uint32_t frame_interval_ms = 50;
    // A full frame slower than this switches Synapse to the reduced frame, and a
    // reduced frame faster than restore_below_ms switches it back.
    static constexpr std::uint32_t reduce_above_ms = 100;
    static constexpr std::uint32_t restore_below_ms = 40;
    // Milliseconds from a steady clock; Synapse reads it before and after each frame.
    using Clock = std::uint32_t (*)() noexcept;

    // The idle time to leave after a frame that took `render_ms` to draw: the rest
    // of its 50 ms, and never less than twice the drawing, so drawing takes at most
    // a third of the time on any device.
    static std::uint32_t paced_wait(std::uint32_t render_ms) noexcept;
    // The quality of the next frame after one drawn at `current` in `render_ms`.
    static SynapseQuality next_quality(SynapseQuality current, std::uint32_t render_ms) noexcept;

    // Frames are drawn on `worker` when there is one (by default the platform's
    // shared worker), otherwise on the thread that ticks the view.
    explicit AuraMercuryView(Clock clock = nullptr, FrameWorker* worker = FrameWorker::shared());
    // Waits for a frame the worker may still be drawing into this view.
    ~AuraMercuryView() override;
    AuraMercuryView(const AuraMercuryView&) = delete;
    AuraMercuryView& operator=(const AuraMercuryView&) = delete;

    lv_obj_t* attach(lv_obj_t* parent, int center_x, int center_y) override;
    void detach() noexcept override;
    void set_state(MercuryState state) noexcept override;
    void set_level(float level) noexcept override;
    void set_reduced_motion(bool reduced) noexcept override;
    bool tick(std::uint32_t elapsed_ms) override;

    std::uint64_t frame_count() const noexcept override { return frame_count_; }
    std::size_t buffer_bytes() const noexcept override { return buffer_.size() * (second_ ? 2U : 1U); }
    lv_obj_t* object() const noexcept override { return image_; }
    std::uint32_t frame_signature() const noexcept;
    SynapseQuality quality() const noexcept { return quality_; }
    std::uint32_t last_frame_ms() const noexcept override { return last_frame_ms_; }
    bool reduced_quality() const noexcept override { return quality_ == SynapseQuality::Reduced; }
    // The quality of the next frame; later frames adapt it to how long they take.
    void set_quality(SynapseQuality quality) noexcept { quality_ = quality; }
    // Pixels the last frame's strokes, glows and circles visited.
    std::uint64_t frame_work() const noexcept { return frame_work_; }

private:
    static constexpr int kSteps = 168;  // samples per light path, as in the presentation
    static constexpr int kPaths = 8;    // four paths and their point reflections
    // The presentation's slow background tint changes so little per pixel that
    // one value per 8 x 8 cell is indistinguishable from per-pixel evaluation.
    static constexpr int kTintCell = 8;
    static constexpr int kTintColumns = (width + kTintCell - 1) / kTintCell;
    static constexpr int kTintRows = (height + kTintCell - 1) / kTintCell;
    // The vignette's factor (of 256) by squared distance from the centre, in steps of 32.
    static constexpr int kVignetteShift = 5;
    static constexpr int kVignetteSteps = ((width * width + height * height) / 4 >> kVignetteShift) + 2;

    struct Point {
        float x;
        float y;
        float z;
    };
    // Light added to a pixel: 0-255 per channel, already scaled by its opacity.
    struct Light {
        float red;
        float green;
        float blue;
    };

    using Frame = std::array<std::uint8_t, width * height * 3>;

    void advance(float seconds) noexcept;
    // Draws the next frame into `target` and returns how long that took.
    std::uint32_t draw_into(std::uint8_t* target) noexcept;
    void render() noexcept;
    static void draw_on_worker(void* view) noexcept;
    // Shows `frame` and paces the next one by the `render_ms` it took to draw.
    void show(std::uint8_t* frame, std::uint32_t render_ms, bool from_worker) noexcept;
    std::uint32_t now_ms() const noexcept;
    void build_paths() noexcept;
    const Point& path(int index, int sample) const noexcept {
        return paths_[static_cast<std::size_t>(index * (kSteps + 1) + sample)];
    }

    void add(int x, int y, const Light& light, float coverage) noexcept;
    void stroke(const Point& from, const Point& to, float line_width, const Light& light) noexcept;
    void stroke_path(const Point* points, int count, float line_width, const Light& light) noexcept;
    void dashed_arc(float rotation, float scale_y, float radius, float start, float end,
                    const std::array<float, 4>& dashes, double offset, float line_width, const Light& light) noexcept;
    void radial_glow(float x, float y, float radius, int red, int green, int blue, float alpha) noexcept;
    void fill_circle(float x, float y, float radius, const Light& light) noexcept;

    void draw_orbits() noexcept;
    void draw_resonance() noexcept;
    void draw_waves() noexcept;
    void draw_lattice() noexcept;
    void draw_particles() noexcept;
    void draw_core() noexcept;
    void finish() noexcept;

    // Light is gathered as 8-bit blue, green, red (LVGL's RGB888 order) and then
    // either shown as is or packed in place to RGB565 on a 16-bit display.
    alignas(4) Frame buffer_{};
    // With a worker, a second frame: the worker draws into one while LVGL shows the other.
    std::unique_ptr<Frame> second_;
    std::uint8_t* canvas_{};  // the frame being drawn
    std::uint8_t* shown_{};   // the frame LVGL shows
    std::array<Point, kPaths * (kSteps + 1)> paths_{};
    std::array<std::array<std::uint8_t, 3>, kTintColumns * kTintRows> tint_{};
    std::array<std::uint16_t, kVignetteSteps> vignette_{};
    lv_image_dsc_t descriptor_{};
    lv_obj_t* image_{};
    std::array<float, 4> weights_{1.0F, 0.0F, 0.0F, 0.0F};  // idle, listening, thinking, speaking
    MercuryState state_{MercuryState::Idle};
    float level_{};
    // Double precision: a float clock a few days old can no longer advance by
    // one 50 ms frame, and the always-on Home keeps Synapse running for days.
    double seconds_{};
    double phase_{};  // seconds weighted by the state's speed
    float speed_{0.32F};
    float spread_{0.14F};
    float weave_{0.26F};
    float wave_{0.16F};
    float drive_{};  // the voice level while listening or speaking
    Clock clock_;
    FrameWorker* worker_;
    std::uint32_t idle_ms_{};                       // idle time since the last frame
    std::uint32_t wait_ms_{frame_interval_ms};      // idle time the last frame asks for
    std::uint32_t debt_ms_{};       // drawing on the ticking thread, not yet back in its elapsed time
    std::uint32_t animation_ms_{};  // time since the last frame began
    std::uint32_t drawn_ms_{};      // how long the worker took for its frame
    std::uint32_t last_frame_ms_{};  // how long the last frame shown took to draw
    bool drawing_{};                // a frame is on the worker
    SynapseQuality quality_{SynapseQuality::Full};
    SynapseQuality frame_quality_{SynapseQuality::Full};  // the frame being drawn: the worker reads only this
    std::uint64_t frame_work_{};
    std::uint64_t frame_count_{};
    bool reduced_motion_{};
    bool attached_before_{};
};

}  // namespace aura::ui

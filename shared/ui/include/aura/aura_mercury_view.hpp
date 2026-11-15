#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "aura/aura_frame_worker.hpp"
#include "aura/aura_presence.hpp"
#include "lvgl.h"

namespace aura::ui {

using MercuryState = PresenceState;

// A panel Synapse's frames can go to straight, past LVGL. On the board LVGL
// spent 47-72 ms showing each of Synapse's frames (measured 2026-11-14), and
// the frame and LVGL fought over the PSRAM: a few frames a second at best. The
// platform gives the strips' memory (internal RAM its DMA reads) and sends rows
// of the screen while it holds LVGL's lock.
class SynapseOutput {
public:
    virtual ~SynapseOutput() = default;
    // Fast memory for strip `index` (0 or 1), `bytes` long, that send() may
    // take; null when there is none.
    virtual void* strip(int index, std::size_t bytes) = 0;
    // LVGL's lock, waiting at most `timeout_ms`; false when not taken.
    virtual bool lock(std::uint32_t timeout_ms) = 0;
    virtual void unlock() = 0;
    // Under the lock: `rows` rows of the screen from `top`, `columns` wide from
    // `left`, RGB565 in the panel's byte order (high byte first). The memory
    // may be written again once the next send() has returned.
    virtual bool send(int left, int top, int columns, int rows, const void* pixels) = 0;
};
// The platform's output (set once at start-up), or null: the simulator.
void set_synapse_output(SynapseOutput* output) noexcept;
SynapseOutput* synapse_output() noexcept;

// How much of the presentation Synapse draws.
enum class SynapseQuality : std::uint8_t {
    Full,     // the approved presentation
    Reduced,  // the lattice from fewer steps: about 70 % of the time, nearly the same light
};

// Where Synapse is drawn. The simulator draws the approved presentation. The
// board, whose ESP32-S3 took ~340 ms a frame for it (the first flash,
// 2026-11-14), draws the device frame: every layer of the presentation at full
// resolution, its lines sharp (fast fixed-point lines one pixel across) and its
// lattice's halo from every fourth step. It is drawn in strips six rows high,
// in fast memory, from a list of the frame's shapes, and is RGB565 everywhere
// (the board's format); its middle rows go to the panel straight where nothing
// LVGL draws lies over them (SynapseOutput). The first device frame gathered
// its light at half resolution in PSRAM: the owner found it blurred and it
// showed a few frames a second.
enum class SynapseProfile : std::uint8_t { Presentation, Device };

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
    // The device frame's rows that go to the panel straight: below the date
    // line Home lays over the band's top, above its caption at the bottom
    // (LVGL draws those rows). Strips of six rows: whole strips, and panel
    // windows that start on an even row and end on an odd one.
    static constexpr int strip_rows = 6;
    static constexpr int direct_top = 30;
    static constexpr int direct_bottom = 246;
    // Milliseconds from a steady clock; Synapse reads it before and after each frame.
    using Clock = std::uint32_t (*)() noexcept;

    // The idle time to leave after a frame that took `render_ms` to draw: the rest
    // of its 50 ms, and never less than twice the drawing, so drawing takes at most
    // a third of the time on any device.
    static std::uint32_t paced_wait(std::uint32_t render_ms) noexcept;
    // The same after a frame drawn on a worker, where the UI thread never draws:
    // the rest of its 50 ms, and never less than a quarter of the drawing (it
    // draws at most four fifths of the time). The board's worker draws below
    // every other task of its core; the rest leaves its idle task its turn.
    static std::uint32_t worker_wait(std::uint32_t render_ms) noexcept;
    // The quality of the next frame after one drawn at `current` in `render_ms`.
    static SynapseQuality next_quality(SynapseQuality current, std::uint32_t render_ms) noexcept;
    // The board's square root inside the device frame's loops: an estimate of
    // 1/sqrt refined twice (Newton), within 1e-5 of the root; 0 for 0 and
    // below. The C library's sqrtf runs from flash there, and a call passes
    // floats through the integer registers: this one is inlined. Its bits
    // move with __builtin_bit_cast: the board's compiler called ROM's memcpy
    // for a 4-byte std::memcpy, twice a root.
    static float estimated_root(float value) noexcept {
        if (!(value > 0.0F)) return 0.0F;
        const auto bits = 0x5F375A86U - (__builtin_bit_cast(std::uint32_t, value) >> 1);
        float inverse = __builtin_bit_cast(float, bits);
        const float half = 0.5F * value;
        inverse *= 1.5F - half * inverse * inverse;
        inverse *= 1.5F - half * inverse * inverse;
        return value * inverse;
    }
    // 1/sqrt, as estimated_root() works it out on the way; 0 for 0 and below.
    static float estimated_inverse_root(float value) noexcept {
        if (!(value > 0.0F)) return 0.0F;
        const auto bits = 0x5F375A86U - (__builtin_bit_cast(std::uint32_t, value) >> 1);
        float inverse = __builtin_bit_cast(float, bits);
        const float half = 0.5F * value;
        inverse *= 1.5F - half * inverse * inverse;
        inverse *= 1.5F - half * inverse * inverse;
        return inverse;
    }
    // 1/value inside the device frame's loops, for a value of either sign
    // between 1e-6 and 1e6 in size: an estimate refined three times (Newton),
    // within 1e-6 of it. The board's compiler divides floats in ROM's
    // __divsf3, a call of ~25 dependent steps.
    static float estimated_reciprocal(float value) noexcept {
        const auto bits = __builtin_bit_cast(std::uint32_t, value);
        float inverse = __builtin_bit_cast(float, (0x7EF311C7U - (bits & 0x7FFFFFFFU)) | (bits & 0x80000000U));
        inverse *= 2.0F - value * inverse;
        inverse *= 2.0F - value * inverse;
        inverse *= 2.0F - value * inverse;
        return inverse;
    }

    // Draws on the platform's shared worker, looked up when the view is shown:
    // app_main builds the UI, and Halo's Synapse with it, before it starts the
    // worker (on the first flash every frame was drawn on the UI thread with the
    // display locked, and the board froze). The platform's profile: Device on
    // the board, Presentation elsewhere.
    AuraMercuryView();
    // Draws on `worker`, or on the thread that ticks the view when it is null;
    // `helper` (with a worker) draws half of each device frame's strips.
    AuraMercuryView(Clock clock, FrameWorker* worker, FrameWorker* helper = nullptr);
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
    std::uint32_t next_tick_ms() const noexcept override;

    std::uint64_t frame_count() const noexcept override { return frame_count_; }
    std::size_t buffer_bytes() const noexcept override { return buffer_.size() * (second_ ? 2U : 1U); }
    lv_obj_t* object() const noexcept override { return image_; }
    std::uint32_t frame_signature() const noexcept;
    SynapseQuality quality() const noexcept { return quality_; }
    std::uint32_t last_frame_ms() const noexcept override { return last_frame_ms_; }
    bool reduced_quality() const noexcept override { return quality_ == SynapseQuality::Reduced; }
    // The quality of the next frame; later frames adapt it to how long they take.
    void set_quality(SynapseQuality quality) noexcept { quality_ = quality; }
    // The profile of the frames from now on (it waits for a frame on the worker).
    void set_profile(SynapseProfile profile);
    SynapseProfile profile() const noexcept { return profile_; }
    // The rows the device frame draws at a time: strip_rows (the panel takes
    // six at a time), or any larger whole share of the frame, which draws the
    // same frame (a check draws it in one strip). Only six-row strips go to
    // the panel straight.
    void set_strip_height(int rows);
    // Pixels the last frame's strokes, glows and circles visited.
    std::uint64_t frame_work() const noexcept { return frame_work_; }
    // Frames shown whose middle rows the worker sent to the panel straight.
    std::uint64_t direct_frames() const noexcept { return direct_frames_; }

private:
    static constexpr int kSteps = 168;  // samples per light path, as in the presentation
    static constexpr int kPaths = 8;    // four paths and their point reflections
    // The presentation's slow background tint changes so little per pixel that
    // one value per 8 x 8 cell is indistinguishable from per-pixel evaluation.
    static constexpr int kTintCell = 8;
    static constexpr int kTintColumns = (width + kTintCell - 1) / kTintCell;
    static constexpr int kTintRows = (height + kTintCell - 1) / kTintCell;
    // The device frame works out this many rows of tint cells a frame: all of
    // them every 8 frames, a fraction of a pixel's drift.
    static constexpr int kTintRowsPerFrame = 4;
    // The vignette's factor (of 256) by squared distance from the centre, in steps of 32.
    static constexpr int kVignetteShift = 5;
    static constexpr int kVignetteSteps = ((width * width + height * height) / 4 >> kVignetteShift) + 2;
    // A strip's light is 8-bit blue, green and red; finished, the same memory
    // holds the strip for the panel (two bytes a pixel, written over it).
    static constexpr int kStrips = height / strip_rows;
    static_assert(height % strip_rows == 0, "the strips cover the frame exactly");
    static_assert(direct_top % strip_rows == 0 && direct_bottom % strip_rows == 0, "direct rows are whole strips");
    static_assert(strip_rows % 2 == 0, "each strip starts on an even row and ends on an odd one");
    // How long a direct strip waits for LVGL's lock before the frame goes to
    // LVGL instead (a full screen LVGL draws takes longer).
    static constexpr std::uint32_t kLockWaitMs = 40;

    // One soft shape (or spark) of a device frame, kept for the strips it
    // crosses; its sharp lines are kept as spans.
    enum class ShapeKind : std::uint8_t { Stroke, Glow, CentredGlow, Nucleus, Disc };

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
    struct Shape {
        ShapeKind kind;
        std::int16_t top;  // the frame rows it may light
        std::int16_t bottom;
        float x0, y0, x1, y1;  // a stroke's ends; a glow's or disc's centre (x0, y0)
        float size;            // width, radius, or the nucleus's sphere
        Light light;
    };
    // A sharp line, set up once for every strip it lights: its columns (rows,
    // when steep) and the rows each column's centre lies between.
    struct Span {
        std::int32_t position;  // at the first column, in 1/256 of a pixel
        std::int32_t step;      // per column
        std::int32_t red;       // light at full coverage, in 1/65536 of a level
        std::int32_t green;
        std::int32_t blue;
        float columns_per_row;  // 256 / step: a strip finds its columns from it
        std::int16_t first;     // columns
        std::int16_t last;
        std::int16_t top;  // the frame rows it lights
        std::int16_t bottom;
        bool steep;
    };
    // Where the device frame's shapes add their light: one strip's, whose first
    // row is frame row `first_row`, lit in rows [top, bottom); `work` counts
    // the pixels they visit. The worker and its helper each draw their own.
    struct Strip {
        std::uint8_t* light;
        int first_row;
        int top;
        int bottom;
        std::uint64_t work;
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
    // The frame's format: RGB565 for the device frame and on a 16-bit display.
    void apply_format() noexcept;

    // Which of a layer's parts a pass draws: the device frame takes its soft
    // parts' shapes first, then its sharp ones (lines one pixel across).
    enum class Part : std::uint8_t { Soft = 1, Sharp = 2, Both = 3 };
    bool draws(Part part) const noexcept {
        return (static_cast<std::uint8_t>(parts_) & static_cast<std::uint8_t>(part)) != 0;
    }

    // `light` added to one pixel of light_ at `coverage`, saturating.
    static void add_to(std::uint8_t* pixel, const Light& light, float coverage) noexcept;
    void add(int x, int y, const Light& light, float coverage) noexcept;
    // A line one pixel across (Xiaolin Wu's), lit by its width. `last` keeps
    // the end pixel: a polyline's joints light once.
    void sharp_line(const Point& from, const Point& to, float line_width, const Light& light, bool last) noexcept;
    // The line set up for drawing; false when it lights nothing.
    static bool line_span(const Point& from, const Point& to, float line_width, const Light& light, bool last,
                          Span& span) noexcept;
    // Its light into the strip's rows; the frame's work counts its pixels
    // once, by span_pixels(), as it is recorded.
    static void draw_span(const Span& span, Strip& strip) noexcept;
    static std::uint64_t span_pixels(const Span& span) noexcept;
    // The strip the primitives draw into outside the device frame's strips.
    Strip whole_strip() const noexcept { return {light_, light_row_, clip_top_, clip_bottom_, 0}; }
    void sharp_disc(float x, float y, float radius, const Light& light) noexcept;
    void stroke(const Point& from, const Point& to, float line_width, const Light& light) noexcept;
    void stroke_path(const Point* points, int count, float line_width, const Light& light) noexcept;
    void dashed_arc(float rotation, float scale_y, float radius, float start, float end,
                    const std::array<float, 4>& dashes, double offset, float line_width, const Light& light) noexcept;
    void radial_glow(float x, float y, float radius, int red, int green, int blue, float alpha) noexcept;
    void glow(float x, float y, float radius, const Light& light) noexcept;
    // radial_glow() at the frame's centre, from a falloff kept once per radius.
    void centred_glow(float radius, int red, int green, int blue, float alpha) noexcept;
    void centred_light(float radius, const Light& light) noexcept;
    // The reactor's nucleus: a radial gradient from radius 1 to its sphere's edge.
    void nucleus(float sphere) noexcept;
    void fill_circle(float x, float y, float radius, const Light& light) noexcept;

    void draw_orbits() noexcept;
    void draw_resonance() noexcept;
    void draw_waves() noexcept;
    void draw_lattice() noexcept;
    void draw_particles() noexcept;
    void draw_core() noexcept;
    // The background tint: every cell for the presentation, a few rows of
    // cells a frame for the device frame (all of them on its first).
    void update_tint() noexcept;
    void finish() noexcept;
    // One finished pixel of the frame being drawn, packed to RGB565 on a 16-bit display.
    void store(int x, int y, int red, int green, int blue) noexcept;

    // ---- The device frame, in strips ----
    // Strips first, first + step, ... of a frame: the worker the even ones and
    // its helper the odd ones, or the worker all of them.
    struct StripRun {
        AuraMercuryView* view;
        int first;
        int step;
        int memory;        // the first of the two strip memories it takes turns with
        bool sending;      // send the direct strips; afterwards: every one was sent
        std::uint64_t work;
    };
    void render_device() noexcept;
    void render_strips(StripRun& run) const noexcept;
    static void render_strips_on_helper(void* run) noexcept;
    void record(ShapeKind kind, float x0, float y0, float x1, float y1, float size, const Light& light, float reach);
    // The shape as recorded, drawn into the strip.
    void replay(const Shape& shape, Strip& strip) const noexcept;
    // The device frame's own soft shapes: the presentation's light, with the
    // work a pixel in integers and few floats.
    void device_stroke(const Shape& shape, Strip& strip) const noexcept;
    void device_glow(const Shape& shape, Strip& strip) const noexcept;
    void device_centred(const Shape& shape, Strip& strip) const noexcept;
    void device_nucleus(const Shape& shape, Strip& strip) const noexcept;
    void device_disc(const Shape& shape, Strip& strip) const noexcept;
    // Memory for strip light `index` (0-3), `bytes` long: the platform's, else
    // the view's own.
    std::uint8_t* strip_light(SynapseOutput* output, int index, std::size_t bytes);
    // The `rows` rows from `top` finished, RGB565, in `light` itself and into
    // the frame being drawn; then, `for_panel`, with each pixel's high byte
    // first in `light`.
    void finish_strip(int top, int rows, std::uint8_t* light, bool for_panel) const noexcept;

    // ---- Direct output ----
    // Before a frame goes to the worker (the ticking thread, under LVGL's lock).
    void prepare_direct() noexcept;
    // Nothing that LVGL draws lies over the direct rows: Synapse is shown as
    // drawn (opaque, untransformed, inside its parents) on the active screen,
    // and nothing drawn after it, in the screen or the layers above, may draw
    // there.
    bool unobstructed() const noexcept;
    // A strip to the panel under LVGL's lock, unless LVGL has invalidated the
    // direct rows since the frame began.
    bool send_strip(SynapseOutput& output, int top, const std::uint8_t* pixels) const noexcept;
    // LVGL's invalidations over the direct rows, and when it has drawn them.
    static void display_event(lv_event_t* event) noexcept;
    void listen(lv_display_t* display) noexcept;
    void stop_listening() noexcept;

    // Light is gathered as 8-bit blue, green, red (LVGL's RGB888 order) and then
    // either shown as is or packed to RGB565.
    alignas(4) Frame buffer_{};
    // With a worker, a second frame: the worker draws into one while LVGL shows the other.
    std::unique_ptr<Frame> second_;
    std::uint8_t* canvas_{};  // the frame being drawn
    std::uint8_t* shown_{};   // the frame LVGL shows
    // Where the light primitives add light (RGB888, `width` pixels a row): the
    // presentation's frame itself, or the device frame's strip, whose first row
    // is frame row light_row_. They light only rows [clip_top_, clip_bottom_).
    std::uint8_t* light_{};
    int light_row_{0};
    int clip_top_{0};
    int clip_bottom_{height};
    Part parts_{Part::Both};
    bool sharp_{false};      // the primitives draw lines and discs sharp
    bool recording_{false};  // the primitives keep their shapes for the strips
    // The edge fade of each row, 0-256, as finish() works it out.
    std::array<std::uint16_t, height> row_fade_{};
    // The device frame: its shapes and sharp lines and, for each strip, those
    // that touch it.
    std::vector<Shape> shapes_;
    std::vector<Span> spans_;
    std::array<std::vector<std::uint16_t>, kStrips> strip_shapes_;
    std::array<std::vector<std::uint16_t>, kStrips> strip_spans_;
    int strip_height_{strip_rows};
    // Four strips' light (the worker's two, its helper's two), when the
    // platform gives none (or not that much); and this frame's four.
    std::unique_ptr<std::uint8_t[]> strip_memory_;
    std::size_t strip_memory_bytes_{};
    std::array<std::uint8_t*, 4> strips_{};
    // The reactor's two glows (radius 112 and 58) for the device frame: for
    // each row of a quadrant of pixels, its falloff (1 - distance / radius,
    // 0-255) and how many pixels it reaches. Worked out once: each frame it
    // took 25,000 square roots on the board.
    static constexpr int kOuterSpan = 113;
    static constexpr int kInnerSpan = 59;
    std::unique_ptr<std::uint8_t[]> reactor_;
    // The device frame's background, RGB888: the tint under the vignette and
    // the edge fade, kept a few rows at a time with the tint. Each strip
    // starts from it, and each shape's light comes dimmed by the vignette
    // where it lies (attenuation()), so a strip's finish only packs its
    // pixels: on the board that finish took 70 cycles a pixel.
    std::unique_ptr<std::uint8_t[]> background_;
    int background_from_{0};  // the rows this frame's tint moved under: [from, to)
    int background_to_{0};
    void refresh_background(int first_row, int last_row) const noexcept;
    // The vignette and the edge fade at (x, y), 0-256.
    int attenuation(float x, float y) const noexcept;
    // The table for a glow of `radius`, or null; `span` its row length.
    const std::uint8_t* reactor_table(float radius, int& span) const noexcept;
    int tint_row_{-1};  // the device frame's next row of tint cells; -1: all of them
    // Direct output: the ticking thread decides for each frame on the worker
    // whether its middle rows go to the panel straight (frame_direct_). LVGL's
    // invalidations of those rows since the frame began (frame_obstructed_),
    // or not yet drawn (pending_overlap_), keep them for LVGL.
    lv_display_t* display_{};               // the display display_event() listens to
    lv_area_t direct_area_{0, 0, -1, -1};  // the direct rows on the screen (none yet)
    bool frame_direct_{false};
    bool direct_sent_{false};  // the worker sent every direct strip of its frame
    std::atomic<bool> frame_obstructed_{false};
    std::atomic<bool> pending_overlap_{false};
    bool rendering_{false};  // LVGL is drawing: its INVALIDATE_AREA events then only try its rounder
    std::uint64_t direct_frames_{};
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
    FrameWorker* helper_;
    bool shared_worker_{};  // the platform's (and its helper), looked up when the view is shown
    std::uint32_t idle_ms_{};                       // idle time since the last frame
    std::uint32_t wait_ms_{frame_interval_ms};      // idle time the last frame asks for
    std::uint32_t debt_ms_{};       // drawing on the ticking thread, not yet back in its elapsed time
    std::uint32_t animation_ms_{};  // time since the last frame began
    std::uint32_t drawn_ms_{};      // how long the worker took for its frame
    std::uint32_t last_frame_ms_{};  // how long the last frame shown took to draw
    bool drawing_{};                // a frame is on the worker
    SynapseProfile profile_{SynapseProfile::Presentation};
    SynapseQuality quality_{SynapseQuality::Full};
    SynapseQuality frame_quality_{SynapseQuality::Full};  // the frame being drawn: the worker reads only this
    std::uint64_t frame_work_{};
    std::uint64_t frame_count_{};
    bool reduced_motion_{};
    bool attached_before_{};
};

}  // namespace aura::ui

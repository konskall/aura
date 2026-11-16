#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "aura/aura_frame_worker.hpp"
#include "lvgl.h"

// The device frames' hottest loops run from IRAM on the board, as Synapse's:
// from flash they missed the instruction cache the other core shares.
#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define FRAME_FAST IRAM_ATTR
#else
#define FRAME_FAST
#endif

namespace aura::ui {

class SynapseOutput;  // aura_mercury_view.hpp: the platform's panel, past LVGL

// How Μάρμαρο's, Σέλας's and Γραφίτης's presences draw. The simulator keeps
// the approved presentation: an RGB565A8 image over LVGL's own glow, haze and
// rings. On the board LVGL scaled those glows for every frame (70-75 % of its
// time), redrew the whole screen whenever one brightened with the voice, and
// waited for a drawing that held the display lock: 3-12 frames a second
// (measured 2026-11-15). The board draws each presence whole, its glow and
// rings included, into one opaque frame on the worker; LVGL only copies the
// part that changed and draws the screen's labels and buttons over it.
enum class PresenceProfile : std::uint8_t { Presentation, Device };

// Device on the board, Presentation elsewhere.
PresenceProfile default_presence_profile() noexcept;

// Pixels [x1, x2) x [y1, y2) of the presence's parent.
struct FrameRect {
    int x1{};
    int y1{};
    int x2{};
    int y2{};

    bool empty() const noexcept { return x2 <= x1 || y2 <= y1; }
    int width() const noexcept { return x2 - x1; }
    int height() const noexcept { return y2 - y1; }
    // The smallest rectangle holding both (an empty one adds nothing).
    FrameRect united(const FrameRect& other) const noexcept;
    FrameRect clipped(const FrameRect& other) const noexcept;
    // [center - reach, center + reach) on both axes.
    static FrameRect around(int center_x, int center_y, int reach) noexcept;
    bool operator==(const FrameRect& other) const noexcept {
        return x1 == other.x1 && y1 == other.y1 && x2 == other.x2 && y2 == other.y2;
    }
    bool operator!=(const FrameRect& other) const noexcept { return !(*this == other); }
};

// A soft light under a device frame, drawn as LVGL draws it: a light field's
// disc (ui::light_field(): Σέλας's haze and corners, Αστερισμός's glow) or the
// pearl's own glow, tinted, at `opacity` (0..255), over what is below.
enum class LightShape : std::uint8_t { Disc, Pearl };
struct FrameLight {
    int center_x{};
    int center_y{};
    int radius{};
    std::uint32_t tint{};
    std::uint8_t opacity{};
    LightShape shape{LightShape::Disc};
    bool operator==(const FrameLight& other) const noexcept {
        return center_x == other.center_x && center_y == other.center_y && radius == other.radius &&
               tint == other.tint && opacity == other.opacity && shape == other.shape;
    }
    bool operator!=(const FrameLight& other) const noexcept { return !(*this == other); }
    FrameRect bounds() const noexcept { return FrameRect::around(center_x, center_y, radius); }
};

// The light fields among `parent`'s children, in drawing order, that reach
// into `area`: hidden, and returned for a device frame to draw instead.
std::vector<FrameLight> absorb_light_fields(lv_obj_t* parent, const FrameRect& area);

// One row of a device frame while it is composed: red, green and blue bytes
// for pixels [x1, x2) of row y.
struct FrameRow {
    std::uint8_t* rgb{};
    int x1{};
    int x2{};
    int y{};
};

// Lays a light over a row: c = c (1 - a) + tint a.
void lay_light(const FrameRow& row, const FrameLight& light) noexcept;
// The first light on a black row: writes every pixel of it (black where the
// light does not reach), the bytes of clearing the row and lay_light(), so the
// row needs no clearing first. Each presence's glow or haze is its first.
void lay_first_light(const FrameRow& row, const FrameLight& light) noexcept;
// Lays `color` (0xRRGGBB) over pixel x at `alpha` (0..256).
inline void lay_color(std::uint8_t* pixel, std::uint32_t color, int alpha) noexcept {
    const int keep = 256 - alpha;
    pixel[0] = static_cast<std::uint8_t>((pixel[0] * keep + static_cast<int>((color >> 16) & 0xFFU) * alpha) >> 8);
    pixel[1] = static_cast<std::uint8_t>((pixel[1] * keep + static_cast<int>((color >> 8) & 0xFFU) * alpha) >> 8);
    pixel[2] = static_cast<std::uint8_t>((pixel[2] * keep + static_cast<int>(color & 0xFFU) * alpha) >> 8);
}
// Additive light (red, green, blue bytes, saturated) over a row's pixel, as
// LVGL lays the presentation's RGB565A8 light: c = c (1 - max/255) + light.
inline void lay_added_light(std::uint8_t* pixel, const std::uint8_t* light) noexcept {
    const int cover = light[0] > light[1] ? (light[0] > light[2] ? light[0] : light[2]) :
                                            (light[1] > light[2] ? light[1] : light[2]);
    if (cover == 0) return;
    const int keep = 255 - cover;
    for (int channel = 0; channel < 3; ++channel) {
        const int value = (pixel[channel] * keep * 257 + 32768) >> 16;
        const int sum = value + light[channel];
        pixel[channel] = static_cast<std::uint8_t>(sum > 255 ? 255 : sum);
    }
}
// RGB565, as LVGL packs a colour (each channel cut to its bits).
void pack_row(const std::uint8_t* rgb, int count, std::uint16_t* out) noexcept;
// The same, with additive light (red, green, blue bytes per pixel) laid over
// each pixel first, as lay_added_light() lays it.
void pack_lit_row(const std::uint8_t* rgb, const std::uint8_t* light, int count, std::uint16_t* out) noexcept;

// sin(x) inside the device frames' loops, within 2e-6 (the board's sinf runs
// from flash): to [-pi/2, pi/2] by whole turns and sin(pi - x), then a
// polynomial.
inline float device_sin(float x) noexcept {
    constexpr float kPi = 3.14159265F;
    const float turns = x * (0.5F / kPi);
    const float whole = static_cast<float>(static_cast<int>(turns + (turns >= 0.0F ? 0.5F : -0.5F)));
    float angle = x - whole * (2.0F * kPi);
    if (angle > 0.5F * kPi) angle = kPi - angle;
    if (angle < -0.5F * kPi) angle = -kPi - angle;
    const float square = angle * angle;
    return angle * (1.0F + square * (-1.6666667e-1F + square * (8.3333310e-3F +
                                       square * (-1.9840874e-4F + square * 2.7525562e-6F))));
}
inline float device_cos(float x) noexcept { return device_sin(x + 1.57079633F); }
// floor and ceil to int inside the loops, without the C library.
inline int device_floor(float value) noexcept {
    const int whole = static_cast<int>(value);
    return static_cast<float>(whole) > value ? whole - 1 : whole;
}
inline int device_ceil(float value) noexcept {
    const int whole = static_cast<int>(value);
    return static_cast<float>(whole) < value ? whole + 1 : whole;
}

// The square root inside the device frames' loops, as Synapse's
// (AuraMercuryView::estimated_root): the board's sqrtf runs from flash.
inline float device_root(float value) noexcept {
    if (!(value > 0.0F)) return 0.0F;
    const auto bits = 0x5F375A86U - (__builtin_bit_cast(std::uint32_t, value) >> 1);
    float inverse = __builtin_bit_cast(float, bits);
    const float half = 0.5F * value;
    inverse *= 1.5F - half * inverse * inverse;
    inverse *= 1.5F - half * inverse * inverse;
    return value * inverse;
}

// A presence's opaque RGB565 frame on the board: two buffers, the one shown
// while the worker draws the next. Each frame redraws only what changed since
// the frame its buffer last held, and LVGL is told only what changed since the
// one shown. Drawn on the caller's thread when there is no worker.
//
// On the worker a frame's changed strips go to the panel straight where
// nothing LVGL draws lies over them (the platform's SynapseOutput, as
// Synapse's rows do), and LVGL draws only the rest of the change: on the first
// flash LVGL took 30-110 ms to show each frame, and its PSRAM traffic slowed
// the worker on the other core by a third (2026-11-16). A strip LVGL is still
// to draw waits until LVGL has drawn it (or goes to LVGL at the frame's end),
// and one LVGL is told to draw once sent goes to LVGL again: the panel never
// shows an older frame over a newer one.
class DeviceFrame {
public:
    // Draws the frame's pixels in `rect` (the parent's pixels, inside `area`,
    // inside one strip) with lane `lane`'s own memory; `pixels` holds the
    // whole frame, row by row.
    using Draw = void (*)(void* owner, int lane, std::uint16_t* pixels, const FrameRect& area, const FrameRect& rect);
    // Once a frame, before its strips: what they share, for the frame that
    // redraws `redraw`.
    using Begin = void (*)(void* owner, const FrameRect& redraw);

    // About 30 frames a second, LVGL's own refresh.
    static constexpr std::uint32_t interval_ms = 33;
    // A frame is drawn in strips this many rows high from its top (or from the
    // owner's row, align_strips()): whole panel windows (from an even row to
    // an odd one) in the platform's memory.
    static constexpr int strip_rows = 8;
    // How long the strips' sends wait for LVGL's lock before LVGL draws them.
    // The helper sends what is drawn between its own strips: on the board it
    // shares LVGL's core, and LVGL holds the lock only while it runs there,
    // when the helper could not. The worker never waits for it until its
    // frame is drawn, then sends what is left. No send tries the lock without
    // waiting: the board's LVGL adapter logs each miss as an error over the
    // 115200-baud console (~6 ms a line, 2026-11-16).
    static constexpr std::uint32_t lock_wait_ms = 40;
    // The worker and its helper on the board's other core draw a frame's
    // strips together, each taking the next one neither has taken, each with
    // its own memory in the owner (lane 0 the worker's, 1 the helper's): on
    // the first flash one core drew 30-130 ms a frame while the other, LVGL's,
    // drew little once the strips went to the panel straight (2026-11-16).
    static constexpr int lanes = 2;

    DeviceFrame(int width, int height, Draw draw, void* owner, Begin begin = nullptr);
    // Waits for a frame still on the worker.
    ~DeviceFrame();
    DeviceFrame(const DeviceFrame&) = delete;
    DeviceFrame& operator=(const DeviceFrame&) = delete;

    void set_worker(FrameWorker* worker);
    FrameWorker* worker() const noexcept { return worker_; }
    // The board's other core, which takes strips as the worker draws (null:
    // one lane).
    void set_helper(FrameWorker* helper);
    FrameWorker* helper() const noexcept { return helper_; }

    // Shows the frame at the bottom of `parent`, its top-left at (x, y). A
    // return to the same place shows the last frame; a first visit, or a new
    // place, needs a whole frame first (needs_whole()): the owner sets its
    // frame up, then calls draw_whole().
    void attach(lv_obj_t* parent, int x, int y);
    // A frame on the worker sends nothing more to the panel: the screen is changing.
    void detach() noexcept;
    // Strips begin at row `row` of the parent and every eight rows from it
    // (the first and last cut by the frame): the owner's own strips of light
    // line up with them. From an even row, the panel's; once attached, before
    // a frame. LVGL draws the next frame.
    void align_strips(int row) noexcept;
    // Everything under the frame changed (another screen's light fields):
    // both buffers are redrawn whole.
    void invalidate_all() noexcept;
    bool needs_whole() const noexcept { return !pending_[front_].empty(); }
    lv_obj_t* object() const noexcept { return image_; }
    FrameRect area() const noexcept { return {x_, y_, x_ + width_, y_ + height_}; }

    enum class Step : std::uint8_t { Wait, Shown, Start, ShownStart };
    // One tick: Shown when it showed a frame the worker drew; Start when the
    // next frame is due (the owner sets it up, then calls start()); ShownStart
    // when both: a frame that took the worker its interval or longer starts
    // the next at once, drawn while LVGL shows this one. On the board, waiting
    // for a later tick, each frame waited for LVGL's (whose drawing holds the
    // display lock the tick needs): the worker and LVGL took turns (2026-11-16).
    Step advance(std::uint32_t elapsed_ms) noexcept;
    // The time the frame starting now moves the animation by.
    std::uint32_t step_ms() const noexcept { return step_ms_; }
    // Draws a frame whose changes since the one shown are `dirty`: on the
    // worker (false: a later tick shows it) or here (true: shown).
    bool start(const FrameRect& dirty);
    // Draws the whole frame here and shows it.
    void draw_whole();
    // Waits for a frame on the worker, which a later tick shows.
    void finish() noexcept;
    // Waits for a frame on the worker and drops it, without LVGL: the owner is
    // going away (a look replaced by another is never detached), and the
    // drawing reads the owner's memory.
    void stop() noexcept;
    // How long until the frame wants its tick (0: now); no_tick on the
    // caller's thread, or while the worker draws (its finish wakes the board).
    std::uint32_t next_tick_ms() const noexcept;

    bool drawing() const noexcept { return drawing_; }
    std::uint64_t frame_count() const noexcept { return frames_; }
    std::uint32_t last_frame_ms() const noexcept { return last_frame_ms_; }
    std::size_t buffer_bytes() const noexcept {
        return 2U * static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * sizeof(std::uint16_t);
    }
    const std::uint16_t* shown() const noexcept { return buffers_[front_].get(); }
    // The parts of each buffer still to be redrawn (for the tests).
    FrameRect pending(int buffer) const noexcept { return pending_[buffer & 1]; }
    FrameRect last_redraw() const noexcept { return redraw_; }
    // Frames with strips that went to the panel straight, and the strips the
    // last one sent there (bit i: the frame's strip i from its top).
    std::uint64_t direct_frames() const noexcept { return direct_frames_; }
    std::uint64_t last_sent() const noexcept { return last_sent_; }

private:
    static void run(void* frame) noexcept;
    static void run_helper(void* frame) noexcept;
    // Draws strips until none is left, with lane `lane`'s memory; the
    // helper's lane sends those planned straight as it goes.
    void draw_strips(int lane) noexcept;
    void present() noexcept;
    // Which of the frame's changed strips may go to the panel straight (under
    // the display lock, before the frame goes to the worker).
    void plan_direct() noexcept;
    // On the worker: the strips drawn and planned straight, sent under LVGL's
    // lock if it is taken within `timeout_ms`; false when it was not. Those
    // LVGL is still to draw wait for a later try, or, at the frame's end
    // (`last`), go to LVGL.
    bool send_strips(std::uint32_t timeout_ms, bool last) noexcept;
    // The panel window (frame pixels, even edges) of strip `strip`'s change.
    FrameRect strip_window(int strip) const noexcept;
    // The strip of the frame's row `row`, and a strip's first and last rows
    // (frame rows [top, bottom)).
    int strip_of(int row) const noexcept { return (row + shift_) / strip_rows; }
    int strip_top(int strip) const noexcept { return strip == 0 ? 0 : strip * strip_rows - shift_; }
    int strip_bottom(int strip) const noexcept {
        const int bottom = (strip + 1) * strip_rows - shift_;
        return bottom < height_ ? bottom : height_;
    }
    // The strips (bits) a screen area lies over; all of them before a frame
    // has found its place on the screen.
    std::uint64_t strips_under(const lv_area_t& area) const noexcept;
    std::uint64_t all_strips() const noexcept;
    void listen(lv_display_t* display) noexcept;
    void stop_listening() noexcept;
    static void display_event(lv_event_t* event);

    int width_;
    int height_;
    Draw draw_;
    void* owner_;
    Begin begin_;
    std::unique_ptr<std::uint16_t[]> buffers_[2];
    lv_image_dsc_t descriptor_{};
    lv_obj_t* image_{};
    int x_{};
    int y_{};
    // The rows the first strip lacks of eight (even: 0-6).
    int shift_{};
    bool placed_{};
    int front_{};
    FrameRect pending_[2]{};
    FrameRect dirty_{};
    FrameRect redraw_{};
    FrameWorker* worker_{};
    bool drawing_{};
    std::uint32_t since_ms_{};
    std::uint32_t step_ms_{};
    std::uint32_t drawn_ms_{};
    std::uint32_t last_frame_ms_{};
    std::uint64_t frames_{};

    // The panel straight: the platform's output for the frame on the worker,
    // its strips planned straight, drawn and waiting for the lock, and sent.
    SynapseOutput* output_{};
    std::uint64_t direct_{};
    std::atomic<std::uint64_t> ready_{};
    std::atomic<std::uint64_t> sent_{};
    // The helper, and the next strip either lane takes.
    FrameWorker* helper_{};
    std::atomic<int> next_strip_{};
    // What LVGL draws over the frame (screen areas), found as it is planned.
    std::vector<lv_area_t> drawn_over_;
    // LVGL's display: the frame's place on its screen (once planned), the
    // strips it was told to draw since its last refresh, and those it was
    // told to draw after they were sent (they go to LVGL again).
    lv_display_t* display_{};
    bool on_screen_{};
    int screen_x_{};
    int screen_y_{};
    bool rendering_{};
    std::atomic<std::uint64_t> lvgl_pending_{~0ULL};
    std::atomic<std::uint64_t> spoiled_{};
    std::uint64_t direct_frames_{};
    std::uint64_t last_sent_{};
};

}  // namespace aura::ui

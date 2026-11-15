#include "aura/aura_mercury_view.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

// LVGL's extra drawing area around an object (shadows, outlines): what
// Synapse's direct output must keep clear of.
#include "src/core/lv_obj_draw_private.h"

// The device frame's strips run from internal RAM on the board: from flash,
// through the 16 KiB instruction cache both cores share with LVGL, a short
// line took ~1,700 cycles a strip (measured 2026-11-15).
#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define SYNAPSE_FAST IRAM_ATTR
#else
#define SYNAPSE_FAST
#endif
// What those loops call is inlined into them: a call out would run from flash.
#define SYNAPSE_INLINE __attribute__((always_inline))
#include "src/misc/lv_area_private.h"  // lv_area_intersect(), lv_area_is_in()

namespace aura::ui {

// Every formula below follows artifacts/halo-synapse.html at its own scale (one
// presentation pixel is one screen pixel). Where the presentation multiplies its
// clock by the state's speed, this renderer uses phase_, the speed integrated
// over time: the same picture while a state lasts, without the presentation's
// sudden spin when the speed changes between states.

namespace {
constexpr float kTau = 6.2831853071795864769F;
constexpr float kCentreX = static_cast<float>(AuraMercuryView::width) / 2.0F;
constexpr float kCentreY = static_cast<float>(AuraMercuryView::height) / 2.0F;
// Light beyond the frame's top and bottom edges fades out over this many rows.
constexpr int kEdgeFade = 18;
// With a worker, the image source stays the same descriptor while its data
// alternates between two frames; an image cache would keep showing the old one.
static_assert(LV_CACHE_DEF_SIZE == 0, "Synapse's frames need LVGL's image cache off");
// The reduced frame's lattice: the soft pass from every fourth step, brighter
// to make up for the joints it no longer overlaps, and the bright pass from
// every second. Measured on the PC: 70 % of a full frame's time, and a mean
// difference of under 3 levels (of 255) in light.
constexpr int kReducedSoftStride = 4;
constexpr int kReducedBrightStride = 2;
constexpr float kReducedSoftGain = 2.2F;
// A 4 x 4 ordered dither: it keeps the faint glows smooth in RGB565.
constexpr std::uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
// One RGB565 pixel from 8-bit channels, dithered by `threshold` (0-15).
[[maybe_unused]] inline std::uint32_t pack565(int red, int green, int blue, int threshold) {
    const int red5 = std::min(31, (red + (threshold >> 1)) >> 3);
    const int green6 = std::min(63, (green + (threshold >> 2)) >> 2);
    const int blue5 = std::min(31, (blue + (threshold >> 1)) >> 3);
    return static_cast<std::uint32_t>((red5 << 11) | (green6 << 5) | blue5);
}

// Idle, listening, thinking and speaking.
constexpr std::array<float, 4> kSpeed{{0.32F, 0.60F, 1.42F, 0.85F}};
constexpr std::array<float, 4> kSpread{{0.14F, 1.00F, 0.48F, 0.78F}};
constexpr std::array<float, 4> kWeave{{0.26F, 0.42F, 1.00F, 0.65F}};
constexpr std::array<float, 4> kWave{{0.16F, 1.00F, 0.34F, 1.00F}};

float weighted(const std::array<float, 4>& weights, const std::array<float, 4>& values) {
    float result = 0.0F;
    for (std::size_t index = 0; index < weights.size(); ++index) result += weights[index] * values[index];
    return result;
}

// std::floor and std::ceil to an int, exactly, without the C library's calls:
// on the board they run from flash, through the instruction cache both cores
// share with LVGL, and the strips called them for every row.
inline int floor_int(float value) {
    const int truncated = static_cast<int>(value);
    return truncated - (static_cast<float>(truncated) > value ? 1 : 0);
}
inline int ceil_int(float value) {
    const int truncated = static_cast<int>(value);
    return truncated + (static_cast<float>(truncated) < value ? 1 : 0);
}
// A float's floor, exactly (the colours take a few thousand a frame).
inline float floor_float(float value) { return static_cast<float>(floor_int(value)); }


float fract(float value) { return value - floor_float(value); }
// The clocks are doubles; every angle and fraction taken from them is reduced
// before it meets the float sample terms.
float spin(double radians) { return static_cast<float>(std::fmod(radians, 6.283185307179586)); }
float cycle(double value) { return static_cast<float>(value - std::floor(value)); }
float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }
float mix(float from, float to, float share) { return from + (to - from) * share; }

// The presentation's rgbAt(): teal to violet to pink, with its opacity.
struct Spectral {
    float red;
    float green;
    float blue;
};

Spectral spectral_colour(float position) {
    constexpr Spectral stops[3] = {{107.0F, 247.0F, 221.0F}, {147.0F, 124.0F, 255.0F}, {255.0F, 91.0F, 198.0F}};
    const float scaled = fract(position) * 2.0F;
    const int index = std::min(1, static_cast<int>(scaled));
    const float share = scaled - static_cast<float>(index);
    // The browser truncates each channel to an integer, as `r | 0` does.
    return {floor_float(mix(stops[index].red, stops[index + 1].red, share)),
            floor_float(mix(stops[index].green, stops[index + 1].green, share)),
            floor_float(mix(stops[index].blue, stops[index + 1].blue, share))};
}

// The columns of a row a stroke can touch: inside the box around the segment
// and within `reach` of its line, so a long diagonal line visits a narrow band
// of each row instead of its whole bounding box. The band's slope and
// half-width are worked out once per segment, not once per row.
class LineBand {
public:
    LineBand(float from_x, float from_y, float delta_x, float delta_y, float reach)
        : narrow_(std::abs(delta_y) >= 1e-3F), from_x_(from_x), from_y_(from_y) {
        if (!narrow_) return;
        slope_ = delta_x / delta_y;
        half_span_ = reach * std::sqrt(1.0F + slope_ * slope_);
    }
    void clip(int row, int& left, int& right) const {
        if (!narrow_) return;
        const float centre = from_x_ + (static_cast<float>(row) + 0.5F - from_y_) * slope_;
        left = std::max(left, floor_int(centre - half_span_));
        right = std::min(right, ceil_int(centre + half_span_));
    }

private:
    bool narrow_;
    float from_x_;
    float from_y_;
    float slope_{};
    float half_span_{};
};

// A turn by an angle, for the device frame's arcs: up to a radian, its cosine
// and sine from their series to the 8th and 9th power (within 3e-7 of them);
// std::cos and std::sin beyond, which run from flash on the board.
struct Turn {
    float cosine;
    float sine;
    static Turn by(float angle) {
        if (std::abs(angle) > 1.0F) return {std::cos(angle), std::sin(angle)};
        const float squared = angle * angle;
        return {1.0F - squared * 0.5F *
                           (1.0F - squared * (1.0F / 12.0F) *
                                       (1.0F - squared * (1.0F / 30.0F) * (1.0F - squared * (1.0F / 56.0F)))),
                angle * (1.0F - squared * (1.0F / 6.0F) *
                                    (1.0F - squared * (1.0F / 20.0F) *
                                                (1.0F - squared * (1.0F / 42.0F) * (1.0F - squared * (1.0F / 72.0F)))))};
    }
    // Turns the direction (cosine, sine) by it.
    void apply(float& cosine_of, float& sine_of) const {
        const float turned = cosine_of * cosine - sine_of * sine;
        sine_of = sine_of * cosine + cosine_of * sine;
        cosine_of = turned;
    }
};

// The board draws the device frame; everywhere else draws the presentation.
#if defined(ESP_PLATFORM)
constexpr SynapseProfile kPlatformProfile = SynapseProfile::Device;
#else
constexpr SynapseProfile kPlatformProfile = SynapseProfile::Presentation;
#endif

// sin and cos of k times each path sample's angle, for the harmonics the paths
// use: the same for every frame, so the paths need no sine per sample.
struct Harmonics {
    float sin1, cos1, sin2, cos2, sin3, cos3, sin5, cos5, sin8, cos8;
};
template <int Steps>
const std::array<Harmonics, Steps + 1>& path_harmonics() {
    static const auto table = [] {
        std::array<Harmonics, Steps + 1> values{};
        for (int sample = 0; sample <= Steps; ++sample) {
            const double angle = static_cast<double>(sample) / Steps * 6.283185307179586;
            const auto at = [angle](double k, bool sine) {
                return static_cast<float>(sine ? std::sin(k * angle) : std::cos(k * angle));
            };
            values[static_cast<std::size_t>(sample)] = {at(1, true), at(1, false), at(2, true), at(2, false),
                                                       at(3, true), at(3, false), at(5, true), at(5, false),
                                                       at(8, true), at(8, false)};
        }
        return values;
    }();
    return table;
}

// sin(k·angle + phase), from the table and the phase's own sine and cosine.
struct Phase {
    explicit Phase(float radians) : sine(std::sin(radians)), cosine(std::cos(radians)) {}
    float of(float sin_k, float cos_k) const { return sin_k * cosine + cos_k * sine; }
    float sine;
    float cosine;
};

// What lies over Synapse's direct rows. A plain object or a button draws only
// its background, border, outline and shadow; any other widget (a label, an
// image, an arc) is taken to draw wherever it lies.
bool draws_itself(const lv_obj_t* object) {
    const lv_obj_class_t* type = lv_obj_get_class(object);
    if (type != &lv_obj_class && type != &lv_button_class) return true;
    const auto seen = [](lv_opa_t opa) { return opa > LV_OPA_MIN; };
    return seen(lv_obj_get_style_bg_opa(object, LV_PART_MAIN)) ||
           lv_obj_get_style_bg_image_src(object, LV_PART_MAIN) != nullptr ||
           (lv_obj_get_style_border_width(object, LV_PART_MAIN) > 0 &&
            seen(lv_obj_get_style_border_opa(object, LV_PART_MAIN))) ||
           (lv_obj_get_style_outline_width(object, LV_PART_MAIN) > 0 &&
            seen(lv_obj_get_style_outline_opa(object, LV_PART_MAIN))) ||
           (lv_obj_get_style_shadow_width(object, LV_PART_MAIN) > 0 &&
            seen(lv_obj_get_style_shadow_opa(object, LV_PART_MAIN)));
}

// Whether `object`, or anything inside it, may draw in `region` (screen coordinates).
bool draws_over(lv_obj_t* object, const lv_area_t& region) {
    if (lv_obj_is_hidden(object) || lv_obj_get_style_opa(object, LV_PART_MAIN) <= LV_OPA_MIN) {
        return false;
    }
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    // A label's extra area is LVGL's margin for letters that lean out of its
    // box sideways (italics, lv_label.c); its lines stay between its top and
    // bottom. Home's date line ends two rows above the direct ones.
    const int32_t extra = lv_obj_get_ext_draw_size(object);
    lv_area_increase(&area, extra, lv_obj_check_type(object, &lv_label_class) ? 0 : extra);
    lv_area_t common{};
    const bool overlaps = lv_area_intersect(&common, &area, &region);
    if (overlaps && draws_itself(object)) return true;
    // Children are clipped to their parent unless it lets them overflow.
    if (!overlaps && !lv_obj_is_overflow_visible(object)) return false;
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0; index < children; ++index) {
        if (draws_over(lv_obj_get_child(object, static_cast<std::int32_t>(index)), region)) return true;
    }
    return false;
}

}  // namespace

AuraMercuryView::AuraMercuryView() : AuraMercuryView(nullptr, FrameWorker::shared(), FrameWorker::helper()) {
    shared_worker_ = true;
    set_profile(kPlatformProfile);
}

AuraMercuryView::AuraMercuryView(Clock clock, FrameWorker* worker, FrameWorker* helper)
    : clock_(clock), worker_(worker), helper_(worker ? helper : nullptr) {
    if (worker_) second_ = std::make_unique<Frame>();
    canvas_ = buffer_.data();
    shown_ = buffer_.data();
    light_ = canvas_;
    descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    descriptor_.header.w = width;
    descriptor_.header.h = height;
    descriptor_.data = shown_;
    apply_format();
    // The vignette: a black radial veil from radius 120 (clear) to 330 (82 %).
    for (std::size_t index = 0; index < vignette_.size(); ++index) {
        const float distance = std::sqrt(static_cast<float>(index << kVignetteShift));
        const float veil = 0.82F * clamp01((distance - 120.0F) / 210.0F);
        vignette_[index] = static_cast<std::uint16_t>((1.0F - veil) * 256.0F + 0.5F);
    }
    // The frame's own fade at its top and bottom edges, as finish() works it out.
    for (int y = 0; y < height; ++y) {
        const int edge = std::min(y, height - 1 - y);
        float fade = edge >= kEdgeFade ? 1.0F : static_cast<float>(edge) / kEdgeFade;
        fade = fade * fade * (3.0F - 2.0F * fade);
        row_fade_[static_cast<std::size_t>(y)] = static_cast<std::uint16_t>(fade * 256.0F + 0.5F);
    }
}

AuraMercuryView::~AuraMercuryView() {
    if (drawing_) worker_->wait_idle();
    // A view replaced by another look's is never detached (apply_appearance).
    stop_listening();
}

void AuraMercuryView::apply_format() noexcept {
#if LV_COLOR_DEPTH == 16
    constexpr bool display_565 = true;
#else
    constexpr bool display_565 = false;
#endif
    // A 16-bit display shows RGB565 anyway: the light is packed, dithered. The
    // device frame is RGB565 on every display, so the simulator's checks see
    // the board's frame.
    if (display_565 || profile_ == SynapseProfile::Device) {
        descriptor_.header.cf = LV_COLOR_FORMAT_RGB565;
        descriptor_.header.stride = width * 2;
        descriptor_.data_size = width * height * 2;
    } else {
        descriptor_.header.cf = LV_COLOR_FORMAT_RGB888;
        descriptor_.header.stride = width * 3;
        descriptor_.data_size = width * height * 3;
    }
}

void AuraMercuryView::set_profile(SynapseProfile profile) {
    // Never change the light under a frame on the worker; the next tick shows it.
    if (drawing_) worker_->wait_idle();
    profile_ = profile;
    apply_format();
    tint_row_ = -1;
    if (profile == SynapseProfile::Device) {
        // A frame has some 400 soft shapes and 2,400 lines; the lists keep
        // their room between frames.
        shapes_.reserve(1000);
        spans_.reserve(4000);
        for (auto& list : strip_shapes_) list.reserve(128);
        for (auto& list : strip_spans_) list.reserve(256);
        if (!reactor_) {
            constexpr std::size_t outer = static_cast<std::size_t>(kOuterSpan) * (kOuterSpan + 1);
            constexpr std::size_t inner = static_cast<std::size_t>(kInnerSpan) * (kInnerSpan + 1);
            reactor_ = std::make_unique<std::uint8_t[]>(outer + inner);
            const auto fill = [](std::uint8_t* table, int span, float radius) {
                // The falloff row by row, then each row's reach.
                const float limit = radius * radius;
                std::uint8_t* reaches = table + static_cast<std::size_t>(span) * span;
                for (int row = 0; row < span; ++row) {
                    const float offset_y = static_cast<float>(row) + 0.5F;
                    int reach = 0;
                    for (int column = 0; column < span; ++column) {
                        const float offset_x = static_cast<float>(column) + 0.5F;
                        const float distance_squared = offset_x * offset_x + offset_y * offset_y;
                        int value = 0;
                        if (distance_squared < limit) {
                            value = std::min(255, static_cast<int>((1.0F - std::sqrt(distance_squared) / radius) *
                                                                       256.0F + 0.5F));
                            reach = column + 1;
                        }
                        table[static_cast<std::size_t>(row * span + column)] = static_cast<std::uint8_t>(value);
                    }
                    reaches[row] = static_cast<std::uint8_t>(reach);
                }
            };
            fill(reactor_.get(), kOuterSpan, 112.0F);
            fill(reactor_.get() + outer, kInnerSpan, 58.0F);
        }
        if (!background_) background_ = std::make_unique<std::uint8_t[]>(buffer_.size());
    } else {
        shapes_ = {};
        spans_ = {};
        for (auto& list : strip_shapes_) list = {};
        for (auto& list : strip_spans_) list = {};
        strip_memory_.reset();
        reactor_.reset();
        background_.reset();
    }
    // An image showing this view's frame takes the new format at its next draw.
    if (image_) lv_image_set_src(image_, &descriptor_);
}

namespace {
std::atomic<SynapseOutput*> g_synapse_output{nullptr};
}  // namespace

void set_synapse_output(SynapseOutput* output) noexcept { g_synapse_output.store(output); }
SynapseOutput* synapse_output() noexcept { return g_synapse_output.load(); }

lv_obj_t* AuraMercuryView::attach(lv_obj_t* parent, int center_x, int center_y) {
    // The platform may start its worker after the UI, and this view, are built.
    if (shared_worker_ && !worker_) {
        worker_ = FrameWorker::shared();
        if (worker_ && !second_) second_ = std::make_unique<Frame>();
    }
    if (shared_worker_ && worker_ && !helper_) helper_ = FrameWorker::helper();
    // A return with a worker shows the last frame at once and lets the worker
    // draw the next: on the board a frame takes ~330 ms, and drawing it here, or
    // waiting here for one still on the worker, held the display lock and froze
    // touch on returns (the first flash, 2026-11-14). A first visit, or one
    // without a worker, draws at once, so the presence is never blank; the first
    // tick draws the next frame.
    const bool returning = attached_before_;
    const bool hand_over = returning && worker_ && second_ && !reduced_motion_;
    // A frame still on the worker belongs to the previous attachment: a return
    // shows it on a later tick; anything else waits for it.
    if (drawing_ && !hand_over) {
        worker_->wait_idle();
        drawing_ = false;
    }
    image_ = lv_image_create(parent);
    // The device frame's middle rows may go to the panel straight: the view
    // follows what LVGL invalidates over them.
    if (profile_ == SynapseProfile::Device) listen(lv_obj_get_display(image_));
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, center_x - width / 2, center_y - height / 2);
    lv_obj_set_clickable(image_, false);
    lv_obj_set_scrollable(image_, false);
    // The frame is opaque light on the black canvas: it goes behind every label
    // and control, including the Home clock and date made before it.
    lv_obj_move_to_index(image_, 0);
    if (reduced_motion_ || !attached_before_) {
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            weights_[index] = index == static_cast<std::size_t>(state_) ? 1.0F : 0.0F;
        }
    }
    attached_before_ = true;
    wait_ms_ = frame_interval_ms;
    idle_ms_ = frame_interval_ms;
    debt_ms_ = 0;
    animation_ms_ = 0;
    frame_count_ = 0;
    // The worker still reads the motion's state: leave it until that frame shows.
    if (hand_over && drawing_) return image_;
    advance(0.0F);
    // The first frame goes to LVGL whole: the new image is not on the screen yet.
    frame_direct_ = false;
    if (hand_over) {
        frame_quality_ = quality_;
        canvas_ = shown_ == buffer_.data() ? second_->data() : buffer_.data();
        drawing_ = worker_->submit(&AuraMercuryView::draw_on_worker, this);
        if (drawing_) return image_;
    }
    canvas_ = shown_;
    frame_quality_ = quality_;
    render();
    ++frame_count_;
    return image_;
}

void AuraMercuryView::detach() noexcept {
    // A frame on the worker sends nothing more: the screen is changing.
    frame_obstructed_ = true;
    stop_listening();
    image_ = nullptr;
}

void AuraMercuryView::listen(lv_display_t* display) noexcept {
    if (display == display_) return;
    stop_listening();
    if (!display) return;
    display_ = display;
    direct_area_ = {0, 0, -1, -1};
    pending_overlap_ = true;
    lv_display_add_event_cb(display, &AuraMercuryView::display_event, LV_EVENT_ALL, this);
}

void AuraMercuryView::stop_listening() noexcept {
    if (display_ && lv_is_initialized()) {
        lv_display_remove_event_cb_with_user_data(display_, &AuraMercuryView::display_event, this);
    }
    display_ = nullptr;
}

void AuraMercuryView::display_event(lv_event_t* event) noexcept {
    auto* self = static_cast<AuraMercuryView*>(lv_event_get_user_data(event));
    switch (lv_event_get_code(event)) {
        case LV_EVENT_RENDER_START: self->rendering_ = true; break;
        case LV_EVENT_RENDER_READY: self->rendering_ = false; break;
        // Everything invalidated before has been drawn.
        case LV_EVENT_REFR_READY: self->pending_overlap_ = false; break;
        case LV_EVENT_INVALIDATE_AREA: {
            // While LVGL draws, this event only tries its rounder on a draw
            // buffer's rows (lv_refr.c, get_max_row()); nothing is invalidated.
            if (self->rendering_) break;
            const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
            lv_area_t common{};
            if (area && lv_area_intersect(&common, area, &self->direct_area_)) {
                self->pending_overlap_ = true;
                self->frame_obstructed_ = true;
            }
            break;
        }
        default: break;
    }
}

void AuraMercuryView::set_state(MercuryState state) noexcept { state_ = state; }
void AuraMercuryView::set_level(float level) noexcept { level_ = std::clamp(level, 0.0F, 1.0F); }
void AuraMercuryView::set_reduced_motion(bool reduced) noexcept { reduced_motion_ = reduced; }

std::uint32_t AuraMercuryView::paced_wait(std::uint32_t render_ms) noexcept {
    // An extreme stall counts as one second, so Synapse still moves every 3 s.
    const std::uint32_t drawing = std::min<std::uint32_t>(render_ms, 1000U);
    const std::uint32_t rest = drawing < frame_interval_ms ? frame_interval_ms - drawing : 0U;
    return std::max(rest, drawing * 2U);
}

std::uint32_t AuraMercuryView::worker_wait(std::uint32_t render_ms) noexcept {
    const std::uint32_t drawing = std::min<std::uint32_t>(render_ms, 1000U);
    const std::uint32_t rest = drawing < frame_interval_ms ? frame_interval_ms - drawing : 0U;
    return std::max(rest, drawing / 4U);
}

SynapseQuality AuraMercuryView::next_quality(SynapseQuality current, std::uint32_t render_ms) noexcept {
    if (current == SynapseQuality::Full && render_ms > reduce_above_ms) return SynapseQuality::Reduced;
    if (current == SynapseQuality::Reduced && render_ms < restore_below_ms) return SynapseQuality::Full;
    return current;
}

std::uint32_t AuraMercuryView::now_ms() const noexcept {
    if (clock_) return clock_();
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool AuraMercuryView::tick(std::uint32_t elapsed_ms) {
    if (!image_ || reduced_motion_) return false;
    // Time spent drawing on this thread comes back inside its next elapsed
    // times; it is not idle time.
    const std::uint32_t repaid = std::min(elapsed_ms, debt_ms_);
    debt_ms_ -= repaid;
    // The animation moves with real time, however few frames a device draws.
    animation_ms_ = std::min<std::uint32_t>(animation_ms_ + elapsed_ms, 1000U);
    if (drawing_) {
        if (worker_->busy()) return false;
        drawing_ = false;
        show(canvas_, drawn_ms_, true);
        return true;
    }
    idle_ms_ = std::min<std::uint32_t>(idle_ms_ + (elapsed_ms - repaid), 2U * wait_ms_);
    if (idle_ms_ < wait_ms_) return false;
    // Another view's frame is still on the shared worker: try again next tick.
    if (worker_ && worker_->busy()) return false;
    // The host ticks about every 33 ms: idle time beyond the wait carries over,
    // so a fast device keeps 20 FPS on average (show() trims it on a slow one).
    idle_ms_ -= wait_ms_;
    advance(static_cast<float>(animation_ms_) / 1000.0F);
    animation_ms_ = 0;
    frame_quality_ = quality_;
    if (worker_) {
        prepare_direct();
        canvas_ = shown_ == buffer_.data() ? second_->data() : buffer_.data();
        drawing_ = worker_->submit(&AuraMercuryView::draw_on_worker, this);
        if (drawing_) return false;
    }
    // Drawn here, under the lock the ticking thread holds: never straight.
    frame_direct_ = false;
    const std::uint32_t render_ms = draw_into(shown_);
    debt_ms_ = render_ms;
    show(shown_, render_ms, false);
    return true;
}

std::uint32_t AuraMercuryView::next_tick_ms() const noexcept {
    // Drawn on the UI thread, Synapse keeps to the UI's pace.
    if (!worker_ || !image_ || reduced_motion_) return no_tick;
    // A frame on the worker shows on the first tick after it is drawn (the
    // worker's finish wakes the board's loop for it).
    if (drawing_) return worker_->busy() ? no_tick : 0U;
    // The next begins once its rest is over, the time drawn here repaid...
    if (idle_ms_ < wait_ms_) return wait_ms_ - idle_ms_ + debt_ms_;
    // ...unless another view's frame holds the shared worker.
    return worker_->busy() ? no_tick : 0U;
}

std::uint32_t AuraMercuryView::draw_into(std::uint8_t* target) noexcept {
    canvas_ = target;
    const std::uint32_t started = now_ms();
    render();
    return now_ms() - started;
}

void AuraMercuryView::draw_on_worker(void* view) noexcept {
    auto* self = static_cast<AuraMercuryView*>(view);
    self->drawn_ms_ = self->draw_into(self->canvas_);
}

void AuraMercuryView::show(std::uint8_t* frame, std::uint32_t render_ms, bool from_worker) noexcept {
    shown_ = frame;
    descriptor_.data = frame;
    const bool direct = direct_sent_ && !frame_obstructed_;
    if (image_) {
        if (direct) {
            // The panel has the middle rows already: LVGL draws the band's top
            // and bottom, under the labels Home lays there.
            lv_area_t area{};
            lv_obj_get_coords(image_, &area);
            const lv_area_t top{area.x1, area.y1, area.x2, area.y1 + direct_top - 1};
            const lv_area_t bottom{area.x1, area.y1 + direct_bottom, area.x2, area.y2};
            lv_obj_invalidate_area(image_, &top);
            lv_obj_invalidate_area(image_, &bottom);
        } else {
            lv_obj_invalidate(image_);
        }
    }
    if (direct) ++direct_frames_;
    direct_sent_ = false;
    ++frame_count_;
    last_frame_ms_ = render_ms;
    // A device that needs longer per frame draws fewer, lighter ones, so drawing
    // keeps to a third of the time: idle time carried over never cuts into the
    // twice-the-drawing a frame asks for, and a worker's frame starts it afresh.
    wait_ms_ = from_worker ? worker_wait(render_ms) : paced_wait(render_ms);
    const std::uint32_t drawing = std::min<std::uint32_t>(render_ms, 1000U);
    const std::uint32_t spare = wait_ms_ > 2U * drawing ? wait_ms_ - 2U * drawing : 0U;
    idle_ms_ = from_worker ? 0U : std::min(idle_ms_, spare);
    quality_ = next_quality(quality_, render_ms);
}

void AuraMercuryView::advance(float seconds) noexcept {
    seconds_ += static_cast<double>(seconds);
    const float ease = seconds > 0.0F ? 1.0F - std::exp(-seconds / 0.2F) : 0.0F;
    float sum = 0.0F;
    for (std::size_t index = 0; index < weights_.size(); ++index) {
        const float target = index == static_cast<std::size_t>(state_) ? 1.0F : 0.0F;
        weights_[index] += (target - weights_[index]) * ease;
        sum += weights_[index];
    }
    if (sum > 0.0F) {
        for (auto& weight : weights_) weight /= sum;
    }
    speed_ = weighted(weights_, kSpeed);
    spread_ = weighted(weights_, kSpread);
    weave_ = weighted(weights_, kWeave);
    wave_ = weighted(weights_, kWave);
    drive_ = (weights_[1] + weights_[3]) * level_;
    phase_ += static_cast<double>(seconds) * speed_;
}

// ---- Light primitives: the canvas operations the presentation uses ----------
// They take presentation pixels and add light to light_ in rows [clip_top_,
// clip_bottom_): the whole frame for the presentation, one strip of the device
// frame. While the device frame records its shapes, they keep them instead.
// Their loops keep what they need in locals: a pixel's write could change the
// view's own fields as far as the compiler knows, and on the board the view
// lies in PSRAM.

inline void AuraMercuryView::add_to(std::uint8_t* pixel, const Light& light, float coverage) noexcept {
    const auto put = [coverage](std::uint8_t& channel, float value) {
        const int sum = static_cast<int>(channel) + static_cast<int>(value * coverage + 0.5F);
        channel = static_cast<std::uint8_t>(std::min(255, sum));
    };
    put(pixel[0], light.blue);
    put(pixel[1], light.green);
    put(pixel[2], light.red);
}

void AuraMercuryView::add(int x, int y, const Light& light, float coverage) noexcept {
    add_to(light_ + (static_cast<std::size_t>(y - light_row_) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)) * 3,
           light, coverage);
}

void AuraMercuryView::record(ShapeKind kind, float x0, float y0, float x1, float y1, float size, const Light& light,
                             float reach) {
    // The rows the shape may light, with a row to spare for rounding.
    const float top = std::min(y0, y1) - reach - 1.0F;
    const float bottom = std::max(y0, y1) + reach + 1.0F;
    if (!(bottom >= 0.0F) || !(top < static_cast<float>(height)) || shapes_.size() >= 65535U) return;
    // Dimmed by the vignette and the edge fade where it lies: the strips
    // add it to a background that has them already.
    const float dim = static_cast<float>(attenuation((x0 + x1) * 0.5F, (y0 + y1) * 0.5F)) / 256.0F;
    shapes_.push_back({kind, static_cast<std::int16_t>(std::max(0.0F, top)),
                       static_cast<std::int16_t>(std::min(static_cast<float>(height - 1), bottom)), x0, y0, x1, y1,
                       size, {light.red * dim, light.green * dim, light.blue * dim}});
}

// ---- The device frame's sharp lines and discs --------------------------------
// One pixel across, with fixed-point steps, so a line costs a few operations a
// pixel (the float strokes cost ~13 µs a step on the board, even at half
// resolution). The vignette and the edge fade come with the strip's finish.

namespace {
// Where a sharp primitive adds its light: the strip's light (its first row
// frame row `first_row`), lit in rows [top, bottom).
struct SharpCanvas {
    std::uint8_t* light;
    int first_row;
    int top;
    int bottom;
    // `red` etc. in 1/65536 of a level at full coverage, `coverage` 0-255;
    // counts the pixels inside in `work`.
    void add(int x, int y, int red, int green, int blue, int coverage, std::uint64_t& work) const {
        if (static_cast<unsigned>(x) >= static_cast<unsigned>(AuraMercuryView::width) || y < top || y >= bottom) {
            return;
        }
        ++work;
        const int add_red = (red * coverage + 32768) >> 16;
        const int add_green = (green * coverage + 32768) >> 16;
        const int add_blue = (blue * coverage + 32768) >> 16;
        if ((add_red | add_green | add_blue) == 0) return;
        auto* pixel = light + (static_cast<std::size_t>(y - first_row) * AuraMercuryView::width +
                               static_cast<std::size_t>(x)) * 3;
        pixel[0] = static_cast<std::uint8_t>(std::min(255, pixel[0] + add_blue));
        pixel[1] = static_cast<std::uint8_t>(std::min(255, pixel[1] + add_green));
        pixel[2] = static_cast<std::uint8_t>(std::min(255, pixel[2] + add_red));
    }
};
}  // namespace

bool AuraMercuryView::line_span(const Point& from, const Point& to, float line_width, const Light& light, bool last,
                                Span& span) noexcept {
    float x0 = from.x;
    float y0 = from.y;
    float x1 = to.x;
    float y1 = to.y;
    const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) {
        std::swap(x0, y0);
        std::swap(x1, y1);
    }
    // A polyline's joint belongs to the next segment: this one leaves out the
    // column holding its end, unless it is the last.
    bool skip_first = false;
    bool skip_final = !last;
    if (x0 > x1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
        std::swap(skip_first, skip_final);
    }
    const float run = x1 - x0;
    const float gradient = run > 1e-6F ? (y1 - y0) * estimated_reciprocal(run) : 0.0F;
    // The browser's stroke lights a pixel-wide line by its width, a hairline
    // proportionally less; one column of a slanted line stands for more length.
    const float strength = line_width * 256.0F * estimated_root(1.0F + gradient * gradient);
    const auto red = static_cast<int>(light.red * strength);
    const auto green = static_cast<int>(light.green * strength);
    const auto blue = static_cast<int>(light.blue * strength);
    if (red <= 0 && green <= 0 && blue <= 0) return false;
    // Columns from the one holding the start to the one holding the end.
    int first = floor_int(x0);
    int final_column = floor_int(x1);
    if (skip_first) ++first;
    if (skip_final) --final_column;
    if (first > final_column) return false;
    // The rows each column's centre lies between, in 1/256 of a pixel.
    const float start = y0 + gradient * (static_cast<float>(first) + 0.5F - x0) - 0.5F;
    const std::int32_t position = floor_int(start * 256.0F + 0.5F);
    const std::int32_t step = floor_int(gradient * 256.0F + 0.5F);
    // The rows it lights: a steep line's columns are rows; a shallow one
    // lights the row each column lies in and the next.
    int top = first;
    int bottom = final_column;
    if (!steep) {
        const std::int32_t at_end = position + step * (final_column - first);
        top = std::min(position, at_end) >> 8;
        bottom = (std::max(position, at_end) >> 8) + 1;
    }
    if (bottom < 0 || top >= height) return false;
    span = {position,
            step,
            red,
            green,
            blue,
            step != 0 ? 256.0F * estimated_reciprocal(static_cast<float>(step)) : 0.0F,
            static_cast<std::int16_t>(first),
            static_cast<std::int16_t>(final_column),
            static_cast<std::int16_t>(std::max(0, top)),
            static_cast<std::int16_t>(std::min(height - 1, bottom)),
            steep};
    return true;
}

std::uint64_t AuraMercuryView::span_pixels(const Span& span) noexcept {
    // Two pixels a step, the steps inside the frame.
    const int first = std::max<int>(span.first, 0);
    const int last = std::min<int>(span.last, span.steep ? height - 1 : width - 1);
    return last >= first ? 2U * static_cast<std::uint64_t>(last - first + 1) : 0U;
}

void SYNAPSE_FAST AuraMercuryView::draw_span(const Span& span, Strip& strip) noexcept {
    // Only the columns that reach the strip's rows, stepped to exactly as the
    // whole line steps, so a line drawn strip by strip lights the same pixels:
    // a column to spare each side for the float's rounding, the pixel's own
    // check exact. A short line steps through all of its few columns.
    int lowest = span.first;
    int highest = span.last;
    std::int32_t position = span.position;
    if (span.steep) {
        lowest = std::max(lowest, strip.top);
        highest = std::min(highest, strip.bottom - 1);
    } else {
        lowest = std::max(lowest, 0);
        highest = std::min(highest, width - 1);
        if (span.step != 0 && span.last - span.first > 8) {
            const float at_first = static_cast<float>(position) * (1.0F / 256.0F);
            const float reach_top = (static_cast<float>(strip.top - 1) - at_first) * span.columns_per_row;
            const float reach_bottom = (static_cast<float>(strip.bottom) - at_first) * span.columns_per_row;
            lowest = std::max(lowest, span.first + floor_int(std::min(reach_top, reach_bottom)) - 1);
            highest = std::min(highest, span.first + ceil_int(std::max(reach_top, reach_bottom)) + 1);
        } else if (span.step == 0) {
            const int row = position >> 8;
            if (row + 1 < strip.top || row >= strip.bottom) return;
        }
    }
    if (lowest > highest) return;
    position += span.step * (lowest - span.first);
    // Everything the steps need in registers: the line's light and step, the
    // strip's first row and its rows as one unsigned range, the position from
    // that row, and no count (span_pixels() counts a line once, as it is
    // recorded). Read through the span and the strip, the compiler reloaded
    // each after every byte stored: ~165 cycles a column on the board
    // (measured 2026-11-15).
    const int red = span.red;
    const int green = span.green;
    const int blue = span.blue;
    const std::int32_t step = span.step;
    std::uint8_t* const strip_pixels = strip.light + (strip.top - strip.first_row) * width * 3;
    const auto rows = static_cast<unsigned>(strip.bottom - strip.top);
    // One pixel's light, at `coverage` 0-255 (none added stores it unchanged).
    const auto light_up = [red, green, blue](std::uint8_t* pixel, int coverage) SYNAPSE_INLINE {
        pixel[0] = static_cast<std::uint8_t>(std::min(255, pixel[0] + ((blue * coverage + 32768) >> 16)));
        pixel[1] = static_cast<std::uint8_t>(std::min(255, pixel[1] + ((green * coverage + 32768) >> 16)));
        pixel[2] = static_cast<std::uint8_t>(std::min(255, pixel[2] + ((red * coverage + 32768) >> 16)));
    };
    if (span.steep) {
        // A row a step, each inside the strip: the columns either side of the
        // line, inside the frame for all of them or tested one by one.
        const int count = highest - lowest + 1;
        const std::int32_t final_position = position + step * (count - 1);
        const bool inside = std::min(position, final_position) >= 0 &&
                            (std::max(position, final_position) >> 8) + 1 < width;
        std::uint8_t* row_pixels = strip_pixels + (lowest - strip.top) * width * 3;
        if (inside) {
            for (int row = 0; row < count; ++row, position += step, row_pixels += width * 3) {
                std::uint8_t* const pixel = row_pixels + (position >> 8) * 3;
                const int lower = position & 255;
                light_up(pixel, 255 - lower);
                light_up(pixel + 3, lower);
            }
        } else {
            for (int row = 0; row < count; ++row, position += step, row_pixels += width * 3) {
                const int column = position >> 8;
                const int lower = position & 255;
                if (static_cast<unsigned>(column) < static_cast<unsigned>(width)) {
                    light_up(row_pixels + column * 3, 255 - lower);
                }
                if (static_cast<unsigned>(column + 1) < static_cast<unsigned>(width)) {
                    light_up(row_pixels + (column + 1) * 3, lower);
                }
            }
        }
    } else {
        // A column a step, inside the frame: the rows either side of the line
        // that lie in the strip.
        position -= strip.top * 256;
        std::uint8_t* column_pixels = strip_pixels + lowest * 3;
        for (int column = lowest; column <= highest; ++column, position += step, column_pixels += 3) {
            const int row = position >> 8;
            const int lower = position & 255;
            if (static_cast<unsigned>(row) < rows) light_up(column_pixels + row * width * 3, 255 - lower);
            if (static_cast<unsigned>(row + 1) < rows) light_up(column_pixels + (row + 1) * width * 3, lower);
        }
    }
}

void AuraMercuryView::sharp_line(const Point& from, const Point& to, float line_width, const Light& light,
                                 bool last) noexcept {
    if (!recording_) {
        Span span{};
        if (!line_span(from, to, line_width, light, last, span)) return;
        Strip strip = whole_strip();
        draw_span(span, strip);
        frame_work_ += span_pixels(span);
        return;
    }
    // Dimmed by the vignette and the edge fade at its middle; a line whose
    // ends the vignette dims differently in pieces, a piece for every 16/256
    // between them, so the dimming follows it (the resonance's ticks run
    // outwards through the vignette). The web's lines cross the clear centre
    // in one piece. The pieces' joints light once.
    const float delta_x = to.x - from.x;
    const float delta_y = to.y - from.y;
    const int pieces = 1 + std::abs(attenuation(from.x, from.y) - attenuation(to.x, to.y)) / 16;
    const float per_piece = pieces > 1 ? estimated_reciprocal(static_cast<float>(pieces)) : 1.0F;
    Point start = from;
    for (int piece = 1; piece <= pieces; ++piece) {
        const float share = static_cast<float>(piece) * per_piece;
        const Point end = piece == pieces ? to : Point{from.x + delta_x * share, from.y + delta_y * share, 0.0F};
        Span span{};
        if (line_span(start, end, line_width, light, piece == pieces ? last : false, span) &&
            spans_.size() < 65535U) {
            const int dim = attenuation((start.x + end.x) * 0.5F, (start.y + end.y) * 0.5F);
            span.red = (span.red * dim) >> 8;
            span.green = (span.green * dim) >> 8;
            span.blue = (span.blue * dim) >> 8;
            spans_.push_back(span);
            frame_work_ += span_pixels(span);
        }
        start = end;
    }
}

void AuraMercuryView::sharp_disc(float x, float y, float radius, const Light& light) noexcept {
    if (recording_) {
        record(ShapeKind::Disc, x, y, x, y, radius, light, radius + 0.5F);
        return;
    }
    Strip strip = whole_strip();
    device_disc({ShapeKind::Disc, 0, 0, x, y, x, y, radius, light}, strip);
    frame_work_ += strip.work;
}

void SYNAPSE_FAST AuraMercuryView::device_disc(const Shape& shape, Strip& strip) const noexcept {
    const float x = shape.x0;
    const float y = shape.y0;
    const float radius = shape.size;
    const Light& light = shape.light;
    const float reach = radius + 0.5F;
    const SharpCanvas canvas{strip.light, strip.first_row, strip.top, strip.bottom};
    const auto red = static_cast<int>(light.red * 256.0F);
    const auto green = static_cast<int>(light.green * 256.0F);
    const auto blue = static_cast<int>(light.blue * 256.0F);
    const int left = floor_int(x - reach);
    const int right = ceil_int(x + reach);
    const int top = std::max(canvas.top, floor_int(y - reach));
    const int bottom = std::min(canvas.bottom - 1, ceil_int(y + reach));
    std::uint64_t work = 0;
    for (int row = top; row <= bottom; ++row) {
        const float offset_y = static_cast<float>(row) + 0.5F - y;
        for (int column = left; column <= right; ++column) {
            const float offset_x = static_cast<float>(column) + 0.5F - x;
            const float coverage = clamp01(reach - estimated_root(offset_x * offset_x + offset_y * offset_y));
            if (coverage > 0.0F) {
                canvas.add(column, row, red, green, blue, static_cast<int>(coverage * 255.0F + 0.5F), work);
            }
        }
    }
    strip.work += work;
}

// One straight stroke with round caps. Lines thinner than a pixel light a
// one-pixel line proportionally less, as the browser's hairlines do.
void AuraMercuryView::stroke(const Point& from, const Point& to, float line_width, const Light& light) noexcept {
    if (sharp_) {
        sharp_line(from, to, line_width, light, true);
        return;
    }
    const float half = std::max(line_width, 1.0F) * 0.5F;
    const float strength = std::min(line_width, 1.0F);
    const float reach = half + 0.5F;
    if (recording_) {
        record(ShapeKind::Stroke, from.x, from.y, to.x, to.y, line_width, light, reach);
        return;
    }
    std::uint8_t* const canvas = light_;
    const int canvas_row = light_row_;
    const int left = std::max(0, floor_int(std::min(from.x, to.x) - reach));
    const int right = std::min(width - 1, ceil_int(std::max(from.x, to.x) + reach));
    const int top = std::max(clip_top_, floor_int(std::min(from.y, to.y) - reach));
    const int bottom = std::min(clip_bottom_ - 1, ceil_int(std::max(from.y, to.y) + reach));
    if (left > right || top > bottom) return;
    const float delta_x = to.x - from.x;
    const float delta_y = to.y - from.y;
    const LineBand band(from.x, from.y, delta_x, delta_y, reach);
    const float length_squared = delta_x * delta_x + delta_y * delta_y;
    const float inverse = length_squared > 1e-6F ? 1.0F / length_squared : 0.0F;
    const float outer = reach * reach;
    const float inner = half > 0.5F ? (half - 0.5F) * (half - 0.5F) : -1.0F;
    // Pixels the stroke covers completely all receive the same light.
    const Light solid{light.red * strength, light.green * strength, light.blue * strength};
    const int solid_blue = static_cast<int>(solid.blue + 0.5F);
    const int solid_green = static_cast<int>(solid.green + 0.5F);
    const int solid_red = static_cast<int>(solid.red + 0.5F);
    std::uint64_t work = 0;
    for (int y = top; y <= bottom; ++y) {
        const float offset_y = static_cast<float>(y) + 0.5F - from.y;
        int first = left;
        int last = right;
        band.clip(y, first, last);
        work += static_cast<std::uint64_t>(last >= first ? last - first + 1 : 0);
        auto* pixel = canvas + (static_cast<std::size_t>(y - canvas_row) * static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(first)) * 3;
        for (int x = first; x <= last; ++x, pixel += 3) {
            const float offset_x = static_cast<float>(x) + 0.5F - from.x;
            const float along = clamp01((offset_x * delta_x + offset_y * delta_y) * inverse);
            const float away_x = offset_x - along * delta_x;
            const float away_y = offset_y - along * delta_y;
            const float distance_squared = away_x * away_x + away_y * away_y;
            if (distance_squared >= outer) continue;
            if (distance_squared <= inner) {
                pixel[0] = static_cast<std::uint8_t>(std::min(255, pixel[0] + solid_blue));
                pixel[1] = static_cast<std::uint8_t>(std::min(255, pixel[1] + solid_green));
                pixel[2] = static_cast<std::uint8_t>(std::min(255, pixel[2] + solid_red));
            } else {
                add_to(pixel, solid, clamp01(reach - std::sqrt(distance_squared)));
            }
        }
    }
    frame_work_ += work;
}

// One stroke along a polyline: a single path, so its joints light once (the
// lattice's separate steps, drawn with stroke(), brighten theirs). Round caps.
void AuraMercuryView::stroke_path(const Point* points, int count, float line_width, const Light& light) noexcept {
    if (count < 2) return;
    if (sharp_) {
        for (int segment = 0; segment + 1 < count; ++segment) {
            sharp_line(points[segment], points[segment + 1], line_width, light, segment + 2 == count);
        }
        return;
    }
    if (recording_) {
        // The device frame draws every polyline sharp; a soft one would keep
        // its segments as strokes, their joints lit twice.
        for (int segment = 0; segment + 1 < count; ++segment) stroke(points[segment], points[segment + 1], line_width, light);
        return;
    }
    const float half = std::max(line_width, 1.0F) * 0.5F;
    const float strength = std::min(line_width, 1.0F);
    const float reach = half + 0.5F;
    const float outer = reach * reach;
    const float inner = half > 0.5F ? (half - 0.5F) * (half - 0.5F) : -1.0F;
    for (int segment = 0; segment + 1 < count; ++segment) {
        const Point& from = points[segment];
        const Point& to = points[segment + 1];
        const bool first = segment == 0;
        const bool last = segment + 2 == count;
        const int left = std::max(0, floor_int(std::min(from.x, to.x) - reach));
        const int right = std::min(width - 1, ceil_int(std::max(from.x, to.x) + reach));
        const int top = std::max(clip_top_, floor_int(std::min(from.y, to.y) - reach));
        const int bottom = std::min(clip_bottom_ - 1, ceil_int(std::max(from.y, to.y) + reach));
        if (left > right || top > bottom) continue;
        const float delta_x = to.x - from.x;
        const float delta_y = to.y - from.y;
        const float length_squared = delta_x * delta_x + delta_y * delta_y;
        const float inverse = length_squared > 1e-6F ? 1.0F / length_squared : 0.0F;
        const LineBand band(from.x, from.y, delta_x, delta_y, reach);
        for (int y = top; y <= bottom; ++y) {
            const float offset_y = static_cast<float>(y) + 0.5F - from.y;
            int start = left;
            int stop = right;
            band.clip(y, start, stop);
            for (int x = start; x <= stop; ++x) {
                const float offset_x = static_cast<float>(x) + 0.5F - from.x;
                float along = (offset_x * delta_x + offset_y * delta_y) * inverse;
                // Each pixel belongs to one segment; the ends keep their caps.
                if ((along < 0.0F && !first) || (along >= 1.0F && !last)) continue;
                along = clamp01(along);
                const float away_x = offset_x - along * delta_x;
                const float away_y = offset_y - along * delta_y;
                const float distance_squared = away_x * away_x + away_y * away_y;
                if (distance_squared >= outer) continue;
                const float coverage =
                    distance_squared <= inner ? 1.0F : clamp01(reach - std::sqrt(distance_squared));
                add(x, y, light, coverage * strength);
            }
        }
    }
}

// A dashed arc of a circle drawn through rotate(rotation) and scale(1, scale_y)
// about the centre, as the presentation's ellipses are. Dash lengths, the dash
// offset and the line width are in the circle's own units.
void AuraMercuryView::dashed_arc(float rotation, float scale_y, float radius, float start, float end,
                                 const std::array<float, 4>& dashes, double offset, float line_width,
                                 const Light& light) noexcept {
    const float pattern = dashes[0] + dashes[1] + dashes[2] + dashes[3];
    const float length = radius * (end - start);
    if (pattern <= 0.0F || length <= 0.0F) return;
    const float cos_r = std::cos(rotation);
    const float sin_r = std::sin(rotation);
    const auto at = [&](float travelled) {
        const float angle = start + travelled / radius;
        const float x = std::cos(angle) * radius;
        const float y = std::sin(angle) * radius * scale_y;
        return Point{kCentreX + x * cos_r - y * sin_r, kCentreY + x * sin_r + y * cos_r, 0.0F};
    };
    // Where the path starts inside the dash pattern.
    auto position = static_cast<float>(std::fmod(offset, static_cast<double>(pattern)));
    if (position < 0.0F) position += pattern;
    int element = 0;
    while (position >= dashes[static_cast<std::size_t>(element)]) {
        position -= dashes[static_cast<std::size_t>(element)];
        element = (element + 1) % 4;
    }
    std::array<Point, 48> points{};
    constexpr int most_pieces = static_cast<int>(points.size()) - 1;
    float travelled = 0.0F;
    if (recording_) {
        // The device frame turns its way round the arc, from its start by each
        // dash's and gap's angle, then each dash a step at a time: the board
        // took its sines from flash for every point, and divided in ROM.
        const float inverse_radius = estimated_reciprocal(radius);
        float cosine = std::cos(start);
        float sine = std::sin(start);
        while (travelled < length) {
            const float remaining = dashes[static_cast<std::size_t>(element)] - position;
            const float stop = std::min(length, travelled + remaining);
            const float angle = (stop - travelled) * inverse_radius;
            if ((element & 1) == 0) {
                // The stroke's width across the scaled circle at the dash's middle.
                const Turn half = Turn::by(angle * 0.5F);
                const float middle_cosine = cosine * half.cosine - sine * half.sine;
                const float middle_sine = sine * half.cosine + cosine * half.sine;
                const float tangent = estimated_root(middle_sine * middle_sine +
                                                     scale_y * scale_y * middle_cosine * middle_cosine);
                const float shown = line_width * scale_y * estimated_reciprocal(std::max(tangent, 1e-3F));
                const int pieces = std::clamp(ceil_int((stop - travelled) * (1.0F / 3.0F)), 1, most_pieces);
                const Turn step = Turn::by(angle * estimated_reciprocal(static_cast<float>(pieces)));
                float point_cosine = cosine;
                float point_sine = sine;
                for (int piece = 0; piece <= pieces; ++piece) {
                    const float x = point_cosine * radius;
                    const float y = point_sine * radius * scale_y;
                    points[static_cast<std::size_t>(piece)] =
                        Point{kCentreX + x * cos_r - y * sin_r, kCentreY + x * sin_r + y * cos_r, 0.0F};
                    step.apply(point_cosine, point_sine);
                }
                stroke_path(points.data(), pieces + 1, shown, light);
            }
            Turn::by(angle).apply(cosine, sine);
            travelled = stop;
            position = 0.0F;
            element = (element + 1) % 4;
        }
        return;
    }
    while (travelled < length) {
        const float remaining = dashes[static_cast<std::size_t>(element)] - position;
        const float stop = std::min(length, travelled + remaining);
        if ((element & 1) == 0) {
            // The stroke's width across the scaled circle at the dash's middle.
            const float middle = start + (travelled + stop) * 0.5F / radius;
            const float tangent = std::hypot(std::sin(middle), scale_y * std::cos(middle));
            const float shown = line_width * scale_y / std::max(tangent, 1e-3F);
            const int pieces = std::clamp(ceil_int((stop - travelled) / 3.0F), 1, most_pieces);
            for (int piece = 0; piece <= pieces; ++piece) {
                points[static_cast<std::size_t>(piece)] =
                    at(travelled + (stop - travelled) * static_cast<float>(piece) / static_cast<float>(pieces));
            }
            stroke_path(points.data(), pieces + 1, shown, light);
        }
        travelled = stop;
        position = 0.0F;
        element = (element + 1) % 4;
    }
}

// A radial gradient from `alpha` at the centre to nothing at `radius`.
void AuraMercuryView::radial_glow(float x, float y, float radius, int red, int green, int blue,
                                  float alpha) noexcept {
    glow(x, y, radius,
         {static_cast<float>(red) * alpha, static_cast<float>(green) * alpha, static_cast<float>(blue) * alpha});
}

void AuraMercuryView::glow(float x, float y, float radius, const Light& light) noexcept {
    if (recording_) {
        record(ShapeKind::Glow, x, y, x, y, radius, light, radius);
        return;
    }
    std::uint8_t* const canvas = light_;
    const int canvas_row = light_row_;
    const int left = std::max(0, floor_int(x - radius));
    const int right = std::min(width - 1, ceil_int(x + radius));
    const int top = std::max(clip_top_, floor_int(y - radius));
    const int bottom = std::min(clip_bottom_ - 1, ceil_int(y + radius));
    const float limit = radius * radius;
    if (right >= left && bottom >= top) {
        frame_work_ += static_cast<std::uint64_t>(right - left + 1) * static_cast<std::uint64_t>(bottom - top + 1);
    }
    for (int row = top; row <= bottom; ++row) {
        const float offset_y = static_cast<float>(row) + 0.5F - y;
        auto* pixels = canvas + static_cast<std::size_t>(row - canvas_row) * width * 3;
        for (int column = left; column <= right; ++column) {
            const float offset_x = static_cast<float>(column) + 0.5F - x;
            const float distance_squared = offset_x * offset_x + offset_y * offset_y;
            if (distance_squared >= limit) continue;
            add_to(pixels + static_cast<std::size_t>(column) * 3, light, 1.0F - std::sqrt(distance_squared) / radius);
        }
    }
}

void AuraMercuryView::centred_glow(float radius, int red, int green, int blue, float alpha) noexcept {
    centred_light(radius,
                  {static_cast<float>(red) * alpha, static_cast<float>(green) * alpha, static_cast<float>(blue) * alpha});
}

void AuraMercuryView::centred_light(float radius, const Light& light) noexcept {
    if (recording_) {
        record(ShapeKind::CentredGlow, kCentreX, kCentreY, kCentreX, kCentreY, radius, light, radius);
        return;
    }
    constexpr int centre_x = width / 2;
    constexpr int centre_y = height / 2;
    // The falloff, 1 - distance / radius, of one row of quadrant pixels at a
    // time: the frame's centre lies on a pixel corner, so the four quadrants'
    // pixels mirror one another. Worked out row by row it needs no table (two
    // of 51 and 14 KiB, which the board read from PSRAM).
    constexpr int kMaxSpan = 128;
    const int span = std::min(kMaxSpan, ceil_int(radius) + 1);
    const float limit = radius * radius;
    std::array<float, kMaxSpan> falloff{};
    std::uint8_t* const canvas = light_;
    const int canvas_row = light_row_;
    const int left = std::max(0, floor_int(static_cast<float>(centre_x) - radius));
    const int right = std::min(width - 1, ceil_int(static_cast<float>(centre_x) + radius));
    const int top = std::max(clip_top_, floor_int(static_cast<float>(centre_y) - radius));
    const int bottom = std::min(clip_bottom_ - 1, ceil_int(static_cast<float>(centre_y) + radius));
    if (right >= left && bottom >= top) {
        frame_work_ += static_cast<std::uint64_t>(right - left + 1) * static_cast<std::uint64_t>(bottom - top + 1);
    }
    for (int row = top; row <= bottom; ++row) {
        // Quadrant rows and columns count outwards from the centre: 0 is the
        // pixel whose corner touches it.
        const int quadrant_row = row >= centre_y ? row - centre_y : centre_y - 1 - row;
        if (quadrant_row >= span) continue;
        const float offset_y = static_cast<float>(quadrant_row) + 0.5F;
        for (int column = 0; column < span; ++column) {
            const float offset_x = static_cast<float>(column) + 0.5F;
            const float distance_squared = offset_x * offset_x + offset_y * offset_y;
            falloff[static_cast<std::size_t>(column)] =
                distance_squared >= limit ? 0.0F : 1.0F - std::sqrt(distance_squared) / radius;
        }
        auto* pixels = canvas + static_cast<std::size_t>(row - canvas_row) * width * 3;
        for (int column = left; column <= right; ++column) {
            const int quadrant_column = column >= centre_x ? column - centre_x : centre_x - 1 - column;
            if (quadrant_column >= span) continue;
            const float coverage = falloff[static_cast<std::size_t>(quadrant_column)];
            if (coverage > 0.0F) add_to(pixels + static_cast<std::size_t>(column) * 3, light, coverage);
        }
    }
}

void AuraMercuryView::nucleus(float sphere) noexcept {
    const float reach = sphere + 0.5F;
    if (recording_) {
        record(ShapeKind::Nucleus, kCentreX, kCentreY, kCentreX, kCentreY, sphere, {}, reach);
        return;
    }
    // A radial gradient from radius 1 to the sphere's edge, added.
    struct Stop {
        float at;
        float red;
        float green;
        float blue;
        float alpha;
    };
    constexpr Stop stops[] = {{0.0F, 245.0F, 255.0F, 255.0F, 1.0F},
                              {0.15F, 184.0F, 255.0F, 241.0F, 0.98F},
                              {0.43F, 82.0F, 218.0F, 216.0F, 0.84F},
                              {0.72F, 42.0F, 27.0F, 94.0F, 0.94F},
                              {1.0F, 4.0F, 2.0F, 13.0F, 1.0F}};
    std::uint8_t* const canvas = light_;
    const int canvas_row = light_row_;
    const int left = floor_int(kCentreX - reach);
    const int right = ceil_int(kCentreX + reach);
    const int top = std::max(clip_top_, floor_int(kCentreY - reach));
    const int bottom = std::min(clip_bottom_ - 1, ceil_int(kCentreY + reach));
    for (int row = top; row <= bottom; ++row) {
        auto* pixels = canvas + static_cast<std::size_t>(row - canvas_row) * width * 3;
        for (int column = left; column <= right; ++column) {
            const float distance = std::hypot(static_cast<float>(column) + 0.5F - kCentreX,
                                              static_cast<float>(row) + 0.5F - kCentreY);
            const float coverage = clamp01(reach - distance);
            if (coverage <= 0.0F) continue;
            const float position = clamp01((distance - 1.0F) / (sphere - 1.0F));
            std::size_t stop = 0;
            while (stop + 2 < std::size(stops) && position > stops[stop + 1].at) ++stop;
            const Stop& from = stops[stop];
            const Stop& to = stops[stop + 1];
            const float share = clamp01((position - from.at) / (to.at - from.at));
            const float alpha = mix(from.alpha, to.alpha, share);
            add_to(pixels + static_cast<std::size_t>(column) * 3,
                   {mix(from.red, to.red, share) * alpha, mix(from.green, to.green, share) * alpha,
                    mix(from.blue, to.blue, share) * alpha},
                   coverage);
        }
    }
}

void AuraMercuryView::fill_circle(float x, float y, float radius, const Light& light) noexcept {
    if (sharp_ || recording_) {
        sharp_disc(x, y, radius, light);
        return;
    }
    const float reach = radius + 0.5F;
    const int left = std::max(0, floor_int(x - reach));
    const int right = std::min(width - 1, ceil_int(x + reach));
    const int top = std::max(clip_top_, floor_int(y - reach));
    const int bottom = std::min(clip_bottom_ - 1, ceil_int(y + reach));
    if (right >= left && bottom >= top) {
        frame_work_ += static_cast<std::uint64_t>(right - left + 1) * static_cast<std::uint64_t>(bottom - top + 1);
    }
    for (int row = top; row <= bottom; ++row) {
        const float offset_y = static_cast<float>(row) + 0.5F - y;
        for (int column = left; column <= right; ++column) {
            const float offset_x = static_cast<float>(column) + 0.5F - x;
            const float coverage = clamp01(reach - std::hypot(offset_x, offset_y));
            if (coverage > 0.0F) add(column, row, light, coverage);
        }
    }
}

// ---- The presentation's layers ---------------------------------------------

void AuraMercuryView::draw_orbits() noexcept {
    // drawOrbitArcs(): five dashed elliptical traces outside the lattice.
    if (!draws(Part::Sharp)) return;
    for (int ring = 0; ring < 5; ++ring) {
        const bool odd = (ring & 1) != 0;
        const float rotation = spin(phase_ * (odd ? -0.38 : 0.29)) + static_cast<float>(ring) * 0.69F;
        const float scale_y = 0.66F + static_cast<float>(ring) * 0.034F + spread_ * 0.032F;
        const float radius = 157.0F + static_cast<float>(ring) * 8.0F + drive_ * 10.0F;
        const std::array<float, 4> dashes{{42.0F + static_cast<float>(ring) * 5.0F,
                                           67.0F + static_cast<float>(ring) * 8.0F, 6.0F, 36.0F}};
        const double offset = -(seconds_ * 25.0 + phase_ * 68.0) * (odd ? -1.0 : 1.0);
        const Spectral colour = spectral_colour(static_cast<float>(ring) * 0.16F + cycle(seconds_ * 0.022));
        const float alpha = clamp01(0.10F + weave_ * 0.052F);
        dashed_arc(rotation, scale_y, radius, 0.0F, kTau, dashes, offset, ring == 0 ? 1.4F : 0.72F,
                   {colour.red * alpha, colour.green * alpha, colour.blue * alpha});
    }
}

void AuraMercuryView::draw_resonance() noexcept {
    // drawResonance(): 84 ticks that become a voice meter while listening and speaking.
    if (!draws(Part::Sharp)) return;
    constexpr int ticks = 84;
    const float active = weights_[1] + weights_[3];
    const float signal = drive_ + (1.0F - active) * weave_ * 0.18F;
    const float turn = spin(phase_ * 0.16);
    const float shimmer = spin(seconds_ * 2.8);
    const float pulse = spin(seconds_ * 3.6);
    const float drift = cycle(seconds_ * 0.028);
    for (int index = 0; index < ticks; ++index) {
        const auto tick = static_cast<float>(index);
        const float angle = tick / ticks * kTau + turn;
        const float texture = 0.5F + 0.5F * std::sin(tick * 1.69F + shimmer);
        const float length = 3.0F + signal * (10.0F + 31.0F * texture);
        const float radius = 176.0F + spread_ * 12.0F + signal * 8.0F * std::sin(tick * 0.68F - pulse);
        const float cos_a = std::cos(angle);
        const float sin_a = std::sin(angle);
        const Point from{kCentreX + cos_a * radius, kCentreY + sin_a * radius * 0.91F, 0.0F};
        const Point to{kCentreX + cos_a * (radius + length), kCentreY + sin_a * (radius + length) * 0.91F, 0.0F};
        const Spectral colour = spectral_colour(tick / ticks * 0.72F + drift);
        const float alpha = clamp01(0.07F + signal * 0.48F);
        stroke(from, to, 0.75F + signal * 1.45F, {colour.red * alpha, colour.green * alpha, colour.blue * alpha});
    }
}

void AuraMercuryView::build_paths() noexcept {
    // buildLoops(): four paths and their exact point reflections, so the
    // reactor stays at the visual centre while the structure transforms.
    const float tilt = spin(seconds_ * 0.17);
    const float turn = spin(phase_ * 0.54);
    const float ripple = spin(seconds_ * 10.2);
    const float twist = spin(seconds_ * 0.46 + phase_ * 0.34);
    const float counter_twist = spin(phase_ * 0.52);
    const float lift = spin(phase_ * 2.05);
    const float sway = spin(seconds_ * 1.2);
    // Each term below is sin(k·angle + phase): the angles' sines come from a
    // table and each phase is a sine and a cosine per loop, instead of nine
    // sines per sample (the board spent ~6 ms a frame on them).
    const auto& harmonics = path_harmonics<kSteps>();
    const Phase sway_phase(sway);
    for (int base = 0; base < kPaths / 2; ++base) {
        const auto loop = static_cast<float>(base);
        const float rotate_x = 0.18F + 0.36F * std::sin(tilt + loop * 0.72F) + loop * 0.066F;
        const float rotate_y = turn + loop * 0.49F;
        const float cos_x = std::cos(rotate_x);
        const float sin_x = std::sin(rotate_x);
        const float cos_y = std::cos(rotate_y);
        const float sin_y = std::sin(rotate_y);
        const Phase voice_phase(loop * 0.73F - ripple);
        const Phase knot_phase(loop * 1.17F + twist);
        const Phase counter_phase(-loop * 0.61F - counter_twist);
        const Phase lift_phase(loop * 1.03F + lift);
        const Phase spread_phase(loop * 0.86F);
        for (int sample = 0; sample <= kSteps; ++sample) {
            const Harmonics& at = harmonics[static_cast<std::size_t>(sample)];
            const float voice = drive_ * wave_ * voice_phase.of(at.sin8, at.cos8);
            const float knot = knot_phase.of(at.sin3, at.cos3);
            const float counter = counter_phase.of(at.sin5, at.cos5);
            const float radius =
                104.0F + loop * 2.15F + knot * (10.0F + weave_ * 8.0F) + counter * 3.5F + voice * 21.0F;
            const float sin_a = at.sin1;
            float x = at.cos1 * radius * (1.0F + spread_ * 0.15F * sin_a * sin_a);
            float y = sin_a * radius * (0.76F + spread_ * 0.13F);
            const float z = lift_phase.of(at.sin2, at.cos2) * (32.0F + weave_ * 40.0F);
            x += spread_ * 14.0F * spread_phase.of(at.sin2, at.cos2);
            // cos(3·angle - sway) = cos(3·angle)·cos(sway) + sin(3·angle)·sin(sway).
            y += voice * 10.0F + spread_ * 8.0F * (at.cos3 * sway_phase.cosine + at.sin3 * sway_phase.sine);
            const float turned_x = x * cos_y + z * sin_y;
            const float turned_z = z * cos_y - x * sin_y;
            const float turned_y = y * cos_x - turned_z * sin_x;
            const float depth = y * sin_x + turned_z * cos_x;
            const float scale = 390.0F / (390.0F - depth);
            const Point point{kCentreX + turned_x * scale, kCentreY + turned_y * scale, depth};
            paths_[static_cast<std::size_t>(base * 2 * (kSteps + 1) + sample)] = point;
            paths_[static_cast<std::size_t>((base * 2 + 1) * (kSteps + 1) + sample)] =
                Point{kCentreX * 2.0F - point.x, kCentreY * 2.0F - point.y, -point.z};
        }
    }
}

void AuraMercuryView::draw_waves() noexcept {
    // drawPulseWaves(): three dotted wavefronts expanding from the reactor.
    if (!draws(Part::Sharp)) return;
    const float active = weights_[1] + weights_[3];
    const float strength = active * 0.26F + drive_ * 0.74F + weights_[2] * 0.23F + weights_[0] * 0.08F;
    for (int ring = 0; ring < 3; ++ring) {
        const auto index = static_cast<float>(ring);
        const float age = cycle(seconds_ * 0.12 + phase_ * 0.045 + ring / 3.0);
        const float radius = 105.0F + age * 112.0F + drive_ * 8.0F;
        const float rotation = spin(phase_ * 0.08) + index * 0.42F;
        const float scale_y = 0.88F + 0.035F * std::sin(spin(seconds_) + index);
        const Spectral colour = spectral_colour(0.12F + index * 0.22F + cycle(seconds_ * 0.016));
        const float alpha = clamp01((1.0F - age) * strength * 0.26F);
        dashed_arc(rotation, scale_y, radius, 0.0F, kTau, {{2.0F, 9.0F + index * 2.0F, 2.0F, 9.0F + index * 2.0F}},
                   -seconds_ * 28.0, 0.7F + strength * 0.8F,
                   {colour.red * alpha, colour.green * alpha, colour.blue * alpha});
    }
}

void AuraMercuryView::draw_lattice() noexcept {
    // drawLattice(): every path step is its own round-capped stroke, so the
    // joints glow brighter: a soft wide pass, then the bright core pass. The
    // device frame draws the soft pass from every fourth step, brighter for
    // the joints it no longer overlaps (as the reduced frame does); its bright
    // pass is sharp, every step.
    const float drift = cycle(seconds_ * 0.024);
    const bool device = profile_ == SynapseProfile::Device;
    const bool reduced = frame_quality_ == SynapseQuality::Reduced && !device;
    // The board divides floats in ROM, a call each: the device frame
    // multiplies by the reciprocals instead (the presentation divides).
    const auto along = [device](int sample) {
        return device ? static_cast<float>(sample) * (1.0F / kSteps) : static_cast<float>(sample) / kSteps;
    };
    for (int pass = 0; pass < 2; ++pass) {
        if (!draws(pass == 0 ? Part::Soft : Part::Sharp)) continue;
        const bool sparse = reduced || (device && pass == 0);
        const int stride = !sparse ? 1 : pass == 0 ? kReducedSoftStride : kReducedBrightStride;
        const float gain = pass == 0 && sparse ? kReducedSoftGain : 1.0F;
        for (int index = 0; index < kPaths; ++index) {
            for (int sample = 0; sample < kSteps; sample += stride) {
                const Point& from = path(index, sample);
                const Point& to = path(index, std::min(sample + stride, kSteps));
                const float depth = (from.z + to.z) * 0.5F + 100.0F;
                const float front = clamp01(device ? depth * (1.0F / 200.0F) : depth / 200.0F);
                const Spectral colour =
                    spectral_colour(along(sample) * 0.58F + static_cast<float>(index) * 0.085F + drift);
                const float alpha = pass == 0 ? (0.022F + front * 0.031F) * gain : clamp01(0.22F + front * 0.76F);
                stroke(from, to, pass == 0 ? 9.0F + front * 5.0F : 0.5F + front * 1.65F,
                       {colour.red * alpha, colour.green * alpha, colour.blue * alpha});
            }
        }
    }
    // The translucent web between neighbouring paths.
    if (!draws(Part::Sharp)) return;
    for (int sample = 0; sample < kSteps; sample += 7) {
        for (int index = 0; index + 1 < kPaths; ++index) {
            const Point& from = path(index, sample);
            const Point& to = path(index + 1, sample);
            const float depth = (from.z + to.z) * 0.5F + 90.0F;
            const float front = clamp01(device ? depth * (1.0F / 180.0F) : depth / 180.0F);
            const Spectral colour = spectral_colour(along(sample) * 0.52F + static_cast<float>(index) * 0.095F);
            const float alpha = 0.045F + front * 0.14F;
            stroke(from, to, 0.48F, {colour.red * alpha, colour.green * alpha, colour.blue * alpha});
        }
    }
}

void AuraMercuryView::draw_particles() noexcept {
    // drawParticles(): sparks travelling along the paths with short trails.
    // Their glows are soft; the trails and the sparks are sharp.
    const int sparks = weights_[2] > 0.5F ? 30 : 18;
    std::array<Point, 11> trail{};
    for (int particle = 0; particle < sparks; ++particle) {
        const int index = particle % kPaths;
        const float progress = cycle(phase_ * (0.070 + (particle % 5) * 0.009) + particle * 0.113);
        const int sample = std::min(kSteps - 1, static_cast<int>(progress * kSteps));
        const Point& head = path(index, sample);
        const float front = clamp01((head.z + 100.0F) / 200.0F);
        for (int step = 10; step >= 0; --step) {
            trail[static_cast<std::size_t>(10 - step)] = path(index, (sample - step + kSteps) % kSteps);
        }
        if (draws(Part::Sharp)) {
            const Spectral line = spectral_colour(progress * 0.68F + static_cast<float>(index) * 0.08F);
            const float line_alpha = 0.12F + front * 0.34F;
            stroke_path(trail.data(), static_cast<int>(trail.size()), 0.65F + front * 1.25F,
                        {line.red * line_alpha, line.green * line_alpha, line.blue * line_alpha});
        }
        if (draws(Part::Soft)) {
            if ((particle & 1) != 0) {
                radial_glow(head.x, head.y, 9.0F + front * 8.0F, 193, 157, 255, 0.10F + front * 0.16F);
            } else {
                radial_glow(head.x, head.y, 9.0F + front * 8.0F, 133, 255, 229, 0.10F + front * 0.16F);
            }
        }
        if (draws(Part::Sharp)) {
            const Spectral spark = spectral_colour(progress * 0.65F + static_cast<float>(index) * 0.1F);
            const float spark_alpha = clamp01(0.45F + front * 0.5F);
            fill_circle(head.x, head.y, 1.0F + front * 1.55F,
                        {spark.red * spark_alpha, spark.green * spark_alpha, spark.blue * spark_alpha});
        }
    }
}

void AuraMercuryView::draw_core() noexcept {
    // drawCore(). The presentation's dark lens is filled while the canvas adds
    // light, so it leaves no mark; the reactor is pure light here as well.
    // Its glows, the nucleus and the highlight are soft; the rings and the
    // nucleus's rim are sharp.
    const float aperture = 58.0F + spread_ * 8.0F + drive_ * 9.0F;
    const float sphere = 20.0F + drive_ * 5.0F + spread_ * 1.5F;
    if (draws(Part::Soft)) {
        centred_glow(112.0F, 104, 78, 255, 0.10F + drive_ * 0.07F);
        centred_glow(58.0F, 85, 247, 222, 0.13F + drive_ * 0.11F);
    }

    for (int ring = 0; ring < 4 && draws(Part::Sharp); ++ring) {
        const bool odd = (ring & 1) != 0;
        const auto index = static_cast<float>(ring);
        const Spectral colour = spectral_colour(index * 0.19F + cycle(seconds_ * 0.04));
        const float alpha = clamp01(0.34F + drive_ * 0.50F);
        dashed_arc(spin(phase_ * (odd ? -0.72 : 0.88)) + index * 1.19F, 0.58F + index * 0.09F,
                   aperture - 9.0F + index * 11.0F, 0.18F + index * 0.34F, 1.12F + index * 0.34F + drive_ * 0.65F,
                   odd ? std::array<float, 4>{{18.0F, 10.0F, 3.0F, 13.0F}}
                       : std::array<float, 4>{{32.0F, 17.0F, 32.0F, 17.0F}},
                   -seconds_ * (24.0 + ring * 7.0), 1.0F + drive_ * 1.8F,
                   {colour.red * alpha, colour.green * alpha, colour.blue * alpha});
    }

    if (draws(Part::Soft)) nucleus(sphere);
    // Its bright rim, one stroke around the sphere.
    if (draws(Part::Sharp)) {
        std::array<Point, 65> rim{};
        for (std::size_t index = 0; index < rim.size(); ++index) {
            const float angle = static_cast<float>(index) / static_cast<float>(rim.size() - 1) * kTau;
            rim[index] = Point{kCentreX + std::cos(angle) * sphere, kCentreY + std::sin(angle) * sphere, 0.0F};
        }
        const Spectral edge = spectral_colour(0.14F + cycle(seconds_ * 0.03));
        stroke_path(rim.data(), static_cast<int>(rim.size()), 1.15F,
                    {edge.red * 0.72F, edge.green * 0.72F, edge.blue * 0.72F});
    }
    if (draws(Part::Soft)) {
        radial_glow(kCentreX - sphere * 0.24F, kCentreY - sphere * 0.27F, 7.0F, 235, 255, 255, 0.16F);
    }
}

inline void AuraMercuryView::store(int x, int y, int red, int green, int blue) noexcept {
    const auto index = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
#if LV_COLOR_DEPTH == 16
    reinterpret_cast<std::uint16_t*>(canvas_)[index] =
        static_cast<std::uint16_t>(pack565(red, green, blue, kBayer[y & 3][x & 3]));
#else
    canvas_[index * 3] = static_cast<std::uint8_t>(blue);
    canvas_[index * 3 + 1] = static_cast<std::uint8_t>(green);
    canvas_[index * 3 + 2] = static_cast<std::uint8_t>(red);
#endif
}

void AuraMercuryView::update_tint() noexcept {
    // The presentation's background tint: two broad radial glows painted first.
    // They drift a pixel in a few seconds, so the device frame works out a few
    // rows of cells a frame (all of them took 6 ms on the board).
    const float violet_x = kCentreX - 74.0F + std::sin(spin(seconds_ * 0.22)) * 19.0F;
    const float violet_y = kCentreY - 24.0F;
    const float teal_x = kCentreX + 82.0F;
    const float teal_y = kCentreY + 17.0F + std::cos(spin(seconds_ * 0.18)) * 16.0F;
    const bool partial = profile_ == SynapseProfile::Device && tint_row_ >= 0;
    const int first = partial ? tint_row_ : 0;
    const int count = partial ? kTintRowsPerFrame : kTintRows;
    for (int step = 0; step < count; ++step) {
        const int row = (first + step) % kTintRows;
        for (int column = 0; column < kTintColumns; ++column) {
            const float x = (static_cast<float>(column) + 0.5F) * kTintCell;
            const float y = (static_cast<float>(row) + 0.5F) * kTintCell;
            const float violet = 0.14F * std::max(0.0F, 1.0F - std::hypot(x - violet_x, y - violet_y) / 205.0F);
            const float teal = 0.095F * std::max(0.0F, 1.0F - std::hypot(x - teal_x, y - teal_y) / 190.0F);
            // Source-over: the teal glow is painted over the violet one.
            auto& cell = tint_[static_cast<std::size_t>(row * kTintColumns + column)];
            cell[0] = static_cast<std::uint8_t>(104.0F * violet * (1.0F - teal) + 86.0F * teal + 0.5F);
            cell[1] = static_cast<std::uint8_t>(21.0F * violet * (1.0F - teal) + 91.0F * teal + 0.5F);
            cell[2] = static_cast<std::uint8_t>(49.0F * violet * (1.0F - teal) + 10.0F * teal + 0.5F);
        }
    }
    tint_row_ = (first + count) % kTintRows;
    // The background's rows under those cells, worked out by the strips that
    // hold them (the worker's and the helper's, each its own).
    background_from_ = first * kTintCell;
    background_to_ = std::min(height, (first + count) * kTintCell);
}

void AuraMercuryView::refresh_background(int first_row, int last_row) const noexcept {
    // As finish() works a pixel out with no light added.
    for (int y = first_row; y < last_row; ++y) {
        const auto* cells = &tint_[static_cast<std::size_t>((y / kTintCell) * kTintColumns)];
        const int row_factor = row_fade_[static_cast<std::size_t>(y)];
        const int doubled_y = 2 * y + 1 - height;
        auto* pixel = background_.get() + static_cast<std::size_t>(y) * width * 3;
        for (int x = 0; x < width; ++x, pixel += 3) {
            const int doubled_x = 2 * x + 1 - width;
            const int squared = (doubled_x * doubled_x + doubled_y * doubled_y) >> 2;
            const int factor = (vignette_[static_cast<std::size_t>(squared >> kVignetteShift)] * row_factor) >> 8;
            const auto& cell = cells[x / kTintCell];
            pixel[0] = static_cast<std::uint8_t>((cell[0] * factor) >> 8);
            pixel[1] = static_cast<std::uint8_t>((cell[1] * factor) >> 8);
            pixel[2] = static_cast<std::uint8_t>((cell[2] * factor) >> 8);
        }
    }
}

int AuraMercuryView::attenuation(float x, float y) const noexcept {
    const int row = std::clamp(floor_int(y), 0, height - 1);
    const int doubled_x = 2 * floor_int(x) + 1 - width;
    const int doubled_y = 2 * row + 1 - height;
    const auto squared = static_cast<std::size_t>((doubled_x * doubled_x + doubled_y * doubled_y) >> 2);
    const auto step = std::min(squared >> kVignetteShift, vignette_.size() - 1);
    return (vignette_[step] * row_fade_[static_cast<std::size_t>(row)]) >> 8;
}

void AuraMercuryView::finish() noexcept {
    // The background tint and the vignette, then this frame's own fade at its
    // top and bottom edges.
    update_tint();
    constexpr int columns = kTintColumns;
    const auto& tint = tint_;
    const auto* bytes = canvas_;
    for (int y = 0; y < height; ++y) {
        const int edge = std::min(y, height - 1 - y);
        float fade = edge >= kEdgeFade ? 1.0F : static_cast<float>(edge) / kEdgeFade;
        fade = fade * fade * (3.0F - 2.0F * fade);
        const auto row_factor = static_cast<int>(fade * 256.0F + 0.5F);
        const int doubled_y = 2 * y + 1 - height;
        for (int x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y * width + x);
            const int doubled_x = 2 * x + 1 - width;
            const int squared = (doubled_x * doubled_x + doubled_y * doubled_y) >> 2;
            const int factor = (vignette_[static_cast<std::size_t>(squared >> kVignetteShift)] * row_factor) >> 8;
            const auto& cell = tint[static_cast<std::size_t>((y / kTintCell) * columns + x / kTintCell)];
            const int blue = (std::min(255, bytes[index * 3] + cell[0]) * factor) >> 8;
            const int green = (std::min(255, bytes[index * 3 + 1] + cell[1]) * factor) >> 8;
            const int red = (std::min(255, bytes[index * 3 + 2] + cell[2]) * factor) >> 8;
            // In place: a pixel is read before it is written, and an RGB565
            // pixel never reaches past the RGB888 one it replaces.
            store(x, y, red, green, blue);
        }
    }
}

void AuraMercuryView::render() noexcept {
    frame_work_ = 0;
    if (profile_ == SynapseProfile::Device) {
        render_device();
    } else {
        // The presentation draws every part into the frame itself.
        light_ = canvas_;
        light_row_ = 0;
        clip_top_ = 0;
        clip_bottom_ = height;
        parts_ = Part::Both;
        sharp_ = false;
        std::memset(light_, 0, buffer_.size());
        draw_orbits();
        draw_resonance();
        build_paths();
        draw_waves();
        draw_lattice();
        draw_particles();
        draw_core();
        finish();
    }
}

// ---- The device frame, in strips ---------------------------------------------
// The frame's shapes first, the soft parts' and then the sharp ones', each
// kept for the strips it crosses; then strip by strip: its light gathered in
// fast memory, finished into the frame shown and, in the middle rows, sent to
// the panel at once. PSRAM is written once a pixel, never read back.

void AuraMercuryView::render_device() noexcept {
    update_tint();
    shapes_.clear();
    spans_.clear();
    recording_ = true;
    light_row_ = 0;
    clip_top_ = 0;
    clip_bottom_ = height;
    parts_ = Part::Soft;
    sharp_ = false;
    build_paths();
    draw_lattice();
    draw_particles();
    draw_core();
    parts_ = Part::Sharp;
    sharp_ = true;
    draw_orbits();
    draw_resonance();
    draw_waves();
    draw_lattice();
    draw_particles();
    draw_core();
    recording_ = false;
    sharp_ = false;
    parts_ = Part::Both;
    const int rows = strip_height_;
    const int strips = height / rows;
    for (auto& list : strip_shapes_) list.clear();
    for (auto& list : strip_spans_) list.clear();
    for (std::size_t index = 0; index < shapes_.size(); ++index) {
        const Shape& shape = shapes_[index];
        for (int strip = shape.top / rows; strip <= shape.bottom / rows; ++strip) {
            strip_shapes_[static_cast<std::size_t>(strip)].push_back(static_cast<std::uint16_t>(index));
        }
    }
    for (std::size_t index = 0; index < spans_.size(); ++index) {
        const Span& span = spans_[index];
        for (int strip = span.top / rows; strip <= span.bottom / rows; ++strip) {
            strip_spans_[static_cast<std::size_t>(strip)].push_back(static_cast<std::uint16_t>(index));
        }
    }
    // The panel takes six rows at a time; the platform's memory is for those.
    SynapseOutput* output = rows == strip_rows ? synapse_output() : nullptr;
    const auto bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(rows) * 3U;
    for (int index = 0; index < 4; ++index) strips_[static_cast<std::size_t>(index)] = strip_light(output, index, bytes);
    const bool sending = frame_direct_ && output != nullptr;
    // The helper (the board's other core) takes the odd strips while this
    // worker draws the even ones; the light adds up in each strip alone.
    StripRun run{this, 0, 1, 0, sending, 0};
    StripRun helped{this, 1, 2, 2, sending, 0};
    const bool shared = helper_ != nullptr && strips > 1 &&
                        helper_->submit(&AuraMercuryView::render_strips_on_helper, &helped);
    if (shared) run.step = 2;
    render_strips(run);
    if (shared) helper_->wait_idle();
    direct_sent_ = run.sending && (!shared || helped.sending);
    frame_work_ += run.work + (shared ? helped.work : 0U);
}

void AuraMercuryView::render_strips_on_helper(void* run) noexcept {
    auto* strips = static_cast<StripRun*>(run);
    strips->view->render_strips(*strips);
}

void AuraMercuryView::render_strips(StripRun& run) const noexcept {
    const int rows = strip_height_;
    const int strips = height / rows;
    SynapseOutput* output = rows == strip_rows ? synapse_output() : nullptr;
    const auto bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(rows) * 3U;
    // Its two memories take turns; once a strip could not be sent, the one
    // that is not on its way to the panel.
    int turn = 0;
    int kept = -1;
    for (int strip = run.first; strip < strips; strip += run.step, ++turn) {
        const int top = strip * rows;
        const int index = kept >= 0 ? kept : run.memory + (turn & 1);
        std::uint8_t* light = strips_[static_cast<std::size_t>(index)];
        // The rows of the background the tint has just moved under, then the
        // strip's light from the background.
        const int refresh_from = std::max(top, background_from_);
        const int refresh_to = std::min(top + rows, background_to_);
        if (refresh_from < refresh_to) refresh_background(refresh_from, refresh_to);
        std::memcpy(light, background_.get() + static_cast<std::size_t>(top) * width * 3U, bytes);
        Strip target{light, top, top, top + rows, 0};
        for (const auto shape : strip_shapes_[static_cast<std::size_t>(strip)]) {
            replay(shapes_[shape], target);
        }
        for (const auto span : strip_spans_[static_cast<std::size_t>(strip)]) draw_span(spans_[span], target);
        const bool direct = run.sending && top >= direct_top && top < direct_bottom;
        finish_strip(top, rows, light, direct);
        if (direct && !send_strip(*output, top, light)) {
            run.sending = false;
            kept = index;
        }
        run.work += target.work;
    }
}

// ---- The device frame's own soft shapes --------------------------------------
// Drawn into a strip with the presentation's light, but with the work a pixel
// in integers and few floats: on the board the presentation's float strokes
// and glows took ~160 cycles a pixel (measured 2026-11-15). A stroke's
// distance along and across its line changes by a constant from one pixel to
// the next, so only its round caps' edge needs a square root, and glows take
// one only inside their circle.

namespace {
// Light added to one RGB888 pixel: `red` etc. in 1/256 of a level, at
// `coverage` 0-256, saturating.
inline void add_scaled(std::uint8_t* pixel, int red, int green, int blue, int coverage) {
    pixel[0] = static_cast<std::uint8_t>(std::min(255, pixel[0] + ((blue * coverage + 32768) >> 16)));
    pixel[1] = static_cast<std::uint8_t>(std::min(255, pixel[1] + ((green * coverage + 32768) >> 16)));
    pixel[2] = static_cast<std::uint8_t>(std::min(255, pixel[2] + ((red * coverage + 32768) >> 16)));
}
inline int in_256ths(float value) { return static_cast<int>(value * 256.0F + 0.5F); }
// A column from where a run begins or ends, which a step near 0 puts far off
// the frame: brought near it first, as an int must hold it.
SYNAPSE_INLINE inline int floor_column(float value) {
    return floor_int(std::clamp(value, -8.0F, static_cast<float>(AuraMercuryView::width) + 8.0F));
}
SYNAPSE_INLINE inline int ceil_column(float value) {
    return ceil_int(std::clamp(value, -8.0F, static_cast<float>(AuraMercuryView::width) + 8.0F));
}
}  // namespace

void SYNAPSE_FAST AuraMercuryView::device_stroke(const Shape& shape, Strip& strip) const noexcept {
    const float line_width = shape.size;
    const float half = std::max(line_width, 1.0F) * 0.5F;
    const float strength = std::min(line_width, 1.0F);
    const float reach = half + 0.5F;
    const float inner = half - 0.5F;
    const float x0 = shape.x0;
    const float y0 = shape.y0;
    const float x1 = shape.x1;
    const float y1 = shape.y1;
    std::uint8_t* const canvas = strip.light;
    const int canvas_row = strip.first_row;
    const int left = std::max(0, floor_int(std::min(x0, x1) - reach));
    const int right = std::min(width - 1, ceil_int(std::max(x0, x1) + reach));
    const int top = std::max(strip.top, floor_int(std::min(y0, y1) - reach));
    const int bottom = std::min(strip.bottom - 1, ceil_int(std::max(y0, y1) + reach));
    if (left > right || top > bottom) return;
    // Whole where the stroke covers a pixel (rounded as the presentation
    // rounds it), and in 1/256 of a level across its edge.
    const float red = shape.light.red * strength;
    const float green = shape.light.green * strength;
    const float blue = shape.light.blue * strength;
    const int solid_red = static_cast<int>(red + 0.5F);
    const int solid_green = static_cast<int>(green + 0.5F);
    const int solid_blue = static_cast<int>(blue + 0.5F);
    const int edge_red = in_256ths(red);
    const int edge_green = in_256ths(green);
    const int edge_blue = in_256ths(blue);
    const float delta_x = x1 - x0;
    const float delta_y = y1 - y0;
    const float length_squared = delta_x * delta_x + delta_y * delta_y;
    const bool point = length_squared <= 1e-6F;
    const float inverse_squared = point ? 0.0F : estimated_reciprocal(length_squared);
    const float inverse_length = point ? 0.0F : estimated_inverse_root(length_squared);
    // At each column's centre: how far along the segment (0 at its start, 1
    // at its end) and across it (the signed distance from its line), each a
    // row's value at column 0 and a step a column, the row's value a step a
    // row from the last. A row's pixels come in runs: the columns between the
    // ends' perpendiculars (0 < along < 1) lit by their distance from the
    // line, the ones before and after by their distance from the nearer end;
    // whole inside, by the edge's coverage on it. The runs' columns are
    // worked out once a row: the body's from the column the line crosses and
    // the columns a unit of along and across takes, the caps' from their
    // chords. Every value a row needs is a register's: read through the shape
    // and the strip, the compiler reloaded them after each byte stored.
    const float along_step = delta_x * inverse_squared;
    const float across_step = -delta_y * inverse_length;
    const bool along_moves = std::abs(along_step) >= 1e-6F;
    const bool across_moves = std::abs(across_step) >= 1e-6F;
    const float per_along = along_moves ? estimated_reciprocal(along_step) : 0.0F;
    const float per_across = across_moves ? estimated_reciprocal(across_step) : 0.0F;
    const float along_rows = delta_y * inverse_squared;
    const float across_rows = delta_x * inverse_length;
    // The columns either side of the line within reach, and inside the edge.
    const float reach_columns = reach * std::abs(per_across);
    const float inner_columns = inner * std::abs(per_across);
    const float inner_squared = inner * inner;
    const float reach_squared = reach * reach;
    // A cap's edge pixel lies within half a pixel of `rim_middle` from its
    // end: one Newton step from there finds its distance to ~0.03 of a pixel
    // for the halo's strokes (the root took ~100 cycles a pixel); thin ones
    // take the root.
    const float rim_middle = reach - 0.5F;
    const bool rim_estimated = rim_middle >= 4.0F;
    const float rim_scale = rim_estimated ? 0.5F * estimated_reciprocal(rim_middle) : 0.0F;
    // The columns of each row within reach of the line, as LineBand finds them.
    const bool narrow = std::abs(delta_y) >= 1e-3F;
    const float slope = narrow ? delta_x * estimated_reciprocal(delta_y) : 0.0F;
    const float half_span = narrow ? reach * estimated_root(1.0F + slope * slope) : 0.0F;
    // The row being drawn.
    std::uint8_t* pixels = canvas;
    const auto solid = [&](int from, int to) SYNAPSE_INLINE {
        auto* pixel = pixels + from * 3;
        for (int column = from; column <= to; ++column, pixel += 3) {
            pixel[0] = static_cast<std::uint8_t>(std::min(255, pixel[0] + solid_blue));
            pixel[1] = static_cast<std::uint8_t>(std::min(255, pixel[1] + solid_green));
            pixel[2] = static_cast<std::uint8_t>(std::min(255, pixel[2] + solid_red));
        }
    };
    const auto edge = [&](int column, float distance) SYNAPSE_INLINE {
        if (distance >= reach) return;
        add_scaled(pixels + column * 3, edge_red, edge_green, edge_blue, in_256ths(std::min(1.0F, reach - distance)));
    };
    // A run's columns lit whole, none past its last when there are none: the
    // run's edge is the rest of it, before them and after.
    const auto inside = [](int lit_last, int& solid_first, int& solid_last) SYNAPSE_INLINE {
        if (solid_first > solid_last) {
            solid_first = lit_last + 1;
            solid_last = lit_last;
        }
    };
    // A round cap's columns among [from, to]: its end at `centre_x`, the row
    // `vertical` away from it (squared).
    const auto cap = [&](int from, int to, float centre_x, float vertical) SYNAPSE_INLINE {
        if (from > to) return;
        const float lit_squared = reach_squared - vertical;
        if (!(lit_squared > 0.0F)) return;
        const float lit_half = estimated_root(lit_squared);
        const float middle = centre_x - 0.5F;
        const int lit_first = std::max(from, floor_column(middle - lit_half) + 1);
        const int lit_last = std::min(to, ceil_column(middle + lit_half) - 1);
        if (lit_first > lit_last) return;
        int solid_first = lit_last + 1;
        int solid_last = lit_last;
        const float solid_squared = inner_squared - vertical;
        if (solid_squared >= 0.0F) {
            const float solid_half = estimated_root(solid_squared);
            solid_first = std::max(lit_first, ceil_column(middle - solid_half));
            solid_last = std::min(lit_last, floor_column(middle + solid_half));
        }
        inside(lit_last, solid_first, solid_last);
        const auto rim = [&](int rim_first, int rim_last) SYNAPSE_INLINE {
            for (int column = rim_first; column <= rim_last; ++column) {
                const float offset = static_cast<float>(column) - middle;
                const float squared = offset * offset + vertical;
                edge(column, rim_estimated ? squared * rim_scale + rim_middle * 0.5F : estimated_root(squared));
            }
        };
        rim(lit_first, solid_first - 1);
        solid(solid_first, solid_last);
        rim(solid_last + 1, lit_last);
    };
    const bool start_left = point || (along_moves && along_step > 0.0F);
    // Each row's values from its own offset, not the last row's: a strip's
    // rows are exactly the whole frame's.
    const float along_base = (0.5F - x0) * along_step;
    const float across_base = -(0.5F - x0) * delta_y * inverse_length;
    unsigned work = 0;
    for (int y = top; y <= bottom; ++y) {
        const float offset_y = static_cast<float>(y) + 0.5F - y0;
        const float end_y = static_cast<float>(y) + 0.5F - y1;
        const float along_at = along_base + offset_y * along_rows;
        const float across_at = across_base + offset_y * across_rows;
        int first = left;
        int last = right;
        if (narrow) {
            const float centre = x0 + offset_y * slope;
            first = std::max(first, floor_int(centre - half_span));
            last = std::min(last, ceil_int(centre + half_span));
        }
        if (first > last) continue;
        work += static_cast<unsigned>(last - first + 1);
        pixels = canvas + (y - canvas_row) * width * 3;
        // The columns between the ends' perpendiculars.
        int body_first = last + 1;
        int body_last = last;
        if (point) {
            // A dot: all of it is the start's cap.
        } else if (along_moves) {
            const float at_start = -along_at * per_along;
            const float at_end = at_start + per_along;
            body_first = std::max(first, floor_column(per_along > 0.0F ? at_start : at_end) + 1);
            body_last = std::min(last, ceil_column(per_along > 0.0F ? at_end : at_start) - 1);
        } else if (along_at > 0.0F && along_at < 1.0F) {
            body_first = first;
            body_last = last;
        }
        if (body_first <= body_last) {
            if (across_moves) {
                // The column the line crosses the row at, and those either side.
                const float crossing = -across_at * per_across;
                const int lit_first = std::max(body_first, floor_column(crossing - reach_columns) + 1);
                const int lit_last = std::min(body_last, ceil_column(crossing + reach_columns) - 1);
                if (lit_first <= lit_last) {
                    int solid_first = std::max(lit_first, ceil_column(crossing - inner_columns));
                    int solid_last = std::min(lit_last, floor_column(crossing + inner_columns));
                    inside(lit_last, solid_first, solid_last);
                    const auto rim = [&](int rim_first, int rim_last) SYNAPSE_INLINE {
                        for (int column = rim_first; column <= rim_last; ++column) {
                            edge(column, std::abs(across_at + static_cast<float>(column) * across_step));
                        }
                    };
                    rim(lit_first, solid_first - 1);
                    solid(solid_first, solid_last);
                    rim(solid_last + 1, lit_last);
                }
            } else {
                // Level with the line's direction: the same distance all along.
                const float distance = std::abs(across_at);
                if (distance <= inner) {
                    solid(body_first, body_last);
                } else {
                    for (int column = body_first; column <= body_last; ++column) edge(column, distance);
                }
            }
        }
        // The caps, on the sides the ends lie: each drawn by one copy of the
        // cap's code (four copies took ~6 KiB of the internal RAM).
        const int left_last = std::min(last, body_first - 1);
        const int right_first = std::max(std::max(first, body_last + 1), left_last + 1);
        const bool start_on_left = start_left || (!along_moves && along_at <= 0.0F);
        const float start_vertical = offset_y * offset_y;
        const float end_vertical = end_y * end_y;
        cap(first, left_last, start_on_left ? x0 : x1, start_on_left ? start_vertical : end_vertical);
        cap(right_first, last, start_on_left ? x1 : x0, start_on_left ? end_vertical : start_vertical);
    }
    strip.work += work;
}

void SYNAPSE_FAST AuraMercuryView::device_glow(const Shape& shape, Strip& strip) const noexcept {
    const float x = shape.x0;
    const float y = shape.y0;
    const float radius = shape.size;
    std::uint8_t* const canvas = strip.light;
    const int canvas_row = strip.first_row;
    const int left = std::max(0, floor_int(x - radius));
    const int right = std::min(width - 1, ceil_int(x + radius));
    const int top = std::max(strip.top, floor_int(y - radius));
    const int bottom = std::min(strip.bottom - 1, ceil_int(y + radius));
    if (left > right || top > bottom) return;
    const int red = in_256ths(shape.light.red);
    const int green = in_256ths(shape.light.green);
    const int blue = in_256ths(shape.light.blue);
    const float limit = radius * radius;
    const float inverse_radius = 1.0F / radius;
    std::uint64_t work = 0;
    for (int row = top; row <= bottom; ++row) {
        const float offset_y = static_cast<float>(row) + 0.5F - y;
        const float row_limit = limit - offset_y * offset_y;
        if (row_limit <= 0.0F) continue;
        // The row's pixels whose centres lie inside the circle.
        const float half = estimated_root(row_limit);
        const int first = std::max(left, floor_int(x - half - 0.5F));
        const int last = std::min(right, ceil_int(x + half - 0.5F));
        if (first > last) continue;
        work += static_cast<std::uint64_t>(last - first + 1);
        auto* pixel = canvas + (static_cast<std::size_t>(row - canvas_row) * width + static_cast<std::size_t>(first)) * 3;
        for (int column = first; column <= last; ++column, pixel += 3) {
            const float offset_x = static_cast<float>(column) + 0.5F - x;
            const float distance_squared = offset_x * offset_x + offset_y * offset_y;
            if (distance_squared >= limit) continue;
            add_scaled(pixel, red, green, blue, in_256ths(1.0F - estimated_root(distance_squared) * inverse_radius));
        }
    }
    strip.work += work;
}

void SYNAPSE_FAST AuraMercuryView::device_centred(const Shape& shape, Strip& strip) const noexcept {
    // The frame's centre lies on a pixel corner, so the four quadrants'
    // pixels mirror one another: each row's falloff is worked out once,
    // outwards from the centre, for both halves.
    constexpr int centre_x = width / 2;
    constexpr int centre_y = height / 2;
    constexpr int kMaxSpan = 128;
    const float radius = shape.size;
    const int span = std::min(kMaxSpan, ceil_int(radius) + 1);
    const float limit = radius * radius;
    const float inverse_radius = 1.0F / radius;
    const int red = in_256ths(shape.light.red);
    const int green = in_256ths(shape.light.green);
    const int blue = in_256ths(shape.light.blue);
    std::uint8_t* const canvas = strip.light;
    const int canvas_row = strip.first_row;
    const int top = std::max(strip.top, floor_int(static_cast<float>(centre_y) - radius));
    const int bottom = std::min(strip.bottom - 1, ceil_int(static_cast<float>(centre_y) + radius));
    // The reactor's own glows come from their tables; any other radius is
    // worked out row by row.
    int table_span = 0;
    const std::uint8_t* table = reactor_table(radius, table_span);
    std::array<std::uint16_t, kMaxSpan> falloff{};
    unsigned work = 0;
    for (int row = top; row <= bottom; ++row) {
        const int quadrant_row = row >= centre_y ? row - centre_y : centre_y - 1 - row;
        if (quadrant_row >= span) continue;
        int reach = 0;
        const std::uint8_t* row_table = nullptr;
        if (table) {
            row_table = table + static_cast<std::size_t>(quadrant_row) * table_span;
            reach = table[static_cast<std::size_t>(table_span) * table_span + quadrant_row];
        } else {
            const float offset_y = static_cast<float>(quadrant_row) + 0.5F;
            for (; reach < span; ++reach) {
                const float offset_x = static_cast<float>(reach) + 0.5F;
                const float distance_squared = offset_x * offset_x + offset_y * offset_y;
                if (distance_squared >= limit) break;
                falloff[static_cast<std::size_t>(reach)] =
                    static_cast<std::uint16_t>(in_256ths(1.0F - estimated_root(distance_squared) * inverse_radius));
            }
        }
        if (reach == 0) continue;
        const int first = std::max(0, centre_x - reach);
        const int last = std::min(width - 1, centre_x + reach - 1);
        work += static_cast<unsigned>(last - first + 1);
        std::uint8_t* const row_pixels = canvas + (row - canvas_row) * width * 3;
        // The row's two halves, each outwards from the centre, a column of
        // the falloff a step: one test and one pointer a pixel (choosing the
        // half and the table each pixel, the board kept the pixel's bytes on
        // the stack).
        const auto light_row = [&](const auto* coverage) SYNAPSE_INLINE {
            std::uint8_t* pixel = row_pixels + centre_x * 3;
            for (int step = 0; step <= last - centre_x; ++step, pixel += 3) {
                const int amount = coverage[step];
                if (amount > 0) add_scaled(pixel, red, green, blue, amount);
            }
            pixel = row_pixels + (centre_x - 1) * 3;
            for (int step = 0; step <= centre_x - 1 - first; ++step, pixel -= 3) {
                const int amount = coverage[step];
                if (amount > 0) add_scaled(pixel, red, green, blue, amount);
            }
        };
        if (row_table) {
            light_row(row_table);
        } else {
            light_row(falloff.data());
        }
    }
    strip.work += work;
}

const std::uint8_t* AuraMercuryView::reactor_table(float radius, int& span) const noexcept {
    if (!reactor_) return nullptr;
    if (radius == 112.0F) {
        span = kOuterSpan;
        return reactor_.get();
    }
    if (radius == 58.0F) {
        span = kInnerSpan;
        return reactor_.get() + static_cast<std::size_t>(kOuterSpan) * (kOuterSpan + 1);
    }
    return nullptr;
}

void SYNAPSE_FAST AuraMercuryView::device_nucleus(const Shape& shape, Strip& strip) const noexcept {
    // The presentation's nucleus (nucleus()), its divisions done once.
    struct Stop {
        float at;
        float red;
        float green;
        float blue;
        float alpha;
    };
    constexpr Stop stops[] = {{0.0F, 245.0F, 255.0F, 255.0F, 1.0F},
                              {0.15F, 184.0F, 255.0F, 241.0F, 0.98F},
                              {0.43F, 82.0F, 218.0F, 216.0F, 0.84F},
                              {0.72F, 42.0F, 27.0F, 94.0F, 0.94F},
                              {1.0F, 4.0F, 2.0F, 13.0F, 1.0F}};
    const float sphere = shape.size;
    const float reach = sphere + 0.5F;
    const float inverse_sphere = 1.0F / (sphere - 1.0F);
    const float inverse_spans[] = {1.0F / (stops[1].at - stops[0].at), 1.0F / (stops[2].at - stops[1].at),
                                   1.0F / (stops[3].at - stops[2].at), 1.0F / (stops[4].at - stops[3].at)};
    std::uint8_t* const canvas = strip.light;
    const int canvas_row = strip.first_row;
    const int left = floor_int(kCentreX - reach);
    const int right = ceil_int(kCentreX + reach);
    const int top = std::max(strip.top, floor_int(kCentreY - reach));
    const int bottom = std::min(strip.bottom - 1, ceil_int(kCentreY + reach));
    for (int row = top; row <= bottom; ++row) {
        const float offset_y = static_cast<float>(row) + 0.5F - kCentreY;
        auto* pixels = canvas + static_cast<std::size_t>(row - canvas_row) * width * 3;
        for (int column = left; column <= right; ++column) {
            const float offset_x = static_cast<float>(column) + 0.5F - kCentreX;
            const float distance = estimated_root(offset_x * offset_x + offset_y * offset_y);
            const float coverage = clamp01(reach - distance);
            if (coverage <= 0.0F) continue;
            const float position = clamp01((distance - 1.0F) * inverse_sphere);
            std::size_t stop = 0;
            while (stop + 2 < std::size(stops) && position > stops[stop + 1].at) ++stop;
            const Stop& from = stops[stop];
            const Stop& to = stops[stop + 1];
            const float share = clamp01((position - from.at) * inverse_spans[stop]);
            const float alpha = mix(from.alpha, to.alpha, share);
            add_to(pixels + static_cast<std::size_t>(column) * 3,
                   {mix(from.red, to.red, share) * alpha, mix(from.green, to.green, share) * alpha,
                    mix(from.blue, to.blue, share) * alpha},
                   coverage);
        }
    }
}

void SYNAPSE_FAST AuraMercuryView::replay(const Shape& shape, Strip& strip) const noexcept {
    switch (shape.kind) {
        case ShapeKind::Stroke: device_stroke(shape, strip); break;
        case ShapeKind::Glow: device_glow(shape, strip); break;
        case ShapeKind::CentredGlow: device_centred(shape, strip); break;
        case ShapeKind::Nucleus: device_nucleus(shape, strip); break;
        case ShapeKind::Disc: device_disc(shape, strip); break;
    }
}

std::uint8_t* AuraMercuryView::strip_light(SynapseOutput* output, int index, std::size_t bytes) {
    if (output) {
        if (void* memory = output->strip(index, bytes)) return static_cast<std::uint8_t*>(memory);
    }
    if (!strip_memory_ || strip_memory_bytes_ != bytes) {
        strip_memory_ = std::make_unique<std::uint8_t[]>(4 * bytes);
        strip_memory_bytes_ = bytes;
    }
    return strip_memory_.get() + static_cast<std::size_t>(index) * bytes;
}

void AuraMercuryView::set_strip_height(int rows) {
    if (rows < strip_rows || height % rows != 0) return;
    if (drawing_) worker_->wait_idle();
    strip_height_ = rows;
}

void SYNAPSE_FAST AuraMercuryView::finish_strip(int top, int rows, std::uint8_t* light,
                                                bool for_panel) const noexcept {
    // The light is final (the strip started from the background, its shapes
    // came dimmed): packed to RGB565 with the ordered dither, four pixels at
    // a time, into the frame shown (in PSRAM) and, for the panel, high byte
    // first into the strip's own memory: over the start of its light, each
    // pixel's light read before its two bytes are written there.
    static_assert(width % 4 == 0, "a row packs four pixels at a time");
    const std::uint8_t* source = light;
    auto* shown = reinterpret_cast<std::uint32_t*>(canvas_ + static_cast<std::size_t>(top) * width * 2U);
    auto* panel = reinterpret_cast<std::uint32_t*>(light);
    const auto swapped = [](std::uint32_t pair) SYNAPSE_INLINE {
        return ((pair & 0x00FF00FFU) << 8) | ((pair >> 8) & 0x00FF00FFU);
    };
    for (int y = top; y < top + rows; ++y) {
        // pack565()'s thresholds for the row's four columns, red's and blue's
        // and green's.
        const std::uint8_t* thresholds = kBayer[y & 3];
        const int red_blue[4] = {thresholds[0] >> 1, thresholds[1] >> 1, thresholds[2] >> 1, thresholds[3] >> 1};
        const int greens[4] = {thresholds[0] >> 2, thresholds[1] >> 2, thresholds[2] >> 2, thresholds[3] >> 2};
        const auto pack = [&](const std::uint8_t* pixel, int column) SYNAPSE_INLINE {
            const int red5 = std::min(31, (pixel[2] + red_blue[column]) >> 3);
            const int green6 = std::min(63, (pixel[1] + greens[column]) >> 2);
            const int blue5 = std::min(31, (pixel[0] + red_blue[column]) >> 3);
            return static_cast<std::uint32_t>((red5 << 11) | (green6 << 5) | blue5);
        };
        for (int x = 0; x < width; x += 4, source += 12) {
            const std::uint32_t first = pack(source, 0) | (pack(source + 3, 1) << 16);
            const std::uint32_t second = pack(source + 6, 2) | (pack(source + 9, 3) << 16);
            shown[0] = first;
            shown[1] = second;
            shown += 2;
            if (for_panel) {
                panel[0] = swapped(first);
                panel[1] = swapped(second);
                panel += 2;
            }
        }
    }
}

// ---- Direct output -----------------------------------------------------------

void AuraMercuryView::prepare_direct() noexcept {
    frame_direct_ = false;
    frame_obstructed_ = false;
    if (profile_ != SynapseProfile::Device || !display_ || !image_ || synapse_output() == nullptr) return;
    // Where everything lies now, not at LVGL's last refresh: an object just
    // made or moved, laid out, invalidates its place (pending_overlap_).
    lv_obj_update_layout(image_);
    lv_area_t area{};
    lv_obj_get_coords(image_, &area);
    const lv_area_t rows{area.x1, area.y1 + direct_top, area.x2, area.y1 + direct_bottom - 1};
    if (rows.x1 != direct_area_.x1 || rows.y1 != direct_area_.y1 || rows.x2 != direct_area_.x2 ||
        rows.y2 != direct_area_.y2) {
        // Shown somewhere new: LVGL draws the rows there first.
        direct_area_ = rows;
        pending_overlap_ = true;
        return;
    }
    frame_direct_ = !pending_overlap_ && unobstructed();
}

bool AuraMercuryView::unobstructed() const noexcept {
    auto* display = lv_obj_get_display(image_);
    if (display != display_ || lv_obj_get_screen(image_) != lv_display_get_screen_active(display) ||
        lv_display_get_screen_prev(display) != nullptr) {
        return false;
    }
    // Inside the screen, in panel windows that start on even coordinates.
    if (direct_area_.x1 < 0 || direct_area_.y1 < 0 ||
        direct_area_.x2 >= lv_display_get_horizontal_resolution(display) ||
        direct_area_.y2 >= lv_display_get_vertical_resolution(display) || (direct_area_.x1 & 1) != 0 ||
        (direct_area_.y1 & 1) != 0 || lv_area_get_width(&direct_area_) != width) {
        return false;
    }
    // Shown as drawn: opaque, untransformed, and inside each of its parents,
    // whose scrollbars and outlines would be drawn over it.
    for (lv_obj_t* object = image_; object; object = lv_obj_get_parent(object)) {
        if (lv_obj_is_hidden(object) || lv_obj_get_style_opa(object, LV_PART_MAIN) < LV_OPA_MAX ||
            lv_obj_get_style_transform_scale_x(object, LV_PART_MAIN) != LV_SCALE_NONE ||
            lv_obj_get_style_transform_scale_y(object, LV_PART_MAIN) != LV_SCALE_NONE ||
            lv_obj_get_style_transform_rotation(object, LV_PART_MAIN) != 0 ||
            lv_obj_get_style_transform_skew_x(object, LV_PART_MAIN) != 0 ||
            lv_obj_get_style_transform_skew_y(object, LV_PART_MAIN) != 0 ||
            lv_obj_get_style_blend_mode(object, LV_PART_MAIN) != LV_BLEND_MODE_NORMAL) {
            return false;
        }
        lv_area_t coords{};
        lv_obj_get_coords(object, &coords);
        if (!lv_area_is_in(&direct_area_, &coords, 0)) return false;
        if (object == image_) continue;
        lv_area_t horizontal{};
        lv_area_t vertical{};
        lv_obj_get_scrollbar_area(object, &horizontal, &vertical);
        lv_area_t common{};
        if (lv_area_intersect(&common, &horizontal, &direct_area_) ||
            lv_area_intersect(&common, &vertical, &direct_area_) ||
            (lv_obj_get_style_outline_width(object, LV_PART_MAIN) > 0 &&
             lv_obj_get_style_outline_opa(object, LV_PART_MAIN) > LV_OPA_MIN)) {
            return false;
        }
    }
    // Nothing drawn after it may draw there: its later siblings, its parents'
    // later siblings, and the layers above the screen.
    for (const lv_obj_t* object = image_; const lv_obj_t* parent = lv_obj_get_parent(object); object = parent) {
        const auto count = static_cast<std::int32_t>(lv_obj_get_child_count(parent));
        for (std::int32_t index = lv_obj_get_index(object) + 1; index < count; ++index) {
            if (draws_over(lv_obj_get_child(parent, index), direct_area_)) return false;
        }
    }
    return !draws_over(lv_display_get_layer_top(display), direct_area_) &&
           !draws_over(lv_display_get_layer_sys(display), direct_area_);
}

bool AuraMercuryView::send_strip(SynapseOutput& output, int top, const std::uint8_t* pixels) const noexcept {
    if (!output.lock(kLockWaitMs)) return false;
    // LVGL has invalidated the rows since the frame began: it draws them now,
    // and the whole frame goes to it.
    const bool sent = !frame_obstructed_ &&
                      output.send(direct_area_.x1, direct_area_.y1 - direct_top + top, width, strip_rows, pixels);
    output.unlock();
    return sent;
}

std::uint32_t AuraMercuryView::frame_signature() const noexcept {
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0; index < buffer_.size(); ++index) {
        hash ^= shown_[index];
        hash *= 16777619U;
    }
    return hash;
}

}  // namespace aura::ui

#include "aura/aura_mercury_view.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

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

float fract(float value) { return value - std::floor(value); }
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
    return {std::floor(mix(stops[index].red, stops[index + 1].red, share)),
            std::floor(mix(stops[index].green, stops[index + 1].green, share)),
            std::floor(mix(stops[index].blue, stops[index + 1].blue, share))};
}

// The columns of row `row` a stroke can touch: inside the box around the
// segment and within `reach` of its line, so a long diagonal line visits a
// narrow band of each row instead of its whole bounding box.
void narrow_to_line(float from_x, float from_y, float delta_x, float delta_y, float reach, int row, int& left,
                    int& right) {
    if (std::abs(delta_y) < 1e-3F) return;
    const float slope = delta_x / delta_y;
    const float centre = from_x + (static_cast<float>(row) + 0.5F - from_y) * slope;
    const float half_span = reach * std::sqrt(1.0F + slope * slope);
    left = std::max(left, static_cast<int>(std::floor(centre - half_span)));
    right = std::min(right, static_cast<int>(std::ceil(centre + half_span)));
}

}  // namespace

AuraMercuryView::AuraMercuryView(Clock clock, FrameWorker* worker) : clock_(clock), worker_(worker) {
    if (worker_) second_ = std::make_unique<Frame>();
    canvas_ = buffer_.data();
    shown_ = buffer_.data();
    descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
#if LV_COLOR_DEPTH == 16
    // A 16-bit display shows RGB565 anyway: pack the light in place, dithered.
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor_.header.stride = width * 2;
    descriptor_.data_size = width * height * 2;
#else
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB888;
    descriptor_.header.stride = width * 3;
    descriptor_.data_size = width * height * 3;
#endif
    descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    descriptor_.header.w = width;
    descriptor_.header.h = height;
    descriptor_.data = shown_;
    // The vignette: a black radial veil from radius 120 (clear) to 330 (82 %).
    for (std::size_t index = 0; index < vignette_.size(); ++index) {
        const float distance = std::sqrt(static_cast<float>(index << kVignetteShift));
        const float veil = 0.82F * clamp01((distance - 120.0F) / 210.0F);
        vignette_[index] = static_cast<std::uint16_t>((1.0F - veil) * 256.0F + 0.5F);
    }
}

AuraMercuryView::~AuraMercuryView() {
    if (drawing_) worker_->wait_idle();
}

lv_obj_t* AuraMercuryView::attach(lv_obj_t* parent, int center_x, int center_y) {
    // A frame still on the worker belongs to the previous attachment.
    if (drawing_) {
        worker_->wait_idle();
        drawing_ = false;
    }
    image_ = lv_image_create(parent);
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
    // The first frame is drawn at once, so the presence is never blank, and the
    // first tick draws the next one.
    wait_ms_ = frame_interval_ms;
    idle_ms_ = frame_interval_ms;
    debt_ms_ = 0;
    animation_ms_ = 0;
    frame_count_ = 0;
    advance(0.0F);
    canvas_ = shown_;
    frame_quality_ = quality_;
    render();
    ++frame_count_;
    return image_;
}

void AuraMercuryView::detach() noexcept { image_ = nullptr; }

void AuraMercuryView::set_state(MercuryState state) noexcept { state_ = state; }
void AuraMercuryView::set_level(float level) noexcept { level_ = std::clamp(level, 0.0F, 1.0F); }
void AuraMercuryView::set_reduced_motion(bool reduced) noexcept { reduced_motion_ = reduced; }

std::uint32_t AuraMercuryView::paced_wait(std::uint32_t render_ms) noexcept {
    // An extreme stall counts as one second, so Synapse still moves every 3 s.
    const std::uint32_t drawing = std::min<std::uint32_t>(render_ms, 1000U);
    const std::uint32_t rest = drawing < frame_interval_ms ? frame_interval_ms - drawing : 0U;
    return std::max(rest, drawing * 2U);
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
        canvas_ = shown_ == buffer_.data() ? second_->data() : buffer_.data();
        drawing_ = worker_->submit(&AuraMercuryView::draw_on_worker, this);
        if (drawing_) return false;
    }
    const std::uint32_t render_ms = draw_into(shown_);
    debt_ms_ = render_ms;
    show(shown_, render_ms, false);
    return true;
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
    if (image_) lv_obj_invalidate(image_);
    ++frame_count_;
    last_frame_ms_ = render_ms;
    // A device that needs longer per frame draws fewer, lighter ones, so drawing
    // keeps to a third of the time: idle time carried over never cuts into the
    // twice-the-drawing a frame asks for, and a worker's frame starts it afresh.
    wait_ms_ = paced_wait(render_ms);
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

void AuraMercuryView::add(int x, int y, const Light& light, float coverage) noexcept {
    auto* pixel = canvas_ + (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 3;
    const auto put = [coverage](std::uint8_t& channel, float value) {
        const int sum = static_cast<int>(channel) + static_cast<int>(value * coverage + 0.5F);
        channel = static_cast<std::uint8_t>(std::min(255, sum));
    };
    put(pixel[0], light.blue);
    put(pixel[1], light.green);
    put(pixel[2], light.red);
}

// One straight stroke with round caps. Lines thinner than a pixel light a
// one-pixel line proportionally less, as the browser's hairlines do.
void AuraMercuryView::stroke(const Point& from, const Point& to, float line_width, const Light& light) noexcept {
    const float half = std::max(line_width, 1.0F) * 0.5F;
    const float strength = std::min(line_width, 1.0F);
    const float reach = half + 0.5F;
    const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x) - reach)));
    const int right = std::min(width - 1, static_cast<int>(std::ceil(std::max(from.x, to.x) + reach)));
    const int top = std::max(0, static_cast<int>(std::floor(std::min(from.y, to.y) - reach)));
    const int bottom = std::min(height - 1, static_cast<int>(std::ceil(std::max(from.y, to.y) + reach)));
    if (left > right || top > bottom) return;
    const float delta_x = to.x - from.x;
    const float delta_y = to.y - from.y;
    const float length_squared = delta_x * delta_x + delta_y * delta_y;
    const float inverse = length_squared > 1e-6F ? 1.0F / length_squared : 0.0F;
    const float outer = reach * reach;
    const float inner = half > 0.5F ? (half - 0.5F) * (half - 0.5F) : -1.0F;
    // Pixels the stroke covers completely all receive the same light.
    const Light solid{light.red * strength, light.green * strength, light.blue * strength};
    const int solid_blue = static_cast<int>(solid.blue + 0.5F);
    const int solid_green = static_cast<int>(solid.green + 0.5F);
    const int solid_red = static_cast<int>(solid.red + 0.5F);
    for (int y = top; y <= bottom; ++y) {
        const float offset_y = static_cast<float>(y) + 0.5F - from.y;
        int first = left;
        int last = right;
        narrow_to_line(from.x, from.y, delta_x, delta_y, reach, y, first, last);
        frame_work_ += static_cast<std::uint64_t>(last >= first ? last - first + 1 : 0);
        auto* pixel = canvas_ + (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(first)) * 3;
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
                add(x, y, solid, clamp01(reach - std::sqrt(distance_squared)));
            }
        }
    }
}

// One stroke along a polyline: a single path, so its joints light once (the
// lattice's separate steps, drawn with stroke(), brighten theirs). Round caps.
void AuraMercuryView::stroke_path(const Point* points, int count, float line_width, const Light& light) noexcept {
    if (count < 2) return;
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
        const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x) - reach)));
        const int right = std::min(width - 1, static_cast<int>(std::ceil(std::max(from.x, to.x) + reach)));
        const int top = std::max(0, static_cast<int>(std::floor(std::min(from.y, to.y) - reach)));
        const int bottom = std::min(height - 1, static_cast<int>(std::ceil(std::max(from.y, to.y) + reach)));
        if (left > right || top > bottom) continue;
        const float delta_x = to.x - from.x;
        const float delta_y = to.y - from.y;
        const float length_squared = delta_x * delta_x + delta_y * delta_y;
        const float inverse = length_squared > 1e-6F ? 1.0F / length_squared : 0.0F;
        for (int y = top; y <= bottom; ++y) {
            const float offset_y = static_cast<float>(y) + 0.5F - from.y;
            int start = left;
            int stop = right;
            narrow_to_line(from.x, from.y, delta_x, delta_y, reach, y, start, stop);
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
    float travelled = 0.0F;
    while (travelled < length) {
        const float remaining = dashes[static_cast<std::size_t>(element)] - position;
        const float stop = std::min(length, travelled + remaining);
        if ((element & 1) == 0) {
            // The stroke's width across the scaled circle at the dash's middle.
            const float middle = start + (travelled + stop) * 0.5F / radius;
            const float tangent = std::hypot(std::sin(middle), scale_y * std::cos(middle));
            const float shown = line_width * scale_y / std::max(tangent, 1e-3F);
            const int pieces = std::clamp(static_cast<int>(std::ceil((stop - travelled) / 3.0F)), 1,
                                          static_cast<int>(points.size()) - 1);
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
    const int left = std::max(0, static_cast<int>(std::floor(x - radius)));
    const int right = std::min(width - 1, static_cast<int>(std::ceil(x + radius)));
    const int top = std::max(0, static_cast<int>(std::floor(y - radius)));
    const int bottom = std::min(height - 1, static_cast<int>(std::ceil(y + radius)));
    const Light light{static_cast<float>(red) * alpha, static_cast<float>(green) * alpha,
                      static_cast<float>(blue) * alpha};
    const float limit = radius * radius;
    if (right >= left && bottom >= top) {
        frame_work_ += static_cast<std::uint64_t>(right - left + 1) * static_cast<std::uint64_t>(bottom - top + 1);
    }
    for (int row = top; row <= bottom; ++row) {
        const float offset_y = static_cast<float>(row) + 0.5F - y;
        for (int column = left; column <= right; ++column) {
            const float offset_x = static_cast<float>(column) + 0.5F - x;
            const float distance_squared = offset_x * offset_x + offset_y * offset_y;
            if (distance_squared >= limit) continue;
            add(column, row, light, 1.0F - std::sqrt(distance_squared) / radius);
        }
    }
}

void AuraMercuryView::fill_circle(float x, float y, float radius, const Light& light) noexcept {
    const float reach = radius + 0.5F;
    const int left = std::max(0, static_cast<int>(std::floor(x - reach)));
    const int right = std::min(width - 1, static_cast<int>(std::ceil(x + reach)));
    const int top = std::max(0, static_cast<int>(std::floor(y - reach)));
    const int bottom = std::min(height - 1, static_cast<int>(std::ceil(y + reach)));
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
    for (int base = 0; base < kPaths / 2; ++base) {
        const auto loop = static_cast<float>(base);
        const float rotate_x = 0.18F + 0.36F * std::sin(tilt + loop * 0.72F) + loop * 0.066F;
        const float rotate_y = turn + loop * 0.49F;
        const float cos_x = std::cos(rotate_x);
        const float sin_x = std::sin(rotate_x);
        const float cos_y = std::cos(rotate_y);
        const float sin_y = std::sin(rotate_y);
        for (int sample = 0; sample <= kSteps; ++sample) {
            const float angle = static_cast<float>(sample) / kSteps * kTau;
            const float voice = drive_ * wave_ * std::sin(angle * 8.0F - ripple + loop * 0.73F);
            const float knot = std::sin(angle * 3.0F + loop * 1.17F + twist);
            const float counter = std::sin(angle * 5.0F - loop * 0.61F - counter_twist);
            const float radius =
                104.0F + loop * 2.15F + knot * (10.0F + weave_ * 8.0F) + counter * 3.5F + voice * 21.0F;
            const float sin_a = std::sin(angle);
            float x = std::cos(angle) * radius * (1.0F + spread_ * 0.15F * sin_a * sin_a);
            float y = sin_a * radius * (0.76F + spread_ * 0.13F);
            const float z = std::sin(angle * 2.0F + loop * 1.03F + lift) * (32.0F + weave_ * 40.0F);
            x += spread_ * 14.0F * std::sin(angle * 2.0F + loop * 0.86F);
            y += voice * 10.0F + spread_ * 8.0F * std::cos(angle * 3.0F - sway);
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
    // joints glow brighter: a soft wide pass, then the bright core pass.
    const float drift = cycle(seconds_ * 0.024);
    const bool reduced = frame_quality_ == SynapseQuality::Reduced;
    for (int pass = 0; pass < 2; ++pass) {
        const int stride = !reduced ? 1 : pass == 0 ? kReducedSoftStride : kReducedBrightStride;
        const float gain = pass == 0 && reduced ? kReducedSoftGain : 1.0F;
        for (int index = 0; index < kPaths; ++index) {
            for (int sample = 0; sample < kSteps; sample += stride) {
                const Point& from = path(index, sample);
                const Point& to = path(index, std::min(sample + stride, kSteps));
                const float front = clamp01(((from.z + to.z) * 0.5F + 100.0F) / 200.0F);
                const Spectral colour = spectral_colour(static_cast<float>(sample) / kSteps * 0.58F +
                                                        static_cast<float>(index) * 0.085F + drift);
                const float alpha = pass == 0 ? (0.022F + front * 0.031F) * gain : clamp01(0.22F + front * 0.76F);
                stroke(from, to, pass == 0 ? 9.0F + front * 5.0F : 0.5F + front * 1.65F,
                       {colour.red * alpha, colour.green * alpha, colour.blue * alpha});
            }
        }
    }
    // The translucent web between neighbouring paths.
    for (int sample = 0; sample < kSteps; sample += 7) {
        for (int index = 0; index + 1 < kPaths; ++index) {
            const Point& from = path(index, sample);
            const Point& to = path(index + 1, sample);
            const float front = clamp01(((from.z + to.z) * 0.5F + 90.0F) / 180.0F);
            const Spectral colour =
                spectral_colour(static_cast<float>(sample) / kSteps * 0.52F + static_cast<float>(index) * 0.095F);
            const float alpha = 0.045F + front * 0.14F;
            stroke(from, to, 0.48F, {colour.red * alpha, colour.green * alpha, colour.blue * alpha});
        }
    }
}

void AuraMercuryView::draw_particles() noexcept {
    // drawParticles(): sparks travelling along the paths with short trails.
    const int count = weights_[2] > 0.5F ? 30 : 18;
    std::array<Point, 11> trail{};
    for (int particle = 0; particle < count; ++particle) {
        const int index = particle % kPaths;
        const float progress = cycle(phase_ * (0.070 + (particle % 5) * 0.009) + particle * 0.113);
        const int sample = std::min(kSteps - 1, static_cast<int>(progress * kSteps));
        const Point& head = path(index, sample);
        const float front = clamp01((head.z + 100.0F) / 200.0F);
        for (int step = 10; step >= 0; --step) {
            trail[static_cast<std::size_t>(10 - step)] = path(index, (sample - step + kSteps) % kSteps);
        }
        const Spectral line = spectral_colour(progress * 0.68F + static_cast<float>(index) * 0.08F);
        const float line_alpha = 0.12F + front * 0.34F;
        stroke_path(trail.data(), static_cast<int>(trail.size()), 0.65F + front * 1.25F,
                    {line.red * line_alpha, line.green * line_alpha, line.blue * line_alpha});
        if ((particle & 1) != 0) {
            radial_glow(head.x, head.y, 9.0F + front * 8.0F, 193, 157, 255, 0.10F + front * 0.16F);
        } else {
            radial_glow(head.x, head.y, 9.0F + front * 8.0F, 133, 255, 229, 0.10F + front * 0.16F);
        }
        const Spectral spark = spectral_colour(progress * 0.65F + static_cast<float>(index) * 0.1F);
        const float spark_alpha = clamp01(0.45F + front * 0.5F);
        fill_circle(head.x, head.y, 1.0F + front * 1.55F,
                    {spark.red * spark_alpha, spark.green * spark_alpha, spark.blue * spark_alpha});
    }
}

void AuraMercuryView::draw_core() noexcept {
    // drawCore(). The presentation's dark lens is filled while the canvas adds
    // light, so it leaves no mark; the reactor is pure light here as well.
    const float aperture = 58.0F + spread_ * 8.0F + drive_ * 9.0F;
    const float sphere = 20.0F + drive_ * 5.0F + spread_ * 1.5F;
    radial_glow(kCentreX, kCentreY, 112.0F, 104, 78, 255, 0.10F + drive_ * 0.07F);
    radial_glow(kCentreX, kCentreY, 58.0F, 85, 247, 222, 0.13F + drive_ * 0.11F);

    for (int ring = 0; ring < 4; ++ring) {
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

    // The nucleus: a radial gradient from radius 1 to the sphere's edge, added.
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
    const float reach = sphere + 0.5F;
    const int left = static_cast<int>(std::floor(kCentreX - reach));
    const int right = static_cast<int>(std::ceil(kCentreX + reach));
    const int top = static_cast<int>(std::floor(kCentreY - reach));
    const int bottom = static_cast<int>(std::ceil(kCentreY + reach));
    for (int row = top; row <= bottom; ++row) {
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
            add(column, row,
                {mix(from.red, to.red, share) * alpha, mix(from.green, to.green, share) * alpha,
                 mix(from.blue, to.blue, share) * alpha},
                coverage);
        }
    }
    // Its bright rim, one stroke around the sphere.
    std::array<Point, 65> rim{};
    for (std::size_t index = 0; index < rim.size(); ++index) {
        const float angle = static_cast<float>(index) / static_cast<float>(rim.size() - 1) * kTau;
        rim[index] = Point{kCentreX + std::cos(angle) * sphere, kCentreY + std::sin(angle) * sphere, 0.0F};
    }
    const Spectral edge = spectral_colour(0.14F + cycle(seconds_ * 0.03));
    stroke_path(rim.data(), static_cast<int>(rim.size()), 1.15F,
                {edge.red * 0.72F, edge.green * 0.72F, edge.blue * 0.72F});
    radial_glow(kCentreX - sphere * 0.24F, kCentreY - sphere * 0.27F, 7.0F, 235, 255, 255, 0.16F);
}

void AuraMercuryView::finish() noexcept {
    // The presentation's background tint (two broad radial glows painted first)
    // and its vignette, then this frame's own fade at its top and bottom edges.
    constexpr int columns = kTintColumns;
    constexpr int rows = kTintRows;
    auto& tint = tint_;
    const float violet_x = kCentreX - 74.0F + std::sin(spin(seconds_ * 0.22)) * 19.0F;
    const float violet_y = kCentreY - 24.0F;
    const float teal_x = kCentreX + 82.0F;
    const float teal_y = kCentreY + 17.0F + std::cos(spin(seconds_ * 0.18)) * 16.0F;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const float x = (static_cast<float>(column) + 0.5F) * kTintCell;
            const float y = (static_cast<float>(row) + 0.5F) * kTintCell;
            const float violet = 0.14F * std::max(0.0F, 1.0F - std::hypot(x - violet_x, y - violet_y) / 205.0F);
            const float teal = 0.095F * std::max(0.0F, 1.0F - std::hypot(x - teal_x, y - teal_y) / 190.0F);
            // Source-over: the teal glow is painted over the violet one.
            auto& cell = tint[static_cast<std::size_t>(row * columns + column)];
            cell[0] = static_cast<std::uint8_t>(104.0F * violet * (1.0F - teal) + 86.0F * teal + 0.5F);
            cell[1] = static_cast<std::uint8_t>(21.0F * violet * (1.0F - teal) + 91.0F * teal + 0.5F);
            cell[2] = static_cast<std::uint8_t>(49.0F * violet * (1.0F - teal) + 10.0F * teal + 0.5F);
        }
    }
    auto* bytes = canvas_;
#if LV_COLOR_DEPTH == 16
    auto* packed = reinterpret_cast<std::uint16_t*>(canvas_);
    // A 4 x 4 ordered dither keeps the faint glows smooth in RGB565.
    constexpr std::uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
#endif
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
#if LV_COLOR_DEPTH == 16
            const int threshold = bayer[y & 3][x & 3];
            const int red5 = std::min(31, (red + (threshold >> 1)) >> 3);
            const int green6 = std::min(63, (green + (threshold >> 2)) >> 2);
            const int blue5 = std::min(31, (blue + (threshold >> 1)) >> 3);
            packed[index] = static_cast<std::uint16_t>((red5 << 11) | (green6 << 5) | blue5);
#else
            bytes[index * 3] = static_cast<std::uint8_t>(blue);
            bytes[index * 3 + 1] = static_cast<std::uint8_t>(green);
            bytes[index * 3 + 2] = static_cast<std::uint8_t>(red);
#endif
        }
    }
}

void AuraMercuryView::render() noexcept {
    frame_work_ = 0;
    std::memset(canvas_, 0, buffer_.size());
    draw_orbits();
    draw_resonance();
    build_paths();
    draw_waves();
    draw_lattice();
    draw_particles();
    draw_core();
    finish();
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

#include "aura/aura_pearl_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace aura::ui {

namespace {
constexpr float kTau = 6.2831853071795864769F;
constexpr float kPi = kTau / 2.0F;
constexpr float kRestRadius = 80.0F;
constexpr int kMaxRingRadius = 205;
constexpr int kGlowTexture = 32;
constexpr int kGlowRadius = 152;  // 1.9 x the pearl
constexpr std::size_t kSphereBytes = static_cast<std::size_t>(AuraPearlView::size) * AuraPearlView::size * 3;

struct Rgb {
    float red;
    float green;
    float blue;
};

constexpr Rgb rgb(int red, int green, int blue) {
    return {static_cast<float>(red) / 255.0F, static_cast<float>(green) / 255.0F, static_cast<float>(blue) / 255.0F};
}

// Per state, in PresenceState order: idle, listening, thinking, speaking.
constexpr std::array<Rgb, 4> kGlow{{rgb(232, 206, 166), rgb(110, 224, 236), rgb(168, 132, 255), rgb(255, 150, 190)}};
constexpr std::array<Rgb, 4> kTint{{rgb(0, 0, 0), rgb(60, 200, 230), rgb(140, 100, 255), rgb(255, 110, 170)}};
constexpr std::uint32_t kComet = 0xC8ACFF;

Rgb mix(const std::array<float, 4>& weights, const std::array<Rgb, 4>& colors) {
    Rgb result{0.0F, 0.0F, 0.0F};
    for (std::size_t index = 0; index < colors.size(); ++index) {
        result.red += colors[index].red * weights[index];
        result.green += colors[index].green * weights[index];
        result.blue += colors[index].blue * weights[index];
    }
    return result;
}

int channel(float value) { return static_cast<int>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F); }

std::uint32_t hex(const Rgb& color) {
    return static_cast<std::uint32_t>(channel(color.red) << 16 | channel(color.green) << 8 | channel(color.blue));
}

std::uint16_t rgb565(const Rgb& color) {
    const auto red = static_cast<unsigned>(channel(color.red));
    const auto green = static_cast<unsigned>(channel(color.green));
    const auto blue = static_cast<unsigned>(channel(color.blue));
    return static_cast<std::uint16_t>(((red & 0xF8U) << 8) | ((green & 0xFCU) << 3) | (blue >> 3));
}

// The body's colour by squared distance from the light (0..1 of 1.2 radii):
// warm white at the light, through cream and taupe, to a dark rim. Bytes,
// not floats: static tables live in the ESP32-S3's internal RAM.
using Shade = std::array<std::uint8_t, 3>;

const std::array<Shade, 256>& body_shades() {
    static const auto shades = [] {
        constexpr std::array<float, 5> positions{0.0F, 0.28F, 0.62F, 0.86F, 1.0F};
        constexpr std::array<Rgb, 5> colors{
            {rgb(255, 250, 242), rgb(239, 228, 212), rgb(203, 187, 165), rgb(127, 110, 90), rgb(46, 38, 31)}};
        std::array<Shade, 256> result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            const float distance = std::sqrt(static_cast<float>(index) / 255.0F);
            std::size_t stop = 1;
            while (stop + 1 < positions.size() && distance > positions[stop]) ++stop;
            const float share = std::clamp((distance - positions[stop - 1]) / (positions[stop] - positions[stop - 1]),
                                           0.0F, 1.0F);
            const auto& from = colors[stop - 1];
            const auto& to = colors[stop];
            result[index] = {static_cast<std::uint8_t>(channel(from.red + (to.red - from.red) * share)),
                             static_cast<std::uint8_t>(channel(from.green + (to.green - from.green) * share)),
                             static_cast<std::uint8_t>(channel(from.blue + (to.blue - from.blue) * share))};
        }
        return result;
    }();
    return shades;
}

// The glow's soft disc (A8, 1 KB, drawn scaled): full to 37 % of its
// radius, then easing out.
const lv_image_dsc_t* glow_texture() {
    static std::array<std::uint8_t, kGlowTexture * kGlowTexture> pixels{};
    static lv_image_dsc_t descriptor{};
    static const bool ready = [] {
        const float half = static_cast<float>(kGlowTexture) / 2.0F;
        for (int y = 0; y < kGlowTexture; ++y) {
            for (int x = 0; x < kGlowTexture; ++x) {
                const float dx = static_cast<float>(x) + 0.5F - half;
                const float dy = static_cast<float>(y) + 0.5F - half;
                const float distance = std::sqrt(dx * dx + dy * dy) / half;
                const float strength = distance <= 0.37F ? 1.0F :
                    (distance >= 1.0F ? 0.0F : std::pow((1.0F - distance) / 0.63F, 1.6F));
                pixels[static_cast<std::size_t>(y * kGlowTexture + x)] =
                    static_cast<std::uint8_t>(strength * 255.0F + 0.5F);
            }
        }
        descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor.header.cf = LV_COLOR_FORMAT_A8;
        descriptor.header.w = kGlowTexture;
        descriptor.header.h = kGlowTexture;
        descriptor.header.stride = kGlowTexture;
        descriptor.data_size = pixels.size();
        descriptor.data = pixels.data();
        return true;
    }();
    (void)ready;
    return &descriptor;
}

int degrees(float radians) {
    const int value = static_cast<int>(std::lround(radians * 180.0F / kPi)) % 360;
    return value < 0 ? value + 360 : value;
}

// The board's angle of a pixel from a ring's centre, in degrees clockwise
// from 3 o'clock (LVGL's arcs): within 0.3 degrees, without the C library.
float device_angle(float dy, float dx) noexcept {
    const float across = std::fabs(dx);
    const float down = std::fabs(dy);
    const float larger = std::max(across, down);
    if (larger <= 0.0F) return 0.0F;
    const float ratio = std::min(across, down) / larger;
    const float square = ratio * ratio;
    float angle = ((-0.0464964749F * square + 0.15931422F) * square - 0.327622764F) * square * ratio + ratio;
    if (down > across) angle = 1.57079637F - angle;
    if (dx < 0.0F) angle = 3.14159274F - angle;
    if (dy < 0.0F) angle = -angle;
    const float result = angle * (180.0F / kPi);
    return result < 0.0F ? result + 360.0F : result;
}
}  // namespace

AuraPearlView::AuraPearlView() : AuraPearlView(default_presence_profile(), nullptr) { shared_worker_ = true; }

AuraPearlView::AuraPearlView(PresenceProfile profile, FrameWorker* worker) : profile_(profile) {
    if (profile_ == PresenceProfile::Device) {
        device_ = std::make_unique<DeviceFrame>(2 * device_reach, 2 * device_reach, &AuraPearlView::draw_device, this,
                                                &AuraPearlView::begin_device);
        device_->set_worker(worker);
        for (auto& row : rows_) row = std::make_unique<std::uint8_t[]>(static_cast<std::size_t>(2 * device_reach) * 3);
    } else {
        buffer_ = std::make_unique<std::uint8_t[]>(kSphereBytes);
        descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor_.header.cf = LV_COLOR_FORMAT_RGB565A8;
        descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
        descriptor_.header.w = size;
        descriptor_.header.h = size;
        descriptor_.header.stride = size * static_cast<int>(sizeof(std::uint16_t));
        descriptor_.data_size = kSphereBytes;
        descriptor_.data = buffer_.get();
    }
    ripple_born_.fill(-1.0F);
}

AuraPearlView::~AuraPearlView() {
    if (device_) device_->stop();
}

std::size_t AuraPearlView::buffer_bytes() const noexcept {
    return device_ ? device_->buffer_bytes() : kSphereBytes;
}

int AuraPearlView::ring_radius() const noexcept {
    if (!device_) return ring_.radius;
    const auto& ring = rings_[kRipples];
    return ring.visible ? ring.reach : 0;
}

lv_obj_t* AuraPearlView::attach(lv_obj_t* parent, int center_x, int center_y) {
    if (device_) return attach_device(parent, center_x, center_y);
    center_x_ = center_x;
    center_y_ = center_y;
    // Behind the screen's content, back to front: the glow, the ripples, the
    // listening ring and the comet.
    std::uint32_t depth = 0;
    glow_ = lv_image_create(parent);
    lv_image_set_src(glow_, glow_texture());
    lv_image_set_scale(glow_, static_cast<std::uint32_t>(2 * kGlowRadius * LV_SCALE_NONE / kGlowTexture));
    lv_obj_set_size(glow_, 2 * kGlowRadius, 2 * kGlowRadius);
    lv_obj_set_pos(glow_, center_x - kGlowRadius, center_y - kGlowRadius);
    lv_obj_set_style_image_recolor_opa(glow_, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(glow_, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(glow_, false);
    lv_obj_set_scrollable(glow_, false);
    lv_obj_move_to_index(glow_, static_cast<std::int32_t>(depth++));
    glow_opacity_ = -1;
    glow_color_ = 0;
    for (auto& ripple : ripples_) ripple = make_ring(parent, false, depth);
    ring_ = make_ring(parent, false, depth);
    for (auto& segment : comet_) segment = make_ring(parent, true, depth);

    image_ = lv_image_create(parent);
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, center_x - size / 2, center_y - size / 2);
    lv_obj_set_clickable(image_, false);
    lv_obj_set_scrollable(image_, false);
    // A new pearl, or a still one, shows the state itself; later screens
    // continue the blend from where the last one left it.
    if (reduced_motion_ || !attached_before_) {
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            weights_[index] = index == static_cast<std::size_t>(state_) ? 1.0F : 0.0F;
        }
    }
    attached_before_ = true;
    accumulator_ms_ = frame_interval_ms;
    frame_count_ = 0;
    advance(0.0F);
    render();
    update_objects();
    return image_;
}

void AuraPearlView::detach() noexcept {
    if (device_) device_->detach();
    image_ = nullptr;
    glow_ = nullptr;
    ring_ = {};
    ripples_.fill({});
    comet_.fill({});
}

void AuraPearlView::set_state(PresenceState state) noexcept { state_ = state; }
void AuraPearlView::set_level(float level) noexcept { level_ = std::clamp(level, 0.0F, 1.0F); }
void AuraPearlView::set_reduced_motion(bool reduced) noexcept { reduced_motion_ = reduced; }

bool AuraPearlView::tick(std::uint32_t elapsed_ms) {
    if (!image_ || reduced_motion_) return false;
    if (device_) {
        const DeviceFrame::Step step = device_->advance(elapsed_ms);
        if (step == DeviceFrame::Step::Wait) return false;
        if (step == DeviceFrame::Step::Shown) return true;
        advance(static_cast<float>(device_->step_ms()) / 1000.0F);
        return start_device_frame() || step == DeviceFrame::Step::ShownStart;
    }
    accumulator_ms_ += elapsed_ms;
    if (accumulator_ms_ < frame_interval_ms) return false;
    const auto step = std::min<std::uint32_t>(accumulator_ms_, 100U);
    accumulator_ms_ = 0;
    advance(static_cast<float>(step) / 1000.0F);
    render();
    lv_obj_invalidate(image_);
    update_objects();
    return true;
}

std::uint32_t AuraPearlView::next_tick_ms() const noexcept {
    if (!device_ || reduced_motion_) return no_tick;
    return device_->next_tick_ms();
}

// Time, the eased blend between states (200 ms) and the pearl's size: it
// breathes when idle, swells with the voice and draws in while thinking.
void AuraPearlView::advance(float seconds) noexcept {
    seconds_ += seconds;
    const float ease = 1.0F - std::exp(-seconds / 0.2F);
    float sum = 0.0F;
    for (std::size_t index = 0; index < weights_.size(); ++index) {
        const float target = index == static_cast<std::size_t>(state_) ? 1.0F : 0.0F;
        weights_[index] += (target - weights_[index]) * ease;
        sum += weights_[index];
    }
    for (auto& weight : weights_) weight /= sum;
    const float breath = std::sin(seconds_ * kTau / 6.5F);
    radius_ = kRestRadius * (1.0F + 0.02F * breath * weights_[0] + 0.05F * level_ * weights_[1] +
                             0.08F * level_ * weights_[3] - 0.04F * weights_[2]);
}

// The sphere's lights for this frame: coloured lights drift slowly inside the
// pearl (screen blend), the state tints it from below and, while thinking,
// two lights circle; the highlight wanders a little at the upper left and
// brightens with the voice.
AuraPearlView::Sphere AuraPearlView::sphere() const noexcept {
    Sphere result;
    const float radius = radius_;
    const auto add = [&](float x, float y, float light_radius, const Rgb& color, float strength) {
        if (strength < 0.004F || result.count >= result.lights.size()) return;
        result.lights[result.count++] = {x, y, 1.0F / (light_radius * light_radius), color.red * strength,
                                         color.green * strength, color.blue * strength};
    };
    const float drift = seconds_ * 0.25F;
    add(std::cos(drift) * radius * 0.5F, std::sin(drift) * radius * 0.45F + radius * 0.2F, radius,
        rgb(150, 176, 255), 0.34F);
    add(std::cos(drift + 2.6F) * radius * 0.55F, std::sin(drift + 2.6F) * radius * 0.45F, radius * 0.85F,
        rgb(255, 170, 192), 0.28F);
    add(std::cos(drift + 4.4F) * radius * 0.5F, std::sin(drift + 4.4F) * radius * 0.5F, radius * 0.7F,
        rgb(170, 255, 226), 0.14F);
    add(0.0F, radius * 0.32F, radius * 1.25F, mix(weights_, kTint), 0.42F * (1.0F - weights_[0]));
    if (weights_[2] > 0.01F) {
        const float turn = seconds_ * 1.7F;
        add(std::cos(turn) * radius * 0.42F, std::sin(turn) * radius * 0.42F, radius * 0.72F, rgb(190, 150, 255),
            0.7F * weights_[2]);
        add(std::cos(turn + kPi) * radius * 0.42F, std::sin(turn + kPi) * radius * 0.42F, radius * 0.6F,
            rgb(255, 236, 214), 0.5F * weights_[2]);
    }
    const float drive = (weights_[1] + weights_[3]) * level_;
    const float highlight_angle = -2.25F + 0.22F * std::sin(seconds_ * 0.45F);
    result.highlight_x = std::cos(highlight_angle) * radius * 0.5F;
    result.highlight_y = std::sin(highlight_angle) * radius * 0.5F;
    result.highlight_inverse = 1.0F / (radius * 0.34F * radius * 0.34F);
    result.highlight_strength = std::min(1.0F, 0.82F + 0.18F * drive);
    result.light_x = -0.2F * radius;
    result.light_y = -0.2F * radius;
    result.body_inverse = 1.0F / (1.44F * radius * radius);
    result.outside = (radius + 1.0F) * (radius + 1.0F);
    result.rim_start = (radius - 1.2F) * (radius - 1.2F);
    result.radius = radius;
    return result;
}

void AuraPearlView::render() {
    const Sphere lit = sphere();
    const auto& lights = lit.lights;
    const std::size_t count = lit.count;
    const float radius = lit.radius;
    const Rgb rim_color = rgb(255, 240, 222);
    const auto& shades = body_shades();

    auto* colors = reinterpret_cast<std::uint16_t*>(buffer_.get());
    auto* alpha = buffer_.get() + size * size * sizeof(std::uint16_t);
    const float center = static_cast<float>(size) / 2.0F;
    for (int y = 0; y < size; ++y) {
        const float dy = static_cast<float>(y) + 0.5F - center;
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x) + 0.5F - center;
            const float squared = dx * dx + dy * dy;
            const auto index = static_cast<std::size_t>(y * size + x);
            if (squared >= lit.outside) {
                colors[index] = 0;
                alpha[index] = 0;
                continue;
            }
            const float from_light_x = dx - lit.light_x;
            const float from_light_y = dy - lit.light_y;
            const float shade = std::min(255.0F, (from_light_x * from_light_x + from_light_y * from_light_y) *
                                                     lit.body_inverse * 255.0F);
            const auto& base = shades[static_cast<std::size_t>(shade)];
            Rgb color{static_cast<float>(base[0]) / 255.0F, static_cast<float>(base[1]) / 255.0F,
                      static_cast<float>(base[2]) / 255.0F};
            for (std::size_t light = 0; light < count; ++light) {
                const float offset_x = dx - lights[light].x;
                const float offset_y = dy - lights[light].y;
                float falloff = 1.0F - (offset_x * offset_x + offset_y * offset_y) * lights[light].inverse_radius_squared;
                if (falloff <= 0.0F) continue;
                falloff *= falloff;
                color.red += (1.0F - color.red) * lights[light].red * falloff;
                color.green += (1.0F - color.green) * lights[light].green * falloff;
                color.blue += (1.0F - color.blue) * lights[light].blue * falloff;
            }
            const float offset_x = dx - lit.highlight_x;
            const float offset_y = dy - lit.highlight_y;
            float highlight = 1.0F - (offset_x * offset_x + offset_y * offset_y) * lit.highlight_inverse;
            if (highlight > 0.0F) {
                highlight *= highlight * lit.highlight_strength;
                color.red += (1.0F - color.red) * highlight;
                color.green += (1.0F - color.green) * highlight;
                color.blue += (1.0F - color.blue) * highlight;
            }
            float coverage = 1.0F;
            if (squared > lit.rim_start) {
                // A faint warm line just inside the edge, then an antialiased edge.
                color.red += (rim_color.red - color.red) * 0.16F;
                color.green += (rim_color.green - color.green) * 0.16F;
                color.blue += (rim_color.blue - color.blue) * 0.16F;
                coverage = std::clamp(radius - std::sqrt(squared) + 0.5F, 0.0F, 1.0F);
            }
            colors[index] = rgb565(color);
            alpha[index] = static_cast<std::uint8_t>(coverage * 255.0F + 0.5F);
        }
    }
    ++frame_count_;
}

AuraPearlView::Ring AuraPearlView::make_ring(lv_obj_t* parent, bool rounded, std::uint32_t& depth) {
    Ring ring;
    ring.arc = lv_arc_create(parent);
    lv_obj_remove_style_all(ring.arc);
    lv_obj_set_style_arc_opa(ring.arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(ring.arc, rounded, LV_PART_INDICATOR);
    lv_obj_set_style_opa(ring.arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_clickable(ring.arc, false);
    lv_obj_set_hidden(ring.arc, true);
    lv_obj_move_to_index(ring.arc, static_cast<std::int32_t>(depth++));
    return ring;
}

// Shows a ring (or a segment from `start` to `end` degrees) stroked at
// `radius`; a faint or empty one is hidden. Only what changed is set.
void AuraPearlView::show_ring(Ring& ring, float radius, float width, float opacity, std::uint32_t color, int start,
                              int end) {
    if (!ring.arc) return;
    const int opa = channel(opacity);
    if (opa < 3 || radius <= 0.0F) {
        if (ring.radius != 0) lv_obj_set_hidden(ring.arc, true);
        ring.radius = 0;
        return;
    }
    const int stroke = std::max(1, static_cast<int>(std::lround(width)));
    const int reach = std::min(kMaxRingRadius, static_cast<int>(std::lround(radius)));
    if (ring.radius == 0) lv_obj_set_hidden(ring.arc, false);
    if (reach != ring.radius || stroke != ring.width) {
        const int box = 2 * reach + stroke;
        lv_obj_set_size(ring.arc, box, box);
        lv_obj_set_pos(ring.arc, center_x_ - box / 2, center_y_ - box / 2);
    }
    if (stroke != ring.width) lv_obj_set_style_arc_width(ring.arc, stroke, LV_PART_INDICATOR);
    if (std::abs(opa - ring.opacity) >= 3) {
        lv_obj_set_style_arc_opa(ring.arc, static_cast<lv_opa_t>(opa), LV_PART_INDICATOR);
        ring.opacity = opa;
    }
    if (color != ring.color) {
        lv_obj_set_style_arc_color(ring.arc, lv_color_hex(color), LV_PART_INDICATOR);
        ring.color = color;
    }
    if (start != ring.start || end != ring.end) {
        lv_arc_set_angles(ring.arc, static_cast<lv_value_precise_t>(start), static_cast<lv_value_precise_t>(end));
        ring.start = start;
        ring.end = end;
    }
    ring.radius = reach;
    ring.width = stroke;
}

// Each ring of this frame, back to front (`place(slot, radius, width,
// opacity, colour, start, end)`): ripples start on each rise of the voice,
// then spread and fade over 1.6 s; listening, a ring widens and brightens
// with the voice; thinking, a comet circles the pearl, its tail thinning and
// fading.
template <typename Place>
void AuraPearlView::each_ring(std::uint32_t glow, Place&& place) {
    const float drive = (weights_[1] + weights_[3]) * level_;
    const float ripple_weight = weights_[1] + weights_[3];
    if (ripple_weight > 0.3F && drive > 0.5F && previous_drive_ <= 0.5F && seconds_ - last_ripple_ > 0.16F) {
        std::size_t slot = 0;
        for (std::size_t index = 1; index < ripple_born_.size(); ++index) {
            if (ripple_born_[index] < ripple_born_[slot]) slot = index;
        }
        ripple_born_[slot] = seconds_;
        last_ripple_ = seconds_;
    }
    previous_drive_ = drive;
    for (std::size_t index = 0; index < ripples_.size(); ++index) {
        const float age = ripple_born_[index] < 0.0F ? 1.0F : (seconds_ - ripple_born_[index]) / 1.6F;
        if (age >= 1.0F) {
            ripple_born_[index] = -1.0F;
            place(index, 0.0F, 0.0F, 0.0F, glow, 0, 360);
            continue;
        }
        const float spread = 1.0F - std::pow(1.0F - age, 2.2F);
        place(index, radius_ + 10.0F + 115.0F * spread, 2.0F, (1.0F - age) * (1.0F - age) * 0.55F * ripple_weight,
              glow, 0, 360);
    }

    place(kRipples, weights_[1] > 0.01F ? radius_ + 14.0F + 26.0F * level_ : 0.0F, 2.0F + 2.5F * level_,
          weights_[1] * (0.3F + 0.6F * level_), glow, 0, 360);

    const float head = seconds_ * 2.6F;
    constexpr float kSpan = 2.2F;
    for (std::size_t index = 0; index < kCometSegments; ++index) {
        const float share = static_cast<float>(index) / static_cast<float>(kCometSegments);
        const float tail = head - (share + 1.0F / static_cast<float>(kCometSegments)) * kSpan;
        place(kRipples + 1 + index, weights_[2] > 0.01F ? radius_ + 17.0F : 0.0F, 3.4F * (1.0F - share) + 0.5F,
              weights_[2] * std::pow(1.0F - share, 1.6F), kComet, degrees(tail), degrees(head - share * kSpan));
    }
}

void AuraPearlView::update_objects() {
    const float drive = (weights_[1] + weights_[3]) * level_;
    const auto glow = hex(mix(weights_, kGlow));
    // The glow brightens with the voice and while thinking; a step of a few
    // levels is needed before it is redrawn.
    if (glow_) {
        const int opacity = channel(0.17F + 0.34F * drive + 0.16F * weights_[2]);
        if (std::abs(opacity - glow_opacity_) >= 3) {
            lv_obj_set_style_image_opa(glow_, static_cast<lv_opa_t>(opacity), 0);
            glow_opacity_ = opacity;
        }
        const std::uint32_t coarse = glow & 0xF8F8F8U;
        if (coarse != glow_color_) {
            lv_obj_set_style_image_recolor(glow_, lv_color_hex(coarse), 0);
            glow_color_ = coarse;
        }
    }
    each_ring(glow, [this](std::size_t slot, float radius, float width, float opacity, std::uint32_t color, int start,
                           int end) {
        Ring& ring = slot < kRipples ? ripples_[slot] : (slot == kRipples ? ring_ : comet_[slot - kRipples - 1]);
        show_ring(ring, radius, width, opacity, color, start, end);
    });
}

// The board: the frame as far as the rings reach, drawn whole on a first
// visit or at a new place; a return shows the last frame and the worker
// draws the next.
lv_obj_t* AuraPearlView::attach_device(lv_obj_t* parent, int center_x, int center_y) {
    if (shared_worker_ && !device_->worker()) device_->set_worker(FrameWorker::shared());
    if (shared_worker_ && !device_->helper()) device_->set_helper(FrameWorker::helper());
    device_->attach(parent, center_x - device_reach, center_y - device_reach);
    center_x_ = center_x;
    center_y_ = center_y;
    image_ = device_->object();
    // Other light fields under the frame are drawn into it, above the glow.
    auto absorbed = absorb_light_fields(parent, device_->area());
    if (absorbed != absorbed_) {
        absorbed_ = std::move(absorbed);
        device_->invalidate_all();
    }
    if (reduced_motion_ || !attached_before_) {
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            weights_[index] = index == static_cast<std::size_t>(state_) ? 1.0F : 0.0F;
        }
    }
    attached_before_ = true;
    advance(0.0F);
    if (device_->needs_whole()) {
        prepare_device();
        device_->draw_whole();
    }
    return image_;
}

AuraPearlView::RingShape AuraPearlView::ring_shape(float radius, float width, float opacity, std::uint32_t color,
                                                   int start, int end, bool rounded) noexcept {
    RingShape shape;
    const int opa = channel(opacity);
    if (opa < 3 || radius <= 0.0F) return shape;
    const int stroke = std::max(1, static_cast<int>(std::lround(width)));
    const int reach = std::min(kMaxRingRadius, static_cast<int>(std::lround(radius)));
    shape.visible = true;
    shape.reach = reach;
    // LVGL's arc: a box of 2 reach + stroke, its radius half of it.
    shape.radius = (2 * reach + stroke) / 2;
    shape.stroke = stroke;
    shape.opacity = opa;
    shape.color = color;
    shape.start = start;
    shape.end = end;
    shape.rounded = rounded;
    return shape;
}

// The frame's glow, rings and sphere, as the presentation shows them.
void AuraPearlView::prepare_device() {
    sphere_ = sphere();
    const float drive = (weights_[1] + weights_[3]) * level_;
    const auto glow = hex(mix(weights_, kGlow));
    // As the presentation's glow: a step of a few levels before it brightens.
    const int opacity = channel(0.17F + 0.34F * drive + 0.16F * weights_[2]);
    if (glow_frame_.radius != kGlowRadius || std::abs(opacity - glow_frame_.opacity) >= 3) {
        glow_frame_.opacity = static_cast<std::uint8_t>(opacity);
    }
    glow_frame_.center_x = center_x_;
    glow_frame_.center_y = center_y_;
    glow_frame_.radius = kGlowRadius;
    glow_frame_.tint = glow & 0xF8F8F8U;
    glow_frame_.shape = LightShape::Pearl;
    each_ring(glow, [this](std::size_t slot, float radius, float width, float opacity, std::uint32_t color, int start,
                           int end) {
        auto shape = ring_shape(radius, width, opacity, color, start, end, slot > kRipples);
        if (shape.visible && shape.rounded) {
            const float middle = static_cast<float>(shape.radius) - 0.5F * static_cast<float>(shape.stroke);
            const float first = static_cast<float>(start) * (kPi / 180.0F);
            const float last = static_cast<float>(end) * (kPi / 180.0F);
            shape.start_cos = std::cos(first);
            shape.start_sin = std::sin(first);
            shape.end_cos = std::cos(last);
            shape.end_sin = std::sin(last);
            shape.start_x = static_cast<float>(center_x_) + middle * shape.start_cos;
            shape.start_y = static_cast<float>(center_y_) + middle * shape.start_sin;
            shape.end_x = static_cast<float>(center_x_) + middle * shape.end_cos;
            shape.end_y = static_cast<float>(center_y_) + middle * shape.end_sin;
            const int span = ((end - start) % 360 + 360) % 360;
            shape.narrow = span > 0 && span < 180;
            if (shape.narrow) {
                // The pixels the segment reaches: its arc at both edges, in
                // steps of under 3 degrees, with its ends' caps and a margin.
                const float outer = static_cast<float>(shape.radius) + 1.0F;
                const float inner = static_cast<float>(shape.radius - shape.stroke) - 1.0F;
                float left = 1e9F;
                float top = 1e9F;
                float right = -1e9F;
                float bottom = -1e9F;
                constexpr int kSteps = 8;
                for (int step = 0; step <= kSteps; ++step) {
                    const float angle = first + (static_cast<float>(span) * (kPi / 180.0F)) *
                                                    (static_cast<float>(step) / static_cast<float>(kSteps));
                    const float across = std::cos(angle);
                    const float down = std::sin(angle);
                    for (const float reach : {outer, inner}) {
                        left = std::min(left, reach * across);
                        right = std::max(right, reach * across);
                        top = std::min(top, reach * down);
                        bottom = std::max(bottom, reach * down);
                    }
                }
                const float margin = 0.5F * static_cast<float>(shape.stroke) + 2.0F;
                shape.box = {center_x_ + device_floor(left - margin), center_y_ + device_floor(top - margin),
                             center_x_ + device_ceil(right + margin) + 1, center_y_ + device_ceil(bottom + margin) + 1};
            }
        }
        rings_[slot] = shape;
    });
}

// The frame's changes: the sphere, every ring that moved (where it was and
// where it is), and the whole frame when the glow brightened or took another
// state's colour.
bool AuraPearlView::start_device_frame() {
    const FrameLight glow_before = glow_frame_;
    const auto rings_before = rings_;
    prepare_device();
    FrameRect dirty = FrameRect::around(center_x_, center_y_, size / 2);
    if (glow_frame_ != glow_before) {
        dirty = device_->area();
    } else {
        for (std::size_t slot = 0; slot < kRings; ++slot) {
            if (rings_[slot] == rings_before[slot]) continue;
            for (const RingShape* ring : {&rings_before[slot], static_cast<const RingShape*>(&rings_[slot])}) {
                if (ring->visible) dirty = dirty.united(FrameRect::around(center_x_, center_y_, ring->radius + 2));
            }
        }
    }
    return device_->start(dirty);
}

// A ring over one row, as LVGL's arc: antialiased at both edges and, for a
// segment, cut at its angles with rounded ends.
void AuraPearlView::lay_ring(const FrameRow& row, const RingShape& ring) const noexcept {
    const float outer = static_cast<float>(ring.radius);
    const float inner = outer - static_cast<float>(ring.stroke);
    const float dy = static_cast<float>(row.y - center_y_) + 0.5F;
    const float limit = outer + 1.0F;
    if (std::fabs(dy) >= limit) return;
    const float reach = device_root(limit * limit - dy * dy);
    const float cx = static_cast<float>(center_x_);
    int first = std::max(row.x1, device_floor(cx - reach - 0.5F));
    int last = std::min(row.x2 - 1, device_ceil(cx + reach));
    // A segment under half a turn reaches only its box (a comet's segment is
    // some 21 degrees: the whole ring's columns were worked out and dropped).
    const bool narrow = ring.narrow;
    if (narrow) {
        if (row.y < ring.box.y1 || row.y >= ring.box.y2) return;
        first = std::max(first, ring.box.x1);
        last = std::min(last, ring.box.x2 - 1);
    }
    if (last < first) return;
    // The hole inside the ring, skipped.
    int hole_first = last + 1;
    int hole_last = last;
    const float hollow = inner - 1.0F;
    if (hollow > 0.0F && std::fabs(dy) < hollow) {
        const float hole = device_root(hollow * hollow - dy * dy);
        hole_first = std::max(first, device_ceil(cx - hole - 0.5F) + 1);
        hole_last = std::min(last, device_floor(cx + hole - 0.5F) - 1);
    }
    // Everything the loop reads is a local (a byte stored to the row could
    // alias the ring's fields).
    const bool whole = ring.end - ring.start >= 360 || (ring.start == 0 && ring.end == 360);
    const bool rounded = ring.rounded;
    const float half = 0.5F * static_cast<float>(ring.stroke);
    const float cap_reach = (half + 1.0F) * (half + 1.0F);
    const float scale = static_cast<float>(ring.opacity) * (256.0F / 255.0F);
    const float start = static_cast<float>(ring.start);
    const float end = static_cast<float>(ring.end);
    const float start_cos = ring.start_cos;
    const float start_sin = ring.start_sin;
    const float end_cos = ring.end_cos;
    const float end_sin = ring.end_sin;
    const float start_x = ring.start_x;
    const float end_x = ring.end_x;
    const float cap_y_start = static_cast<float>(row.y) + 0.5F - ring.start_y;
    const float cap_y_end = static_cast<float>(row.y) + 0.5F - ring.end_y;
    const float cap_start_squared = cap_y_start * cap_y_start;
    const float cap_end_squared = cap_y_end * cap_y_end;
    const int red = static_cast<int>((ring.color >> 16) & 0xFFU);
    const int green = static_cast<int>((ring.color >> 8) & 0xFFU);
    const int blue = static_cast<int>(ring.color & 0xFFU);
    const float dy_squared = dy * dy;
    const int center = center_x_;
    std::uint8_t* const rgb = row.rgb;
    const int row_x1 = row.x1;
    for (int x = first; x <= last; ++x) {
        if (x == hole_first && hole_first <= hole_last) {
            x = hole_last;
            continue;
        }
        const float dx = static_cast<float>(x - center) + 0.5F;
        const float distance = device_root(dx * dx + dy_squared);
        float cover = std::clamp(outer - distance + 0.5F, 0.0F, 1.0F) * std::clamp(distance - inner + 0.5F, 0.0F, 1.0F);
        if (!whole) {
            // Clockwise of the start and short of the end: each a cross product
            // for a narrow segment, the angle itself otherwise.
            bool inside;
            if (narrow) {
                inside = start_cos * dy - start_sin * dx >= 0.0F && dx * end_sin - dy * end_cos >= 0.0F;
            } else {
                const float angle = device_angle(dy, dx);
                inside = start <= end ? (angle >= start && angle <= end) : (angle >= start || angle <= end);
            }
            if (!inside) cover = 0.0F;
            if (rounded) {
                const float pixel_x = static_cast<float>(x) + 0.5F;
                const float to_start_x = pixel_x - start_x;
                const float to_end_x = pixel_x - end_x;
                const float start_squared = to_start_x * to_start_x + cap_start_squared;
                const float end_squared = to_end_x * to_end_x + cap_end_squared;
                if (start_squared < cap_reach) {
                    cover = std::max(cover, std::clamp(half - device_root(start_squared) + 0.5F, 0.0F, 1.0F));
                }
                if (end_squared < cap_reach) {
                    cover = std::max(cover, std::clamp(half - device_root(end_squared) + 0.5F, 0.0F, 1.0F));
                }
            }
        }
        if (cover <= 0.0F) continue;
        const int alpha = static_cast<int>(cover * scale + 0.5F);
        if (alpha <= 0) continue;
        const int keep = 256 - alpha;
        std::uint8_t* pixel = rgb + static_cast<std::ptrdiff_t>(x - row_x1) * 3;
        pixel[0] = static_cast<std::uint8_t>((pixel[0] * keep + red * alpha) >> 8);
        pixel[1] = static_cast<std::uint8_t>((pixel[1] * keep + green * alpha) >> 8);
        pixel[2] = static_cast<std::uint8_t>((pixel[2] * keep + blue * alpha) >> 8);
    }
}

// The sphere's light at grid row `row` (every other pixel from the centre, a
// pixel's centre at 2 row + 0.5): the presentation's shade, lights and
// highlight at each grid column.
void FRAME_FAST AuraPearlView::shade_grid_row(int row, float* out) const noexcept {
    const Sphere& lit = sphere_;
    const Shade* shades = body_shades().data();
    const float dy = 2.0F * static_cast<float>(row) + 0.5F;
    // Locals: the stores to `out` could alias the view's fields.
    const std::size_t count = lit.count;
    std::array<float, 6> light_x{};
    std::array<float, 6> light_y{};
    std::array<float, 6> light_inverse{};
    std::array<float, 6> light_red{};
    std::array<float, 6> light_green{};
    std::array<float, 6> light_blue{};
    for (std::size_t index = 0; index < count; ++index) {
        const auto& light = lit.lights[index];
        const float offset_y = dy - light.y;
        light_x[index] = light.x;
        light_y[index] = offset_y * offset_y;
        light_inverse[index] = light.inverse_radius_squared;
        light_red[index] = light.red;
        light_green[index] = light.green;
        light_blue[index] = light.blue;
    }
    const float body_scale = lit.body_inverse * 255.0F;
    const float body_x = lit.light_x;
    const float highlight_x0 = lit.highlight_x;
    const float highlight_inverse = lit.highlight_inverse;
    const float highlight_strength = lit.highlight_strength;
    const float from_light_y = dy - lit.light_y;
    const float from_light_y_squared = from_light_y * from_light_y;
    const float highlight_y = dy - lit.highlight_y;
    const float highlight_y_squared = highlight_y * highlight_y;
    constexpr float kByte = 1.0F / 255.0F;
    for (int column = 0; column < kGridColumns; ++column, out += 3) {
        const float dx = 2.0F * static_cast<float>(kGridFirst + column) + 0.5F;
        const float from_light_x = dx - body_x;
        const float shade = std::min(255.0F, (from_light_x * from_light_x + from_light_y_squared) * body_scale);
        const Shade& base = shades[static_cast<int>(shade)];
        float red = static_cast<float>(base[0]) * kByte;
        float green = static_cast<float>(base[1]) * kByte;
        float blue = static_cast<float>(base[2]) * kByte;
        for (std::size_t index = 0; index < count; ++index) {
            const float offset_x = dx - light_x[index];
            float falloff = 1.0F - (offset_x * offset_x + light_y[index]) * light_inverse[index];
            if (falloff <= 0.0F) continue;
            falloff *= falloff;
            red += (1.0F - red) * light_red[index] * falloff;
            green += (1.0F - green) * light_green[index] * falloff;
            blue += (1.0F - blue) * light_blue[index] * falloff;
        }
        const float highlight_x = dx - highlight_x0;
        float highlight = 1.0F - (highlight_x * highlight_x + highlight_y_squared) * highlight_inverse;
        if (highlight > 0.0F) {
            highlight *= highlight * highlight_strength;
            red += (1.0F - red) * highlight;
            green += (1.0F - green) * highlight;
            blue += (1.0F - blue) * highlight;
        }
        out[0] = red;
        out[1] = green;
        out[2] = blue;
    }
}

// Grid row `row`, worked out once a frame: the frame's rows ask for them in
// order, each for one or two.
const float* AuraPearlView::grid_row(int lane, int row) const noexcept {
    auto& index = grid_index_[static_cast<std::size_t>(lane)];
    auto& rows = grid_rows_[static_cast<std::size_t>(lane)];
    for (std::size_t slot = 0; slot < index.size(); ++slot) {
        if (index[slot] == row) return rows[slot].data();
    }
    const std::size_t slot = index[0] < index[1] ? 0 : 1;
    shade_grid_row(row, rows[slot].data());
    index[slot] = row;
    return rows[slot].data();
}

// The sphere over one row: the light between the grid's points, its rim and
// coverage at every pixel, laid over the glow and the rings.
void FRAME_FAST AuraPearlView::lay_sphere(const FrameRow& row, int lane) const noexcept {
    const Sphere& lit = sphere_;
    if (row.y < center_y_ - size / 2 || row.y >= center_y_ + size / 2) return;
    const float dy = static_cast<float>(row.y - center_y_) + 0.5F;
    const float rest = lit.outside - dy * dy;
    if (rest <= 0.0F) return;
    const float half = device_root(rest);
    const float cx = static_cast<float>(center_x_);
    const int first = std::max({row.x1, center_x_ - size / 2, device_floor(cx - half - 0.5F)});
    const int last = std::min({row.x2 - 1, center_x_ + size / 2 - 1, device_ceil(cx + half)});
    if (last < first) return;
    // The grid rows around this row: one when it lies on the grid, else the
    // two it lies between.
    const int offset_y = row.y - center_y_;
    const int grid_y = offset_y >= 0 ? offset_y / 2 : -((-offset_y + 1) / 2);
    const bool between_rows = offset_y - 2 * grid_y != 0;
    const float* upper = grid_row(lane, grid_y);
    const float* lower = between_rows ? grid_row(lane, grid_y + 1) : upper;
    const Rgb rim_color = rgb(255, 240, 222);
    const float outside = lit.outside;
    const float rim_start = lit.rim_start;
    const float radius = lit.radius;
    const float dy_squared = dy * dy;
    std::uint8_t* pixel = row.rgb + static_cast<std::ptrdiff_t>(first - row.x1) * 3;
    for (int x = first; x <= last; ++x, pixel += 3) {
        const int offset_x = x - center_x_;
        const float dx = static_cast<float>(offset_x) + 0.5F;
        const float squared = dx * dx + dy_squared;
        if (squared >= outside) continue;
        const int grid_x = offset_x >= 0 ? offset_x / 2 : -((-offset_x + 1) / 2);
        const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(grid_x - kGridFirst) * 3;
        // Two rows' values are summed (one row twice when on it), a column's
        // two when between columns.
        float red = upper[at] + lower[at];
        float green = upper[at + 1] + lower[at + 1];
        float blue = upper[at + 2] + lower[at + 2];
        float share = 0.5F;
        if (offset_x - 2 * grid_x != 0) {
            red += upper[at + 3] + lower[at + 3];
            green += upper[at + 4] + lower[at + 4];
            blue += upper[at + 5] + lower[at + 5];
            share *= 0.5F;
        }
        red *= share;
        green *= share;
        blue *= share;
        int alpha = 256;
        if (squared > rim_start) {
            // A faint warm line just inside the edge, then an antialiased edge.
            red += (rim_color.red - red) * 0.16F;
            green += (rim_color.green - green) * 0.16F;
            blue += (rim_color.blue - blue) * 0.16F;
            const float coverage = std::clamp(radius - device_root(squared) + 0.5F, 0.0F, 1.0F);
            const int byte = static_cast<int>(coverage * 255.0F + 0.5F);
            alpha = byte + (byte >> 7);
        }
        if (alpha <= 0) continue;
        const int keep = 256 - alpha;
        pixel[0] = static_cast<std::uint8_t>((pixel[0] * keep + channel(red) * alpha) >> 8);
        pixel[1] = static_cast<std::uint8_t>((pixel[1] * keep + channel(green) * alpha) >> 8);
        pixel[2] = static_cast<std::uint8_t>((pixel[2] * keep + channel(blue) * alpha) >> 8);
    }
}

// On the worker: row by row, the glow, the light fields above it, the rings
// back to front and the sphere.
// On the worker, once a frame: the sphere's grid rows are this frame's
// light, none kept from the last (its strips share them).
void AuraPearlView::begin_device(void* view, const FrameRect&) {
    auto* self = static_cast<AuraPearlView*>(view);
    for (auto& index : self->grid_index_) index = {std::numeric_limits<int>::min(), std::numeric_limits<int>::min()};
}

void AuraPearlView::draw_device(void* view, int lane, std::uint16_t* pixels, const FrameRect& area,
                                const FrameRect& rect) noexcept {
    auto* self = static_cast<AuraPearlView*>(view);
    std::uint8_t* row = self->rows_[static_cast<std::size_t>(lane)].get();
    const int width = rect.width();
    for (int y = rect.y1; y < rect.y2; ++y) {
        // The glow is the black row's first light: it writes the row whole.
        const FrameRow composed{row, rect.x1, rect.x2, y};
        lay_first_light(composed, self->glow_frame_);
        for (const auto& field : self->absorbed_) lay_light(composed, field);
        for (const auto& ring : self->rings_) {
            if (ring.visible) self->lay_ring(composed, ring);
        }
        self->lay_sphere(composed, lane);
        pack_row(row, width,
                 pixels + static_cast<std::size_t>(y - area.y1) * static_cast<std::size_t>(area.width()) +
                     static_cast<std::size_t>(rect.x1 - area.x1));
    }
}

}  // namespace aura::ui

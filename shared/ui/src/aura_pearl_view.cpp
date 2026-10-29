#include "aura/aura_pearl_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace aura::ui {

namespace {
constexpr float kTau = 6.2831853071795864769F;
constexpr float kPi = kTau / 2.0F;
constexpr float kRestRadius = 80.0F;
constexpr int kMaxRingRadius = 205;
constexpr int kGlowTexture = 32;
constexpr int kGlowRadius = 152;  // 1.9 x the pearl

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
}  // namespace

AuraPearlView::AuraPearlView() {
    descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB565A8;
    descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    descriptor_.header.w = size;
    descriptor_.header.h = size;
    descriptor_.header.stride = size * static_cast<int>(sizeof(std::uint16_t));
    descriptor_.data_size = buffer_.size();
    descriptor_.data = buffer_.data();
    ripple_born_.fill(-1.0F);
}

lv_obj_t* AuraPearlView::attach(lv_obj_t* parent, int center_x, int center_y) {
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

void AuraPearlView::render() {
    struct Light {
        float x;
        float y;
        float inverse_radius_squared;
        Rgb color;  // already times its strength
    };
    std::array<Light, 6> lights{};
    std::size_t count = 0;
    const float radius = radius_;
    const auto add = [&](float x, float y, float light_radius, const Rgb& color, float strength) {
        if (strength < 0.004F || count >= lights.size()) return;
        lights[count++] = {x, y, 1.0F / (light_radius * light_radius),
                           {color.red * strength, color.green * strength, color.blue * strength}};
    };
    // Coloured lights drift slowly inside the pearl (screen blend); the
    // state tints it from below and, while thinking, two lights circle.
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
    // The highlight wanders a little at the upper left and brightens with the voice.
    const float drive = (weights_[1] + weights_[3]) * level_;
    const float highlight_angle = -2.25F + 0.22F * std::sin(seconds_ * 0.45F);
    const float highlight_x = std::cos(highlight_angle) * radius * 0.5F;
    const float highlight_y = std::sin(highlight_angle) * radius * 0.5F;
    const float highlight_inverse = 1.0F / (radius * 0.34F * radius * 0.34F);
    const float highlight_strength = std::min(1.0F, 0.82F + 0.18F * drive);
    const float light_x = -0.2F * radius;
    const float light_y = -0.2F * radius;
    const float body_inverse = 1.0F / (1.44F * radius * radius);
    const float outside = (radius + 1.0F) * (radius + 1.0F);
    const float rim_start = (radius - 1.2F) * (radius - 1.2F);
    const Rgb rim_color = rgb(255, 240, 222);
    const auto& shades = body_shades();

    auto* colors = reinterpret_cast<std::uint16_t*>(buffer_.data());
    auto* alpha = buffer_.data() + size * size * sizeof(std::uint16_t);
    const float center = static_cast<float>(size) / 2.0F;
    for (int y = 0; y < size; ++y) {
        const float dy = static_cast<float>(y) + 0.5F - center;
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x) + 0.5F - center;
            const float squared = dx * dx + dy * dy;
            const auto index = static_cast<std::size_t>(y * size + x);
            if (squared >= outside) {
                colors[index] = 0;
                alpha[index] = 0;
                continue;
            }
            const float from_light_x = dx - light_x;
            const float from_light_y = dy - light_y;
            const float shade = std::min(255.0F, (from_light_x * from_light_x + from_light_y * from_light_y) *
                                                     body_inverse * 255.0F);
            const auto& base = shades[static_cast<std::size_t>(shade)];
            Rgb color{static_cast<float>(base[0]) / 255.0F, static_cast<float>(base[1]) / 255.0F,
                      static_cast<float>(base[2]) / 255.0F};
            for (std::size_t light = 0; light < count; ++light) {
                const float offset_x = dx - lights[light].x;
                const float offset_y = dy - lights[light].y;
                float falloff = 1.0F - (offset_x * offset_x + offset_y * offset_y) * lights[light].inverse_radius_squared;
                if (falloff <= 0.0F) continue;
                falloff *= falloff;
                color.red += (1.0F - color.red) * lights[light].color.red * falloff;
                color.green += (1.0F - color.green) * lights[light].color.green * falloff;
                color.blue += (1.0F - color.blue) * lights[light].color.blue * falloff;
            }
            const float offset_x = dx - highlight_x;
            const float offset_y = dy - highlight_y;
            float highlight = 1.0F - (offset_x * offset_x + offset_y * offset_y) * highlight_inverse;
            if (highlight > 0.0F) {
                highlight *= highlight * highlight_strength;
                color.red += (1.0F - color.red) * highlight;
                color.green += (1.0F - color.green) * highlight;
                color.blue += (1.0F - color.blue) * highlight;
            }
            float coverage = 1.0F;
            if (squared > rim_start) {
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

    // Ripples start on each rise of the voice, then spread and fade over 1.6 s.
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
            show_ring(ripples_[index], 0.0F, 0.0F, 0.0F, glow);
            continue;
        }
        const float spread = 1.0F - std::pow(1.0F - age, 2.2F);
        show_ring(ripples_[index], radius_ + 10.0F + 115.0F * spread, 2.0F,
                  (1.0F - age) * (1.0F - age) * 0.55F * ripple_weight, glow);
    }

    // Listening: a ring that widens and brightens with the voice.
    show_ring(ring_, weights_[1] > 0.01F ? radius_ + 14.0F + 26.0F * level_ : 0.0F, 2.0F + 2.5F * level_,
              weights_[1] * (0.3F + 0.6F * level_), glow);

    // Thinking: a comet circling the pearl, its tail thinning and fading.
    const float head = seconds_ * 2.6F;
    constexpr float kSpan = 2.2F;
    for (std::size_t index = 0; index < comet_.size(); ++index) {
        const float share = static_cast<float>(index) / static_cast<float>(comet_.size());
        const float tail = head - (share + 1.0F / static_cast<float>(comet_.size())) * kSpan;
        show_ring(comet_[index], weights_[2] > 0.01F ? radius_ + 17.0F : 0.0F, 3.4F * (1.0F - share) + 0.5F,
                  weights_[2] * std::pow(1.0F - share, 1.6F), kComet, degrees(tail), degrees(head - share * kSpan));
    }
}

}  // namespace aura::ui

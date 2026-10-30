#include "aura/aura_stars_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "aura/aura_theme.hpp"

namespace aura::ui {

namespace {
constexpr float kTau = 6.2831853071795864769F;
constexpr float kMiddle = AuraStarsView::size / 2.0F;
// The reference's 118 px sphere swells to 1.28 of itself with a loud voice;
// 106 px keeps that, and the largest dot, inside the 280 px canvas.
constexpr float kRadius = 106.0F;
constexpr float kTilt = 0.38F;

struct Rgb {
    float red;
    float green;
    float blue;
};

constexpr Rgb rgb(int red, int green, int blue) {
    return {static_cast<float>(red) / 255.0F, static_cast<float>(green) / 255.0F, static_cast<float>(blue) / 255.0F};
}

// In PresenceState order: idle, listening, thinking, speaking.
constexpr std::array<Rgb, 4> kColor{{rgb(214, 226, 255), rgb(110, 232, 245), rgb(182, 150, 255), rgb(255, 150, 200)}};
constexpr std::array<Rgb, 4> kHot{{rgb(255, 255, 255), rgb(225, 255, 255), rgb(244, 226, 255), rgb(255, 232, 242)}};

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

// The reference's small seeded generator (mulberry32), for each point's own phase.
float next_random(std::uint32_t& state) {
    state += 0x6D2B79F5U;
    std::uint32_t value = state;
    value = (value ^ (value >> 15U)) * (value | 1U);
    value ^= value + (value ^ (value >> 7U)) * (value | 61U);
    return static_cast<float>(value ^ (value >> 14U)) / 4294967296.0F;
}
}  // namespace

AuraStarsView::AuraStarsView() {
    descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB565A8;
    descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    descriptor_.header.w = size;
    descriptor_.header.h = size;
    descriptor_.header.stride = size * static_cast<int>(sizeof(std::uint16_t));
    descriptor_.data_size = buffer_.size();
    descriptor_.data = buffer_.data();
    // Fibonacci points: even over the sphere.
    const float golden = kTau / 2.0F * (3.0F - std::sqrt(5.0F));
    std::uint32_t seed = 7;
    for (std::size_t index = 0; index < points_.size(); ++index) {
        const float y = 1.0F - 2.0F * (static_cast<float>(index) + 0.5F) / static_cast<float>(points_.size());
        const float ring = std::sqrt(1.0F - y * y);
        const float angle = static_cast<float>(index) * golden;
        points_[index] = {std::cos(angle) * ring, y, std::sin(angle) * ring, next_random(seed)};
    }
}

lv_obj_t* AuraStarsView::attach(lv_obj_t* parent, int center_x, int center_y) {
    // The glow sits behind the screen's content; the canvas on top.
    glow_ = light_field(parent, center_x, center_y, static_cast<int>(kRadius * 1.9F), 0xD6E2FF, LV_OPA_TRANSP);
    lv_obj_move_to_index(glow_, 0);
    glow_opacity_ = -1;
    glow_color_ = 0;
    image_ = lv_image_create(parent);
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, center_x - size / 2, center_y - size / 2);
    lv_obj_set_clickable(image_, false);
    lv_obj_set_scrollable(image_, false);
    // A new presence, or a still one, shows the state itself; later screens
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
    update_glow();
    return image_;
}

void AuraStarsView::detach() noexcept {
    image_ = nullptr;
    glow_ = nullptr;
}

void AuraStarsView::set_state(PresenceState state) noexcept { state_ = state; }
void AuraStarsView::set_level(float level) noexcept { level_ = std::clamp(level, 0.0F, 1.0F); }
void AuraStarsView::set_reduced_motion(bool reduced) noexcept { reduced_motion_ = reduced; }

bool AuraStarsView::tick(std::uint32_t elapsed_ms) {
    if (!image_ || reduced_motion_) return false;
    accumulator_ms_ += elapsed_ms;
    if (accumulator_ms_ < frame_interval_ms) return false;
    const auto step = std::min<std::uint32_t>(accumulator_ms_, 100U);
    accumulator_ms_ = 0;
    advance(static_cast<float>(step) / 1000.0F);
    render();
    lv_obj_invalidate(image_);
    update_glow();
    return true;
}

// Time, the eased blend between states (200 ms) and the sphere's turn:
// slow while waiting, quicker with the voice and while thinking.
void AuraStarsView::advance(float seconds) noexcept {
    seconds_ += seconds;
    const float ease = 1.0F - std::exp(-seconds / 0.2F);
    float sum = 0.0F;
    for (std::size_t index = 0; index < weights_.size(); ++index) {
        const float target = index == static_cast<std::size_t>(state_) ? 1.0F : 0.0F;
        weights_[index] += (target - weights_[index]) * ease;
        sum += weights_[index];
    }
    for (auto& weight : weights_) weight /= sum;
    const auto& w = weights_;
    turn_ += seconds * (0.16F * w[0] + (0.35F + 0.5F * level_) * w[1] + 0.55F * w[2] + 0.28F * w[3]);
    band_ = std::sin(seconds_ * 1.6F);
}

// An antialiased dot of light added to the canvas.
void AuraStarsView::stamp(float x, float y, float radius, float red, float green, float blue) noexcept {
    const int reach = static_cast<int>(std::ceil(radius + 1.0F));
    const int center_x = static_cast<int>(x);
    const int center_y = static_cast<int>(y);
    for (int dy = -reach; dy <= reach; ++dy) {
        const int row = center_y + dy;
        if (row < 0 || row >= size) continue;
        for (int dx = -reach; dx <= reach; ++dx) {
            const int column = center_x + dx;
            if (column < 0 || column >= size) continue;
            const float px = static_cast<float>(column) + 0.5F - x;
            const float py = static_cast<float>(row) + 0.5F - y;
            const float cover = std::clamp(radius + 0.5F - std::sqrt(px * px + py * py), 0.0F, 1.0F);
            if (cover <= 0.0F) continue;
            const auto index = static_cast<std::size_t>(row * size + column);
            auto* red_green = buffer_.data() + index * 2;
            auto* blue_byte = buffer_.data() + static_cast<std::size_t>(size * size) * 2 + index;
            const auto add = [cover](std::uint8_t& target, float value) {
                target = static_cast<std::uint8_t>(
                    std::min(255, static_cast<int>(target) + static_cast<int>(value * cover * 255.0F)));
            };
            add(red_green[0], red);
            add(red_green[1], green);
            add(*blue_byte, blue);
        }
    }
}

void AuraStarsView::render() {
    std::fill(buffer_.begin(), buffer_.end(), std::uint8_t{0});
    const auto& w = weights_;
    const float drive = (w[1] + w[3]) * level_;
    const float breath = std::sin(seconds_ * kTau / 6.5F);
    const float radius = kRadius * (1.0F + 0.02F * breath * w[0] + 0.05F * level_ * w[3]);
    const Rgb color = mix(w, kColor);
    const Rgb hot = mix(w, kHot);
    const float tilt_cos = std::cos(kTilt);
    const float tilt_sin = std::sin(kTilt);
    const float turn_cos = std::cos(turn_);
    const float turn_sin = std::sin(turn_);
    const float twist_wave = std::sin(seconds_ * 0.9F);
    float swell = 0.0F;
    for (const auto& point : points_) {
        float x = point.x;
        float y = point.y;
        float z = point.z;
        if (w[2] > 0.01F) {
            // Thinking twists the sphere about its axis and flattens it a little.
            const float twist = w[2] * 1.3F * y * twist_wave;
            const float twisted_x = x * std::cos(twist) - z * std::sin(twist);
            z = x * std::sin(twist) + z * std::cos(twist);
            x = twisted_x;
            y *= 1.0F - 0.18F * w[2];
        }
        const float turned_x = x * turn_cos - z * turn_sin;
        const float turned_z = x * turn_sin + z * turn_cos;
        const float tilted_y = y * tilt_cos - turned_z * tilt_sin;
        const float tilted_z = y * tilt_sin + turned_z * tilt_cos;
        float lift = 1.0F;
        if (w[1] > 0.01F) {
            lift += w[1] * level_ * 0.28F *
                    (0.5F + 0.5F * std::sin(6.0F * x + 4.5F * seconds_ + point.seed * 6.0F) *
                                std::sin(5.0F * y - 3.2F * seconds_));
        }
        if (w[3] > 0.01F) {
            lift += w[3] * (0.03F + 0.17F * level_) *
                    std::cos(8.0F * std::acos(std::clamp(tilted_z, -1.0F, 1.0F)) - 9.0F * seconds_);
        }
        swell = std::max(swell, lift - 1.0F);
        const float depth = (tilted_z + 1.0F) / 2.0F;
        float bright = 0.16F + 0.84F * depth;
        if (w[2] > 0.01F) {
            const float off_band = (y - band_) / 0.16F;
            bright += w[2] * 0.9F * std::exp(-off_band * off_band);
        }
        const Rgb& tone = bright > 1.0F ? hot : color;
        const float light = std::min(1.0F, bright);
        stamp(kMiddle + turned_x * radius * lift, kMiddle + tilted_y * radius * lift,
              (0.8F + 1.9F * depth) * (1.0F + 0.5F * drive), tone.red * light, tone.green * light, tone.blue * light);
    }
    swell_ = swell;

    // The added light becomes colour and coverage: alpha is the brightest
    // channel, so on black the canvas shows exactly the light that was added.
    auto* colors = reinterpret_cast<std::uint16_t*>(buffer_.data());
    auto* alpha = buffer_.data() + static_cast<std::size_t>(size * size) * 2;
    for (std::size_t index = 0; index < static_cast<std::size_t>(size * size); ++index) {
        const int red = buffer_[index * 2];
        const int green = buffer_[index * 2 + 1];
        const int blue = alpha[index];
        const int coverage = std::max(red, std::max(green, blue));
        if (coverage == 0) continue;
        const auto r = static_cast<unsigned>(red * 255 / coverage);
        const auto g = static_cast<unsigned>(green * 255 / coverage);
        const auto b = static_cast<unsigned>(blue * 255 / coverage);
        colors[index] = static_cast<std::uint16_t>(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
        alpha[index] = static_cast<std::uint8_t>(coverage);
    }
    ++frame_count_;
}

// The glow: the state's colour, brighter with the voice and while thinking;
// redrawn only after a step of a few levels.
void AuraStarsView::update_glow() {
    if (!glow_) return;
    const float drive = (weights_[1] + weights_[3]) * level_;
    const int opacity = channel(0.10F + 0.20F * drive + 0.08F * weights_[2]);
    if (std::abs(opacity - glow_opacity_) >= 3) {
        lv_obj_set_style_image_opa(glow_, static_cast<lv_opa_t>(opacity), 0);
        glow_opacity_ = opacity;
    }
    const std::uint32_t coarse = hex(mix(weights_, kColor)) & 0xF8F8F8U;
    if (coarse != glow_color_) {
        lv_obj_set_style_image_recolor(glow_, lv_color_hex(coarse), 0);
        glow_color_ = coarse;
    }
}

}  // namespace aura::ui

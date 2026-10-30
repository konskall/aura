#include "aura/aura_aurora_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "aura/aura_theme.hpp"

namespace aura::ui {

namespace {
constexpr float kTau = 6.2831853071795864769F;
constexpr float kPi = kTau / 2.0F;
// The reference's centre (240, 240) inside the band.
constexpr float kMiddleX = AuraAuroraView::width / 2.0F;
constexpr float kMiddleY = AuraAuroraView::height / 2.0F;

struct Rgb {
    float red;
    float green;
    float blue;
};

constexpr Rgb rgb(int red, int green, int blue) {
    return {static_cast<float>(red) / 255.0F, static_cast<float>(green) / 255.0F, static_cast<float>(blue) / 255.0F};
}

struct Ribbon {
    float waves;   // across the band
    float speed;
    float sway;    // share of the amplitude
    float offset;  // phase
};
constexpr std::array<Ribbon, 3> kRibbons{{{1.55F, 1.0F, 1.0F, 0.0F}, {2.25F, 1.32F, 0.78F, 2.1F},
                                         {1.15F, 0.78F, 0.62F, 4.2F}}};

// Per ribbon, in PresenceState order: idle, listening, thinking, speaking.
constexpr std::array<std::array<Rgb, 4>, 3> kColors{{
    {{rgb(70, 200, 190), rgb(56, 225, 201), rgb(157, 107, 255), rgb(255, 111, 177)}},
    {{rgb(80, 120, 230), rgb(80, 170, 255), rgb(90, 140, 255), rgb(170, 110, 255)}},
    {{rgb(140, 110, 230), rgb(130, 240, 255), rgb(255, 120, 200), rgb(255, 170, 120)}},
}};
constexpr std::array<Rgb, 4> kHaze{{rgb(70, 100, 200), rgb(40, 170, 200), rgb(120, 80, 230), rgb(220, 80, 160)}};

// The reference strokes each ribbon four times: a wide faint glow down to a
// bright core (half-widths in px, strengths).
struct Layer {
    float half_width;
    float strength;
};
constexpr std::array<Layer, 4> kLayers{{{15.0F, 0.07F}, {7.0F, 0.16F}, {2.5F, 0.42F}, {0.9F, 0.95F}}};

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

// The ribbons fade in and out at the band's ends (the reference's gradient
// from x 30 to 450).
float end_fade(float x) {
    const float along = (x - 6.0F) / 420.0F;
    if (along <= 0.0F || along >= 1.0F) return 0.0F;
    if (along < 0.22F) return along / 0.22F;
    if (along > 0.78F) return (1.0F - along) / 0.22F;
    return 1.0F;
}
}  // namespace

AuraAuroraView::AuraAuroraView() {
    descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB565A8;
    descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    descriptor_.header.w = width;
    descriptor_.header.h = height;
    descriptor_.header.stride = width * static_cast<int>(sizeof(std::uint16_t));
    descriptor_.data_size = buffer_.size();
    descriptor_.data = buffer_.data();
}

lv_obj_t* AuraAuroraView::attach(lv_obj_t* parent, int center_x, int center_y) {
    // The haze glows behind the screen's content; the band sits on top.
    haze_ = light_field(parent, center_x, center_y, 270, 0x4664C8, LV_OPA_TRANSP);
    lv_obj_move_to_index(haze_, 0);
    haze_opacity_ = -1;
    haze_color_ = 0;
    image_ = lv_image_create(parent);
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, center_x - width / 2, center_y - height / 2);
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
    update_haze();
    return image_;
}

void AuraAuroraView::detach() noexcept {
    image_ = nullptr;
    haze_ = nullptr;
}

void AuraAuroraView::set_state(PresenceState state) noexcept { state_ = state; }
void AuraAuroraView::set_level(float level) noexcept { level_ = std::clamp(level, 0.0F, 1.0F); }
void AuraAuroraView::set_reduced_motion(bool reduced) noexcept { reduced_motion_ = reduced; }

bool AuraAuroraView::tick(std::uint32_t elapsed_ms) {
    if (!image_ || reduced_motion_) return false;
    accumulator_ms_ += elapsed_ms;
    if (accumulator_ms_ < frame_interval_ms) return false;
    const auto step = std::min<std::uint32_t>(accumulator_ms_, 100U);
    accumulator_ms_ = 0;
    advance(static_cast<float>(step) / 1000.0F);
    render();
    lv_obj_invalidate(image_);
    update_haze();
    return true;
}

// Time, the eased blend between states (200 ms), each ribbon's phase and the
// sway: gentle while waiting, following the voice while listening and speaking.
void AuraAuroraView::advance(float seconds) noexcept {
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
    const float speed = 0.7F * w[0] + (1.6F + 2.6F * level_) * w[1] + 1.2F * w[2] + (1.4F + 2.4F * level_) * w[3];
    for (std::size_t index = 0; index < phases_.size(); ++index) {
        phases_[index] += seconds * speed * kRibbons[index].speed;
    }
    // The reference sways up to 108 px; the band is 200 px tall, so the voice's
    // share is scaled to keep the glow inside it.
    amplitude_ = 10.0F * w[0] + (16.0F + 56.0F * level_) * w[1] + 10.0F * w[2] + (14.0F + 52.0F * level_) * w[3];
}

void AuraAuroraView::add_light(int x, int y, float red, float green, float blue) noexcept {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    const auto index = static_cast<std::size_t>(y * width + x);
    auto* red_green = buffer_.data() + index * 2;
    auto* blue_byte = buffer_.data() + static_cast<std::size_t>(width * height) * 2 + index;
    const auto add = [](std::uint8_t& target, float value) {
        target = static_cast<std::uint8_t>(std::min(255, static_cast<int>(target) + static_cast<int>(value * 255.0F)));
    };
    add(red_green[0], red);
    add(red_green[1], green);
    add(*blue_byte, blue);
}

// Waving: each ribbon is a curve across the band, so each column holds one
// point of it; the four layers are lit by the distance across the curve.
void AuraAuroraView::wave(float drive, float strength) {
    const float widen = 1.0F + 0.5F * drive;
    const float reach = kLayers[0].half_width * widen + 1.0F;
    for (int x = 0; x < width; ++x) {
        const float fade = end_fade(static_cast<float>(x) + 0.5F);
        if (fade <= 0.0F) continue;
        const float along = (static_cast<float>(x) + 0.5F - 12.0F) / 408.0F;
        if (along <= 0.0F || along >= 1.0F) continue;
        const auto curve = [&](std::size_t ribbon, float u) {
            const auto& shape = kRibbons[ribbon];
            const float envelope = std::pow(std::sin(kPi * u), 1.5F);
            const float phase = phases_[ribbon] + shape.offset;
            return kMiddleY + envelope * shape.sway * amplitude_ *
                                  (0.75F * std::sin(shape.waves * kTau * u + phase) +
                                   0.35F * std::sin(2.6F * shape.waves * kTau * u - phase * 1.3F));
        };
        for (std::size_t ribbon = 0; ribbon < kRibbons.size(); ++ribbon) {
            const float y = curve(ribbon, along);
            const float slope = curve(ribbon, std::min(0.9999F, along + 1.0F / 408.0F)) - y;
            const float across = 1.0F / std::sqrt(1.0F + slope * slope);
            const Rgb color = mix(weights_, kColors[ribbon]);
            const float scale = fade * strength;
            const int top = std::max(0, static_cast<int>(std::floor(y - reach)));
            const int bottom = std::min(height - 1, static_cast<int>(std::ceil(y + reach)));
            for (int row = top; row <= bottom; ++row) {
                const float distance = std::abs(static_cast<float>(row) + 0.5F - y) * across;
                float light = 0.0F;
                for (const auto& layer : kLayers) {
                    light += layer.strength * std::clamp(layer.half_width * widen + 0.5F - distance, 0.0F, 1.0F);
                }
                if (light <= 0.0F) continue;
                light *= scale;
                add_light(x, row, color.red * light, color.green * light, color.blue * light);
            }
        }
    }
}

// A soft dot of light: linear from the centre out, or squared for a glow.
void AuraAuroraView::soft_dot(float x, float y, float radius, bool squared, float red, float green, float blue,
                              float peak) noexcept {
    const int reach = static_cast<int>(std::ceil(radius));
    const int center_x = static_cast<int>(x);
    const int center_y = static_cast<int>(y);
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const float px = static_cast<float>(center_x + dx) + 0.5F - x;
            const float py = static_cast<float>(center_y + dy) + 0.5F - y;
            const float distance = std::sqrt(px * px + py * py);
            if (distance >= radius) continue;
            float light = 1.0F - distance / radius;
            if (squared) light *= light;
            light *= peak;
            add_light(center_x + dx, center_y + dy, red * light, green * light, blue * light);
        }
    }
}

// Tying into the loop (and thinking): the ribbons fold back on themselves,
// so they are drawn as points along each curve: a small core at every point,
// lit for the length it covers, and a wide glow every 0.7 of its radius. A
// light travels round the loop.
void AuraAuroraView::stamp(float drive, float strength) {
    constexpr int kSamples = 340;
    const float widen = 1.0F + 0.5F * drive;
    const float glow = 10.0F * widen;
    const float core = 2.2F * widen;
    const float loop = weights_[2];
    for (std::size_t ribbon = 0; ribbon < kRibbons.size(); ++ribbon) {
        const auto& shape = kRibbons[ribbon];
        const Rgb color = mix(weights_, kColors[ribbon]);
        const float phase = phases_[ribbon] + shape.offset;
        const float spin = seconds_ * 1.5F + static_cast<float>(ribbon) * 0.9F;
        float last_x = 0.0F;
        float last_y = 0.0F;
        float since_glow = glow;
        for (int sample = 0; sample < kSamples; ++sample) {
            const float u = static_cast<float>(sample) / static_cast<float>(kSamples - 1);
            const float envelope = std::pow(std::sin(kPi * u), 1.5F);
            const float wave_x = 12.0F + 408.0F * u;
            const float wave_y = kMiddleY + envelope * shape.sway * amplitude_ *
                                                (0.75F * std::sin(shape.waves * kTau * u + phase) +
                                                 0.35F * std::sin(2.6F * shape.waves * kTau * u - phase * 1.3F));
            const float turn = kTau * u + spin;
            const float loop_x = kMiddleX + (118.0F + 10.0F * static_cast<float>(ribbon)) * std::sin(turn);
            const float loop_y = kMiddleY + (64.0F - 6.0F * static_cast<float>(ribbon)) * std::sin(2.0F * turn);
            const float x = wave_x + (loop_x - wave_x) * loop;
            const float y = wave_y + (loop_y - wave_y) * loop;
            const float step = sample == 0 ? core : std::hypot(x - last_x, y - last_y);
            last_x = x;
            last_y = y;
            const float travelling = std::pow(std::max(0.0F, std::cos(kTau * (u - seconds_ * 0.35F))), 6.0F);
            const float scale = end_fade(x) * strength * (1.0F + 0.6F * loop * travelling);
            if (scale <= 0.0F) {
                since_glow = glow;
                continue;
            }
            soft_dot(x, y, core, false, color.red, color.green, color.blue,
                     scale * 0.95F * std::clamp(step, 0.5F, core) / core);
            since_glow += step;
            if (since_glow >= glow * 0.7F) {
                soft_dot(x, y, glow, true, color.red, color.green, color.blue, scale * 0.3F);
                since_glow = 0.0F;
            }
        }
    }
}

void AuraAuroraView::render() {
    std::fill(buffer_.begin(), buffer_.end(), std::uint8_t{0});
    const float drive = (weights_[1] + weights_[3]) * level_;
    const float strength = 0.55F + 0.45F * std::min(1.0F, drive + weights_[2] + 0.4F * weights_[0]);
    if (weights_[2] > 0.02F) stamp(drive, strength);
    else wave(drive, strength);

    // The added light becomes colour and coverage: alpha is the brightest
    // channel, so on black the band shows exactly the light that was added.
    auto* colors = reinterpret_cast<std::uint16_t*>(buffer_.data());
    auto* alpha = buffer_.data() + static_cast<std::size_t>(width * height) * 2;
    for (std::size_t index = 0; index < static_cast<std::size_t>(width * height); ++index) {
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

// The haze: tinted by the state, brighter with the voice and while thinking;
// redrawn only after a step of a few levels.
void AuraAuroraView::update_haze() {
    if (!haze_) return;
    const float drive = (weights_[1] + weights_[3]) * level_;
    const int opacity = channel(0.10F + 0.18F * drive + 0.08F * weights_[2]);
    if (std::abs(opacity - haze_opacity_) >= 3) {
        lv_obj_set_style_image_opa(haze_, static_cast<lv_opa_t>(opacity), 0);
        haze_opacity_ = opacity;
    }
    const std::uint32_t coarse = hex(mix(weights_, kHaze)) & 0xF8F8F8U;
    if (coarse != haze_color_) {
        lv_obj_set_style_image_recolor(haze_, lv_color_hex(coarse), 0);
        haze_color_ = coarse;
    }
}

}  // namespace aura::ui

#include "aura/aura_mercury_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "aura/aura_theme.hpp"

namespace aura::ui {

namespace {
constexpr float kTau = 6.2831853071795864769F;
constexpr int kBasePaths = 4;
constexpr int kPathCount = kBasePaths * 2;
constexpr int kPathSamples = 168;
constexpr float kCanvasScale = static_cast<float>(AuraMercuryView::size) / 224.0F;
constexpr float kVisualScale = 1.18F;

struct Rgb {
    float red;
    float green;
    float blue;
};

constexpr Rgb rgb(int red, int green, int blue) {
    return {static_cast<float>(red) / 255.0F, static_cast<float>(green) / 255.0F,
            static_cast<float>(blue) / 255.0F};
}

// Idle, listening, thinking and speaking. The spectral path colour remains
// visible in every state; this tone pulls the lattice towards its state colour.
constexpr std::array<Rgb, 4> kStateTone{{rgb(184, 154, 255), rgb(102, 248, 224), rgb(166, 116, 255),
                                         rgb(255, 99, 196)}};
constexpr std::array<float, 4> kSpeed{{0.32F, 0.60F, 1.42F, 0.85F}};
constexpr std::array<float, 4> kSpread{{0.14F, 1.00F, 0.48F, 0.78F}};
constexpr std::array<float, 4> kWeave{{0.26F, 0.42F, 1.00F, 0.65F}};
constexpr std::array<float, 4> kWave{{0.16F, 1.00F, 0.34F, 1.00F}};

float weighted(const std::array<float, 4>& weights, const std::array<float, 4>& values) {
    float result = 0.0F;
    for (std::size_t index = 0; index < weights.size(); ++index) result += weights[index] * values[index];
    return result;
}

Rgb weighted(const std::array<float, 4>& weights, const std::array<Rgb, 4>& values) {
    Rgb result{};
    for (std::size_t index = 0; index < weights.size(); ++index) {
        result.red += weights[index] * values[index].red;
        result.green += weights[index] * values[index].green;
        result.blue += weights[index] * values[index].blue;
    }
    return result;
}

Rgb blend(const Rgb& from, const Rgb& to, float share) {
    return {from.red + (to.red - from.red) * share, from.green + (to.green - from.green) * share,
            from.blue + (to.blue - from.blue) * share};
}

float wrap(float value) { return value - std::floor(value); }

Rgb spectrum(float position) {
    constexpr std::array<Rgb, 3> stops{{rgb(107, 247, 221), rgb(147, 124, 255), rgb(255, 91, 198)}};
    const float scaled = wrap(position) * 2.0F;
    const int index = std::min(1, static_cast<int>(scaled));
    return blend(stops[static_cast<std::size_t>(index)], stops[static_cast<std::size_t>(index + 1)],
                 scaled - static_cast<float>(index));
}

int channel(float value) { return static_cast<int>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F); }

std::uint32_t hex(const Rgb& color) {
    return static_cast<std::uint32_t>((channel(color.red) << 16) | (channel(color.green) << 8) |
                                      channel(color.blue));
}
}  // namespace

AuraMercuryView::AuraMercuryView() {
    descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB565A8;
    descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    descriptor_.header.w = size;
    descriptor_.header.h = size;
    descriptor_.header.stride = size * static_cast<int>(sizeof(std::uint16_t));
    descriptor_.data_size = buffer_.size();
    descriptor_.data = buffer_.data();
}

lv_obj_t* AuraMercuryView::attach(lv_obj_t* parent, int center_x, int center_y) {
    // The approved canvas uses a 112 px outer radial glow.  The former field
    // was wider than the complete 252 px source and read as a flat wash rather
    // than light emitted by the reactor.
    glow_ = light_field(parent, center_x, center_y, 112, 0x755CFF, LV_OPA_TRANSP);
    lv_obj_move_to_index(glow_, 0);
    glow_opacity_ = -1;
    glow_color_ = 0;
    image_ = lv_image_create(parent);
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, center_x - size / 2, center_y - size / 2);
    lv_obj_set_clickable(image_, false);
    lv_obj_set_scrollable(image_, false);
    if (reduced_motion_ || !attached_before_) {
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            weights_[index] = index == static_cast<std::size_t>(state_) ? 1.0F : 0.0F;
        }
    }
    attached_before_ = true;
    accumulator_ms_ = frame_interval_ms;
    interval_ms_ = frame_interval_ms;
    frame_count_ = 0;
    advance(0.0F);
    render();
    update_glow();
    return image_;
}

void AuraMercuryView::detach() noexcept {
    image_ = nullptr;
    glow_ = nullptr;
}

void AuraMercuryView::set_state(MercuryState state) noexcept { state_ = state; }
void AuraMercuryView::set_level(float level) noexcept { level_ = std::clamp(level, 0.0F, 1.0F); }
void AuraMercuryView::set_reduced_motion(bool reduced) noexcept { reduced_motion_ = reduced; }

std::uint32_t AuraMercuryView::paced_interval(std::uint32_t render_ms) noexcept {
    // Drawing takes at most a third of the time, and Synapse never drops below 5 FPS.
    return std::clamp<std::uint32_t>(std::min<std::uint32_t>(render_ms, 1000U) * 3U, frame_interval_ms, 200U);
}

bool AuraMercuryView::tick(std::uint32_t elapsed_ms) {
    if (!image_ || reduced_motion_) return false;
    accumulator_ms_ += elapsed_ms;
    if (accumulator_ms_ < interval_ms_) return false;
    // Preserve the fractional remainder.  The host UI normally ticks near
    // 30 Hz; clearing the accumulator forced a nominal 50 ms renderer back to
    // one frame every two host ticks (about 15 FPS).
    accumulator_ms_ -= interval_ms_;
    accumulator_ms_ = std::min<std::uint32_t>(accumulator_ms_, interval_ms_);
    advance(static_cast<float>(interval_ms_) / 1000.0F);
    // A device that needs longer per frame draws fewer of them, so the rest of
    // the UI keeps two thirds of the time.
    const std::uint32_t started = lv_tick_get();
    render();
    interval_ms_ = paced_interval(lv_tick_elaps(started));
    lv_obj_invalidate(image_);
    update_glow();
    return true;
}

void AuraMercuryView::advance(float seconds) noexcept {
    seconds_ += seconds;
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
    phase_ += seconds * speed_;
}

AuraMercuryView::Point AuraMercuryView::path_point(int path, float u) const noexcept {
    const int base = path / 2;
    const bool reflected = (path & 1) != 0;
    const float angle = u * kTau;
    const float voice = drive_ * wave_ * std::sin(angle * 8.0F - phase_ * 10.2F + static_cast<float>(base) * 0.73F);
    const float knot = std::sin(angle * 3.0F + static_cast<float>(base) * 1.17F +
                                phase_ * (0.46F + speed_ * 0.34F));
    const float counter = std::sin(angle * 5.0F - static_cast<float>(base) * 0.61F - phase_ * 0.52F);
    // Spatially this is the approved 480 px proposal at roughly two-thirds
    // scale.  The earlier production reduction was substantially smaller and
    // also removed most of the voice displacement, which made the lattice
    // look sparse and nearly static on the 480 x 480 panel.
    const float radius = 69.5F + static_cast<float>(base) * 1.43F + knot * (6.7F + weave_ * 5.3F) +
                         counter * 2.33F + voice * 14.0F;
    float x = std::cos(angle) * radius * (1.0F + spread_ * 0.15F * std::pow(std::sin(angle), 2.0F));
    float y = std::sin(angle) * radius * (0.76F + spread_ * 0.13F);
    float z = std::sin(angle * 2.0F + static_cast<float>(base) * 1.03F + phase_ * 2.05F) *
              (21.3F + weave_ * 26.7F);
    x += spread_ * 9.3F * std::sin(angle * 2.0F + static_cast<float>(base) * 0.86F);
    y += voice * 6.7F + spread_ * 5.3F * std::cos(angle * 3.0F - phase_ * 1.2F);

    const float rotate_x = 0.18F + 0.36F * std::sin(seconds_ * 0.17F + static_cast<float>(base) * 0.72F) +
                           static_cast<float>(base) * 0.066F;
    const float rotate_y = phase_ * 0.54F + static_cast<float>(base) * 0.49F;
    const float cos_y = std::cos(rotate_y);
    const float sin_y = std::sin(rotate_y);
    const float turned_x = x * cos_y + z * sin_y;
    const float turned_z = z * cos_y - x * sin_y;
    const float cos_x = std::cos(rotate_x);
    const float sin_x = std::sin(rotate_x);
    const float turned_y = y * cos_x - turned_z * sin_x;
    const float depth = y * sin_x + turned_z * cos_x;
    constexpr float focal = 260.0F;
    const float scale = focal / (focal - depth);
    Point result{static_cast<float>(size) / 2.0F + turned_x * scale * kVisualScale,
                 static_cast<float>(size) / 2.0F + turned_y * scale * kVisualScale, depth};
    if (reflected) {
        result.x = static_cast<float>(size) - result.x;
        result.y = static_cast<float>(size) - result.y;
        result.z = -result.z;
    }
    return result;
}

void AuraMercuryView::add_light(int x, int y, float red, float green, float blue) noexcept {
    if (x < 0 || x >= size || y < 0 || y >= size) return;
    const auto index = static_cast<std::size_t>(y * size + x);
    auto* red_green = buffer_.data() + index * 2;
    auto* blue_byte = buffer_.data() + static_cast<std::size_t>(size * size) * 2 + index;
    const auto add = [](std::uint8_t& target, float value) {
        target = static_cast<std::uint8_t>(
            std::min(255, static_cast<int>(target) + static_cast<int>(std::max(0.0F, value) * 255.0F)));
    };
    add(red_green[0], red);
    add(red_green[1], green);
    add(*blue_byte, blue);
}

void AuraMercuryView::soft_dot(float x, float y, float radius, float red, float green, float blue,
                               float peak) noexcept {
    const int reach = static_cast<int>(std::ceil(radius + 0.5F));
    const int center_x = static_cast<int>(x);
    const int center_y = static_cast<int>(y);
    for (int offset_y = -reach; offset_y <= reach; ++offset_y) {
        for (int offset_x = -reach; offset_x <= reach; ++offset_x) {
            const float pixel_x = static_cast<float>(center_x + offset_x) + 0.5F - x;
            const float pixel_y = static_cast<float>(center_y + offset_y) + 0.5F - y;
            const float distance = std::sqrt(pixel_x * pixel_x + pixel_y * pixel_y);
            if (distance >= radius + 0.5F) continue;
            float light = std::clamp(radius + 0.5F - distance, 0.0F, 1.0F);
            if (radius > 2.0F) light *= std::max(0.0F, 1.0F - distance / (radius + 0.5F));
            light *= peak;
            add_light(center_x + offset_x, center_y + offset_y, red * light, green * light, blue * light);
        }
    }
}

void AuraMercuryView::light_line(const Point& from, const Point& to, float radius, float red, float green,
                                 float blue, float peak) noexcept {
    const float delta_x = to.x - from.x;
    const float delta_y = to.y - from.y;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(delta_x, delta_y))));
    for (int step = 0; step <= steps; ++step) {
        const float share = static_cast<float>(step) / static_cast<float>(steps);
        soft_dot(from.x + delta_x * share, from.y + delta_y * share, radius, red, green, blue, peak);
    }
}

void AuraMercuryView::overlay_dot(float x, float y, float radius, float red, float green, float blue,
                                  float opacity) noexcept {
    const int reach = static_cast<int>(std::ceil(radius + 0.5F));
    const int center_x = static_cast<int>(x);
    const int center_y = static_cast<int>(y);
    for (int offset_y = -reach; offset_y <= reach; ++offset_y) {
        for (int offset_x = -reach; offset_x <= reach; ++offset_x) {
            const int target_x = center_x + offset_x;
            const int target_y = center_y + offset_y;
            if (target_x < 0 || target_x >= size || target_y < 0 || target_y >= size) continue;
            const float pixel_x = static_cast<float>(target_x) + 0.5F - x;
            const float pixel_y = static_cast<float>(target_y) + 0.5F - y;
            const float distance = std::hypot(pixel_x, pixel_y);
            if (distance >= radius + 0.5F) continue;
            float coverage = std::clamp(radius + 0.5F - distance, 0.0F, 1.0F);
            if (radius > 1.0F) coverage *= std::max(0.0F, 1.0F - distance / (radius + 0.5F));
            const float blend_amount = std::clamp(opacity * coverage, 0.0F, 1.0F);
            const auto index = static_cast<std::size_t>(target_y * size + target_x);
            const auto composite = [blend_amount](std::uint8_t& destination, float source) {
                destination = static_cast<std::uint8_t>(
                    std::clamp(static_cast<float>(destination) * (1.0F - blend_amount) +
                                   source * 255.0F * blend_amount,
                               0.0F, 255.0F) +
                    0.5F);
            };
            composite(buffer_[index * 2], red);
            composite(buffer_[index * 2 + 1], green);
            composite(buffer_[static_cast<std::size_t>(size * size) * 2 + index], blue);
        }
    }
}

void AuraMercuryView::overlay_line(const Point& from, const Point& to, float radius, float red, float green,
                                   float blue, float opacity) noexcept {
    const float delta_x = to.x - from.x;
    const float delta_y = to.y - from.y;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(delta_x, delta_y))));
    for (int step = 0; step <= steps; ++step) {
        const float share = static_cast<float>(step) / static_cast<float>(steps);
        overlay_dot(from.x + delta_x * share, from.y + delta_y * share, radius, red, green, blue, opacity);
    }
}

void AuraMercuryView::clear_core(float radius) noexcept {
    const int center = size / 2;
    const int reach = static_cast<int>(std::ceil(radius));
    for (int y = center - reach; y <= center + reach; ++y) {
        for (int x = center - reach; x <= center + reach; ++x) {
            const float distance = std::hypot(static_cast<float>(x - center), static_cast<float>(y - center));
            if (distance > radius) continue;
            const float position = distance / radius;
            const float inner_stop = std::min(0.30F, 12.0F / radius);
            float darkness = 0.12F;
            if (position > inner_stop && position <= 0.46F) {
                darkness = 0.12F + (position - inner_stop) / (0.46F - inner_stop) * 0.16F;
            } else if (position > 0.46F && position <= 0.72F) {
                darkness = 0.28F + (position - 0.46F) / 0.26F * 0.56F;
            } else if (position > 0.72F) {
                darkness = 0.84F * (1.0F - (position - 0.72F) / 0.28F);
            }
            const float transmission = 1.0F - std::clamp(darkness, 0.0F, 0.84F);
            const auto index = static_cast<std::size_t>(y * size + x);
            buffer_[index * 2] =
                static_cast<std::uint8_t>(static_cast<float>(buffer_[index * 2]) * transmission);
            buffer_[index * 2 + 1] =
                static_cast<std::uint8_t>(static_cast<float>(buffer_[index * 2 + 1]) * transmission);
            auto& blue = buffer_[static_cast<std::size_t>(size * size) * 2 + index];
            blue = static_cast<std::uint8_t>(static_cast<float>(blue) * transmission);
        }
    }
}

void AuraMercuryView::draw_atmosphere(float drive) {
    const float center = static_cast<float>(size) / 2.0F;
    const Rgb tone = weighted(weights_, kStateTone);
    const float drift = std::sin(seconds_ * 0.22F) * 5.0F;
    const float lift = std::cos(seconds_ * 0.18F) * 4.0F;
    constexpr float violet_radius = 80.0F * kCanvasScale;
    constexpr float teal_radius = 72.0F * kCanvasScale;
    constexpr float reactor_outer_radius = 112.0F;
    constexpr float reactor_inner_radius = 58.0F;
    const float violet_x = center - 30.0F * kCanvasScale + drift;
    const float violet_y = center - 10.0F * kCanvasScale;
    const float teal_x = center + 34.0F * kCanvasScale;
    const float teal_y = center + 8.0F * kCanvasScale + lift;
    const Rgb reactor_outer = blend(rgb(104, 78, 255), tone, 0.08F);
    const Rgb reactor_inner = rgb(85, 247, 222);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float violet_dx = static_cast<float>(x) + 0.5F - violet_x;
            const float violet_dy = static_cast<float>(y) + 0.5F - violet_y;
            const float teal_dx = static_cast<float>(x) + 0.5F - teal_x;
            const float teal_dy = static_cast<float>(y) + 0.5F - teal_y;
            const float core_dx = static_cast<float>(x) + 0.5F - center;
            const float core_dy = static_cast<float>(y) + 0.5F - center;
            const float core_distance = std::sqrt(core_dx * core_dx + core_dy * core_dy);
            float violet = 1.0F - (violet_dx * violet_dx + violet_dy * violet_dy) /
                                      (violet_radius * violet_radius);
            float teal = 1.0F - (teal_dx * teal_dx + teal_dy * teal_dy) / (teal_radius * teal_radius);
            float reactor_violet = std::max(0.0F, 1.0F - core_distance / reactor_outer_radius);
            float reactor_teal = std::max(0.0F, 1.0F - core_distance / reactor_inner_radius);
            violet = std::max(0.0F, violet);
            teal = std::max(0.0F, teal);
            violet = violet * violet * (0.115F + weights_[2] * 0.035F + drive * 0.025F);
            teal = teal * teal * (0.080F + drive * 0.040F);
            reactor_violet *= 0.14F + drive * 0.08F;
            reactor_teal *= 0.20F + drive * 0.14F;
            if (violet <= 0.0F && teal <= 0.0F && reactor_violet <= 0.0F && reactor_teal <= 0.0F) continue;
            add_light(x, y,
                      tone.red * violet * 0.42F + teal * 0.08F + reactor_outer.red * reactor_violet +
                          reactor_inner.red * reactor_teal,
                      tone.green * violet * 0.18F + teal * 0.72F + reactor_outer.green * reactor_violet +
                          reactor_inner.green * reactor_teal,
                      tone.blue * violet * 0.82F + teal * 0.64F + reactor_outer.blue * reactor_violet +
                          reactor_inner.blue * reactor_teal);
        }
    }
}

void AuraMercuryView::draw_orbits(float drive) {
    const float center = static_cast<float>(size) / 2.0F;
    const Rgb tone = weighted(weights_, kStateTone);
    constexpr int arc_samples = 120;
    for (int ring = 0; ring < 5; ++ring) {
        const float radius = (84.0F + static_cast<float>(ring) * 4.0F + drive * 2.0F) * kCanvasScale;
        const float rotation = phase_ * (ring % 2 == 0 ? 0.29F : -0.38F) + static_cast<float>(ring) * 0.69F;
        Point previous{};
        bool has_previous = false;
        for (int sample = 0; sample <= arc_samples; ++sample) {
            const float angle = static_cast<float>(sample) / static_cast<float>(arc_samples) * kTau;
            const float oval_y = std::sin(angle) * radius * (0.66F + 0.034F * static_cast<float>(ring));
            const float oval_x = std::cos(angle) * radius;
            const Point point{center + oval_x * std::cos(rotation) - oval_y * std::sin(rotation),
                              center + oval_x * std::sin(rotation) + oval_y * std::cos(rotation), 0.0F};
            const bool visible = ((sample + ring * 7) % (27 + ring * 3)) < (10 + ring * 2);
            if (visible && has_previous) {
                const Rgb color = blend(spectrum(static_cast<float>(ring) * 0.19F + angle / kTau), tone, 0.12F);
                light_line(previous, point, ring == 0 ? 0.72F : 0.48F, color.red, color.green, color.blue,
                           0.12F + weave_ * 0.055F);
            }
            has_previous = visible;
            previous = point;
        }
    }

    constexpr int ticks = 84;
    const float active = weights_[1] + weights_[3];
    const float signal = std::clamp(drive + (1.0F - active) * weave_ * 0.18F, 0.0F, 1.0F);
    for (int index = 0; index < ticks; ++index) {
        const float angle = static_cast<float>(index) / static_cast<float>(ticks) * kTau + phase_ * 0.16F;
        const float texture = 0.5F + 0.5F * std::sin(static_cast<float>(index) * 1.69F + seconds_ * 2.8F);
        const float radius = (84.0F + spread_ * 4.0F + signal * 5.3F *
                                                          std::sin(static_cast<float>(index) * 0.68F - seconds_ * 3.6F)) *
                             kCanvasScale;
        const float length = (2.0F + signal * (6.0F + 14.0F * texture)) * kCanvasScale;
        const Point from{center + std::cos(angle) * radius, center + std::sin(angle) * radius * 0.91F, 0.0F};
        const Point to{center + std::cos(angle) * (radius + length),
                       center + std::sin(angle) * (radius + length) * 0.91F, 0.0F};
        const Rgb color = blend(spectrum(static_cast<float>(index) / static_cast<float>(ticks) * 0.72F), tone, 0.14F);
        light_line(from, to, 0.58F + signal * 0.72F, color.red, color.green, color.blue,
                   0.10F + signal * 0.50F);
    }

    const float wave_strength = 0.04F + drive * 0.26F + weights_[2] * 0.08F;
    for (int ring = 0; ring < 3; ++ring) {
        const float age = wrap(seconds_ * (0.12F + speed_ * 0.045F) + static_cast<float>(ring) / 3.0F);
        const float radius = (66.0F + age * 39.0F + drive * 2.5F) * kCanvasScale;
        Point previous{};
        bool has_previous = false;
        for (int sample = 0; sample <= arc_samples; ++sample) {
            const float angle = static_cast<float>(sample) / static_cast<float>(arc_samples) * kTau;
            const Point point{center + std::cos(angle) * radius,
                              center + std::sin(angle) * radius * (0.88F + 0.035F * std::sin(seconds_ + ring)),
                              0.0F};
            const bool visible = ((sample + ring * 5) % 10) < 4;
            if (visible && has_previous) {
                const Rgb color = blend(spectrum(0.12F + static_cast<float>(ring) * 0.22F + age * 0.16F), tone,
                                        0.14F);
                light_line(previous, point, 0.5F, color.red, color.green, color.blue,
                           (1.0F - age) * wave_strength);
            }
            has_previous = visible;
            previous = point;
        }
    }
}

void AuraMercuryView::draw_paths(float drive) {
    const Rgb tone = weighted(weights_, kStateTone);
    for (int path = 0; path < kPathCount; ++path) {
        Point previous = path_point(path, 0.0F);
        for (int sample = 1; sample <= kPathSamples; ++sample) {
            const float u = static_cast<float>(sample) / static_cast<float>(kPathSamples);
            const Point point = path_point(path, u);
            const float front = std::clamp((point.z + 80.0F) / 160.0F, 0.0F, 1.0F);
            const Rgb color = blend(spectrum(u * 0.58F + static_cast<float>(path) * 0.085F + phase_ * 0.024F),
                                    tone, 0.14F);
            if ((sample & 1) == 0) {
                soft_dot(point.x, point.y, 4.8F + front * 1.3F + drive * 0.6F, color.red, color.green, color.blue,
                         0.060F + front * 0.065F);
            }
            if (sample % 3 == 0) {
                soft_dot(point.x, point.y, 2.2F + front * 0.8F, color.red, color.green, color.blue,
                         0.055F + front * 0.075F);
            }
            light_line(previous, point, 0.56F + front * 0.72F, color.red, color.green, color.blue,
                       0.22F + front * 0.76F);
            if (front > 0.74F && sample % 7 == 0) {
                soft_dot(point.x, point.y, 1.6F + drive * 0.5F, 0.88F, 0.97F, 1.0F,
                         (front - 0.74F) * 1.6F);
            }
            previous = point;
        }
    }

    // A translucent web between adjacent paths is a defining feature of the
    // approved proposal.  It is deliberately dimmer than the eight main paths
    // but much denser than the former four-spoke approximation.
    for (int path = 0; path < kPathCount - 1; ++path) {
        for (int sample = 7; sample < kPathSamples; sample += 9) {
            const float u = static_cast<float>(sample) / static_cast<float>(kPathSamples);
            const Point from = path_point(path, u);
            const Point to = path_point(path + 1, u);
            const float front = std::clamp(((from.z + to.z) * 0.5F + 90.0F) / 180.0F, 0.0F, 1.0F);
            const Rgb color = blend(spectrum(u * 0.52F + static_cast<float>(path) * 0.095F), tone, 0.16F);
            light_line(from, to, 0.46F, color.red, color.green, color.blue, 0.055F + front * 0.160F);
        }
    }

    const int particle_count = weights_[2] > 0.5F ? 30 : 18;
    for (int particle = 0; particle < particle_count; ++particle) {
        const int path = particle % kPathCount;
        const float progress = wrap(seconds_ * speed_ * (0.070F + static_cast<float>(particle % 5) * 0.009F) +
                                    static_cast<float>(particle) * 0.113F);
        const Point head = path_point(path, progress);
        const float front = std::clamp((head.z + 100.0F) / 200.0F, 0.0F, 1.0F);
        const Rgb color = blend(spectrum(progress * 0.68F + static_cast<float>(path) * 0.08F), tone, 0.12F);
        for (int trail = 10; trail >= 0; --trail) {
            const float u = wrap(progress - static_cast<float>(trail) * 0.009F);
            const Point point = path_point(path, u);
            const float share = 1.0F - static_cast<float>(trail) / 11.0F;
            soft_dot(point.x, point.y, 0.7F + share * (0.9F + front), color.red, color.green, color.blue,
                     0.07F + share * (0.22F + front * 0.36F));
        }
        soft_dot(head.x, head.y, 7.5F + front * 6.0F, color.red, color.green, color.blue,
                 0.080F + front * 0.130F);
        soft_dot(head.x, head.y, 1.0F + front * 1.3F, color.red, color.green, color.blue,
                 0.42F + front * 0.48F);
    }
}

void AuraMercuryView::draw_reactor(float drive) {
    const float center = static_cast<float>(size) / 2.0F;
    const Rgb tone = weighted(weights_, kStateTone);
    const float aperture = 58.0F + spread_ * 8.0F + drive * 9.0F;
    const float sphere = 20.0F + drive * 5.0F + spread_ * 1.5F;
    clear_core(aperture);

    // RGB565 compresses the two browser radial glows into a dark band once the
    // aperture lens is applied. Restore their visible cyan-violet corona only
    // outside the sphere, with a smooth crest at 36 px and no hard ring.
    constexpr float corona_crest = 36.0F;
    constexpr float corona_outer = 68.0F;
    const Rgb corona_color = blend(blend(rgb(85, 247, 222), rgb(104, 78, 255), 0.55F), tone, 0.12F);
    const float corona_peak = 0.28F + drive * 0.08F;
    const int corona_reach = static_cast<int>(std::ceil(corona_outer));
    for (int offset_y = -corona_reach; offset_y <= corona_reach; ++offset_y) {
        for (int offset_x = -corona_reach; offset_x <= corona_reach; ++offset_x) {
            const float distance = std::hypot(static_cast<float>(offset_x), static_cast<float>(offset_y));
            if (distance <= sphere || distance >= corona_outer) continue;
            float strength = distance < corona_crest ? (distance - sphere) / (corona_crest - sphere)
                                                     : 1.0F - (distance - corona_crest) /
                                                                  (corona_outer - corona_crest);
            strength = std::clamp(strength, 0.0F, 1.0F);
            strength = strength * strength * (3.0F - 2.0F * strength) * corona_peak;
            add_light(size / 2 + offset_x, size / 2 + offset_y, corona_color.red * strength,
                      corona_color.green * strength, corona_color.blue * strength);
        }
    }

    for (int ring = 0; ring < 4; ++ring) {
        const float radius = aperture - 9.0F + static_cast<float>(ring) * 11.0F;
        const float start = phase_ * (ring % 2 == 0 ? 0.88F : -0.72F) +
                            static_cast<float>(ring) * 1.19F + 0.18F + static_cast<float>(ring) * 0.34F;
        const float sweep = 0.94F + drive * 0.65F;
        Point previous{};
        constexpr int samples = 22;
        for (int sample = 0; sample <= samples; ++sample) {
            const float angle = start + static_cast<float>(sample) / static_cast<float>(samples) * sweep;
            const float oval = 0.58F + static_cast<float>(ring) * 0.09F;
            const Point point{center + std::cos(angle) * radius, center + std::sin(angle) * radius * oval, 0.0F};
            if (sample > 0) {
                const Rgb color = blend(spectrum(static_cast<float>(ring) * 0.19F + phase_ * 0.04F), tone, 0.22F);
                light_line(previous, point, 0.55F + drive * 0.85F, color.red, color.green, color.blue,
                           0.34F + drive * 0.50F);
            }
            previous = point;
        }
    }

    // Keep the approved white centre broad enough to read on RGB565.  Directly
    // sampling the browser stops made their 15% white band collapse to a pin
    // light after quantisation; these intermediate cyan stops preserve the
    // same visual gradient and measured radial luminance on the 480 px output.
    constexpr std::array<float, 6> stops{{0.0F, 0.28F, 0.52F, 0.74F, 0.90F, 1.0F}};
    constexpr std::array<Rgb, 6> colors{{rgb(245, 255, 255), rgb(184, 255, 241), rgb(112, 235, 226),
                                         rgb(72, 126, 180), rgb(55, 105, 150), rgb(4, 2, 13)}};
    constexpr std::array<float, 6> opacities{{1.0F, 0.98F, 0.90F, 0.94F, 0.98F, 1.0F}};
    const int reach = static_cast<int>(std::ceil(sphere));
    for (int offset_y = -reach; offset_y <= reach; ++offset_y) {
        for (int offset_x = -reach; offset_x <= reach; ++offset_x) {
            const float distance = std::hypot(static_cast<float>(offset_x), static_cast<float>(offset_y));
            if (distance > sphere) continue;
            const float position = distance / sphere;
            std::size_t segment = 0;
            while (segment + 1 < stops.size() - 1 && position > stops[segment + 1]) ++segment;
            const float share = (position - stops[segment]) / (stops[segment + 1] - stops[segment]);
            const Rgb color = blend(colors[segment], colors[segment + 1], share);
            const float opacity = opacities[segment] + (opacities[segment + 1] - opacities[segment]) * share;
            const auto index = static_cast<std::size_t>((size / 2 + offset_y) * size + size / 2 + offset_x);
            const auto composite = [opacity](std::uint8_t& destination, float source) {
                const float old = static_cast<float>(destination) / 255.0F;
                destination = static_cast<std::uint8_t>(
                    std::clamp(source * opacity + old * (1.0F - opacity), 0.0F, 1.0F) * 255.0F + 0.5F);
            };
            composite(buffer_[index * 2], color.red);
            composite(buffer_[index * 2 + 1], color.green);
            composite(buffer_[static_cast<std::size_t>(size * size) * 2 + index], color.blue);
        }
    }
    Point ring_previous{center + sphere, center, 0.0F};
    constexpr int core_ring_samples = 48;
    for (int sample = 1; sample <= core_ring_samples; ++sample) {
        const float angle = static_cast<float>(sample) / static_cast<float>(core_ring_samples) * kTau;
        const Point ring_point{center + std::cos(angle) * sphere, center + std::sin(angle) * sphere, 0.0F};
        const Rgb ring_color = blend(spectrum(0.14F + phase_ * 0.03F), tone, 0.10F);
        light_line(ring_previous, ring_point, 0.64F, ring_color.red, ring_color.green, ring_color.blue, 0.72F);
        ring_previous = ring_point;
    }
    soft_dot(center - sphere * 0.24F, center - sphere * 0.27F, 7.0F, 0.92F, 1.0F, 1.0F, 0.16F);
    soft_dot(center, center, 12.0F, 0.96F, 1.0F, 1.0F, 0.48F);
}

void AuraMercuryView::draw_foreground_paths(float drive) {
    // The full lattice is laid down before the reactor, forming its rear
    // hemisphere. Redraw only the z-positive strands where they cross the
    // nucleus. This is a deliberate depth pass: the core remains luminous,
    // but is visibly enclosed by the network instead of pasted over it.
    const float center = static_cast<float>(size) / 2.0F;
    const float sphere = 20.0F + drive * 5.0F + spread_ * 1.5F;
    const float integration_radius = sphere + 8.0F;
    const Rgb tone = weighted(weights_, kStateTone);
    for (int path = 0; path < kPathCount; ++path) {
        Point previous = path_point(path, 0.0F);
        for (int sample = 1; sample <= kPathSamples; ++sample) {
            const float u = static_cast<float>(sample) / static_cast<float>(kPathSamples);
            const Point point = path_point(path, u);
            const float midpoint_x = (previous.x + point.x) * 0.5F - center;
            const float midpoint_y = (previous.y + point.y) * 0.5F - center;
            const float distance = std::hypot(midpoint_x, midpoint_y);
            const float depth = (previous.z + point.z) * 0.5F;
            if (depth > 0.0F && distance < integration_radius) {
                const float edge_fade = std::clamp((integration_radius - distance) / 8.0F, 0.0F, 1.0F);
                const float front = std::clamp((depth + 12.0F) / 72.0F, 0.0F, 1.0F);
                const Rgb color = blend(
                    spectrum(u * 0.58F + static_cast<float>(path) * 0.085F + phase_ * 0.024F), tone, 0.10F);
                light_line(previous, point, 0.62F + front * 0.62F, color.red, color.green, color.blue,
                           edge_fade * (0.22F + front * 0.52F));
                if (sample % 5 == 0) {
                    soft_dot(point.x, point.y, 1.1F + front * 0.7F, 0.86F, 0.98F, 1.0F,
                             edge_fade * (0.10F + front * 0.22F));
                }
            }
            previous = point;
        }
    }

    // Each point-reflected pair represents one strand passing behind and then
    // in front of the reactor. Keep two restrained front half-chords visible
    // at every phase. Their directions come from the live 3D path geometry,
    // so they rotate with the lattice instead of looking like static spokes.
    for (int filament = 0; filament < 2; ++filament) {
        const int pair = filament * 2;
        const float u = wrap(0.17F + static_cast<float>(filament) * 0.31F + phase_ * 0.035F);
        const Point first = path_point(pair, u);
        const Point second = path_point(pair + 1, u);
        const Point& foreground = first.z >= second.z ? first : second;
        float direction_x = foreground.x - center;
        float direction_y = foreground.y - center;
        const float length = std::max(1.0F, std::hypot(direction_x, direction_y));
        direction_x /= length;
        direction_y /= length;
        const float perpendicular_x = -direction_y;
        const float perpendicular_y = direction_x;
        const float bend = std::sin(phase_ * 0.7F + static_cast<float>(filament) * 2.1F) * 2.2F;
        const Point entry{center - direction_x * sphere * 0.62F + perpendicular_x * bend * 0.45F,
                          center - direction_y * sphere * 0.62F + perpendicular_y * bend * 0.45F, 0.0F};
        const Point middle{center + direction_x * sphere * 0.18F + perpendicular_x * bend,
                           center + direction_y * sphere * 0.18F + perpendicular_y * bend, 0.0F};
        const Point exit{center + direction_x * integration_radius + perpendicular_x * bend * 0.25F,
                         center + direction_y * integration_radius + perpendicular_y * bend * 0.25F, 0.0F};
        const Rgb color = blend(spectrum(u * 0.58F + static_cast<float>(pair) * 0.085F), tone, 0.08F);
        const float opacity = 0.44F + drive * 0.10F;
        overlay_line(entry, middle, 1.05F + drive * 0.22F, color.red, color.green, color.blue, opacity);
        overlay_line(middle, exit, 1.05F + drive * 0.22F, color.red, color.green, color.blue, opacity * 0.86F);
    }
}

void AuraMercuryView::finalize() {
    auto* colors = reinterpret_cast<std::uint16_t*>(buffer_.data());
    auto* alpha = buffer_.data() + static_cast<std::size_t>(size * size) * 2;
    int lit_radius = -1;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const auto index = static_cast<std::size_t>(y * size + x);
            const float dx = static_cast<float>(x) + 0.5F - static_cast<float>(size) / 2.0F;
            const float dy = static_cast<float>(y) + 0.5F - static_cast<float>(size) / 2.0F;
            const float distance = std::sqrt(dx * dx + dy * dy);
            float vignette = std::clamp((105.0F * kCanvasScale - distance) / (8.0F * kCanvasScale), 0.0F, 1.0F);
            vignette = vignette * vignette * (3.0F - 2.0F * vignette);
            const int red = static_cast<int>(static_cast<float>(buffer_[index * 2]) * vignette);
            const int green = static_cast<int>(static_cast<float>(buffer_[index * 2 + 1]) * vignette);
            const int blue = static_cast<int>(static_cast<float>(alpha[index]) * vignette);
            const int coverage = std::max(red, std::max(green, blue));
            if (coverage == 0) {
                colors[index] = 0;
                alpha[index] = 0;
                continue;
            }
            const auto normalized_red = static_cast<unsigned>(red * 255 / coverage);
            const auto normalized_green = static_cast<unsigned>(green * 255 / coverage);
            const auto normalized_blue = static_cast<unsigned>(blue * 255 / coverage);
            colors[index] = static_cast<std::uint16_t>(((normalized_red & 0xF8U) << 8) |
                                                        ((normalized_green & 0xFCU) << 3) |
                                                        (normalized_blue >> 3));
            alpha[index] = static_cast<std::uint8_t>(coverage);
            if (coverage >= 3) {
                lit_radius = std::max(lit_radius, std::max(std::abs(x - size / 2), std::abs(y - size / 2)));
            }
        }
    }
    lit_center_x_ = size / 2;
    lit_center_y_ = size / 2;
    if (lit_radius >= 0) {
        lit_extent_ = lit_radius * 2 + 1;
    } else {
        lit_extent_ = 0;
    }
}

void AuraMercuryView::render() {
    std::fill(buffer_.begin(), buffer_.end(), std::uint8_t{0});
    draw_atmosphere(drive_);
    draw_orbits(drive_);
    draw_paths(drive_);
    draw_reactor(drive_);
    draw_foreground_paths(drive_);
    finalize();
    ++frame_count_;
}

void AuraMercuryView::update_glow() {
    if (!glow_) return;
    const Rgb tone = weighted(weights_, kStateTone);
    const int opacity = channel(0.025F + 0.045F * drive_ + 0.025F * weights_[2]);
    if (std::abs(opacity - glow_opacity_) >= 3) {
        lv_obj_set_style_image_opa(glow_, static_cast<lv_opa_t>(opacity), 0);
        glow_opacity_ = opacity;
    }
    const std::uint32_t coarse = hex(tone) & 0xF8F8F8U;
    if (coarse != glow_color_) {
        lv_obj_set_style_image_recolor(glow_, lv_color_hex(coarse), 0);
        glow_color_ = coarse;
    }
}

std::uint32_t AuraMercuryView::frame_signature() const noexcept {
    std::uint32_t hash = 2166136261U;
    for (const auto byte : buffer_) {
        hash ^= byte;
        hash *= 16777619U;
    }
    return hash;
}

}  // namespace aura::ui

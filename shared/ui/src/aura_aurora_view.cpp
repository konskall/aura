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
constexpr std::size_t kBandBytes = static_cast<std::size_t>(AuraAuroraView::width) * AuraAuroraView::height * 3;

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

// Light added to one pixel's red, green and blue bytes, as add_light() adds it.
// A loop dot's light by its squared distance's share of its radius squared,
// in 1024 steps and one past, 0..4096: a core's 1 - d / r, a glow's square of
// it. A pixel reads it between two steps: no root a pixel on the board.
struct DotFalloff {
    std::array<std::uint16_t, 1026> core{};
    std::array<std::uint16_t, 1026> glow{};
};

const DotFalloff& dot_falloff() {
    static const DotFalloff tables = [] {
        DotFalloff falloff;
        for (std::size_t step = 0; step < falloff.core.size(); ++step) {
            const float share = std::min(1.0F, static_cast<float>(step) / 1024.0F);
            const float value = 1.0F - std::sqrt(share);
            falloff.core[step] = static_cast<std::uint16_t>(value * 4096.0F + 0.5F);
            falloff.glow[step] = static_cast<std::uint16_t>(value * value * 4096.0F + 0.5F);
        }
        return falloff;
    }();
    return tables;
}

inline void add_bytes(std::uint8_t* target, float red, float green, float blue) noexcept {
    target[0] = static_cast<std::uint8_t>(std::min(255, static_cast<int>(target[0]) + static_cast<int>(red * 255.0F)));
    target[1] =
        static_cast<std::uint8_t>(std::min(255, static_cast<int>(target[1]) + static_cast<int>(green * 255.0F)));
    target[2] = static_cast<std::uint8_t>(std::min(255, static_cast<int>(target[2]) + static_cast<int>(blue * 255.0F)));
}
}  // namespace

AuraAuroraView::AuraAuroraView() : AuraAuroraView(default_presence_profile(), nullptr) { shared_worker_ = true; }

AuraAuroraView::AuraAuroraView(PresenceProfile profile, FrameWorker* worker) : profile_(profile) {
    if (profile_ == PresenceProfile::Device) {
        device_ = std::make_unique<DeviceFrame>(tokens::display_width, tokens::display_height,
                                                &AuraAuroraView::draw_device, this, &AuraAuroraView::begin_device);
        device_->set_worker(worker);
        for (auto& row : rows_) row = std::make_unique<std::uint8_t[]>(static_cast<std::size_t>(tokens::display_width) * 3);
        for (auto& light : lights_) light = std::make_unique<std::uint8_t[]>(static_cast<std::size_t>(kStripRows) * width * 3);
        along_ = std::make_unique<float[]>(width);
        next_ = std::make_unique<float[]>(width);
        fade_ = std::make_unique<float[]>(width);
        envelope_ = std::make_unique<float[]>(width);
        next_envelope_ = std::make_unique<float[]>(width);
        curve_ = std::make_unique<float[]>(kRibbons.size() * width);
        across_ = std::make_unique<float[]>(kRibbons.size() * width);
        tops_ = std::make_unique<std::int16_t[]>(kRibbons.size() * width);
        bottoms_ = std::make_unique<std::int16_t[]>(kRibbons.size() * width);
        for (int x = 0; x < width; ++x) {
            fade_[x] = end_fade(static_cast<float>(x) + 0.5F);
            along_[x] = (static_cast<float>(x) + 0.5F - 12.0F) / 408.0F;
            if (fade_[x] <= 0.0F || along_[x] <= 0.0F || along_[x] >= 1.0F) continue;
            next_[x] = std::min(0.9999F, along_[x] + 1.0F / 408.0F);
            envelope_[x] = std::pow(std::sin(kPi * along_[x]), 1.5F);
            next_envelope_[x] = std::pow(std::sin(kPi * next_[x]), 1.5F);
        }
        dots_.reserve(1400);
    } else {
        buffer_ = std::make_unique<std::uint8_t[]>(kBandBytes);
        descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor_.header.cf = LV_COLOR_FORMAT_RGB565A8;
        descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
        descriptor_.header.w = width;
        descriptor_.header.h = height;
        descriptor_.header.stride = width * static_cast<int>(sizeof(std::uint16_t));
        descriptor_.data_size = kBandBytes;
        descriptor_.data = buffer_.get();
    }
}

AuraAuroraView::~AuraAuroraView() {
    if (device_) device_->stop();
}

std::size_t AuraAuroraView::buffer_bytes() const noexcept {
    return device_ ? device_->buffer_bytes() : kBandBytes;
}

lv_obj_t* AuraAuroraView::attach(lv_obj_t* parent, int center_x, int center_y) {
    if (device_) return attach_device(parent, center_x, center_y);
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
    if (device_) device_->detach();
    image_ = nullptr;
    haze_ = nullptr;
}

void AuraAuroraView::set_state(PresenceState state) noexcept { state_ = state; }
void AuraAuroraView::set_level(float level) noexcept { level_ = std::clamp(level, 0.0F, 1.0F); }
void AuraAuroraView::set_reduced_motion(bool reduced) noexcept { reduced_motion_ = reduced; }

bool AuraAuroraView::tick(std::uint32_t elapsed_ms) {
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
    update_haze();
    return true;
}

std::uint32_t AuraAuroraView::next_tick_ms() const noexcept {
    if (!device_ || reduced_motion_) return no_tick;
    return device_->next_tick_ms();
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
    auto* red_green = buffer_.get() + index * 2;
    auto* blue_byte = buffer_.get() + static_cast<std::size_t>(width * height) * 2 + index;
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
    std::fill(buffer_.get(), buffer_.get() + kBandBytes, std::uint8_t{0});
    const float drive = (weights_[1] + weights_[3]) * level_;
    const float strength = 0.55F + 0.45F * std::min(1.0F, drive + weights_[2] + 0.4F * weights_[0]);
    if (weights_[2] > 0.02F) stamp(drive, strength);
    else wave(drive, strength);

    // The added light becomes colour and coverage: alpha is the brightest
    // channel, so on black the band shows exactly the light that was added.
    auto* colors = reinterpret_cast<std::uint16_t*>(buffer_.get());
    auto* alpha = buffer_.get() + static_cast<std::size_t>(width * height) * 2;
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

// The board: the whole screen, drawn whole on a first visit or around a new
// centre; a return shows the last frame and the worker draws the next.
lv_obj_t* AuraAuroraView::attach_device(lv_obj_t* parent, int center_x, int center_y) {
    if (shared_worker_ && !device_->worker()) device_->set_worker(FrameWorker::shared());
    if (shared_worker_ && !device_->helper()) device_->set_helper(FrameWorker::helper());
    device_->attach(parent, 0, 0);
    if (!placed_ || center_x != center_x_ || center_y != center_y_) device_->invalidate_all();
    placed_ = true;
    center_x_ = center_x;
    center_y_ = center_y;
    // The frame's strips follow the band's eight rows, each one of its strips of light.
    device_->align_strips(center_y - height / 2);
    image_ = device_->object();
    // The screen's light fields are drawn into the frame, above the haze.
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

FrameLight AuraAuroraView::haze_light() const noexcept {
    const float drive = (weights_[1] + weights_[3]) * level_;
    FrameLight light;
    light.center_x = center_x_;
    light.center_y = center_y_;
    light.radius = haze_radius;
    light.tint = hex(mix(weights_, kHaze)) & 0xF8F8F8U;
    light.opacity = static_cast<std::uint8_t>(channel(0.10F + 0.18F * drive + 0.08F * weights_[2]));
    return light;
}

// What the worker draws: the motion's state as render() reads it, and the haze.
void AuraAuroraView::prepare_device() {
    job_.weights = weights_;
    job_.phases = phases_;
    job_.amplitude = amplitude_;
    job_.seconds = seconds_;
    job_.drive = (weights_[1] + weights_[3]) * level_;
    job_.strength = 0.55F + 0.45F * std::min(1.0F, job_.drive + weights_[2] + 0.4F * weights_[0]);
    job_.loop = weights_[2] > 0.02F;
    // As the presentation's haze: a step of a few levels before it brightens.
    FrameLight haze = haze_light();
    if (haze_frame_.radius == haze.radius && std::abs(haze.opacity - haze_frame_.opacity) < 3) {
        haze.opacity = haze_frame_.opacity;
    }
    haze_frame_ = haze;
}

// The frame's changes: the band, and the whole screen when the haze
// brightened or took another state's colour.
bool AuraAuroraView::start_device_frame() {
    const FrameLight before = haze_frame_;
    prepare_device();
    FrameRect dirty{center_x_ - width / 2, center_y_ - height / 2, center_x_ + width / 2, center_y_ + height / 2};
    if (haze_frame_ != before) dirty = device_->area();
    return device_->start(dirty);
}

// On the worker: each ribbon's curve at every band column, as wave() finds
// it (device_sin for the board's sinf from flash), the band rows its layers
// light there, and the four layers' light by the distance across a curve.
void AuraAuroraView::device_curves() noexcept {
    const float widen = 1.0F + 0.5F * job_.drive;
    const float reach = kLayers[0].half_width * widen + 1.0F;
    for (int step = 0; step < kLayerSteps; ++step) {
        const float distance = static_cast<float>(step) * (1.0F / 16.0F);
        float value = 0.0F;
        for (const auto& layer : kLayers) {
            value += layer.strength * std::clamp(layer.half_width * widen + 0.5F - distance, 0.0F, 1.0F);
        }
        layers_[static_cast<std::size_t>(step)] = value;
    }
    for (std::size_t ribbon = 0; ribbon < kRibbons.size(); ++ribbon) {
        const auto& shape = kRibbons[ribbon];
        const float phase = job_.phases[ribbon] + shape.offset;
        float* heights = curve_.get() + ribbon * width;
        float* acrosses = across_.get() + ribbon * width;
        std::int16_t* tops = tops_.get() + ribbon * width;
        std::int16_t* bottoms = bottoms_.get() + ribbon * width;
        const float sway = shape.sway * job_.amplitude;
        const float waves = shape.waves * kTau;
        const auto curve = [&](float u, float envelope) {
            return kMiddleY + envelope * sway *
                                  (0.75F * device_sin(waves * u + phase) +
                                   0.35F * device_sin(2.6F * waves * u - phase * 1.3F));
        };
        for (int x = 0; x < width; ++x) {
            if (fade_[x] <= 0.0F || along_[x] <= 0.0F || along_[x] >= 1.0F) continue;
            const float y = curve(along_[x], envelope_[x]);
            const float slope = curve(next_[x], next_envelope_[x]) - y;
            heights[x] = y;
            acrosses[x] = 1.0F / device_root(1.0F + slope * slope);
            tops[x] = static_cast<std::int16_t>(std::max(0, device_floor(y - reach)));
            bottoms[x] = static_cast<std::int16_t>(std::min(height - 1, device_ceil(y + reach)));
        }
    }
}

// On the worker: the loop's dots, as stamp() lays them (device_sin and
// device_cos for the board's C library).
void AuraAuroraView::device_dots() {
    dots_.clear();
    constexpr int kSamples = 340;
    const float widen = 1.0F + 0.5F * job_.drive;
    const float glow = 10.0F * widen;
    const float core = 2.2F * widen;
    const float loop = job_.weights[2];
    const int core_reach = device_ceil(core);
    const int glow_reach = device_ceil(glow);
    for (std::size_t ribbon = 0; ribbon < kRibbons.size(); ++ribbon) {
        const auto& shape = kRibbons[ribbon];
        const Rgb color = mix(job_.weights, kColors[ribbon]);
        const float phase = job_.phases[ribbon] + shape.offset;
        const float spin = job_.seconds * 1.5F + static_cast<float>(ribbon) * 0.9F;
        float last_x = 0.0F;
        float last_y = 0.0F;
        float since_glow = glow;
        for (int sample = 0; sample < kSamples; ++sample) {
            const float u = static_cast<float>(sample) / static_cast<float>(kSamples - 1);
            const float rise = std::max(0.0F, device_sin(kPi * u));
            const float envelope = rise * device_root(rise);
            const float wave_x = 12.0F + 408.0F * u;
            const float wave_y = kMiddleY + envelope * shape.sway * job_.amplitude *
                                                (0.75F * device_sin(shape.waves * kTau * u + phase) +
                                                 0.35F * device_sin(2.6F * shape.waves * kTau * u - phase * 1.3F));
            const float turn = kTau * u + spin;
            const float loop_x = kMiddleX + (118.0F + 10.0F * static_cast<float>(ribbon)) * device_sin(turn);
            const float loop_y = kMiddleY + (64.0F - 6.0F * static_cast<float>(ribbon)) * device_sin(2.0F * turn);
            const float x = wave_x + (loop_x - wave_x) * loop;
            const float y = wave_y + (loop_y - wave_y) * loop;
            const float step_x = x - last_x;
            const float step_y = y - last_y;
            const float step = sample == 0 ? core : device_root(step_x * step_x + step_y * step_y);
            last_x = x;
            last_y = y;
            const float near = std::max(0.0F, device_cos(kTau * (u - job_.seconds * 0.35F)));
            const float near_squared = near * near;
            const float travelling = near_squared * near_squared * near_squared;
            const float scale = end_fade(x) * job_.strength * (1.0F + 0.6F * loop * travelling);
            if (scale <= 0.0F) {
                since_glow = glow;
                continue;
            }
            const int column = static_cast<int>(x);
            const int row = static_cast<int>(y);
            dots_.push_back({x, y, core, 1.0F / core, color.red, color.green, color.blue,
                             scale * 0.95F * std::clamp(step, 0.5F, core) / core, false, column, row, core_reach});
            since_glow += step;
            if (since_glow >= glow * 0.7F) {
                dots_.push_back({x, y, glow, 1.0F / glow, color.red, color.green, color.blue, scale * 0.3F, true,
                                 column, row, glow_reach});
                since_glow = 0.0F;
            }
        }
    }
}

// The ribbons' light on band rows [top, bottom) and columns [first, last],
// row `top` first: the curves' layers, or the loop's dots. Each pixel is
// worked out from its own row and column, so a strip matches the whole.
void FRAME_FAST AuraAuroraView::light_strip(std::uint8_t* light, int top, int bottom, int first,
                                            int last) const noexcept {
    if (!job_.loop) {
        std::array<Rgb, 3> colors{};
        for (std::size_t ribbon = 0; ribbon < kRibbons.size(); ++ribbon) {
            const Rgb color = mix(job_.weights, kColors[ribbon]);
            colors[ribbon] = {color.red * 255.0F, color.green * 255.0F, color.blue * 255.0F};
        }
        const float* layers = layers_.data();
        for (int x = first; x <= last; ++x) {
            if (fade_[x] <= 0.0F || along_[x] <= 0.0F || along_[x] >= 1.0F) continue;
            const float scale = fade_[x] * job_.strength;
            for (std::size_t ribbon = 0; ribbon < kRibbons.size(); ++ribbon) {
                const std::size_t at = ribbon * width + static_cast<std::size_t>(x);
                const int row_top = std::max(static_cast<int>(tops_[at]), top);
                const int row_bottom = std::min(static_cast<int>(bottoms_[at]), bottom - 1);
                if (row_bottom < row_top) continue;
                // Locals: a byte stored to the strip could alias the curves.
                const float y = curve_[at];
                const float across = across_[at] * 16.0F;
                const float red = colors[ribbon].red * scale;
                const float green = colors[ribbon].green * scale;
                const float blue = colors[ribbon].blue * scale;
                std::uint8_t* target =
                    light + (static_cast<std::size_t>(row_top - top) * width + static_cast<std::size_t>(x)) * 3;
                for (int row = row_top; row <= row_bottom; ++row, target += static_cast<std::size_t>(width) * 3) {
                    const float position = std::fabs(static_cast<float>(row) + 0.5F - y) * across;
                    const int index = static_cast<int>(position);
                    if (index >= kLayerSteps - 1) continue;
                    const float part = position - static_cast<float>(index);
                    const float value = layers[index] + (layers[index + 1] - layers[index]) * part;
                    if (value <= 0.0F) continue;
                    target[0] = static_cast<std::uint8_t>(
                        std::min(255, static_cast<int>(target[0]) + static_cast<int>(red * value)));
                    target[1] = static_cast<std::uint8_t>(
                        std::min(255, static_cast<int>(target[1]) + static_cast<int>(green * value)));
                    target[2] = static_cast<std::uint8_t>(
                        std::min(255, static_cast<int>(target[2]) + static_cast<int>(blue * value)));
                }
            }
        }
        return;
    }
    // The dots of these rows' eight (the strips follow the band's grid), in
    // integers: positions in 1/64 px, the falloff from its tables, the colour
    // times the peak in 8.8 bits.
    const DotFalloff& falloff = dot_falloff();
    const int bucket = top / kStripRows;
    const std::uint16_t* index = bucket_dots_.data() + bucket_starts_[static_cast<std::size_t>(bucket)];
    const std::uint16_t* const end = bucket_dots_.data() + bucket_starts_[static_cast<std::size_t>(bucket) + 1];
    for (; index != end; ++index) {
        // Locals: a byte stored to the strip could alias the dot.
        const Dot dot = dots_[*index];
        const int row_top = std::max({dot.row - dot.reach, top, 0});
        const int row_bottom = std::min({dot.row + dot.reach, bottom - 1, height - 1});
        if (row_bottom < row_top) continue;
        const std::int32_t x64 = device_floor(dot.x * 64.0F + 0.5F);
        const std::int32_t y64 = device_floor(dot.y * 64.0F + 0.5F);
        const std::int32_t radius64 = device_floor(dot.radius * 64.0F + 0.5F);
        const std::int32_t limit = radius64 * radius64;
        if (limit <= 0) continue;
        // The share in 30 bits: inside the dot the product stays within 32.
        const std::uint32_t inverse = (1U << 30) / static_cast<std::uint32_t>(limit);
        const std::uint16_t* table = dot.squared ? falloff.glow.data() : falloff.core.data();
        // ints, not std::int32_t: on the board that is a long, and std::min() takes one type.
        const int red = static_cast<int>(dot.red * dot.peak * (255.0F * 256.0F) + 0.5F);
        const int green = static_cast<int>(dot.green * dot.peak * (255.0F * 256.0F) + 0.5F);
        const int blue = static_cast<int>(dot.blue * dot.peak * (255.0F * 256.0F) + 0.5F);
        for (int row = row_top; row <= row_bottom; ++row) {
            const std::int32_t py = row * 64 + 32 - y64;
            const std::int32_t py_squared = py * py;
            if (py_squared >= limit) continue;
            // The columns inside the dot on this row, a pixel to spare.
            const float half = device_root(static_cast<float>(limit - py_squared)) * (1.0F / 64.0F);
            const int column_first = std::max({dot.column - dot.reach, first, 0, device_floor(dot.x - half - 0.5F)});
            const int column_last = std::min({dot.column + dot.reach, last, width - 1, device_ceil(dot.x + half)});
            std::uint8_t* target =
                light + (static_cast<std::size_t>(row - top) * width + static_cast<std::size_t>(column_first)) * 3;
            std::int32_t px = column_first * 64 + 32 - x64;
            for (int column = column_first; column <= column_last; ++column, target += 3, px += 64) {
                const std::int32_t squared = px * px + py_squared;
                if (squared >= limit) continue;
                const std::uint32_t share = static_cast<std::uint32_t>(squared) * inverse;
                const std::uint32_t step = share >> 20U;
                const auto part = static_cast<std::int32_t>((share >> 10U) & 0x3FFU);
                const int low = table[step];
                const int value = low + (((static_cast<int>(table[step + 1]) - low) * static_cast<int>(part)) >> 10);
                if (value <= 0) continue;
                target[0] = static_cast<std::uint8_t>(std::min(255, target[0] + ((red * value) >> 20)));
                target[1] = static_cast<std::uint8_t>(std::min(255, target[1] + ((green * value) >> 20)));
                target[2] = static_cast<std::uint8_t>(std::min(255, target[2] + ((blue * value) >> 20)));
            }
        }
    }
}

// The dots of each eight band rows, in drawing order, so each strip reads
// only its own (a frame of the loop has some 1,300; all of them per strip
// missed the board's cache).
void AuraAuroraView::bucket_dots() {
    bucket_starts_.fill(0);
    const auto rows = [](const Dot& dot, int& first, int& last) {
        first = std::max(dot.row - dot.reach, 0) / kStripRows;
        last = std::min(dot.row + dot.reach, height - 1) / kStripRows;
        return dot.row + dot.reach >= 0 && dot.row - dot.reach < height;
    };
    for (const auto& dot : dots_) {
        int first = 0;
        int last = 0;
        if (!rows(dot, first, last)) continue;
        for (int bucket = first; bucket <= last; ++bucket) ++bucket_starts_[static_cast<std::size_t>(bucket) + 1];
    }
    for (std::size_t bucket = 1; bucket < bucket_starts_.size(); ++bucket) {
        bucket_starts_[bucket] += bucket_starts_[bucket - 1];
    }
    bucket_dots_.resize(static_cast<std::size_t>(bucket_starts_.back()));
    std::array<int, kBuckets> filled{};
    for (std::size_t index = 0; index < dots_.size(); ++index) {
        int first = 0;
        int last = 0;
        if (!rows(dots_[index], first, last)) continue;
        for (int bucket = first; bucket <= last; ++bucket) {
            const auto slot = static_cast<std::size_t>(bucket_starts_[static_cast<std::size_t>(bucket)] +
                                                       filled[static_cast<std::size_t>(bucket)]++);
            bucket_dots_[slot] = static_cast<std::uint16_t>(index);
        }
    }
}

// On the worker, once a frame that redraws part of the band: the ribbons'
// curves, or the loop's dots, which its strips share.
void AuraAuroraView::begin_device(void* view, const FrameRect& redraw) {
    auto* self = static_cast<AuraAuroraView*>(view);
    const FrameRect band{self->center_x_ - width / 2, self->center_y_ - height / 2, self->center_x_ + width / 2,
                         self->center_y_ + height / 2};
    if (band.clipped(redraw).empty()) return;
    if (self->job_.loop) {
        self->device_dots();
        self->bucket_dots();
    } else {
        self->device_curves();
    }
}

// On the worker: the haze, the light fields above it and the ribbons' light,
// eight rows at a time.
void AuraAuroraView::draw_device(void* view, int lane, std::uint16_t* pixels, const FrameRect& area,
                                 const FrameRect& rect) noexcept {
    auto* self = static_cast<AuraAuroraView*>(view);
    const FrameRect band{self->center_x_ - width / 2, self->center_y_ - height / 2, self->center_x_ + width / 2,
                         self->center_y_ + height / 2};
    const FrameRect lit = band.clipped(rect);
    std::uint8_t* row = self->rows_[static_cast<std::size_t>(lane)].get();
    std::uint8_t* light = self->lights_[static_cast<std::size_t>(lane)].get();
    const int row_width = rect.width();
    // Strips of eight rows on the band's grid: each holds one bucket's dots.
    for (int top = rect.y1, bottom = 0; top < rect.y2; top = bottom) {
        const int grid = top - band.y1;
        const int cell = grid >= 0 ? grid / kStripRows : -((-grid + kStripRows - 1) / kStripRows);
        bottom = std::min(rect.y2, band.y1 + (cell + 1) * kStripRows);
        const int light_top = std::max(top, lit.y1);
        const int light_bottom = std::min(bottom, lit.y2);
        const bool lights = !lit.empty() && light_top < light_bottom;
        if (lights) {
            std::fill(light, light + static_cast<std::size_t>(kStripRows) * width * 3, std::uint8_t{0});
            self->light_strip(light, light_top - band.y1, light_bottom - band.y1, lit.x1 - band.x1,
                              lit.x2 - 1 - band.x1);
        }
        for (int y = top; y < bottom; ++y) {
            // The haze is the black row's first light: it writes the row whole.
            const FrameRow composed{row, rect.x1, rect.x2, y};
            lay_first_light(composed, self->haze_frame_);
            for (const auto& field : self->absorbed_) lay_light(composed, field);
            std::uint16_t* out = pixels + static_cast<std::size_t>(y - area.y1) * static_cast<std::size_t>(area.width()) +
                                 static_cast<std::size_t>(rect.x1 - area.x1);
            if (lights && y >= light_top && y < light_bottom) {
                // The ribbons' light laid over the haze as each pixel is packed.
                const std::uint8_t* added =
                    light + (static_cast<std::size_t>(y - light_top) * width + static_cast<std::size_t>(lit.x1 - band.x1)) * 3;
                const int before = lit.x1 - rect.x1;
                pack_row(row, before, out);
                pack_lit_row(row + static_cast<std::size_t>(before) * 3, added, lit.x2 - lit.x1, out + before);
                pack_row(row + static_cast<std::size_t>(lit.x2 - rect.x1) * 3, rect.x2 - lit.x2,
                         out + (lit.x2 - rect.x1));
            } else {
                pack_row(row, row_width, out);
            }
        }
    }
}

}  // namespace aura::ui

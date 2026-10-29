#include "aura/aura_mercury_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace aura::ui {

namespace {
constexpr int kLutSize = 1024;
constexpr float kTau = 6.2831853071795864769F;

const std::array<int, kLutSize>& sine_lut() {
    static const auto values = [] {
        std::array<int, kLutSize> result{};
        for (int index = 0; index < kLutSize; ++index) {
            result[index] = static_cast<int>(std::sin(kTau * static_cast<float>(index) /
                                                      static_cast<float>(kLutSize)) * 255.0F);
        }
        return result;
    }();
    return values;
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
    image_ = lv_image_create(parent);
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, center_x - size / 2, center_y - size / 2);
    lv_obj_set_clickable(image_, false);
    lv_obj_set_scrollable(image_, false);
    accumulator_ms_ = frame_interval_ms;
    frame_count_ = 0;
    render();
    return image_;
}

void AuraMercuryView::detach() noexcept { image_ = nullptr; }
void AuraMercuryView::set_state(MercuryState state) noexcept { state_ = state; }
void AuraMercuryView::set_level(float level) noexcept { level_ = std::clamp(level, 0.0F, 1.0F); }
void AuraMercuryView::set_reduced_motion(bool reduced) noexcept { reduced_motion_ = reduced; }

bool AuraMercuryView::tick(std::uint32_t elapsed_ms) {
    if (!image_ || reduced_motion_) return false;
    accumulator_ms_ += elapsed_ms;
    if (accumulator_ms_ < frame_interval_ms) return false;
    const auto step = accumulator_ms_;
    accumulator_ms_ = 0;
    int speed = 2;
    if (state_ == MercuryState::Listening) speed = 5;
    else if (state_ == MercuryState::Thinking) speed = 7;
    else if (state_ == MercuryState::Speaking) speed = 6 + static_cast<int>(level_ * 7.0F);
    phase_ += std::max<std::uint32_t>(1U, step * static_cast<std::uint32_t>(speed) / 4U);
    render();
    lv_obj_invalidate(image_);
    return true;
}

int AuraMercuryView::wave(int phase) noexcept { return sine_lut()[static_cast<unsigned>(phase) & (kLutSize - 1)]; }

std::uint16_t AuraMercuryView::rgb565(int red, int green, int blue) noexcept {
    red = std::clamp(red, 0, 255);
    green = std::clamp(green, 0, 255);
    blue = std::clamp(blue, 0, 255);
    return static_cast<std::uint16_t>(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

void AuraMercuryView::render() {
    auto* colors = reinterpret_cast<std::uint16_t*>(buffer_.data());
    auto* alpha = buffer_.data() + size * size * sizeof(std::uint16_t);
    const int center = size / 2;
    // Keep a real transparent safety margin even at the largest animated
    // deformation so no state reads as if it were cropped by its image box.
    const int radius = center - 11;
    const int radius_squared = radius * radius;
    int activity = 18;
    int contour = 44;
    int phase_rate = 2;
    int cyan_gain = 28;
    int violet_gain = 42;
    int rose_gain = 8;
    if (state_ == MercuryState::Listening) {
        activity = 30 + static_cast<int>(level_ * 44.0F);
        contour = 52;
        phase_rate = 3;
        cyan_gain = 72 + static_cast<int>(level_ * 30.0F);
        violet_gain = 25;
        rose_gain = 2;
    } else if (state_ == MercuryState::Thinking) {
        activity = 52;
        contour = 70;
        phase_rate = 4;
        cyan_gain = 18;
        violet_gain = 82;
        rose_gain = 26;
    } else if (state_ == MercuryState::Speaking) {
        activity = 36 + static_cast<int>(level_ * 58.0F);
        contour = 64;
        phase_rate = 3;
        cyan_gain = 30;
        violet_gain = 56;
        rose_gain = 48 + static_cast<int>(level_ * 30.0F);
    }

    const int phase = static_cast<int>(phase_ & 0x7FFFFFFFU);
    for (int y = 0; y < size; ++y) {
        const int dy = y - center;
        for (int x = 0; x < size; ++x) {
            const int dx = x - center;
            const int r2 = dx * dx + dy * dy;
            const int edge_flow = wave(phase + dx * 9 - dy * 7) + wave(phase * 2 + dx * 4 + dy * 11);
            // Keep the silhouette deformation inside a narrow radial band. A larger
            // coefficient turns the planar flow field into visible diagonal chords,
            // which reads as image cropping rather than liquid-metal motion.
            const int boundary = radius_squared + edge_flow * activity / 32;
            const auto index = static_cast<std::size_t>(y * size + x);
            if (r2 > boundary) {
                colors[index] = 0;
                alpha[index] = 0;
                continue;
            }

            const int depth = std::clamp(255 - (r2 * 255 / radius_squared), 0, 255);
            int flow_a = wave(phase * phase_rate + dx * 13 + dy * 5 + depth * 2);
            int flow_b = wave(-phase * (phase_rate + 1) + dx * 5 - dy * 12 + (r2 >> 2));
            int flow_c = wave(phase + dx * 3 + dy * 17 + flow_a / 2);
            if (state_ == MercuryState::Listening) {
                flow_a = wave(phase * 3 + dx * 16 + dy * 3 + depth);
                flow_c = wave(-phase * 2 + dx * 2 + dy * 20 + flow_a / 3);
            } else if (state_ == MercuryState::Thinking) {
                flow_a = wave(phase * 4 + dx * 8 - dy * 14 + depth * 3);
                flow_b = wave(-phase * 5 + dx * 15 + dy * 7 + (r2 >> 3));
            } else if (state_ == MercuryState::Speaking) {
                flow_b = wave(-phase * 4 + dx * 4 - dy * 17 + depth * 2);
                flow_c = wave(phase * 2 + dx * 11 + dy * 9 + flow_b / 2);
            }
            const int metal = wave(flow_a + flow_b / 2 + flow_c / 3 + depth * contour / 16 + phase);
            const int chrome = std::abs(metal);
            const int sharp = std::max(0, chrome - 150) * 2;
            int luminance = 30 + depth / 5 + chrome * 2 / 5 + sharp;

            const int highlight_distance = (dx + 29) * (dx + 29) + (dy + 34) * (dy + 34);
            if (highlight_distance < 720) luminance += (720 - highlight_distance) / 4;
            const int lower_shadow = std::max(0, dy - 20) * 2;
            luminance -= lower_shadow;

            const int iridescence = wave(phase + dx * 7 - dy * 3 + depth * 3);
            const int cyan = std::max(0, iridescence) * (cyan_gain + activity / 3) / 255;
            const int violet = std::max(0, -iridescence) * (violet_gain + activity / 2) / 255;
            const int rose_wave = wave(phase * 2 - dx * 5 + dy * 8 + depth);
            const int rose = std::max(0, rose_wave) * rose_gain / 255;
            const int edge = 255 - depth;
            const int rim = edge > 205 ? (edge - 205) * 2 : 0;
            const int red = luminance * 84 / 100 + violet + rose + rim / 3;
            const int green = luminance * 91 / 100 + cyan + rim / 2 - rose / 4;
            const int blue = luminance + cyan + violet + rose / 2 + rim;
            colors[index] = rgb565(red, green, blue);
            alpha[index] = static_cast<std::uint8_t>(std::clamp((boundary - r2) * 2, 0, 255));
        }
    }
    ++frame_count_;
}

}  // namespace aura::ui

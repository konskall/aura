#include "aura/photo_zoom.hpp"

#include <algorithm>
#include <cmath>

namespace aura {

float PhotoZoom::fit() const noexcept {
    if (image_width_ <= 0 || image_height_ <= 0 || frame_width_ <= 0 || frame_height_ <= 0) return 1.0F;
    return std::min(static_cast<float>(frame_width_) / static_cast<float>(image_width_),
                    static_cast<float>(frame_height_) / static_cast<float>(image_height_));
}

float PhotoZoom::scale() const noexcept { return fit() * zoom_; }

void PhotoZoom::set_frame(int image_width, int image_height, int frame_width, int frame_height) noexcept {
    if (image_width == image_width_ && image_height == image_height_ && frame_width == frame_width_ &&
        frame_height == frame_height_) {
        return;
    }
    image_width_ = image_width;
    image_height_ = image_height;
    frame_width_ = frame_width;
    frame_height_ = frame_height;
    reset();
}

void PhotoZoom::reset() noexcept {
    zoom_ = 1.0F;
    pinching_ = false;
    center_x_ = static_cast<float>(image_width_) / 2.0F;
    center_y_ = static_cast<float>(image_height_) / 2.0F;
}

void PhotoZoom::clamp_center() noexcept {
    const float visible_half_w = static_cast<float>(frame_width_) / (2.0F * scale());
    const float visible_half_h = static_cast<float>(frame_height_) / (2.0F * scale());
    const float width = static_cast<float>(image_width_);
    const float height = static_cast<float>(image_height_);
    // A side of the picture smaller than the frame stays centred in it.
    center_x_ = visible_half_w * 2.0F >= width ? width / 2.0F
                                               : std::clamp(center_x_, visible_half_w, width - visible_half_w);
    center_y_ = visible_half_h * 2.0F >= height ? height / 2.0F
                                                : std::clamp(center_y_, visible_half_h, height - visible_half_h);
}

void PhotoZoom::pinch(float ax, float ay, float bx, float by) noexcept {
    const float gap = std::hypot(ax - bx, ay - by);
    if (gap < kMinFingerGap) return;
    const float middle_x = (ax + bx) / 2.0F - static_cast<float>(frame_width_) / 2.0F;
    const float middle_y = (ay + by) / 2.0F - static_cast<float>(frame_height_) / 2.0F;
    if (!pinching_) {
        pinching_ = true;
        start_gap_ = gap;
        start_zoom_ = zoom_;
        anchor_x_ = center_x_ + middle_x / scale();
        anchor_y_ = center_y_ + middle_y / scale();
    }
    zoom_ = std::clamp(start_zoom_ * gap / start_gap_, 1.0F, kMaxZoom);
    center_x_ = anchor_x_ - middle_x / scale();
    center_y_ = anchor_y_ - middle_y / scale();
    clamp_center();
}

void PhotoZoom::pinch_end() noexcept {
    if (!pinching_) return;
    pinching_ = false;
    if (zoom_ < 1.05F) reset();
}

void PhotoZoom::pan(float dx, float dy) noexcept {
    if (!zoomed()) return;
    center_x_ -= dx / scale();
    center_y_ -= dy / scale();
    clamp_center();
}

void photo_zoom_render(const PhotoImage& image, const PhotoZoom& zoom, std::uint16_t* out, bool smooth) noexcept {
    const int width = zoom.frame_width();
    const int height = zoom.frame_height();
    const int image_width = static_cast<int>(image.width);
    const int image_height = static_cast<int>(image.height);
    if (!out || width <= 0 || height <= 0 || image_width <= 0 || image_height <= 0 ||
        image.pixels.size() != image.width * image.height) {
        return;
    }
    const float step = 1.0F / zoom.scale();
    // The picture's point at the top left pixel's centre.
    const float left = zoom.center_x() - (static_cast<float>(width) / 2.0F - 0.5F) * step;
    const float top = zoom.center_y() - (static_cast<float>(height) / 2.0F - 0.5F) * step;
    // 16.16 fixed point: a float for each pixel of the frame costs more than the frame.
    constexpr std::int32_t kOne = 1 << 16;
    const auto fixed_step = static_cast<std::int32_t>(step * static_cast<float>(kOne));
    const auto fixed_left = static_cast<std::int32_t>(left * static_cast<float>(kOne));
    const auto fixed_top = static_cast<std::int32_t>(top * static_cast<float>(kOne));
    const std::int32_t limit_x = image_width * kOne;
    const std::int32_t limit_y = image_height * kOne;
    const auto* pixels = image.pixels.data();
    std::int32_t source_y = fixed_top;
    for (int y = 0; y < height; ++y, source_y += fixed_step) {
        std::uint16_t* row = out + static_cast<std::ptrdiff_t>(y) * width;
        if (source_y < 0 || source_y >= limit_y) {
            std::fill_n(row, width, static_cast<std::uint16_t>(0));
            continue;
        }
        const std::uint16_t* line = pixels + static_cast<std::ptrdiff_t>(source_y >> 16) * image_width;
        std::int32_t source_x = fixed_left;
        if (!smooth) {
            for (int x = 0; x < width; ++x, source_x += fixed_step) {
                row[x] = source_x < 0 || source_x >= limit_x ? static_cast<std::uint16_t>(0) : line[source_x >> 16];
            }
            continue;
        }
        const std::uint16_t* next = source_y >> 16 < image_height - 1 ? line + image_width : line;
        const auto blend_y = static_cast<std::uint32_t>((source_y & 0xFFFF) >> 11);  // 0..31
        for (int x = 0; x < width; ++x, source_x += fixed_step) {
            if (source_x < 0 || source_x >= limit_x) {
                row[x] = 0;
                continue;
            }
            const int column = source_x >> 16;
            const int right = column < image_width - 1 ? column + 1 : column;
            const auto blend_x = static_cast<std::uint32_t>((source_x & 0xFFFF) >> 11);
            // RGB565 spread over 32 bits (green apart), so one multiply blends all three.
            const auto spread = [](std::uint16_t pixel) noexcept {
                return (static_cast<std::uint32_t>(pixel) | (static_cast<std::uint32_t>(pixel) << 16)) & 0x07E0F81FU;
            };
            const auto mix = [](std::uint32_t a, std::uint32_t b, std::uint32_t t) noexcept {
                return (a * (32U - t) + b * t) >> 5 & 0x07E0F81FU;
            };
            const auto upper = mix(spread(line[column]), spread(line[right]), blend_x);
            const auto lower = mix(spread(next[column]), spread(next[right]), blend_x);
            const auto value = mix(upper, lower, blend_y);
            row[x] = static_cast<std::uint16_t>(value | (value >> 16));
        }
    }
}

}  // namespace aura

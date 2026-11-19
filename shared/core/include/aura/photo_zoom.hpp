#pragma once

#include "aura/photos.hpp"

#include <cstdint>

namespace aura {

// Zooming a photo with two fingers (the owner, 2026-11-18). All in the frame's
// pixels: what part of the picture the frame shows, while the fingers move it.
// The picture starts whole; the point between the two fingers stays under them.
class PhotoZoom {
public:
    static constexpr float kMaxZoom = 4.0F;
    // Closer than this on the glass, two touches are too near to measure.
    static constexpr float kMinFingerGap = 24.0F;

    // The picture and the frame that shows it. A new picture or frame starts whole.
    void set_frame(int image_width, int image_height, int frame_width, int frame_height) noexcept;
    void reset() noexcept;

    // Two fingers on the glass, in the frame's coordinates, however they moved.
    void pinch(float ax, float ay, float bx, float by) noexcept;
    // They lifted. A picture barely zoomed goes back to whole.
    void pinch_end() noexcept;
    bool pinching() const noexcept { return pinching_; }
    // One finger dragging a zoomed picture.
    void pan(float dx, float dy) noexcept;

    float zoom() const noexcept { return zoom_; }
    bool zoomed() const noexcept { return zoom_ > 1.001F; }
    // Frame pixels per picture pixel, and the picture's point at the frame's centre.
    float scale() const noexcept;
    float center_x() const noexcept { return center_x_; }
    float center_y() const noexcept { return center_y_; }
    int frame_width() const noexcept { return frame_width_; }
    int frame_height() const noexcept { return frame_height_; }

private:
    float fit() const noexcept;
    void clamp_center() noexcept;

    int image_width_{0};
    int image_height_{0};
    int frame_width_{0};
    int frame_height_{0};
    float zoom_{1.0F};
    float center_x_{0.0F};
    float center_y_{0.0F};
    bool pinching_{false};
    float start_gap_{0.0F};
    float start_zoom_{1.0F};
    float anchor_x_{0.0F};  // the picture's point between the fingers
    float anchor_y_{0.0F};
};

// The frame's pixels of `image` under `zoom` (frame_width x frame_height RGB565
// into `out`), black around a picture smaller than the frame. `smooth` blends
// neighbouring pixels for a still picture; while the fingers move, the nearest
// pixel is a quarter of the work.
void photo_zoom_render(const PhotoImage& image, const PhotoZoom& zoom, std::uint16_t* out, bool smooth) noexcept;

}  // namespace aura

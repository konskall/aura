#include "aura/screenshot.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

#include "SDL.h"

namespace {
struct FlushCapture {
    lv_display_flush_cb_t original{};
    std::vector<std::uint8_t> pixels;
    std::uint32_t stride{};
    std::int32_t width{};
    std::int32_t height{};
    bool complete{};
};

thread_local FlushCapture* active_capture{};

void capture_flush(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixel_map) {
    auto* capture = active_capture;
    if (capture && lv_display_flush_is_last(display) && area &&
        area->x1 == 0 && area->y1 == 0 &&
        area->x2 + 1 == capture->width && area->y2 + 1 == capture->height) {
        std::memcpy(capture->pixels.data(), pixel_map, capture->pixels.size());
        capture->complete = true;
    }
    if (capture && capture->original) capture->original(display, area, pixel_map);
}
}

namespace aura::simulator {

bool save_display_bmp(lv_display_t* display, const std::string& path) {
    if (!display) return false;
    const auto width = lv_display_get_horizontal_resolution(display);
    const auto height = lv_display_get_vertical_resolution(display);
    const auto color_format = lv_display_get_color_format(display);
    if (width <= 0 || height <= 0) return false;

    FlushCapture capture;
    capture.original = lv_display_get_flush_cb(display);
    capture.stride = lv_draw_buf_width_to_stride(static_cast<std::uint32_t>(width), color_format);
    capture.width = width;
    capture.height = height;
    capture.pixels.resize(static_cast<std::size_t>(capture.stride) * static_cast<std::size_t>(height));
    active_capture = &capture;
    lv_display_set_flush_cb(display, capture_flush);
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_refr_now(display);
    lv_display_set_flush_cb(display, capture.original);
    active_capture = nullptr;
    if (!capture.complete) return false;

    Uint32 pixel_format = SDL_PIXELFORMAT_UNKNOWN;
    int depth = 0;
    if (color_format == LV_COLOR_FORMAT_ARGB8888) {
        pixel_format = SDL_PIXELFORMAT_ARGB8888;
        depth = 32;
    } else if (color_format == LV_COLOR_FORMAT_XRGB8888) {
        pixel_format = SDL_PIXELFORMAT_RGB888;
        depth = 32;
    } else if (color_format == LV_COLOR_FORMAT_RGB888) {
        pixel_format = SDL_PIXELFORMAT_BGR24;
        depth = 24;
    } else if (color_format == LV_COLOR_FORMAT_RGB565) {
        pixel_format = SDL_PIXELFORMAT_RGB565;
        depth = 16;
    } else {
        return false;
    }
    auto* wrapped = SDL_CreateRGBSurfaceWithFormatFrom(
        capture.pixels.data(), width, height, depth, static_cast<int>(capture.stride), pixel_format);
    if (!wrapped) return false;
    auto* surface = SDL_ConvertSurfaceFormat(wrapped, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(wrapped);
    if (!surface) return false;

    std::error_code error;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    const bool saved = !error && SDL_SaveBMP(surface, path.c_str()) == 0;
    SDL_FreeSurface(surface);
    return saved;
}

}  // namespace aura::simulator

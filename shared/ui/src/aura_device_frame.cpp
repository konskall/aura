#include "aura/aura_device_frame.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

#include "aura/aura_mercury_view.hpp"
#include "aura/aura_presence.hpp"
#include "aura/aura_theme.hpp"
#include "src/core/lv_obj_draw_private.h"  // lv_obj_get_ext_draw_size()
#include "src/misc/lv_area_private.h"      // lv_area_intersect(), lv_area_is_in()

namespace aura::ui {

namespace {
// The pearl's glow (aura_pearl_view.cpp, glow_texture()): full to 37 % of its
// radius, then easing out. By squared distance (0..1 in 256 steps), 0..65535.
const std::array<std::uint16_t, 257>& pearl_glow() {
    static const auto table = [] {
        std::array<std::uint16_t, 257> values{};
        for (std::size_t index = 0; index < values.size(); ++index) {
            const float distance = std::sqrt(static_cast<float>(index) / 256.0F);
            const float strength = distance <= 0.37F ? 1.0F :
                (distance >= 1.0F ? 0.0F : std::pow((1.0F - distance) / 0.63F, 1.6F));
            values[index] = static_cast<std::uint16_t>(strength * 65535.0F + 0.5F);
        }
        return values;
    }();
    return table;
}

std::uint32_t hex(lv_color_t color) {
    return static_cast<std::uint32_t>(color.red) << 16 | static_cast<std::uint32_t>(color.green) << 8 | color.blue;
}

// What lies over a frame, as Synapse's direct rows find it
// (aura_mercury_view.cpp): a plain object or a button draws only its
// background, border, outline and shadow; any other widget (a label, an
// image, an arc) is taken to draw wherever it lies.
bool draws_itself(const lv_obj_t* object) {
    const lv_obj_class_t* type = lv_obj_get_class(object);
    if (type != &lv_obj_class && type != &lv_button_class) return true;
    const auto seen = [](lv_opa_t opa) { return opa > LV_OPA_MIN; };
    return seen(lv_obj_get_style_bg_opa(object, LV_PART_MAIN)) ||
           lv_obj_get_style_bg_image_src(object, LV_PART_MAIN) != nullptr ||
           (lv_obj_get_style_border_width(object, LV_PART_MAIN) > 0 &&
            seen(lv_obj_get_style_border_opa(object, LV_PART_MAIN))) ||
           (lv_obj_get_style_outline_width(object, LV_PART_MAIN) > 0 &&
            seen(lv_obj_get_style_outline_opa(object, LV_PART_MAIN))) ||
           (lv_obj_get_style_shadow_width(object, LV_PART_MAIN) > 0 &&
            seen(lv_obj_get_style_shadow_opa(object, LV_PART_MAIN)));
}

// The parts of `frame` (screen coordinates) where `object`, or anything
// inside it, may draw.
void collect_drawn(lv_obj_t* object, const lv_area_t& frame, std::vector<lv_area_t>& drawn) {
    if (lv_obj_is_hidden(object) || lv_obj_get_style_opa(object, LV_PART_MAIN) <= LV_OPA_MIN) return;
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    // A label's extra area is LVGL's margin for letters that lean out of its
    // box sideways (italics, lv_label.c); its lines stay between its top and
    // bottom.
    const int32_t extra = lv_obj_get_ext_draw_size(object);
    lv_area_increase(&area, extra, lv_obj_check_type(object, &lv_label_class) ? 0 : extra);
    lv_area_t common{};
    const bool overlaps = lv_area_intersect(&common, &area, &frame);
    // Children are clipped to their parent unless it lets them overflow.
    const bool clips = !lv_obj_is_overflow_visible(object);
    if (overlaps && draws_itself(object)) {
        drawn.push_back(common);
        if (clips) return;
    }
    if (!overlaps && clips) return;
    const std::uint32_t children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0; index < children; ++index) {
        collect_drawn(lv_obj_get_child(object, static_cast<std::int32_t>(index)), frame, drawn);
    }
}

// Whether `image`, at `coords` on the screen, shows as it is drawn: on the
// screen shown (no screen sliding or fading in), opaque, untransformed, and
// inside each of its parents, whose scrollbars and outlines would be drawn
// over it.
bool shown_as_drawn(lv_obj_t* image, const lv_area_t& coords) {
    auto* display = lv_obj_get_display(image);
    if (lv_obj_get_screen(image) != lv_display_get_screen_active(display) ||
        lv_display_get_screen_prev(display) != nullptr) {
        return false;
    }
    for (lv_obj_t* object = image; object; object = lv_obj_get_parent(object)) {
        if (lv_obj_is_hidden(object) || lv_obj_get_style_opa(object, LV_PART_MAIN) < LV_OPA_MAX ||
            lv_obj_get_style_transform_scale_x(object, LV_PART_MAIN) != LV_SCALE_NONE ||
            lv_obj_get_style_transform_scale_y(object, LV_PART_MAIN) != LV_SCALE_NONE ||
            lv_obj_get_style_transform_rotation(object, LV_PART_MAIN) != 0 ||
            lv_obj_get_style_transform_skew_x(object, LV_PART_MAIN) != 0 ||
            lv_obj_get_style_transform_skew_y(object, LV_PART_MAIN) != 0 ||
            lv_obj_get_style_blend_mode(object, LV_PART_MAIN) != LV_BLEND_MODE_NORMAL) {
            return false;
        }
        lv_area_t area{};
        lv_obj_get_coords(object, &area);
        if (!lv_area_is_in(&coords, &area, 0)) return false;
        if (object == image) continue;
        lv_area_t horizontal{};
        lv_area_t vertical{};
        lv_obj_get_scrollbar_area(object, &horizontal, &vertical);
        lv_area_t common{};
        if (lv_area_intersect(&common, &horizontal, &coords) || lv_area_intersect(&common, &vertical, &coords) ||
            (lv_obj_get_style_outline_width(object, LV_PART_MAIN) > 0 &&
             lv_obj_get_style_outline_opa(object, LV_PART_MAIN) > LV_OPA_MIN)) {
            return false;
        }
    }
    return true;
}

// The platform's two strip memories take turns: one is written while the
// panel may still read the other, which may be written again once the next
// strip has gone (aura_board_display_draw()). Only the worker sends.
std::uint32_t g_strip_turn = 0;
}  // namespace

PresenceProfile default_presence_profile() noexcept {
#if defined(ESP_PLATFORM)
    return PresenceProfile::Device;
#else
    return PresenceProfile::Presentation;
#endif
}

FrameRect FrameRect::united(const FrameRect& other) const noexcept {
    if (empty()) return other;
    if (other.empty()) return *this;
    return {std::min(x1, other.x1), std::min(y1, other.y1), std::max(x2, other.x2), std::max(y2, other.y2)};
}

FrameRect FrameRect::clipped(const FrameRect& other) const noexcept {
    const FrameRect result{std::max(x1, other.x1), std::max(y1, other.y1), std::min(x2, other.x2),
                           std::min(y2, other.y2)};
    return result.empty() ? FrameRect{} : result;
}

FrameRect FrameRect::around(int center_x, int center_y, int reach) noexcept {
    return {center_x - reach, center_y - reach, center_x + reach, center_y + reach};
}

std::vector<FrameLight> absorb_light_fields(lv_obj_t* parent, const FrameRect& area) {
    std::vector<FrameLight> lights;
    if (!parent) return lights;
    const std::uint32_t count = lv_obj_get_child_count(parent);
    for (std::uint32_t index = 0; index < count; ++index) {
        lv_obj_t* child = lv_obj_get_child(parent, static_cast<std::int32_t>(index));
        if (!is_light_field(child) || lv_obj_is_hidden(child)) continue;
        // ui::light_field() places a 2r square at the centre less r.
        const int radius = static_cast<int>(lv_obj_get_style_width(child, LV_PART_MAIN)) / 2;
        FrameLight light;
        light.center_x = static_cast<int>(lv_obj_get_style_x(child, LV_PART_MAIN)) + radius;
        light.center_y = static_cast<int>(lv_obj_get_style_y(child, LV_PART_MAIN)) + radius;
        light.radius = radius;
        light.tint = hex(lv_obj_get_style_image_recolor(child, LV_PART_MAIN));
        light.opacity = lv_obj_get_style_image_opa(child, LV_PART_MAIN);
        light.shape = LightShape::Disc;
        if (light.bounds().clipped(area).empty()) continue;
        lv_obj_set_hidden(child, true);
        lights.push_back(light);
    }
    return lights;
}

void FRAME_FAST lay_light(const FrameRow& row, const FrameLight& light) noexcept {
    if (light.opacity == 0 || light.radius <= 0 || row.x2 <= row.x1) return;
    // Doubled coordinates from the light's centre are exact integers: x' =
    // 2 (x - cx) + 1, y' = 2 (y - cy) + 1, and x'^2 + y'^2 = 4 d^2. Each pixel's
    // distance is exact wherever its row starts, so a part of the frame
    // redrawn alone matches the whole; and integers, not floats, on the board.
    const std::int32_t limit = 4 * light.radius * light.radius;
    const std::int32_t y_doubled = 2 * (row.y - light.center_y) + 1;
    const std::int32_t y_squared = y_doubled * y_doubled;
    if (y_squared >= limit) return;
    // The columns the disc reaches on this row, a pixel to spare.
    const int reach = static_cast<int>(device_root(static_cast<float>(limit - y_squared)) * 0.5F) + 1;
    const int first = std::max(row.x1, light.center_x - reach - 1);
    const int last = std::min(row.x2 - 1, light.center_x + reach);
    if (last < first) return;
    // The share of the radius squared in 26 fractional bits: inside the disc
    // the product stays within 32 bits.
    const std::uint32_t inverse = (1U << 26) / static_cast<std::uint32_t>(limit);
    // Opacity 0..255 as 0..256.
    const std::uint32_t opacity = light.opacity + (light.opacity >> 7U);
    const int red = static_cast<int>((light.tint >> 16) & 0xFFU);
    const int green = static_cast<int>((light.tint >> 8) & 0xFFU);
    const int blue = static_cast<int>(light.tint & 0xFFU);
    std::int32_t x_doubled = 2 * (first - light.center_x) + 1;
    std::int32_t distance = x_doubled * x_doubled + y_squared;
    std::int32_t step = 4 * x_doubled + 4;  // to the next column's: (x' + 2)^2 - x'^2
    std::uint8_t* pixel = row.rgb + static_cast<std::ptrdiff_t>(first - row.x1) * 3;
    std::uint8_t* const end = pixel + static_cast<std::ptrdiff_t>(last - first + 1) * 3;
    if (light.shape == LightShape::Disc) {
        for (; pixel != end; pixel += 3, distance += step, step += 8) {
            if (distance >= limit) continue;
            // (1 - share)^2 in 16 bits.
            const std::uint32_t rest = ((1U << 26) - static_cast<std::uint32_t>(distance) * inverse) >> 11;
            const auto alpha = static_cast<int>((((rest * rest) >> 14) * opacity) >> 16);
            if (alpha == 0) continue;
            const int keep = 256 - alpha;
            pixel[0] = static_cast<std::uint8_t>((pixel[0] * keep + red * alpha) >> 8);
            pixel[1] = static_cast<std::uint8_t>((pixel[1] * keep + green * alpha) >> 8);
            pixel[2] = static_cast<std::uint8_t>((pixel[2] * keep + blue * alpha) >> 8);
        }
        return;
    }
    const std::uint16_t* pearl = pearl_glow().data();
    for (; pixel != end; pixel += 3, distance += step, step += 8) {
        if (distance >= limit) continue;
        // The pearl's falloff by the share, between its table's steps.
        const std::uint32_t share = static_cast<std::uint32_t>(distance) * inverse;
        const std::uint32_t index = share >> 18U;
        const auto part = static_cast<std::int32_t>((share >> 3U) & 0x7FFFU);
        const auto low = static_cast<std::int32_t>(pearl[index]);
        const auto high = static_cast<std::int32_t>(pearl[index + 1]);
        const auto strength = static_cast<std::uint32_t>(low + (((high - low) * part) >> 15));
        const auto alpha = static_cast<int>((strength * opacity) >> 16);
        if (alpha == 0) continue;
        const int keep = 256 - alpha;
        pixel[0] = static_cast<std::uint8_t>((pixel[0] * keep + red * alpha) >> 8);
        pixel[1] = static_cast<std::uint8_t>((pixel[1] * keep + green * alpha) >> 8);
        pixel[2] = static_cast<std::uint8_t>((pixel[2] * keep + blue * alpha) >> 8);
    }
}

void FRAME_FAST lay_first_light(const FrameRow& row, const FrameLight& light) noexcept {
    if (row.x2 <= row.x1) return;
    std::uint8_t* const start = row.rgb;
    const auto bytes = static_cast<std::size_t>(row.x2 - row.x1) * 3;
    // As lay_light(), whose blend over black is the tint times its alpha.
    const std::int32_t limit = 4 * light.radius * light.radius;
    const std::int32_t y_doubled = 2 * (row.y - light.center_y) + 1;
    const std::int32_t y_squared = y_doubled * y_doubled;
    if (light.opacity == 0 || light.radius <= 0 || y_squared >= limit) {
        std::memset(start, 0, bytes);
        return;
    }
    const int reach = static_cast<int>(device_root(static_cast<float>(limit - y_squared)) * 0.5F) + 1;
    const int first = std::max(row.x1, light.center_x - reach - 1);
    const int last = std::min(row.x2 - 1, light.center_x + reach);
    if (last < first) {
        std::memset(start, 0, bytes);
        return;
    }
    // Black on either side of the columns the light reaches.
    std::memset(start, 0, static_cast<std::size_t>(first - row.x1) * 3);
    std::memset(start + static_cast<std::ptrdiff_t>(last + 1 - row.x1) * 3, 0,
                static_cast<std::size_t>(row.x2 - 1 - last) * 3);
    const std::uint32_t inverse = (1U << 26) / static_cast<std::uint32_t>(limit);
    const std::uint32_t opacity = light.opacity + (light.opacity >> 7U);
    const int red = static_cast<int>((light.tint >> 16) & 0xFFU);
    const int green = static_cast<int>((light.tint >> 8) & 0xFFU);
    const int blue = static_cast<int>(light.tint & 0xFFU);
    std::int32_t x_doubled = 2 * (first - light.center_x) + 1;
    std::int32_t distance = x_doubled * x_doubled + y_squared;
    std::int32_t step = 4 * x_doubled + 4;
    std::uint8_t* pixel = start + static_cast<std::ptrdiff_t>(first - row.x1) * 3;
    std::uint8_t* const end = pixel + static_cast<std::ptrdiff_t>(last - first + 1) * 3;
    if (light.shape == LightShape::Disc) {
        for (; pixel != end; pixel += 3, distance += step, step += 8) {
            int alpha = 0;
            if (distance < limit) {
                const std::uint32_t rest = ((1U << 26) - static_cast<std::uint32_t>(distance) * inverse) >> 11;
                alpha = static_cast<int>((((rest * rest) >> 14) * opacity) >> 16);
            }
            pixel[0] = static_cast<std::uint8_t>((red * alpha) >> 8);
            pixel[1] = static_cast<std::uint8_t>((green * alpha) >> 8);
            pixel[2] = static_cast<std::uint8_t>((blue * alpha) >> 8);
        }
        return;
    }
    const std::uint16_t* pearl = pearl_glow().data();
    for (; pixel != end; pixel += 3, distance += step, step += 8) {
        int alpha = 0;
        if (distance < limit) {
            const std::uint32_t share = static_cast<std::uint32_t>(distance) * inverse;
            const std::uint32_t index = share >> 18U;
            const auto part = static_cast<std::int32_t>((share >> 3U) & 0x7FFFU);
            const auto low = static_cast<std::int32_t>(pearl[index]);
            const auto high = static_cast<std::int32_t>(pearl[index + 1]);
            const auto strength = static_cast<std::uint32_t>(low + (((high - low) * part) >> 15));
            alpha = static_cast<int>((strength * opacity) >> 16);
        }
        pixel[0] = static_cast<std::uint8_t>((red * alpha) >> 8);
        pixel[1] = static_cast<std::uint8_t>((green * alpha) >> 8);
        pixel[2] = static_cast<std::uint8_t>((blue * alpha) >> 8);
    }
}

void FRAME_FAST pack_row(const std::uint8_t* rgb, int count, std::uint16_t* out) noexcept {
    for (int index = 0; index < count; ++index, rgb += 3) {
        out[index] = static_cast<std::uint16_t>(((rgb[0] & 0xF8U) << 8) | ((rgb[1] & 0xFCU) << 3) | (rgb[2] >> 3));
    }
}

void FRAME_FAST pack_lit_row(const std::uint8_t* rgb, const std::uint8_t* light, int count,
                             std::uint16_t* out) noexcept {
    for (int index = 0; index < count; ++index, rgb += 3, light += 3) {
        int red = rgb[0];
        int green = rgb[1];
        int blue = rgb[2];
        const int added_red = light[0];
        const int added_green = light[1];
        const int added_blue = light[2];
        const int cover = std::max(added_red, std::max(added_green, added_blue));
        if (cover != 0) {
            const int keep = (255 - cover) * 257;
            red = std::min(255, ((red * keep + 32768) >> 16) + added_red);
            green = std::min(255, ((green * keep + 32768) >> 16) + added_green);
            blue = std::min(255, ((blue * keep + 32768) >> 16) + added_blue);
        }
        out[index] = static_cast<std::uint16_t>((static_cast<unsigned>(red & 0xF8) << 8) |
                                                (static_cast<unsigned>(green & 0xFC) << 3) |
                                                (static_cast<unsigned>(blue) >> 3));
    }
}

DeviceFrame::DeviceFrame(int width, int height, Draw draw, void* owner, Begin begin)
    : width_(width), height_(height), draw_(draw), owner_(owner), begin_(begin) {
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (auto& buffer : buffers_) buffer = std::make_unique<std::uint16_t[]>(count);
    descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    descriptor_.header.w = static_cast<std::uint32_t>(width);
    descriptor_.header.h = static_cast<std::uint32_t>(height);
    descriptor_.header.stride = static_cast<std::uint32_t>(width) * sizeof(std::uint16_t);
    descriptor_.data_size = static_cast<std::uint32_t>(count * sizeof(std::uint16_t));
    descriptor_.data = reinterpret_cast<const std::uint8_t*>(buffers_[0].get());
}

DeviceFrame::~DeviceFrame() {
    if (drawing_ && worker_) worker_->wait_idle();
    stop_listening();
}

void DeviceFrame::set_worker(FrameWorker* worker) {
    if (worker == worker_) return;
    finish();
    worker_ = worker;
}

void DeviceFrame::set_helper(FrameWorker* helper) {
    if (helper == helper_) return;
    finish();
    helper_ = helper;
}

void DeviceFrame::attach(lv_obj_t* parent, int x, int y) {
    // The last visit's image went with its screen. A frame still on the worker
    // belongs to that visit: it shows first.
    image_ = nullptr;
    finish();
    const bool moved = !placed_ || x != x_ || y != y_;
    x_ = x;
    y_ = y;
    placed_ = true;
    if (moved) invalidate_all();
    image_ = lv_image_create(parent);
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, x, y);
    lv_obj_set_clickable(image_, false);
    lv_obj_set_scrollable(image_, false);
    // Opaque light on the black canvas: behind every label and control.
    lv_obj_move_to_index(image_, 0);
    since_ms_ = interval_ms;
    // Nothing is known of what LVGL draws over it until its next refresh.
    on_screen_ = false;
    lvgl_pending_.store(~0ULL, std::memory_order_relaxed);
    listen(lv_obj_get_display(image_));
}

void DeviceFrame::detach() noexcept {
    // Every strip waits for LVGL, whose drawing is no longer heard: a frame on
    // the worker sends nothing more, and nothing it sent counts as shown.
    lvgl_pending_.store(~0ULL, std::memory_order_relaxed);
    spoiled_.store(~0ULL, std::memory_order_relaxed);
    stop_listening();
    image_ = nullptr;
}

void DeviceFrame::align_strips(int row) noexcept {
    finish();
    const int offset = ((row - y_) % strip_rows + strip_rows) % strip_rows & ~1;
    shift_ = (strip_rows - offset) % strip_rows;
    // What LVGL was told was placed by the strips before.
    lvgl_pending_.store(~0ULL, std::memory_order_relaxed);
}

void DeviceFrame::invalidate_all() noexcept {
    pending_[0] = area();
    pending_[1] = area();
}

DeviceFrame::Step DeviceFrame::advance(std::uint32_t elapsed_ms) noexcept {
    since_ms_ = std::min<std::uint32_t>(since_ms_ + elapsed_ms, 1000U);
    bool shown = false;
    if (drawing_) {
        if (worker_->busy()) return Step::Wait;
        drawing_ = false;
        present();
        shown = true;
    }
    if (since_ms_ < interval_ms) return shown ? Step::Shown : Step::Wait;
    // Another view's frame still holds the shared worker.
    if (worker_ && worker_->busy()) return shown ? Step::Shown : Step::Wait;
    step_ms_ = std::min<std::uint32_t>(since_ms_, 100U);
    since_ms_ = 0;
    return shown ? Step::ShownStart : Step::Start;
}

bool DeviceFrame::start(const FrameRect& dirty) {
    dirty_ = dirty.clipped(area());
    // The back buffer holds the frame before the one shown: it lacks both.
    redraw_ = pending_[front_ ^ 1].united(dirty_).clipped(area());
    if (worker_) {
        plan_direct();
        drawing_ = worker_->submit(&DeviceFrame::run, this);
        if (drawing_) return false;
    }
    // Drawn here, under the lock the caller holds: never straight.
    direct_ = 0;
    output_ = nullptr;
    run(this);
    present();
    return true;
}

void DeviceFrame::draw_whole() {
    finish();
    dirty_ = area();
    redraw_ = area();
    direct_ = 0;
    output_ = nullptr;
    run(this);
    present();
}

void DeviceFrame::finish() noexcept {
    if (!drawing_) return;
    worker_->wait_idle();
    drawing_ = false;
    present();
}

void DeviceFrame::stop() noexcept {
    if (drawing_ && worker_) worker_->wait_idle();
    drawing_ = false;
}

std::uint32_t DeviceFrame::next_tick_ms() const noexcept {
    if (!image_ || !worker_) return PresenceView::no_tick;
    if (drawing_) return worker_->busy() ? PresenceView::no_tick : 0U;
    if (since_ms_ < interval_ms) return interval_ms - since_ms_;
    return worker_->busy() ? PresenceView::no_tick : 0U;
}

void DeviceFrame::run(void* frame) noexcept {
    auto* self = static_cast<DeviceFrame*>(frame);
    const auto started = std::chrono::steady_clock::now();
    const FrameRect redraw = self->redraw_;
    if (!redraw.empty()) {
        if (self->begin_) self->begin_(self->owner_, redraw);
        self->next_strip_.store(self->strip_of(redraw.y1 - self->y_), std::memory_order_relaxed);
        self->ready_.store(0, std::memory_order_relaxed);
        // The helper takes strips as this thread does, while there are any.
        const bool helped = self->helper_ != nullptr && redraw.height() > strip_rows &&
                            self->helper_->submit(&DeviceFrame::run_helper, self);
        self->draw_strips(0);
        if (helped) self->helper_->wait_idle();
        // The last strips wait for the lock a while; LVGL draws those it never gets.
        if (self->ready_.load(std::memory_order_acquire) != 0) self->send_strips(lock_wait_ms, true);
        self->ready_.store(0, std::memory_order_relaxed);
    }
    self->drawn_ms_ = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
}

void DeviceFrame::run_helper(void* frame) noexcept { static_cast<DeviceFrame*>(frame)->draw_strips(1); }

void DeviceFrame::draw_strips(int lane) noexcept {
    const FrameRect redraw = redraw_;
    const FrameRect area = this->area();
    std::uint16_t* pixels = buffers_[front_ ^ 1].get();
    const std::uint64_t direct = direct_;
    for (;;) {
        const int strip = next_strip_.fetch_add(1, std::memory_order_relaxed);
        const int top = area.y1 + strip_top(strip);
        if (top >= redraw.y2) break;
        const FrameRect rows = FrameRect{area.x1, top, area.x2, area.y1 + strip_bottom(strip)}.clipped(redraw);
        if (!rows.empty()) draw_(owner_, lane, pixels, area, rows);
        if (direct != 0 && ((direct >> strip) & 1U) != 0) ready_.fetch_or(1ULL << strip, std::memory_order_release);
        // The helper sends what both have drawn so far (see lock_wait_ms).
        if (lane == 1 && ready_.load(std::memory_order_acquire) != 0) send_strips(lock_wait_ms, false);
    }
}

void DeviceFrame::present() noexcept {
    const int drawn = front_ ^ 1;
    pending_[drawn] = {};
    // The buffer shown until now lacks this frame's changes.
    pending_[front_] = pending_[front_].united(dirty_);
    front_ = drawn;
    descriptor_.data = reinterpret_cast<const std::uint8_t*>(buffers_[front_].get());
    // The strips the panel has from this frame; LVGL draws the rest of the change.
    const std::uint64_t straight = sent_.load(std::memory_order_relaxed) & ~spoiled_.load(std::memory_order_relaxed);
    if (image_ && straight == 0 && dirty_ == area()) {
        // A new image has no coordinates until LVGL lays it out.
        lv_obj_invalidate(image_);
    } else if (image_ && !dirty_.empty()) {
        lv_area_t coords{};
        lv_obj_get_coords(image_, &coords);
        const auto invalidate = [&](const FrameRect& rect) {
            const lv_area_t changed{coords.x1 + rect.x1 - x_, coords.y1 + rect.y1 - y_, coords.x1 + rect.x2 - x_ - 1,
                                    coords.y1 + rect.y2 - y_ - 1};
            lv_obj_invalidate_area(image_, &changed);
        };
        if (straight == 0) {
            invalidate(dirty_);
        } else {
            // The change's strips the panel lacks, a band of them at a time.
            const int last = strip_of(dirty_.y2 - 1 - y_);
            for (int strip = strip_of(dirty_.y1 - y_); strip <= last; ++strip) {
                if (((straight >> strip) & 1U) != 0) continue;
                int end = strip;
                while (end < last && ((straight >> (end + 1)) & 1U) == 0) ++end;
                invalidate(dirty_.clipped({x_, y_ + strip_top(strip), x_ + width_, y_ + strip_bottom(end)}));
                strip = end;
            }
        }
    }
    if (straight != 0) ++direct_frames_;
    last_sent_ = straight;
    direct_ = 0;
    sent_.store(0, std::memory_order_relaxed);
    output_ = nullptr;
    ++frames_;
    last_frame_ms_ = drawn_ms_;
}

// ---- The panel straight ------------------------------------------------------

std::uint64_t DeviceFrame::all_strips() const noexcept {
    const int strips = strip_of(height_ - 1) + 1;
    return strips >= 64 ? ~0ULL : (1ULL << strips) - 1ULL;
}

FrameRect DeviceFrame::strip_window(int strip) const noexcept {
    const FrameRect changed{dirty_.x1 - x_, dirty_.y1 - y_, dirty_.x2 - x_, dirty_.y2 - y_};
    const FrameRect rows = FrameRect{0, strip_top(strip), width_, strip_bottom(strip)}.clipped(changed);
    if (rows.empty()) return {};
    // Panel windows start on even coordinates and end on odd ones.
    return {rows.x1 & ~1, rows.y1 & ~1, std::min(width_, (rows.x2 + 1) & ~1), std::min(height_, (rows.y2 + 1) & ~1)};
}

std::uint64_t DeviceFrame::strips_under(const lv_area_t& area) const noexcept {
    if (!on_screen_) return all_strips();
    const lv_area_t frame{screen_x_, screen_y_, screen_x_ + width_ - 1, screen_y_ + height_ - 1};
    lv_area_t common{};
    if (!lv_area_intersect(&common, &area, &frame)) return 0;
    const int first = static_cast<int>(common.y1) - screen_y_;
    const int last = static_cast<int>(common.y2) - screen_y_;
    std::uint64_t strips = 0;
    for (int strip = strip_of(first); strip <= strip_of(last) && strip < 64; ++strip) strips |= 1ULL << strip;
    return strips;
}

void DeviceFrame::plan_direct() noexcept {
    direct_ = 0;
    sent_.store(0, std::memory_order_relaxed);
    ready_.store(0, std::memory_order_relaxed);
    output_ = nullptr;
    spoiled_.store(0, std::memory_order_relaxed);
    SynapseOutput* output = synapse_output();
    if (!output || !worker_ || !image_ || !display_ || dirty_.empty() || strip_of(height_ - 1) >= 64 ||
        (width_ & 1) != 0 || (height_ & 1) != 0) {
        return;
    }
    // Where everything lies now, not at LVGL's last refresh: an object just
    // made or moved invalidates its place as it is laid out.
    lv_obj_update_layout(image_);
    lv_area_t coords{};
    lv_obj_get_coords(image_, &coords);
    if (lv_obj_get_display(image_) != display_ || (coords.x1 & 1) != 0 || (coords.y1 & 1) != 0 ||
        lv_area_get_width(&coords) != width_ || lv_area_get_height(&coords) != height_) {
        return;
    }
    if (on_screen_ && (coords.x1 != screen_x_ || coords.y1 != screen_y_)) {
        // Moved: what LVGL was told since was placed by the old place. LVGL
        // draws the frame where it is now first.
        screen_x_ = static_cast<int>(coords.x1);
        screen_y_ = static_cast<int>(coords.y1);
        lvgl_pending_.store(~0ULL, std::memory_order_relaxed);
        return;
    }
    on_screen_ = true;
    screen_x_ = static_cast<int>(coords.x1);
    screen_y_ = static_cast<int>(coords.y1);
    if (!shown_as_drawn(image_, coords)) return;
    // Everything drawn after the frame: its later siblings, its parents' later
    // siblings, and the layers above the screen.
    drawn_over_.clear();
    for (const lv_obj_t* object = image_; lv_obj_t* parent = lv_obj_get_parent(object); object = parent) {
        const auto count = static_cast<std::int32_t>(lv_obj_get_child_count(parent));
        for (std::int32_t index = lv_obj_get_index(object) + 1; index < count; ++index) {
            collect_drawn(lv_obj_get_child(parent, index), coords, drawn_over_);
        }
    }
    collect_drawn(lv_display_get_layer_top(display_), coords, drawn_over_);
    collect_drawn(lv_display_get_layer_sys(display_), coords, drawn_over_);
    // The changed strips nothing lies over.
    const int last = strip_of(dirty_.y2 - 1 - y_);
    for (int strip = strip_of(dirty_.y1 - y_); strip <= last; ++strip) {
        const FrameRect window = strip_window(strip);
        if (window.empty()) continue;
        const lv_area_t panel{screen_x_ + window.x1, screen_y_ + window.y1, screen_x_ + window.x2 - 1,
                              screen_y_ + window.y2 - 1};
        bool covered = false;
        for (const auto& over : drawn_over_) {
            lv_area_t common{};
            if (lv_area_intersect(&common, &over, &panel)) {
                covered = true;
                break;
            }
        }
        if (!covered) direct_ |= 1ULL << strip;
    }
    if (direct_ != 0) output_ = output;
}

bool DeviceFrame::send_strips(std::uint32_t timeout_ms, bool last) noexcept {
    SynapseOutput* output = output_;
    if (!output || !output->lock(timeout_ms)) return false;
    const std::uint16_t* pixels = buffers_[front_ ^ 1].get();
    // LVGL is still to draw there, from the frame shown until this one shows:
    // sent now, the strip would be drawn over. It waits for LVGL's drawing.
    std::uint64_t ready = ready_.exchange(0, std::memory_order_acquire);
    const std::uint64_t waiting = ready & lvgl_pending_.load(std::memory_order_relaxed);
    ready &= ~waiting;
    if (!last && waiting != 0) ready_.fetch_or(waiting, std::memory_order_relaxed);
    while (ready != 0) {
        const int strip = __builtin_ctzll(ready);
        const std::uint64_t bit = 1ULL << strip;
        ready &= ~bit;
        const FrameRect window = strip_window(strip);
        const auto columns = static_cast<std::size_t>(window.width());
        const std::size_t bytes = columns * static_cast<std::size_t>(window.height()) * sizeof(std::uint16_t);
        auto* memory = static_cast<std::uint16_t*>(output->strip(static_cast<int>(g_strip_turn & 1U), bytes));
        if (!memory) break;
        // The panel takes RGB565 high byte first.
        std::uint16_t* out = memory;
        for (int y = window.y1; y < window.y2; ++y, out += columns) {
            const std::uint16_t* row = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                                       static_cast<std::size_t>(window.x1);
            for (std::size_t x = 0; x < columns; ++x) {
                const unsigned value = row[x];
                out[x] = static_cast<std::uint16_t>((value << 8) | (value >> 8));
            }
        }
        if (!output->send(screen_x_ + window.x1, screen_y_ + window.y1, window.width(), window.height(), memory)) break;
        ++g_strip_turn;
        sent_.fetch_or(bit, std::memory_order_relaxed);
    }
    output->unlock();
    return true;
}

void DeviceFrame::listen(lv_display_t* display) noexcept {
    if (display == display_) return;
    stop_listening();
    if (!display) return;
    display_ = display;
    lv_display_add_event_cb(display, &DeviceFrame::display_event, LV_EVENT_ALL, this);
}

void DeviceFrame::stop_listening() noexcept {
    if (display_ && lv_is_initialized()) {
        lv_display_remove_event_cb_with_user_data(display_, &DeviceFrame::display_event, this);
    }
    display_ = nullptr;
    rendering_ = false;
}

void DeviceFrame::display_event(lv_event_t* event) {
    auto* self = static_cast<DeviceFrame*>(lv_event_get_user_data(event));
    switch (lv_event_get_code(event)) {
        case LV_EVENT_RENDER_START: self->rendering_ = true; break;
        case LV_EVENT_RENDER_READY: self->rendering_ = false; break;
        // Everything LVGL was told to draw before has been drawn.
        case LV_EVENT_REFR_READY: self->lvgl_pending_.store(0, std::memory_order_relaxed); break;
        case LV_EVENT_INVALIDATE_AREA: {
            // While LVGL draws, this event only tries its rounder on a draw
            // buffer's rows (lv_refr.c, get_max_row()): nothing is invalidated.
            if (self->rendering_) break;
            const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
            const std::uint64_t strips = area ? self->strips_under(*area) : self->all_strips();
            if (strips == 0) break;
            self->lvgl_pending_.fetch_or(strips, std::memory_order_relaxed);
            // LVGL draws them from the frame shown: those already sent go to LVGL again.
            self->spoiled_.fetch_or(strips & self->sent_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            break;
        }
        default: break;
    }
}

}  // namespace aura::ui

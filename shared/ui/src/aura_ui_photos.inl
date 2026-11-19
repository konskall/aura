    PhotoSnapshot photos{};
    std::size_t photo_page{0};
    bool photo_fullscreen{false};
    PhotoZoom photo_zoom;
    PhotoBuffer<std::uint16_t> photo_zoom_pixels;
    lv_image_dsc_t photo_zoom_descriptor{};
    lv_obj_t* photo_frame{};
    lv_obj_t* photo_zoom_image{};
    const PhotoImage* photo_zoom_shown{nullptr};
    bool photo_touch_moved{false};
    int photo_touch_travel{0};
    std::size_t photo_touch_points{0};
    std::string photo_delete_name;
    lv_image_dsc_t photo_descriptor{}, ambient_descriptor{};
    const l10n::PhotoStrings& p() const { return l10n::photo_strings(interface_language); }
    static void photos_open_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->photo_page = 0;
        if (self->callbacks.on_photos_refresh) self->callbacks.on_photos_refresh();
        self->navigate(UiView::Photos);
    }
    static void photos_refresh_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->accept_action() && self->callbacks.on_photos_refresh) self->callbacks.on_photos_refresh();
    }
    static void photo_select_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action() || !self->callbacks.on_photo_select) return;
        const auto index = user_value(event);
        if (!self->callbacks.on_photo_select(index)) return;
        self->photo_fullscreen = false;
        self->navigate(UiView::PhotoViewer);
    }
    static void photo_page_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->photo_page = user_value(event);
        self->render(UiView::Photos);
    }
    static void photo_next_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->accept_action() && self->callbacks.on_photo_advance)
            self->callbacks.on_photo_advance(user_value(event) ? 1 : -1);
    }
    static void photo_play_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->accept_action() && self->callbacks.on_photo_play)
            self->callbacks.on_photo_play(!self->photos.playing);
    }
    static void photo_fullscreen_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        // A tap only: never the end of a pinch or of a drag across the picture.
        if (self->photo_touch_moved || !self->accept_action()) return;
        self->photo_fullscreen = !self->photo_fullscreen;
        self->render(UiView::PhotoViewer);
    }

    // ---- Zooming with two fingers (the owner, 2026-11-18) -------------------

    // The picture as it is zoomed now, in the frame's own image: a plain copy to
    // draw, where LVGL's own scaling would transform every pixel of every frame.
    void photo_zoom_show(bool smooth) {
        if (!photo_frame || !photos.image) return;
        auto* picture = lv_obj_get_child(photo_frame, 0);
        if (!photo_zoom.zoomed()) {
            if (photo_zoom_image) lv_obj_set_hidden(photo_zoom_image, true);
            if (picture) lv_obj_set_hidden(picture, false);
            return;
        }
        const auto width = photo_zoom.frame_width();
        const auto height = photo_zoom.frame_height();
        const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (photo_zoom_pixels.size() != count && !photo_zoom_pixels.resize(count)) {
            photo_zoom.reset();
            return;
        }
        photo_zoom_render(*photos.image, photo_zoom, photo_zoom_pixels.data(), smooth);
        photo_zoom_descriptor = {};
        photo_zoom_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        photo_zoom_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
        photo_zoom_descriptor.header.w = static_cast<std::uint32_t>(width);
        photo_zoom_descriptor.header.h = static_cast<std::uint32_t>(height);
        photo_zoom_descriptor.header.stride = static_cast<std::uint32_t>(width) * 2U;
        photo_zoom_descriptor.data_size = static_cast<std::uint32_t>(count * 2U);
        photo_zoom_descriptor.data = reinterpret_cast<const std::uint8_t*>(photo_zoom_pixels.data());
        if (!photo_zoom_image) {
            photo_zoom_image = lv_image_create(photo_frame);
            lv_obj_set_pos(photo_zoom_image, 0, 0);
            lv_obj_set_clickable(photo_zoom_image, false);
        }
        lv_image_cache_drop(&photo_zoom_descriptor);
        lv_image_set_src(photo_zoom_image, &photo_zoom_descriptor);
        lv_obj_set_hidden(photo_zoom_image, false);
        if (picture && picture != photo_zoom_image) lv_obj_set_hidden(picture, true);
        lv_obj_invalidate(photo_zoom_image);
    }

    // Each read of the fingers: two of them on an open photo zoom it.
    void photo_touch(const TouchPoint* points, std::size_t count) {
        if (current_view != UiView::PhotoViewer || !photo_frame) return;
        if (count >= 2U) {
            lv_area_t area{};
            lv_obj_get_coords(photo_frame, &area);
            const auto before = photo_zoom.zoom();
            const auto center_x = photo_zoom.center_x();
            const auto center_y = photo_zoom.center_y();
            photo_zoom.pinch(static_cast<float>(points[0].x - area.x1), static_cast<float>(points[0].y - area.y1),
                             static_cast<float>(points[1].x - area.x1), static_cast<float>(points[1].y - area.y1));
            photo_touch_moved = true;
            if (photo_zoom.zoom() != before || photo_zoom.center_x() != center_x || photo_zoom.center_y() != center_y) {
                photo_zoom_show(false);
            }
            return;
        }
        if (photo_zoom.pinching()) {
            photo_zoom.pinch_end();
            photo_zoom_show(true);
        }
    }

    // One finger on the picture: a drag moves a zoomed one.
    static void photo_frame_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        const auto code = lv_event_get_code(event);
        if (code == LV_EVENT_PRESSED) {
            self->photo_touch_moved = false;
            self->photo_touch_travel = 0;
            return;
        }
        if (code == LV_EVENT_RELEASED) {
            if (self->photo_zoom.zoomed() && !self->photo_zoom.pinching()) self->photo_zoom_show(true);
            return;
        }
        lv_point_t step{};
        lv_indev_get_vect(lv_indev_active(), &step);
        self->photo_touch_travel += std::abs(step.x) + std::abs(step.y);
        if (self->photo_touch_travel > 10) self->photo_touch_moved = true;
        if (!self->photo_zoom.zoomed() || self->photo_zoom.pinching() || (step.x == 0 && step.y == 0)) return;
        self->photo_zoom.pan(static_cast<float>(step.x), static_cast<float>(step.y));
        self->photo_zoom_show(false);
    }
    static void photo_settings_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->accept_action()) self->navigate(UiView::PhotoSettings);
    }
    static void photo_interval_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (!self->callbacks.on_photo_interval ||
            !self->callbacks.on_photo_interval(static_cast<unsigned>(user_value(event))))
            self->show_toast(self->p().save_failed, ui::tokens::coral);
    }
    static void photo_ambient_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const bool enabled = !self->photos.settings.ambient_name.empty();
        if (!self->callbacks.on_photo_ambient || !self->callbacks.on_photo_ambient(!enabled))
            self->show_toast(self->p().save_failed, ui::tokens::coral);
    }
    static void photo_use_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->accept_action() && (!self->callbacks.on_photo_ambient || !self->callbacks.on_photo_ambient(true)))
            self->show_toast(self->p().save_failed, ui::tokens::coral);
    }
    static void photo_delete_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action() || self->photos.selected >= self->photos.names.size() || self->photos.deleting)
            return;
        self->photo_delete_name = self->photos.names[self->photos.selected];
        if (self->callbacks.on_photo_play) self->callbacks.on_photo_play(false);
        self->navigate(UiView::PhotoDelete);
    }
    static void photo_delete_confirm_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (!self->callbacks.on_photo_delete || !self->callbacks.on_photo_delete(self->photo_delete_name)) {
            self->show_toast(self->p().delete_failed, ui::tokens::coral);
            return;
        }
        self->photo_delete_name.clear();
        self->return_to(UiView::Photos);
    }
    static void photo_delete_cancel_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->accept_action()) {
            self->photo_delete_name.clear();
            self->go_back();
        }
    }
    void render_photo_delete() {
        make_header(p().delete_title);
        if (photo_delete_name.empty() && photos.selected < photos.names.size())
            photo_delete_name = photos.names[photos.selected];
        if (photos.demo) header_tag(t().demo, ui::tokens::amber);
        auto* well = icon_well(content, 80, ui::IconRole::Delete, ui::tokens::coral, 40);
        lv_obj_set_pos(well, 200, 96);
        auto* name = label(content, photo_delete_name.c_str(), 32, 192, 416, ui::font_caption(), ui::tokens::text,
                           LV_TEXT_ALIGN_CENTER);
        lv_obj_set_height(name, 112);
        label(content, p().delete_hint, 32, 312, 416, ui::font_caption(), ui::tokens::text_secondary,
              LV_TEXT_ALIGN_CENTER);
        auto* cancel = pill(content, p().cancel, ui::ButtonTone::Secondary, 24, 396, 204, photo_delete_cancel_event);
        auto* confirm =
            pill(content, p().delete_confirm, ui::ButtonTone::Danger, 240, 396, 216, photo_delete_confirm_event);
        lv_obj_set_height(cancel, 64);
        lv_obj_set_height(confirm, 64);
    }
    const char* photo_error_text() const {
        switch (photos.error) {
            case PhotoError::TooLarge: return p().too_large;
            case PhotoError::Storage: return p().missing;
            case PhotoError::Save: return p().save_failed;
            case PhotoError::Delete: return p().delete_failed;
            default: return p().bad_file;
        }
    }
    lv_obj_t* photo_image(lv_obj_t* parent, const PhotoImage& image, lv_image_dsc_t& descriptor, int x, int y,
                          int width, int height) {
        if (!image.width || !image.height || image.pixels.size() != image.width * image.height) return nullptr;
        descriptor = {};
        descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
        descriptor.header.w = image.width;
        descriptor.header.h = image.height;
        descriptor.header.stride = image.width * 2;
        descriptor.data_size = static_cast<std::uint32_t>(image.pixels.size() * 2);
        descriptor.data = reinterpret_cast<const std::uint8_t*>(image.pixels.data());
        auto* frame = lv_obj_create(parent);
        ui::reset(frame);
        lv_obj_set_pos(frame, x, y);
        lv_obj_set_size(frame, width, height);
        lv_obj_set_style_bg_color(frame, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
        auto* picture = lv_image_create(frame);
        lv_image_set_src(picture, &descriptor);
        const auto scale = std::min(1024U, std::min(static_cast<unsigned>(width) * 256U / image.width,
                                                    static_cast<unsigned>(height) * 256U / image.height));
        lv_image_set_scale(picture, scale);
        lv_obj_center(picture);
        lv_obj_set_clickable(picture, false);
        return frame;
    }
    void render_photos() {
        make_header(p().title, ui::IconRole::Settings, photo_settings_event, ui::tokens::mint);
        auto* list = make_list();
        if (photos.scanning || photos.names.empty()) {
            auto* well = icon_well(content, 80, ui::IconRole::Image, ui::tokens::mint, 40);
            lv_obj_set_pos(well, 200, 128);
            label(content,
                  photos.scanning ? p().loading : (photos.error == PhotoError::Storage ? p().missing : p().empty),
                  kGutter, 230, kWidth, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            label(content, photos.desktop ? p().desktop_hint : p().card_hint, kGutter + 8, 286, kWidth - 16,
                  ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            // Files arrive through Αρχεία now, where File transfer lives.
            auto* send = pill(content, fx().title, ui::ButtonTone::Primary, 24, 396, 344, files_open_event);
            lv_obj_set_height(send, 64);
            auto* refresh =
                round_button(content, 400, 400, 56, ui::IconRole::Refresh, ui::tokens::mint, photos_refresh_event);
            ui::set_button_disabled(refresh, photos.scanning);
            return;
        }
        const std::size_t pages = (photos.names.size() + 7) / 8;
        photo_page = std::min(photo_page, pages - 1);
        int y = 0;
        if (photos.error == PhotoError::Delete || photos.deleting) {
            line(list, photos.deleting ? p().deleting : p().delete_failed, kGutter, 0, kWidth, ui::font_caption(),
                 ui::tokens::coral);
            y = 52;
        }
        for (std::size_t index = photo_page * 8; index < photos.names.size() && index < (photo_page + 1) * 8; ++index) {
            auto* row = list_row(list, y, photos.names[index].c_str(), nullptr, ui::IconRole::Image, ui::tokens::mint);
            tappable(row, photo_select_event, index);
            y += kRowPitch;
        }
        if (pages > 1) {
            auto* prev =
                round_button(list, kGutter, y, 56, ui::IconRole::ChevronLeft, ui::tokens::mint, photo_page_event);
            set_value(prev, photo_page ? photo_page - 1 : 0);
            ui::set_button_disabled(prev, photo_page == 0);
            char page[48];
            std::snprintf(page, sizeof(page), p().page, static_cast<unsigned>(photo_page + 1),
                          static_cast<unsigned>(pages));
            line(list, page, 96, y + 12, 288, ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            auto* next = round_button(list, 400, y, 56, ui::IconRole::Chevron, ui::tokens::mint, photo_page_event);
            set_value(next, photo_page + 1);
            ui::set_button_disabled(next, photo_page + 1 >= pages);
            y += 72;
        }
        auto* refresh = list_row(list, y, p().refresh, nullptr, ui::IconRole::Refresh, ui::tokens::mint);
        tappable(refresh, photos_refresh_event, 0);
        y += kRowPitch;
        list_end(list, y);
    }
    void render_photo_viewer() {
        if (!photo_fullscreen) {
            make_header(p().title, ui::IconRole::Settings, photo_settings_event, ui::tokens::mint);
        } else {
            header_action = true;
        }
        const int top = photo_fullscreen ? 0 : 96, height = photo_fullscreen ? 480 : 268;
        photo_frame = nullptr;
        photo_zoom_image = nullptr;
        if (photos.image && !photos.loading) {
            if (auto* frame = photo_image(content, *photos.image, photo_descriptor, 0, top, 480, height)) {
                lv_obj_set_clickable(frame, true);
                lv_obj_add_event_cb(frame, photo_fullscreen_event, LV_EVENT_CLICKED, this);
                for (const auto code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED})
                    lv_obj_add_event_cb(frame, photo_frame_event, code, this);
                // Another picture starts whole; the same one keeps its zoom.
                if (photo_zoom_shown != photos.image.get()) {
                    photo_zoom_shown = photos.image.get();
                    photo_zoom.reset();
                }
                photo_zoom.set_frame(static_cast<int>(photos.image->width), static_cast<int>(photos.image->height), 480,
                                     height);
                photo_frame = frame;
                photo_zoom_show(true);
            }
        } else {
            label(content, photos.loading ? p().loading : photo_error_text(), kGutter + 12, top + 96, kWidth - 24,
                  ui::font_body(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        }
        if (photo_fullscreen) {
            if (photos.demo) {
                auto* demo = ui::make_tag(content, t().demo, ui::tokens::amber);
                lv_obj_set_pos(demo, 16, 16);
            }
            return;
        }
        if (photos.selected < photos.names.size())
            line(content, photos.names[photos.selected].c_str(), kGutter, 368, kWidth, ui::font_caption(),
                 ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        char count[48];
        std::snprintf(count, sizeof(count), p().count, static_cast<unsigned>(photos.selected + 1),
                      static_cast<unsigned>(photos.names.size()));
        auto* prev = round_button(content, 24, 408, 56, ui::IconRole::ChevronLeft, ui::tokens::mint, photo_next_event);
        set_value(prev, 0);
        auto* play = round_button(content, 212, 404, 64, photos.playing ? ui::IconRole::Pause : ui::IconRole::Play,
                                  ui::tokens::mint, photo_play_event);
        if (photos.selected < photos.names.size() && callbacks.on_photo_delete) {
            auto* remove =
                round_button(content, 312, 408, 56, ui::IconRole::Delete, ui::tokens::coral, photo_delete_event);
            ui::set_button_disabled(remove, photos.deleting || photos.scanning);
        }
        auto* next = round_button(content, 400, 408, 56, ui::IconRole::Chevron, ui::tokens::mint, photo_next_event);
        set_value(next, 1);
        line(content, count, 88, 424, 112, ui::font_caption(), ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
        ui::set_button_disabled(prev, photos.names.size() < 2 || photos.loading);
        ui::set_button_disabled(next, photos.names.size() < 2 || photos.loading);
        ui::set_button_disabled(play, photos.names.size() < 2 || !photos.image || photos.loading);
    }
    void render_photo_settings() {
        make_header(p().settings);
        if (photos.demo) header_tag(t().demo, ui::tokens::amber);
        const bool can_delete = photos.selected < photos.names.size() && callbacks.on_photo_delete;
        auto* list = make_list(kHeader, can_delete ? 384 - kHeader : ui::tokens::display_height - kHeader);
        line(list, p().interval, kGutter, 4, kWidth, ui::font_caption(), ui::tokens::text_secondary);
        const unsigned intervals[]{5, 10, 30, 60};
        for (unsigned i = 0; i < 4; ++i) {
            char text[32];
            std::snprintf(text, sizeof(text), p().seconds, intervals[i]);
            const bool selected = photos.settings.interval_seconds == intervals[i];
            auto* tile = lv_button_create(list);
            ui::reset(tile);
            ui::style_platter(tile, selected ? ui::tokens::mint : ui::tokens::blue, ui::tokens::radius_medium,
                              selected);
            lv_obj_set_pos(tile, kGutter + (i % 2) * 222, 36 + (i / 2) * 72);
            lv_obj_set_size(tile, 210, 64);
            lv_obj_center(ui::make_label(tile, intervals[i] == 60 ? p().minute : text,
                                         selected ? ui::tokens::mint : ui::tokens::text, ui::font_caption()));
            tappable(tile, photo_interval_event, intervals[i]);
        }
        const bool chosen =
            photos.selected < photos.names.size() && photos.settings.ambient_name == photos.names[photos.selected];
        const bool enabled = !photos.settings.ambient_name.empty();
        auto* row = list_row(list, 184, p().ambient, enabled ? p().ambient_hint : p().ambient_none, ui::IconRole::Moon,
                             ui::tokens::mint, kRowHeight, 252);
        add_switch(row, enabled);
        tappable(row, photo_ambient_event, 0);
        ui::set_button_disabled(row, !enabled && (!photos.image || photos.loading));
        if (enabled && !chosen && photos.image && !photos.loading) {
            auto* use = pill(list, p().use_photo, ui::ButtonTone::Primary, kGutter, 282, kWidth, photo_use_event);
            lv_obj_set_height(use, 56);
        } else if (enabled) {
            line(list, photos.settings.ambient_name.c_str(), kGutter + 8, 286, kWidth - 16, ui::font_caption(),
                 ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        }
        line(list, photos.error == PhotoError::Save ? p().save_failed : p().local_hint, kGutter + 4, 348, kWidth - 8,
             ui::font_caption(), ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
        if (can_delete) {
            auto* remove =
                pill(content, p().delete_photo, ui::ButtonTone::Danger, kGutter, 396, kWidth, photo_delete_event);
            lv_obj_set_height(remove, 64);
            ui::set_button_disabled(remove, photos.deleting || photos.scanning);
        }
        list_end(list, 380);
    }

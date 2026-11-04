    TransferSnapshot transfer{};
    PhotoBuffer<std::uint16_t> transfer_qr_pixels;
    lv_image_dsc_t transfer_qr_descriptor{};
    lv_obj_t* transfer_timer{};
    lv_obj_t* transfer_bar{};
    const l10n::TransferStrings& f() const { return l10n::transfer_strings(interface_language); }
    static void transfer_start_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (!self->callbacks.on_transfer_start || !self->callbacks.on_transfer_start())
            self->show_toast(self->f().failed, ui::tokens::coral);
    }
    static void transfer_stop_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->accept_action() && self->callbacks.on_transfer_stop) self->callbacks.on_transfer_stop();
    }
    static void transfer_browser_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->accept_action() && self->callbacks.on_transfer_browser) self->callbacks.on_transfer_browser();
    }
    bool transfer_qr() {
        std::array<std::uint8_t, aura_qrcodegen_BUFFER_LEN_FOR_VERSION(6)> temp{}, code{};
        if (!aura_qrcodegen_encodeText(transfer.address.c_str(), temp.data(), code.data(), aura_qrcodegen_Ecc_MEDIUM, 1,
                                       6, aura_qrcodegen_Mask_AUTO, true))
            return false;
        constexpr unsigned edge = 216;
        if (transfer_qr_pixels.size() != edge * edge && !transfer_qr_pixels.resize(edge * edge)) return false;
        lv_image_cache_drop(&transfer_qr_descriptor);  // the pixels change below
        const auto modules = static_cast<unsigned>(aura_qrcodegen_getSize(code.data()));
        const unsigned scale = edge / (modules + 8), origin = (edge - modules * scale) / 2;
        for (unsigned y = 0; y < edge; ++y)
            for (unsigned x = 0; x < edge; ++x) {
                const bool inside =
                    x >= origin && y >= origin && x < origin + modules * scale && y < origin + modules * scale;
                const bool black =
                    inside && aura_qrcodegen_getModule(code.data(), static_cast<int>((x - origin) / scale),
                                                       static_cast<int>((y - origin) / scale));
                transfer_qr_pixels[y * edge + x] = black ? 0 : 0xFFFF;
            }
        // A plain image, not an lv_canvas: LVGL 9.6's canvas destructor drops the
        // wrong image-cache key (the address of its buffer pointer).
        transfer_qr_descriptor = {};
        transfer_qr_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        transfer_qr_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
        transfer_qr_descriptor.header.w = edge;
        transfer_qr_descriptor.header.h = edge;
        transfer_qr_descriptor.header.stride = edge * 2;
        transfer_qr_descriptor.data_size = edge * edge * 2;
        transfer_qr_descriptor.data = reinterpret_cast<const std::uint8_t*>(transfer_qr_pixels.data());
        auto* image = lv_image_create(content);
        lv_image_set_src(image, &transfer_qr_descriptor);
        lv_obj_set_pos(image, 24, 140);
        return true;
    }
    const char* transfer_problem() const {
        switch (transfer.error) {
            case TransferError::Network: return f().network;
            case TransferError::Storage: return f().storage;
            case TransferError::Exists: return f().exists;
            case TransferError::Size:
            case TransferError::Image: return f().image;
            case TransferError::Interrupted: return f().interrupted;
            default: return f().failed;
        }
    }
    void update_transfer_progress() {
        if (transfer_timer) {
            char remaining[64];
            std::snprintf(remaining, sizeof(remaining), f().remaining, transfer.remaining_seconds / 60,
                          transfer.remaining_seconds % 60);
            set_text_if_changed(transfer_timer, remaining);
        }
        if (transfer_bar)
            lv_bar_set_value(transfer_bar,
                             transfer.total ? static_cast<int>(transfer.received * 100 / transfer.total) : 0,
                             LV_ANIM_OFF);
    }
    void render_transfer() {
        transfer_timer = nullptr;
        transfer_bar = nullptr;
        const bool receiving = transfer.state == TransferState::Receiving;
        const bool ready = transfer.state == TransferState::Ready || receiving;
        const bool starting = transfer.state == TransferState::Starting;
        if (callbacks.on_transfer_browser && ready && !transfer.demo)
            make_header(f().title, ui::IconRole::Globe, transfer_browser_event, ui::tokens::mint);
        else
            make_header(f().title);
        if (!ready) {
            if (transfer.demo) header_tag(t().demo, ui::tokens::amber);
            const auto* title = starting                                   ? f().starting
                                : transfer.state == TransferState::Expired ? f().expired
                                : transfer.state == TransferState::Locked  ? f().locked
                                : transfer.state == TransferState::Failed  ? f().failed
                                                                           : f().entry;
            auto* well = icon_well(content, 88, ui::IconRole::Download, ui::tokens::mint, 44);
            lv_obj_set_pos(well, 196, 116);
            label(content, title, kGutter, 224, kWidth, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            label(content, transfer.state == TransferState::Failed ? transfer_problem() : f().intro, kGutter + 8, 276,
                  kWidth - 16, ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            pill(content, starting ? f().stop : f().start, starting ? ui::ButtonTone::Danger : ui::ButtonTone::Primary,
                 kGutter, 384, kWidth, starting ? transfer_stop_event : transfer_start_event);
            return;
        }
        std::string status = transfer.loopback ? f().local_only : f().ready;
        if (receiving) status = f().receiving;
        if (transfer.demo) status = std::string(t().demo) + " · " + status;
        line(content, status.c_str(), kGutter, 100, kWidth, ui::font_caption(), ui::tokens::mint);
        transfer_qr();
        line(content, f().pin, 264, 140, 192, ui::font_caption(), ui::tokens::text_secondary);
        auto pin = transfer.pin;
        if (pin.size() == 6) pin.insert(3, " ");
        line(content, pin.c_str(), 264, 174, 192, ui::font_title(), ui::tokens::text);
        auto* address =
            label(content, transfer.address.c_str(), 264, 232, 192, ui::font_caption(), ui::tokens::text_secondary);
        lv_obj_set_height(address, 90);
        transfer_timer = line(content, "", 264, 328, 192, ui::font_caption(), ui::tokens::text_secondary);
        char completed[48];
        std::snprintf(completed, sizeof(completed), f().completed, transfer.completed);
        std::string detail =
            transfer.error != TransferError::None ? transfer_problem()
            : receiving                           ? transfer.last_name
                        : std::string(completed) + (transfer.last_name.empty() ? "" : " · " + transfer.last_name);
        line(content, detail.c_str(), 24, 366, 432, ui::font_caption(), ui::tokens::text_secondary);
        transfer_bar = lv_bar_create(content);
        lv_obj_set_pos(transfer_bar, 24, 394);
        lv_obj_set_size(transfer_bar, 432, 4);
        lv_obj_set_style_bg_color(transfer_bar, ui::color(ui::tokens::mint), LV_PART_INDICATOR);
        lv_obj_set_hidden(transfer_bar, !receiving);
        auto* stop = pill(content, f().stop, ui::ButtonTone::Danger, kGutter, 408, kWidth, transfer_stop_event);
        lv_obj_set_height(stop, 64);
        update_transfer_progress();
    }

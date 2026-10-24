    // ---- Sound and System settings -------------------------------------------
    // Included inside AuraUi::Impl (see aura_ui.cpp).

    static void sound_setting_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        auto next = self->sound;
        next.volume = self->audio.volume;
        next.muted = self->audio.muted;
        if (user_value(event) == 0) next.message_chime = !next.message_chime;
        else next.spoken_replies = !next.spoken_replies;
        self->sound = next;
        if (self->callbacks.on_sound_settings) self->callbacks.on_sound_settings(next);
        self->render(UiView::Audio);
    }

    static void sound_preview_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (self->callbacks.on_sound_preview) self->callbacks.on_sound_preview();
    }

    static void diagnostics_tone_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (self->callbacks.on_audio_test) self->callbacks.on_audio_test();
    }

    static void diagnostics_wav_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (self->callbacks.on_audio_wav) self->callbacks.on_audio_wav();
    }

    static void system_action_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->pending_system = static_cast<SystemAction>(user_value(event));
        self->navigate(UiView::SystemConfirm);
    }

    static void system_confirm_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        lv_indev_wait_release(lv_indev_active());
        // The platform answers with boot() or power_off().
        if (self->callbacks.on_system_action) self->callbacks.on_system_action(self->pending_system);
    }

    static void power_on_event(lv_event_t* event) {
        auto* self = self_of(event);
        lv_indev_wait_release(lv_indev_active());
        if (self->callbacks.on_system_action) self->callbacks.on_system_action(SystemAction::PowerOn);
    }

    void update_volume_label() {
        if (!sound_volume_label) return;
        char percent[16];
        std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(audio.volume * 100.0F + 0.5F));
        set_text_if_changed(sound_volume_label, percent);
    }

    // Volume, mute, the message chime, spoken answers and a test sound. The
    // speaker tests and the demo WAV live in Diagnostics.
    void render_audio() {
        make_header(v().sound);
        auto* list = make_list();
        int y = 0;
        auto* volume_row = list_row(list, y, c().sound_volume, nullptr, ui::IconRole::VolumeUp, ui::tokens::ice,
                                    kRowHeight, 220);
        sound_volume_label = ui::make_label(volume_row, "", ui::tokens::text_secondary, ui::font_headline());
        lv_obj_align(sound_volume_label, LV_ALIGN_RIGHT_MID, 0, 0);
        update_volume_label();
        y += kRowHeight + 22;
        auto* volume = lv_slider_create(list);
        lv_obj_set_size(volume, kWidth - 48, 10);
        lv_obj_set_pos(volume, kGutter + 24, y);
        lv_slider_set_range(volume, 0, 100);
        lv_slider_set_value(volume, static_cast<int>(audio.volume * 100.0F + 0.5F), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(volume, ui::color(ui::tokens::surface_high), LV_PART_MAIN);
        lv_obj_set_style_bg_color(volume, ui::color(ui::tokens::ice), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(volume, ui::color(ui::tokens::text), LV_PART_KNOB);
        lv_obj_set_style_pad_all(volume, 8, LV_PART_KNOB);
        lv_obj_set_ext_click_area(volume, 18);
        lv_obj_add_event_cb(volume, audio_volume_event, LV_EVENT_VALUE_CHANGED, this);
        y += 40;

        auto* mute_row = list_row(list, y, c().sound_mute, c().sound_mute_detail, ui::IconRole::Muted,
                                  ui::tokens::coral, kRowHeight, 252);
        add_switch(mute_row, audio.muted);
        lv_obj_set_clickable(mute_row, true);
        lv_obj_add_event_cb(mute_row, audio_mute_event, LV_EVENT_CLICKED, this);
        y += kRowPitch;
        auto* chime_row = list_row(list, y, c().sound_chime, c().sound_chime_detail, ui::IconRole::BellRing,
                                   ui::tokens::pink, kRowHeight, 252);
        add_switch(chime_row, sound.message_chime);
        tappable(chime_row, sound_setting_event, 0);
        y += kRowPitch;
        auto* spoken_row = list_row(list, y, c().sound_spoken, c().sound_spoken_detail, ui::IconRole::Speaker,
                                    ui::tokens::lilac, kRowHeight, 252);
        add_switch(spoken_row, sound.spoken_replies);
        tappable(spoken_row, sound_setting_event, 1);
        y += kRowPitch + 8;

        auto* preview = pill(list, c().sound_preview, ui::ButtonTone::Primary, kGutter, y, kWidth,
                             sound_preview_event);
        ui::set_button_disabled(preview, !callbacks.on_sound_preview || audio.muted);
        y += kButtonHeight + 12;
        auto* note = label(list, c().sound_alarm_note, kGutter, y, kWidth, ui::font_caption(),
                           ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
        lv_obj_update_layout(note);
        list_end(list, y + lv_obj_get_height(note));
    }

    // Software, diagnostics and the power actions.
    void render_system() {
        make_header(c().system_title);
        auto* list = make_list();
        int y = 0;
        char version[64];
        std::snprintf(version, sizeof(version), c().system_software_format, update.current_version.c_str());
        settings_row(list, y, c().system_update, update.current_version.empty() ? nullptr : version,
                     ui::IconRole::Download, ui::tokens::blue, UiView::Update);
        y += kRowPitch;
        settings_row(list, y, c().system_diagnostics, c().system_diagnostics_detail, ui::IconRole::Cpu,
                     ui::tokens::lilac, UiView::Diagnostics);
        y += kRowPitch + 8;
        line(list, c().system_power, kGutter + 4, y, kWidth - 8, ui::font_caption(), ui::tokens::text_secondary);
        y += 40;
        const bool available = static_cast<bool>(callbacks.on_system_action);
        const struct {
            const char* title;
            const char* detail;
            ui::IconRole role;
            std::uint32_t accent;
            SystemAction action;
        } actions[] = {
            {c().system_restart, nullptr, ui::IconRole::Refresh, ui::tokens::ice, SystemAction::Restart},
            {c().system_power_off, nullptr, ui::IconRole::Power, ui::tokens::amber, SystemAction::PowerOff},
            {c().system_factory_reset, c().system_factory_reset_detail, ui::IconRole::Warning, ui::tokens::coral,
             SystemAction::FactoryReset},
        };
        for (const auto& action : actions) {
            auto* row = list_row(list, y, action.title, action.detail, action.role, action.accent);
            add_chevron(row);
            if (available) tappable(row, system_action_event, static_cast<std::uintptr_t>(action.action));
            else lv_obj_set_style_opa(row, LV_OPA_60, 0);
            y += kRowPitch;
        }
        list_end(list, y, 8);
    }

    void render_system_confirm() {
        make_header(c().system_title);
        const char* question = c().system_restart_question;
        const char* detail = c().system_restart_detail;
        const char* confirm = c().system_restart;
        ui::IconRole role = ui::IconRole::Refresh;
        std::uint32_t accent = ui::tokens::ice;
        ui::ButtonTone tone = ui::ButtonTone::Primary;
        if (pending_system == SystemAction::PowerOff) {
            question = c().system_power_off_question;
            detail = c().system_power_off_detail;
            confirm = c().system_power_off;
            role = ui::IconRole::Power;
            accent = ui::tokens::amber;
        } else if (pending_system == SystemAction::FactoryReset) {
            question = c().system_reset_question;
            detail = c().system_reset_detail;
            confirm = c().system_reset_confirm;
            role = ui::IconRole::Warning;
            accent = ui::tokens::coral;
            tone = ui::ButtonTone::Danger;
        }
        state_symbol(100, 88, role, accent);
        state_text(204, question, detail);
        pill(content, t().cancel, ui::ButtonTone::Secondary, kGutter, kActionY, kHalfWidth, back_event);
        auto* go = pill(content, confirm, tone, kGutter + kHalfWidth + 16, kActionY, kHalfWidth,
                        system_confirm_event);
        ui::set_button_disabled(go, !callbacks.on_system_action);
    }

    // Switched off: black, and only a long press anywhere does anything.
    void render_power_off() {
        header_action = true;
        auto* catcher = lv_button_create(content);
        ui::reset(catcher);
        lv_obj_set_size(catcher, ui::tokens::display_width, ui::tokens::display_height);
        lv_obj_set_pos(catcher, 0, 0);
        lv_obj_set_style_bg_opa(catcher, LV_OPA_TRANSP, 0);
        lv_obj_set_clickable(catcher, true);
        lv_obj_add_event_cb(catcher, power_on_event, LV_EVENT_LONG_PRESSED, this);
    }

    void render_boot() {
        header_action = true;
        auto* brand = line(content, t().aura_name, 0, 184, ui::tokens::display_width, ui::font_title(),
                           ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_letter_space(brand, 8, 0);
        boot_spinner = spinner_arc(content, 44, ui::tokens::ice, 3);
        lv_obj_set_pos(boot_spinner, (ui::tokens::display_width - 44) / 2, 280);
        line(content, c().system_starting, 0, 340, ui::tokens::display_width, ui::font_caption(),
             ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
    }

    void system_tick(std::uint32_t elapsed_ms) {
        if (boot_remaining_ms == 0) return;
        if (boot_spinner && !reduced_motion) {
            lv_arc_set_rotation(boot_spinner, static_cast<int32_t>((phase_ms / 4U) % 360U));
        }
        boot_remaining_ms = elapsed_ms >= boot_remaining_ms ? 0U : boot_remaining_ms - elapsed_ms;
        if (boot_remaining_ms == 0) {
            navigation.reset();
            render(UiView::Home, true);
        }
    }

    // Only the boot and switched-off screens show while the device is off or starting.
    bool blocked_by_power(UiView view) const {
        if (powered_off) return view != UiView::PowerOff;
        if (boot_remaining_ms > 0) return view != UiView::Boot;
        return false;
    }

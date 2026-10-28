    // ---- Sound and System settings -------------------------------------------
    // Included inside AuraUi::Impl (see aura_ui.cpp).

    static void sound_setting_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        auto next = self->sound;
        next.volume = self->audio.volume;
        next.muted = self->audio.muted;
        if (user_value(event) == 0) next.message_chime = !next.message_chime;
        else if (user_value(event) == 1) next.spoken_replies = !next.spoken_replies;
        else if (user_value(event) == 2) next.continuous_conversation = !next.continuous_conversation;
        else if (user_value(event) == 3) next.barge_in = !next.barge_in;
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
        self->pending_system = SystemAction::PowerOn;
        if (self->callbacks.on_system_action) self->callbacks.on_system_action(SystemAction::PowerOn);
    }

    static void voice_choice_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        const auto selected = static_cast<AssistantVoice>(user_value(event));
        if (selected != AssistantVoice::Athina && selected != AssistantVoice::Nestoras) return;
        self->sound.assistant_voice = selected;
        self->sound.volume = self->audio.volume;
        self->sound.muted = self->audio.muted;
        if (self->callbacks.on_sound_settings) self->callbacks.on_sound_settings(self->sound);
        self->render(UiView::VoiceSettings);
    }

    static void voice_preview_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (self->callbacks.on_voice_preview) self->callbacks.on_voice_preview(self->sound.assistant_voice);
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
        const bool speech_ready = capabilities.speech_output;
        if (speech_ready) tappable(spoken_row, sound_setting_event, 1);
        else ui::set_button_disabled(spoken_row, true);
        y += kRowPitch;
        auto* continuous_row = list_row(list, y, c().sound_continuous, c().sound_continuous_detail,
                                        ui::IconRole::Voice, ui::tokens::ice, kRowHeight, 252);
        add_switch(continuous_row, sound.continuous_conversation);
        const bool conversation_ready = capabilities.microphone && capabilities.speech_recognition &&
                                        capabilities.speech_output;
        if (conversation_ready) tappable(continuous_row, sound_setting_event, 2);
        else ui::set_button_disabled(continuous_row, true);
        y += kRowPitch;
        auto* barge_row = list_row(list, y, c().sound_barge_in, c().sound_barge_in_detail,
                                   ui::IconRole::Microphone, ui::tokens::mint, kRowHeight, 252);
        add_switch(barge_row, sound.barge_in);
        if (conversation_ready) tappable(barge_row, sound_setting_event, 3);
        else ui::set_button_disabled(barge_row, true);
        y += kRowPitch;

        const char* voice_name = sound.assistant_voice == AssistantVoice::Athina ? c().voice_athina :
                                  c().voice_nestoras;
        auto* voice_row = settings_row(list, y, c().sound_voice, voice_name, ui::IconRole::Voice,
                                       ui::tokens::lilac, UiView::VoiceSettings);
        ui::set_button_disabled(voice_row, !speech_ready);
        y += kRowPitch + 8;

        auto* preview = pill(list, c().sound_preview, ui::ButtonTone::Primary, kGutter, y, kWidth,
                             sound_preview_event);
        // This is the local message/alarm sound preview, not neural speech.
        // Keep it available when the speaker works even if STT/TTS is absent.
        ui::set_button_disabled(preview, !callbacks.on_sound_preview || audio.muted);
        y += kButtonHeight + 12;
        auto* note = label(list, c().sound_alarm_note, kGutter, y, kWidth, ui::font_caption(),
                           ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
        lv_obj_update_layout(note);
        list_end(list, y + lv_obj_get_height(note));
    }

    void render_voice_settings() {
        make_header(c().voice_settings_title);
        auto* list = make_list();
        const struct {
            AssistantVoice voice;
            const char* title;
            const char* detail;
            std::uint32_t accent;
        } choices[] = {
            {AssistantVoice::Athina, c().voice_athina, c().voice_athina_detail, ui::tokens::pink},
            {AssistantVoice::Nestoras, c().voice_nestoras, c().voice_nestoras_detail, ui::tokens::ice},
        };
        int y = 0;
        for (const auto& choice : choices) {
            const bool selected = sound.assistant_voice == choice.voice;
            auto* row = list_row(list, y, choice.title, choice.detail, ui::IconRole::Voice,
                                 selected ? choice.accent : ui::tokens::indigo, kRowHeight, 252);
            if (selected) {
                auto* check = icon_view(row, ui::IconRole::Check, 28, choice.accent);
                lv_obj_align(check, LV_ALIGN_RIGHT_MID, 0, 0);
            }
            if (capabilities.speech_output) {
                tappable(row, voice_choice_event, static_cast<std::uintptr_t>(choice.voice));
            } else {
                ui::set_button_disabled(row, true);
            }
            y += kRowPitch;
        }
        y += 8;
        auto* preview = pill(list, c().voice_preview, ui::ButtonTone::Primary, kGutter, y, kWidth,
                             voice_preview_event);
        ui::set_button_disabled(preview, !capabilities.speech_output || !callbacks.on_voice_preview || audio.muted);
        y += kButtonHeight + 16;
        auto* note = label(list, c().voice_online_note, kGutter + 8, y, kWidth - 16, ui::font_caption(),
                           ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
        lv_obj_update_layout(note);
        list_end(list, y + lv_obj_get_height(note), 12);
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

    // A compact procedural "AI core" shared by confirmations, restart and the
    // low-power screen. It uses only circles/arcs, so it stays cheap on ESP32.
    void system_orb(int center_y, ui::IconRole role, std::uint32_t accent, bool quiet = false) {
        const int core_size = quiet ? 132 : 164;
        const int outer_size = quiet ? 116 : 146;
        const int inner_size = quiet ? 92 : 118;
        const int well_size = quiet ? 66 : 86;
        const int orbit_size = quiet ? 6 : 10;
        const int orbit_radius = quiet ? 56 : 72;
        const lv_opa_t halo_opa = quiet ? static_cast<lv_opa_t>(LV_OPA_10) : static_cast<lv_opa_t>(LV_OPA_20);
        system_core = make_dot(content, core_size, accent, halo_opa);
        lv_obj_set_pos(system_core, (ui::tokens::display_width - core_size) / 2, center_y - core_size / 2);
        lv_obj_set_style_transform_pivot_x(system_core, core_size / 2, 0);
        lv_obj_set_style_transform_pivot_y(system_core, core_size / 2, 0);

        system_ring_outer = spinner_arc(content, outer_size, accent, quiet ? 2 : 4);
        lv_obj_set_pos(system_ring_outer, (ui::tokens::display_width - outer_size) / 2,
                       center_y - outer_size / 2);
        lv_arc_set_angles(system_ring_outer, 18, quiet ? 86 : 124);
        lv_obj_set_style_arc_opa(system_ring_outer, quiet ? LV_OPA_30 : LV_OPA_80, LV_PART_INDICATOR);

        system_ring_inner = spinner_arc(content, inner_size,
                                         quiet ? ui::tokens::text_tertiary : ui::tokens::lilac,
                                         quiet ? 1 : 2);
        lv_obj_set_pos(system_ring_inner, (ui::tokens::display_width - inner_size) / 2,
                       center_y - inner_size / 2);
        lv_arc_set_angles(system_ring_inner, 210, quiet ? 270 : 322);
        lv_obj_set_style_arc_opa(system_ring_inner, quiet ? LV_OPA_20 : LV_OPA_60, LV_PART_INDICATOR);

        auto* well = make_dot(content, well_size, ui::tokens::surface_high,
                              quiet ? static_cast<lv_opa_t>(LV_OPA_30) : static_cast<lv_opa_t>(LV_OPA_COVER));
        lv_obj_set_pos(well, (ui::tokens::display_width - well_size) / 2, center_y - well_size / 2);
        lv_obj_set_style_border_width(well, quiet ? 1 : 2, 0);
        lv_obj_set_style_border_color(well, ui::color(accent), 0);
        lv_obj_set_style_border_opa(well, quiet ? LV_OPA_20 : LV_OPA_60, 0);
        auto* icon = icon_view(well, role, quiet ? 30 : 40, accent);
        lv_obj_center(icon);

        system_orbit_center_y = center_y;
        system_orbit_radius = orbit_radius;
        system_orbit_size = orbit_size;
        system_orbit_dot = make_dot(content, orbit_size, accent, quiet ? LV_OPA_30 : LV_OPA_COVER);
        lv_obj_set_pos(system_orbit_dot, ui::tokens::display_width / 2 - orbit_size / 2,
                       center_y - orbit_radius - orbit_size / 2);
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
        system_orb(158, role, accent);
        auto* heading = label(content, question, kGutter, 238, kWidth, ui::font_headline(), ui::tokens::text,
                              LV_TEXT_ALIGN_CENTER);
        lv_obj_set_height(heading, 58);
        auto* body = label(content, detail, kGutter + 8, 296, kWidth - 16, ui::font_caption(),
                           ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_height(body, 80);

        auto* cancel = ui::make_button(content, t().cancel, ui::ButtonTone::Secondary, ui::font_caption());
        lv_obj_set_size(cancel, kHalfWidth, kButtonHeight);
        lv_obj_set_pos(cancel, kGutter, kActionY);
        lv_obj_add_event_cb(cancel, back_event, LV_EVENT_CLICKED, this);
        auto* go = ui::make_button(content, confirm, tone, ui::font_caption());
        lv_obj_set_size(go, kHalfWidth, kButtonHeight);
        lv_obj_set_pos(go, kGutter + kHalfWidth + 16, kActionY);
        lv_obj_add_event_cb(go, system_confirm_event, LV_EVENT_CLICKED, this);
        ui::set_button_disabled(go, !callbacks.on_system_action);
    }

    // Switched off stays predominantly black. A very low-energy wake affordance
    // replaces the previous completely blank screen and accepts only a long press.
    void render_power_off() {
        header_action = true;
        auto* brand = line(content, t().aura_name, 0, 76, ui::tokens::display_width, ui::font_brand(),
                           ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_letter_space(brand, 8, 0);
        lv_obj_set_style_opa(brand, LV_OPA_40, 0);
        system_orb(220, ui::IconRole::Power, ui::tokens::ice, true);
        auto* hint = label(content, c().system_power_off_hint, 48, 322, 384, ui::font_caption(),
                           ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_opa(hint, LV_OPA_40, 0);

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
        const char* title = c().system_restarting;
        const char* detail = c().system_restarting_detail;
        ui::IconRole role = ui::IconRole::Sparkles;
        std::uint32_t accent = ui::tokens::ice;
        if (pending_system == SystemAction::FactoryReset) {
            title = c().system_resetting;
            detail = c().system_resetting_detail;
            role = ui::IconRole::Success;
            accent = ui::tokens::mint;
        } else if (pending_system == SystemAction::PowerOn) {
            title = c().system_powering_on;
            detail = c().system_powering_on_detail;
        }

        auto* brand = line(content, t().aura_name, 0, 58, ui::tokens::display_width, ui::font_brand(),
                           ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_letter_space(brand, 8, 0);
        system_orb(190, role, accent);
        boot_spinner = system_ring_outer;
        line(content, title, kGutter, 302, kWidth, ui::font_title(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        line(content, detail, kGutter, 348, kWidth, ui::font_caption(), ui::tokens::text_secondary,
             LV_TEXT_ALIGN_CENTER);

        system_progress = lv_bar_create(content);
        lv_obj_set_size(system_progress, 240, 5);
        lv_obj_set_pos(system_progress, 120, 414);
        lv_bar_set_range(system_progress, 0, 100);
        lv_bar_set_value(system_progress, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(system_progress, ui::color(ui::tokens::surface_high), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(system_progress, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(system_progress, ui::color(accent), LV_PART_INDICATOR);
        lv_obj_set_style_radius(system_progress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(system_progress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        line(content, c().system_starting, 0, 434, ui::tokens::display_width, ui::font_caption(),
             ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
    }

    void system_tick(std::uint32_t elapsed_ms) {
        const bool system_scene = current_view == UiView::SystemConfirm || current_view == UiView::PowerOff ||
                                  current_view == UiView::Boot;
        if (system_scene && !capture_mode && !reduced_motion) {
            const float seconds = static_cast<float>(phase_ms) / 1000.0F;
            const float pulse = 0.5F + std::sin(seconds * 2.4F) * 0.5F;
            if (system_core) {
                lv_obj_set_style_transform_scale(system_core, 250 + static_cast<int>(pulse * 12.0F), 0);
                if (current_view != UiView::PowerOff) {
                    lv_obj_set_style_bg_opa(system_core, static_cast<lv_opa_t>(28 + pulse * 24.0F), 0);
                }
            }
            if (system_ring_outer) {
                lv_arc_set_rotation(system_ring_outer, static_cast<int32_t>((phase_ms / 9U) % 360U));
            }
            if (system_ring_inner) {
                lv_arc_set_rotation(system_ring_inner, static_cast<int32_t>(360U - (phase_ms / 15U) % 360U));
            }
            if (system_orbit_dot) {
                // Around the orb it was built for: the power-off orb is smaller.
                const float angle = seconds * 1.7F;
                const auto radius = static_cast<float>(system_orbit_radius);
                lv_obj_set_pos(system_orbit_dot,
                               ui::tokens::display_width / 2 - system_orbit_size / 2 +
                                   static_cast<int>(std::cos(angle) * radius),
                               system_orbit_center_y - system_orbit_size / 2 +
                                   static_cast<int>(std::sin(angle) * radius));
            }
        }
        if (boot_remaining_ms == 0) return;
        boot_remaining_ms = elapsed_ms >= boot_remaining_ms ? 0U : boot_remaining_ms - elapsed_ms;
        if (system_progress) {
            const auto progress = 100U - (boot_remaining_ms * 100U / 1800U);
            lv_bar_set_value(system_progress, static_cast<int32_t>(progress), LV_ANIM_OFF);
        }
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

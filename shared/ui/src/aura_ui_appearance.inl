// Settings > Appearance. Included inside AuraUi::Impl.
// docs/superpowers/specs/2026-10-29-aura-appearances-design.md

    const char* appearance_name(Appearance look) const {
        switch (look) {
            case Appearance::Marble: return c().appearance_marble;
            case Appearance::Aurora: return c().appearance_aurora;
            case Appearance::Graphite: return c().appearance_graphite;
            case Appearance::Halo: break;
        }
        return c().appearance_halo;
    }

    // Switches the whole UI to `look`. The old presence lives until the new
    // view has replaced its image, so LVGL never draws from a freed buffer.
    void apply_appearance(Appearance look) {
        ui::set_active_appearance(look);
        if (look == appearance) return;
        appearance = look;
        auto previous = std::move(presence);
        presence = ui::make_presence(look);
        if (content) render(current_view);
        previous.reset();
    }

    static void appearance_card_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto index = reinterpret_cast<std::intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event)));
        if (index < 0 || static_cast<std::size_t>(index) >= kAppearances.size()) return;
        const auto look = kAppearances[static_cast<std::size_t>(index)];
        self->apply_appearance(look);
        if (self->callbacks.on_display_settings) {
            auto settings = self->display_power.settings;
            settings.appearance = look;
            self->callbacks.on_display_settings(settings);
        }
    }

    // One 210 x 172 card per look, drawn in that look's own surface, title font
    // and colours; the chosen one carries a check and an outline in its accent.
    void render_appearance_settings() {
        make_header(c().appearance_title);
        constexpr int kCardWidth = 210;
        constexpr int kCardHeight = 172;
        constexpr int kColumns[2] = {24, 246};
        constexpr int kRows[2] = {100, 284};
        const ui::IconRole sample_icons[3] = {ui::IconRole::Home, ui::IconRole::Sparkles, ui::IconRole::Weather};
        // Each card draws its own look's icon family.
        auto icon_view_at = [this](lv_obj_t* parent, ui::IconRole role, Appearance family, int size,
                                   std::uint32_t tint, int x, int y) {
            if (auto* icon = icon_image(parent, ui::icon_asset(role, family), size, tint)) lv_obj_set_pos(icon, x, y);
        };
        for (std::size_t index = 0; index < kAppearances.size(); ++index) {
            const auto look = kAppearances[index];
            const auto& palette = ui::palette_for(look);
            const auto& fonts = ui::fonts_for(look);
            const bool selected = look == appearance;
            auto* card = lv_obj_create(content);
            ui::reset(card);
            lv_obj_set_pos(card, kColumns[index % 2], kRows[index / 2]);
            lv_obj_set_size(card, kCardWidth, kCardHeight);
            lv_obj_set_style_bg_color(card, ui::color(palette.surface), 0);
            lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(card, ui::metrics_for(look).platter_radius, 0);
            lv_obj_set_style_border_width(card, selected ? 2 : 0, 0);
            lv_obj_set_style_border_color(card, ui::color(palette.blue), 0);
            lv_obj_set_style_transform_width(card, -3, LV_STATE_PRESSED);
            lv_obj_set_style_transform_height(card, -3, LV_STATE_PRESSED);
            // The name has the card's width («Μάρμαρο» in its serif); the chosen
            // look's check sits beside the swatches.
            line(card, appearance_name(look), 16, 16, 178, fonts.title, palette.text);
            if (selected) icon_view_at(card, ui::IconRole::Check, look, 28, palette.blue, 166, 127);
            for (int sample = 0; sample < 3; ++sample) {
                icon_view_at(card, sample_icons[sample], look, 32, palette.text, 16 + sample * 44, 74);
            }
            const std::uint32_t swatches[4] = {palette.canvas, palette.text, palette.blue, palette.indigo};
            for (int swatch = 0; swatch < 4; ++swatch) {
                auto* dot = lv_obj_create(card);
                ui::reset(dot);
                lv_obj_set_pos(dot, 16 + swatch * 26, 132);
                lv_obj_set_size(dot, 18, 18);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(dot, ui::color(swatches[swatch]), 0);
                lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(dot, 1, 0);
                lv_obj_set_style_border_color(dot, ui::color(palette.separator), 0);
            }
            lv_obj_set_user_data(card, reinterpret_cast<void*>(static_cast<std::intptr_t>(index)));
            lv_obj_set_clickable(card, true);
            lv_obj_add_event_cb(card, appearance_card_event, LV_EVENT_CLICKED, this);
        }
    }

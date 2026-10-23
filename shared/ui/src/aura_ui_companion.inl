// Included inside AuraUi::Impl (aura_ui.cpp): the v13 "Companion" screens.
// It is not a translation unit of its own; it shares the Impl helpers.
// Design: docs/design/AURA_V13_COMPANION_DESIGN.md.

    // ---- v13 "Companion" state ---------------------------------------------

    ClockSnapshot clock{};
    NotesSnapshot notes{};
    VoiceSnapshot voice{};
    TranslationSnapshot translation{};
    PhoneSnapshot phone{};
    UpdateSnapshot update{};
    DisplaySnapshot display_power{};
    AssistantExtras extras{};
    CompanionCapabilities capabilities{};
    std::uint64_t clock_signature{0};
    UiView aod_return_view{UiView::Home};
    lv_obj_t* dim_overlay{};
    std::uint32_t selected_note{0};
    ClockAlertKind selected_clock_kind{ClockAlertKind::Timer};
    std::uint32_t selected_clock_id{0};
    std::uint32_t selected_phone_uid{0};
    std::string reminder_draft;
    std::string note_draft;
    int reminder_step{0};
    std::array<std::int64_t, 6> reminder_choices{};
    int alarm_hour{7};
    int alarm_minute{0};
    bool alarm_daily{false};
    bool update_confirming{false};
    bool note_delete_armed{false};
    std::uint32_t note_delete_armed_at{0};
    // Widgets updated in place between renders (cleared by every render).
    std::vector<std::pair<std::uint32_t, lv_obj_t*>> timer_labels;
    lv_obj_t* home_timer_label{};
    lv_obj_t* clock_item_label{};
    lv_obj_t* alert_ring{};
    lv_obj_t* record_ring{};
    lv_obj_t* record_time{};
    lv_obj_t* translate_ring{};
    lv_obj_t* reminder_area{};
    lv_obj_t* note_area{};
    lv_obj_t* update_bar{};
    lv_obj_t* update_label{};

    const l10n::CompanionStrings& c() const { return l10n::companion_el_gr(); }

    void reset_companion_pointers() {
        timer_labels.clear();
        home_timer_label = nullptr;
        clock_item_label = nullptr;
        alert_ring = nullptr;
        record_ring = nullptr;
        record_time = nullptr;
        translate_ring = nullptr;
        reminder_area = nullptr;
        note_area = nullptr;
        update_bar = nullptr;
        update_label = nullptr;
    }

    static int language_index(const std::string& code) {
        static constexpr const char* kCodes[] = {"el", "en", "de", "fr", "it", "es"};
        for (int index = 0; index < 6; ++index) {
            if (code == kCodes[index]) return index;
        }
        return 1;
    }

    static std::string countdown_text(std::uint64_t remaining_ms) {
        const auto seconds = static_cast<unsigned>((remaining_ms + 999U) / 1000U);
        char text[16];
        if (seconds >= 3600U) {
            std::snprintf(text, sizeof(text), "%u:%02u:%02u", seconds / 3600U, (seconds % 3600U) / 60U, seconds % 60U);
        } else {
            std::snprintf(text, sizeof(text), "%02u:%02u", seconds / 60U, seconds % 60U);
        }
        return text;
    }

    std::string duration_words(std::uint32_t seconds) const {
        char text[48];
        if (seconds % 3600U == 0U) {
            if (seconds == 3600U) return c().hour_one;
            std::snprintf(text, sizeof(text), c().hours_format, static_cast<int>(seconds / 3600U));
            return text;
        }
        if (seconds % 60U == 0U) {
            if (seconds == 60U) return c().minute_one;
            std::snprintf(text, sizeof(text), c().minutes_format, static_cast<int>(seconds / 60U));
            return text;
        }
        return countdown_text(static_cast<std::uint64_t>(seconds) * 1000U);
    }

    // "Σήμερα 18:00", "Αύριο 09:00" or "24 Οκτωβρίου 09:00".
    std::string when_text(std::int64_t minute) const {
        const auto when = from_minute_index(minute);
        char text[64];
        const std::int64_t days = clock.clock_valid ? minute / 1440 - clock.now_minute / 1440 : -1;
        if (days == 0 || days == 1) {
            std::snprintf(text, sizeof(text), c().day_time_format, days == 0 ? c().today : c().tomorrow, when.hour,
                          when.minute);
        } else {
            std::snprintf(text, sizeof(text), "%d %s %02d:%02d", when.day, v().month_names[when.month - 1], when.hour,
                          when.minute);
        }
        return text;
    }

    std::string note_stamp(std::int64_t minute) const {
        if (minute <= 0) return {};
        const auto when = from_minute_index(minute);
        char text[64];
        std::snprintf(text, sizeof(text), "%d %s · %02d:%02d", when.day, v().month_names[when.month - 1], when.hour,
                      when.minute);
        return text;
    }

    static std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
        for (int index = 0; index < 8; ++index) {
            hash ^= (value >> (index * 8)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    // What the clock screens draw, apart from the ticking seconds.
    static std::uint64_t clock_structure(const ClockSnapshot& value) {
        std::uint64_t hash = 1469598103934665603ULL;
        hash = mix(hash, value.timers.size());
        for (const auto& timer : value.timers) hash = mix(mix(hash, timer.id), timer.paused ? 1U : 0U);
        hash = mix(hash, value.alarms.size());
        for (const auto& alarm : value.alarms) {
            hash = mix(mix(mix(hash, alarm.id), static_cast<std::uint64_t>(alarm.hour * 60 + alarm.minute)),
                       (alarm.daily ? 2U : 0U) | (alarm.enabled ? 1U : 0U));
        }
        hash = mix(hash, value.reminders.size());
        for (const auto& reminder : value.reminders) {
            hash = mix(mix(hash, reminder.id), static_cast<std::uint64_t>(reminder.due_minute));
        }
        hash = mix(hash, value.alert ? (static_cast<std::uint64_t>(value.alert->kind) << 32U) | value.alert->id :
                                       0xFFFFFFFFFFFFULL);
        return mix(hash, value.clock_valid ? 1U : 0U);
    }

    // Pops back to `view` if it is on the stack, otherwise opens it.
    void return_to(UiView view) {
        while (navigation.current() != view && navigation.depth() > 0) navigation.back();
        if (navigation.current() != view) navigation.navigate(view);
        render(view);
    }

    static std::uintptr_t user_value(lv_event_t* event) {
        return reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event)));
    }

    static void set_value(lv_obj_t* object, std::uintptr_t value) {
        lv_obj_set_user_data(object, reinterpret_cast<void*>(value));
    }

    lv_obj_t* add_switch(lv_obj_t* row, bool checked) {
        auto* toggle = lv_switch_create(row);
        lv_obj_set_size(toggle, 64, 36);
        lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(toggle, ui::color(ui::tokens::surface_glass), LV_PART_MAIN);
        lv_obj_set_style_bg_color(toggle, ui::color(ui::tokens::blue), kIndicatorChecked);
        lv_obj_set_style_bg_color(toggle, ui::color(ui::tokens::text), LV_PART_KNOB);
        lv_obj_set_clickable(toggle, false);
        if (checked) lv_obj_add_state(toggle, LV_STATE_CHECKED);
        return toggle;
    }

    lv_obj_t* tappable(lv_obj_t* object, lv_event_cb_t callback, std::uintptr_t value = 0) {
        lv_obj_set_clickable(object, true);
        set_value(object, value);
        lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, this);
        return object;
    }

    // ---- Events ------------------------------------------------------------

    static Impl* self_of(lv_event_t* event) { return static_cast<Impl*>(lv_event_get_user_data(event)); }

    static void open_clock_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action()) self->navigate(UiView::Clock);
    }

    static void briefing_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (self->callbacks.on_briefing_open) self->callbacks.on_briefing_open();
        self->navigate(UiView::Briefing);
    }

    static void clock_new_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action()) self->navigate(UiView::ClockNew);
    }

    static void reminder_new_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->reminder_step = 0;
        self->reminder_draft.clear();
        self->navigate(UiView::ReminderNew);
    }

    static void timer_preset_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_timer_start) return;
        const auto minutes = static_cast<std::uint32_t>(user_value(event));
        if (self->callbacks.on_timer_start(minutes * 60U, {})) {
            self->return_to(UiView::Clock);
        } else {
            self->show_toast(self->c().clock_full, ui::tokens::coral);
        }
    }

    static void alarm_roller_event(lv_event_t* event) {
        auto* self = self_of(event);
        auto* roller = lv_event_get_target_obj(event);
        const auto selected = static_cast<int>(lv_roller_get_selected(roller));
        if (user_value(event) == 0) self->alarm_hour = selected;
        else self->alarm_minute = selected;
    }

    static void alarm_daily_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->alarm_daily = !self->alarm_daily;
        self->render(UiView::AlarmNew);
    }

    static void alarm_save_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_alarm_add) return;
        if (self->callbacks.on_alarm_add(self->alarm_hour, self->alarm_minute, self->alarm_daily)) {
            self->return_to(UiView::Clock);
        } else {
            self->show_toast(self->c().clock_full, ui::tokens::coral);
        }
    }

    static void reminder_text_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->reminder_area) return;
        const char* text = lv_textarea_get_text(self->reminder_area);
        self->reminder_draft = text ? text : "";
    }

    static void reminder_next_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (sanitize_text(self->reminder_draft, ClockServicesModel::kMaxReminderBytes).empty()) return;
        self->reminder_step = 1;
        self->render(UiView::ReminderNew);
    }

    static void reminder_choice_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_reminder_add) return;
        const auto index = user_value(event);
        if (index >= self->reminder_choices.size()) return;
        if (self->callbacks.on_reminder_add(self->reminder_choices[index], self->reminder_draft)) {
            self->reminder_draft.clear();
            self->reminder_step = 0;
            self->return_to(UiView::Clock);
            self->show_toast(self->c().action_done, ui::tokens::mint);
        } else {
            self->show_toast(self->c().clock_needs_time, ui::tokens::coral);
        }
    }

    // Rows encode the item kind in the top byte of their value.
    static void clock_item_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        const auto value = user_value(event);
        self->selected_clock_kind = static_cast<ClockAlertKind>(value >> 24U);
        self->selected_clock_id = static_cast<std::uint32_t>(value & 0xFFFFFFU);
        self->navigate(UiView::ClockItem);
    }

    static void alarm_switch_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_alarm_enable) return;
        const auto id = static_cast<std::uint32_t>(user_value(event));
        for (const auto& alarm : self->clock.alarms) {
            if (alarm.id == id) self->callbacks.on_alarm_enable(id, !alarm.enabled);
        }
    }

    static void timer_pause_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_timer_pause) return;
        for (const auto& timer : self->clock.timers) {
            if (timer.id == self->selected_clock_id) self->callbacks.on_timer_pause(timer.id, !timer.paused);
        }
    }

    static void clock_item_delete_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        const auto id = self->selected_clock_id;
        switch (self->selected_clock_kind) {
            case ClockAlertKind::Timer: if (self->callbacks.on_timer_cancel) self->callbacks.on_timer_cancel(id); break;
            case ClockAlertKind::Alarm: if (self->callbacks.on_alarm_remove) self->callbacks.on_alarm_remove(id); break;
            case ClockAlertKind::Reminder:
                if (self->callbacks.on_reminder_remove) self->callbacks.on_reminder_remove(id);
                break;
        }
        self->return_to(UiView::Clock);
    }

    static void alert_dismiss_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_alert_dismiss) self->callbacks.on_alert_dismiss();
    }

    static void alert_snooze_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_alert_snooze) return;
        self->callbacks.on_alert_snooze(static_cast<int>(user_value(event)));
    }

    static void note_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->selected_note = static_cast<std::uint32_t>(user_value(event));
        self->note_delete_armed = false;
        self->navigate(UiView::NoteDetail);
    }

    static void note_record_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_note_record) return;
        if (self->callbacks.on_note_record()) self->navigate(UiView::NoteRecord);
        else self->show_toast(self->c().note_needs_voice, ui::tokens::coral);
    }

    static void note_record_stop_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_note_record_stop) self->callbacks.on_note_record_stop();
    }

    static void note_record_cancel_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (self->callbacks.on_note_record_cancel) self->callbacks.on_note_record_cancel();
        self->return_to(UiView::Notes);
    }

    static void note_typed_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->note_draft.clear();
        self->navigate(UiView::NoteNew);
    }

    static void note_text_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->note_area) return;
        const char* text = lv_textarea_get_text(self->note_area);
        self->note_draft = text ? text : "";
    }

    static void note_save_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_note_add) return;
        if (sanitize_text(self->note_draft, NotesModel::kMaxTextBytes, true).empty()) return;
        if (self->callbacks.on_note_add(self->note_draft)) {
            self->note_draft.clear();
            self->return_to(UiView::Notes);
            self->show_toast(self->c().note_saved, ui::tokens::mint);
        } else {
            self->show_toast(self->c().notes_full, ui::tokens::coral);
        }
    }

    static void note_play_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_note_play) self->callbacks.on_note_play(self->selected_note);
    }

    // Deleting needs a second tap within four seconds.
    static void note_delete_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (!self->note_delete_armed) {
            self->note_delete_armed = true;
            self->note_delete_armed_at = self->phase_ms;
            self->show_toast(self->c().delete_again, ui::tokens::coral);
            return;
        }
        self->note_delete_armed = false;
        if (self->callbacks.on_note_delete) self->callbacks.on_note_delete(self->selected_note);
        self->return_to(UiView::Notes);
    }

    static void action_confirm_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_action_confirm) self->callbacks.on_action_confirm();
    }

    static void action_decline_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_action_decline) self->callbacks.on_action_decline();
    }

    static void conversation_clear_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (self->callbacks.on_conversation_clear) self->callbacks.on_conversation_clear();
        if (self->callbacks.on_ask) self->callbacks.on_ask();
    }

    static void speak_answer_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_speak || self->snapshot.answer_text.empty()) return;
        self->callbacks.on_speak(self->snapshot.answer_text, "el");
    }

    static void briefing_speak_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_speak) return;
        self->callbacks.on_speak(self->briefing_speech(), "el");
    }

    static void translate_record_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_translate_record) return;
        const auto side = user_value(event) == 0 ? TranslateSide::Mine : TranslateSide::Theirs;
        const bool recording_this = self->voice.state == VoiceCaptureState::Recording &&
                                    self->voice.purpose == VoicePurpose::Translate &&
                                    self->translation_active_side() == side;
        if (recording_this) {
            if (self->callbacks.on_translate_stop) self->callbacks.on_translate_stop();
        } else if (!self->callbacks.on_translate_record(side)) {
            self->show_toast(self->c().translate_needs_voice, ui::tokens::coral);
        }
    }

    static void translate_language_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_translate_language) return;
        static constexpr const char* kTheirs[] = {"en", "de", "fr", "it", "es"};
        const int current = language_index(self->translation.their_language);
        const int next = current >= 5 ? 0 : current;  // index in kTheirs of the following language
        self->callbacks.on_translate_language(kTheirs[next]);
    }

    static void wake_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->callbacks.on_wake) self->callbacks.on_wake();
    }

    static void phone_notification_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->selected_phone_uid = static_cast<std::uint32_t>(user_value(event));
        self->navigate(UiView::PhoneNotification);
    }

    static void phone_call_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_phone_call || !self->phone.incoming_call) return;
        self->callbacks.on_phone_call(self->phone.incoming_call->uid, user_value(event) != 0);
    }

    static void phone_dismiss_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (self->callbacks.on_phone_dismiss) self->callbacks.on_phone_dismiss(self->selected_phone_uid);
        self->return_to(UiView::Notifications);
    }

    static void phone_pair_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_phone_pair) self->callbacks.on_phone_pair();
    }

    static void phone_forget_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_phone_forget) self->callbacks.on_phone_forget();
    }

    // value: 0 always-on, 1 raise, 2 flip, 3 timeout
    static void display_setting_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_display_settings) return;
        auto settings = self->display_power.settings;
        switch (user_value(event)) {
            case 0: settings.always_on = !settings.always_on; break;
            case 1: settings.raise_to_wake = !settings.raise_to_wake; break;
            case 2: settings.flip_to_mute = !settings.flip_to_mute; break;
            default:
                settings.timeout_ms = settings.timeout_ms <= 15000 ? 30000 : (settings.timeout_ms <= 30000 ? 60000 : 15000);
                break;
        }
        self->callbacks.on_display_settings(settings);
    }

    static void update_check_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_update_check) self->callbacks.on_update_check();
    }

    static void update_install_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (!self->update_confirming) {
            self->update_confirming = true;
            self->render(UiView::Update);
            return;
        }
        self->update_confirming = false;
        if (self->callbacks.on_update_install) self->callbacks.on_update_install();
    }

    static void update_cancel_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->update_confirming = false;
        self->render(UiView::Update);
    }

    static void update_restart_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_update_restart) self->callbacks.on_update_restart();
    }

    // ---- Home, launcher, settings and notifications additions ---------------

    const TimerItem* soonest_timer() const {
        const TimerItem* best = nullptr;
        for (const auto& timer : clock.timers) {
            if (!timer.paused && (!best || timer.remaining_ms < best->remaining_ms)) best = &timer;
        }
        if (!best && !clock.timers.empty()) best = &clock.timers.front();
        return best;
    }

    // While a timer runs, Home shows it instead of the weather glance.
    bool render_home_timer_chip() {
        const auto* timer = soonest_timer();
        if (!timer) return false;
        const auto text = countdown_text(timer->remaining_ms);
        auto* value = chip(kGutter, 404, 344, timer->paused ? ui::IconRole::Pause : ui::IconRole::Timer,
                           ui::tokens::coral, text.c_str(),
                           timer->label.empty() ? (timer->paused ? c().paused : c().timer) : timer->label.c_str(),
                           open_clock_event);
        home_timer_label = lv_obj_get_child(value, 1);
        return true;
    }

    // The date line on Home opens "Today".
    void attach_briefing_target() {
        if (!home_date) return;
        lv_obj_set_clickable(home_date, true);
        lv_obj_set_ext_click_area(home_date, 14);
        lv_obj_add_event_cb(home_date, briefing_event, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* app_cell_in(lv_obj_t* parent, int column, int row, const char* title, ui::IconRole role,
                          std::uint32_t accent, UiView view, lv_event_cb_t special = nullptr) {
        auto* cell = lv_button_create(parent);
        ui::reset(cell);
        lv_obj_set_clickable(cell, true);
        lv_obj_set_size(cell, 144, 164);
        lv_obj_set_pos(cell, kGutter + column * 144, 8 + row * 172);
        lv_obj_set_style_radius(cell, ui::tokens::radius_large, 0);
        lv_obj_set_style_bg_color(cell, ui::color(ui::tokens::surface), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
        auto* circle = make_dot(cell, 104, accent, kTint);
        lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 8);
        auto* icon = icon_view(circle, role, 46, accent);
        lv_obj_center(icon);
        line(cell, title, 0, 120, 144, ui::font_caption(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        if (special) {
            lv_obj_add_event_cb(cell, special, LV_EVENT_CLICKED, this);
        } else {
            set_target(cell, view);
            lv_obj_add_event_cb(cell, nav_event, LV_EVENT_CLICKED, this);
        }
        return cell;
    }

    // Nine apps in a 3-column grid that scrolls; the third row peeks.
    void render_launcher_grid() {
        make_header(v().applications);
        auto* grid = make_list();
        app_cell_in(grid, 0, 0, v().conversation, ui::IconRole::Sparkles, ui::tokens::lilac, UiView::Assistant, ask_event);
        app_cell_in(grid, 1, 0, v().weather, ui::IconRole::Weather, ui::tokens::amber, UiView::Weather,
                    weather_open_event);
        app_cell_in(grid, 2, 0, v().apps_messages, ui::IconRole::Message, ui::tokens::mint, UiView::AoratonRooms);
        app_cell_in(grid, 0, 1, c().app_clock, ui::IconRole::Timer, ui::tokens::coral, UiView::Clock);
        app_cell_in(grid, 1, 1, c().app_notes, ui::IconRole::Notebook, ui::tokens::pink, UiView::Notes);
        app_cell_in(grid, 2, 1, c().app_translate, ui::IconRole::Languages, ui::tokens::ice, UiView::Translate);
        app_cell_in(grid, 0, 2, c().app_today, ui::IconRole::Sunrise, ui::tokens::magenta, UiView::Briefing,
                    briefing_event);
        auto* alerts = app_cell_in(grid, 1, 2, v().notifications, ui::IconRole::Notification, ui::tokens::indigo,
                                   UiView::Notifications);
        app_cell_in(grid, 2, 2, t().settings_title, ui::IconRole::Settings, ui::tokens::blue, UiView::Settings);
        list_end(grid, 8 + 3 * 172, 8);
        if (notification_total() > 0) {
            char count[12];
            std::snprintf(count, sizeof(count), "%d", notification_total());
            auto* badge = ui::make_tag(alerts, count, ui::tokens::pink);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(badge, ui::color(ui::tokens::text), 0);
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -12, 4);
        }
    }

    std::string phone_summary() const {
        switch (phone.link) {
            case PhoneLinkState::Connected: {
                char text[96];
                std::snprintf(text, sizeof(text), c().phone_connected_format, phone.device_name.c_str());
                return text;
            }
            case PhoneLinkState::Pairing: return c().phone_pairing;
            case PhoneLinkState::Disconnected: return c().phone_disconnected;
            case PhoneLinkState::NotPaired: return c().phone_not_paired;
            case PhoneLinkState::Unavailable: break;
        }
        return c().phone_unavailable;
    }

    int render_settings_companion(lv_obj_t* list, int y) {
        settings_row(list, y, c().display_title, display_power.settings.always_on ? c().always_on : nullptr,
                     ui::IconRole::Display, ui::tokens::indigo, UiView::DisplaySettings);
        y += kRowPitch;
        const auto summary = phone_summary();
        settings_row(list, y, c().phone_title, summary.c_str(), ui::IconRole::Smartphone, ui::tokens::mint,
                     UiView::PhoneLink);
        y += kRowPitch;
        // The wake word needs a trained "Aura" model; until then the row explains why.
        auto* wake = list_row(list, y, c().wake_word, capabilities.wake_word ? c().enabled : c().wake_word_unavailable,
                              ui::IconRole::Ear, ui::tokens::lilac);
        if (!capabilities.wake_word) lv_obj_set_style_opa(wake, LV_OPA_60, 0);
        y += kRowPitch;
        char version[64];
        std::snprintf(version, sizeof(version), c().update_version_format, update.current_version.c_str());
        settings_row(list, y, c().update_title, version, ui::IconRole::Download, ui::tokens::blue, UiView::Update);
        return y + kRowPitch;
    }

    int render_phone_rows(lv_obj_t* list, int y) {
        std::size_t shown = 0;
        for (const auto& item : phone.notifications) {
            if (++shown > 8) break;
            std::string title = item.app;
            if (!item.title.empty()) title += " · " + item.title;
            std::string detail = item.message;
            if (item.demo) detail = std::string(t().demo) + " · " + detail;
            auto* row = list_row(list, y, title.c_str(), detail.empty() ? nullptr : detail.c_str(),
                                 ui::IconRole::Smartphone, ui::tokens::mint);
            tappable(row, phone_notification_event, item.uid);
            y += kRowPitch;
        }
        return y;
    }

    // ---- Assistant ---------------------------------------------------------

    struct ActionCopy {
        ui::IconRole role;
        std::uint32_t accent;
        std::string title;
        std::string detail;
    };

    ActionCopy describe_action(const AssistantAction& action) const {
        char text[96];
        switch (action.type) {
            case AssistantActionType::Timer: {
                std::snprintf(text, sizeof(text), c().action_timer_format,
                              duration_words(static_cast<std::uint32_t>(action.seconds)).c_str());
                return {ui::IconRole::Timer, ui::tokens::coral, text,
                        action.label.empty() ? c().action_question : action.label};
            }
            case AssistantActionType::Alarm: {
                std::snprintf(text, sizeof(text), c().action_alarm_format, action.hour, action.minute);
                std::string detail = action.daily ? c().every_day : c().once;
                if (!action.label.empty()) detail += " · " + action.label;
                return {ui::IconRole::Reminder, ui::tokens::amber, text, detail};
            }
            case AssistantActionType::Reminder: {
                if (action.in_minutes > 0) {
                    std::snprintf(text, sizeof(text), c().action_in_minutes_format, action.in_minutes);
                } else {
                    std::snprintf(text, sizeof(text), c().action_at_format, action.tomorrow ? c().tomorrow : c().today,
                                  action.hour, action.minute);
                }
                return {ui::IconRole::BellRing, ui::tokens::magenta, action.text, text};
            }
            case AssistantActionType::Note:
                return {ui::IconRole::Notebook, ui::tokens::pink, c().action_note_title, action.text};
            case AssistantActionType::Message:
            case AssistantActionType::None: break;
        }
        return {ui::IconRole::Sparkles, ui::tokens::lilac, c().action_question, {}};
    }

    void render_answer_companion() {
        const bool can_speak = capabilities.speech_output && callbacks.on_speak && !snapshot.answer_text.empty();
        if (can_speak) make_header(t().answer_title, ui::IconRole::VolumeUp, speak_answer_event, ui::tokens::mint);
        else make_header(t().answer_title);
        line(content, snapshot.user_text.empty() ? t().ask_example : snapshot.user_text.c_str(),
             kGutter + 4, 100, kWidth - 8, ui::font_caption(), ui::tokens::text_tertiary);
        const bool confirming = snapshot.state == AssistantState::ActionConfirmation && extras.pending_action;
        auto* reader = lv_obj_create(content);
        ui::reset(reader);
        lv_obj_set_size(reader, kWidth, confirming ? 136 : 236);
        lv_obj_set_pos(reader, kGutter, 132);
        lv_obj_set_clickable(reader, true);
        lv_obj_set_scrollable(reader, true);
        lv_obj_set_scroll_dir(reader, LV_DIR_VER);
        lv_obj_set_overflow_visible(reader, false);
        ui::style_scrollbar(reader);
        primary_scroller = reader;
        answer_label = ui::make_label(reader,
            snapshot.answer_text.empty() ? t().no_answer_yet : snapshot.answer_text.c_str(),
            ui::tokens::text, ui::font_body());
        lv_obj_set_width(answer_label, kWidth - 16);
        lv_obj_set_style_text_line_space(answer_label, 6, 0);
        lv_obj_align(answer_label, LV_ALIGN_TOP_LEFT, 0, 0);
        if (confirming) {
            const auto copy = describe_action(*extras.pending_action);
            auto* card = list_row(content, 280, copy.title.c_str(), copy.detail.empty() ? nullptr : copy.detail.c_str(),
                                  copy.role, copy.accent);
            ui::style_platter(card, copy.accent, ui::tokens::radius_large, true);
            pill(content, c().confirm, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth, action_confirm_event);
            pill(content, c().decline, ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, kActionY, kHalfWidth,
                 action_decline_event);
        } else if (extras.memory_turns > 0 && callbacks.on_conversation_clear) {
            pill(content, t().ask_again, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth, ask_event);
            pill(content, c().new_conversation, ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, kActionY,
                 kHalfWidth, conversation_clear_event);
        } else {
            pill(content, t().ask_again, ui::ButtonTone::Primary, kGutter, kActionY, kWidth, ask_event);
        }
    }

    // ---- Clock -------------------------------------------------------------

    lv_obj_t* heading_of(lv_obj_t* row) { return lv_obj_get_child(row, 1); }

    void render_clock() {
        make_header(c().app_clock, ui::IconRole::Plus, clock_new_event, ui::tokens::coral);
        if (clock.timers.empty() && clock.alarms.empty() && clock.reminders.empty()) {
            state_symbol(128, 112, ui::IconRole::Timer, ui::tokens::coral);
            state_text(264, c().clock_empty, c().clock_empty_hint);
            return;
        }
        auto* list = make_list();
        int y = 0;
        for (const auto& timer : clock.timers) {
            const auto text = countdown_text(timer.remaining_ms);
            auto* row = list_row(list, y, text.c_str(),
                                 timer.paused ? c().paused : (timer.label.empty() ? c().timer : timer.label.c_str()),
                                 timer.paused ? ui::IconRole::Pause : ui::IconRole::Timer, ui::tokens::coral);
            add_chevron(row);
            timer_labels.emplace_back(timer.id, heading_of(row));
            tappable(row, clock_item_event,
                     (static_cast<std::uintptr_t>(ClockAlertKind::Timer) << 24U) | (timer.id & 0xFFFFFFU));
            y += kRowPitch;
        }
        for (const auto& alarm : clock.alarms) {
            char time[8];
            std::snprintf(time, sizeof(time), "%02d:%02d", alarm.hour, alarm.minute);
            std::string detail = alarm.enabled ? (alarm.daily ? c().every_day : c().once) : c().alarm_off;
            if (!alarm.label.empty()) detail += " · " + alarm.label;
            auto* row = list_row(list, y, time, detail.c_str(), ui::IconRole::Reminder,
                                 alarm.enabled ? ui::tokens::amber : ui::tokens::text_tertiary, kRowHeight, 220);
            tappable(row, clock_item_event,
                     (static_cast<std::uintptr_t>(ClockAlertKind::Alarm) << 24U) | (alarm.id & 0xFFFFFFU));
            // The switch is its own 88 x 64 target at the row's end.
            auto* toggle = add_switch(row, alarm.enabled);
            lv_obj_set_ext_click_area(toggle, 14);
            tappable(toggle, alarm_switch_event, alarm.id);
            y += kRowPitch;
        }
        for (const auto& reminder : clock.reminders) {
            const auto when = when_text(reminder.due_minute);
            auto* row = list_row(list, y, reminder.text.c_str(), when.c_str(), ui::IconRole::BellRing,
                                 ui::tokens::magenta);
            add_chevron(row);
            tappable(row, clock_item_event,
                     (static_cast<std::uintptr_t>(ClockAlertKind::Reminder) << 24U) | (reminder.id & 0xFFFFFFU));
            y += kRowPitch;
        }
        list_end(list, y);
    }

    void render_clock_new() {
        make_header(c().app_clock);
        auto* list = make_list();
        settings_row(list, 0, c().new_timer, nullptr, ui::IconRole::Timer, ui::tokens::coral, UiView::TimerNew);
        settings_row(list, kRowPitch, c().new_alarm, nullptr, ui::IconRole::Reminder, ui::tokens::amber,
                     UiView::AlarmNew);
        auto* reminder = list_row(list, kRowPitch * 2, c().new_reminder, nullptr, ui::IconRole::BellRing,
                                  ui::tokens::magenta);
        add_chevron(reminder);
        tappable(reminder, reminder_new_event);
        list_end(list, kRowPitch * 3);
    }

    // Nine presets, one tap each; other lengths are a sentence to the assistant.
    void render_timer_new() {
        make_header(c().new_timer);
        static constexpr std::uint32_t kMinutes[] = {1, 3, 5, 10, 15, 20, 30, 45, 60};
        for (int index = 0; index < 9; ++index) {
            const auto minutes = kMinutes[index];
            auto* button = lv_button_create(content);
            ui::reset(button);
            ui::style_platter(button, ui::tokens::coral, ui::tokens::radius_large, false);
            lv_obj_set_style_bg_color(button, ui::color(ui::tokens::surface_high), LV_STATE_PRESSED);
            lv_obj_set_size(button, 136, 84);
            lv_obj_set_pos(button, kGutter + (index % 3) * 148, 104 + (index / 3) * 92);
            char number[8];
            std::snprintf(number, sizeof(number), "%u", minutes == 60U ? 1U : minutes);
            auto* value = ui::make_label(button, number, ui::tokens::text, ui::font_title());
            lv_obj_align(value, LV_ALIGN_CENTER, 0, -12);
            auto* unit = ui::make_label(button, minutes == 60U ? c().unit_hour :
                                        (minutes == 1U ? c().unit_minute : c().unit_minutes),
                                        ui::tokens::text_secondary, ui::font_caption());
            lv_obj_align(unit, LV_ALIGN_CENTER, 0, 20);
            tappable(button, timer_preset_event, minutes);
        }
    }

    static const char* roller_options(int count) {
        static const std::string hours = [] {
            std::string value;
            for (int index = 0; index < 24; ++index) {
                char text[4];
                std::snprintf(text, sizeof(text), "%02d", index);
                if (index > 0) value += '\n';
                value += text;
            }
            return value;
        }();
        static const std::string minutes = [] {
            std::string value;
            for (int index = 0; index < 60; ++index) {
                char text[4];
                std::snprintf(text, sizeof(text), "%02d", index);
                if (index > 0) value += '\n';
                value += text;
            }
            return value;
        }();
        return count == 24 ? hours.c_str() : minutes.c_str();
    }

    lv_obj_t* make_roller(int count, int selected, int x, std::uintptr_t which) {
        auto* roller = lv_roller_create(content);
        lv_roller_set_options(roller, roller_options(count), LV_ROLLER_MODE_INFINITE);
        lv_obj_set_style_text_font(roller, ui::font_title(), 0);
        lv_obj_set_style_text_line_space(roller, 14, 0);
        lv_obj_set_style_text_align(roller, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(roller, ui::color(ui::tokens::text_tertiary), 0);
        lv_obj_set_style_bg_color(roller, ui::color(ui::tokens::surface), 0);
        lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(roller, 0, 0);
        lv_obj_set_style_radius(roller, ui::tokens::radius_large, 0);
        lv_obj_set_style_bg_color(roller, ui::color(ui::tokens::surface_glass), LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller, ui::color(ui::tokens::text), LV_PART_SELECTED);
        lv_obj_set_style_text_font(roller, ui::font_title(), LV_PART_SELECTED);
        lv_roller_set_visible_row_count(roller, 3);
        lv_obj_set_width(roller, 152);
        lv_obj_set_pos(roller, x, 104);
        lv_roller_set_selected(roller, static_cast<std::uint32_t>(selected), LV_ANIM_OFF);
        set_value(roller, which);
        lv_obj_add_event_cb(roller, alarm_roller_event, LV_EVENT_VALUE_CHANGED, this);
        return roller;
    }

    void render_alarm_new() {
        make_header(c().new_alarm);
        make_roller(24, alarm_hour, 72, 0);
        make_roller(60, alarm_minute, 256, 1);
        label(content, ":", 224, 150, 32, ui::font_title(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        auto* daily = list_row(content, 280, c().every_day, nullptr, ui::IconRole::Calendar, ui::tokens::indigo,
                               kRowHeight, 252);
        tappable(daily, alarm_daily_event);
        add_switch(daily, alarm_daily);
        pill(content, c().save, ui::ButtonTone::Primary, kGutter, kActionY, kWidth, alarm_save_event);
    }

    void render_reminder_new() {
        if (reminder_step == 0) {
            make_header(c().new_reminder, ui::IconRole::Check, reminder_next_event, ui::tokens::blue);
            reminder_area = make_text_input(kGutter, 100, kWidth, 72, c().reminder_placeholder, true);
            if (!reminder_draft.empty()) lv_textarea_set_text(reminder_area, reminder_draft.c_str());
            lv_textarea_set_max_length(reminder_area, 120);
            lv_obj_add_event_cb(reminder_area, reminder_text_event, LV_EVENT_VALUE_CHANGED, this);
            lv_obj_add_event_cb(reminder_area, reminder_next_event, LV_EVENT_READY, this);
            make_form_keyboard(reminder_area, ui::KeyboardProfile::Text, 184);
            return;
        }
        make_header(c().reminder_when);
        if (!clock.clock_valid) {
            state_symbol(128, 112, ui::IconRole::Clock, ui::tokens::amber);
            state_text(264, c().clock_needs_time, nullptr);
            return;
        }
        const std::int64_t now = clock.now_minute;
        const std::int64_t day = (now / 1440) * 1440;
        struct Choice { std::int64_t due; std::string text; };
        std::vector<Choice> choices;
        char text[64];
        for (const int minutes : {10, 30}) {
            std::snprintf(text, sizeof(text), c().in_minutes_format, minutes);
            choices.push_back({now + minutes, text});
        }
        choices.push_back({now + 60, c().in_hour});
        std::snprintf(text, sizeof(text), c().in_hours_format, 3);
        choices.push_back({now + 180, text});
        if (now - day < 20 * 60 - 30) {
            std::snprintf(text, sizeof(text), c().tonight_format, 20, 0);
            choices.push_back({day + 20 * 60, text});
        }
        std::snprintf(text, sizeof(text), c().tomorrow_format, 9, 0);
        choices.push_back({day + 1440 + 9 * 60, text});
        auto* list = make_list();
        int y = 0;
        for (std::size_t index = 0; index < choices.size() && index < reminder_choices.size(); ++index) {
            reminder_choices[index] = choices[index].due;
            auto* row = list_row(list, y, choices[index].text.c_str(), nullptr, ui::IconRole::BellRing,
                                 ui::tokens::magenta);
            tappable(row, reminder_choice_event, index);
            y += kRowPitch;
        }
        list_end(list, y);
    }

    void render_clock_item() {
        const std::uint32_t id = selected_clock_id;
        if (selected_clock_kind == ClockAlertKind::Timer) {
            const TimerItem* timer = nullptr;
            for (const auto& item : clock.timers) if (item.id == id) timer = &item;
            make_header(c().timer);
            if (!timer) {
                state_text(200, c().clock_empty, nullptr);
                return;
            }
            const auto text = countdown_text(timer->remaining_ms);
            clock_item_label = label(content, text.c_str(), 0, 128, ui::tokens::display_width, ui::font_display(),
                                     timer->paused ? ui::tokens::text_secondary : ui::tokens::text,
                                     LV_TEXT_ALIGN_CENTER);
            line(content, timer->label.empty() ? duration_words(timer->duration_s).c_str() : timer->label.c_str(),
                 kGutter, 224, kWidth, ui::font_body(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            pill(content, timer->paused ? c().resume : c().pause, ui::ButtonTone::Secondary, kGutter, kActionY,
                 kHalfWidth, timer_pause_event);
            pill(content, c().cancel, ui::ButtonTone::Danger, kGutter + kHalfWidth + 16, kActionY, kHalfWidth,
                 clock_item_delete_event);
            return;
        }
        if (selected_clock_kind == ClockAlertKind::Alarm) {
            const AlarmItem* alarm = nullptr;
            for (const auto& item : clock.alarms) if (item.id == id) alarm = &item;
            make_header(c().alarm);
            if (!alarm) {
                state_text(200, c().clock_empty, nullptr);
                return;
            }
            char time[8];
            std::snprintf(time, sizeof(time), "%02d:%02d", alarm->hour, alarm->minute);
            label(content, time, 0, 112, ui::tokens::display_width, ui::font_display(),
                  alarm->enabled ? ui::tokens::text : ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
            std::string detail = alarm->daily ? c().every_day : c().once;
            if (!alarm->label.empty()) detail += " · " + alarm->label;
            line(content, detail.c_str(), kGutter, 196, kWidth, ui::font_body(), ui::tokens::text_secondary,
                 LV_TEXT_ALIGN_CENTER);
            auto* row = list_row(content, 252, c().enabled, nullptr, ui::IconRole::Reminder, ui::tokens::amber,
                                 kRowHeight, 252);
            tappable(row, alarm_switch_event, alarm->id);
            add_switch(row, alarm->enabled);
            pill(content, c().delete_item, ui::ButtonTone::Danger, kGutter, kActionY, kWidth, clock_item_delete_event);
            return;
        }
        const ReminderItem* reminder = nullptr;
        for (const auto& item : clock.reminders) if (item.id == id) reminder = &item;
        make_header(c().reminder);
        if (!reminder) {
            state_text(200, c().clock_empty, nullptr);
            return;
        }
        state_symbol(104, 88, ui::IconRole::BellRing, ui::tokens::magenta);
        const auto when = when_text(reminder->due_minute);
        state_text(204, reminder->text.c_str(), when.c_str());
        pill(content, c().delete_item, ui::ButtonTone::Danger, kGutter, kActionY, kWidth, clock_item_delete_event);
    }

    void render_clock_alert() {
        header_action = true;
        if (!clock.alert) {
            state_text(200, c().clock_empty, nullptr);
            return;
        }
        const auto& alert = *clock.alert;
        alert_ring = spinner_arc(content, 176, ui::tokens::amber, 6);
        lv_arc_set_angles(alert_ring, 0, 360);
        lv_obj_set_pos(alert_ring, 152, 40);
        state_symbol(64, 128, ui::IconRole::BellRing, ui::tokens::amber);
        char time[8];
        std::snprintf(time, sizeof(time), "%02d:%02d", alert.hour, alert.minute);
        std::string detail = alert.label;
        const char* title = c().reminder;
        if (alert.kind == ClockAlertKind::Timer) {
            title = c().alert_time_up;
            if (detail.empty()) detail = duration_words(alert.duration_s);
        } else if (alert.kind == ClockAlertKind::Alarm) {
            title = time;
            if (detail.empty()) detail = c().alarm;
        }
        state_text(236, title, detail.empty() ? nullptr : detail.c_str());
        pill(content, c().alert_stop, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth, alert_dismiss_event);
        auto* snooze = pill(content, alert.kind == ClockAlertKind::Timer ? c().alert_plus_minute : c().alert_snooze,
                            ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, kActionY, kHalfWidth,
                            alert_snooze_event);
        set_value(snooze, alert.kind == ClockAlertKind::Timer ? 1U : 5U);
    }

    void update_clock_labels() {
        if (home_timer_label) {
            if (const auto* timer = soonest_timer()) {
                set_text_if_changed(home_timer_label, countdown_text(timer->remaining_ms).c_str());
            }
        }
        for (const auto& [id, target] : timer_labels) {
            for (const auto& timer : clock.timers) {
                if (timer.id == id) set_text_if_changed(target, countdown_text(timer.remaining_ms).c_str());
            }
        }
        if (clock_item_label && selected_clock_kind == ClockAlertKind::Timer) {
            for (const auto& timer : clock.timers) {
                if (timer.id == selected_clock_id) {
                    set_text_if_changed(clock_item_label, countdown_text(timer.remaining_ms).c_str());
                }
            }
        }
    }

    // ---- Notes -------------------------------------------------------------

    static std::string first_line(const std::string& text) {
        const auto end = text.find('\n');
        return end == std::string::npos ? text : text.substr(0, end);
    }

    void render_notes() {
        make_header(c().app_notes);
        if (notes.notes.empty()) {
            state_symbol(112, 112, ui::IconRole::Notebook, ui::tokens::pink);
            state_text(244, c().notes_empty, c().notes_empty_hint);
        } else {
            auto* list = make_list(kHeader, kActionY - 4 - kHeader);
            int y = 0;
            std::size_t shown = 0;
            for (const auto& note : notes.notes) {
                if (++shown > 30) break;
                const bool voice_note = !note.audio_file.empty();
                const std::string title = note.text.empty() ? std::string(c().voice_note_title) : first_line(note.text);
                std::string detail = note_stamp(note.created_minute);
                if (voice_note) {
                    char length[16];
                    std::snprintf(length, sizeof(length), "%u:%02u", note.audio_ms / 60000U, (note.audio_ms / 1000U) % 60U);
                    detail += detail.empty() ? length : std::string(" · ") + length;
                    if (note.transcript == NoteTranscript::Pending) detail += std::string(" · ") + c().transcript_pending;
                    if (note.transcript == NoteTranscript::Failed) detail += std::string(" · ") + c().transcript_failed;
                }
                auto* row = list_row(list, y, title.c_str(), detail.empty() ? nullptr : detail.c_str(),
                                     voice_note ? ui::IconRole::Microphone : ui::IconRole::Notebook, ui::tokens::pink);
                tappable(row, note_event, note.id);
                y += kRowPitch;
            }
            list_end(list, y);
        }
        auto* voice_button = pill(content, c().note_voice, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth,
                                  note_record_event);
        ui::set_button_disabled(voice_button, !capabilities.microphone || !callbacks.on_note_record);
        auto* typed = pill(content, c().note_typed, ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, kActionY,
                           kHalfWidth, note_typed_event);
        ui::set_button_disabled(typed, !callbacks.on_note_add);
    }

    void render_note_new() {
        make_header(c().action_note_title, ui::IconRole::Check, note_save_event, ui::tokens::blue,
                    static_cast<bool>(callbacks.on_note_add));
        note_area = make_text_input(kGutter, 100, kWidth, 96, c().note_placeholder, false);
        if (!note_draft.empty()) lv_textarea_set_text(note_area, note_draft.c_str());
        lv_textarea_set_max_length(note_area, 400);
        lv_obj_add_event_cb(note_area, note_text_event, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(note_area, note_save_event, LV_EVENT_READY, this);
        make_form_keyboard(note_area, ui::KeyboardProfile::Text);
    }

    const NoteItem* find_note(std::uint32_t id) const {
        for (const auto& note : notes.notes) {
            if (note.id == id) return &note;
        }
        return nullptr;
    }

    void render_note_detail() {
        const auto* note = find_note(selected_note);
        if (!note) {
            make_header(c().app_notes);
            state_text(200, c().notes_empty, nullptr);
            return;
        }
        const auto stamp = note_stamp(note->created_minute);
        const bool voice_note = !note->audio_file.empty();
        make_header(voice_note ? c().note_voice : c().app_notes, ui::IconRole::Delete, note_delete_event,
                    ui::tokens::coral, static_cast<bool>(callbacks.on_note_delete));
        if (!stamp.empty()) {
            line(content, stamp.c_str(), kGutter + 4, 100, kWidth - 8, ui::font_caption(), ui::tokens::text_tertiary);
        }
        auto* reader = lv_obj_create(content);
        ui::reset(reader);
        lv_obj_set_size(reader, kWidth, voice_note ? 236 : 328);
        lv_obj_set_pos(reader, kGutter, 136);
        lv_obj_set_clickable(reader, true);
        lv_obj_set_scrollable(reader, true);
        lv_obj_set_scroll_dir(reader, LV_DIR_VER);
        lv_obj_set_overflow_visible(reader, false);
        ui::style_scrollbar(reader);
        primary_scroller = reader;
        const char* body = note->text.c_str();
        if (note->text.empty()) {
            body = note->transcript == NoteTranscript::Pending ? c().transcript_pending : c().transcript_failed;
        }
        auto* text = ui::make_label(reader, body, note->text.empty() ? ui::tokens::text_tertiary : ui::tokens::text,
                                    ui::font_body());
        lv_obj_set_width(text, kWidth - 16);
        lv_obj_set_style_text_line_space(text, 6, 0);
        if (voice_note) {
            auto* play = pill(content, c().play, ui::ButtonTone::Secondary, kGutter, kActionY, kWidth, note_play_event);
            ui::set_button_disabled(play, !callbacks.on_note_play);
        }
    }

    void render_note_record() {
        const bool processing = voice.state == VoiceCaptureState::Processing;
        make_header(c().recording_title);
        header_action = true;
        record_ring = spinner_arc(content, 200, processing ? ui::tokens::lilac : ui::tokens::coral, 4);
        lv_arc_set_angles(record_ring, 0, processing ? 100 : 360);
        lv_obj_set_pos(record_ring, 140, 92);
        state_symbol(120, 144, ui::IconRole::Microphone, processing ? ui::tokens::lilac : ui::tokens::coral);
        char elapsed[16];
        std::snprintf(elapsed, sizeof(elapsed), "%u:%02u", voice.elapsed_ms / 60000U, (voice.elapsed_ms / 1000U) % 60U);
        record_time = label(content, elapsed, 0, 300, ui::tokens::display_width, ui::font_headline(), ui::tokens::text,
                            LV_TEXT_ALIGN_CENTER);
        line(content, processing ? c().transcript_pending : c().recording_detail, kGutter, 340, kWidth,
             ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        auto* stop = pill(content, c().done, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth,
                          note_record_stop_event);
        ui::set_button_disabled(stop, processing);
        pill(content, c().cancel, ui::ButtonTone::Danger, kGutter + kHalfWidth + 16, kActionY, kHalfWidth,
             note_record_cancel_event);
    }

    void update_record_widgets() {
        if (record_ring && voice.state == VoiceCaptureState::Recording) {
            lv_obj_set_style_arc_width(record_ring, 4 + static_cast<int>(voice.level * 10.0F), LV_PART_INDICATOR);
        }
        if (record_time) {
            char elapsed[16];
            std::snprintf(elapsed, sizeof(elapsed), "%u:%02u", voice.elapsed_ms / 60000U,
                          (voice.elapsed_ms / 1000U) % 60U);
            set_text_if_changed(record_time, elapsed);
        }
    }

    // ---- Today -------------------------------------------------------------

    struct BriefingLine {
        ui::IconRole role;
        std::uint32_t accent;
        std::string title;
        std::string detail;
        UiView target;
    };

    Briefing current_briefing() const {
        BriefingInput input;
        input.status = &status;
        input.weather = &weather;
        input.clock = &clock;
        input.unread_messages = static_cast<int>(aoraton.notifications.size()) + snapshot.notification_count;
        input.phone_notifications = static_cast<int>(phone.notifications.size());
        return build_briefing(input);
    }

    const char* greeting(BriefingPeriod period) const {
        switch (period) {
            case BriefingPeriod::Morning: return c().greeting_morning;
            case BriefingPeriod::Afternoon: return c().greeting_afternoon;
            case BriefingPeriod::Evening: return c().greeting_evening;
            case BriefingPeriod::Night: break;
        }
        return c().greeting_night;
    }

    std::vector<BriefingLine> briefing_lines(const Briefing& briefing) const {
        std::vector<BriefingLine> lines;
        char title[96];
        char detail[160];
        for (const auto& item : briefing.items) {
            switch (item.kind) {
                case BriefingItemKind::Weather: {
                    std::snprintf(title, sizeof(title), "%d° · %s", item.value, weather_condition_text(item.condition));
                    std::snprintf(detail, sizeof(detail), c().briefing_weather_detail_format, item.text.c_str(),
                                  item.high, item.low, std::max(0, item.rain_percent));
                    std::string text = detail;
                    if (item.demo) text = std::string(v().demo_badge) + " · " + text;
                    lines.push_back({weather_condition_icon(item.condition), weather_condition_accent(item.condition),
                                     title, text, UiView::Weather});
                    break;
                }
                case BriefingItemKind::Alarm:
                    std::snprintf(title, sizeof(title), c().action_alarm_format, item.hour, item.minute);
                    lines.push_back({ui::IconRole::Reminder, ui::tokens::amber, title, item.text, UiView::Clock});
                    break;
                case BriefingItemKind::Reminders:
                    if (item.count == 1) std::snprintf(title, sizeof(title), "%s", c().briefing_reminders_one);
                    else std::snprintf(title, sizeof(title), c().briefing_reminders_format, item.count);
                    std::snprintf(detail, sizeof(detail), c().briefing_next_format, item.hour, item.minute,
                                  item.text.c_str());
                    lines.push_back({ui::IconRole::BellRing, ui::tokens::magenta, title, detail, UiView::Clock});
                    break;
                case BriefingItemKind::Timers:
                    if (item.count == 1) std::snprintf(title, sizeof(title), "%s", c().briefing_timers_one);
                    else std::snprintf(title, sizeof(title), c().briefing_timers_format, item.count);
                    lines.push_back({ui::IconRole::Timer, ui::tokens::coral, title,
                                     countdown_text(static_cast<std::uint64_t>(item.value) * 1000U), UiView::Clock});
                    break;
                case BriefingItemKind::Messages:
                    if (item.count == 1) std::snprintf(title, sizeof(title), "%s", c().briefing_messages_one);
                    else std::snprintf(title, sizeof(title), c().briefing_messages_format, item.count);
                    lines.push_back({ui::IconRole::Message, ui::tokens::mint, title, {}, UiView::Notifications});
                    break;
                case BriefingItemKind::PhoneNotifications:
                    if (item.count == 1) std::snprintf(title, sizeof(title), "%s", c().briefing_phone_one);
                    else std::snprintf(title, sizeof(title), c().briefing_phone_format, item.count);
                    lines.push_back({ui::IconRole::Smartphone, ui::tokens::mint, title, {}, UiView::Notifications});
                    break;
            }
        }
        return lines;
    }

    std::string date_text() const {
        if (!(status.date_valid && status.month >= 1 && status.month <= 12 && status.weekday >= 0 &&
              status.weekday <= 6)) {
            return {};
        }
        char date[80];
        std::snprintf(date, sizeof(date), v().date_format, v().weekday_names[status.weekday], status.day,
                      v().month_names[status.month - 1]);
        return date;
    }

    // Spoken form: the same facts without the visual separators.
    std::string briefing_speech() const {
        const auto briefing = current_briefing();
        std::string text = greeting(briefing.period);
        const auto date = date_text();
        if (!date.empty()) text += ". " + date;
        const auto lines = briefing_lines(briefing);
        if (lines.empty()) text += std::string(". ") + c().briefing_empty;
        for (const auto& item : lines) {
            text += ". " + item.title;
            if (!item.detail.empty()) text += ", " + item.detail;
        }
        std::string spoken;
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (text.compare(index, 2, "\xC2\xB7") == 0) { spoken += ','; ++index; continue; }  // middle dot
            if (text.compare(index, 2, "\xC2\xB0") == 0) { ++index; continue; }                 // degree sign
            spoken += text[index];
        }
        return spoken + ".";
    }

    static void briefing_line_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        const auto view = static_cast<UiView>(user_value(event));
        if (view == UiView::Weather) {
            self->navigate(UiView::Weather);
            if (self->callbacks.on_weather_open) self->callbacks.on_weather_open();
            return;
        }
        self->navigate(view);
    }

    void render_briefing() {
        const auto briefing = current_briefing();
        if (capabilities.speech_output && callbacks.on_speak) {
            make_header(greeting(briefing.period), ui::IconRole::VolumeUp, briefing_speak_event, ui::tokens::mint);
        } else {
            make_header(greeting(briefing.period));
        }
        const auto date = date_text();
        line(content, date.empty() ? c().app_today : date.c_str(), kGutter + 4, 96, kWidth - 8, ui::font_caption(),
             ui::tokens::text_secondary);
        const auto lines = briefing_lines(briefing);
        if (lines.empty()) {
            state_symbol(144, 112, ui::IconRole::Sunrise, ui::tokens::magenta);
            state_text(280, c().briefing_empty, nullptr);
            return;
        }
        auto* list = make_list(136, ui::tokens::display_height - 136);
        int y = 0;
        for (const auto& item : lines) {
            auto* row = list_row(list, y, item.title.c_str(), item.detail.empty() ? nullptr : item.detail.c_str(),
                                 item.role, item.accent);
            add_chevron(row);
            tappable(row, briefing_line_event, static_cast<std::uintptr_t>(item.target));
            y += kRowPitch;
        }
        list_end(list, y);
    }

    // ---- Translation -------------------------------------------------------

    TranslateSide translation_active_side() const {
        if (translation.entries.empty()) return TranslateSide::Mine;
        return translation.entries.back().side;
    }

    // One half of the table: language, the latest text in that language, and
    // its own microphone. The top half is drawn upside down for the other person.
    void translate_half(lv_obj_t* parent, int top, TranslateSide side, int language, const std::string& text,
                        bool placeholder) {
        line(parent, c().language_names[language], kGutter, top + 12, 300, ui::font_caption(),
             ui::tokens::text_tertiary);
        auto* body = label(parent, text.c_str(), kGutter, top + 44, 320, text.size() > 60 ? ui::font_body() :
                           ui::font_title(), placeholder ? ui::tokens::text_secondary : ui::tokens::text);
        lv_obj_set_height(body, 152);
        lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
        const bool recording = voice.state == VoiceCaptureState::Recording && voice.purpose == VoicePurpose::Translate &&
                               translation_active_side() == side;
        const std::uint32_t accent = recording ? ui::tokens::coral :
            (side == TranslateSide::Mine ? ui::tokens::ice : ui::tokens::magenta);
        auto* mic = round_button(parent, 368, top + 88, 88, recording ? ui::IconRole::Stop : ui::IconRole::Microphone,
                                 accent, translate_record_event);
        set_value(mic, side == TranslateSide::Mine ? 0U : 1U);
        ui::set_button_disabled(mic, !capabilities.microphone || !capabilities.speech_recognition ||
                                     !callbacks.on_translate_record);
        if (recording) {
            translate_ring = spinner_arc(parent, 104, ui::tokens::coral, 4);
            lv_arc_set_angles(translate_ring, 0, 360);
            lv_obj_set_pos(translate_ring, 360, top + 80);
        }
    }

    void render_translate() {
        header_action = true;
        const int mine = 0;
        const int theirs = language_index(translation.their_language);
        std::string mine_text = c().translate_tap[mine];
        std::string theirs_text = c().translate_tap[theirs];
        bool mine_placeholder = true;
        bool theirs_placeholder = true;
        if (!(capabilities.microphone && capabilities.speech_recognition)) {
            mine_text = c().translate_needs_voice;
        }
        if (!translation.entries.empty()) {
            const auto& entry = translation.entries.back();
            const bool from_mine = entry.side == TranslateSide::Mine;
            const int source = from_mine ? mine : theirs;
            const int target = from_mine ? theirs : mine;
            std::string source_text;
            std::string target_text;
            bool target_placeholder = true;
            switch (entry.status) {
                case TranslationStatus::Listening: source_text = c().translate_listening[source]; break;
                case TranslationStatus::Transcribing: source_text = c().translate_working[source]; break;
                case TranslationStatus::Translating:
                    source_text = entry.source;
                    target_text = c().translate_working[target];
                    break;
                case TranslationStatus::Ready:
                    source_text = entry.source;
                    target_text = entry.translated;
                    target_placeholder = false;
                    break;
                case TranslationStatus::Failed:
                    source_text = entry.source.empty() ? c().translate_failed : entry.source;
                    target_text = c().translate_failed;
                    break;
            }
            const bool source_placeholder = entry.status == TranslationStatus::Listening ||
                                            entry.status == TranslationStatus::Transcribing;
            if (from_mine) {
                mine_text = source_text;
                mine_placeholder = source_placeholder;
                if (!target_text.empty()) { theirs_text = target_text; theirs_placeholder = target_placeholder; }
            } else {
                theirs_text = source_text;
                theirs_placeholder = source_placeholder;
                if (!target_text.empty()) { mine_text = target_text; mine_placeholder = target_placeholder; }
            }
        }
        auto* top = lv_obj_create(content);
        ui::reset(top);
        lv_obj_set_size(top, ui::tokens::display_width, 208);
        lv_obj_set_pos(top, 0, 0);
        lv_obj_set_style_transform_pivot_x(top, ui::tokens::display_width / 2, 0);
        lv_obj_set_style_transform_pivot_y(top, 104, 0);
        lv_obj_set_style_transform_rotation(top, 1800, 0);
        translate_half(top, 0, TranslateSide::Theirs, theirs, theirs_text, theirs_placeholder);
        translate_half(content, 272, TranslateSide::Mine, mine, mine_text, mine_placeholder);
        // The middle band: back, and the other person's language.
        auto* back = round_button(content, 16, 208, ui::tokens::HEADER_BUTTON, ui::IconRole::ChevronLeft,
                                  ui::tokens::text, back_event, true);
        lv_obj_add_event_cb(back, home_event, LV_EVENT_LONG_PRESSED, this);
        char languages[32];
        std::snprintf(languages, sizeof(languages), "%s  \xE2\x86\x92  %s", c().language_codes[mine],
                      c().language_codes[theirs]);
        auto* swap = pill(content, languages, ui::ButtonTone::Secondary, 152, 208, 176, translate_language_event);
        lv_obj_set_height(swap, ui::tokens::HEADER_BUTTON);
        ui::set_button_disabled(swap, !callbacks.on_translate_language);
    }

    // ---- Always-on clock, calls and phone ------------------------------------

    void render_always_on() {
        header_action = true;
        // The whole screen wakes the device; that first touch does nothing else.
        auto* catcher = lv_button_create(content);
        ui::reset(catcher);
        lv_obj_set_size(catcher, ui::tokens::display_width, ui::tokens::display_height);
        lv_obj_set_pos(catcher, 0, 0);
        lv_obj_set_style_bg_opa(catcher, LV_OPA_TRANSP, 0);
        lv_obj_set_clickable(catcher, true);
        lv_obj_add_event_cb(catcher, wake_event, LV_EVENT_CLICKED, this);
        if (display_power.mode == DisplayMode::Off) return;
        const int dx = display_power.shift_x;
        const int dy = display_power.shift_y;
        constexpr std::uint32_t kDim = 0x6F7A8C;
        if (status.clock_valid) {
            char time[12];
            std::snprintf(time, sizeof(time), "%02d:%02d", status.hour, status.minute);
            label(content, time, dx, 140 + dy, ui::tokens::display_width, ui::font_clock(), kDim, LV_TEXT_ALIGN_CENTER);
        }
        const auto date = date_text();
        if (!date.empty()) {
            line(content, date.c_str(), kGutter + dx, 228 + dy, kWidth, ui::font_caption(), kDim, LV_TEXT_ALIGN_CENTER);
        }
        if (const auto alarm = next_alarm(clock)) {
            char time[8];
            std::snprintf(time, sizeof(time), "%02d:%02d", alarm->hour, alarm->minute);
            auto* icon = icon_view(content, ui::IconRole::Reminder, 24, kDim);
            lv_obj_set_pos(icon, 196 + dx, 276 + dy);
            line(content, time, 228 + dx, 274 + dy, 80, ui::font_caption(), kDim);
        }
        if (notification_total() > 0) {
            auto* dot = make_dot(content, 10, ui::tokens::pink, LV_OPA_70);
            lv_obj_set_pos(dot, 235 + dx, 320 + dy);
        }
    }

    void render_phone_call() {
        header_action = true;
        if (!phone.incoming_call) {
            state_text(200, c().incoming_call, nullptr);
            return;
        }
        const auto& call = *phone.incoming_call;
        state_symbol(64, 112, ui::IconRole::Phone, ui::tokens::mint);
        std::string detail = call.app.empty() ? std::string(c().incoming_call) :
                             std::string(c().incoming_call) + " · " + call.app;
        if (call.demo) detail += std::string(" · ") + t().demo;
        state_text(196, call.title.empty() ? c().incoming_call : call.title.c_str(), detail.c_str(), c().call_on_phone);
        auto* decline = round_button(content, call.can_accept ? 96 : 192, 360, 96, ui::IconRole::PhoneOff,
                                     ui::tokens::coral, phone_call_event);
        set_value(decline, 0U);
        ui::set_button_disabled(decline, !call.can_decline || !callbacks.on_phone_call);
        if (call.can_accept) {
            auto* accept = round_button(content, 288, 360, 96, ui::IconRole::Phone, ui::tokens::mint, phone_call_event);
            set_value(accept, 1U);
            ui::set_button_disabled(accept, !callbacks.on_phone_call);
        }
    }

    void render_phone_notification() {
        const PhoneNotification* item = nullptr;
        for (const auto& value : phone.notifications) {
            if (value.uid == selected_phone_uid) item = &value;
        }
        if (!item) {
            make_header(c().phone_notifications);
            state_text(200, v().notification_empty, nullptr);
            return;
        }
        make_header(item->app.empty() ? c().phone_notifications : item->app.c_str());
        if (item->demo) header_tag(t().demo, ui::tokens::amber);
        auto* title = label(content, item->title.c_str(), kGutter, 104, kWidth, ui::font_title(), ui::tokens::text);
        lv_obj_set_height(title, 80);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        auto* reader = lv_obj_create(content);
        ui::reset(reader);
        lv_obj_set_size(reader, kWidth, 180);
        lv_obj_set_pos(reader, kGutter, 192);
        lv_obj_set_scrollable(reader, true);
        lv_obj_set_scroll_dir(reader, LV_DIR_VER);
        lv_obj_set_overflow_visible(reader, false);
        primary_scroller = reader;
        auto* body = ui::make_label(reader, item->message.c_str(), ui::tokens::text_secondary, ui::font_body());
        lv_obj_set_width(body, kWidth - 16);
        auto* dismiss = pill(content, c().dismiss_notification, ui::ButtonTone::Secondary, kGutter, kActionY, kWidth,
                             phone_dismiss_event);
        ui::set_button_disabled(dismiss, !callbacks.on_phone_dismiss);
    }

    void render_phone_link() {
        make_header(c().phone_title);
        const bool connected = phone.link == PhoneLinkState::Connected;
        const std::uint32_t accent = connected ? ui::tokens::mint :
            (phone.link == PhoneLinkState::Unavailable ? ui::tokens::text_tertiary : ui::tokens::ice);
        state_symbol(104, 104, ui::IconRole::Smartphone, accent);
        if (connected) {
            state_text(224, c().phone_connected, phone.device_name.c_str());
        } else {
            const auto summary = phone_summary();
            state_text(220, summary.c_str(), c().phone_iphone_help, c().phone_android_help);
        }
        if (connected || phone.link == PhoneLinkState::Disconnected) {
            auto* forget = pill(content, c().phone_forget, ui::ButtonTone::Danger, kGutter, kActionY, kWidth,
                                phone_forget_event);
            ui::set_button_disabled(forget, !callbacks.on_phone_forget);
        } else {
            auto* pair = pill(content, c().phone_pair, ui::ButtonTone::Primary, kGutter, kActionY, kWidth,
                              phone_pair_event);
            ui::set_button_disabled(pair, !capabilities.phone_link || !callbacks.on_phone_pair ||
                                          phone.link == PhoneLinkState::Pairing);
        }
    }

    // ---- Display and software update ------------------------------------------

    lv_obj_t* setting_switch_row(lv_obj_t* list, int y, const char* title, const char* detail, ui::IconRole role,
                                 std::uint32_t accent, bool checked, bool available, std::uintptr_t value) {
        auto* row = list_row(list, y, title, available ? detail : c().needs_motion_sensor, role, accent, kRowHeight, 252);
        add_switch(row, checked && available);
        if (available) tappable(row, display_setting_event, value);
        else lv_obj_set_style_opa(row, LV_OPA_60, 0);
        return row;
    }

    void render_display_settings() {
        make_header(c().display_title);
        auto* list = make_list();
        const auto& settings = display_power.settings;
        setting_switch_row(list, 0, c().always_on, c().always_on_detail, ui::IconRole::Moon, ui::tokens::indigo,
                           settings.always_on, true, 0);
        setting_switch_row(list, kRowPitch, c().raise_to_wake, c().raise_detail, ui::IconRole::Hand, ui::tokens::mint,
                           settings.raise_to_wake, capabilities.imu, 1);
        setting_switch_row(list, kRowPitch * 2, c().flip_to_mute, c().flip_detail, ui::IconRole::Flip,
                           ui::tokens::amber, settings.flip_to_mute, capabilities.imu, 2);
        char timeout[48];
        std::snprintf(timeout, sizeof(timeout), c().seconds_format, static_cast<unsigned>(settings.timeout_ms / 1000U));
        auto* row = list_row(list, kRowPitch * 3, c().screen_timeout, timeout, ui::IconRole::Display, ui::tokens::blue);
        add_chevron(row);
        tappable(row, display_setting_event, 3);
        list_end(list, kRowPitch * 4);
    }

    void render_update() {
        make_header(c().update_short);
        if (update.demo_source) header_tag(t().demo, ui::tokens::amber);
        char version[64];
        std::snprintf(version, sizeof(version), c().update_version_format, update.current_version.c_str());
        state_symbol(96, 96, ui::IconRole::Download, update.state == UpdateState::Failed ? ui::tokens::coral :
                                                                                      ui::tokens::blue);
        if (!update.source_configured) {
            state_text(208, version, c().update_no_source);
            return;
        }
        const char* note = update.demo_source ? c().update_demo_note : nullptr;
        char text[96];
        switch (update.state) {
            case UpdateState::Idle:
            case UpdateState::UpToDate: {
                state_text(208, update.state == UpdateState::UpToDate ? c().update_up_to_date : version,
                           update.state == UpdateState::UpToDate ? version : nullptr, note);
                auto* check = pill(content, c().update_check, update.state == UpdateState::Idle ?
                                   ui::ButtonTone::Primary : ui::ButtonTone::Secondary, kGutter, kActionY, kWidth,
                                   update_check_event);
                ui::set_button_disabled(check, !callbacks.on_update_check);
                return;
            }
            case UpdateState::Checking:
                state_text(208, c().update_checking, version, note);
                return;
            case UpdateState::Available: {
                if (!update.available) return;
                const auto tenths = (update.available->size_bytes * 10U + 524288U) / 1048576U;
                std::snprintf(text, sizeof(text), c().update_available_format, update.available->version.c_str(),
                              static_cast<int>(tenths / 10U), static_cast<int>(tenths % 10U));
                const auto block = update_block(update, status.connection_known && status.online,
                                                status.battery_percent, status.power_known, status.charging);
                const char* reason = block == UpdateBlock::Offline ? c().update_block_offline :
                    (block == UpdateBlock::LowBattery ? c().update_block_battery : nullptr);
                state_text(208, text, update.available->notes.empty() ? nullptr : update.available->notes.c_str(),
                           update_confirming ? c().update_install_confirm : (reason ? reason : note));
                if (update_confirming) {
                    pill(content, c().update_install, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth,
                         update_install_event);
                    pill(content, c().cancel, ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, kActionY,
                         kHalfWidth, update_cancel_event);
                } else {
                    auto* install = pill(content, c().update_install, ui::ButtonTone::Primary, kGutter, kActionY,
                                         kWidth, update_install_event);
                    ui::set_button_disabled(install, block != UpdateBlock::None || !callbacks.on_update_install);
                }
                return;
            }
            case UpdateState::Downloading: {
                std::snprintf(text, sizeof(text), c().update_downloading_format,
                              static_cast<int>(update.progress * 100.0F));
                update_label = lv_obj_get_child(state_text(208, text, version, note), 0);
                auto* bar = lv_bar_create(content);
                update_bar = bar;
                lv_obj_set_size(bar, kWidth, 12);
                lv_obj_set_pos(bar, kGutter, 340);
                lv_bar_set_range(bar, 0, 100);
                lv_bar_set_value(bar, static_cast<int32_t>(update.progress * 100.0F), LV_ANIM_OFF);
                lv_obj_set_style_bg_color(bar, ui::color(ui::tokens::surface_high), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_bg_color(bar, ui::color(ui::tokens::blue), LV_PART_INDICATOR);
                lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
                lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
                return;
            }
            case UpdateState::Verifying:
                state_text(208, c().update_verifying, version, note);
                return;
            case UpdateState::ReadyToRestart: {
                state_text(208, c().update_ready, version, note);
                auto* restart = pill(content, c().update_restart, ui::ButtonTone::Primary, kGutter, kActionY, kWidth,
                                     update_restart_event);
                ui::set_button_disabled(restart, !callbacks.on_update_restart);
                return;
            }
            case UpdateState::Failed: {
                char code[96] = {};
                if (!update.error_code.empty()) {
                    std::snprintf(code, sizeof(code), t().error_code_format, update.error_code.c_str());
                }
                state_text(208, c().update_failed, version, code[0] ? code : note);
                auto* retry = pill(content, c().update_check, ui::ButtonTone::Secondary, kGutter, kActionY, kWidth,
                                   update_check_event);
                ui::set_button_disabled(retry, !callbacks.on_update_check);
                return;
            }
        }
    }

    void update_progress_widgets() {
        if (update_bar) lv_bar_set_value(update_bar, static_cast<int32_t>(update.progress * 100.0F), LV_ANIM_OFF);
        if (update_label && update.state == UpdateState::Downloading) {
            char text[96];
            std::snprintf(text, sizeof(text), c().update_downloading_format, static_cast<int>(update.progress * 100.0F));
            set_text_if_changed(update_label, text);
        }
    }

    // ---- Dispatch, back, tick ------------------------------------------------

    void render_companion(UiView view) {
        switch (view) {
            case UiView::Clock: render_clock(); break;
            case UiView::ClockNew: render_clock_new(); break;
            case UiView::TimerNew: render_timer_new(); break;
            case UiView::AlarmNew: render_alarm_new(); break;
            case UiView::ReminderNew: render_reminder_new(); break;
            case UiView::ClockItem: render_clock_item(); break;
            case UiView::ClockAlert: render_clock_alert(); break;
            case UiView::Notes: render_notes(); break;
            case UiView::NoteNew: render_note_new(); break;
            case UiView::NoteDetail: render_note_detail(); break;
            case UiView::NoteRecord: render_note_record(); break;
            case UiView::Briefing: render_briefing(); break;
            case UiView::Translate: render_translate(); break;
            case UiView::AlwaysOn: render_always_on(); break;
            case UiView::PhoneCall: render_phone_call(); break;
            case UiView::PhoneNotification: render_phone_notification(); break;
            case UiView::PhoneLink: render_phone_link(); break;
            case UiView::DisplaySettings: render_display_settings(); break;
            case UiView::Update: render_update(); break;
            default: break;
        }
    }

    // Back from a companion screen; returns true when it was handled here.
    bool companion_back() {
        if (current_view == UiView::ReminderNew && reminder_step == 1) {
            reminder_step = 0;
            render(UiView::ReminderNew);
            return true;
        }
        if (current_view == UiView::NoteRecord && callbacks.on_note_record_cancel) callbacks.on_note_record_cancel();
        if (current_view == UiView::Translate && voice.state != VoiceCaptureState::Idle &&
            voice.purpose == VoicePurpose::Translate && callbacks.on_translate_stop) {
            callbacks.on_translate_stop();
        }
        return false;
    }

    // Full-screen moments carry no status chrome.
    static bool immersive(UiView view) {
        return view == UiView::AlwaysOn || view == UiView::Translate || view == UiView::ClockAlert ||
               view == UiView::PhoneCall;
    }

    void companion_tick() {
        if (alert_ring && !capture_mode && !reduced_motion) {
            const auto phase = static_cast<int>(phase_ms % 1200U);
            const int pulse = phase < 600 ? phase : 1200 - phase;
            lv_obj_set_style_arc_opa(alert_ring, static_cast<lv_opa_t>(90 + pulse * 150 / 600), LV_PART_INDICATOR);
        }
        if (translate_ring && voice.state == VoiceCaptureState::Recording) {
            lv_obj_set_style_arc_width(translate_ring, 3 + static_cast<int>(voice.level * 9.0F), LV_PART_INDICATOR);
        }
        if (note_delete_armed && phase_ms - note_delete_armed_at > kForgetArmMs) note_delete_armed = false;
    }

    // Dimmed screens: a translucent black veil (the panel brightness on device).
    void update_dim_overlay() {
        const bool dim = display_power.mode == DisplayMode::Dimmed;
        if (!dim && !dim_overlay) return;
        if (!dim_overlay) {
            dim_overlay = lv_obj_create(lv_layer_top());
            ui::reset(dim_overlay);
            lv_obj_set_size(dim_overlay, ui::tokens::display_width, ui::tokens::display_height);
            lv_obj_set_style_bg_color(dim_overlay, ui::color(ui::tokens::canvas), 0);
            lv_obj_set_style_bg_opa(dim_overlay, 150, 0);
            lv_obj_set_clickable(dim_overlay, false);
        }
        lv_obj_set_hidden(dim_overlay, !dim);
    }

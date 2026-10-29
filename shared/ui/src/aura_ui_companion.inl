// Included inside AuraUi::Impl (aura_ui.cpp): the v13 "Companion" screens.
// It is not a translation unit of its own; it shares the Impl helpers.
// Design: docs/design/AURA_V13_COMPANION_DESIGN.md.

    // ---- v13 "Companion" state ---------------------------------------------

    ClockSnapshot clock{};
    NotesSnapshot notes{};
    OrganizerSnapshot organizer{};
    VoiceSnapshot voice{};
    TranslationSnapshot translation{};
    PhoneSnapshot phone{};
    UpdateSnapshot update{};
    DisplaySnapshot display_power{};
    RadioSnapshot radio{};
    FuelPriceSnapshot fuel{};
    AssistantExtras extras{};
    LauncherLayoutSnapshot launcher_layout{};
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
    OrganizerKind organizer_kind{OrganizerKind::Event};
    std::string organizer_draft;
    int organizer_step{0};
    int organizer_day_offset{0};
    int organizer_hour{9};
    int organizer_minute{0};
    std::string radio_query_draft;
    bool radio_typing{false};
    bool radio_reordering{false};
    bool launcher_reordering{false};
    std::optional<std::size_t> launcher_selected_app;
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
    lv_obj_t* clock_item_arc{};
    lv_obj_t* alert_ring{};
    lv_obj_t* record_ring{};
    lv_obj_t* record_time{};
    lv_obj_t* translate_ring{};
    lv_obj_t* reminder_area{};
    lv_obj_t* note_area{};
    lv_obj_t* organizer_area{};
    lv_obj_t* update_bar{};
    lv_obj_t* update_label{};
    lv_obj_t* radio_search_area{};

    const l10n::CompanionStrings& c() const {
        return interface_language == InterfaceLanguage::English ? l10n::companion_en_us() :
                                                                  l10n::companion_el_gr();
    }

    void reset_companion_pointers() {
        timer_labels.clear();
        home_timer_label = nullptr;
        clock_item_label = nullptr;
        clock_item_arc = nullptr;
        alert_ring = nullptr;
        record_ring = nullptr;
        record_time = nullptr;
        translate_ring = nullptr;
        reminder_area = nullptr;
        note_area = nullptr;
        organizer_area = nullptr;
        update_bar = nullptr;
        update_label = nullptr;
        radio_search_area = nullptr;
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

    const RadioStation* radio_station(std::uintptr_t value) const {
        constexpr std::uintptr_t kResult = 1U << 16U;
        const bool result = (value & kResult) != 0;
        const auto index = static_cast<std::size_t>(value & ~kResult);
        const auto& stations = result ? radio.results : radio.favorites;
        return index < stations.size() ? &stations[index] : nullptr;
    }

    static void radio_station_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action() || !self->callbacks.on_radio_play) return;
        const auto* station = self->radio_station(user_value(event));
        if (!station) return;
        self->callbacks.on_radio_play(*station);
        self->return_to(UiView::Radio);
    }

    static void radio_stop_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->accept_action() && self->callbacks.on_radio_stop) self->callbacks.on_radio_stop();
    }

    static void radio_favorite_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action() || !self->callbacks.on_radio_favorite || self->radio.station.id.empty()) return;
        self->callbacks.on_radio_favorite(self->radio.station);
    }

    static void radio_reorder_toggle_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->radio_reordering = !self->radio_reordering;
        self->render(UiView::Radio);
    }

    static void radio_move_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_radio_move) return;
        constexpr std::uintptr_t kDown = 1U << 16U;
        const auto value = user_value(event);
        self->callbacks.on_radio_move(static_cast<std::size_t>(value & ~kDown), (value & kDown) ? 1 : -1);
    }

    static void radio_search_open_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->radio_typing = true;
        self->navigate(UiView::RadioSearch);
    }

    void submit_radio_search() {
        if (!radio_search_area || !callbacks.on_radio_search) return;
        const char* value = lv_textarea_get_text(radio_search_area);
        const std::string query = value ? value : "";
        if (query.size() < 2) return;
        radio_query_draft = query;
        radio_typing = false;
        callbacks.on_radio_search(query);
        render(UiView::RadioSearch);
    }

    static void radio_search_submit_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->submit_radio_search();
    }

    static void radio_search_ready_event(lv_event_t* event) {
        lv_async_call([](void* data) { static_cast<Impl*>(data)->submit_radio_search(); },
                      lv_event_get_user_data(event));
    }

    static void radio_query_changed_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        const char* value = lv_textarea_get_text(lv_event_get_current_target_obj(event));
        self->radio_query_draft = value ? value : "";
    }

    lv_obj_t* add_switch(lv_obj_t* row, bool checked) {
        auto* toggle = lv_switch_create(row);
        lv_obj_set_size(toggle, 64, 36);
        lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(toggle, ui::color(ui::tokens::surface_glass), LV_PART_MAIN);
        lv_obj_set_style_bg_color(toggle, ui::color(ui::tokens::toggle_on), kIndicatorChecked);
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
        const auto which = user_value(event);
        if (which == 0) self->alarm_hour = selected;
        else if (which == 1) self->alarm_minute = selected;
        else if (which == 2) self->organizer_hour = selected;
        else self->organizer_minute = selected;
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

    static void conversation_history_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->navigate(UiView::ConversationHistory);
    }

    static void conversation_open_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (!self->extras.conversation_history.empty()) self->navigate(UiView::ConversationHistory);
        else if (self->callbacks.on_ask) self->callbacks.on_ask();
    }

    static void conversation_history_clear_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (self->callbacks.on_conversation_clear) self->callbacks.on_conversation_clear();
        self->render(UiView::ConversationHistory);
    }

    static void speak_answer_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_speak || self->snapshot.answer_text.empty()) return;
        self->callbacks.on_speak(self->snapshot.answer_text,
                                 interface_language_code(self->interface_language));
    }

    static void briefing_speak_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_speak) return;
        self->callbacks.on_speak(self->briefing_speech(),
                                 interface_language_code(self->interface_language));
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

    // value: 0 always-on, 1 raise, 2 flip, 3 timeout, 4 brightness
    static void display_setting_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_display_settings) return;
        auto settings = self->display_power.settings;
        switch (user_value(event)) {
            case 0: settings.always_on = !settings.always_on; break;
            case 1: settings.raise_to_wake = !settings.raise_to_wake; break;
            case 2: settings.flip_to_mute = !settings.flip_to_mute; break;
            case 3:
                settings.timeout_ms = settings.timeout_ms <= 15000 ? 30000 : (settings.timeout_ms <= 30000 ? 60000 : 15000);
                break;
            default:
                settings.active_brightness_percent = settings.active_brightness_percent >= 100 ? 25 :
                    ((settings.active_brightness_percent / 25) + 1) * 25;
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

    // Edit modes belong to the screen that started them. Leaving it by any
    // route (back, swipe, Home, a voice command) ends them; the dark always-on
    // clock is the exception, because it only covers the screen while the
    // owner pauses and returns to the same view.
    void leave_edit_modes(UiView next) {
        if (next == UiView::AlwaysOn) return;
        if (current_view == UiView::Launcher && next != UiView::Launcher) {
            launcher_reordering = false;
            launcher_selected_app.reset();
        }
        if (current_view == UiView::Radio && next != UiView::Radio) radio_reordering = false;
    }

    static void launcher_reorder_toggle_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_launcher_app_move) return;
        self->launcher_reordering = !self->launcher_reordering;
        self->launcher_selected_app.reset();
        self->render(UiView::Launcher);
        self->show_toast(self->launcher_reordering ? self->c().app_layout_pick : self->c().app_layout_saved,
                         self->launcher_reordering ? ui::tokens::lilac : ui::tokens::mint);
    }

    static void launcher_app_reorder_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->launcher_reordering || !self->callbacks.on_launcher_app_move) return;
        const auto target = static_cast<std::size_t>(user_value(event));
        if (target >= kLauncherAppCount) return;
        if (!self->launcher_selected_app) {
            self->launcher_selected_app = target;
            self->render(UiView::Launcher);
            self->show_toast(self->c().app_layout_place, ui::tokens::lilac);
            return;
        }
        const auto from = *self->launcher_selected_app;
        self->launcher_selected_app.reset();
        if (from == target) {
            self->render(UiView::Launcher);
            self->show_toast(self->c().app_layout_pick, ui::tokens::text_secondary);
            return;
        }
        self->callbacks.on_launcher_app_move(from, target);
    }

    lv_obj_t* app_cell_in(lv_obj_t* parent, int column, int row, std::size_t app_index, const char* title,
                           ui::IconRole role, std::uint32_t accent, UiView view,
                           lv_event_cb_t special = nullptr) {
        // Edit mode outlines every cell; a small inset keeps the outlines from
        // touching, so the grid reads as separate tiles.
        const int inset = launcher_reordering ? 4 : 0;
        const int width = 144 - inset * 2;
        const auto grid = launcher_grid();
        auto* cell = lv_button_create(parent);
        ui::reset(cell);
        lv_obj_set_clickable(cell, true);
        lv_obj_set_size(cell, width, grid.cell_height - inset * 2);
        lv_obj_set_pos(cell, kGutter + column * 144 + inset, grid.top + row * grid.pitch + inset);
        lv_obj_set_style_radius(cell, ui::tokens::radius_large, 0);
        lv_obj_set_style_bg_color(cell, ui::color(ui::tokens::surface), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
        if (appearance == Appearance::Marble) {
            // Μάρμαρο: a hairline ring with an ivory icon and a quieter name.
            auto* circle = icon_well(cell, grid.circle, role, accent, 40);
            lv_obj_set_style_image_recolor(lv_obj_get_child(circle, 0), ui::color(ui::tokens::text), 0);
            lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, grid.circle_y - inset);
        } else {
            auto* circle = make_dot(cell, grid.circle, accent, kTint);
            lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, grid.circle_y - inset);
            auto* icon = icon_view(circle, role, 46, accent);
            lv_obj_center(icon);
        }
        line(cell, title, 0, grid.title_y - inset, width, ui::font_caption(),
             appearance == Appearance::Marble ? ui::tokens::text_secondary : ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        if (launcher_reordering) {
            const bool selected = launcher_selected_app && *launcher_selected_app == app_index;
            lv_obj_set_style_border_width(cell, selected ? 3 : 1, 0);
            lv_obj_set_style_border_color(cell, ui::color(selected ? accent : ui::tokens::surface_high), 0);
            lv_obj_set_style_border_opa(cell, selected ? LV_OPA_COVER : LV_OPA_40, 0);
            char position[8];
            std::snprintf(position, sizeof(position), "%u", static_cast<unsigned>(app_index + 1));
            auto* badge = ui::make_tag(cell, position, selected ? accent : ui::tokens::text_tertiary);
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -8, 4);
            set_value(cell, app_index);
            lv_obj_add_event_cb(cell, launcher_app_reorder_event, LV_EVENT_CLICKED, this);
        } else if (special) {
            lv_obj_add_event_cb(cell, special, LV_EVENT_CLICKED, this);
        } else {
            set_target(cell, view);
            lv_obj_add_event_cb(cell, nav_event, LV_EVENT_CLICKED, this);
        }
        return cell;
    }

    struct LauncherGrid {
        int top;
        int pitch;
        int cell_height;
        int circle;
        int circle_y;
        int title_y;
    };

    // Halo's cells are 164 px with 104 px circles, so a third row peeks in;
    // Μάρμαρο fits three rows of 84 px hairline circles on one screen.
    LauncherGrid launcher_grid() const {
        if (appearance == Appearance::Marble) return {4, 128, 124, 84, 2, 90};
        return {8, 172, 164, 104, 8, 120};
    }

    // Unread notifications: a count in Halo, a gold dot by the circle in Μάρμαρο.
    void launcher_unread_mark(lv_obj_t* cell, int count, std::uint32_t accent) {
        if (appearance == Appearance::Marble) {
            auto* dot = make_dot(cell, 14, ui::tokens::indigo);
            lv_obj_set_pos(dot, 95, 7);
            return;
        }
        char text[12];
        std::snprintf(text, sizeof(text), "%d", count);
        auto* badge = ui::make_tag(cell, text, accent);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(badge, ui::color(ui::tokens::text), 0);
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -12, 4);
    }

    // Apps use a 3-column scrolling grid; later rows intentionally peek.
    void render_launcher_grid() {
        make_header(launcher_reordering ? c().app_layout_title : v().applications,
                    launcher_reordering ? ui::IconRole::Check : ui::IconRole::Edit,
                    launcher_reorder_toggle_event, launcher_reordering ? ui::tokens::mint : ui::tokens::lilac,
                    static_cast<bool>(callbacks.on_launcher_app_move));
        auto* grid = make_list();
        for (std::size_t index = 0; index < launcher_layout.order.size(); ++index) {
            const int column = static_cast<int>(index % 3);
            const int row = static_cast<int>(index / 3);
            lv_obj_t* cell = nullptr;
            switch (launcher_layout.order[index]) {
                case LauncherApp::Conversation:
                    cell = app_cell_in(grid, column, row, index, v().conversation, ui::IconRole::Sparkles,
                                       ui::tokens::lilac, UiView::Assistant, conversation_open_event);
                    // A count of saved talks; Μάρμαρο marks only what is unread.
                    if (!launcher_reordering && !extras.conversation_history.empty() &&
                        appearance != Appearance::Marble) {
                        char count[8];
                        std::snprintf(count, sizeof(count), "%u",
                                      static_cast<unsigned>(extras.conversation_history.size()));
                        auto* badge = ui::make_tag(cell, count, ui::tokens::lilac);
                        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -10, 6);
                    }
                    break;
                case LauncherApp::Weather:
                    cell = app_cell_in(grid, column, row, index, v().weather, ui::IconRole::Weather,
                                       ui::tokens::amber, UiView::Weather, weather_open_event);
                    break;
                case LauncherApp::Messages:
                    cell = app_cell_in(grid, column, row, index, v().apps_messages, ui::IconRole::Message,
                                       ui::tokens::mint, UiView::AoratonRooms);
                    break;
                case LauncherApp::Clock:
                    cell = app_cell_in(grid, column, row, index, c().app_clock, ui::IconRole::Timer,
                                       ui::tokens::coral, UiView::Clock);
                    break;
                case LauncherApp::Notes:
                    cell = app_cell_in(grid, column, row, index, c().app_notes, ui::IconRole::Notebook,
                                       ui::tokens::pink, UiView::Notes);
                    break;
                case LauncherApp::Translate:
                    cell = app_cell_in(grid, column, row, index, c().app_translate, ui::IconRole::Languages,
                                       ui::tokens::ice, UiView::Translate);
                    break;
                case LauncherApp::Today:
                    cell = app_cell_in(grid, column, row, index, c().app_today, ui::IconRole::Sunrise,
                                       ui::tokens::magenta, UiView::Briefing, briefing_event);
                    break;
                case LauncherApp::Radio: {
                    cell = app_cell_in(grid, column, row, index, c().app_radio, ui::IconRole::Audio,
                                       ui::tokens::magenta, UiView::Radio);
                    const bool ready = callbacks.on_radio_play && callbacks.on_radio_stop && callbacks.on_radio_search;
                    ui::set_button_disabled(cell, !launcher_reordering && !ready);
                    break;
                }
                case LauncherApp::Notifications:
                    cell = app_cell_in(grid, column, row, index, v().notifications, ui::IconRole::Notification,
                                       ui::tokens::indigo, UiView::Notifications);
                    if (!launcher_reordering && notification_total() > 0) {
                        launcher_unread_mark(cell, notification_total(), ui::tokens::pink);
                    }
                    break;
                case LauncherApp::Settings:
                    cell = app_cell_in(grid, column, row, index, t().settings_title, ui::IconRole::Settings,
                                       ui::tokens::blue, UiView::Settings);
                    break;
                case LauncherApp::Organizer:
                    cell = app_cell_in(grid, column, row, index, c().app_organizer, ui::IconRole::Calendar,
                                       ui::tokens::indigo, UiView::Calendar);
                    break;
                case LauncherApp::Count: break;
            }
            (void)cell;
        }
        const int rows = static_cast<int>((launcher_layout.order.size() + 2U) / 3U);
        const auto layout = launcher_grid();
        list_end(grid, layout.top + rows * layout.pitch, 8);
    }

    static std::uintptr_t organizer_value(OrganizerKind kind, std::uint32_t id) {
        return (static_cast<std::uintptr_t>(kind) << 30U) | id;
    }

    static OrganizerKind organizer_kind_from(std::uintptr_t value) {
        return static_cast<OrganizerKind>((value >> 30U) & 0x3U);
    }

    static std::uint32_t organizer_id_from(std::uintptr_t value) {
        return static_cast<std::uint32_t>(value & 0x3FFFFFFFU);
    }

    static void organizer_tab_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->organizer_kind = static_cast<OrganizerKind>(user_value(event));
        self->render(UiView::Calendar);
    }

    static void organizer_new_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->organizer_step = 0;
        self->organizer_draft.clear();
        // Every new event starts from today: an earlier "Tomorrow" never sticks.
        self->organizer_day_offset = 0;
        if (self->clock.clock_valid) {
            const auto now = self->clock.now_minute;
            const auto start = organizer_default_start(now);
            const auto when = from_minute_index(start);
            self->organizer_day_offset = static_cast<int>(start / 1440 - now / 1440);
            self->organizer_hour = when.hour;
            self->organizer_minute = when.minute;
        }
        self->navigate(UiView::OrganizerNew);
    }

    static void organizer_text_event(lv_event_t* event) {
        auto* self = self_of(event);
        const char* text = lv_textarea_get_text(lv_event_get_current_target_obj(event));
        self->organizer_draft = text ? text : "";
    }

    static void organizer_save_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        const auto clean = sanitize_text(self->organizer_draft, OrganizerModel::kMaxTextBytes, true);
        if (clean.empty()) return;
        if (self->organizer_kind == OrganizerKind::Event && self->organizer_step == 0) {
            if (!self->clock.clock_valid) {
                self->show_toast(self->c().organizer_no_time, ui::tokens::coral);
                return;
            }
            self->organizer_step = 1;
            self->render(UiView::OrganizerNew);
            return;
        }
        bool saved = false;
        if (self->organizer_kind == OrganizerKind::Event && self->callbacks.on_organizer_event_add) {
            const auto day = self->clock.now_minute / 1440 + self->organizer_day_offset;
            const auto start = day * 1440 + self->organizer_hour * 60 + self->organizer_minute;
            if (start < self->clock.now_minute) {
                self->show_toast(self->c().organizer_past_time, ui::tokens::coral);
                return;
            }
            saved = self->callbacks.on_organizer_event_add(clean, start, 60);
        } else if (self->organizer_kind == OrganizerKind::Task && self->callbacks.on_organizer_task_add) {
            saved = self->callbacks.on_organizer_task_add(clean);
        } else if (self->organizer_kind == OrganizerKind::Shopping && self->callbacks.on_organizer_shopping_add) {
            saved = self->callbacks.on_organizer_shopping_add(clean);
        }
        if (saved) {
            self->organizer_draft.clear();
            self->organizer_step = 0;
            self->return_to(UiView::Calendar);
            self->show_toast(self->c().organizer_saved, ui::tokens::mint);
        } else {
            self->show_toast(self->c().organizer_full, ui::tokens::coral);
        }
    }

    static void organizer_day_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->organizer_day_offset = static_cast<int>(user_value(event));
        self->render(UiView::OrganizerNew);
    }

    static void organizer_toggle_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_organizer_toggle) return;
        const auto value = user_value(event);
        self->callbacks.on_organizer_toggle(organizer_kind_from(value), organizer_id_from(value));
    }

    static void organizer_remove_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action() || !self->callbacks.on_organizer_remove) return;
        const auto value = user_value(event);
        self->callbacks.on_organizer_remove(organizer_kind_from(value), organizer_id_from(value));
    }

    const char* radio_status_text() const {
        switch (radio.state) {
            case RadioState::Connecting: return c().radio_connecting;
            case RadioState::Playing: return radio.title.empty() ? c().radio_playing : radio.title.c_str();
            case RadioState::Error: return c().radio_stream_error;
            case RadioState::Idle: break;
        }
        return c().radio_stopped;
    }

    void render_radio_station_rows(lv_obj_t* list, const std::vector<RadioStation>& stations,
                                   int& y, bool results) {
        constexpr std::uintptr_t kResult = 1U << 16U;
        for (std::size_t index = 0; index < stations.size(); ++index) {
            const auto& station = stations[index];
            char detail[48];
            const char* subtitle = nullptr;
            if (station.bitrate > 0) {
                std::snprintf(detail, sizeof(detail), c().radio_kbps_format, station.bitrate);
                subtitle = detail;
            }
            const bool reorder = !results && radio_reordering && callbacks.on_radio_move;
            auto* row = list_row(list, y, station.name.c_str(), subtitle, ui::IconRole::Audio,
                                 icon_accent(index), kRowHeight, reorder ? 218 : 0);
            if (reorder) {
                constexpr std::uintptr_t kDown = 1U << 16U;
                auto* up = round_button(row, 326, 24, 40, ui::IconRole::ChevronLeft, ui::tokens::ice,
                                        radio_move_event);
                auto* down = round_button(row, 374, 24, 40, ui::IconRole::ChevronLeft, ui::tokens::lilac,
                                          radio_move_event);
                set_value(up, index);
                set_value(down, kDown | index);
                if (auto* icon = lv_obj_get_child(up, 0)) lv_image_set_rotation(icon, 900);
                if (auto* icon = lv_obj_get_child(down, 0)) lv_image_set_rotation(icon, 2700);
                ui::set_button_disabled(up, index == 0);
                ui::set_button_disabled(down, index + 1 == stations.size());
            } else {
                add_chevron(row);
                set_value(row, (results ? kResult : 0U) | index);
                lv_obj_set_clickable(row, true);
                lv_obj_add_event_cb(row, radio_station_event, LV_EVENT_CLICKED, this);
            }
            y += kRowPitch;
        }
    }

    void render_radio() {
        make_header(c().radio_title, ui::IconRole::Search, radio_search_open_event, ui::tokens::blue,
                    static_cast<bool>(callbacks.on_radio_search));
        auto* list = make_list();
        int y = 8;
        if (!radio.station.id.empty()) {
            auto* current = list_row(list, y, radio.station.name.c_str(), radio_status_text(), ui::IconRole::Audio,
                                     radio.state == RadioState::Error ? ui::tokens::coral : ui::tokens::magenta,
                                     104, 300);
            if (radio.state == RadioState::Playing) {
                auto* playing = make_dot(current, 12, ui::tokens::mint);
                lv_obj_align(playing, LV_ALIGN_RIGHT_MID, -4, 0);
            }
            y += 116;
            auto* stop = pill(list, c().radio_stop, ui::ButtonTone::Secondary, kGutter, y, kHalfWidth,
                              radio_stop_event);
            ui::set_button_disabled(stop, radio.state == RadioState::Idle || !callbacks.on_radio_stop);
            const bool favorite = std::any_of(radio.favorites.begin(), radio.favorites.end(), [&](const auto& item) {
                return item.id == radio.station.id;
            });
            auto* fav = pill(list, favorite ? c().radio_remove_favorite : c().radio_add_favorite,
                             ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, y, kHalfWidth,
                             radio_favorite_event);
            ui::set_button_disabled(fav, !callbacks.on_radio_favorite);
            y += kButtonHeight + 20;
        }
        line(list, c().radio_favorites, kGutter + 4, y, kWidth - 8, ui::font_caption(),
             ui::tokens::text_secondary);
        y += 40;
        if (radio.favorites.size() > 1 && callbacks.on_radio_move) {
            pill(list, radio_reordering ? c().radio_reorder_done : c().radio_reorder,
                 ui::ButtonTone::Secondary, kGutter, y, kWidth, radio_reorder_toggle_event);
            y += kButtonHeight + 12;
        }
        render_radio_station_rows(list, radio.favorites, y, false);
        list_end(list, y);
    }

    void render_radio_search() {
        if (radio_typing) {
            make_header(c().radio_search, ui::IconRole::Search, radio_search_submit_event, ui::tokens::blue,
                        static_cast<bool>(callbacks.on_radio_search));
            radio_search_area = make_text_input(kGutter, 100, kWidth, 72, c().radio_search_placeholder, true);
            lv_textarea_set_max_length(radio_search_area, 60);
            if (!radio_query_draft.empty()) lv_textarea_set_text(radio_search_area, radio_query_draft.c_str());
            lv_obj_add_event_cb(radio_search_area, radio_query_changed_event, LV_EVENT_VALUE_CHANGED, this);
            lv_obj_add_event_cb(radio_search_area, radio_search_ready_event, LV_EVENT_READY, this);
            make_form_keyboard(radio_search_area, ui::KeyboardProfile::Text, 184);
            return;
        }
        make_header(c().radio_search, ui::IconRole::Search, radio_search_open_event, ui::tokens::blue,
                    static_cast<bool>(callbacks.on_radio_search));
        if (radio.searching) {
            auto* spinner = spinner_arc(content, 96, ui::tokens::blue, 6);
            lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -16);
            state_text(300, c().radio_search, radio.query.c_str());
            return;
        }
        if (!radio.search_error.empty()) {
            state_symbol(72, 128, ui::IconRole::Offline, ui::tokens::coral);
            // The internal code is for developers; the owner reads what to do.
            state_text(228, c().radio_directory_error,
                       developer_mode ? radio.search_error.c_str() : c().radio_search_retry);
            return;
        }
        if (radio.results.empty()) {
            state_symbol(72, 132, ui::IconRole::Search, ui::tokens::text_tertiary);
            state_text(236, c().radio_no_results, radio.query.empty() ? nullptr : radio.query.c_str());
            return;
        }
        auto* list = make_list();
        int y = 8;
        line(list, c().radio_results, kGutter + 4, y, kWidth - 8, ui::font_caption(),
             ui::tokens::text_secondary);
        y += 40;
        render_radio_station_rows(list, radio.results, y, true);
        list_end(list, y);
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
                if (action.in_minutes == 1) {
                    std::snprintf(text, sizeof(text), "%s", c().action_in_one_minute);
                } else if (action.in_minutes > 0) {
                    std::snprintf(text, sizeof(text), c().action_in_minutes_format, action.in_minutes);
                } else {
                    std::snprintf(text, sizeof(text), c().action_at_format, action.tomorrow ? c().tomorrow : c().today,
                                  action.hour, action.minute);
                }
                return {ui::IconRole::BellRing, ui::tokens::magenta, action.text, text};
            }
            case AssistantActionType::Note:
                return {ui::IconRole::Notebook, ui::tokens::pink, c().action_note_title, action.text};
            case AssistantActionType::Task:
                return {ui::IconRole::Confirmation, ui::tokens::mint, c().action_task_title, action.text};
            case AssistantActionType::Shopping:
                return {ui::IconRole::List, ui::tokens::amber, c().action_shopping_title, action.text};
            case AssistantActionType::CalendarEvent: {
                const auto when = from_minute_index(action.start_minute);
                std::snprintf(text, sizeof(text), "%02d/%02d · %02d:%02d", when.day, when.month,
                              when.hour, when.minute);
                return {ui::IconRole::Calendar, ui::tokens::blue, action.text, text};
            }
            case AssistantActionType::Volume:
                std::snprintf(text, sizeof(text), action.relative ? "%+d%%" : "%d%%", action.value);
                return {ui::IconRole::VolumeUp, ui::tokens::mint, c().action_volume_title, text};
            case AssistantActionType::Brightness:
                std::snprintf(text, sizeof(text), "%d%%", action.value);
                return {ui::IconRole::Brightness, ui::tokens::amber, c().action_brightness_title, text};
            case AssistantActionType::Mute:
                return {action.value != 0 ? ui::IconRole::Muted : ui::IconRole::VolumeUp,
                        ui::tokens::lilac,
                        action.value != 0 ? c().action_mute_title : c().action_unmute_title, {}};
            case AssistantActionType::Message:
            case AssistantActionType::None: break;
        }
        return {ui::IconRole::Sparkles, ui::tokens::lilac, c().action_question, {}};
    }

    // The final answer, or the streamed part of it while it still arrives.
    const char* shown_answer() const {
        if (!snapshot.answer_text.empty()) return snapshot.answer_text.c_str();
        if (snapshot.state == AssistantState::Responding && !snapshot.partial_text.empty()) {
            return snapshot.partial_text.c_str();
        }
        return t().no_answer_yet;
    }

    // A streamed delta changes only the answer text: it is set on the shown
    // label (the speaking screen shows no text) instead of rebuilding the screen.
    void update_streamed_answer() {
        if (answer_label) lv_label_set_text(answer_label, shown_answer());
        else if (audio.state != AudioState::Playing && audio.state != AudioState::Paused) render(UiView::Assistant);
    }

    bool can_speak_answer() const {
        return capabilities.speech_output && callbacks.on_speak && !snapshot.answer_text.empty();
    }

    void render_answer_companion() {
        if (can_speak_answer()) make_header(t().answer_title, ui::IconRole::VolumeUp, speak_answer_event, ui::tokens::mint);
        else make_header(t().answer_title);
        line(content, snapshot.user_text.empty() ? t().ask_example : snapshot.user_text.c_str(),
             kGutter + 4, 100, kWidth - 8, ui::font_caption(), ui::tokens::text_tertiary);
        answer_body(132, 236);
    }

    // The answer in a scroller at `y`, then the actions; a pending action takes
    // 100 px of the reader for its card.
    void answer_body(int y, int height) {
        const bool confirming = snapshot.state == AssistantState::ActionConfirmation && extras.pending_action;
        auto* reader = lv_obj_create(content);
        ui::reset(reader);
        lv_obj_set_size(reader, kWidth, confirming ? height - 100 : height);
        lv_obj_set_pos(reader, kGutter, y);
        lv_obj_set_clickable(reader, true);
        lv_obj_set_scrollable(reader, true);
        lv_obj_set_scroll_dir(reader, LV_DIR_VER);
        lv_obj_set_overflow_visible(reader, false);
        ui::style_scrollbar(reader);
        primary_scroller = reader;
        answer_label = ui::make_label(reader, shown_answer(), ui::tokens::text, ui::font_body());
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
            pill(content, c().conversation_history, ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, kActionY,
                 kHalfWidth, conversation_history_event);
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
            std::snprintf(number, sizeof(number), "%u", static_cast<unsigned>(minutes == 60U ? 1U : minutes));
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
            if (appearance == Appearance::Marble) {
                render_timer_marble(*timer);
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
                if (timer.id != selected_clock_id) continue;
                const auto text = countdown_text(timer.remaining_ms);
                set_text_if_changed(clock_item_label, text.c_str());
                if (clock_item_arc) {
                    lv_arc_set_value(clock_item_arc, timer_permille(timer));
                    place_timer_digits(text);
                }
            }
        }
    }

    // ---- Calendar, tasks and shopping --------------------------------------

    void organizer_delete_button(lv_obj_t* row, OrganizerKind kind, std::uint32_t id) {
        auto* remove = round_button(row, 356, 16, 56, ui::IconRole::Delete, ui::tokens::coral,
                                    organizer_remove_event, true);
        set_value(remove, organizer_value(kind, id));
    }

    int conversation_bubble(lv_obj_t* list, int y, const char* speaker, const std::string& text,
                            std::uint32_t accent, bool mine) {
        auto* card = lv_obj_create(list);
        ui::reset(card);
        ui::style_platter(card, accent, ui::tokens::radius_large, false);
        lv_obj_set_size(card, 384, LV_SIZE_CONTENT);
        lv_obj_set_pos(card, mine ? 80 : kGutter, y);
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(card, 16, 0);
        lv_obj_set_style_pad_row(card, 8, 0);
        auto* who = ui::make_label(card, speaker, accent, ui::font_caption());
        lv_obj_set_width(who, 352);
        auto* body = ui::make_label(card, text.c_str(), ui::tokens::text, ui::font_body());
        lv_obj_set_width(body, 352);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_update_layout(card);
        return y + lv_obj_get_height(card) + 12;
    }

    void render_conversation_history() {
        make_header(c().conversation_history, ui::IconRole::Delete, conversation_history_clear_event,
                    ui::tokens::coral, !extras.conversation_history.empty() &&
                    static_cast<bool>(callbacks.on_conversation_clear));
        if (extras.conversation_history.empty()) {
            state_symbol(112, 128, ui::IconRole::Chat, ui::tokens::lilac);
            state_text(264, c().conversation_empty, nullptr);
            pill(content, c().conversation_ask, ui::ButtonTone::Primary, kGutter, kActionY, kWidth, ask_event);
            return;
        }
        auto* list = make_list();
        int y = 8;
        for (const auto& turn : extras.conversation_history) {
            y = conversation_bubble(list, y, c().conversation_you, turn.question, ui::tokens::blue, true);
            y = conversation_bubble(list, y, c().conversation_aura, turn.answer, ui::tokens::lilac, false);
        }
        y += 4;
        pill(list, c().conversation_ask, ui::ButtonTone::Primary, kGutter, y, kHalfWidth, ask_event);
        pill(list, c().new_conversation, ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, y,
             kHalfWidth, conversation_clear_event);
        list_end(list, y + kButtonHeight, 20);
        lv_obj_update_layout(list);
        lv_obj_scroll_to_y(list, lv_obj_get_scroll_bottom(list), LV_ANIM_OFF);
    }

    void render_organizer() {
        make_header(c().app_organizer, ui::IconRole::Plus, organizer_new_event, ui::tokens::blue);
        struct Tab { OrganizerKind kind; const char* title; };
        const Tab tabs[]{{OrganizerKind::Event, c().organizer_events},
                         {OrganizerKind::Task, c().organizer_tasks},
                         {OrganizerKind::Shopping, c().organizer_shopping}};
        for (int index = 0; index < 3; ++index) {
            // Compact tabs end at y 132, above the list that starts at 140.
            auto* tab = pill(content, tabs[index].title,
                             organizer_kind == tabs[index].kind ? ui::ButtonTone::Primary : ui::ButtonTone::Secondary,
                             kGutter + index * 144, 76, 136, organizer_tab_event);
            lv_obj_set_height(tab, 56);
            if (auto* tab_label = lv_obj_get_child(tab, 0)) {
                lv_obj_set_style_text_font(tab_label, ui::font_caption(), 0);
            }
            set_value(tab, static_cast<std::uintptr_t>(tabs[index].kind));
        }

        auto* list = make_list(140, 328);
        int y = 0;
        if (organizer_kind == OrganizerKind::Event) {
            for (const auto& event : organizer.events) {
                if (clock.clock_valid && event.start_minute + event.duration_minutes < clock.now_minute) continue;
                const auto when = when_text(event.start_minute);
                // "45 λεπτά", "1 h": short enough beside the delete button.
                const auto minutes = static_cast<unsigned>(event.duration_minutes);
                char duration[32];
                if (minutes >= 60U && minutes % 60U == 0U) {
                    std::snprintf(duration, sizeof(duration), "%u %s", minutes / 60U,
                                  minutes == 60U ? c().unit_hour_short : c().unit_hours_short);
                } else {
                    std::snprintf(duration, sizeof(duration), "%u %s", minutes, c().unit_minutes_short);
                }
                char detail[128];
                std::snprintf(detail, sizeof(detail), "%s · %s", when.c_str(), duration);
                auto* row = list_row(list, y, event.title.c_str(), detail, ui::IconRole::Calendar,
                                     ui::tokens::indigo, kRowHeight, 248);
                organizer_delete_button(row, OrganizerKind::Event, event.id);
                y += kRowPitch;
            }
        } else if (organizer_kind == OrganizerKind::Task) {
            for (const auto& item : organizer.tasks) {
                auto* row = list_row(list, y, item.text.c_str(),
                                     item.completed ? c().done : c().organizer_tap_complete,
                                     item.completed ? ui::IconRole::Check : ui::IconRole::List,
                                     item.completed ? ui::tokens::mint : ui::tokens::ice,
                                     kRowHeight, 248);
                tappable(row, organizer_toggle_event, organizer_value(OrganizerKind::Task, item.id));
                organizer_delete_button(row, OrganizerKind::Task, item.id);
                y += kRowPitch;
            }
        } else {
            for (const auto& item : organizer.shopping) {
                auto* row = list_row(list, y, item.text.c_str(),
                                     item.checked ? c().done : c().organizer_tap_complete,
                                     item.checked ? ui::IconRole::Check : ui::IconRole::List,
                                     item.checked ? ui::tokens::mint : ui::tokens::amber,
                                     kRowHeight, 248);
                tappable(row, organizer_toggle_event, organizer_value(OrganizerKind::Shopping, item.id));
                organizer_delete_button(row, OrganizerKind::Shopping, item.id);
                y += kRowPitch;
            }
        }
        if (y == 0) {
            const char* empty = organizer_kind == OrganizerKind::Event ? c().organizer_events_empty :
                                (organizer_kind == OrganizerKind::Task ? c().organizer_tasks_empty :
                                                                        c().organizer_shopping_empty);
            line(list, empty, 24, 116, kWidth - 48, ui::font_body(), ui::tokens::text_secondary,
                 LV_TEXT_ALIGN_CENTER);
            y = 260;
        }
        list_end(list, y);
    }

    void render_organizer_new() {
        const char* title = organizer_kind == OrganizerKind::Event ? c().organizer_new_event :
                            (organizer_kind == OrganizerKind::Task ? c().organizer_new_task :
                                                                    c().organizer_new_shopping);
        if (organizer_kind != OrganizerKind::Event || organizer_step == 0) {
            make_header(title, ui::IconRole::Check, organizer_save_event, ui::tokens::blue);
            const char* placeholder = organizer_kind == OrganizerKind::Event ? c().organizer_event_placeholder :
                                      (organizer_kind == OrganizerKind::Task ? c().organizer_task_placeholder :
                                                                              c().organizer_shopping_placeholder);
            organizer_area = make_text_input(kGutter, 100, kWidth, 72, placeholder, true);
            if (!organizer_draft.empty()) lv_textarea_set_text(organizer_area, organizer_draft.c_str());
            lv_textarea_set_max_length(organizer_area, static_cast<std::uint32_t>(OrganizerModel::kMaxTextBytes));
            lv_obj_add_event_cb(organizer_area, organizer_text_event, LV_EVENT_VALUE_CHANGED, this);
            lv_obj_add_event_cb(organizer_area, organizer_save_event, LV_EVENT_READY, this);
            make_form_keyboard(organizer_area, ui::KeyboardProfile::Text, 184);
            return;
        }
        make_header(c().organizer_choose_time);
        make_roller(24, organizer_hour, 72, 2);
        make_roller(60, organizer_minute, 256, 3);
        label(content, ":", 224, 150, 32, ui::font_title(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        auto* today = pill(content, c().today,
                           organizer_day_offset == 0 ? ui::ButtonTone::Primary : ui::ButtonTone::Secondary,
                           kGutter, 300, kHalfWidth, organizer_day_event);
        set_value(today, 0);
        auto* tomorrow = pill(content, c().tomorrow,
                              organizer_day_offset == 1 ? ui::ButtonTone::Primary : ui::ButtonTone::Secondary,
                              kGutter + kHalfWidth + 16, 300, kHalfWidth, organizer_day_event);
        set_value(tomorrow, 1);
        pill(content, c().save, ui::ButtonTone::Primary, kGutter, kActionY, kWidth, organizer_save_event);
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
            // Every kept note (at most NotesModel::kMaxNotes) has a row: an older
            // note must stay reachable to be read or deleted.
            auto* list = make_list(kHeader, kActionY - 4 - kHeader);
            int y = 0;
            for (const auto& note : notes.notes) {
                const bool voice_note = !note.audio_file.empty();
                const std::string title = note.text.empty() ? std::string(c().voice_note_title) : first_line(note.text);
                std::string detail = note_stamp(note.created_minute);
                if (voice_note) {
                    char length[16];
                    std::snprintf(length, sizeof(length), "%u:%02u", static_cast<unsigned>(note.audio_ms / 60000U),
                                  static_cast<unsigned>((note.audio_ms / 1000U) % 60U));
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
        make_header(voice_note ? c().voice_note_header : c().app_notes, ui::IconRole::Delete, note_delete_event,
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
        std::snprintf(elapsed, sizeof(elapsed), "%u:%02u", static_cast<unsigned>(voice.elapsed_ms / 60000U),
                      static_cast<unsigned>((voice.elapsed_ms / 1000U) % 60U));
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
            std::snprintf(elapsed, sizeof(elapsed), "%u:%02u", static_cast<unsigned>(voice.elapsed_ms / 60000U),
                          static_cast<unsigned>((voice.elapsed_ms / 1000U) % 60U));
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
        bool interactive{true};
    };

    Briefing current_briefing() const {
        BriefingInput input;
        input.status = &status;
        input.weather = &weather;
        input.fuel = &fuel;
        input.clock = &clock;
        input.unread_messages = static_cast<int>(aoraton.notifications.size()) + snapshot.notification_count;
        input.phone_notifications = static_cast<int>(phone.notifications.size());
        input.language = interface_language;
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
                case BriefingItemKind::Namedays:
                    lines.push_back({ui::IconRole::Sparkles, ui::tokens::lilac, c().briefing_namedays_title,
                                     item.text, UiView::Briefing, false});
                    break;
                case BriefingItemKind::NextHoliday:
                    std::snprintf(title, sizeof(title), "%s", c().briefing_next_holiday_format);
                    if (item.value == 0) {
                        std::snprintf(detail, sizeof(detail), c().briefing_holiday_today, item.text.c_str());
                    } else {
                        std::snprintf(detail, sizeof(detail), c().briefing_holiday_date_format,
                                      item.text.c_str(), item.date_day, item.date_month);
                    }
                    lines.push_back({ui::IconRole::Calendar, ui::tokens::ice, title, detail,
                                     UiView::Briefing, false});
                    break;
                case BriefingItemKind::FuelPrices: {
                    const auto price = [this](int millieur) {
                        return fuel_price_text(millieur, c().decimal_separator);
                    };
                    std::snprintf(detail, sizeof(detail), c().briefing_fuel_prices_format,
                                  price(item.value).c_str(), price(item.high).c_str(), price(item.low).c_str());
                    lines.push_back({ui::IconRole::Droplet, ui::tokens::amber, c().briefing_fuel_title,
                                     detail, UiView::Briefing, false});
                    std::string period = item.text;
                    if (period.size() == 10 && period[4] == '-' && period[7] == '-') {
                        period = period.substr(8, 2) + "/" + period.substr(5, 2);
                    }
                    std::string badge;
                    if (item.demo) badge += " · DEMO";
                    if (item.rain_percent > 0) badge += c().briefing_fuel_stale;
                    std::snprintf(detail, sizeof(detail), c().briefing_fuel_source_date_format,
                                  period.c_str(), badge.c_str());
                    lines.push_back({ui::IconRole::Information, ui::tokens::ice, c().briefing_fuel_source,
                                     detail, UiView::Briefing, false});
                    break;
                }
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
            if (item.interactive) {
                add_chevron(row);
                tappable(row, briefing_line_event, static_cast<std::uintptr_t>(item.target));
            }
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
        const bool recording = voice.state == VoiceCaptureState::Recording && voice.purpose == VoicePurpose::Translate &&
                               translation_active_side() == side;
        const std::uint32_t accent = recording ? ui::tokens::coral :
            (side == TranslateSide::Mine ? ui::tokens::ice : ui::tokens::magenta);
        auto* card = lv_obj_create(parent);
        ui::reset(card);
        ui::style_platter(card, accent, ui::tokens::radius_large, false);
        lv_obj_set_size(card, kWidth, 188);
        lv_obj_set_pos(card, kGutter, top + 8);
        auto* language_tag = ui::make_tag(card, c().language_names[language], accent);
        lv_obj_set_pos(language_tag, 16, 14);
        auto* body = label(card, text.c_str(), 16, 52, 316, text.size() > 60 ? ui::font_body() :
                           ui::font_title(), placeholder ? ui::tokens::text_secondary : ui::tokens::text);
        lv_obj_set_height(body, 118);
        lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
        auto* mic = round_button(card, 344, 56, 72, recording ? ui::IconRole::Stop : ui::IconRole::Microphone,
                                 accent, translate_record_event);
        set_value(mic, side == TranslateSide::Mine ? 0U : 1U);
        ui::set_button_disabled(mic, !capabilities.microphone || !capabilities.speech_recognition ||
                                     !callbacks.on_translate_record);
        if (recording) {
            translate_ring = spinner_arc(card, 84, ui::tokens::coral, 4);
            lv_arc_set_angles(translate_ring, 0, 360);
            lv_obj_set_pos(translate_ring, 338, 50);
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
        auto* swap = pill(content, languages, ui::ButtonTone::Secondary, 124, 208, 232, translate_language_event);
        lv_obj_set_height(swap, ui::tokens::HEADER_BUTTON);
        ui::set_button_disabled(swap, !callbacks.on_translate_language);
    }

    // ---- Always-on clock, calls and phone ------------------------------------

    // Where the newest notification came from; DEMO data says so.
    std::string notification_source() const {
        if (!aoraton.notifications.empty()) return aoraton.notifications.back().room_alias;
        if (!phone.notifications.empty()) {
            const auto& newest = phone.notifications.front();
            return newest.demo ? newest.app + " · " + t().demo : newest.app;
        }
        return snapshot.notification_count > 0 ? std::string(t().demo) : std::string{};
    }

    // A row of items centred on the screen; the burn-in shift moves it too.
    lv_obj_t* centered_row(int y, int gap) {
        auto* row = lv_obj_create(content);
        ui::reset(row);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, gap, 0);
        lv_obj_align(row, LV_ALIGN_TOP_MID, display_power.shift_x, y + display_power.shift_y);
        return row;
    }

    // Ambient clock: weather, time, date, new messages and the next alarm on
    // black. Drawn once per minute (or when its data changes), never animated.
    void render_always_on() {
        header_action = true;
        const bool lit = display_power.mode != DisplayMode::Off;
        const int dx = display_power.shift_x;
        const int dy = display_power.shift_y;
        const auto& sky = weather.data;
        const bool has_weather = lit && weather.has_data && sky.valid();
        const std::uint32_t tint = has_weather && sky.is_day ? weather_condition_accent(sky.condition) :
                                                               ui::tokens::indigo;
        const int total = notification_total();
        const bool radio_on = radio.state == RadioState::Playing || radio.state == RadioState::Connecting;
        const auto alarm = next_alarm(clock);
        // The rows stack from the weather down; the whole block is centred on
        // the screen whichever rows are present.
        const int weather_height = has_weather ? 90 : 0;
        int block_height = weather_height + 118;  // clock and date
        if (total > 0) block_height += 62;
        if (radio_on) block_height += (total > 0 ? 40 : 62);
        if (alarm) block_height += (total > 0 || radio_on) ? 40 : 62;
        const int top = ui::tokens::display_height / 2 - block_height / 2;
        // A faint halo in the colour of the sky behind the clock: four thin
        // steps read as one soft glow, and only its core rises above black. It
        // stays within the clock block (260 px), so most of the AMOLED is dark.
        if (lit) {
            for (int size = 260; size >= 140; size -= 40) {
                auto* halo = make_dot(content, size, tint, 10);
                lv_obj_set_pos(halo, ui::tokens::display_width / 2 + dx - size / 2,
                               ui::tokens::display_height / 2 + dy - size / 2);
            }
        }
        // The whole screen wakes the device; that first touch does nothing else.
        auto* catcher = lv_button_create(content);
        ui::reset(catcher);
        lv_obj_set_size(catcher, ui::tokens::display_width, ui::tokens::display_height);
        lv_obj_set_pos(catcher, 0, 0);
        lv_obj_set_style_bg_opa(catcher, LV_OPA_TRANSP, 0);
        lv_obj_set_clickable(catcher, true);
        lv_obj_add_event_cb(catcher, wake_event, LV_EVENT_CLICKED, this);
        if (!lit) return;
        constexpr std::uint32_t kDim = 0x6F7A8C;
        constexpr std::uint32_t kFaint = 0x535D6D;
        if (has_weather) {
            auto* row = centered_row(top, 10);
            if (auto* icon = icon_view(row, weather_condition_icon(sky.condition), 34, tint)) {
                lv_obj_set_style_opa(icon, LV_OPA_70, 0);
            }
            char temperature[16];
            std::snprintf(temperature, sizeof(temperature), "%d°", whole_degrees(sky.temperature_c));
            ui::make_label(row, temperature, kDim, ui::font_title());
            auto* city = ui::make_label(row, sky.location.name.c_str(), kFaint, ui::font_caption());
            lv_label_set_long_mode(city, LV_LABEL_LONG_DOT);
            lv_obj_set_style_max_width(city, 200, 0);
            char range[48];
            std::snprintf(range, sizeof(range), "↑ %d°   ↓ %d°", whole_degrees(sky.daily_maximum_c),
                          whole_degrees(sky.daily_minimum_c));
            std::string detail = range;
            if (sky.origin == WeatherDataOrigin::Demo) {
                detail += std::string(" · ") + v().demo_badge;
            } else if (sky.origin == WeatherDataOrigin::Cache || sky.stale) {
                detail += std::string(" · ") + v().weather_cached;
            }
            line(content, detail.c_str(), kGutter + dx, top + 44 + dy, kWidth, ui::font_caption(), kFaint,
                 LV_TEXT_ALIGN_CENTER);
        }
        const int clock_y = top + weather_height;
        if (status.clock_valid) {
            char time[12];
            std::snprintf(time, sizeof(time), "%02d:%02d", status.hour, status.minute);
            label(content, time, dx, clock_y + dy, ui::tokens::display_width, ui::font_clock(), kDim,
                  LV_TEXT_ALIGN_CENTER);
        }
        const auto date = date_text();
        if (!date.empty()) {
            line(content, date.c_str(), kGutter + dx, clock_y + 92 + dy, kWidth, ui::font_caption(), kDim,
                 LV_TEXT_ALIGN_CENTER);
        }
        int y = clock_y + 150;
        if (total > 0) {
            auto* row = centered_row(y, 10);
            make_dot(row, 10, ui::tokens::pink, LV_OPA_80);
            char count[24];
            std::snprintf(count, sizeof(count), c().aod_new_format, total);
            std::string text = total == 1 ? c().aod_new_one : count;
            const auto source = notification_source();
            if (!source.empty()) text += " · " + source;
            auto* note = ui::make_label(row, text.c_str(), kDim, ui::font_caption());
            lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
            lv_obj_set_style_max_width(note, 340, 0);
            y += 40;
        }
        if (radio_on) {
            auto* row = centered_row(y, 8);
            icon_view(row, ui::IconRole::Audio, 24, ui::tokens::magenta);
            char now_playing[128];
            std::snprintf(now_playing, sizeof(now_playing), c().radio_aod_format, radio.station.name.c_str());
            auto* station = ui::make_label(row, now_playing, kDim, ui::font_caption());
            lv_label_set_long_mode(station, LV_LABEL_LONG_DOT);
            lv_obj_set_style_max_width(station, 330, 0);
            y += 40;
        }
        if (alarm) {
            auto* row = centered_row(y, 8);
            icon_view(row, ui::IconRole::Reminder, 24, kFaint);
            char time[8];
            std::snprintf(time, sizeof(time), "%02d:%02d", alarm->hour, alarm->minute);
            ui::make_label(row, time, kFaint, ui::font_caption());
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
        char brightness[24];
        std::snprintf(brightness, sizeof(brightness), "%d%%", settings.active_brightness_percent);
        row = list_row(list, kRowPitch * 4, t().brightness, brightness,
                       ui::IconRole::Brightness, ui::tokens::amber);
        add_chevron(row);
        tappable(row, display_setting_event, 4);
        list_end(list, kRowPitch * 5);
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
            case UiView::Calendar: render_organizer(); break;
            case UiView::OrganizerNew: render_organizer_new(); break;
            case UiView::Notes: render_notes(); break;
            case UiView::NoteNew: render_note_new(); break;
            case UiView::NoteDetail: render_note_detail(); break;
            case UiView::NoteRecord: render_note_record(); break;
            case UiView::Briefing: render_briefing(); break;
            case UiView::Translate: render_translate(); break;
            case UiView::ConversationHistory: render_conversation_history(); break;
            case UiView::AlwaysOn: render_always_on(); break;
            case UiView::PhoneCall: render_phone_call(); break;
            case UiView::PhoneNotification: render_phone_notification(); break;
            case UiView::PhoneLink: render_phone_link(); break;
            case UiView::DisplaySettings: render_display_settings(); break;
            case UiView::AppearanceSettings: render_appearance_settings(); break;
            case UiView::Update: render_update(); break;
            case UiView::Radio: render_radio(); break;
            case UiView::RadioSearch: render_radio_search(); break;
            default: break;
        }
    }

    // Back from a companion screen; returns true when it was handled here.
    bool companion_back() {
        if (current_view == UiView::Launcher && launcher_reordering) {
            launcher_reordering = false;
            launcher_selected_app.reset();
            render(UiView::Launcher);
            show_toast(c().app_layout_saved, ui::tokens::mint);
            return true;
        }
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
        if (current_view == UiView::OrganizerNew && organizer_kind == OrganizerKind::Event && organizer_step == 1) {
            organizer_step = 0;
            render(UiView::OrganizerNew);
            return true;
        }
        if (current_view == UiView::RadioSearch && radio_typing) {
            radio_typing = false;
            render(UiView::RadioSearch);
            return true;
        }
        return false;
    }

    // Full-screen moments carry no status chrome.
    static bool immersive(UiView view) {
        return view == UiView::AlwaysOn || view == UiView::Translate || view == UiView::ClockAlert ||
               view == UiView::PhoneCall || view == UiView::PowerOff || view == UiView::Boot;
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

    // Dimmed screens: a translucent black veil where nothing else dims them.
    // A device that lowers its own backlight gets no veil on top of that.
    void update_dim_overlay() {
        const bool dim = !capabilities.panel_brightness &&
                         (display_power.mode == DisplayMode::Dimmed ||
                          (display_power.mode == DisplayMode::Active && display_power.brightness_percent < 100));
        if (!dim && !dim_overlay) return;
        if (!dim_overlay) {
            dim_overlay = lv_obj_create(lv_layer_top());
            ui::reset(dim_overlay);
            lv_obj_set_size(dim_overlay, ui::tokens::display_width, ui::tokens::display_height);
            lv_obj_set_style_bg_color(dim_overlay, ui::color(ui::tokens::canvas), 0);
            lv_obj_set_clickable(dim_overlay, false);
        }
        const int darkness = std::clamp(255 - display_power.brightness_percent * 255 / 100, 0, 220);
        lv_obj_set_style_bg_opa(dim_overlay, static_cast<lv_opa_t>(darkness), 0);
        lv_obj_set_hidden(dim_overlay, !dim);
    }

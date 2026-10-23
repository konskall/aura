// Included at the end of aura_ui.cpp (namespace aura): the v13 "Companion"
// part of the AuraUi interface. Like the other apply_* methods, each one
// compares the sequence first and redraws only the views that show the data.

void AuraUi::apply_clock(const ClockSnapshot& snapshot) {
    auto& self = *impl_;
    if (snapshot.sequence == self.clock.sequence) return;
    const auto previous_alert = self.clock.alert;
    const auto signature = Impl::clock_structure(snapshot);
    const bool structural = signature != self.clock_signature;
    self.clock = snapshot;
    self.clock_signature = signature;
    const bool new_alert = snapshot.alert && (!previous_alert || previous_alert->id != snapshot.alert->id ||
                                              previous_alert->kind != snapshot.alert->kind);
    if (new_alert) {
        if (self.current_view != UiView::ClockAlert) self.navigation.navigate(UiView::ClockAlert);
        self.render(UiView::ClockAlert, true);
        return;
    }
    if (!snapshot.alert && self.current_view == UiView::ClockAlert) {
        self.render(self.navigation.back());
        return;
    }
    if (structural) {
        switch (self.current_view) {
            case UiView::Home:
            case UiView::Clock:
            case UiView::ClockItem:
            case UiView::Briefing:
            case UiView::AlwaysOn:
                self.render(self.current_view);
                return;
            default: break;
        }
    }
    self.update_clock_labels();
}

void AuraUi::apply_notes(const NotesSnapshot& snapshot) {
    auto& self = *impl_;
    if (snapshot.sequence == self.notes.sequence) return;
    self.notes = snapshot;
    if (self.current_view == UiView::Notes || self.current_view == UiView::NoteDetail) self.render(self.current_view);
}

void AuraUi::apply_voice(const VoiceSnapshot& snapshot) {
    auto& self = *impl_;
    if (snapshot.sequence == self.voice.sequence) return;
    const auto previous = self.voice;
    self.voice = snapshot;
    const bool structural = previous.state != snapshot.state || previous.purpose != snapshot.purpose ||
                            previous.error_code != snapshot.error_code;
    if (self.current_view == UiView::NoteRecord) {
        if (structural && snapshot.state == VoiceCaptureState::Idle) {
            self.return_to(UiView::Notes);
            if (!snapshot.error_code.empty()) self.show_toast(self.c().note_needs_voice, ui::tokens::coral);
        } else if (structural) {
            self.render(UiView::NoteRecord);
        } else {
            self.update_record_widgets();
        }
        return;
    }
    if (self.current_view == UiView::Translate && structural) self.render(UiView::Translate);
}

void AuraUi::apply_translation(const TranslationSnapshot& snapshot) {
    auto& self = *impl_;
    if (snapshot.sequence == self.translation.sequence) return;
    self.translation = snapshot;
    if (self.current_view == UiView::Translate) self.render(UiView::Translate);
}

void AuraUi::apply_phone(const PhoneSnapshot& snapshot) {
    auto& self = *impl_;
    if (snapshot.sequence == self.phone.sequence) return;
    const bool had_call = self.phone.incoming_call.has_value();
    const bool count_changed = snapshot.notifications.size() != self.phone.notifications.size();
    self.phone = snapshot;
    if (snapshot.incoming_call && !had_call) {
        if (self.current_view != UiView::PhoneCall) self.navigation.navigate(UiView::PhoneCall);
        self.render(UiView::PhoneCall, true);
        return;
    }
    if (!snapshot.incoming_call && self.current_view == UiView::PhoneCall) {
        self.render(self.navigation.back());
        return;
    }
    switch (self.current_view) {
        case UiView::PhoneCall:
        case UiView::PhoneLink:
        case UiView::PhoneNotification:
        case UiView::Notifications:
        case UiView::Settings:
            self.render(self.current_view);
            break;
        case UiView::Home:
        case UiView::Launcher:
        case UiView::Briefing:
        case UiView::AlwaysOn:
            if (count_changed) self.render(self.current_view);
            break;
        default: break;
    }
}

void AuraUi::apply_update(const UpdateSnapshot& snapshot) {
    auto& self = *impl_;
    if (snapshot.sequence == self.update.sequence) return;
    // Download progress only moves the bar: a redraw per percent would drop taps.
    const bool structural = snapshot.state != self.update.state || snapshot.error_code != self.update.error_code ||
                            snapshot.available.has_value() != self.update.available.has_value() ||
                            snapshot.source_configured != self.update.source_configured;
    self.update = snapshot;
    if (snapshot.state != UpdateState::Available) self.update_confirming = false;
    if (self.current_view == UiView::Update) {
        if (structural) self.render(UiView::Update);
        else self.update_progress_widgets();
    } else if (self.current_view == UiView::Settings && structural) {
        self.render(UiView::Settings);
    }
}

void AuraUi::apply_display(const DisplaySnapshot& snapshot) {
    auto& self = *impl_;
    if (snapshot.sequence == self.display_power.sequence) return;
    const auto previous = self.display_power;
    self.display_power = snapshot;
    self.update_dim_overlay();
    const bool dark = snapshot.mode == DisplayMode::AlwaysOn || snapshot.mode == DisplayMode::Off;
    if (dark) {
        if (self.current_view != UiView::AlwaysOn) self.aod_return_view = self.current_view;
        self.render(UiView::AlwaysOn);
        return;
    }
    const bool settings_changed = snapshot.settings.always_on != previous.settings.always_on ||
        snapshot.settings.raise_to_wake != previous.settings.raise_to_wake ||
        snapshot.settings.flip_to_mute != previous.settings.flip_to_mute ||
        snapshot.settings.timeout_ms != previous.settings.timeout_ms;
    if (self.current_view == UiView::AlwaysOn) {
        self.render(self.aod_return_view, true);
    } else if (settings_changed &&
               (self.current_view == UiView::DisplaySettings || self.current_view == UiView::Settings)) {
        self.render(self.current_view);
    }
    if (snapshot.muted_by_flip && !previous.muted_by_flip) {
        self.show_toast(self.c().muted_face_down, ui::tokens::amber);
    }
}

void AuraUi::apply_assistant_extras(const AssistantExtras& extras) {
    auto& self = *impl_;
    if (extras.sequence == self.extras.sequence) return;
    self.extras = extras;
    if (self.current_view == UiView::Assistant) self.render(UiView::Assistant);
}

void AuraUi::set_capabilities(const CompanionCapabilities& capabilities) {
    auto& self = *impl_;
    const bool changed = capabilities.microphone != self.capabilities.microphone ||
        capabilities.speech_recognition != self.capabilities.speech_recognition ||
        capabilities.speech_output != self.capabilities.speech_output || capabilities.imu != self.capabilities.imu ||
        capabilities.phone_link != self.capabilities.phone_link ||
        capabilities.wake_word != self.capabilities.wake_word;
    self.capabilities = capabilities;
    if (changed && self.current_view != UiView::AlwaysOn) self.render(self.current_view);
}

void AuraUi::open_briefing() {
    impl_->navigation.reset();
    impl_->navigate(UiView::Briefing);
}

void AuraUi::open_note(std::uint32_t id) {
    impl_->selected_note = id;
    impl_->navigate(UiView::NoteDetail);
}

void AuraUi::open_phone_notification(std::uint32_t uid) {
    impl_->selected_phone_uid = uid;
    impl_->navigate(UiView::PhoneNotification);
}

void AuraUi::show_message(const char* text, bool error) {
    if (!text || !*text) return;
    impl_->show_toast(text, error ? ui::tokens::coral : ui::tokens::mint);
}

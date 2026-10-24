#include "aura/aura_ui.hpp"

#include "aura/aura_theme.hpp"
#include "aura/briefing.hpp"
#include "aura/companion_strings.hpp"
#include "aura/text_util.hpp"
#include "aura/aura_icons.hpp"
#include "aura/aura_keyboard.hpp"
#include "aura/aura_mercury_view.hpp"
#include "aura/motion_model.hpp"
#include "aura/navigation_stack.hpp"
#include "aura/strings.hpp"
#include "aura/visual_strings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace aura {

namespace {

// Conversation bubbles are LVGL objects; on the device only the newest
// messages are built (the service keeps the bounded history).
constexpr std::size_t kConversationRenderLimit = 40;
constexpr std::uint32_t kToastMs = 2600;
constexpr std::uint32_t kForgetArmMs = 4000;

// v12 "Halo" geometry (docs/design/AURA_V12_HALO_DESIGN.md).
constexpr int kGutter = ui::tokens::GUTTER;
constexpr int kWidth = ui::tokens::CONTENT_WIDTH;
constexpr int kHeader = ui::tokens::HEADER_HEIGHT;
constexpr int kRowHeight = ui::tokens::ROW_HEIGHT;
constexpr int kRowPitch = ui::tokens::ROW_PITCH;
constexpr int kButtonHeight = ui::tokens::BUTTON_HEIGHT;
// The bottom action row holds one full-width pill or two half-width pills.
constexpr int kActionY = 380;
constexpr int kHalfWidth = (kWidth - 16) / 2;
constexpr int kKeyboardY = 208;
constexpr int kKeyboardHeight = 272;
// Icon wells and round buttons tint their accent at 22 %.
constexpr lv_opa_t kTint = 56;
constexpr int kHaloCenterX = ui::tokens::HERO_RECT.x + ui::tokens::HERO_RECT.width / 2;
constexpr int kHaloCenterY = ui::tokens::HERO_RECT.y + ui::tokens::HERO_RECT.height / 2;
constexpr int kMeterCenterY = 292;
// Centre of the weather hero halo inside its 190 px scene.
constexpr int kWeatherHaloCenterX = 94;
constexpr int kWeatherHaloCenterY = 79;

void set_text_if_changed(lv_obj_t* label, const char* text) {
    const char* current = lv_label_get_text(label);
    if (!current || std::strcmp(current, text) != 0) lv_label_set_text(label, text);
}

ui::IconRole wifi_bars_icon(int bars) {
    if (bars >= 3) return ui::IconRole::Wifi;
    return bars == 2 ? ui::IconRole::WifiMedium : ui::IconRole::WifiLow;
}

lv_obj_t* make_dot(lv_obj_t* parent, int size, std::uint32_t fill, lv_opa_t opacity = LV_OPA_COVER) {
    auto* dot = lv_obj_create(parent);
    ui::reset(dot);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, ui::color(fill), 0);
    lv_obj_set_style_bg_opa(dot, opacity, 0);
    lv_obj_set_scrollable(dot, false);
    return dot;
}

void set_target(lv_obj_t* object, UiView view) {
    lv_obj_set_user_data(object, reinterpret_cast<void*>(static_cast<std::intptr_t>(view)));
}

constexpr std::array<std::uint32_t, 6> kIconAccents{
    ui::tokens::blue, ui::tokens::indigo, ui::tokens::magenta,
    ui::tokens::mint, ui::tokens::amber, ui::tokens::pink
};

// C++20 (ESP-IDF builds with gnu++2b) deprecates mixing the LVGL part/state enums.
constexpr lv_style_selector_t kIndicatorChecked =
    static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | static_cast<lv_style_selector_t>(LV_STATE_CHECKED);

}  // namespace

struct AuraUi::Impl {
    lv_display_t* display{};
    lv_obj_t* screen{};
    lv_obj_t* content{};
    // Status chrome: Wi-Fi and battery on Home; the time (plus offline, low
    // battery and charging only when they apply) on every other screen.
    lv_obj_t* status_bar{};
    lv_obj_t* status_left{};
    lv_obj_t* status_right{};
    lv_obj_t* status_time{};
    lv_obj_t* status_network{};
    lv_obj_t* status_network_button{};
    lv_obj_t* status_bluetooth{};
    lv_obj_t* status_developer{};
    lv_obj_t* status_muted{};
    lv_obj_t* status_alert{};
    lv_obj_t* status_battery{};
    lv_obj_t* status_battery_glyph{};
    lv_obj_t* status_battery_fill{};
    lv_obj_t* status_charging{};
    lv_obj_t* header_back{};
    lv_obj_t* header_title{};
    bool header_action{false};
    lv_obj_t* home_clock{};
    lv_obj_t* home_date{};
    lv_obj_t* hero_image{};
    lv_obj_t* halo{};
    ui::MercuryState halo_state{ui::MercuryState::Idle};
    lv_obj_t* state_label{};
    lv_obj_t* answer_label{};
    lv_obj_t* audio_state_label{};
    lv_obj_t* audio_hint_label{};
    lv_obj_t* audio_progress_bar{};
    std::array<lv_obj_t*, 9> audio_meter{};
    lv_obj_t* compose_area{};
    lv_obj_t* compose_status_label{};
    lv_obj_t* weather_search_area{};
    lv_obj_t* weather_scroll{};
    lv_obj_t* weather_scene{};
    lv_obj_t* weather_halo{};
    lv_obj_t* weather_icon{};
    lv_obj_t* weather_moon_cutout{};
    lv_obj_t* weather_spinner{};
    std::array<lv_obj_t*, 12> weather_particles{};
    std::array<lv_obj_t*, 3> weather_cloud_layers{};
    std::array<lv_obj_t*, 3> weather_mist_layers{};
    std::array<lv_obj_t*, 6> weather_stars{};
    std::array<lv_obj_t*, kWeatherHourlyCapacity> weather_hour_cards{};
    std::array<lv_obj_t*, kWeatherDailyCapacity> weather_day_cards{};
    lv_obj_t* weather_detail_label{};
    lv_obj_t* host_metrics_label{};
    lv_obj_t* wifi_password_area{};
    lv_obj_t* wifi_password_status{};
    lv_obj_t* unlock_area{};
    lv_obj_t* unlock_status{};
    lv_obj_t* wifi_visibility_icon{};
    lv_obj_t* wifi_spinner{};
    lv_obj_t* wifi_forget_label{};
    // The main vertical scroller of the visible view; a same-view refresh
    // (new data) restores its position instead of jumping to the top.
    lv_obj_t* primary_scroller{};
    lv_obj_t* toast{};
    lv_obj_t* toast_label{};
    std::uint32_t toast_remaining_ms{0};

    AuraUiCallbacks callbacks{};
    AssistantSnapshot snapshot{};
    AudioDiagnostics audio{};
    AoratonSnapshot aoraton{};
    DeviceStatusSnapshot status{};
    WeatherSnapshot weather{};
    WifiSnapshot wifi{};
    std::string wifi_target_ssid;
    WifiSecurity wifi_target_security{WifiSecurity::Unknown};
    bool wifi_password_visible{false};
    bool wifi_forget_armed{false};
    std::uint32_t wifi_forget_armed_at{0};
    // Drafts survive data refreshes and navigation; the Wi-Fi password never
    // gets a draft.
    std::string weather_query_draft;
    std::string compose_draft;
    // The city picker shows presets first; the keyboard opens on request.
    bool weather_typing{false};
    NavigationStack<UiView> navigation{UiView::Home};
    MotionModel motion{};
    ui::AuraMercuryView mercury{};
    UiView current_view{UiView::Home};
    bool live_provider{false};
    bool developer_mode{false};
    // Where each list was scrolled when it was left; used only when going back.
    std::vector<std::pair<UiView, int32_t>> scroll_memory;
    bool restore_scroll{false};
    SoundSettings sound{};
    lv_obj_t* sound_volume_label{};
    SystemAction pending_system{SystemAction::Restart};
    bool powered_off{false};
    std::uint32_t boot_remaining_ms{0};
    lv_obj_t* boot_spinner{};
    bool reduced_motion{false};
    bool capture_mode{false};
    bool input_simulated{false};
    float input_amplitude{0.0F};
    float host_loop_hz{0.0F};
    float host_handler_average_ms{0.0F};
    float host_handler_max_ms{0.0F};
    std::uint32_t phase_ms{0};
    std::uint32_t animation_ms{0};
    std::uint32_t motion_sample_ms{0};
    std::uint64_t motion_sample_updates{0};
    float motion_update_rate{0.0F};
    std::uint64_t weather_motion_updates{0};
    std::uint64_t weather_forecast_interactions{0};
    std::uint64_t weather_motion_sample_updates{0};
    float weather_motion_update_rate{0.0F};
    std::uint32_t last_action_ms{0};
    bool action_seen{false};
    unsigned typography_page{0};

    const l10n::Strings& t() const { return l10n::el_gr(); }
    const l10n::VisualStrings& v() const { return l10n::visual_el_gr(); }

    bool accept_action() {
        if (action_seen && phase_ms - last_action_ms < 75U) return false;
        last_action_ms = phase_ms;
        action_seen = true;
        return true;
    }

    int notification_total() const {
        return snapshot.notification_count + static_cast<int>(aoraton.notifications.size()) +
               static_cast<int>(phone.notifications.size());
    }

    static void nav_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto raw = reinterpret_cast<std::intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event)));
        self->navigate(static_cast<UiView>(raw));
    }

    static void back_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->go_back();
    }

    void go_back() {
        if (companion_back()) return;
        // Back first closes the city keyboard, then leaves the picker.
        if (current_view == UiView::WeatherLocation && weather_typing) {
            weather_typing = false;
            render(UiView::WeatherLocation);
            return;
        }
        render_back(navigation.back());
    }

    // Long-pressing the back button goes straight home.
    static void home_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED) lv_indev_wait_release(lv_indev_active());
        if (self->callbacks.on_home) self->callbacks.on_home();
        self->navigation.reset();
        self->render(UiView::Home);
    }

    static void notifications_clear_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_notifications_clear) self->callbacks.on_notifications_clear();
    }

    static void ask_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_ask) self->callbacks.on_ask();
    }

    static void end_listening_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_end_listening) self->callbacks.on_end_listening();
    }

    static void cancel_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_cancel) self->callbacks.on_cancel();
    }

    static void retry_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_retry) self->callbacks.on_retry();
    }

    static void provider_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        // Without a provider adapter (the device today) there is nothing to
        // switch; the row must not claim a LIVE provider.
        if (!self->accept_action() || !self->callbacks.on_provider_changed) return;
        self->live_provider = !self->live_provider;
        self->callbacks.on_provider_changed(self->live_provider);
        self->render(UiView::Settings);
    }

    static void weather_open_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->navigate(UiView::Weather);
        if (self->callbacks.on_weather_open) self->callbacks.on_weather_open();
    }

    static void weather_refresh_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_weather_refresh) self->callbacks.on_weather_refresh();
    }

    static void weather_change_city_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->weather_typing = false;
        self->navigate(UiView::WeatherLocation);
    }

    static void weather_field_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->weather_typing = true;
        self->render(UiView::WeatherLocation);
    }

    static void weather_search_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action() || !self->weather_search_area) return;
        const char* query = lv_textarea_get_text(self->weather_search_area);
        if (!query || std::string_view(query).size() < 2) return;
        const std::string query_copy(query);
        if (self->callbacks.on_weather_search) self->callbacks.on_weather_search(query_copy);
    }

    static void weather_query_changed_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->weather_search_area) return;
        const char* text_value = lv_textarea_get_text(self->weather_search_area);
        self->weather_query_draft = text_value ? text_value : "";
    }

    static void weather_preset_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto* query = static_cast<const char*>(lv_obj_get_user_data(lv_event_get_current_target_obj(event)));
        if (!query || !*query) return;
        if (self->callbacks.on_weather_search) self->callbacks.on_weather_search(query);
    }

    static void weather_result_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto index = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(
            lv_obj_get_user_data(lv_event_get_current_target_obj(event))));
        if (index >= self->weather.suggestion_count) return;
        self->weather_typing = false;
        self->navigation.replace(UiView::Weather);
        self->render(UiView::Weather);
        if (self->callbacks.on_weather_select_result) self->callbacks.on_weather_select_result(index);
    }

    static void weather_hour_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto index = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(
            lv_obj_get_user_data(lv_event_get_current_target_obj(event))));
        ++self->weather_forecast_interactions;
        self->set_weather_detail(false, index);
    }

    static void weather_day_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto index = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(
            lv_obj_get_user_data(lv_event_get_current_target_obj(event))));
        ++self->weather_forecast_interactions;
        self->set_weather_detail(true, index);
    }

    static void motion_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->reduced_motion = !self->reduced_motion;
        self->motion.set_reduced(self->reduced_motion);
        self->render(UiView::Settings);
    }

    static void audio_test_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_audio_test) self->callbacks.on_audio_test();
    }

    static void audio_wav_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_audio_wav) self->callbacks.on_audio_wav();
    }

    static void audio_pause_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->audio.state == AudioState::Playing || self->audio.state == AudioState::Paused) {
            if (self->callbacks.on_audio_pause_resume) self->callbacks.on_audio_pause_resume();
        } else if (self->callbacks.on_audio_test) {
            self->callbacks.on_audio_test();
        }
    }

    static void audio_stop_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_audio_stop) self->callbacks.on_audio_stop();
    }

    static void audio_mute_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_audio_muted) self->callbacks.on_audio_muted(!self->audio.muted);
    }

    static void audio_volume_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        const int value = lv_slider_get_value(lv_event_get_target_obj(event));
        if (self->callbacks.on_audio_volume) self->callbacks.on_audio_volume(static_cast<float>(value) / 100.0F);
    }

    static void room_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto index = reinterpret_cast<std::intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event)));
        if (index < 0 || static_cast<std::size_t>(index) >= self->aoraton.rooms.size()) return;
        const auto& room = self->aoraton.rooms[static_cast<std::size_t>(index)];
        if (!self->aoraton.mock_mode && !room.key_available && self->aoraton.pairing == AoratonPairingState::Paired) {
            // A locked room opens the key screen instead of failing silently.
            self->navigate(UiView::AoratonUnlock);
            return;
        }
        const bool opened = self->callbacks.on_aoraton_room && self->callbacks.on_aoraton_room(room.id);
        if (!opened) {
            self->render(UiView::AoratonRooms);
            return;
        }
        self->aoraton.selected_room_id = room.id;
        self->navigate(UiView::AoratonConversation);
    }

    static void pairing_start_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_aoraton_pair_start && self->callbacks.on_aoraton_pair_start()) {
            self->navigate(UiView::AoratonPair);
        }
    }

    static void pairing_confirm_sas_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_aoraton_pair_confirm_sas) self->callbacks.on_aoraton_pair_confirm_sas();
    }

    static void pairing_cancel_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_aoraton_pair_cancel) self->callbacks.on_aoraton_pair_cancel();
        self->navigation.replace(UiView::AoratonRooms);
        self->render(UiView::AoratonRooms);
    }

    static void pairing_done_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->navigation.replace(UiView::AoratonRooms);
        self->render(UiView::AoratonRooms);
    }

    static void unpair_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_aoraton_unpair) self->callbacks.on_aoraton_unpair();
        self->navigation.replace(UiView::AoratonRooms);
        self->render(UiView::AoratonRooms);
    }

    static void unlock_submit_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->submit_unlock();
    }

    static void unlock_ready_event(lv_event_t* event) {
        lv_async_call([](void* data) {
            auto* self = static_cast<Impl*>(data);
            if (self->current_view == UiView::AoratonUnlock) self->submit_unlock();
        }, lv_event_get_user_data(event));
    }

    static void paste_invite_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_aoraton_paste_invite) self->callbacks.on_aoraton_paste_invite();
        self->render(UiView::AoratonPair);
    }

    void submit_unlock() {
        if (!unlock_area || !callbacks.on_aoraton_unlock) return;
        const char* typed = lv_textarea_get_text(unlock_area);
        if (!typed || !*typed) return;
        std::string passphrase(typed);
        const bool queued = callbacks.on_aoraton_unlock(passphrase);
        volatile char* bytes = passphrase.empty() ? nullptr : &passphrase[0];
        for (std::size_t index = 0; index < passphrase.size(); ++index) bytes[index] = 0;
        if (queued) lv_textarea_set_text(unlock_area, "");
        update_unlock_status();
    }

    static void aoraton_notification_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto index = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(
            lv_obj_get_user_data(lv_event_get_current_target_obj(event))));
        if (index >= self->aoraton.notifications.size()) return;
        const std::string room_id = self->aoraton.notifications[index].room_id;
        if (!self->callbacks.on_aoraton_room || !self->callbacks.on_aoraton_room(room_id)) {
            self->navigate(UiView::AoratonRooms);
            return;
        }
        self->aoraton.selected_room_id = room_id;
        self->navigate(UiView::AoratonConversation);
    }

    static void compose_changed_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self->compose_area) {
            const char* text_value = lv_textarea_get_text(self->compose_area);
            self->compose_draft = text_value ? text_value : "";
        }
        self->update_compose_status();
    }

    void update_compose_status() {
        if (!compose_status_label || !compose_area) return;
        const char* text_value = lv_textarea_get_text(compose_area);
        const std::size_t bytes = text_value ? std::char_traits<char>::length(text_value) : 0;
        char counter[48];
        std::snprintf(counter, sizeof(counter), t().compose_bytes_format,
                      static_cast<unsigned>(bytes), static_cast<unsigned>(aoraton.max_message_bytes));
        lv_label_set_text(compose_status_label, counter);
        lv_obj_set_style_text_color(compose_status_label,
            ui::color(bytes > aoraton.max_message_bytes ? ui::tokens::coral : ui::tokens::text_tertiary), 0);
    }

    static void compose_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->aoraton.selected_room_id.empty() && !self->aoraton.rooms.empty() && self->callbacks.on_aoraton_room) {
            self->aoraton.selected_room_id = self->aoraton.rooms.front().id;
            if (!self->callbacks.on_aoraton_room(self->aoraton.rooms.front().id)) return;
        }
        if (!self->aoraton.rooms.empty()) self->navigate(UiView::AoratonCompose);
    }

    static void prepare_message_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->prepare_message();
    }

    // The keyboard's OK key reports READY from inside its own event; the
    // resulting navigation would delete that keyboard, so it runs next cycle.
    static void compose_ready_event(lv_event_t* event) {
        lv_async_call([](void* data) { static_cast<Impl*>(data)->prepare_message(); },
                      lv_event_get_user_data(event));
    }

    static void weather_search_ready_event(lv_event_t* event) {
        lv_async_call([](void* data) {
            auto* self = static_cast<Impl*>(data);
            if (!self->weather_search_area) return;
            const char* query = lv_textarea_get_text(self->weather_search_area);
            if (!query || std::string_view(query).size() < 2 || !self->callbacks.on_weather_search) return;
            self->callbacks.on_weather_search(std::string(query));
        }, lv_event_get_user_data(event));
    }

    static void wifi_password_ready_event(lv_event_t* event) {
        lv_async_call([](void* data) {
            auto* self = static_cast<Impl*>(data);
            if (self->current_view == UiView::WifiPassword) self->submit_wifi();
        }, lv_event_get_user_data(event));
    }

    void prepare_message() {
        if (!compose_area || aoraton.selected_room_id.empty()) return;
        const char* text_value = lv_textarea_get_text(compose_area);
        if (text_value && callbacks.on_aoraton_prepare &&
            callbacks.on_aoraton_prepare(aoraton.selected_room_id, text_value)) {
            aoraton.pending_room_id = aoraton.selected_room_id;
            aoraton.pending_text = text_value;
            aoraton.awaiting_confirmation = true;
            navigate(UiView::AoratonConfirm);
            return;
        }
        // The service rejected the draft; say why instead of failing silently.
        const std::size_t bytes = text_value ? std::char_traits<char>::length(text_value) : 0;
        show_toast(bytes == 0 ? t().compose_empty :
                   (bytes > aoraton.max_message_bytes ? t().compose_too_long : t().compose_rejected),
                   ui::tokens::coral);
    }

    static void confirm_message_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_aoraton_confirm && self->callbacks.on_aoraton_confirm()) {
            self->aoraton.awaiting_confirmation = false;
            self->aoraton.pending_text.clear();
            self->compose_draft.clear();
            self->navigation.replace(UiView::AoratonConversation);
            self->render(UiView::AoratonConversation);
        }
    }

    static void cancel_message_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_aoraton_cancel) self->callbacks.on_aoraton_cancel();
        self->aoraton.awaiting_confirmation = false;
        self->aoraton.pending_text.clear();
        self->navigation.replace(UiView::AoratonConversation);
        self->render(UiView::AoratonConversation);
    }

    static void wifi_open_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->current_view != UiView::Wifi) self->navigate(UiView::Wifi);
        if (self->callbacks.on_wifi_open) self->callbacks.on_wifi_open();
    }

    static void wifi_scan_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_wifi_scan) self->callbacks.on_wifi_scan();
    }

    void open_wifi_password(const std::string& ssid, WifiSecurity security) {
        wifi_target_ssid = ssid;
        wifi_target_security = security;
        wifi_password_visible = false;
        navigate(UiView::WifiPassword);
    }

    static void wifi_network_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto index = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(
            lv_obj_get_user_data(lv_event_get_current_target_obj(event))));
        if (index >= self->wifi.network_count || self->wifi.link == WifiLinkState::Connecting) return;
        const auto network = self->wifi.networks[index];
        if (network.connected) {
            self->navigate(UiView::WifiDetails);
        } else if (!wifi_security_supported(network.security)) {
            self->show_toast(self->t().wifi_error_unsupported, ui::tokens::coral);
        } else {
            self->open_wifi_password(network.ssid, network.security);
        }
    }

    static void wifi_status_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        const auto& wifi = self->wifi;
        if (wifi.link == WifiLinkState::Connected) {
            self->navigate(UiView::WifiDetails);
        } else if (wifi.link == WifiLinkState::Disconnected && !wifi.connect_error.empty() &&
                   !wifi.target_ssid.empty()) {
            self->open_wifi_password(wifi.target_ssid, wifi.target_security);
        }
    }

    static void wifi_cancel_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        if (self->callbacks.on_wifi_cancel) self->callbacks.on_wifi_cancel();
    }

    static void wifi_connect_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->submit_wifi();
    }

    void submit_wifi() {
        std::string password;
        if (wifi_target_security != WifiSecurity::Open && wifi_password_area) {
            const char* typed = lv_textarea_get_text(wifi_password_area);
            password = typed ? typed : "";
        }
        // Validate locally first so the rule is explained before any request
        // leaves the UI.
        const char* problem = nullptr;
        switch (wifi_check_password(wifi_target_security, password)) {
            case WifiPasswordCheck::Ok:
            case WifiPasswordCheck::NotRequired: break;
            case WifiPasswordCheck::TooShort: problem = t().wifi_error_password_short; break;
            case WifiPasswordCheck::TooLong: problem = t().wifi_error_password_long; break;
            case WifiPasswordCheck::InvalidCharacters: problem = t().wifi_error_password_invalid; break;
            case WifiPasswordCheck::UnsupportedSecurity: problem = t().wifi_error_unsupported; break;
        }
        const bool started = !problem && callbacks.on_wifi_connect &&
            callbacks.on_wifi_connect(wifi_target_ssid, wifi_target_security, password);
        std::fill(password.begin(), password.end(), '\0');
        if (!started) {
            show_toast(problem ? problem : wifi_error_text(wifi.connect_error), ui::tokens::coral);
            return;
        }
        if (wifi_password_area) lv_textarea_set_text(wifi_password_area, "");
        render_back(navigation.back());
    }

    static void wifi_visibility_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action() || !self->wifi_password_area) return;
        self->wifi_password_visible = !self->wifi_password_visible;
        lv_textarea_set_password_mode(self->wifi_password_area, !self->wifi_password_visible);
        if (self->wifi_visibility_icon) {
            lv_image_set_src(self->wifi_visibility_icon, ui::icon_asset(
                self->wifi_password_visible ? ui::IconRole::VisibilityOff : ui::IconRole::Visibility));
        }
    }

    static void wifi_forget_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        // Two-step confirmation fits the small panel better than a dialog.
        if (!self->wifi_forget_armed) {
            self->wifi_forget_armed = true;
            self->wifi_forget_armed_at = self->phase_ms;
            if (self->wifi_forget_label) lv_label_set_text(self->wifi_forget_label, self->t().wifi_forget_confirm);
            return;
        }
        self->wifi_forget_armed = false;
        if (self->callbacks.on_wifi_forget) self->callbacks.on_wifi_forget();
        self->show_toast(self->t().wifi_toast_forgotten, ui::tokens::mint);
        self->render_back(self->navigation.back());
    }

    static void network_settings_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (!self->accept_action()) return;
        self->navigate(UiView::Wifi);
        if (self->callbacks.on_wifi_open) self->callbacks.on_wifi_open();
    }

    const char* wifi_error_text(const std::string& code) const {
        if (code == "WRONG_PASSWORD") return t().wifi_error_wrong_password;
        if (code == "NETWORK_NOT_FOUND") return t().wifi_error_not_found;
        if (code == "CONNECT_TIMEOUT") return t().wifi_error_timeout;
        if (code == "PASSWORD_TOO_SHORT") return t().wifi_error_password_short;
        if (code == "PASSWORD_TOO_LONG") return t().wifi_error_password_long;
        if (code == "PASSWORD_INVALID") return t().wifi_error_password_invalid;
        if (code == "UNSUPPORTED_SECURITY") return t().wifi_error_unsupported;
        if (code == "SCAN_FAILED") return t().wifi_error_scan;
        return t().wifi_error_generic;
    }

    void show_toast(const char* text, std::uint32_t accent) {
        if (powered_off || boot_remaining_ms > 0) return;
        if (!toast) {
            toast = lv_obj_create(screen);
            ui::reset(toast);
            lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_max_width(toast, kWidth, 0);
            lv_obj_set_style_bg_color(toast, ui::color(ui::tokens::surface_glass), 0);
            lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(toast, ui::tokens::radius_large, 0);
            lv_obj_set_style_border_width(toast, 2, 0);
            lv_obj_set_style_pad_hor(toast, 24, 0);
            lv_obj_set_style_pad_ver(toast, 14, 0);
            toast_label = ui::make_label(toast, "", ui::tokens::text, ui::font_body());
            lv_obj_set_style_max_width(toast_label, kWidth - 48, 0);
        }
        lv_label_set_text(toast_label, text);
        lv_obj_set_style_border_color(toast, ui::color(accent), 0);
        lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 16);
        lv_obj_set_hidden(toast, false);
        lv_obj_move_foreground(toast);
        toast_remaining_ms = capture_mode ? 0 : kToastMs;
        if (capture_mode) lv_obj_set_hidden(toast, true);
    }

    static void gesture_event(lv_event_t* event) {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        const lv_dir_t direction = lv_indev_get_gesture_dir(lv_indev_active());
        if (self->current_view == UiView::Home) {
            // Home: up or left opens the apps, down shows notifications.
            if (direction == LV_DIR_LEFT || direction == LV_DIR_TOP) {
                lv_indev_wait_release(lv_indev_active());
                self->navigate(UiView::Launcher);
            } else if (direction == LV_DIR_BOTTOM) {
                lv_indev_wait_release(lv_indev_active());
                self->navigate(UiView::Notifications);
            }
            return;
        }
        if (direction != LV_DIR_RIGHT) return;
        // Swipe right is "back" wherever the view has no horizontal control
        // of its own (strips, sliders, keyboards).
        switch (self->current_view) {
            case UiView::Launcher:
            case UiView::Settings:
            case UiView::Notifications:
            case UiView::Diagnostics:
            case UiView::Battery:
            case UiView::AoratonRooms:
            case UiView::AoratonPair:
            case UiView::AoratonConversation:
            case UiView::Wifi:
            case UiView::WifiDetails:
            case UiView::Cards:
            case UiView::Navigation:
            case UiView::Tools:
            case UiView::Calendar:
            case UiView::Camera:
            case UiView::VoiceSettings:
            case UiView::Clock:
            case UiView::ClockNew:
            case UiView::TimerNew:
            case UiView::ClockItem:
            case UiView::Notes:
            case UiView::NoteDetail:
            case UiView::Briefing:
            case UiView::PhoneNotification:
            case UiView::PhoneLink:
            case UiView::DisplaySettings:
            case UiView::Update:
            case UiView::System:
            case UiView::SystemConfirm:
                lv_indev_wait_release(lv_indev_active());
                self->render_back(self->navigation.back());
                break;
            default: break;
        }
    }

    const char* weather_condition_text(WeatherCondition condition) const {
        switch (condition) {
            case WeatherCondition::Clear: return v().weather_clear;
            case WeatherCondition::PartlyCloudy: return v().weather_partly_cloudy;
            case WeatherCondition::Cloudy: return v().weather_cloudy;
            case WeatherCondition::Fog: return v().weather_fog;
            case WeatherCondition::Drizzle: return v().weather_drizzle;
            case WeatherCondition::Rain: return v().weather_rain;
            case WeatherCondition::Snow: return v().weather_snow;
            case WeatherCondition::Thunderstorm: return v().weather_thunderstorm;
            case WeatherCondition::Windy: return v().weather_windy;
            case WeatherCondition::Unknown: return v().weather_unknown;
        }
        return v().weather_unknown;
    }

    ui::IconRole weather_condition_icon(WeatherCondition condition) const {
        switch (condition) {
            case WeatherCondition::Clear: return ui::IconRole::Sun;
            case WeatherCondition::PartlyCloudy: return ui::IconRole::Weather;
            case WeatherCondition::Cloudy:
            case WeatherCondition::Fog: return ui::IconRole::Cloud;
            case WeatherCondition::Drizzle: return ui::IconRole::CloudDrizzle;
            case WeatherCondition::Rain: return ui::IconRole::CloudRain;
            case WeatherCondition::Snow: return ui::IconRole::Snowflake;
            case WeatherCondition::Thunderstorm: return ui::IconRole::CloudLightning;
            case WeatherCondition::Windy: return ui::IconRole::Wind;
            case WeatherCondition::Unknown: return ui::IconRole::Weather;
        }
        return ui::IconRole::Weather;
    }

    std::uint32_t weather_condition_accent(WeatherCondition condition) const {
        switch (condition) {
            case WeatherCondition::Clear: return ui::tokens::amber;
            case WeatherCondition::PartlyCloudy: return ui::tokens::ice;
            case WeatherCondition::Cloudy:
            case WeatherCondition::Fog: return ui::tokens::text_secondary;
            case WeatherCondition::Drizzle:
            case WeatherCondition::Rain: return ui::tokens::blue;
            case WeatherCondition::Snow: return ui::tokens::ice;
            case WeatherCondition::Thunderstorm: return ui::tokens::lilac;
            case WeatherCondition::Windy: return ui::tokens::mint;
            case WeatherCondition::Unknown: return ui::tokens::text_tertiary;
        }
        return ui::tokens::blue;
    }

    std::string weather_time_label(const std::string& iso_time) const {
        const auto separator = iso_time.find('T');
        if (separator != std::string::npos && separator + 6 <= iso_time.size()) {
            return iso_time.substr(separator + 1, 5);
        }
        return iso_time.size() <= 5 ? iso_time : iso_time.substr(iso_time.size() - 5);
    }

    std::string weather_day_label(const std::string& iso_date, std::size_t index) const {
        if (index == 0) return v().weather_today;
        if (index == 1) return v().weather_tomorrow;
        if (iso_date.size() >= 10) return iso_date.substr(8, 2) + "/" + iso_date.substr(5, 2);
        return iso_date;
    }

    void set_weather_detail(bool daily, std::size_t index) {
        if (!weather_detail_label || !weather.has_data) return;
        for (auto* card : weather_hour_cards) {
            if (card) lv_obj_set_style_border_width(card, 0, 0);
        }
        for (auto* card : weather_day_cards) {
            if (card) lv_obj_set_style_border_width(card, 0, 0);
        }
        char detail[192];
        if (daily) {
            if (index >= weather.data.daily_count || index >= weather_day_cards.size()) return;
            const auto& item = weather.data.daily[index];
            std::snprintf(detail, sizeof(detail), "%s  ·  %.0f/%.0f°C  ·  %s %d%%",
                          weather_day_label(item.date, index).c_str(), item.minimum_c, item.maximum_c,
                          v().weather_rain, item.rain_probability_max_percent);
            lv_obj_set_style_border_width(weather_day_cards[index], 2, 0);
            lv_obj_set_style_border_color(weather_day_cards[index],
                ui::color(weather_condition_accent(item.condition)), 0);
        } else {
            if (index >= weather.data.hourly_count || index >= weather_hour_cards.size()) return;
            const auto& item = weather.data.hourly[index];
            std::snprintf(detail, sizeof(detail), "%s  ·  %.0f°C  ·  %s  ·  %s %d%%",
                          weather_time_label(item.time).c_str(), item.temperature_c,
                          weather_condition_text(item.condition), v().weather_rain,
                          item.rain_probability_percent);
            lv_obj_set_style_border_width(weather_hour_cards[index], 2, 0);
            lv_obj_set_style_border_color(weather_hour_cards[index],
                ui::color(weather_condition_accent(item.condition)), 0);
        }
        lv_label_set_text(weather_detail_label, detail);
    }

    // ---- Components -------------------------------------------------------

    lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, int width,
                    const lv_font_t* font = nullptr, std::uint32_t color = ui::tokens::text,
                    lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
        auto* value = ui::make_label(parent, text, color, font ? font : ui::font_body());
        lv_obj_set_width(value, width);
        lv_obj_set_style_text_align(value, align, 0);
        lv_obj_set_pos(value, x, y);
        return value;
    }

    // One line that ends in an ellipsis instead of wrapping.
    lv_obj_t* line(lv_obj_t* parent, const char* text, int x, int y, int width, const lv_font_t* font,
                   std::uint32_t color = ui::tokens::text, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
        auto* value = label(parent, text, x, y, width, font, color, align);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_obj_set_height(value, lv_font_get_line_height(font));
        return value;
    }

    // Icons are 64 px A8 sources drawn at `size`, centred in a size x size box.
    lv_obj_t* icon_view(lv_obj_t* parent, ui::IconRole role, int size = 28,
                        std::uint32_t color = ui::tokens::text) {
        const auto* source = ui::icon_asset(role);
        if (!source) return nullptr;
        auto* value = lv_image_create(parent);
        lv_image_set_src(value, source);
        lv_image_set_scale(value, static_cast<std::uint32_t>(std::max(1, size) * LV_SCALE_NONE / 64));
        lv_obj_set_size(value, size, size);
        lv_obj_set_style_image_recolor(value, ui::color(color), 0);
        lv_obj_set_style_image_recolor_opa(value, LV_OPA_COVER, 0);
        lv_obj_set_clickable(value, false);
        lv_obj_set_scrollable(value, false);
        return value;
    }

    // A thin progress ring whose 100-degree segment the tick rotates.
    lv_obj_t* spinner_arc(lv_obj_t* parent, int size, std::uint32_t accent, int width) {
        auto* arc = lv_arc_create(parent);
        lv_obj_set_size(arc, size, size);
        lv_obj_set_style_pad_all(arc, 0, 0);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_angles(arc, 0, 100);
        lv_obj_set_style_arc_color(arc, ui::color(ui::tokens::separator), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, ui::color(accent), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_clickable(arc, false);
        return arc;
    }

    lv_obj_t* glow_shape(lv_obj_t* parent, int width, int height, int x, int y,
                         std::uint32_t accent, lv_opa_t opacity, int radius = LV_RADIUS_CIRCLE) {
        auto* shape = lv_obj_create(parent);
        ui::reset(shape);
        lv_obj_set_size(shape, width, height);
        lv_obj_set_pos(shape, x, y);
        lv_obj_set_style_radius(shape, radius, 0);
        lv_obj_set_style_bg_color(shape, ui::color(accent), 0);
        lv_obj_set_style_bg_opa(shape, opacity, 0);
        return shape;
    }

    // A tinted circle (accent at 18 %) with the icon in the accent colour;
    // `neutral` makes a graphite circle with a white icon (back button).
    lv_obj_t* round_button(lv_obj_t* parent, int x, int y, int size, ui::IconRole role,
                           std::uint32_t accent, lv_event_cb_t callback = nullptr, bool neutral = false) {
        auto* button = lv_button_create(parent);
        ui::reset(button);
        lv_obj_set_clickable(button, true);
        lv_obj_set_size(button, size, size);
        lv_obj_set_pos(button, x, y);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(button, ui::color(neutral ? ui::tokens::surface : accent), 0);
        lv_obj_set_style_bg_opa(button, neutral ? static_cast<lv_opa_t>(LV_OPA_COVER) : kTint, 0);
        lv_obj_set_style_bg_color(button, ui::color(neutral ? ui::tokens::surface_high : accent),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button, neutral ? LV_OPA_COVER : LV_OPA_40, LV_STATE_PRESSED);
        lv_obj_set_style_transform_width(button, -3, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(button, -3, LV_STATE_PRESSED);
        if (role != ui::IconRole::Count) {
            auto* icon = icon_view(button, role, size >= 88 ? 40 : 30, neutral ? ui::tokens::text : accent);
            lv_obj_center(icon);
        }
        if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
        return button;
    }

    lv_obj_t* nav_round_button(lv_obj_t* parent, int x, int y, int size, ui::IconRole role,
                               std::uint32_t accent, UiView view, bool neutral = false) {
        auto* button = round_button(parent, x, y, size, role, accent, nullptr, neutral);
        set_target(button, view);
        lv_obj_add_event_cb(button, nav_event, LV_EVENT_CLICKED, this);
        return button;
    }

    lv_obj_t* pill(lv_obj_t* parent, const char* text, ui::ButtonTone tone, int x, int y, int width,
                   lv_event_cb_t callback) {
        auto* button = ui::make_button(parent, text, tone);
        lv_obj_set_size(button, width, kButtonHeight);
        lv_obj_set_pos(button, x, y);
        if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
        return button;
    }

    // A 432 x 88 platter: tinted icon well, 28 px title, optional 22 px
    // subtitle. `text_width` narrows both when the row carries its own control.
    lv_obj_t* list_row(lv_obj_t* parent, int y, const char* title, const char* subtitle,
                       ui::IconRole role, std::uint32_t accent, int height = kRowHeight, int text_width = 0) {
        auto* row = lv_obj_create(parent);
        ui::reset(row);
        ui::style_platter(row, accent, ui::tokens::radius_large, false);
        lv_obj_set_size(row, kWidth, height);
        lv_obj_set_pos(row, kGutter, y);
        lv_obj_set_style_pad_left(row, 16, 0);
        lv_obj_set_style_pad_right(row, 20, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        const bool has_icon = role != ui::IconRole::Count;
        const int title_height = lv_font_get_line_height(ui::font_headline());
        const int detail_height = subtitle ? lv_font_get_line_height(ui::font_caption()) : 0;
        const int text_x = has_icon ? 68 : 4;
        if (has_icon) {
            auto* well = make_dot(row, 52, accent, kTint);
            lv_obj_align(well, LV_ALIGN_LEFT_MID, 0, 0);
            auto* icon = icon_view(well, role, 30, accent);
            lv_obj_center(icon);
        }
        const int width = text_width > 0 ? text_width : kWidth - 36 - text_x - 36;
        auto* heading = ui::make_label(row, title, ui::tokens::text, ui::font_headline());
        lv_obj_set_size(heading, width, title_height);
        lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
        lv_obj_align(heading, LV_ALIGN_LEFT_MID, text_x, -detail_height / 2);
        if (subtitle) {
            auto* detail = ui::make_label(row, subtitle, ui::tokens::text_secondary, ui::font_caption());
            lv_obj_set_size(detail, width, detail_height);
            lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
            lv_obj_align(detail, LV_ALIGN_LEFT_MID, text_x, title_height / 2);
        }
        return row;
    }

    lv_obj_t* add_chevron(lv_obj_t* row) {
        auto* arrow = icon_view(row, ui::IconRole::Chevron, 28, ui::tokens::text_tertiary);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
        return arrow;
    }

    // Back (long press: Home), a 32 px title, and either the time (status
    // chrome) or one action button on the right.
    void make_header(const char* title, ui::IconRole action = ui::IconRole::Count,
                     lv_event_cb_t action_callback = nullptr, std::uint32_t action_accent = ui::tokens::blue,
                     bool action_enabled = true) {
        header_back = round_button(content, 16, 16, ui::tokens::HEADER_BUTTON, ui::IconRole::ChevronLeft,
                                   ui::tokens::text, back_event, true);
        lv_obj_add_event_cb(header_back, home_event, LV_EVENT_LONG_PRESSED, this);
        header_title = line(content, title, 96, 28, 284, ui::font_title(), ui::tokens::text);
        header_action = action != ui::IconRole::Count;
        if (header_action) {
            auto* button = round_button(content, 400, 16, ui::tokens::HEADER_BUTTON, action, action_accent,
                                        action_callback);
            ui::set_button_disabled(button, !action_enabled);
        }
    }

    // Header-right tag (e.g. TEST) that replaces the time.
    void header_tag(const char* text, std::uint32_t accent) {
        header_action = true;
        auto* tag = ui::make_tag(content, text, accent);
        lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, -kGutter, 34);
    }

    // A 64 px pill with an icon, a 28 px value and a 22 px detail.
    lv_obj_t* chip(int x, int y, int width, ui::IconRole role, std::uint32_t accent,
                   const char* title, const char* detail, lv_event_cb_t callback) {
        auto* value = lv_button_create(content);
        ui::reset(value);
        lv_obj_set_clickable(value, true);
        ui::style_platter(value, accent, LV_RADIUS_CIRCLE, false);
        lv_obj_set_size(value, width, ui::tokens::CHIP_HEIGHT);
        lv_obj_set_pos(value, x, y);
        lv_obj_set_style_pad_left(value, 18, 0);
        lv_obj_set_style_pad_right(value, 22, 0);
        lv_obj_set_style_pad_ver(value, 0, 0);
        lv_obj_set_layout(value, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(value, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(value, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(value, 12, 0);
        icon_view(value, role, 32, accent);
        auto* heading = ui::make_label(value, title, ui::tokens::text, ui::font_headline());
        lv_label_set_long_mode(heading, LV_LABEL_LONG_CLIP);
        if (detail) {
            auto* note = ui::make_label(value, detail, ui::tokens::text_secondary, ui::font_caption());
            lv_obj_set_flex_grow(note, 1);
            lv_obj_set_height(note, lv_font_get_line_height(ui::font_caption()));
            lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
        }
        if (callback) lv_obj_add_event_cb(value, callback, LV_EVENT_CLICKED, this);
        return value;
    }

    // A full-width vertical scroller below the header.
    lv_obj_t* make_list(int y = kHeader, int height = ui::tokens::display_height - kHeader) {
        auto* list = lv_obj_create(content);
        ui::reset(list);
        lv_obj_set_size(list, ui::tokens::display_width, height);
        lv_obj_set_pos(list, 0, y);
        lv_obj_set_clickable(list, true);
        lv_obj_set_scrollable(list, true);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_overflow_visible(list, false);
        ui::style_scrollbar(list);
        primary_scroller = list;
        return list;
    }

    // Keeps the last row clear of the bottom edge when a list scrolls.
    void list_end(lv_obj_t* list, int y, int space = 16) {
        auto* spacer = lv_obj_create(list);
        ui::reset(spacer);
        lv_obj_set_size(spacer, 1, space);
        lv_obj_set_pos(spacer, 0, y);
    }

    lv_obj_t* settings_row(lv_obj_t* list, int y, const char* title, const char* subtitle, ui::IconRole role,
                           std::uint32_t accent, UiView view) {
        auto* row = list_row(list, y, title, subtitle, role, accent);
        add_chevron(row);
        set_target(row, view);
        lv_obj_set_clickable(row, true);
        lv_obj_add_event_cb(row, nav_event, LV_EVENT_CLICKED, this);
        return row;
    }

    // A centred tinted circle with an icon: the anchor of state screens.
    lv_obj_t* state_symbol(int y, int size, ui::IconRole role, std::uint32_t accent) {
        auto* well = make_dot(content, size, accent, kTint);
        lv_obj_set_pos(well, (ui::tokens::display_width - size) / 2, y);
        auto* icon = icon_view(well, role, size / 2, accent);
        lv_obj_center(icon);
        return well;
    }

    // Title + explanation, centred, stacked by their real heights.
    lv_obj_t* state_text(int y, const char* title, const char* detail, const char* note = nullptr) {
        auto* column = lv_obj_create(content);
        ui::reset(column);
        lv_obj_set_size(column, kWidth, LV_SIZE_CONTENT);
        lv_obj_set_pos(column, kGutter, y);
        lv_obj_set_layout(column, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(column, 8, 0);
        auto* heading = ui::make_label(column, title, ui::tokens::text, ui::font_title());
        lv_obj_set_width(heading, lv_pct(100));
        lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
        if (detail) {
            auto* body = ui::make_label(column, detail, ui::tokens::text_secondary, ui::font_body());
            lv_obj_set_width(body, lv_pct(100));
            lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
        }
        if (note) {
            auto* caption = ui::make_label(column, note, ui::tokens::text_tertiary, ui::font_caption());
            lv_obj_set_width(caption, lv_pct(100));
            lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
        }
        return column;
    }

    lv_obj_t* make_text_input(int x, int y, int width, int height, const char* placeholder, bool one_line) {
        auto* area = lv_textarea_create(content);
        lv_obj_set_size(area, width, height);
        lv_obj_set_pos(area, x, y);
        if (one_line) lv_textarea_set_one_line(area, true);
        lv_textarea_set_placeholder_text(area, placeholder);
        lv_obj_set_style_text_font(area, ui::font_body(), 0);
        lv_obj_set_style_text_color(area, ui::color(ui::tokens::text), 0);
        lv_obj_set_style_text_color(area, ui::color(ui::tokens::text_tertiary), LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_bg_color(area, ui::color(ui::tokens::surface), 0);
        lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(area, ui::color(ui::tokens::separator), 0);
        lv_obj_set_style_border_color(area, ui::color(ui::tokens::blue), LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(area, 2, 0);
        lv_obj_set_style_radius(area, ui::tokens::radius_medium, 0);
        lv_obj_set_style_pad_hor(area, 20, 0);
        lv_obj_set_style_pad_ver(area, one_line ? (height - lv_font_get_line_height(ui::font_body())) / 2 - 2 : 12, 0);
        lv_obj_set_style_bg_color(area, ui::color(ui::tokens::ice), LV_PART_CURSOR);
        // A physical keyboard (simulator) types into the visible field directly.
        if (auto* group = lv_group_get_default()) {
            lv_group_add_obj(group, area);
            lv_group_focus_obj(area);
        }
        lv_obj_add_state(area, LV_STATE_FOCUSED);
        return area;
    }

    lv_obj_t* make_form_keyboard(lv_obj_t* area, ui::KeyboardProfile profile, int y = kKeyboardY) {
        auto* keyboard = ui::create_keyboard(content, area, profile);
        lv_obj_set_size(keyboard, ui::tokens::display_width, ui::tokens::display_height - y);
        lv_obj_set_pos(keyboard, 0, y);
        return keyboard;
    }

    void create_status() {
        status_bar = lv_obj_create(screen);
        ui::reset(status_bar);
        lv_obj_set_size(status_bar, ui::tokens::display_width, kHeader);
        lv_obj_set_pos(status_bar, 0, 0);

        // Home keeps its status in a 40 px band above the clock.
        status_left = lv_obj_create(status_bar);
        ui::reset(status_left);
        lv_obj_set_size(status_left, LV_SIZE_CONTENT, 40);
        lv_obj_set_pos(status_left, 14, 0);
        lv_obj_set_layout(status_left, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(status_left, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(status_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(status_left, 4, 0);

        // The connectivity glyph is also the shortcut to Wi-Fi settings.
        status_network_button = lv_obj_create(status_left);
        ui::reset(status_network_button);
        lv_obj_set_size(status_network_button, 40, 40);
        lv_obj_set_style_radius(status_network_button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(status_network_button, ui::color(ui::tokens::surface_high), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(status_network_button, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_clickable(status_network_button, true);
        lv_obj_set_ext_click_area(status_network_button, 12);
        lv_obj_add_event_cb(status_network_button, wifi_open_event, LV_EVENT_CLICKED, this);
        status_network = icon_view(status_network_button, ui::IconRole::Wifi, 28, ui::tokens::text);
        lv_obj_center(status_network);

        status_bluetooth = icon_view(status_left, ui::IconRole::Bluetooth, 24, ui::tokens::text_secondary);
        status_muted = icon_view(status_left, ui::IconRole::Muted, 24, ui::tokens::text_secondary);
        status_developer = ui::make_tag(status_left, t().developer_badge, ui::tokens::amber);

        status_right = lv_obj_create(status_bar);
        ui::reset(status_right);
        lv_obj_set_size(status_right, LV_SIZE_CONTENT, 40);
        lv_obj_set_layout(status_right, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(status_right, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(status_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(status_right, 8, 0);

        status_alert = icon_view(status_right, ui::IconRole::WifiOff, 28, ui::tokens::coral);
        status_charging = icon_view(status_right, ui::IconRole::Charging, 28, ui::tokens::mint);
        status_battery = ui::make_label(status_right, "", ui::tokens::text, ui::font_caption());
        status_battery_glyph = lv_obj_create(status_right);
        ui::reset(status_battery_glyph);
        lv_obj_set_size(status_battery_glyph, 42, 22);
        auto* outline = lv_obj_create(status_battery_glyph);
        ui::reset(outline);
        lv_obj_set_size(outline, 36, 20);
        lv_obj_set_pos(outline, 0, 1);
        lv_obj_set_style_radius(outline, 6, 0);
        lv_obj_set_style_border_width(outline, 2, 0);
        lv_obj_set_style_border_color(outline, ui::color(ui::tokens::text_secondary), 0);
        lv_obj_set_style_border_opa(outline, LV_OPA_COVER, 0);
        status_battery_fill = lv_obj_create(outline);
        ui::reset(status_battery_fill);
        lv_obj_set_size(status_battery_fill, 26, 12);
        lv_obj_set_pos(status_battery_fill, 4, 4);
        lv_obj_set_style_radius(status_battery_fill, 2, 0);
        lv_obj_set_style_bg_color(status_battery_fill, ui::color(ui::tokens::mint), 0);
        lv_obj_set_style_bg_opa(status_battery_fill, LV_OPA_COVER, 0);
        auto* cap = lv_obj_create(status_battery_glyph);
        ui::reset(cap);
        lv_obj_set_size(cap, 4, 8);
        lv_obj_set_pos(cap, 38, 7);
        lv_obj_set_style_radius(cap, 2, 0);
        lv_obj_set_style_bg_color(cap, ui::color(ui::tokens::text_secondary), 0);
        lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
        status_time = ui::make_label(status_right, "", ui::tokens::text_secondary, ui::font_body());
    }

    void move_status_foreground() {
        if (status_bar) lv_obj_move_foreground(status_bar);
    }

    // ---- Presence: halo ring + Mercury orb --------------------------------

    // The ring stays below luminance 145 inside the orb's image bounds (the
    // Mercury evidence rule) and is updated with Mercury frames (15 FPS).
    void create_presence(ui::MercuryState state, std::uint32_t accent) {
        halo_state = state;
        const int size = ui::tokens::HALO_RADIUS * 2 + 8;
        halo = lv_arc_create(content);
        lv_obj_set_size(halo, size, size);
        lv_obj_set_pos(halo, kHaloCenterX - size / 2, kHaloCenterY - size / 2);
        lv_obj_set_style_pad_all(halo, 0, 0);
        lv_obj_set_style_bg_opa(halo, LV_OPA_TRANSP, 0);
        lv_arc_set_bg_angles(halo, 0, 360);
        lv_arc_set_angles(halo, 0, state == ui::MercuryState::Thinking ? 100 : 360);
        lv_arc_set_rotation(halo, 270);
        lv_obj_set_style_arc_color(halo, ui::color(ui::tokens::separator), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(halo, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_arc_width(halo, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_color(halo, ui::color(accent), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(halo, state == ui::MercuryState::Thinking ? 6 : 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(halo, state == ui::MercuryState::Idle ? 120 : 190, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(halo, true, LV_PART_INDICATOR);
        lv_obj_set_style_opa(halo, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_clickable(halo, false);
        mercury.set_state(state);
        mercury.set_level(state == ui::MercuryState::Listening ? input_amplitude : audio.envelope);
        mercury.set_reduced_motion(reduced_motion);
        hero_image = mercury.attach(content, ui::tokens::MERCURY_RECT.x, ui::tokens::MERCURY_RECT.y);
    }

    void update_halo(const MotionFrame& frame) {
        if (!halo) return;
        int width = 3;
        int opacity = 150;
        switch (halo_state) {
            case ui::MercuryState::Idle:
                opacity = 90 + static_cast<int>(frame.pulse * 70.0F);
                break;
            case ui::MercuryState::Listening:
                width = 3 + static_cast<int>(input_amplitude * 7.0F);
                opacity = 150 + static_cast<int>(input_amplitude * 50.0F);
                break;
            case ui::MercuryState::Thinking:
                width = 6;
                opacity = 220;
                lv_arc_set_rotation(halo, static_cast<int32_t>((270U + phase_ms / 3U) % 360U));
                break;
            case ui::MercuryState::Speaking:
                width = 3 + static_cast<int>(audio.envelope * 7.0F);
                opacity = 160 + static_cast<int>(audio.envelope * 60.0F);
                break;
        }
        lv_obj_set_style_arc_width(halo, width, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(halo, static_cast<lv_opa_t>(std::clamp(opacity, 0, 220)), LV_PART_INDICATOR);
    }

    // State title and caption above the presence.
    void stage_title(const char* title, const char* caption, std::uint32_t caption_color) {
        state_label = line(content, title, kGutter, 28, kWidth, ui::font_title(), ui::tokens::text,
                           LV_TEXT_ALIGN_CENTER);
        if (caption) line(content, caption, kGutter, 76, kWidth, ui::font_caption(), caption_color,
                          LV_TEXT_ALIGN_CENTER);
    }

    // ---- Screens -----------------------------------------------------------

    void update_home_text() {
        if (home_clock && status.clock_valid) {
            char clock[12];
            std::snprintf(clock, sizeof(clock), "%02d:%02d", status.hour, status.minute);
            set_text_if_changed(home_clock, clock);
        }
        if (!home_date) return;
        if (status.date_valid && status.month >= 1 && status.month <= 12 &&
            status.weekday >= 0 && status.weekday <= 6) {
            char date[80];
            std::snprintf(date, sizeof(date), v().date_format, v().weekday_names[status.weekday], status.day,
                          v().month_names[status.month - 1]);
            set_text_if_changed(home_date, date);
        } else {
            set_text_if_changed(home_date, t().presence_idle_hint);
        }
    }

    void render_weather_chip() {
        const char* title = v().weather;
        std::string detail = weather.has_saved_location ? weather.saved_location.name : v().weather_choose_city;
        ui::IconRole role = ui::IconRole::Weather;
        std::uint32_t accent = ui::tokens::amber;
        char temperature[16];
        if (weather.has_data) {
            std::snprintf(temperature, sizeof(temperature), "%.0f°", weather.data.temperature_c);
            title = temperature;
            detail = weather.data.location.name;
            if (weather.data.origin == WeatherDataOrigin::Demo) detail += std::string(" · ") + v().demo_badge;
            role = weather_condition_icon(weather.data.condition);
            accent = weather_condition_accent(weather.data.condition);
        }
        chip(kGutter, 404, 344, role, accent, title, detail.c_str(), weather_open_event);
    }

    void render_home() {
        // The clock, or the wordmark until the clock is known.
        if (status.clock_valid) {
            home_clock = label(content, "", 0, 42, ui::tokens::display_width, ui::font_clock(), ui::tokens::text,
                               LV_TEXT_ALIGN_CENTER);
        } else {
            auto* brand = label(content, t().aura_name, 0, 64, ui::tokens::display_width, ui::font_brand(),
                                ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_style_text_letter_space(brand, 10, 0);
        }
        home_date = line(content, "", kGutter, 116, kWidth, ui::font_body(), ui::tokens::text_secondary,
                         LV_TEXT_ALIGN_CENTER);
        update_home_text();
        attach_briefing_target();

        create_presence(ui::MercuryState::Idle, ui::tokens::lilac);
        // The whole halo is the talk button.
        auto* talk = lv_button_create(content);
        ui::reset(talk);
        lv_obj_set_size(talk, ui::tokens::HERO_RECT.width, ui::tokens::HERO_RECT.height);
        lv_obj_set_pos(talk, ui::tokens::HERO_RECT.x, ui::tokens::HERO_RECT.y);
        lv_obj_set_style_radius(talk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(talk, ui::color(ui::tokens::text), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(talk, LV_OPA_10, LV_STATE_PRESSED);
        lv_obj_set_clickable(talk, static_cast<bool>(callbacks.on_ask));
        lv_obj_add_event_cb(talk, ask_event, LV_EVENT_CLICKED, this);
        line(content, callbacks.on_ask ? t().home_hint : t().provider_unavailable, kGutter, 368, kWidth,
             ui::font_caption(), ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);

        if (!render_home_timer_chip()) render_weather_chip();
        auto* apps = nav_round_button(content, 392, 404, ui::tokens::CHIP_HEIGHT, ui::IconRole::Apps,
                                      ui::tokens::text, UiView::Launcher, true);
        if (notification_total() > 0) {
            auto* dot = make_dot(apps, 14, ui::tokens::pink);
            lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -4, 4);
        }
        motion.enter(MotionMode::Idle);
    }

    void render_listening() {
        create_presence(ui::MercuryState::Listening, ui::tokens::ice);
        stage_title(v().listening_title, input_simulated ? v().synthetic_input : t().privacy_listening,
                    ui::tokens::amber);
        pill(content, t().finish_listening, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth,
             end_listening_event);
        pill(content, t().cancel, ui::ButtonTone::Danger, kGutter + kHalfWidth + 16, kActionY, kHalfWidth,
             cancel_event);
        motion.enter(MotionMode::Listening);
        motion.set_amplitude(input_amplitude);
    }

    void render_thinking() {
        create_presence(ui::MercuryState::Thinking, ui::tokens::lilac);
        if (snapshot.state == AssistantState::Transcribing) {
            stage_title(c().transcribing_title, c().transcribing_detail, ui::tokens::text_secondary);
        } else {
            stage_title(v().thinking_title,
                        snapshot.user_text.empty() ? v().thinking_subtitle : snapshot.user_text.c_str(),
                        ui::tokens::text_secondary);
        }
        pill(content, t().cancel, ui::ButtonTone::Danger, 120, kActionY, 240, cancel_event);
        motion.enter(MotionMode::Thinking);
    }

    void create_meter(lv_obj_t* parent, int count = 9) {
        const int start = 240 - ((count - 1) * 24) / 2;
        for (int index = 0; index < count; ++index) {
            auto* bar = lv_obj_create(parent);
            ui::reset(bar);
            lv_obj_set_size(bar, 6, 14 + (index % 3) * 4);
            lv_obj_set_pos(bar, start + index * 24 - 3, kMeterCenterY - 7);
            lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(bar, ui::color(index % 2 == 0 ? ui::tokens::ice : ui::tokens::lilac), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            audio_meter[static_cast<std::size_t>(index)] = bar;
        }
    }

    void render_speaking() {
        create_presence(ui::MercuryState::Speaking, ui::tokens::magenta);
        const bool paused = audio.state == AudioState::Paused;
        stage_title(v().speaking_title, paused ? t().pause : v().speaking_subtitle,
                    paused ? ui::tokens::amber : ui::tokens::text_secondary);
        round_button(content, 80, 372, 88, paused ? ui::IconRole::Play : ui::IconRole::Pause,
                     ui::tokens::blue, audio_pause_event);
        // Same convention as the Sound screen: the glyph shows the current state.
        round_button(content, 196, 372, 88, audio.muted ? ui::IconRole::Muted : ui::IconRole::VolumeUp,
                     audio.muted ? ui::tokens::amber : ui::tokens::mint, audio_mute_event);
        round_button(content, 312, 372, 88, ui::IconRole::Stop, ui::tokens::coral, audio_stop_event);
        motion.enter(MotionMode::Speaking);
        motion.set_amplitude(audio.envelope);
    }

    void render_answer_reader() {
        render_answer_companion();
    }

    struct FailureCopy {
        const char* title;
        const char* detail;
        bool network;
    };

    // The screen names the actual cause; a missing CLI is not "no connection".
    FailureCopy failure_copy(bool error) const {
        const auto& code = snapshot.error_code;
        if (code == "NETWORK_UNAVAILABLE") return {t().offline_title, t().error_network_detail, true};
        if (code == "BRIDGE_UNAVAILABLE" || code == "PROVIDER_UNAVAILABLE") {
            return {t().error_provider_title, t().error_bridge_detail, false};
        }
        if (code == "CLI_NOT_INSTALLED") return {t().error_provider_title, t().error_cli_missing_detail, false};
        if (code == "NOT_LOGGED_IN") return {t().error_provider_title, t().error_login_detail, false};
        if (code == "NO_SPEECH") return {c().voice_error_title, c().no_speech_detail, false};
        if (code == "STT_UNAVAILABLE") return {c().voice_error_title, c().stt_unavailable_detail, false};
        if (code == "MIC_UNAVAILABLE") return {c().voice_error_title, c().mic_unavailable_detail, false};
        if (code == "TIMEOUT") return {t().error_title, t().error_timeout_detail, false};
        if (code == "BUSY") return {t().error_title, t().error_busy_detail, false};
        return error ? FailureCopy{t().error_title, t().error_body, false} :
                       FailureCopy{t().offline_title, v().offline_detail, false};
    }

    void render_offline(bool error = false) {
        const auto copy = failure_copy(error);
        const std::uint32_t accent = error ? ui::tokens::coral : ui::tokens::magenta;
        state_symbol(48, 96, copy.network ? ui::IconRole::WifiOff :
                     (error ? ui::IconRole::Warning : ui::IconRole::Offline), accent);
        char code_line[96] = {};
        if (!snapshot.error_code.empty()) {
            std::snprintf(code_line, sizeof(code_line), t().error_code_format, snapshot.error_code.c_str());
        }
        state_text(160, copy.title, copy.detail, code_line[0] ? code_line : nullptr);
        auto* retry = pill(content, t().retry, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth, retry_event);
        ui::set_button_disabled(retry, !callbacks.on_retry);
        if (copy.network) {
            pill(content, t().wifi_title, ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, kActionY, kHalfWidth,
                 network_settings_event);
        } else {
            pill(content, v().back_home, ui::ButtonTone::Secondary, kGutter + kHalfWidth + 16, kActionY, kHalfWidth,
                 home_event);
        }
        motion.enter(error ? MotionMode::Error : MotionMode::None);
    }

    void render_assistant() {
        switch (snapshot.state) {
            case AssistantState::Listening:
            case AssistantState::WakeDetected: render_listening(); break;
            case AssistantState::Transcribing:
            case AssistantState::Thinking: render_thinking(); break;
            case AssistantState::Responding:
                if (audio.state == AudioState::Playing || audio.state == AudioState::Paused) render_speaking();
                else render_answer_reader();
                break;
            case AssistantState::Offline: render_offline(false); break;
            case AssistantState::Error: render_offline(true); break;
            case AssistantState::ActionConfirmation: render_answer_reader(); break;
            default: render_home(); break;
        }
    }

    void render_launcher() {
        render_launcher_grid();
    }

    void render_notifications() {
        make_header(v().notifications);
        const bool has_demo = snapshot.notification_count > 0;
        const std::size_t total = aoraton.notifications.size();
        if (!has_demo && total == 0 && phone.notifications.empty()) {
            state_symbol(128, 112, ui::IconRole::Notification, ui::tokens::lilac);
            state_text(264, v().notification_empty, nullptr);
            return;
        }
        auto* list = make_list(kHeader, has_demo ? kActionY - 4 - kHeader : ui::tokens::display_height - kHeader);
        int y = 0;
        if (has_demo) {
            auto* message = list_row(list, y, v().new_message, v().notification_demo_detail,
                                     ui::IconRole::Unread, ui::tokens::pink, kRowHeight, 248);
            set_target(message, UiView::AoratonRooms);
            lv_obj_set_clickable(message, true);
            lv_obj_add_event_cb(message, nav_event, LV_EVENT_CLICKED, this);
            char count[12];
            std::snprintf(count, sizeof(count), "%d", snapshot.notification_count);
            auto* badge = ui::make_tag(message, count, ui::tokens::pink);
            lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
            y += kRowPitch;
        }
        // Real-service Aoraton notifications (newest first). Previews appear only
        // when the service included them under its privacy setting.
        for (std::size_t shown = 0; shown < std::min<std::size_t>(total, 6); ++shown) {
            const std::size_t index = total - 1 - shown;
            const auto& item = aoraton.notifications[index];
            const std::string detail = item.preview.empty() ?
                std::string(t().aoraton_notification_title) + " · " + item.timestamp : item.preview;
            auto* row = list_row(list, y, item.room_alias.c_str(), detail.c_str(),
                                 ui::IconRole::Message, ui::tokens::lilac);
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_set_clickable(row, true);
            lv_obj_add_event_cb(row, aoraton_notification_event, LV_EVENT_CLICKED, this);
            y += kRowPitch;
        }
        y = render_phone_rows(list, y);
        list_end(list, y);
        if (has_demo) {
            pill(content, v().notification_clear, ui::ButtonTone::Danger, kGutter, kActionY, kWidth,
                 notifications_clear_event);
        }
    }

    const char* security_text() const {
        return aoraton.runtime_mode == AoratonRuntimeMode::Mock ? t().secure_demo :
            (aoraton.runtime_mode == AoratonRuntimeMode::IntegrationTest ? t().secure_test : t().secure_live);
    }

    void render_aoraton_rooms() {
        if (!aoraton.mock_mode && aoraton.pairing != AoratonPairingState::Paired) {
            const bool reauth = aoraton.pairing == AoratonPairingState::ReauthorizationRequired;
            make_header(t().aoraton);
            if (aoraton.runtime_mode == AoratonRuntimeMode::IntegrationTest) header_tag(t().secure_test, ui::tokens::amber);
            state_symbol(96, 88, reauth ? ui::IconRole::Lock : ui::IconRole::Encrypted,
                         reauth ? ui::tokens::amber : ui::tokens::lilac);
            line(content, reauth ? c().aoraton_reauth : t().aoraton_connect, kGutter, 196, kWidth, ui::font_title(),
                 ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            label(content, reauth ? c().aoraton_reauth_detail : c().aoraton_sign_in_help, kGutter, 242, kWidth,
                  ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            auto* connect = pill(content, c().aoraton_sign_in, ui::ButtonTone::Primary, kGutter, kActionY, kWidth,
                                 pairing_start_event);
            ui::set_button_disabled(connect, !callbacks.on_aoraton_pair_start);
            return;
        }
        const bool connected = aoraton.connection == AoratonConnectionState::Connected;
        make_header(t().aoraton, ui::IconRole::Compose, compose_event, ui::tokens::blue,
                    !aoraton.rooms.empty() && connected);
        auto* list = make_list();
        // The live account is polled, so it never claims instant delivery.
        const char* status = !connected ? t().disconnected :
                             (aoraton.mock_mode ? security_text() : c().aoraton_live_polling);
        line(list, status, kGutter + 4, 0, kWidth - 8, ui::font_caption(),
             connected ? ui::tokens::mint : ui::tokens::coral);
        int y = 40;
        // All authorized rooms (bounded by the service) live in one vertical scroller.
        const std::size_t count = aoraton.rooms.size();
        for (std::size_t index = 0; index < count; ++index) {
            const auto& room = aoraton.rooms[index];
            const char* preview = !room.key_available ?
                                  (aoraton.mock_mode ? t().aoraton_room_locked : c().aoraton_room_locked) :
                                  (room.preview.empty() ? t().no_messages : room.preview.c_str());
            auto* row = list_row(list, y, room.display_name.c_str(), preview,
                                 index % 3 == 2 ? ui::IconRole::Room : ui::IconRole::Message,
                                 kIconAccents[index % kIconAccents.size()], kRowHeight,
                                 room.unread > 0 ? 248 : 0);
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<std::intptr_t>(index)));
            lv_obj_set_clickable(row, true);
            lv_obj_add_event_cb(row, room_event, LV_EVENT_CLICKED, this);
            if (room.unread > 0) {
                char unread[12];
                std::snprintf(unread, sizeof(unread), "%u", static_cast<unsigned>(room.unread));
                auto* badge = ui::make_tag(row, unread, ui::tokens::blue);
                lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
            }
            y += kRowPitch;
        }
        if (count == 0) {
            list_row(list, y, t().rooms, t().rooms_demo_hint, ui::IconRole::Inbox, ui::tokens::indigo);
            y += kRowPitch;
        }
        if (!aoraton.mock_mode && aoraton.pairing == AoratonPairingState::Paired) {
            settings_row(list, y, c().aoraton_account_title, aoraton.account_label.empty() ? nullptr :
                         aoraton.account_label.c_str(), ui::IconRole::Settings, ui::tokens::ice, UiView::AoratonPair);
            y += kRowPitch;
        }
        list_end(list, y);
    }

    bool key_state_is_error() const {
        switch (aoraton.key_state) {
            case AoratonKeyState::WrongPassphrase:
            case AoratonKeyState::NotConfigured:
            case AoratonKeyState::NotMember:
            case AoratonKeyState::InvalidInvite:
            case AoratonKeyState::Failed: return true;
            case AoratonKeyState::Unknown:
            case AoratonKeyState::Unlocking:
            case AoratonKeyState::Ready: break;
        }
        return false;
    }

    const char* key_state_text() const {
        switch (aoraton.key_state) {
            case AoratonKeyState::Unlocking: return c().aoraton_keys_unlocking;
            case AoratonKeyState::WrongPassphrase: return c().aoraton_keys_wrong;
            case AoratonKeyState::NotConfigured: return c().aoraton_keys_not_configured;
            case AoratonKeyState::NotMember: return c().aoraton_keys_not_member;
            case AoratonKeyState::InvalidInvite: return c().aoraton_keys_invalid_invite;
            case AoratonKeyState::Failed: return c().aoraton_keys_failed;
            case AoratonKeyState::Unknown:
            case AoratonKeyState::Ready: break;
        }
        return nullptr;
    }

    std::string key_count_text() const {
        if (aoraton.key_count == 0) return c().aoraton_keys_none;
        if (aoraton.key_count == 1) return c().aoraton_keys_one;
        char text[48];
        std::snprintf(text, sizeof(text), c().aoraton_keys_format, static_cast<unsigned>(aoraton.key_count));
        return text;
    }

    // The first character of the account name (UTF-8), for its avatar.
    std::string account_initial() const {
        const auto& name = aoraton.account_label;
        if (name.empty()) return {};
        const auto lead = static_cast<unsigned char>(name.front());
        const std::size_t bytes = lead < 0x80 ? 1 : (lead >= 0xF0 ? 4 : (lead >= 0xE0 ? 3 : 2));
        return name.substr(0, std::min(bytes, name.size()));
    }

    // The signed-in account: a card with who and whether the link is up, then
    // room keys, invite paste and sign out, all above the fold.
    void render_aoraton_account() {
        make_header(t().aoraton);
        auto* list = make_list();
        auto* card = lv_obj_create(list);
        ui::reset(card);
        ui::style_platter(card, ui::tokens::lilac, ui::tokens::radius_large, true);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_size(card, kWidth, 104);
        lv_obj_set_pos(card, kGutter, 0);
        auto* avatar = make_dot(card, 64, ui::tokens::lilac, kTint);
        lv_obj_set_pos(avatar, 20, 20);
        const auto initial = account_initial();
        if (initial.empty()) {
            if (auto* icon = icon_view(avatar, ui::IconRole::Encrypted, 32, ui::tokens::lilac)) lv_obj_center(icon);
        } else {
            lv_obj_center(ui::make_label(avatar, initial.c_str(), ui::tokens::lilac, ui::font_title()));
        }
        line(card, aoraton.account_label.empty() ? c().aoraton_account_title : aoraton.account_label.c_str(),
             104, 18, kWidth - 124, ui::font_headline(), ui::tokens::text);
        const bool connected = aoraton.connection == AoratonConnectionState::Connected;
        const std::uint32_t link_color = connected ? ui::tokens::mint : ui::tokens::coral;
        lv_obj_set_pos(make_dot(card, 10, link_color), 106, 67);
        const std::string link = std::string(connected ? t().connected : t().disconnected) + " · " + c().aoraton_e2ee;
        line(card, link.c_str(), 124, 58, kWidth - 144, ui::font_caption(), link_color);
        int y = 112;
        const bool invite_error = aoraton.key_state == AoratonKeyState::NotMember ||
                                  aoraton.key_state == AoratonKeyState::InvalidInvite;
        const char* key_text = key_state_text();
        const std::string count_text = key_count_text();
        settings_row(list, y, c().aoraton_keys, key_text && !invite_error ? key_text : count_text.c_str(),
                     ui::IconRole::Lock, ui::tokens::lilac, UiView::AoratonUnlock);
        y += kRowPitch;
        auto* paste = list_row(list, y, c().aoraton_paste_invite, invite_error ? key_text : c().aoraton_paste_hint,
                               ui::IconRole::Copy, ui::tokens::blue);
        lv_obj_set_clickable(paste, static_cast<bool>(callbacks.on_aoraton_paste_invite));
        lv_obj_add_event_cb(paste, paste_invite_event, LV_EVENT_CLICKED, this);
        // Back returns to the rooms; sign-out stays above the fold.
        y += kRowPitch;
        auto* sign_out = pill(list, c().aoraton_sign_out, ui::ButtonTone::Danger, kGutter, y, kWidth, unpair_event);
        ui::set_button_disabled(sign_out, !callbacks.on_aoraton_unpair);
        list_end(list, y + kButtonHeight + 16);
    }

    void render_aoraton_unlock() {
        make_header(c().aoraton_unlock_title, ui::IconRole::Check, unlock_submit_event, ui::tokens::blue,
                    static_cast<bool>(callbacks.on_aoraton_unlock));
        unlock_area = make_text_input(kGutter, 100, kWidth, 72, c().aoraton_unlock_placeholder, true);
        lv_textarea_set_password_mode(unlock_area, true);
        lv_textarea_set_max_length(unlock_area, 256);
        lv_obj_add_event_cb(unlock_area, unlock_ready_event, LV_EVENT_READY, this);
        unlock_status = line(content, c().aoraton_unlock_hint, kGutter + 4, 176, kWidth - 8, ui::font_caption(),
                             ui::tokens::text_tertiary);
        update_unlock_status();
        make_form_keyboard(unlock_area, ui::KeyboardProfile::Password);
    }

    // Key results arrive while the owner may be typing: only the status line
    // changes, so a re-render never wipes the field.
    void update_unlock_status() {
        if (!unlock_status) return;
        const char* state = key_state_text();
        const bool unlocked = aoraton.key_state == AoratonKeyState::Ready && aoraton.key_count > 0;
        const std::string text = state ? std::string(state) :
                                 (unlocked ? key_count_text() : std::string(c().aoraton_unlock_hint));
        lv_label_set_text(unlock_status, text.c_str());
        lv_obj_set_style_text_color(unlock_status,
                                    ui::color(key_state_is_error() ? ui::tokens::coral :
                                              (unlocked ? ui::tokens::mint : ui::tokens::text_tertiary)),
                                    0);
    }

    const char* sign_in_error_title() const {
        if (aoraton.pairing == AoratonPairingState::Expired || aoraton.last_error == "SIGN_IN_TIMEOUT") {
            return c().aoraton_timeout_title;
        }
        if (aoraton.last_error == "SIGN_IN_NETWORK") return c().aoraton_sign_in_network;
        if (aoraton.last_error == "SIGN_IN_UNAVAILABLE") return c().aoraton_sign_in_unavailable;
        return c().aoraton_sign_in_failed;
    }

    void render_aoraton_pair() {
        const auto state = aoraton.pairing;
        if (state == AoratonPairingState::Paired) {
            render_aoraton_account();
            return;
        }
        make_header(t().aoraton);
        if (state == AoratonPairingState::NotPaired || state == AoratonPairingState::ReauthorizationRequired) {
            const bool reauth = state == AoratonPairingState::ReauthorizationRequired;
            state_symbol(100, 96, reauth ? ui::IconRole::Lock : ui::IconRole::Encrypted,
                         reauth ? ui::tokens::amber : ui::tokens::lilac);
            state_text(212, reauth ? c().aoraton_reauth : t().aoraton_connect,
                       reauth ? c().aoraton_reauth_detail : c().aoraton_sign_in_help);
            auto* connect = pill(content, c().aoraton_sign_in, ui::ButtonTone::Primary, kGutter, kActionY, kWidth,
                                 pairing_start_event);
            ui::set_button_disabled(connect, !callbacks.on_aoraton_pair_start);
            return;
        }

        char expiry[32] = {};
        if (aoraton.pairing_seconds_remaining > 0) {
            std::snprintf(expiry, sizeof(expiry), "%us", static_cast<unsigned>(aoraton.pairing_seconds_remaining));
        }
        if (state == AoratonPairingState::AwaitingSasConfirmation) {
            label(content, t().aoraton_pair_sas, kGutter, 100, kWidth, ui::font_caption(),
                  ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            auto* sas_card = lv_obj_create(content);
            ui::reset(sas_card);
            ui::style_platter(sas_card, ui::tokens::lilac, ui::tokens::radius_large, true);
            lv_obj_set_size(sas_card, kWidth, 112);
            lv_obj_set_pos(sas_card, kGutter, 164);
            auto* code = label(sas_card, aoraton.pairing_sas.c_str(), 0, 0, kWidth - 36, ui::font_display(),
                               ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            lv_obj_align(code, LV_ALIGN_CENTER, 0, 0);
            if (expiry[0]) line(content, expiry, kGutter, 292, kWidth, ui::font_caption(), ui::tokens::text_tertiary,
                                LV_TEXT_ALIGN_CENTER);
            pill(content, t().cancel, ui::ButtonTone::Danger, kGutter, kActionY, 160, pairing_cancel_event);
            pill(content, t().aoraton_pair_confirm, ui::ButtonTone::Primary, kGutter + 176, kActionY, kWidth - 176,
                 pairing_confirm_sas_event);
            return;
        }
        if (state == AoratonPairingState::Expired || state == AoratonPairingState::Error) {
            const bool timed_out = state == AoratonPairingState::Expired || aoraton.last_error == "SIGN_IN_TIMEOUT";
            state_symbol(timed_out ? 80 : 100, 96, ui::IconRole::Error, ui::tokens::coral);
            const bool backend_missing = aoraton.last_error == "PAIRING_TEST_BACKEND_NOT_CONFIGURED";
            auto* column = state_text(timed_out ? 188 : 212,
                                      backend_missing ? t().aoraton_pair_failed : sign_in_error_title(),
                                      backend_missing ? t().aoraton_pair_backend_missing :
                                      (timed_out ? c().aoraton_timeout_detail : nullptr));
            (void)column;
            pill(content, t().retry, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth, pairing_start_event);
            pill(content, t().cancel, ui::ButtonTone::Danger, kGutter + kHalfWidth + 16, kActionY, kHalfWidth,
                 pairing_cancel_event);
            return;
        }
        if (!aoraton.pairing_qr_payload.empty()) {
            label(content, t().aoraton_pair_scan, kGutter, 96, kWidth, ui::font_caption(),
                  ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            auto* qr = lv_qrcode_create(content);
            lv_qrcode_set_size(qr, 184);
            lv_qrcode_set_dark_color(qr, lv_color_black());
            lv_qrcode_set_light_color(qr, lv_color_white());
            lv_qrcode_set_quiet_zone(qr, true);
            lv_qrcode_set_data(qr, aoraton.pairing_qr_payload.c_str());
            lv_obj_set_pos(qr, (ui::tokens::display_width - 184) / 2, 156);
            std::string fallback;
            if (!aoraton.pairing_code.empty()) fallback = std::string(t().aoraton_pair_code) + ": " + aoraton.pairing_code;
            if (expiry[0]) fallback += (fallback.empty() ? "" : " · ") + std::string(expiry);
            if (!fallback.empty()) {
                line(content, fallback.c_str(), kGutter, 346, kWidth, ui::font_caption(), ui::tokens::text,
                     LV_TEXT_ALIGN_CENTER);
            }
        } else {
            auto* spinner = lv_spinner_create(content);
            lv_obj_set_size(spinner, 96, 96);
            lv_obj_set_pos(spinner, (ui::tokens::display_width - 96) / 2, 120);
            lv_obj_set_style_arc_color(spinner, ui::color(ui::tokens::indigo), LV_PART_INDICATOR);
            label(content, c().aoraton_sign_in_waiting, kGutter, 240, kWidth, ui::font_body(),
                  ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            if (expiry[0]) line(content, expiry, kGutter, 312, kWidth, ui::font_caption(), ui::tokens::text_tertiary,
                                LV_TEXT_ALIGN_CENTER);
        }
        pill(content, t().cancel, ui::ButtonTone::Danger, kGutter, kActionY, kWidth, pairing_cancel_event);
    }

    const AoratonRoom* selected_room() const {
        for (const auto& room : aoraton.rooms) {
            if (room.id == aoraton.selected_room_id) return &room;
        }
        return aoraton.rooms.empty() ? nullptr : &aoraton.rooms.front();
    }

    // Outgoing delivery stage; nothing claims "sent" before the transport says so.
    const char* delivery_text(AoratonDelivery delivery, std::uint32_t& color) const {
        switch (delivery) {
            case AoratonDelivery::Queued:
            case AoratonDelivery::Prepared: return t().delivery_queued;
            case AoratonDelivery::Sending:
            case AoratonDelivery::Submitted: return t().delivery_submitted;
            case AoratonDelivery::Acknowledged: return t().delivery_acknowledged;
            case AoratonDelivery::Sent: return aoraton.mock_mode ? t().delivery_demo : t().delivery_acknowledged;
            case AoratonDelivery::Failed: color = ui::tokens::coral; return t().delivery_failed;
            case AoratonDelivery::Uncertain: color = ui::tokens::amber; return t().delivery_uncertain;
            case AoratonDelivery::Received: break;
        }
        return nullptr;
    }

    void render_aoraton_conversation() {
        const auto* room = selected_room();
        make_header(room ? room->display_name.c_str() : t().aoraton);
        line(content, security_text(), kGutter + 4, 98, kWidth - 8, ui::font_caption(), ui::tokens::mint);
        auto* thread = lv_obj_create(content);
        ui::reset(thread);
        lv_obj_set_size(thread, kWidth, 240);
        lv_obj_set_pos(thread, kGutter, 132);
        lv_obj_set_clickable(thread, true);
        lv_obj_set_scrollable(thread, true);
        lv_obj_set_scroll_dir(thread, LV_DIR_VER);
        lv_obj_set_overflow_visible(thread, false);
        ui::style_scrollbar(thread);
        // Bubbles size to their text (messages may be up to 1500 bytes) and stack
        // in a column; each line container places incoming left, outgoing right.
        lv_obj_set_layout(thread, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(thread, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(thread, 10, 0);
        bool any = false;
        lv_obj_t* last_line = nullptr;
        if (room) {
            // Build only the newest messages: each bubble is several LVGL
            // objects, and the service already owns the bounded history.
            std::size_t in_room = 0;
            for (const auto& message : aoraton.messages) in_room += message.room_id == room->id ? 1U : 0U;
            std::size_t skip = in_room > kConversationRenderLimit ? in_room - kConversationRenderLimit : 0;
            for (const auto& message : aoraton.messages) {
                if (message.room_id != room->id) continue;
                if (skip > 0) {
                    --skip;
                    continue;
                }
                any = true;
                const bool incoming = message.direction == AoratonDirection::Incoming;
                auto* row = lv_obj_create(thread);
                last_line = row;
                ui::reset(row);
                lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
                lv_obj_set_layout(row, LV_LAYOUT_FLEX);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, incoming ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_END,
                                      LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                auto* bubble = lv_obj_create(row);
                ui::reset(bubble);
                lv_obj_set_style_bg_color(bubble, ui::color(incoming ? ui::tokens::surface : 0x173A6B), 0);
                lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(bubble, ui::tokens::radius_medium, 0);
                lv_obj_set_style_pad_hor(bubble, 16, 0);
                lv_obj_set_style_pad_ver(bubble, 12, 0);
                lv_obj_set_size(bubble, 352, LV_SIZE_CONTENT);
                lv_obj_set_layout(bubble, LV_LAYOUT_FLEX);
                lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_style_pad_row(bubble, 4, 0);
                auto* text = ui::make_label(bubble, message.local_display_text.c_str(), ui::tokens::text,
                                            ui::font_body());
                lv_obj_set_width(text, lv_pct(100));
                std::uint32_t status_color = ui::tokens::text_tertiary;
                if (const char* status_text = delivery_text(message.delivery, status_color)) {
                    auto* state = ui::make_label(bubble, status_text, status_color, ui::font_caption());
                    lv_obj_set_width(state, lv_pct(100));
                    lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_RIGHT, 0);
                }
            }
        }
        if (!any) label(thread, t().no_messages, 0, 20, kWidth, ui::font_body(), ui::tokens::text_secondary);
        // A conversation opens at its newest message, like every chat client.
        if (last_line) {
            lv_obj_update_layout(thread);
            lv_obj_scroll_to_view(last_line, LV_ANIM_OFF);
        }
        // A reply bar, like every chat client, opens the composer.
        auto* reply = lv_button_create(content);
        ui::reset(reply);
        lv_obj_set_clickable(reply, true);
        ui::style_platter(reply, ui::tokens::blue, LV_RADIUS_CIRCLE, false);
        lv_obj_set_size(reply, kWidth, 72);
        lv_obj_set_pos(reply, kGutter, 388);
        lv_obj_set_style_pad_hor(reply, 24, 0);
        lv_obj_set_style_pad_ver(reply, 0, 0);
        lv_obj_set_layout(reply, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(reply, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(reply, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(reply, 14, 0);
        icon_view(reply, ui::IconRole::Compose, 30, ui::tokens::blue);
        auto* hint = ui::make_label(reply, t().message_placeholder, ui::tokens::text_tertiary, ui::font_body());
        lv_obj_set_flex_grow(hint, 1);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
        lv_obj_set_height(hint, lv_font_get_line_height(ui::font_body()));
        lv_obj_add_event_cb(reply, compose_event, LV_EVENT_CLICKED, this);
    }

    void render_aoraton_compose() {
        make_header(t().compose, ui::IconRole::Send, prepare_message_event, ui::tokens::blue,
                    static_cast<bool>(callbacks.on_aoraton_prepare));
        compose_area = make_text_input(kGutter, 100, kWidth, 72, t().message_placeholder, false);
        if (!compose_draft.empty()) lv_textarea_set_text(compose_area, compose_draft.c_str());
        lv_obj_add_event_cb(compose_area, compose_changed_event, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(compose_area, compose_ready_event, LV_EVENT_READY, this);
        compose_status_label = line(content, "", kGutter + 4, 176, 240, ui::font_caption(), ui::tokens::text_tertiary);
        update_compose_status();
        const char* mode = aoraton.runtime_mode == AoratonRuntimeMode::Mock ? t().demo :
            (aoraton.runtime_mode == AoratonRuntimeMode::IntegrationTest ? t().secure_test : t().encrypted);
        auto* tag = ui::make_tag(content, mode, aoraton.runtime_mode == AoratonRuntimeMode::Live ?
                                 ui::tokens::mint : ui::tokens::amber);
        lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, -kGutter, 176);
        make_form_keyboard(compose_area, ui::KeyboardProfile::Text);
    }

    void render_aoraton_confirm() {
        const auto* room = selected_room();
        make_header(v().confirm_short);
        label(content, t().confirm_hint, kGutter + 4, 98, kWidth - 8, ui::font_caption(), ui::tokens::text_secondary);
        auto* card = lv_obj_create(content);
        ui::reset(card);
        ui::style_platter(card, ui::tokens::lilac, ui::tokens::radius_large, true);
        lv_obj_set_size(card, kWidth, 208);
        lv_obj_set_pos(card, kGutter, 158);
        lv_obj_set_style_pad_all(card, 20, 0);
        line(card, room ? room->display_name.c_str() : t().aoraton, 0, 0, kWidth - 44, ui::font_headline(),
             ui::tokens::lilac);
        auto* body = label(card, aoraton.pending_text.c_str(), 0, 44, kWidth - 44, ui::font_body(), ui::tokens::text);
        lv_obj_set_height(body, 92);
        lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
        line(card, aoraton.runtime_mode == AoratonRuntimeMode::Mock ? t().delivery_demo : security_text(),
             0, 140, kWidth - 44, ui::font_caption(), ui::tokens::text_tertiary);
        pill(content, t().cancel, ui::ButtonTone::Danger, kGutter, kActionY, 160, cancel_message_event);
        pill(content, v().send_action, ui::ButtonTone::Primary, kGutter + 176, kActionY, kWidth - 176,
             confirm_message_event);
    }

    void render_weather_state(WeatherState state) {
        make_header(v().weather);
        const bool no_city = state == WeatherState::Unconfigured;
        const bool offline = state == WeatherState::Offline;
        const bool loading = state == WeatherState::Loading;
        const std::uint32_t accent = no_city ? ui::tokens::lilac :
            (offline ? ui::tokens::magenta : (loading ? ui::tokens::ice : ui::tokens::coral));
        weather_halo = make_dot(content, 104, accent, kTint);
        lv_obj_set_pos(weather_halo, (ui::tokens::display_width - 104) / 2, 104);
        if (loading) {
            weather_spinner = spinner_arc(weather_halo, 88, accent, 5);
            lv_obj_center(weather_spinner);
            auto* icon = icon_view(weather_halo, ui::IconRole::Weather, 40, accent);
            lv_obj_center(icon);
        } else {
            const auto role = no_city ? ui::IconRole::Location :
                (offline ? ui::IconRole::Offline : ui::IconRole::Error);
            auto* icon = icon_view(weather_halo, role, 52, accent);
            lv_obj_center(icon);
        }
        const char* title = no_city ? v().weather_no_city_title :
            (offline ? v().weather_offline_title :
             (loading ? v().weather_loading : v().weather_error_title));
        const char* detail = no_city ? v().weather_no_city_detail :
            (offline ? v().weather_offline_detail :
             (loading ? (weather.has_saved_location ? weather.saved_location.name.c_str() : v().weather_source) :
              (weather.error_code == "CITY_NOT_FOUND" ? v().weather_not_found : v().weather_error_detail)));
        state_text(224, title, detail);
        if (!loading) {
            const bool choose = no_city || weather.error_code == "CITY_NOT_FOUND";
            pill(content, choose ? v().weather_choose_city : v().weather_retry, ui::ButtonTone::Primary,
                 kGutter, kActionY, kWidth, choose ? weather_change_city_event : weather_refresh_event);
        }
    }

    // A horizontally scrolling strip inside the weather scroller.
    lv_obj_t* weather_strip(int y, int height) {
        auto* strip = lv_obj_create(weather_scroll);
        ui::reset(strip);
        lv_obj_set_size(strip, ui::tokens::display_width, height);
        lv_obj_set_pos(strip, 0, y);
        lv_obj_set_clickable(strip, true);
        lv_obj_set_scrollable(strip, true);
        lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
        lv_obj_set_scrollbar_mode(strip, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_elastic(strip, true);
        lv_obj_set_overflow_visible(strip, false);
        lv_obj_set_style_clip_corner(strip, true, 0);
        return strip;
    }

    // Lets the last card of a strip scroll fully into view with a gutter.
    void strip_end(lv_obj_t* strip, int x) {
        auto* spacer = lv_obj_create(strip);
        ui::reset(spacer);
        lv_obj_set_size(spacer, kGutter, 1);
        lv_obj_set_pos(spacer, x, 0);
    }

    lv_obj_t* weather_card(lv_obj_t* strip, int x, int width, int height, std::uint32_t accent, bool button) {
        auto* card = button ? lv_button_create(strip) : lv_obj_create(strip);
        ui::reset(card);
        ui::style_platter(card, accent, ui::tokens::radius_medium, false);
        lv_obj_set_style_pad_all(card, 14, 0);
        lv_obj_set_size(card, width, height);
        lv_obj_set_pos(card, x, 4);
        lv_obj_set_clickable(card, button);
        return card;
    }

    void render_weather_ready(bool refreshing) {
        make_header(v().weather, ui::IconRole::Refresh, weather_refresh_event, ui::tokens::ice,
                    static_cast<bool>(callbacks.on_weather_refresh));
        const auto& data = weather.data;
        const auto accent = weather_condition_accent(data.condition);

        weather_scroll = lv_obj_create(content);
        ui::reset(weather_scroll);
        lv_obj_set_size(weather_scroll, ui::tokens::display_width, ui::tokens::display_height - kHeader);
        lv_obj_set_pos(weather_scroll, 0, kHeader);
        lv_obj_set_clickable(weather_scroll, true);
        lv_obj_set_scrollable(weather_scroll, true);
        lv_obj_set_scroll_dir(weather_scroll, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(weather_scroll, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_elastic(weather_scroll, true);
        lv_obj_set_overflow_visible(weather_scroll, false);
        lv_obj_set_style_clip_corner(weather_scroll, true, 0);
        ui::style_scrollbar(weather_scroll);
        primary_scroller = weather_scroll;

        // The city line opens the city picker.
        auto* city = lv_button_create(weather_scroll);
        ui::reset(city);
        lv_obj_set_clickable(city, true);
        lv_obj_set_size(city, LV_SIZE_CONTENT, 48);
        lv_obj_set_pos(city, 12, 0);
        lv_obj_set_style_pad_hor(city, 12, 0);
        lv_obj_set_style_radius(city, 24, 0);
        lv_obj_set_style_bg_color(city, ui::color(ui::tokens::surface_high), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(city, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_layout(city, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(city, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(city, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(city, 8, 0);
        icon_view(city, ui::IconRole::Location, 26, ui::tokens::ice);
        auto* name = ui::make_label(city, data.location.name.c_str(), ui::tokens::text, ui::font_headline());
        lv_obj_set_style_max_width(name, 230, 0);
        lv_obj_set_height(name, lv_font_get_line_height(ui::font_headline()));
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        icon_view(city, ui::IconRole::Edit, 22, ui::tokens::text_secondary);
        lv_obj_add_event_cb(city, weather_change_city_event, LV_EVENT_CLICKED, this);

        const bool demo = data.origin == WeatherDataOrigin::Demo;
        const bool cached = data.origin == WeatherDataOrigin::Cache || data.stale;
        if (demo || cached || refreshing) {
            auto* badge = ui::make_tag(weather_scroll,
                demo ? v().demo_badge : (refreshing ? v().weather_refreshing : v().weather_cached),
                demo ? ui::tokens::amber : (refreshing ? ui::tokens::ice : ui::tokens::magenta));
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -kGutter, 10);
        }

        char temperature[24];
        std::snprintf(temperature, sizeof(temperature), "%.0f°C", data.temperature_c);
        label(weather_scroll, temperature, kGutter, 60, 240, ui::font_display(), ui::tokens::text);
        line(weather_scroll, weather_condition_text(data.condition), kGutter + 4, 134, 236, ui::font_headline(), accent);
        char range[64];
        std::snprintf(range, sizeof(range), "↓ %.0f°   ↑ %.0f°", data.daily_minimum_c, data.daily_maximum_c);
        line(weather_scroll, range, kGutter + 4, 172, 236, ui::font_body(), ui::tokens::text_secondary);
        const auto observed_time = weather_time_label(data.observed_at);
        const char* provider_name = data.provider == WeatherProvider::TlesDetector ?
            v().weather_tlesdetector_source : v().weather_source;
        const std::string updated = (demo ? std::string{} : std::string(provider_name) + " · ") +
            v().weather_updated + " " + observed_time;
        label(weather_scroll, updated.c_str(), kGutter + 4, 208, 236, ui::font_caption(), ui::tokens::text_tertiary);

        weather_scene = lv_obj_create(weather_scroll);
        ui::reset(weather_scene);
        lv_obj_set_size(weather_scene, 190, 190);
        lv_obj_set_pos(weather_scene, 266, 40);
        lv_obj_set_overflow_visible(weather_scene, false);
        lv_obj_set_style_clip_corner(weather_scene, true, 0);
        weather_halo = make_dot(weather_scene, 138, accent, LV_OPA_20);
        lv_obj_set_pos(weather_halo, kWeatherHaloCenterX - 69, kWeatherHaloCenterY - 69);
        // The halo breathes and the sun turns about their own centres.
        lv_obj_set_style_transform_pivot_x(weather_halo, 69, 0);
        lv_obj_set_style_transform_pivot_y(weather_halo, 69, 0);

        const bool night = !data.is_day;
        const bool precipitation = data.condition == WeatherCondition::Rain ||
            data.condition == WeatherCondition::Drizzle || data.condition == WeatherCondition::Thunderstorm;
        const bool snow = data.condition == WeatherCondition::Snow;
        const bool wind = data.condition == WeatherCondition::Windy;
        if (night && data.condition == WeatherCondition::Clear) {
            weather_icon = make_dot(weather_scene, 64, ui::tokens::ice, LV_OPA_COVER);
            lv_obj_set_pos(weather_icon, 62, 45);
            weather_moon_cutout = make_dot(weather_scene, 56, ui::tokens::canvas, LV_OPA_COVER);
            lv_obj_set_pos(weather_moon_cutout, 82, 38);
            for (std::size_t index = 0; index < weather_stars.size(); ++index) {
                weather_stars[index] = make_dot(weather_scene, index % 2 == 0 ? 4 : 3,
                                                ui::tokens::ice, LV_OPA_80);
                lv_obj_set_pos(weather_stars[index], 20 + static_cast<int>((index * 29) % 145),
                               20 + static_cast<int>((index * 37) % 116));
            }
        } else {
            const int size = data.condition == WeatherCondition::Clear ? 82 : 76;
            weather_icon = icon_view(weather_scene, weather_condition_icon(data.condition), size, accent);
            lv_obj_set_pos(weather_icon, kWeatherHaloCenterX - size / 2, kWeatherHaloCenterY - size / 2);
            lv_obj_set_style_transform_pivot_x(weather_icon, size / 2, 0);
            lv_obj_set_style_transform_pivot_y(weather_icon, size / 2, 0);
        }
        if (data.condition == WeatherCondition::Cloudy || data.condition == WeatherCondition::PartlyCloudy ||
            data.condition == WeatherCondition::Fog || precipitation || snow) {
            const std::size_t layer_count = data.condition == WeatherCondition::Cloudy ? 3U : 2U;
            for (std::size_t index = 0; index < layer_count; ++index) {
                weather_cloud_layers[index] = icon_view(weather_scene, ui::IconRole::Cloud,
                    52 - static_cast<int>(index) * 6,
                    index == 0 ? ui::tokens::ice : ui::tokens::text_secondary);
                lv_obj_set_pos(weather_cloud_layers[index], 40 + static_cast<int>(index) * 54,
                               80 + static_cast<int>(index % 2) * 18);
                lv_obj_set_style_opa(weather_cloud_layers[index],
                    static_cast<lv_opa_t>(190 - index * 35), 0);
            }
        }
        if (data.condition == WeatherCondition::Fog) {
            for (std::size_t index = 0; index < weather_mist_layers.size(); ++index) {
                weather_mist_layers[index] = glow_shape(weather_scene, 112 - static_cast<int>(index) * 14,
                    3, 38 + static_cast<int>(index) * 7, 126 + static_cast<int>(index) * 13,
                    ui::tokens::ice, static_cast<lv_opa_t>(110 - index * 18), LV_RADIUS_CIRCLE);
            }
        }
        if (precipitation || snow || wind) {
            const std::size_t count = data.condition == WeatherCondition::Drizzle ? 6U : weather_particles.size();
            const bool heavy = data.weather_code == 55 || data.weather_code == 65 || data.weather_code == 67 ||
                               data.weather_code == 75 || data.weather_code == 82;
            for (std::size_t index = 0; index < count; ++index) {
                auto* particle = lv_obj_create(weather_scene);
                ui::reset(particle);
                const int width = wind ? 22 : (snow ? (heavy ? 7 : 5) : (heavy ? 4 : 3));
                const int height = wind ? 3 : (snow ? width : (heavy ? 17 : 11));
                lv_obj_set_size(particle, width, height);
                lv_obj_set_pos(particle, 26 + static_cast<int>(index % 6) * 25,
                               118 + static_cast<int>(index / 6) * 18);
                lv_obj_set_style_radius(particle, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(particle, ui::color(snow ? ui::tokens::ice : accent), 0);
                lv_obj_set_style_bg_opa(particle, heavy ? LV_OPA_90 : LV_OPA_60, 0);
                weather_particles[index] = particle;
            }
        }

        // Metrics: icon, label and value per card.
        auto* metrics = weather_strip(268, 128);
        struct MetricCard {
            ui::IconRole icon{ui::IconRole::Information};
            const char* title{};
            std::uint32_t accent{ui::tokens::ice};
            std::array<char, 28> value{};
        };
        std::array<MetricCard, 9> metric_cards{};
        std::size_t metric_count = 0;
        const auto add_metric = [&](ui::IconRole icon, const char* title, std::uint32_t color,
                                    const char* format, auto value) {
            if (metric_count >= metric_cards.size()) return;
            auto& card = metric_cards[metric_count++];
            card.icon = icon;
            card.title = title;
            card.accent = color;
            std::snprintf(card.value.data(), card.value.size(), format, value);
        };
        add_metric(ui::IconRole::Droplet, v().humidity, ui::tokens::ice, "%d%%", data.humidity_percent);
        add_metric(ui::IconRole::Wind, v().wind, ui::tokens::mint, "%.0f km/h", data.wind_kmh);
        add_metric(ui::IconRole::Thermometer, v().feels_like, ui::tokens::amber,
                   "%.0f°C", data.apparent_temperature_c);
        add_metric(ui::IconRole::CloudRain, v().weather_rain, ui::tokens::blue,
                   "%d%%", data.rain_probability_percent);
        if (data.metrics.has(WeatherMetric::Pressure)) {
            add_metric(ui::IconRole::Information, v().weather_pressure, ui::tokens::lilac,
                       "%.0f hPa", data.metrics.pressure_hpa);
        }
        if (data.metrics.has(WeatherMetric::WindGust)) {
            add_metric(ui::IconRole::Wind, v().weather_gusts, ui::tokens::mint,
                       "%.0f km/h", data.metrics.wind_gust_kmh);
        }
        if (data.metrics.has(WeatherMetric::Visibility)) {
            add_metric(ui::IconRole::Visibility, v().weather_visibility, ui::tokens::ice,
                       "%.1f km", data.metrics.visibility_km);
        }
        if (data.metrics.has(WeatherMetric::UvIndex)) {
            add_metric(ui::IconRole::Sun, v().weather_uv, ui::tokens::amber,
                       "%.1f", data.metrics.uv_index);
        }
        if (data.metrics.has(WeatherMetric::AirQuality)) {
            add_metric(ui::IconRole::Cloud, v().weather_air_quality, ui::tokens::blue,
                       "%d AQI", data.metrics.european_aqi);
        }
        for (std::size_t index = 0; index < metric_count; ++index) {
            const auto& metric = metric_cards[index];
            auto* card = weather_card(metrics, kGutter + static_cast<int>(index) * 164, 152, 120, metric.accent, false);
            auto* metric_icon = icon_view(card, metric.icon, 28, metric.accent);
            lv_obj_set_pos(metric_icon, 0, 0);
            line(card, metric.title, 36, 0, 88, ui::font_caption(), ui::tokens::text_secondary);
            line(card, metric.value.data(), 0, 52, 124, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        }
        strip_end(metrics, kGutter + static_cast<int>(metric_count) * 164);

        const bool show_hazards = data.provider == WeatherProvider::TlesDetector;
        const int offset = show_hazards ? 188 : 0;
        if (show_hazards) {
            line(weather_scroll, v().weather_hazards, kGutter, 408, kWidth, ui::font_headline(), ui::tokens::text);
            const bool urgent = data.hazards.nearby_alert_count > 0 || data.hazards.lightning_observed;
            const auto hazard_accent = urgent ? ui::tokens::amber : ui::tokens::ice;
            auto* hazard_card = lv_obj_create(weather_scroll);
            ui::reset(hazard_card);
            ui::style_platter(hazard_card, hazard_accent, ui::tokens::radius_medium, urgent);
            lv_obj_set_size(hazard_card, kWidth, 132);
            lv_obj_set_pos(hazard_card, kGutter, 448);
            auto* warning_icon = icon_view(hazard_card, ui::IconRole::Warning, 28, hazard_accent);
            lv_obj_set_pos(warning_icon, 0, 6);
            char alert_line[96];
            if (!data.hazards.alerts_available) {
                std::snprintf(alert_line, sizeof(alert_line), "%s", v().weather_alerts_unavailable);
            } else if (data.hazards.nearby_alert_count > 0) {
                std::snprintf(alert_line, sizeof(alert_line), v().weather_nearby_alerts_format,
                              data.hazards.nearby_alert_count);
            } else if (data.hazards.active_alert_count > 0) {
                std::snprintf(alert_line, sizeof(alert_line), v().weather_active_alerts_format,
                              data.hazards.active_alert_count);
            } else {
                std::snprintf(alert_line, sizeof(alert_line), "%s", v().weather_no_active_alerts);
            }
            line(hazard_card, alert_line, 40, 4, kWidth - 72, ui::font_body(), ui::tokens::text);
            auto* lightning_icon = icon_view(hazard_card, ui::IconRole::CloudLightning, 28,
                                             data.hazards.lightning_observed ? ui::tokens::magenta : ui::tokens::blue);
            lv_obj_set_pos(lightning_icon, 0, 56);
            char lightning_line[112];
            if (!data.hazards.lightning_available) {
                std::snprintf(lightning_line, sizeof(lightning_line), "%s", v().weather_lightning_unavailable);
            } else if (!data.hazards.lightning_observed) {
                std::snprintf(lightning_line, sizeof(lightning_line), "%s", v().weather_no_nearby_lightning);
            } else if (data.hazards.xweather_available && data.hazards.max_positive_ka > 0) {
                std::snprintf(lightning_line, sizeof(lightning_line), v().weather_nearby_lightning_cg_format,
                              data.hazards.nearest_lightning_km, data.hazards.max_positive_ka);
            } else {
                std::snprintf(lightning_line, sizeof(lightning_line), v().weather_nearby_lightning_format,
                              data.hazards.nearest_lightning_km, data.hazards.lightning_age_minutes);
            }
            label(hazard_card, lightning_line, 40, 54, kWidth - 72, ui::font_caption(), ui::tokens::text);
        }

        line(weather_scroll, v().weather_hourly, kGutter, 408 + offset, kWidth, ui::font_headline(), ui::tokens::text);
        auto* hourly_strip = weather_strip(448 + offset, 172);
        const std::size_t hourly_count = std::min<std::size_t>(data.hourly_count, weather_hour_cards.size());
        for (std::size_t index = 0; index < hourly_count; ++index) {
            const auto& hour = data.hourly[index];
            const auto hour_accent = weather_condition_accent(hour.condition);
            auto* card = weather_card(hourly_strip, kGutter + static_cast<int>(index) * 132, 120, 164,
                                      hour_accent, true);
            const auto time = weather_time_label(hour.time);
            line(card, time.c_str(), 0, 0, 92, ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            auto* hour_icon = icon_view(card, weather_condition_icon(hour.condition), 40, hour_accent);
            lv_obj_set_pos(hour_icon, 26, 32);
            char value[16];
            std::snprintf(value, sizeof(value), "%.0f°", hour.temperature_c);
            line(card, value, 0, 74, 92, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            char chance[16];
            std::snprintf(chance, sizeof(chance), "%d%%", hour.rain_probability_percent);
            line(card, chance, 0, 108, 92, ui::font_caption(), ui::tokens::ice, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_user_data(card, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_add_event_cb(card, weather_hour_event, LV_EVENT_CLICKED, this);
            weather_hour_cards[index] = card;
        }
        strip_end(hourly_strip, kGutter + static_cast<int>(hourly_count) * 132);

        auto* detail_card = lv_obj_create(weather_scroll);
        ui::reset(detail_card);
        ui::style_platter(detail_card, ui::tokens::blue, ui::tokens::radius_medium, false);
        lv_obj_set_size(detail_card, kWidth, 100);
        lv_obj_set_pos(detail_card, kGutter, 628 + offset);
        auto* detail_icon = icon_view(detail_card, ui::IconRole::Information, 28, ui::tokens::ice);
        lv_obj_set_pos(detail_icon, 0, 4);
        weather_detail_label = label(detail_card, "", 40, 0, kWidth - 72, ui::font_body(), ui::tokens::text);

        line(weather_scroll, v().weather_daily, kGutter, 744 + offset, kWidth, ui::font_headline(), ui::tokens::text);
        auto* daily_strip = weather_strip(784 + offset, 172);
        const std::size_t daily_count = std::min<std::size_t>(data.daily_count, weather_day_cards.size());
        for (std::size_t index = 0; index < daily_count; ++index) {
            const auto& day_item = data.daily[index];
            const auto day_accent = weather_condition_accent(day_item.condition);
            auto* card = weather_card(daily_strip, kGutter + static_cast<int>(index) * 164, 152, 164,
                                      day_accent, true);
            const auto day = weather_day_label(day_item.date, index);
            line(card, day.c_str(), 0, 0, 124, ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            auto* day_icon = icon_view(card, weather_condition_icon(day_item.condition), 40, day_accent);
            lv_obj_set_pos(day_icon, 42, 32);
            char value[28];
            std::snprintf(value, sizeof(value), "%.0f / %.0f°", day_item.minimum_c, day_item.maximum_c);
            line(card, value, 0, 74, 124, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            char chance[16];
            std::snprintf(chance, sizeof(chance), "%d%%", day_item.rain_probability_max_percent);
            line(card, chance, 0, 108, 124, ui::font_caption(), ui::tokens::ice, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_user_data(card, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_add_event_cb(card, weather_day_event, LV_EVENT_CLICKED, this);
            weather_day_cards[index] = card;
        }
        strip_end(daily_strip, kGutter + static_cast<int>(daily_count) * 164);
        list_end(weather_scroll, 956 + offset, 24);
        if (hourly_count > 0) set_weather_detail(false, 0);
    }

    void render_weather() {
        if (weather.state == WeatherState::Ready && weather.has_data) {
            render_weather_ready(false);
        } else if (weather.state == WeatherState::Loading && weather.has_data) {
            render_weather_ready(true);
        } else {
            render_weather_state(weather.state);
        }
    }

    void render_weather_location() {
        if (weather.state == WeatherState::Searching) {
            make_header(v().weather_choose_city);
            weather_halo = make_dot(content, 104, ui::tokens::ice, kTint);
            lv_obj_set_pos(weather_halo, (ui::tokens::display_width - 104) / 2, 120);
            weather_spinner = spinner_arc(weather_halo, 88, ui::tokens::ice, 5);
            lv_obj_center(weather_spinner);
            auto* search_icon = icon_view(weather_halo, ui::IconRole::Search, 40, ui::tokens::ice);
            lv_obj_center(search_icon);
            state_text(244, v().weather_searching, weather.search_query.c_str());
            return;
        }
        if (weather.state == WeatherState::SearchResults && weather.suggestion_count > 0) {
            make_header(v().weather_choose_city);
            line(content, v().weather_results, kGutter + 4, 98, kWidth - 8, ui::font_caption(),
                 ui::tokens::text_secondary);
            auto* list = make_list(132, ui::tokens::display_height - 132);
            const std::size_t visible = std::min<std::size_t>(weather.suggestion_count, 5);
            int y = 0;
            for (std::size_t index = 0; index < visible; ++index) {
                const auto& location = weather.suggestions[index];
                std::string detail = location.region;
                if (!detail.empty() && !location.country.empty()) detail += " · ";
                detail += location.country;
                auto* row = list_row(list, y, location.name.c_str(), detail.empty() ? nullptr : detail.c_str(),
                                     ui::IconRole::Location,
                                     index == 0 ? ui::tokens::ice : kIconAccents[index % kIconAccents.size()]);
                add_chevron(row);
                lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
                lv_obj_set_clickable(row, true);
                lv_obj_add_event_cb(row, weather_result_event, LV_EVENT_CLICKED, this);
                y += kRowPitch;
            }
            list_end(list, y);
            return;
        }

        if (weather_typing) {
            // Search failures are announced with a toast (see apply_weather) so
            // the form keeps its full height for the keyboard.
            make_header(v().weather_choose_city, ui::IconRole::Search, weather_search_event, ui::tokens::blue,
                        static_cast<bool>(callbacks.on_weather_search));
            weather_search_area = make_text_input(kGutter, 100, kWidth, 72, v().weather_search_hint, true);
            lv_textarea_set_max_length(weather_search_area, 80);
            // A refresh (e.g. a finished background fetch) keeps what was typed.
            const std::string& query = !weather_query_draft.empty() ? weather_query_draft : weather.search_query;
            if (!query.empty()) lv_textarea_set_text(weather_search_area, query.c_str());
            lv_obj_add_event_cb(weather_search_area, weather_query_changed_event, LV_EVENT_VALUE_CHANGED, this);
            lv_obj_add_event_cb(weather_search_area, weather_search_ready_event, LV_EVENT_READY, this);
            make_form_keyboard(weather_search_area, ui::KeyboardProfile::Text, 184);
            return;
        }

        make_header(v().weather_choose_city);
        // The search field opens the keyboard; presets need a single tap.
        auto* field = lv_button_create(content);
        ui::reset(field);
        lv_obj_set_clickable(field, true);
        ui::style_platter(field, ui::tokens::blue, ui::tokens::radius_medium, false);
        lv_obj_set_size(field, kWidth, 72);
        lv_obj_set_pos(field, kGutter, 104);
        lv_obj_set_style_pad_hor(field, 20, 0);
        lv_obj_set_style_pad_ver(field, 0, 0);
        lv_obj_set_layout(field, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(field, 14, 0);
        icon_view(field, ui::IconRole::Search, 30, ui::tokens::text_secondary);
        const bool drafted = !weather_query_draft.empty();
        auto* hint = ui::make_label(field, drafted ? weather_query_draft.c_str() : v().weather_search_hint,
                                    drafted ? ui::tokens::text : ui::tokens::text_tertiary, ui::font_body());
        lv_obj_set_flex_grow(hint, 1);
        lv_obj_set_height(hint, lv_font_get_line_height(ui::font_body()));
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(field, weather_field_event, LV_EVENT_CLICKED, this);

        line(content, v().weather_quick_choices, kGutter + 4, 196, kWidth - 8, ui::font_caption(),
             ui::tokens::text_secondary);
        const char* cities[] = {v().weather_city_athens, v().weather_city_sparta,
                                v().weather_city_thessaloniki, v().weather_city_patras};
        for (int index = 0; index < 4; ++index) {
            auto* preset = lv_button_create(content);
            ui::reset(preset);
            lv_obj_set_clickable(preset, true);
            ui::style_platter(preset, ui::tokens::ice, ui::tokens::radius_large, false);
            lv_obj_set_size(preset, kHalfWidth, 80);
            lv_obj_set_pos(preset, kGutter + (index % 2) * (kHalfWidth + 16), 232 + (index / 2) * 88);
            lv_obj_set_style_pad_hor(preset, 12, 0);
            lv_obj_set_style_pad_ver(preset, 0, 0);
            auto* name = ui::make_label(preset, cities[index], ui::tokens::text, ui::font_headline());
            lv_obj_set_width(name, lv_pct(100));
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_obj_set_height(name, lv_font_get_line_height(ui::font_headline()));
            lv_obj_center(name);
            lv_obj_set_user_data(preset, const_cast<char*>(cities[index]));
            lv_obj_add_event_cb(preset, weather_preset_event, LV_EVENT_CLICKED, this);
        }
    }

    std::string wifi_summary() const {
        char buffer[160];
        switch (wifi.link) {
            case WifiLinkState::Unavailable: return t().wifi_unavailable;
            case WifiLinkState::Connecting:
                std::snprintf(buffer, sizeof(buffer), t().wifi_connecting_format,
                              wifi_display_name(wifi.target_ssid).c_str());
                return buffer;
            case WifiLinkState::Connected:
                return wifi.connected_ssid.empty() ? std::string(t().wifi_connected)
                                                   : wifi_display_name(wifi.connected_ssid);
            case WifiLinkState::Disconnected: break;
        }
        return t().wifi_disconnected;
    }

    void render_settings() {
        make_header(t().settings_title);
        auto* list = make_list();
        int y = 0;
        const int bars = wifi.link == WifiLinkState::Connected ? wifi_signal_bars(wifi.connected_rssi_dbm) : 0;
        const auto summary = wifi_summary();
        auto* wifi_row = list_row(list, y, t().wifi_title, summary.c_str(),
            bars > 0 ? wifi_bars_icon(bars) : ui::IconRole::WifiOff,
            bars > 0 ? ui::tokens::mint : ui::tokens::ice);
        add_chevron(wifi_row);
        lv_obj_set_clickable(wifi_row, true);
        lv_obj_add_event_cb(wifi_row, wifi_open_event, LV_EVENT_CLICKED, this);
        y += kRowPitch;

        const bool provider_available = static_cast<bool>(callbacks.on_provider_changed);
        auto* provider_row = list_row(list, y, t().provider,
            !provider_available ? t().provider_unavailable : (live_provider ? t().live_codex : t().demo_provider),
            ui::IconRole::Sparkles, ui::tokens::lilac, kRowHeight, provider_available ? 228 : 0);
        if (provider_available) {
            lv_obj_set_clickable(provider_row, true);
            lv_obj_add_event_cb(provider_row, provider_event, LV_EVENT_CLICKED, this);
            auto* badge = ui::make_tag(provider_row, live_provider ? t().live : t().demo,
                                      live_provider ? ui::tokens::mint : ui::tokens::amber);
            lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
        }
        y += kRowPitch;

        auto* motion_row = list_row(list, y, v().reduced_motion, v().reduced_motion_detail,
                                    ui::IconRole::Visibility, ui::tokens::indigo, kRowHeight, 252);
        lv_obj_set_clickable(motion_row, true);
        lv_obj_add_event_cb(motion_row, motion_event, LV_EVENT_CLICKED, this);
        auto* motion_switch = lv_switch_create(motion_row);
        lv_obj_set_size(motion_switch, 64, 36);
        lv_obj_align(motion_switch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(motion_switch, ui::color(ui::tokens::surface_glass), LV_PART_MAIN);
        lv_obj_set_style_bg_color(motion_switch, ui::color(ui::tokens::blue), kIndicatorChecked);
        lv_obj_set_style_bg_color(motion_switch, ui::color(ui::tokens::text), LV_PART_KNOB);
        lv_obj_set_clickable(motion_switch, false);
        if (reduced_motion) lv_obj_add_state(motion_switch, LV_STATE_CHECKED);
        y += kRowPitch;

        settings_row(list, y, v().weather,
            weather.has_saved_location ? weather.saved_location.name.c_str() : v().weather_choose_city,
            ui::IconRole::Weather, ui::tokens::amber, UiView::WeatherLocation);
        y += kRowPitch;
        settings_row(list, y, v().sound, nullptr, ui::IconRole::Audio, ui::tokens::pink, UiView::Audio);
        y += kRowPitch;
        settings_row(list, y, v().status_title, nullptr, ui::IconRole::Battery, ui::tokens::mint, UiView::Battery);
        y += kRowPitch;
        settings_row(list, y, v().system, nullptr, ui::IconRole::Cpu, ui::tokens::blue, UiView::System);
        y += kRowPitch;
        y = render_settings_companion(list, y);
        list_end(list, y, 8);
    }

    const char* wifi_security_text(WifiSecurity security) const {
        if (security == WifiSecurity::Open) return t().wifi_open;
        return wifi_security_supported(security) ? t().wifi_secured : t().wifi_unsupported;
    }

    // DEMO networks carry the label in their name; a desktop stand-in for a
    // real join says that the password was not checked.
    const char* wifi_link_note() const {
        if (wifi.link_origin == WifiOrigin::Host) return t().wifi_connected_host;
        if (wifi.link_origin == WifiOrigin::Simulated) return t().wifi_connected_simulated;
        return nullptr;
    }

    void render_wifi_status_card(lv_obj_t* parent, int y) {
        char title[160];
        std::string detail;
        std::uint32_t accent = ui::tokens::ice;
        ui::IconRole role = ui::IconRole::WifiOff;
        bool tappable = false;
        switch (wifi.link) {
            case WifiLinkState::Connected: {
                const auto name = wifi.connected_ssid.empty() ? std::string(t().wifi_connected)
                                                              : wifi_display_name(wifi.connected_ssid);
                std::snprintf(title, sizeof(title), "%s", name.c_str());
                const char* note = wifi_link_note();
                detail = note ? note : t().wifi_connected;
                if (!note && !wifi.ip_address.empty()) detail += " · " + wifi.ip_address;
                accent = ui::tokens::mint;
                role = wifi_bars_icon(wifi_signal_bars(wifi.connected_rssi_dbm));
                tappable = true;
                break;
            }
            // The network name is the title so it survives truncation; the
            // subtitle carries the state.
            case WifiLinkState::Connecting:
                std::snprintf(title, sizeof(title), "%s", wifi_display_name(wifi.target_ssid).c_str());
                detail = v().wifi_connecting;
                accent = ui::tokens::ice;
                role = ui::IconRole::Wifi;
                break;
            case WifiLinkState::Disconnected:
            case WifiLinkState::Unavailable:
                if (!wifi.connect_error.empty() && !wifi.target_ssid.empty()) {
                    std::snprintf(title, sizeof(title), "%s", wifi_display_name(wifi.target_ssid).c_str());
                    detail = wifi_error_text(wifi.connect_error);
                    accent = ui::tokens::coral;
                    role = ui::IconRole::Warning;
                    tappable = true;
                } else {
                    std::snprintf(title, sizeof(title), "%s", t().wifi_disconnected);
                    detail = t().wifi_choose_network;
                    accent = ui::tokens::text_tertiary;
                }
                break;
        }
        const bool connecting = wifi.link == WifiLinkState::Connecting;
        auto* card = list_row(parent, y, title, detail.empty() ? nullptr : detail.c_str(), role, accent,
                              kRowHeight, connecting ? 180 : 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, ui::color(accent), 0);
        lv_obj_set_style_border_opa(card, LV_OPA_60, 0);
        if (connecting) {
            // The arc spins in AuraUi::tick, independent of the motion model.
            wifi_spinner = spinner_arc(card, 60, ui::tokens::ice, 3);
            lv_obj_align(wifi_spinner, LV_ALIGN_LEFT_MID, -4, 0);
            auto* cancel = ui::make_button(card, t().cancel, ui::ButtonTone::Danger, ui::font_caption());
            lv_obj_set_size(cancel, 124, 52);
            lv_obj_set_style_pad_hor(cancel, 10, 0);
            lv_obj_align(cancel, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_add_event_cb(cancel, wifi_cancel_event, LV_EVENT_CLICKED, this);
            ui::set_button_disabled(cancel, !callbacks.on_wifi_cancel);
        } else if (tappable) {
            add_chevron(card);
            lv_obj_set_clickable(card, true);
            lv_obj_add_event_cb(card, wifi_status_event, LV_EVENT_CLICKED, this);
        }
    }

    void render_wifi() {
        if (wifi.link == WifiLinkState::Unavailable) {
            make_header(t().wifi_title);
            state_symbol(120, 112, ui::IconRole::WifiOff, ui::tokens::coral);
            char code_line[96] = {};
            if (!wifi.unavailable_reason.empty()) {
                std::snprintf(code_line, sizeof(code_line), t().error_code_format, wifi.unavailable_reason.c_str());
            }
            state_text(256, t().wifi_unavailable, t().wifi_unavailable_detail, code_line[0] ? code_line : nullptr);
            return;
        }
        make_header(t().wifi_title, ui::IconRole::Refresh, wifi_scan_event, ui::tokens::ice,
                    !wifi.scanning && static_cast<bool>(callbacks.on_wifi_scan));
        auto* list = make_list();
        render_wifi_status_card(list, 0);
        line(list, t().wifi_networks, kGutter + 4, 104, 250, ui::font_caption(), ui::tokens::text_secondary);
        if (wifi.scanning) {
            line(list, t().wifi_scanning, 236, 104, 216, ui::font_caption(), ui::tokens::ice, LV_TEXT_ALIGN_RIGHT);
        } else if (wifi.scan_origin == WifiOrigin::Demo && wifi.scanned) {
            auto* tag = ui::make_tag(list, v().demo_badge, ui::tokens::amber);
            lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, -kGutter, 102);
        }
        int y = 140;
        std::size_t shown = 0;
        for (std::size_t index = 0; index < wifi.network_count; ++index) {
            const auto& network = wifi.networks[index];
            // The connected network already heads the screen in its own card.
            if (network.connected) continue;
            const auto name = wifi_display_name(network.ssid);
            std::string detail = network.saved ? std::string(t().wifi_saved) + " · " : std::string{};
            detail += wifi_security_text(network.security);
            const bool supported = wifi_security_supported(network.security);
            auto* row = list_row(list, y, name.c_str(), detail.c_str(),
                                 wifi_bars_icon(wifi_signal_bars(network.rssi_dbm)),
                                 supported ? ui::tokens::ice : ui::tokens::text_tertiary);
            if (network.security != WifiSecurity::Open) {
                auto* lock = icon_view(row, ui::IconRole::Lock, 26,
                                       supported ? ui::tokens::text_secondary : ui::tokens::coral);
                lv_obj_align(lock, LV_ALIGN_RIGHT_MID, 0, 0);
            }
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_set_clickable(row, true);
            lv_obj_add_event_cb(row, wifi_network_event, LV_EVENT_CLICKED, this);
            if (!supported) lv_obj_set_style_opa(row, LV_OPA_60, 0);
            y += kRowPitch;
            ++shown;
        }
        if (shown == 0) {
            const char* message = wifi.scanning ? t().wifi_scanning :
                (!wifi.scan_error.empty() ? t().wifi_error_scan : t().wifi_no_networks);
            label(list, message, kGutter, y + 24, kWidth, ui::font_body(), ui::tokens::text_secondary,
                  LV_TEXT_ALIGN_CENTER);
            y += 96;
        }
        list_end(list, y);
    }

    void render_wifi_password() {
        const auto name = wifi_display_name(wifi_target_ssid);
        if (wifi_target_security == WifiSecurity::Open) {
            make_header(name.c_str());
            state_symbol(112, 104, ui::IconRole::Wifi, ui::tokens::amber);
            state_text(232, t().wifi_open, t().wifi_open_warning);
            auto* connect = pill(content, t().wifi_connect, ui::ButtonTone::Primary, kGutter, kActionY, kWidth,
                                 wifi_connect_event);
            ui::set_button_disabled(connect, !callbacks.on_wifi_connect);
            return;
        }
        make_header(name.c_str(), ui::IconRole::Check, wifi_connect_event, ui::tokens::blue,
                    static_cast<bool>(callbacks.on_wifi_connect));
        wifi_password_area = make_text_input(kGutter, 100, 352, 72, t().wifi_password_placeholder, true);
        lv_textarea_set_password_mode(wifi_password_area, !wifi_password_visible);
        lv_textarea_set_max_length(wifi_password_area, static_cast<std::uint32_t>(kWifiPskHexBytes));
        lv_obj_add_event_cb(wifi_password_area, wifi_password_ready_event, LV_EVENT_READY, this);
        auto* visibility = round_button(content, 392, 104, ui::tokens::HEADER_BUTTON, ui::IconRole::Count,
                                        ui::tokens::ice, wifi_visibility_event);
        wifi_visibility_icon = icon_view(visibility, wifi_password_visible ? ui::IconRole::VisibilityOff :
                                         ui::IconRole::Visibility, 30, ui::tokens::ice);
        lv_obj_center(wifi_visibility_icon);
        wifi_password_status = line(content, t().wifi_password_hint, kGutter + 4, 176, kWidth - 8,
                                    ui::font_caption(), ui::tokens::text_tertiary);
        make_form_keyboard(wifi_password_area, ui::KeyboardProfile::Password);
    }

    void detail_line(lv_obj_t* card, const char* name, const char* value) {
        auto* row = lv_obj_create(card);
        ui::reset(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        line(row, name, 0, 2, 172, ui::font_caption(), ui::tokens::text_secondary);
        line(row, value, 176, 0, kWidth - 40 - 176, ui::font_body(), ui::tokens::text, LV_TEXT_ALIGN_RIGHT);
    }

    void render_wifi_details() {
        const std::string& ssid = !wifi.connected_ssid.empty() ? wifi.connected_ssid : wifi.saved_ssid;
        const auto name = wifi_display_name(ssid);
        make_header(name.empty() ? t().wifi_title : name.c_str());
        const bool connected = wifi.link == WifiLinkState::Connected;
        const int bars = connected ? wifi_signal_bars(wifi.connected_rssi_dbm) : 0;

        auto* card = lv_obj_create(content);
        ui::reset(card);
        ui::style_platter(card, connected ? ui::tokens::mint : ui::tokens::text_tertiary, ui::tokens::radius_large,
                          false);
        lv_obj_set_style_pad_all(card, 20, 0);
        lv_obj_set_size(card, kWidth, LV_SIZE_CONTENT);
        lv_obj_set_pos(card, kGutter, 104);
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 12, 0);
        detail_line(card, t().connectivity, connected ? t().wifi_connected : t().wifi_disconnected);
        if (connected) {
            char signal[64];
            std::snprintf(signal, sizeof(signal), "%s · %d dBm",
                          bars >= 3 ? t().wifi_signal_strong : (bars == 2 ? t().wifi_signal_medium : t().wifi_signal_weak),
                          wifi.connected_rssi_dbm);
            detail_line(card, t().wifi_signal, signal);
        }
        if (wifi.connected_security != WifiSecurity::Unknown) {
            detail_line(card, t().wifi_security, wifi_security_text(wifi.connected_security));
        }
        if (!wifi.ip_address.empty()) detail_line(card, t().wifi_ip, wifi.ip_address.c_str());

        if (wifi.link_origin == WifiOrigin::Host) {
            label(content, t().wifi_host_note, kGutter, 392, kWidth, ui::font_caption(), ui::tokens::text_tertiary,
                  LV_TEXT_ALIGN_CENTER);
            return;
        }
        if (const char* note = wifi_link_note()) {
            label(content, note, kGutter, 320, kWidth, ui::font_caption(), ui::tokens::amber, LV_TEXT_ALIGN_CENTER);
        }
        if (!wifi.saved_ssid.empty()) {
            auto* forget = pill(content, t().wifi_forget, ui::ButtonTone::Danger, kGutter, kActionY, kWidth,
                                wifi_forget_event);
            wifi_forget_label = lv_obj_get_child(forget, 0);
            ui::set_button_disabled(forget, !callbacks.on_wifi_forget);
        }
    }

    void render_battery() {
        make_header(v().status_short);
        const bool low = status.power_known && status.battery_percent <= 20 && !status.charging;
        const std::uint32_t accent = low ? ui::tokens::amber : ui::tokens::mint;
        auto* gauge = lv_arc_create(content);
        lv_obj_set_size(gauge, 200, 200);
        lv_obj_set_pos(gauge, kGutter, 112);
        lv_obj_set_style_pad_all(gauge, 0, 0);
        lv_arc_set_rotation(gauge, 135);
        lv_arc_set_bg_angles(gauge, 0, 270);
        lv_arc_set_range(gauge, 0, 100);
        lv_arc_set_value(gauge, status.power_known ? status.battery_percent : 0);
        lv_obj_set_style_arc_width(gauge, 16, LV_PART_MAIN);
        lv_obj_set_style_arc_color(gauge, ui::color(ui::tokens::surface_high), LV_PART_MAIN);
        lv_obj_set_style_arc_width(gauge, 16, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(gauge, ui::color(accent), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(gauge, true, LV_PART_INDICATOR);
        lv_obj_set_style_opa(gauge, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_clickable(gauge, false);
        if (status.power_known && status.charging) {
            auto* bolt = icon_view(content, ui::IconRole::Charging, 30, ui::tokens::mint);
            lv_obj_set_pos(bolt, kGutter + 85, 140);
        }
        char value[16];
        if (status.power_known) std::snprintf(value, sizeof(value), "%d%%", status.battery_percent);
        else std::snprintf(value, sizeof(value), "%s", v().status_unknown);
        line(content, value, kGutter, 180, 200, ui::font_display(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        const char* power_origin = status.power_origin == StatusOrigin::Simulated ? v().status_sim :
                                   status.power_origin == StatusOrigin::Live ? v().status_live : v().status_unknown;
        line(content, status.power_known && status.charging ? v().charging : power_origin, kGutter, 252, 200,
             ui::font_caption(), status.power_known && status.charging ? ui::tokens::mint : ui::tokens::text_tertiary,
             LV_TEXT_ALIGN_CENTER);

        const char* connection = !status.connection_known ? v().status_unknown :
                                 status.online ? t().connected : t().disconnected;
        const char* connection_origin = status.connection_origin == StatusOrigin::Simulated ? v().status_sim :
                                        status.connection_origin == StatusOrigin::Live ? v().status_live : v().status_unknown;
        line(content, t().connectivity, 248, 124, 208, ui::font_caption(), ui::tokens::text_secondary);
        line(content, connection, 248, 154, 208, ui::font_headline(),
             status.online ? ui::tokens::text : ui::tokens::coral);
        auto* origin = ui::make_tag(content, connection_origin,
                                    status.connection_origin == StatusOrigin::Simulated ? ui::tokens::amber : ui::tokens::mint);
        lv_obj_set_pos(origin, 248, 198);
        if (status.power_origin == StatusOrigin::Simulated || status.connection_origin == StatusOrigin::Simulated) {
            label(content, v().simulator_value, 248, 244, 208, ui::font_caption(), ui::tokens::text_tertiary);
        }
        pill(content, audio.muted ? t().unmute : t().mute, ui::ButtonTone::Secondary, kGutter, kActionY, kWidth,
             audio_mute_event);
    }

    void update_host_metrics_label() {
        if (!host_metrics_label) return;
        char host_value[180];
        std::snprintf(host_value, sizeof(host_value), t().host_metrics_format,
                      host_loop_hz, host_handler_average_ms, host_handler_max_ms);
        char motion_value[120];
        std::snprintf(motion_value, sizeof(motion_value), v().motion_metrics_format,
                      motion_update_rate, static_cast<unsigned long long>(motion.update_count()));
        std::string value = host_value;
        value += "\n";
        value += motion_value;
        lv_label_set_text(host_metrics_label, value.c_str());
    }

    void render_diagnostics() {
        make_header(t().diagnostics);
        auto* list = make_list();
        // Column layouts stack the multi-line metric labels by their real heights.
        lv_obj_set_layout(list, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(list, 12, 0);
        lv_obj_set_style_pad_hor(list, kGutter, 0);
        lv_obj_set_style_pad_bottom(list, 16, 0);
        const auto column_card = [&](std::uint32_t accent, bool selected) {
            auto* card = lv_obj_create(list);
            ui::reset(card);
            ui::style_platter(card, accent, ui::tokens::radius_large, selected);
            lv_obj_set_style_pad_all(card, 20, 0);
            lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_layout(card, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(card, 8, 0);
            return card;
        };
        auto* host = column_card(ui::tokens::mint, true);
        auto* host_title = ui::make_label(host, t().host_metrics, ui::tokens::mint, ui::font_headline());
        lv_obj_set_width(host_title, lv_pct(100));
        host_metrics_label = ui::make_label(host, "", ui::tokens::text_secondary, ui::font_caption());
        lv_obj_set_width(host_metrics_label, lv_pct(100));
        update_host_metrics_label();
        auto* device = column_card(ui::tokens::lilac, false);
        auto* device_title = ui::make_label(device, t().device_estimates, ui::tokens::lilac, ui::font_headline());
        lv_obj_set_width(device_title, lv_pct(100));
        auto* device_text = ui::make_label(device, t().device_metrics_text, ui::tokens::text_secondary, ui::font_caption());
        lv_obj_set_width(device_text, lv_pct(100));
        auto* storage = ui::make_label(device, t().storage_demo, ui::tokens::text_tertiary, ui::font_caption());
        lv_obj_set_width(storage, lv_pct(100));
        // Speaker checks for development: a test tone and the bundled WAV.
        auto* speaker = column_card(ui::tokens::pink, false);
        auto* speaker_title = ui::make_label(speaker, t().audio_settings, ui::tokens::pink, ui::font_headline());
        lv_obj_set_width(speaker_title, lv_pct(100));
        audio_state_label = ui::make_label(speaker, "", ui::tokens::text_secondary, ui::font_caption());
        lv_obj_set_width(audio_state_label, lv_pct(100));
        auto* buttons = lv_obj_create(speaker);
        ui::reset(buttons);
        lv_obj_set_size(buttons, lv_pct(100), kButtonHeight);
        constexpr int kSpeakerButton = (kWidth - 40 - 12) / 2;
        pill(buttons, v().audio_test, ui::ButtonTone::Secondary, 0, 0, kSpeakerButton, diagnostics_tone_event);
        pill(buttons, v().demo_wav, ui::ButtonTone::Secondary, kSpeakerButton + 12, 0, kSpeakerButton,
             diagnostics_wav_event);
        update_audio_widgets();
    }

    void update_audio_widgets() {
        if (audio_progress_bar) {
            lv_bar_set_value(audio_progress_bar,
                             static_cast<int>(std::clamp(audio.progress, 0.0F, 1.0F) * 1000.0F),
                             LV_ANIM_OFF);
        }
        const float envelope = audio.state == AudioState::Playing && !audio.muted ? audio.envelope : 0.12F;
        const std::uint32_t visual_phase = reduced_motion ? 0U : phase_ms;
        for (std::size_t index = 0; index < audio_meter.size(); ++index) {
            if (!audio_meter[index]) continue;
            const float wave = 0.5F + 0.5F * std::sin(static_cast<float>(visual_phase) / 125.0F + static_cast<float>(index));
            const int height = 8 + static_cast<int>((14.0F + envelope * 30.0F) * wave);
            lv_obj_set_height(audio_meter[index], height);
            lv_obj_set_y(audio_meter[index], kMeterCenterY - height / 2);
        }
        if (audio_state_label) {
            const char* state_text = t().audio_ready_hint;
            if (audio.state == AudioState::Playing) state_text = t().audio_playing_hint;
            else if (audio.state == AudioState::Paused) state_text = t().pause;
            else if (audio.state == AudioState::Error || !audio.device_available) state_text = t().audio_error;
            set_text_if_changed(audio_state_label, state_text);
        }
        if (audio_hint_label) set_text_if_changed(audio_hint_label, audio.backend.c_str());
        update_volume_label();
    }

    void render_unavailable(const char* title) {
        make_header(title);
        state_symbol(120, 120, ui::IconRole::Close, ui::tokens::lilac);
        state_text(264, v().not_available_title, v().not_available_detail);
    }

    void render_typography_proof() {
        make_header(v().typography_proof);
        const auto role = [&](int index, int y) {
            line(content, v().typography_roles[index], kGutter, y, kWidth, ui::font_caption(),
                 ui::tokens::text_tertiary);
        };
        if (typography_page == 0) {
            role(0, 100);
            label(content, "18:24", kGutter, 128, kWidth, ui::font_clock(), ui::tokens::text);
            role(1, 208);
            label(content, "22°C  92%", kGutter, 236, kWidth, ui::font_display(), ui::tokens::text);
            role(2, 312);
            line(content, v().listening_title, kGutter, 340, kWidth, ui::font_title(), ui::tokens::text);
            line(content, v().thinking_title, kGutter, 384, kWidth, ui::font_title(), ui::tokens::lilac);
        } else if (typography_page == 1) {
            role(3, 100);
            label(content, v().typography_sample_1, kGutter, 128, kWidth, ui::font_headline(), ui::tokens::text);
            role(4, 210);
            label(content, v().typography_sample_4, kGutter, 238, kWidth, ui::font_body(), ui::tokens::text_secondary);
            role(5, 320);
            label(content, v().typography_sample_5, kGutter, 348, kWidth, ui::font_caption(), ui::tokens::text);
        } else {
            line(content, v().typography_ibm, kGutter, 100, kWidth, ui::font_caption(), ui::tokens::mint);
            label(content, v().typography_sample_6, kGutter, 132, kWidth, ui::font_title(), ui::tokens::text);
            label(content, v().typography_sample_7, kGutter, 228, kWidth, ui::font_body(), ui::tokens::text_secondary);
            label(content, v().typography_sample_8, kGutter, 330, kWidth, ui::font_headline(), ui::tokens::lilac);
        }
        const char* page = typography_page == 0 ? "1 / 3" : typography_page == 1 ? "2 / 3" : "3 / 3";
        line(content, page, kGutter, 444, kWidth, ui::font_caption(), ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
    }

    void update_motion(std::uint32_t elapsed_ms) {
        if (capture_mode || !motion.active()) return;
        if (motion.mode() == MotionMode::Listening) motion.set_amplitude(input_amplitude);
        else if (motion.mode() == MotionMode::Speaking) motion.set_amplitude(audio.envelope);
        const auto frame = motion.tick(elapsed_ms);
        if (mercury.object()) {
            mercury.set_level(motion.mode() == MotionMode::Listening ? input_amplitude : audio.envelope);
            mercury.set_reduced_motion(reduced_motion);
            // The halo follows the Mercury frame rate, so both share one redraw.
            if (mercury.tick(elapsed_ms)) update_halo(frame);
        }
        update_audio_widgets();
    }

    void update_weather_motion(std::uint32_t elapsed_ms) {
        (void)elapsed_ms;
        if (capture_mode || reduced_motion ||
            (current_view != UiView::Weather && current_view != UiView::WeatherLocation)) return;
        ++weather_motion_updates;
        if (weather_spinner) {
            lv_arc_set_rotation(weather_spinner, static_cast<int32_t>((phase_ms / 5U) % 360U));
        }
        if (current_view != UiView::Weather || !weather.has_data) return;
        const float seconds = static_cast<float>(phase_ms) / 1000.0F;
        const float pulse = 0.5F + 0.5F * std::sin(seconds * 2.2F);
        if (weather_halo) {
            lv_obj_set_style_transform_scale(weather_halo, 246 + static_cast<int>(pulse * 20.0F), 0);
            lv_obj_set_style_bg_opa(weather_halo, static_cast<lv_opa_t>(28 + pulse * 34.0F), 0);
            if (weather.data.condition == WeatherCondition::Thunderstorm) {
                const bool flash = (phase_ms % 3600U) > 3240U && (phase_ms % 3600U) < 3320U;
                lv_obj_set_style_bg_opa(weather_halo, flash ? static_cast<lv_opa_t>(LV_OPA_70) :
                                        static_cast<lv_opa_t>(28 + pulse * 34.0F), 0);
            }
        }
        if (weather_icon) {
            const bool drifting = weather.data.condition == WeatherCondition::PartlyCloudy ||
                weather.data.condition == WeatherCondition::Cloudy || weather.data.condition == WeatherCondition::Fog;
            const int base_x = !weather.data.is_day && weather.data.condition == WeatherCondition::Clear ? 62 :
                kWeatherHaloCenterX - static_cast<int>(lv_obj_get_width(weather_icon)) / 2;
            lv_obj_set_x(weather_icon, base_x + (drifting ? static_cast<int>(std::sin(seconds * 0.9F) * 5.0F) : 0));
            lv_obj_set_style_transform_rotation(weather_icon,
                weather.data.condition == WeatherCondition::Clear && weather.data.is_day ?
                    static_cast<int32_t>((phase_ms / 12U) % 3600U) : 0,
                0);
        }
        if (weather_moon_cutout) {
            lv_obj_set_x(weather_moon_cutout, 82);
        }
        for (std::size_t index = 0; index < weather_cloud_layers.size(); ++index) {
            if (!weather_cloud_layers[index]) continue;
            const float direction = index % 2 == 0 ? 1.0F : -1.0F;
            lv_obj_set_x(weather_cloud_layers[index], 40 + static_cast<int>(index) * 54 +
                static_cast<int>(std::sin(seconds * (0.55F + static_cast<float>(index) * 0.12F) +
                                          static_cast<float>(index)) * 5.0F * direction));
        }
        for (std::size_t index = 0; index < weather_mist_layers.size(); ++index) {
            if (!weather_mist_layers[index]) continue;
            lv_obj_set_x(weather_mist_layers[index], 38 + static_cast<int>(index) * 7 +
                static_cast<int>(std::sin(seconds * (0.38F + static_cast<float>(index) * 0.08F) +
                                          static_cast<float>(index)) * 12.0F));
        }
        for (std::size_t index = 0; index < weather_stars.size(); ++index) {
            if (!weather_stars[index]) continue;
            const float twinkle = 0.5F + 0.5F * std::sin(seconds * (1.4F + static_cast<float>(index) * 0.17F) +
                                                         static_cast<float>(index) * 1.3F);
            lv_obj_set_style_bg_opa(weather_stars[index], static_cast<lv_opa_t>(70 + twinkle * 150.0F), 0);
        }
        for (std::size_t index = 0; index < weather_particles.size(); ++index) {
            auto* particle = weather_particles[index];
            if (!particle) continue;
            if (weather.data.condition == WeatherCondition::Windy) {
                const int travel = static_cast<int>((phase_ms / 8U + index * 23U) % 146U);
                lv_obj_set_pos(particle, 18 + travel, 104 + static_cast<int>(index % 5) * 14);
                lv_obj_set_style_bg_opa(particle, static_cast<lv_opa_t>(70 + (index % 6) * 24), 0);
            } else {
                const int period = weather.data.condition == WeatherCondition::Snow ? 66 : 58;
                const unsigned speed = weather.data.condition == WeatherCondition::Snow ? 19U :
                    ((weather.data.weather_code == 65 || weather.data.weather_code == 82) ? 6U : 10U);
                const int travel = static_cast<int>((phase_ms / speed + index * 19U) % static_cast<unsigned>(period));
                const int drift = weather.data.condition == WeatherCondition::Snow ?
                    static_cast<int>(std::sin(seconds * 1.2F + static_cast<float>(index)) * 7.0F) : 0;
                lv_obj_set_pos(particle, 26 + static_cast<int>(index % 6) * 25 + drift, 112 + travel);
            }
        }
    }

    void reset_view_pointers() {
        mercury.detach();
        header_back = nullptr;
        header_title = nullptr;
        header_action = false;
        home_clock = nullptr;
        home_date = nullptr;
        hero_image = nullptr;
        halo = nullptr;
        state_label = nullptr;
        answer_label = nullptr;
        audio_state_label = nullptr;
        sound_volume_label = nullptr;
        boot_spinner = nullptr;
        audio_hint_label = nullptr;
        audio_progress_bar = nullptr;
        audio_meter.fill(nullptr);
        compose_area = nullptr;
        compose_status_label = nullptr;
        weather_search_area = nullptr;
        weather_scroll = nullptr;
        weather_scene = nullptr;
        weather_halo = nullptr;
        weather_icon = nullptr;
        weather_moon_cutout = nullptr;
        weather_spinner = nullptr;
        weather_particles.fill(nullptr);
        weather_cloud_layers.fill(nullptr);
        weather_mist_layers.fill(nullptr);
        weather_stars.fill(nullptr);
        weather_hour_cards.fill(nullptr);
        weather_day_cards.fill(nullptr);
        weather_detail_label = nullptr;
        host_metrics_label = nullptr;
        wifi_password_area = nullptr;
        wifi_password_status = nullptr;
        unlock_area = nullptr;
        unlock_status = nullptr;
        wifi_visibility_icon = nullptr;
        wifi_spinner = nullptr;
        wifi_forget_label = nullptr;
        wifi_forget_armed = false;
        primary_scroller = nullptr;
        reset_companion_pointers();
    }

    void navigate(UiView view) {
        navigation.navigate(view);
        render(view);
    }

    // Going back reopens a list where it was left, not at its top.
    void render_back(UiView view) {
        restore_scroll = true;
        render(view);
    }

    int32_t& remembered_scroll(UiView view) {
        for (auto& entry : scroll_memory) {
            if (entry.first == view) return entry.second;
        }
        scroll_memory.emplace_back(view, 0);
        return scroll_memory.back().second;
    }

    // Data refreshes re-render the visible view (e.g. every second while a pairing
    // countdown runs); only a view change or an explicit state change fades in.
    void render(UiView view, bool fade = false) {
        if (blocked_by_power(view)) return;
        const bool refresh = view == current_view;
        const bool restoring = restore_scroll && !refresh;
        restore_scroll = false;
        if (!refresh && primary_scroller) remembered_scroll(current_view) = lv_obj_get_scroll_y(primary_scroller);
        const int32_t saved_scroll = refresh && primary_scroller ? lv_obj_get_scroll_y(primary_scroller) :
                                     (restoring ? remembered_scroll(view) : 0);
        fade = fade || !refresh;
        // Leaving the city picker always reopens it on its presets.
        if (!refresh && view != UiView::WeatherLocation) weather_typing = false;
        current_view = view;
        motion.leave();
        reset_view_pointers();
        lv_obj_clean(content);
        switch (view) {
            case UiView::Home: render_home(); break;
            case UiView::Launcher: render_launcher(); break;
            case UiView::Assistant: render_assistant(); break;
            case UiView::Cards: render_unavailable(v().search); break;
            case UiView::Notifications: render_notifications(); break;
            case UiView::Settings: render_settings(); break;
            case UiView::Diagnostics: render_diagnostics(); break;
            case UiView::Audio: render_audio(); break;
            case UiView::AoratonRooms: render_aoraton_rooms(); break;
            case UiView::AoratonPair: render_aoraton_pair(); break;
            case UiView::AoratonUnlock: render_aoraton_unlock(); break;
            case UiView::AoratonConversation: render_aoraton_conversation(); break;
            case UiView::AoratonCompose: render_aoraton_compose(); break;
            case UiView::AoratonConfirm: render_aoraton_confirm(); break;
            case UiView::Navigation: render_unavailable(v().navigation); break;
            case UiView::Weather: render_weather(); break;
            case UiView::WeatherLocation: render_weather_location(); break;
            case UiView::Tools: render_unavailable(v().applications); break;
            case UiView::Calendar: render_unavailable(v().calendar); break;
            case UiView::Camera: render_unavailable(v().camera); break;
            case UiView::Battery: render_battery(); break;
            case UiView::VoiceSettings: render_unavailable(v().voice_title); break;
            case UiView::TypographyProof: render_typography_proof(); break;
            case UiView::Wifi: render_wifi(); break;
            case UiView::WifiPassword: render_wifi_password(); break;
            case UiView::WifiDetails: render_wifi_details(); break;
            case UiView::Clock:
            case UiView::ClockNew:
            case UiView::TimerNew:
            case UiView::AlarmNew:
            case UiView::ReminderNew:
            case UiView::ClockItem:
            case UiView::ClockAlert:
            case UiView::Notes:
            case UiView::NoteNew:
            case UiView::NoteDetail:
            case UiView::NoteRecord:
            case UiView::Briefing:
            case UiView::Translate:
            case UiView::AlwaysOn:
            case UiView::PhoneCall:
            case UiView::PhoneNotification:
            case UiView::PhoneLink:
            case UiView::DisplaySettings:
            case UiView::Update: render_companion(view); break;
            case UiView::System: render_system(); break;
            case UiView::SystemConfirm: render_system_confirm(); break;
            case UiView::PowerOff: render_power_off(); break;
            case UiView::Boot: render_boot(); break;
        }
        if ((refresh || restoring) && primary_scroller && saved_scroll > 0) {
            lv_obj_update_layout(primary_scroller);
            const int32_t limit = lv_obj_get_scroll_y(primary_scroller) + lv_obj_get_scroll_bottom(primary_scroller);
            lv_obj_scroll_to_y(primary_scroller, std::max<int32_t>(0, std::min(saved_scroll, limit)), LV_ANIM_OFF);
        }
        move_status_foreground();
        if (toast) lv_obj_move_foreground(toast);
        update_status();
        lv_obj_invalidate(status_bar);
        if (fade && !capture_mode && !reduced_motion) lv_obj_fade_in(content, ui::tokens::content_ms, 0);
    }

    void update_status() {
        if (!status_time || !status_battery || !status_network) return;
        // Text and image sources are only replaced when they change, so the
        // once-per-second status poll does not redraw the chrome on the device.
        update_home_text();
        const bool home = current_view == UiView::Home;
        char clock[12];
        if (status.clock_valid) std::snprintf(clock, sizeof(clock), "%02d:%02d", status.hour, status.minute);
        else std::snprintf(clock, sizeof(clock), "%s", v().status_unknown);
        set_text_if_changed(status_time, clock);

        char battery[16];
        if (status.power_known) std::snprintf(battery, sizeof(battery), "%d%%", status.battery_percent);
        else std::snprintf(battery, sizeof(battery), "%s", v().status_unknown);
        set_text_if_changed(status_battery, battery);
        const bool low = status.power_known && status.battery_percent <= 20 && !status.charging;
        lv_obj_set_style_text_color(status_battery, ui::color(low ? ui::tokens::amber : ui::tokens::text), 0);

        const bool online = status.connection_known && status.online;
        const auto* network_icon = ui::icon_asset(online ? wifi_bars_icon(status.wifi_signal_level) : ui::IconRole::WifiOff);
        if (lv_image_get_src(status_network) != network_icon) lv_image_set_src(status_network, network_icon);
        lv_obj_set_style_image_recolor(status_network, ui::color(online ? ui::tokens::text : ui::tokens::coral), 0);

        // Home shows the full status; other screens only what needs attention.
        lv_obj_set_hidden(status_left, !home);
        lv_obj_set_hidden(status_right, !home && header_action);
        lv_obj_set_hidden(status_time, home);
        lv_obj_set_hidden(status_alert, home || !(status.connection_known && !status.online));
        lv_obj_set_hidden(status_bluetooth, !(status.bluetooth_known && status.bluetooth_enabled));
        lv_obj_set_hidden(status_developer, !developer_mode);
        lv_obj_set_hidden(status_muted, !audio.muted);
        lv_obj_set_hidden(status_charging, !(status.power_known && status.charging));
        const bool show_battery = home || low;
        lv_obj_set_hidden(status_battery, !show_battery);
        lv_obj_set_hidden(status_battery_glyph, !show_battery || !status.power_known);
        if (status.power_known) {
            lv_obj_set_width(status_battery_fill, std::max(3, status.battery_percent * 26 / 100));
            lv_obj_set_style_bg_color(status_battery_fill,
                ui::color(low ? ui::tokens::coral : ui::tokens::mint), 0);
        }
        lv_obj_align(status_right, LV_ALIGN_TOP_RIGHT, -20, home ? 0 : 28);
        // The header title gives way to whatever the status cluster shows.
        if (header_title && !header_action) {
            lv_obj_update_layout(status_right);
            const int right_x = ui::tokens::display_width - 20 - lv_obj_get_width(status_right);
            lv_obj_set_width(header_title, std::max(120, right_x - 12 - 96));
        }
        lv_obj_set_hidden(status_bar, immersive(current_view));
    }

#include "aura_ui_companion.inl"
#include "aura_ui_system.inl"
};

AuraUi::AuraUi() : impl_(std::make_unique<Impl>()) {}
AuraUi::~AuraUi() = default;

void AuraUi::create(lv_display_t* display, AuraUiCallbacks callbacks) {
    impl_->display = display;
    impl_->callbacks = std::move(callbacks);
    lv_display_set_default(display);
    impl_->screen = lv_obj_create(nullptr);
    ui::reset(impl_->screen);
    lv_obj_set_style_bg_color(impl_->screen, ui::color(ui::tokens::canvas), 0);
    lv_obj_set_style_bg_opa(impl_->screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(impl_->screen, ui::font_body(), 0);

    impl_->content = lv_obj_create(impl_->screen);
    ui::reset(impl_->content);
    lv_obj_set_size(impl_->content, ui::tokens::display_width, ui::tokens::display_height);
    lv_obj_set_pos(impl_->content, 0, 0);
    lv_obj_set_scrollable(impl_->content, false);
    lv_obj_set_clickable(impl_->content, true);
    // Every child bubbles gestures by default, up to the first ancestor that
    // does not: without this the screen, not `content`, receives them.
    lv_obj_set_gesture_bubble(impl_->content, false);
    lv_obj_add_event_cb(impl_->content, Impl::gesture_event, LV_EVENT_GESTURE, impl_.get());

    impl_->create_status();
    impl_->render(UiView::Home);
    impl_->update_status();
    lv_screen_load(impl_->screen);
}

// Every apply_* compares the sequence before copying: the platforms call them
// every loop, and a deep copy of unchanged messages or forecasts per frame is
// wasted heap traffic on the ESP32.
void AuraUi::apply(const AssistantSnapshot& snapshot) {
    if (snapshot.sequence == impl_->snapshot.sequence) return;
    const auto old_state = impl_->snapshot.state;
    const auto old_generation = impl_->snapshot.turn_generation;
    const bool notifications_changed = snapshot.notification_count != impl_->snapshot.notification_count;
    impl_->snapshot = snapshot;
    // Only a real assistant transition pulls the user to the Assistant view; an
    // unrelated update (battery, notification) must not undo a Back navigation.
    const bool transition = snapshot.state != old_state || snapshot.turn_generation != old_generation;
    if (snapshot.state == AssistantState::Idle && old_state != AssistantState::Idle &&
        impl_->current_view == UiView::Assistant) {
        impl_->navigation.reset();
        impl_->render(UiView::Home);
    } else if (transition &&
               (snapshot.state == AssistantState::Offline || snapshot.state == AssistantState::Error ||
                (is_turn_active(snapshot.state) && snapshot.state != AssistantState::ActionConfirmation))) {
        if (impl_->current_view != UiView::Assistant) impl_->navigation.navigate(UiView::Assistant);
        impl_->render(UiView::Assistant, true);
    } else if (impl_->current_view == UiView::Home || impl_->current_view == UiView::Assistant ||
               impl_->current_view == UiView::Launcher || impl_->current_view == UiView::Settings ||
               impl_->current_view == UiView::Notifications ||
               (notifications_changed && impl_->current_view == UiView::AlwaysOn)) {
        impl_->render(impl_->current_view);
    }
}

void AuraUi::apply_sound(const SoundSettings& settings) {
    const bool changed = settings.message_chime != impl_->sound.message_chime ||
                         settings.spoken_replies != impl_->sound.spoken_replies;
    impl_->sound = settings;
    if (changed && impl_->current_view == UiView::Audio) impl_->render(UiView::Audio);
}

void AuraUi::power_off() {
    impl_->boot_remaining_ms = 0;
    impl_->navigation.reset();
    impl_->render(UiView::PowerOff);
    impl_->powered_off = true;
    if (impl_->toast) lv_obj_set_hidden(impl_->toast, true);
}

void AuraUi::boot() {
    impl_->powered_off = false;
    impl_->navigation.reset();
    impl_->boot_remaining_ms = 1800;
    impl_->render(UiView::Boot, true);
    if (impl_->toast) lv_obj_set_hidden(impl_->toast, true);
}

void AuraUi::reset_preferences() {
    impl_->reduced_motion = false;
    impl_->motion.set_reduced(false);
    impl_->scroll_memory.clear();
}

void AuraUi::apply_audio(const AudioDiagnostics& diagnostics) {
    const bool structural_change = diagnostics.state != impl_->audio.state ||
        diagnostics.muted != impl_->audio.muted ||
        diagnostics.device_available != impl_->audio.device_available ||
        diagnostics.error_code != impl_->audio.error_code;
    const bool mute_changed = diagnostics.muted != impl_->audio.muted;
    impl_->audio = diagnostics;
    if (mute_changed) impl_->update_status();
    if (impl_->current_view == UiView::Battery) {
        // The mute button there names the state it switches to.
        if (structural_change) impl_->render(UiView::Battery);
    } else if (impl_->current_view == UiView::Diagnostics) {
        impl_->update_audio_widgets();
    } else if (impl_->current_view == UiView::Audio) {
        if (structural_change) impl_->render(UiView::Audio);
        else impl_->update_audio_widgets();
    } else if (impl_->current_view == UiView::Assistant &&
               impl_->snapshot.state == AssistantState::Responding) {
        if (structural_change) impl_->render(UiView::Assistant);
        else impl_->update_audio_widgets();
    }
}

void AuraUi::apply_aoraton(const AoratonSnapshot& snapshot) {
    if (snapshot.sequence == impl_->aoraton.sequence) return;
    const auto& old = impl_->aoraton;
    // The list is capped, so a new message can keep the size the same.
    const bool notifications_changed = snapshot.notifications.size() != old.notifications.size() ||
        (!snapshot.notifications.empty() &&
         snapshot.notifications.back().message_id != old.notifications.back().message_id);
    const bool keys_unlocked = old.key_state == AoratonKeyState::Unlocking &&
                               snapshot.key_state == AoratonKeyState::Ready && snapshot.key_count > 0;
    impl_->aoraton = snapshot;
    if (keys_unlocked) {
        char message[64];
        std::snprintf(message, sizeof(message), impl_->c().aoraton_keys_unlocked_format,
                      static_cast<unsigned>(snapshot.key_count));
        // Done typing: back to where the keys were asked for.
        const bool typing = impl_->current_view == UiView::AoratonUnlock;
        if (typing) impl_->render(impl_->navigation.back());
        impl_->show_toast(snapshot.key_count == 1 ? impl_->c().aoraton_keys_unlocked_one : message,
                          ui::tokens::mint);
        if (typing) return;
    }
    if (impl_->current_view == UiView::AoratonUnlock) {
        impl_->update_unlock_status();
        return;
    }
    if (impl_->current_view == UiView::AoratonRooms ||
        impl_->current_view == UiView::Notifications ||
        impl_->current_view == UiView::AoratonPair ||
        impl_->current_view == UiView::AoratonConversation ||
        impl_->current_view == UiView::AoratonConfirm ||
        (notifications_changed && (impl_->current_view == UiView::Home ||
                                   impl_->current_view == UiView::Launcher ||
                                   impl_->current_view == UiView::AlwaysOn))) {
        impl_->render(impl_->current_view);
    }
}

void AuraUi::apply_status(const DeviceStatusSnapshot& snapshot) {
    if (snapshot.sequence == impl_->status.sequence) return;
    const bool status_screen_change = snapshot.power_known != impl_->status.power_known ||
        snapshot.battery_percent != impl_->status.battery_percent ||
        snapshot.charging != impl_->status.charging ||
        snapshot.power_origin != impl_->status.power_origin ||
        snapshot.connection_known != impl_->status.connection_known ||
        snapshot.online != impl_->status.online ||
        snapshot.wifi_signal_level != impl_->status.wifi_signal_level ||
        snapshot.connection_origin != impl_->status.connection_origin ||
        snapshot.bluetooth_known != impl_->status.bluetooth_known ||
        snapshot.bluetooth_enabled != impl_->status.bluetooth_enabled ||
        snapshot.bluetooth_origin != impl_->status.bluetooth_origin;
    // Home swaps the wordmark for the clock once the time is known.
    const bool home_layout_change = snapshot.clock_valid != impl_->status.clock_valid;
    impl_->status = snapshot;
    if (home_layout_change && impl_->current_view == UiView::Home) impl_->render(UiView::Home);
    else impl_->update_status();
    if (status_screen_change && impl_->current_view == UiView::Battery) {
        impl_->render(UiView::Battery);
    }
}

void AuraUi::apply_weather(const WeatherSnapshot& snapshot) {
    if (snapshot.sequence == impl_->weather.sequence) return;
    const auto old_state = impl_->weather.state;
    impl_->weather = snapshot;
    if (impl_->current_view == UiView::WeatherLocation && old_state != snapshot.state &&
        (snapshot.state == WeatherState::Offline || snapshot.state == WeatherState::Error)) {
        const auto& v = l10n::visual_el_gr();
        impl_->show_toast(snapshot.state == WeatherState::Offline ? v.weather_offline_title :
                              (snapshot.error_code == "CITY_NOT_FOUND" ? v.weather_not_found : v.weather_error_title),
                          snapshot.state == WeatherState::Offline ? ui::tokens::magenta : ui::tokens::coral);
    }
    if (impl_->current_view == UiView::Weather || impl_->current_view == UiView::WeatherLocation ||
        impl_->current_view == UiView::Settings || impl_->current_view == UiView::Home ||
        impl_->current_view == UiView::AlwaysOn) {
        impl_->render(impl_->current_view);
    }
}

void AuraUi::apply_wifi(const WifiSnapshot& snapshot) {
    if (snapshot.sequence == impl_->wifi.sequence) return;
    const auto old_link = impl_->wifi.link;
    impl_->wifi = snapshot;
    if (old_link == WifiLinkState::Connecting && snapshot.link == WifiLinkState::Connected) {
        char message[160];
        std::snprintf(message, sizeof(message), impl_->t().wifi_toast_connected_format,
                      wifi_display_name(snapshot.connected_ssid).c_str());
        impl_->show_toast(message, ui::tokens::mint);
    } else if (old_link == WifiLinkState::Connecting && !snapshot.connect_error.empty()) {
        impl_->show_toast(impl_->wifi_error_text(snapshot.connect_error), ui::tokens::coral);
    }
    // The password form is never rebuilt by data: it would wipe what is typed.
    if (impl_->current_view == UiView::Wifi || impl_->current_view == UiView::WifiDetails ||
        impl_->current_view == UiView::Settings) {
        impl_->render(impl_->current_view);
    }
}

void AuraUi::set_input_amplitude(float amplitude, bool simulated) {
    const bool source_changed = impl_->input_simulated != simulated;
    impl_->input_amplitude = std::clamp(amplitude, 0.0F, 1.0F);
    impl_->input_simulated = simulated;
    impl_->motion.set_amplitude(impl_->input_amplitude);
    if (source_changed && impl_->current_view == UiView::Assistant &&
        impl_->snapshot.state == AssistantState::Listening) {
        impl_->render(UiView::Assistant);
    }
}

void AuraUi::tick(std::uint32_t elapsed_ms) {
    impl_->phase_ms += elapsed_ms;
    impl_->animation_ms += elapsed_ms;
    impl_->motion_sample_ms += elapsed_ms;
    const std::uint32_t interval = 33U;
    if (impl_->animation_ms >= interval) {
        const auto delta = impl_->animation_ms;
        impl_->animation_ms = 0;
        impl_->update_motion(delta);
        impl_->update_weather_motion(delta);
        impl_->companion_tick();
        impl_->system_tick(delta);
        if (impl_->wifi_spinner && !impl_->capture_mode && !impl_->reduced_motion) {
            lv_arc_set_rotation(impl_->wifi_spinner, static_cast<int32_t>((impl_->phase_ms / 4U) % 360U));
        }
    }
    if (impl_->toast_remaining_ms > 0) {
        impl_->toast_remaining_ms = elapsed_ms >= impl_->toast_remaining_ms ? 0U :
                                    impl_->toast_remaining_ms - elapsed_ms;
        if (impl_->toast_remaining_ms == 0 && impl_->toast) lv_obj_set_hidden(impl_->toast, true);
    }
    if (impl_->wifi_forget_armed && impl_->phase_ms - impl_->wifi_forget_armed_at > kForgetArmMs) {
        impl_->wifi_forget_armed = false;
        if (impl_->wifi_forget_label) lv_label_set_text(impl_->wifi_forget_label, impl_->t().wifi_forget);
    }
    if (impl_->motion_sample_ms >= 1000U) {
        const auto updates = impl_->motion.update_count();
        const auto delta_updates = updates - impl_->motion_sample_updates;
        impl_->motion_update_rate = static_cast<float>(delta_updates) * 1000.0F /
                                    static_cast<float>(impl_->motion_sample_ms);
        impl_->motion_sample_updates = updates;
        const auto weather_delta_updates =
            impl_->weather_motion_updates - impl_->weather_motion_sample_updates;
        impl_->weather_motion_update_rate = static_cast<float>(weather_delta_updates) * 1000.0F /
                                            static_cast<float>(impl_->motion_sample_ms);
        impl_->weather_motion_sample_updates = impl_->weather_motion_updates;
        impl_->motion_sample_ms = 0;
        impl_->update_host_metrics_label();
    }
}

void AuraUi::show(UiView view) {
    impl_->weather_typing = false;
    impl_->navigation.replace(view);
    impl_->render(view);
}

void AuraUi::open_wifi_network(const std::string& ssid, WifiSecurity security) {
    if (!wifi_ssid_valid(ssid)) return;
    impl_->wifi_target_ssid = ssid;
    impl_->wifi_target_security = security;
    impl_->wifi_password_visible = false;
    impl_->navigation.replace(UiView::Wifi);
    impl_->navigate(UiView::WifiPassword);
}

void AuraUi::show_typography_proof(unsigned page) {
    impl_->typography_page = std::min(page, 2U);
    impl_->navigation.replace(UiView::TypographyProof);
    impl_->render(UiView::TypographyProof);
}

void AuraUi::set_provider_live(bool live) {
    impl_->live_provider = live;
    if (impl_->current_view == UiView::Settings) impl_->render(UiView::Settings);
}

void AuraUi::set_developer_mode(bool enabled) {
    impl_->developer_mode = enabled;
    impl_->update_status();
}

void AuraUi::set_capture_mode(bool enabled) {
    impl_->capture_mode = enabled;
}

void AuraUi::set_host_metrics(float loop_hz, float average_handler_ms, float maximum_handler_ms) {
    impl_->host_loop_hz = loop_hz;
    impl_->host_handler_average_ms = average_handler_ms;
    impl_->host_handler_max_ms = maximum_handler_ms;
    impl_->update_host_metrics_label();
}

float AuraUi::motion_update_hz() const noexcept { return impl_->motion_update_rate; }
std::uint64_t AuraUi::motion_update_count() const noexcept { return impl_->motion.update_count(); }
float AuraUi::weather_motion_update_hz() const noexcept { return impl_->weather_motion_update_rate; }
std::uint64_t AuraUi::weather_motion_update_count() const noexcept { return impl_->weather_motion_updates; }
std::uint64_t AuraUi::weather_forecast_interaction_count() const noexcept {
    return impl_->weather_forecast_interactions;
}
std::uint64_t AuraUi::mercury_frame_count() const noexcept { return impl_->mercury.frame_count(); }
std::size_t AuraUi::mercury_buffer_bytes() const noexcept { return impl_->mercury.buffer_bytes(); }
bool AuraUi::reduced_motion() const noexcept { return impl_->reduced_motion; }

UiView AuraUi::view() const noexcept { return impl_->current_view; }
lv_obj_t* AuraUi::root() const noexcept { return impl_->screen; }

#include "aura_ui_companion_api.inl"

}  // namespace aura

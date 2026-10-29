#pragma once

#include "aura/assistant_companion.hpp"
#include "aura/assistant_state.hpp"
#include "aura/app_layout.hpp"
#include "aura/aoraton.hpp"
#include "aura/audio_service.hpp"
#include "aura/clock_services.hpp"
#include "aura/device_status.hpp"
#include "aura/display_power.hpp"
#include "aura/fuel_prices.hpp"
#include "aura/language.hpp"
#include "aura/notes.hpp"
#include "aura/organizer.hpp"
#include "aura/ota_update.hpp"
#include "aura/phone_link.hpp"
#include "aura/radio.hpp"
#include "aura/sounds.hpp"
#include "aura/translation.hpp"
#include "aura/voice.hpp"
#include "aura/weather.hpp"
#include "aura/wifi.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "lvgl.h"

namespace aura {

enum class UiView {
    Home,
    Launcher,
    Assistant,
    ConversationHistory,
    Notifications,
    Settings,
    LanguageSettings,
    Diagnostics,
    Audio,
    AoratonRooms,
    AoratonPair,
    AoratonConversation,
    AoratonCompose,
    AoratonConfirm,
    Weather,
    WeatherSettings,
    WeatherLocation,
    Calendar,
    OrganizerNew,
    Battery,
    VoiceSettings,
    TypographyProof,
    Wifi,
    WifiPassword,
    WifiDetails,
    // v13 "Companion"
    Clock,
    ClockNew,
    TimerNew,
    AlarmNew,
    ReminderNew,
    ClockItem,
    ClockAlert,
    Notes,
    NoteNew,
    NoteDetail,
    NoteRecord,
    Briefing,
    Translate,
    AlwaysOn,
    PhoneCall,
    PhoneNotification,
    PhoneLink,
    DisplaySettings,
    AppearanceSettings,
    Update,
    Radio,
    RadioSearch,
    // aoraton.com account
    AoratonUnlock,
    // System: device information and power
    System,
    SystemConfirm,
    PowerOff,
    Boot,
};

// Power actions from Settings > System. PowerOn comes from a long press on the
// switched-off screen.
enum class SystemAction { Restart, PowerOff, PowerOn, FactoryReset };

// What this platform can do right now; the UI never offers the rest.
struct CompanionCapabilities {
    bool microphone{false};
    bool speech_recognition{false};
    bool speech_output{false};  // a voice for the selected interface language
    bool imu{false};            // motion sensor (raise to wake, turn over)
    bool phone_link{false};     // a phone notification transport
    bool wake_word{false};      // a trained wake-word model
    bool panel_brightness{false};  // the panel dims its own backlight (no veil)
    bool power_button{false};      // power off ends with the side button, not a touch
};

// Assistant state that lives outside AssistantSnapshot.
struct AssistantExtras {
    std::optional<AssistantAction> pending_action;
    std::size_t memory_turns{0};
    std::vector<ConversationTurn> conversation_history;  // bounded, RAM only
    std::string assistant_provider;
    std::string speech_recognition_provider;
    std::string speech_output_provider;
    bool web_search_ready{false};
    bool spoken_reply{false};  // the reply is being or was spoken
    std::uint64_t sequence{0};
};

struct AuraUiCallbacks {
    std::function<void()> on_ask;
    std::function<void()> on_end_listening;
    std::function<void()> on_cancel;
    std::function<void()> on_retry;
    std::function<void()> on_home;
    std::function<void()> on_notifications_clear;
    std::function<void(bool live)> on_provider_changed;
    std::function<void(InterfaceLanguage language)> on_language_changed;
    std::function<void()> on_audio_test;
    std::function<void()> on_audio_wav;
    std::function<void()> on_audio_pause_resume;
    std::function<void()> on_audio_stop;
    std::function<void(bool muted)> on_audio_muted;
    std::function<void(float volume)> on_audio_volume;
    std::function<bool(const std::string& room_id)> on_aoraton_room;
    std::function<bool(const std::string& room_id, const std::string& text)> on_aoraton_prepare;
    std::function<bool()> on_aoraton_confirm;
    std::function<void()> on_aoraton_cancel;
    std::function<bool()> on_aoraton_pair_start;
    std::function<bool()> on_aoraton_pair_confirm_sas;
    std::function<void()> on_aoraton_pair_cancel;
    std::function<void()> on_aoraton_unpair;
    // The passphrase is handed over once and the field is cleared; the invite
    // link is read by the platform (the PC clipboard) and never shown.
    std::function<bool(const std::string& passphrase)> on_aoraton_unlock;
    std::function<bool()> on_aoraton_paste_invite;
    std::function<void()> on_weather_open;
    std::function<void()> on_weather_refresh;
    std::function<void(WeatherRefreshInterval interval)> on_weather_refresh_interval;
    std::function<void(const std::string& query)> on_weather_search;
    std::function<void(std::size_t index)> on_weather_select_result;
    std::function<void()> on_wifi_open;
    std::function<void()> on_wifi_scan;
    // The password goes straight to the platform adapter; the UI clears its
    // copy after the call and it never enters a snapshot.
    std::function<bool(const std::string& ssid, WifiSecurity security, const std::string& password)> on_wifi_connect;
    std::function<void()> on_wifi_cancel;
    std::function<void()> on_wifi_forget;

    // v13 "Companion". Every action with a side effect is taken on an explicit
    // tap; assistant proposals arrive as AssistantExtras::pending_action.
    std::function<void()> on_action_confirm;
    std::function<void()> on_action_decline;
    std::function<void()> on_conversation_clear;
    std::function<void(const std::string& text, const std::string& language)> on_speak;
    std::function<bool(std::uint32_t seconds, const std::string& label)> on_timer_start;
    std::function<void(std::uint32_t id, bool paused)> on_timer_pause;
    std::function<void(std::uint32_t id)> on_timer_cancel;
    std::function<bool(int hour, int minute, bool daily)> on_alarm_add;
    std::function<void(std::uint32_t id, bool enabled)> on_alarm_enable;
    std::function<void(std::uint32_t id)> on_alarm_remove;
    std::function<bool(std::int64_t due_minute, const std::string& text)> on_reminder_add;
    std::function<void(std::uint32_t id)> on_reminder_remove;
    std::function<void()> on_alert_dismiss;
    std::function<void(int minutes)> on_alert_snooze;
    std::function<bool(const std::string& text)> on_note_add;
    std::function<bool()> on_note_record;
    std::function<void()> on_note_record_stop;
    std::function<void()> on_note_record_cancel;
    std::function<void(std::uint32_t id)> on_note_play;
    std::function<void(std::uint32_t id)> on_note_delete;
    std::function<bool(const std::string& title, std::int64_t start_minute,
                       std::uint32_t duration_minutes)> on_organizer_event_add;
    std::function<bool(const std::string& text)> on_organizer_task_add;
    std::function<bool(const std::string& text)> on_organizer_shopping_add;
    std::function<void(OrganizerKind kind, std::uint32_t id)> on_organizer_toggle;
    std::function<void(OrganizerKind kind, std::uint32_t id)> on_organizer_remove;
    std::function<bool(TranslateSide side)> on_translate_record;
    std::function<void()> on_translate_stop;
    std::function<void(const std::string& language)> on_translate_language;
    std::function<void()> on_briefing_open;
    std::function<void(const DisplaySettings& settings)> on_display_settings;
    std::function<void()> on_wake;
    std::function<void(std::uint32_t uid, bool accept)> on_phone_call;
    std::function<void(std::uint32_t uid)> on_phone_dismiss;
    std::function<void()> on_phone_pair;
    std::function<void()> on_phone_forget;
    std::function<void()> on_update_check;
    std::function<void()> on_update_install;
    std::function<void()> on_update_restart;
    std::function<void(const RadioStation& station)> on_radio_play;
    std::function<void()> on_radio_stop;
    std::function<void(const std::string& query)> on_radio_search;
    std::function<void(const RadioStation& station)> on_radio_favorite;
    std::function<void(std::size_t index, int direction)> on_radio_move;
    std::function<void(std::size_t from, std::size_t to)> on_launcher_app_move;
    std::function<void(const SoundSettings& settings)> on_sound_settings;
    std::function<void()> on_sound_preview;
    std::function<void(AssistantVoice voice)> on_voice_preview;
    std::function<void(SystemAction action)> on_system_action;
};

class AuraUi {
public:
    AuraUi();
    ~AuraUi();
    AuraUi(const AuraUi&) = delete;
    AuraUi& operator=(const AuraUi&) = delete;

    void create(lv_display_t* display, AuraUiCallbacks callbacks);
    void apply(const AssistantSnapshot& snapshot);
    void apply_audio(const AudioDiagnostics& diagnostics);
    void apply_aoraton(const AoratonSnapshot& snapshot);
    void apply_status(const DeviceStatusSnapshot& snapshot);
    void apply_weather(const WeatherSnapshot& snapshot);
    void apply_wifi(const WifiSnapshot& snapshot);
    void apply_clock(const ClockSnapshot& snapshot);
    void apply_notes(const NotesSnapshot& snapshot);
    void apply_organizer(const OrganizerSnapshot& snapshot);
    void apply_voice(const VoiceSnapshot& snapshot);
    void apply_translation(const TranslationSnapshot& snapshot);
    void apply_phone(const PhoneSnapshot& snapshot);
    void apply_update(const UpdateSnapshot& snapshot);
    void apply_display(const DisplaySnapshot& snapshot);
    void apply_radio(const RadioSnapshot& snapshot);
    void apply_launcher_layout(const LauncherLayoutSnapshot& snapshot);
    // Opens the apps grid in its edit mode (the same as its Edit button):
    // select an app, then the position it moves to.
    void edit_launcher_layout();
    void apply_fuel(const FuelPriceSnapshot& snapshot);
    void apply_assistant_extras(const AssistantExtras& extras);
    // Chime and spoken-reply choices; volume and mute come with apply_audio.
    void apply_sound(const SoundSettings& settings);
    // A dark, switched-off screen; a long press asks the platform to power on.
    void power_off();
    // The start-up screen, then Home. Nothing else is shown until it ends.
    void boot();
    // Forgets the choices the UI keeps itself (factory reset).
    void reset_preferences();
    // May be called before create() to restore the persisted choice without a
    // Greek first-frame flash. User changes are reported through the callback.
    void set_language(InterfaceLanguage language);
    InterfaceLanguage language() const noexcept;
    // The look of the whole device (Settings > Appearance). May be called before
    // create() to restore the persisted choice without a Halo first frame; a
    // user's choice is reported through on_display_settings.
    void set_appearance(Appearance appearance);
    Appearance appearance() const noexcept;
    void set_capabilities(const CompanionCapabilities& capabilities);
    // Opens "Today" (the platform does so once each morning).
    void open_briefing();
    void open_organizer(OrganizerKind kind);
    void open_note(std::uint32_t id);
    void open_clock_item(ClockAlertKind kind, std::uint32_t id);
    void open_phone_notification(std::uint32_t uid);
    // Opens the same confirmation flow used by Settings > System. It never
    // executes the action; the user still has to press the confirm button.
    void show_system_confirmation(SystemAction action);
    void show_message(const char* text, bool error);
    void set_input_amplitude(float amplitude, bool simulated);
    void tick(std::uint32_t elapsed_ms);
    void show(UiView view);
    // Opens `view` as if from Home: Back returns Home, never to a screen
    // that happened to be open before.
    void show_from_home(UiView view);
    // Opens the join form (password or open-network confirmation) for one network.
    void open_wifi_network(const std::string& ssid, WifiSecurity security);
    void show_typography_proof(unsigned page);
    void set_provider_live(bool live);
    void set_developer_mode(bool enabled);
    void set_capture_mode(bool enabled);
    void set_host_metrics(float loop_hz, float average_handler_ms, float maximum_handler_ms);
    float motion_update_hz() const noexcept;
    std::uint64_t motion_update_count() const noexcept;
    float weather_motion_update_hz() const noexcept;
    std::uint64_t weather_motion_update_count() const noexcept;
    std::uint64_t weather_forecast_interaction_count() const noexcept;
    std::uint64_t mercury_frame_count() const noexcept;
    std::size_t mercury_buffer_bytes() const noexcept;
    bool reduced_motion() const noexcept;
    UiView view() const noexcept;
    lv_obj_t* root() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aura

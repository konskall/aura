#include "aura/aura_icons.hpp"

#include "aura/aura_appearance.hpp"
#include "aura_icon_assets.hpp"
#include "aura_icon_assets_material.hpp"
#include "aura_icon_assets_phosphor.hpp"
#include "aura_icon_assets_solar.hpp"

namespace aura::ui {

#define AURA_ICON_CASE(role, asset) case IconRole::role: return &aura_icon_##asset

namespace {

const lv_image_dsc_t* lucide_asset(IconRole role) noexcept {
    switch (role) {
        AURA_ICON_CASE(Home, home); AURA_ICON_CASE(Back, arrow_left); AURA_ICON_CASE(Forward, arrow_right);
        AURA_ICON_CASE(Menu, menu); AURA_ICON_CASE(Close, x); AURA_ICON_CASE(Apps, layout_grid);
        AURA_ICON_CASE(Chevron, chevron_right); AURA_ICON_CASE(Refresh, rotate_cw); AURA_ICON_CASE(More, ellipsis);
        AURA_ICON_CASE(Microphone, mic); AURA_ICON_CASE(MicMuted, mic_off); AURA_ICON_CASE(Chat, message_circle);
        AURA_ICON_CASE(Search, search); AURA_ICON_CASE(Send, send); AURA_ICON_CASE(Cancel, circle_x);
        AURA_ICON_CASE(Thinking, brain); AURA_ICON_CASE(Copy, copy); AURA_ICON_CASE(Confirmation, circle_check);
        AURA_ICON_CASE(Voice, audio_lines); AURA_ICON_CASE(Play, play); AURA_ICON_CASE(Pause, pause);
        AURA_ICON_CASE(Stop, square); AURA_ICON_CASE(Speaker, volume_2); AURA_ICON_CASE(VolumeDown, volume_1);
        AURA_ICON_CASE(VolumeUp, volume_2); AURA_ICON_CASE(Muted, volume_x); AURA_ICON_CASE(TestSound, audio_waveform);
        AURA_ICON_CASE(Record, circle); AURA_ICON_CASE(Message, message_square); AURA_ICON_CASE(Inbox, inbox);
        AURA_ICON_CASE(Unread, mail); AURA_ICON_CASE(Notification, bell); AURA_ICON_CASE(Compose, square_pen);
        AURA_ICON_CASE(Reply, reply); AURA_ICON_CASE(Room, users); AURA_ICON_CASE(Lock, lock);
        AURA_ICON_CASE(Encrypted, shield_check); AURA_ICON_CASE(Connection, link); AURA_ICON_CASE(Wifi, wifi);
        AURA_ICON_CASE(WifiOff, wifi_off); AURA_ICON_CASE(Bluetooth, bluetooth); AURA_ICON_CASE(Battery, battery_medium);
        AURA_ICON_CASE(Charging, battery_charging); AURA_ICON_CASE(Display, monitor); AURA_ICON_CASE(Brightness, sun);
        AURA_ICON_CASE(Settings, settings); AURA_ICON_CASE(Storage, hard_drive); AURA_ICON_CASE(Cpu, cpu);
        AURA_ICON_CASE(Memory, memory_stick); AURA_ICON_CASE(Weather, cloud_sun); AURA_ICON_CASE(Sun, sun);
        AURA_ICON_CASE(Cloud, cloud); AURA_ICON_CASE(CloudRain, cloud_rain); AURA_ICON_CASE(CloudDrizzle, cloud_drizzle);
        AURA_ICON_CASE(CloudLightning, cloud_lightning); AURA_ICON_CASE(Snowflake, snowflake);
        AURA_ICON_CASE(Wind, wind); AURA_ICON_CASE(Droplet, droplet); AURA_ICON_CASE(Thermometer, thermometer);
        AURA_ICON_CASE(Clock, clock_3); AURA_ICON_CASE(Location, map_pin);
        AURA_ICON_CASE(Calendar, calendar_days); AURA_ICON_CASE(Reminder, alarm_clock); AURA_ICON_CASE(Timer, timer);
        AURA_ICON_CASE(Camera, camera); AURA_ICON_CASE(Tools, wrench); AURA_ICON_CASE(Warning, triangle_alert);
        AURA_ICON_CASE(Error, circle_alert); AURA_ICON_CASE(Success, circle_check); AURA_ICON_CASE(Retry, rotate_cw);
        AURA_ICON_CASE(Help, circle_help); AURA_ICON_CASE(Information, info); AURA_ICON_CASE(Offline, cloud_off);
        AURA_ICON_CASE(Audio, music); AURA_ICON_CASE(Delete, trash_2); AURA_ICON_CASE(Visibility, eye);
        AURA_ICON_CASE(Power, zap); AURA_ICON_CASE(List, list); AURA_ICON_CASE(File, file_text);
        AURA_ICON_CASE(Edit, pen_line); AURA_ICON_CASE(Calculator, calculator); AURA_ICON_CASE(Image, image);
        AURA_ICON_CASE(Navigation, navigation); AURA_ICON_CASE(Globe, globe_2);
        AURA_ICON_CASE(WifiLow, wifi_low); AURA_ICON_CASE(WifiMedium, wifi_high);
        AURA_ICON_CASE(VisibilityOff, eye_off); AURA_ICON_CASE(Sparkles, sparkles);
        AURA_ICON_CASE(ChevronLeft, chevron_left); AURA_ICON_CASE(Check, check); AURA_ICON_CASE(Plus, plus);
        AURA_ICON_CASE(Languages, languages);
        AURA_ICON_CASE(Notebook, notebook_pen);
        AURA_ICON_CASE(Sunrise, sunrise);
        AURA_ICON_CASE(Smartphone, smartphone);
        AURA_ICON_CASE(Phone, phone);
        AURA_ICON_CASE(PhoneOff, phone_off);
        AURA_ICON_CASE(Download, download);
        AURA_ICON_CASE(BellRing, bell_ring);
        AURA_ICON_CASE(Moon, moon);
        AURA_ICON_CASE(Hand, hand);
        AURA_ICON_CASE(Flip, flip_vertical);
        AURA_ICON_CASE(Ear, ear);
        AURA_ICON_CASE(Swap, arrow_left_right);
        AURA_ICON_CASE(Palette, palette);
        case IconRole::Count: break;
    }
    return nullptr;
}

#undef AURA_ICON_CASE

// Marble: Phosphor Light; Aurora: Solar Bold Duotone; Graphite: Material Symbols Rounded.
const lv_image_dsc_t* family_asset(IconRole role, Appearance appearance) noexcept {
    switch (appearance) {
        case Appearance::Marble: return aura_icon_phosphor(role);
        case Appearance::Aurora: return aura_icon_solar(role);
        case Appearance::Graphite: return aura_icon_material(role);
        case Appearance::Halo: break;
    }
    return nullptr;
}

}  // namespace

const lv_image_dsc_t* icon_asset(IconRole role, Appearance appearance) noexcept {
    if (const auto* own = family_asset(role, appearance)) return own;
    return lucide_asset(role);
}

const lv_image_dsc_t* icon_asset(IconRole role) noexcept { return icon_asset(role, active_appearance()); }

bool icon_is_fallback(IconRole role, Appearance appearance) noexcept {
    return appearance != Appearance::Halo && family_asset(role, appearance) == nullptr;
}

const char* icon_role_name(IconRole role) noexcept {
    static constexpr const char* names[] = {
        "home", "back", "forward", "menu", "close", "apps", "chevron", "refresh", "more",
        "microphone", "mic-muted", "chat", "search", "send", "cancel", "thinking", "copy", "confirmation", "voice",
        "play", "pause", "stop", "speaker", "volume-down", "volume-up", "muted", "test-sound", "record",
        "message", "inbox", "unread", "notification", "compose", "reply", "room", "lock", "encrypted", "connection",
        "wifi", "wifi-off", "bluetooth", "battery", "charging", "display", "brightness", "settings", "storage", "cpu", "memory",
        "weather", "sun", "cloud", "cloud-rain", "cloud-drizzle", "cloud-lightning", "snowflake",
        "wind", "droplet", "thermometer", "clock", "location", "calendar", "reminder", "timer", "camera", "tools", "warning", "error", "success", "retry",
        "help", "information", "offline", "audio", "delete", "visibility", "power", "list", "file", "edit", "calculator", "image",
        "navigation", "globe", "wifi-low", "wifi-medium", "visibility-off", "sparkles",
        "chevron-left", "check", "plus",
        "languages", "notebook", "sunrise", "smartphone", "phone", "phone-off", "download", "bell-ring", "moon", "hand", "flip", "ear", "swap",
        "palette"
    };
    const auto index = static_cast<std::size_t>(role);
    return index < icon_role_count() ? names[index] : "invalid";
}

}  // namespace aura::ui

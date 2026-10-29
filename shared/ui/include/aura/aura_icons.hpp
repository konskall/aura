#pragma once

#include <cstddef>

#include "aura/appearance.hpp"
#include "lvgl.h"

namespace aura::ui {

enum class IconRole {
    Home, Back, Forward, Menu, Close, Apps, Chevron, Refresh, More,
    Microphone, MicMuted, Chat, Search, Send, Cancel, Thinking, Copy, Confirmation, Voice,
    Play, Pause, Stop, Speaker, VolumeDown, VolumeUp, Muted, TestSound, Record,
    Message, Inbox, Unread, Notification, Compose, Reply, Room, Lock, Encrypted, Connection,
    Wifi, WifiOff, Bluetooth, Battery, Charging, Display, Brightness, Settings, Storage, Cpu, Memory,
    Weather, Sun, Cloud, CloudRain, CloudDrizzle, CloudLightning, Snowflake,
    Wind, Droplet, Thermometer, Clock, Location, Calendar, Reminder, Timer, Camera, Tools,
    Warning, Error, Success, Retry, Help, Information, Offline,
    Audio, Delete, Visibility, Power, List, File, Edit, Calculator, Image, Navigation, Globe,
    WifiLow, WifiMedium, VisibilityOff, Sparkles,
    ChevronLeft, Check, Plus,
    Languages, Notebook, Sunrise, Smartphone, Phone, PhoneOff, Download, BellRing, Moon, Hand, Flip, Ear, Swap,
    Palette,
    Count,
};

// The icon in the active appearance's family.
const lv_image_dsc_t* icon_asset(IconRole role) noexcept;
// The icon in `appearance`'s family (Lucide for Halo); a role the family has
// no faithful glyph for uses the Lucide asset.
const lv_image_dsc_t* icon_asset(IconRole role, Appearance appearance) noexcept;
bool icon_is_fallback(IconRole role, Appearance appearance) noexcept;
const char* icon_role_name(IconRole role) noexcept;
constexpr std::size_t icon_role_count() noexcept { return static_cast<std::size_t>(IconRole::Count); }

}  // namespace aura::ui

#pragma once

#include <cstdint>
#include <string_view>

namespace aura {

// The interface language is shared business state so the simulator and the
// embedded build select the same UI copy, keyboard, recognition and voice.
enum class InterfaceLanguage : std::uint8_t { Greek, English };

constexpr const char* interface_language_code(InterfaceLanguage language) noexcept {
    return language == InterfaceLanguage::English ? "en" : "el";
}

constexpr const char* interface_language_tag(InterfaceLanguage language) noexcept {
    return language == InterfaceLanguage::English ? "en-US" : "el-GR";
}

constexpr InterfaceLanguage parse_interface_language(std::string_view code) noexcept {
    return code == "en" || code == "en-US" ? InterfaceLanguage::English : InterfaceLanguage::Greek;
}

}  // namespace aura

set(AURA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

idf_component_register(
    SRCS
        "${AURA_ROOT}/shared/core/src/aoraton.cpp"
        "${AURA_ROOT}/shared/core/src/assistant_companion.cpp"
        "${AURA_ROOT}/shared/core/src/briefing.cpp"
        "${AURA_ROOT}/shared/core/src/clock_services.cpp"
        "${AURA_ROOT}/shared/core/src/display_power.cpp"
        "${AURA_ROOT}/shared/core/src/notes.cpp"
        "${AURA_ROOT}/shared/core/src/ota_update.cpp"
        "${AURA_ROOT}/shared/core/src/phone_link.cpp"
        "${AURA_ROOT}/shared/core/src/text_util.cpp"
        "${AURA_ROOT}/shared/core/src/translation.cpp"
        "${AURA_ROOT}/shared/core/src/voice.cpp"
        "${AURA_ROOT}/shared/core/src/aoraton_crypto.cpp"
        "${AURA_ROOT}/shared/core/src/aoraton_real.cpp"
        "${AURA_ROOT}/shared/core/src/json_reader.cpp"
        "${AURA_ROOT}/shared/core/src/aoraton_account.cpp"
        "${AURA_ROOT}/shared/core/src/aoraton_account_worker.cpp"
        "${AURA_ROOT}/shared/core/src/audio_service.cpp"
        "${AURA_ROOT}/shared/core/src/assistant_state.cpp"
        "${AURA_ROOT}/shared/core/src/device_status.cpp"
        "${AURA_ROOT}/shared/core/src/motion_model.cpp"
        "${AURA_ROOT}/shared/core/src/mock_provider.cpp"
        "${AURA_ROOT}/shared/core/src/weather.cpp"
        "${AURA_ROOT}/shared/core/src/wifi.cpp"
        "${AURA_ROOT}/shared/localization/src/companion_strings.cpp"
        "${AURA_ROOT}/shared/localization/src/strings.cpp"
        "${AURA_ROOT}/shared/localization/src/visual_strings.cpp"
        "${AURA_ROOT}/shared/ui/src/aura_ui.cpp"
        "${AURA_ROOT}/shared/ui/src/aura_theme.cpp"
        "${AURA_ROOT}/shared/ui/src/aura_icons.cpp"
        "${AURA_ROOT}/shared/ui/src/aura_keyboard.cpp"
        "${AURA_ROOT}/shared/ui/src/aura_mercury_view.cpp"
        "${AURA_ROOT}/shared/assets/icons/aura_icon_assets.cpp"
        "${AURA_ROOT}/shared/assets/fonts/aura_font_plex_regular_22.c"
        "${AURA_ROOT}/shared/assets/fonts/aura_font_plex_regular_26.c"
        "${AURA_ROOT}/shared/assets/fonts/aura_font_plex_medium_28.c"
        "${AURA_ROOT}/shared/assets/fonts/aura_font_plex_semibold_32.c"
        "${AURA_ROOT}/shared/assets/fonts/aura_font_plex_light_72.c"
        "${AURA_ROOT}/shared/assets/fonts/aura_font_plex_light_96.c"
        "${AURA_ROOT}/shared/assets/fonts/aura_font_space_medium_26.c"
    INCLUDE_DIRS
        "${AURA_ROOT}/shared/core/include"
        "${AURA_ROOT}/shared/localization/include"
        "${AURA_ROOT}/shared/ui/include"
        "${AURA_ROOT}/shared/assets/fonts"
        "${AURA_ROOT}/shared/assets/icons"
    REQUIRES lvgl
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
# ESP-IDF 5.5 compiles C++ as gnu++2b with -Werror. The shared sources are kept
# clean for that mode; scripts/check-firmware-flags.ps1 verifies it on Windows.

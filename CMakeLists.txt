cmake_minimum_required(VERSION 3.24)
project(aura VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(CTest)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
    URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
)
FetchContent_MakeAvailable(SDL2)

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/simulator/config/lv_conf.h" CACHE FILEPATH "" FORCE)
set(LV_BUILD_LVGL_H_SIMPLE_INCLUDE ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    lvgl
    URL ${CMAKE_CURRENT_SOURCE_DIR}/.tools/downloads/lvgl-9.6.0.tar.gz
    URL_HASH SHA256=1c10f77b8e5e7776366d56de5645958f3312b3926b71a2a811d819fc0ad0cbaa
)
FetchContent_MakeAvailable(lvgl)

target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/simulator/config
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/assets/fonts
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)
target_link_libraries(lvgl PRIVATE SDL2::SDL2-static)

add_library(aura_core STATIC
    shared/core/src/aoraton.cpp
    shared/core/src/assistant_companion.cpp
    shared/core/src/briefing.cpp
    shared/core/src/clock_services.cpp
    shared/core/src/display_power.cpp
    shared/core/src/sounds.cpp
    shared/core/src/notes.cpp
    shared/core/src/ota_update.cpp
    shared/core/src/phone_link.cpp
    shared/core/src/text_util.cpp
    shared/core/src/translation.cpp
    shared/core/src/voice.cpp
    shared/core/src/aoraton_crypto.cpp
    shared/core/src/aoraton_real.cpp
    shared/core/src/json_reader.cpp
    shared/core/src/aoraton_account.cpp
    shared/core/src/aoraton_account_worker.cpp
    shared/core/src/audio_service.cpp
    shared/core/src/assistant_state.cpp
    shared/core/src/device_status.cpp
    shared/core/src/motion_model.cpp
    shared/core/src/mock_provider.cpp
    shared/core/src/weather.cpp
    shared/core/src/wifi.cpp
    shared/localization/src/companion_strings.cpp
    shared/localization/src/strings.cpp
    shared/localization/src/visual_strings.cpp
)
target_include_directories(aura_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/localization/include
)

add_library(aura_ui STATIC
    shared/ui/src/aura_ui.cpp
    shared/ui/src/aura_theme.cpp
    shared/ui/src/aura_icons.cpp
    shared/ui/src/aura_keyboard.cpp
    shared/ui/src/aura_mercury_view.cpp
    shared/assets/icons/aura_icon_assets.cpp
    shared/assets/fonts/aura_font_plex_regular_22.c
    shared/assets/fonts/aura_font_plex_regular_26.c
    shared/assets/fonts/aura_font_plex_medium_28.c
    shared/assets/fonts/aura_font_plex_semibold_32.c
    shared/assets/fonts/aura_font_plex_light_72.c
    shared/assets/fonts/aura_font_plex_light_96.c
    shared/assets/fonts/aura_font_space_medium_26.c
)
target_include_directories(aura_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/localization/include
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/assets/fonts
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/assets/icons
)
target_compile_definitions(aura_ui PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE AURA_SIMULATOR=1)
target_link_libraries(aura_ui PUBLIC aura_core lvgl)

add_executable(aura_simulator
    simulator/app/src/main.cpp
    simulator/app/src/control_events.cpp
    simulator/app/src/local_codex_provider.cpp
    simulator/app/src/bridge_http.cpp
    simulator/app/src/bridge_speech_service.cpp
    simulator/app/src/companion_runtime.cpp
    simulator/app/src/screenshot.cpp
    simulator/app/src/open_meteo_weather_service.cpp
    simulator/app/src/weather_store.cpp
    simulator/app/src/windows_audio_backend.cpp
    simulator/app/src/windows_aoraton_crypto.cpp
    simulator/app/src/windows_aoraton_vault.cpp
    simulator/app/src/windows_wifi_service.cpp
    simulator/app/src/fail_closed_aoraton_transport.cpp
    simulator/app/src/winhttp_client.cpp
    simulator/app/src/loopback_sign_in.cpp
    simulator/app/src/aoraton_account_transport.cpp
)
target_include_directories(aura_simulator PRIVATE simulator/app/include)
target_compile_definitions(aura_simulator PRIVATE LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE SDL_MAIN_HANDLED)
target_link_libraries(aura_simulator PRIVATE aura_ui lvgl SDL2::SDL2-static)
if(WIN32)
    target_link_libraries(aura_simulator PRIVATE winhttp ws2_32 shell32 bcrypt crypt32 wlanapi)
    target_link_options(aura_simulator PRIVATE -mconsole)
endif()

if(BUILD_TESTING)
    find_program(AURA_NODE_EXECUTABLE node)
    if(AURA_NODE_EXECUTABLE)
        add_test(NAME aura_aoraton_webcrypto_oracle
            COMMAND ${AURA_NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/aoraton_webcrypto_oracle.mjs)
        add_test(NAME aura_aoraton_vault_vectors
            COMMAND ${AURA_NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/make_aoraton_vault_vectors.mjs --check)
    endif()
    add_executable(aura_core_tests tests/unit/assistant_state_test.cpp)
    target_link_libraries(aura_core_tests PRIVATE aura_core)
    add_test(NAME aura_core_tests COMMAND aura_core_tests)
    add_executable(aura_audio_tests tests/unit/audio_service_test.cpp)
    target_link_libraries(aura_audio_tests PRIVATE aura_core)
    add_test(NAME aura_audio_tests COMMAND aura_audio_tests)
    add_executable(aura_json_reader_tests tests/unit/json_reader_test.cpp)
    target_link_libraries(aura_json_reader_tests PRIVATE aura_core)
    add_test(NAME aura_json_reader_tests COMMAND aura_json_reader_tests)
    add_executable(aura_aoraton_tests tests/unit/aoraton_service_test.cpp)
    target_link_libraries(aura_aoraton_tests PRIVATE aura_core)
    add_test(NAME aura_aoraton_tests COMMAND aura_aoraton_tests)
    add_executable(aura_aoraton_real_tests tests/unit/aoraton_real_service_test.cpp)
    target_link_libraries(aura_aoraton_real_tests PRIVATE aura_core)
    add_test(NAME aura_aoraton_real_tests COMMAND aura_aoraton_real_tests)
    add_executable(aura_aoraton_fail_closed_tests
        tests/unit/fail_closed_aoraton_transport_test.cpp
        simulator/app/src/fail_closed_aoraton_transport.cpp
    )
    target_include_directories(aura_aoraton_fail_closed_tests PRIVATE simulator/app/include)
    target_link_libraries(aura_aoraton_fail_closed_tests PRIVATE aura_core)
    add_test(NAME aura_aoraton_fail_closed_tests COMMAND aura_aoraton_fail_closed_tests)
    add_executable(aura_control_events_tests
        tests/unit/control_events_test.cpp
        simulator/app/src/control_events.cpp
    )
    target_include_directories(aura_control_events_tests PRIVATE simulator/app/include)
    add_test(NAME aura_control_events_tests COMMAND aura_control_events_tests)
    add_executable(aura_wifi_tests tests/unit/wifi_test.cpp)
    target_link_libraries(aura_wifi_tests PRIVATE aura_core)
    add_test(NAME aura_wifi_tests COMMAND aura_wifi_tests)
    add_executable(aura_companion_models_tests tests/unit/companion_models_test.cpp)
    target_link_libraries(aura_companion_models_tests PRIVATE aura_core)
    add_test(NAME aura_companion_models_tests COMMAND aura_companion_models_tests)
    add_executable(aura_ui_recovery_models_tests tests/unit/ui_recovery_models_test.cpp)
    target_link_libraries(aura_ui_recovery_models_tests PRIVATE aura_core)
    add_test(NAME aura_ui_recovery_models_tests COMMAND aura_ui_recovery_models_tests)
    add_executable(aura_ui_assets_tests tests/unit/ui_assets_test.cpp)
    target_link_libraries(aura_ui_assets_tests PRIVATE aura_ui)
    add_test(NAME aura_ui_assets_tests COMMAND aura_ui_assets_tests)
    add_executable(aura_weather_tests
        tests/unit/weather_test.cpp
        simulator/app/src/open_meteo_weather_service.cpp
        simulator/app/src/weather_store.cpp
    )
    target_include_directories(aura_weather_tests PRIVATE simulator/app/include)
    target_link_libraries(aura_weather_tests PRIVATE aura_core)
    if(WIN32)
        target_link_libraries(aura_weather_tests PRIVATE winhttp)
    endif()
    add_test(NAME aura_weather_tests COMMAND aura_weather_tests)
    if(WIN32)
        add_executable(aura_aoraton_crypto_tests
            tests/unit/aoraton_crypto_test.cpp
            simulator/app/src/windows_aoraton_crypto.cpp
        )
        target_include_directories(aura_aoraton_crypto_tests PRIVATE simulator/app/include)
        target_link_libraries(aura_aoraton_crypto_tests PRIVATE aura_core bcrypt)
        add_test(NAME aura_aoraton_crypto_tests COMMAND aura_aoraton_crypto_tests)
        add_executable(aura_aoraton_account_tests
            tests/unit/aoraton_account_test.cpp
            simulator/app/src/windows_aoraton_crypto.cpp
        )
        target_include_directories(aura_aoraton_account_tests PRIVATE simulator/app/include)
        target_compile_definitions(aura_aoraton_account_tests PRIVATE
            AURA_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        target_link_libraries(aura_aoraton_account_tests PRIVATE aura_core bcrypt)
        add_test(NAME aura_aoraton_account_tests COMMAND aura_aoraton_account_tests)
        add_executable(aura_aoraton_account_worker_tests
            tests/unit/aoraton_account_worker_test.cpp
            simulator/app/src/windows_aoraton_crypto.cpp
        )
        target_include_directories(aura_aoraton_account_worker_tests PRIVATE simulator/app/include)
        target_compile_definitions(aura_aoraton_account_worker_tests PRIVATE
            AURA_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        target_link_libraries(aura_aoraton_account_worker_tests PRIVATE aura_core bcrypt)
        add_test(NAME aura_aoraton_account_worker_tests COMMAND aura_aoraton_account_worker_tests)
        add_executable(aura_aoraton_account_smoke
            tests/smoke/aoraton_account_smoke.cpp
            simulator/app/src/winhttp_client.cpp
            simulator/app/src/loopback_sign_in.cpp
            simulator/app/src/aoraton_account_transport.cpp
            simulator/app/src/windows_aoraton_crypto.cpp
            simulator/app/src/windows_aoraton_vault.cpp
        )
        target_include_directories(aura_aoraton_account_smoke PRIVATE simulator/app/include)
        target_link_libraries(aura_aoraton_account_smoke PRIVATE aura_core winhttp ws2_32 shell32 bcrypt crypt32)
        if(AURA_NODE_EXECUTABLE)
            add_test(NAME aura_aoraton_account_smoke
                COMMAND ${AURA_NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/run_aoraton_account_smoke.mjs
                        $<TARGET_FILE:aura_aoraton_account_smoke>)
        endif()
        add_executable(aura_aoraton_vault_tests
            tests/unit/aoraton_vault_test.cpp
            simulator/app/src/windows_aoraton_vault.cpp
        )
        target_include_directories(aura_aoraton_vault_tests PRIVATE simulator/app/include)
        target_link_libraries(aura_aoraton_vault_tests PRIVATE aura_core crypt32)
        add_test(NAME aura_aoraton_vault_tests COMMAND aura_aoraton_vault_tests)
    endif()
endif()

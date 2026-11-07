idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES aura_shared aura_board aura_audio aura_weather aura_aoraton aura_network aura_companion aura_radio
             nvs_flash
             pthread esp32_s3_touch_amoled_2_16
)

# The aoraton.com project's public values (its Supabase URL, anon key and the
# inco opt-in), built in so the owner's phone alone can sign the device in
# (ADR 0010): from the git-ignored config/aoraton.local.json
# (scripts/setup-aoraton.ps1) when it exists at build time. The values the PC
# helper stores in NVS come first. Nothing else is read from the file.
set(aura_aoraton_config "${CMAKE_CURRENT_LIST_DIR}/../../config/aoraton.local.json")
set(AURA_AORATON_BUILT_IN_URL "")
set(AURA_AORATON_BUILT_IN_ANON_KEY "")
set(AURA_AORATON_BUILT_IN_INCO "false")
if(EXISTS "${aura_aoraton_config}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${aura_aoraton_config}")
    file(READ "${aura_aoraton_config}" aura_aoraton_json)
    string(JSON aura_aoraton_url ERROR_VARIABLE aura_aoraton_url_error GET "${aura_aoraton_json}" url)
    string(JSON aura_aoraton_key ERROR_VARIABLE aura_aoraton_key_error GET "${aura_aoraton_json}" anon_key)
    string(JSON aura_aoraton_inco ERROR_VARIABLE aura_aoraton_inco_error GET "${aura_aoraton_json}" inco_enabled)
    # Only the characters such values hold, so nothing leaves the C strings.
    if(NOT aura_aoraton_url_error AND NOT aura_aoraton_key_error AND
       aura_aoraton_url MATCHES "^https://[A-Za-z0-9.-]+$" AND aura_aoraton_key MATCHES "^[A-Za-z0-9._-]+$")
        set(AURA_AORATON_BUILT_IN_URL "${aura_aoraton_url}")
        set(AURA_AORATON_BUILT_IN_ANON_KEY "${aura_aoraton_key}")
        if(NOT aura_aoraton_inco_error AND aura_aoraton_inco)
            set(AURA_AORATON_BUILT_IN_INCO "true")
        endif()
        message(STATUS "AURA: aoraton.com project settings built in from config/aoraton.local.json")
    else()
        message(WARNING "AURA: config/aoraton.local.json holds no project url and anon key; none built in")
    endif()
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/aoraton_build_config.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/aoraton_build_config.h" @ONLY)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

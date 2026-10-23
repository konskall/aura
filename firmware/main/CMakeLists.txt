idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES aura_shared aura_board aura_audio aura_weather aura_aoraton aura_network aura_companion nvs_flash
             esp32_s3_touch_amoled_2_16
)

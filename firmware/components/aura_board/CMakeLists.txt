idf_component_register(
    SRCS "src/aura_board_profile.cpp" "src/aura_display.c" "src/aura_board_chips.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp32_s3_touch_amoled_2_16 esp_driver_i2c aura_shared
)

idf_component_register(
    SRCS "src/aura_board_profile.cpp" "src/aura_display.c" "src/aura_board_chips.cpp"
         "src/aura_internal_stack.cpp" "src/aura_board_rotation.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp32_s3_touch_amoled_2_16 esp_driver_i2c aura_shared
    # aura_display.c starts the panel itself (the BSP's sequence, kept dark).
    PRIV_REQUIRES esp_lcd esp_lcd_co5300 esp_driver_spi esp_lcd_touch_cst9217
)

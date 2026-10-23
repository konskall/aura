idf_component_register(
    SRCS
        "src/esp32_companion.cpp"
        "src/esp32_microphone.cpp"
        "src/esp32_motion.cpp"
        "src/esp32_ota.cpp"
        "src/esp32_phone_link.cpp"
    INCLUDE_DIRS "include"
    REQUIRES aura_shared aura_board esp32_s3_touch_amoled_2_16 esp_codec_dev bt esp_driver_gpio esp_driver_i2c
             esp_http_client esp_https_ota app_update esp_timer mbedtls nvs_flash freertos
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

idf_component_register(
    SRCS
        "src/bsp_amplifier_control.cpp"
        "src/esp32_audio_backend.cpp"
    INCLUDE_DIRS "include"
    REQUIRES aura_shared esp32_s3_touch_amoled_2_16 esp_codec_dev freertos
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

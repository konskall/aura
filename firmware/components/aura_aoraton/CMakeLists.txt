idf_component_register(
    SRCS "src/esp32_aoraton_crypto.cpp" "src/esp32_aoraton_account.cpp" "src/esp32_voice_decoder.cpp"
    INCLUDE_DIRS "include"
    REQUIRES aura_shared mbedtls esp_hw_support esp_http_client esp-tls tcp_transport nvs_flash pthread esp_timer
             freertos log esp_audio_codec
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

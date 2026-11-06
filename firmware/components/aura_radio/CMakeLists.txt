idf_component_register(
    SRCS "src/esp32_radio_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES aura_shared aura_audio esp_http_client esp-tls tcp_transport mbedtls pthread freertos log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

idf_component_register(
    SRCS "src/esp32_aoraton_crypto.cpp"
    INCLUDE_DIRS "include"
    REQUIRES aura_shared mbedtls esp_hw_support
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

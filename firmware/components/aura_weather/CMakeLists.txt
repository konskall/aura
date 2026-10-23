idf_component_register(
    SRCS "src/esp32_weather_backend.cpp"
    INCLUDE_DIRS "include"
    REQUIRES aura_shared esp_http_client nvs_flash json mbedtls
)

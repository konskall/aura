idf_component_register(
    SRCS "src/esp32_network.cpp" "src/esp32_https_client.cpp"
    INCLUDE_DIRS "include"
    REQUIRES aura_shared esp_event esp_netif esp_timer esp_wifi nvs_flash esp_http_client mbedtls
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

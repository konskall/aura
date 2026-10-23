idf_component_register(
    SRCS "src/esp32_network.cpp"
    INCLUDE_DIRS "include"
    REQUIRES aura_shared esp_event esp_netif esp_timer esp_wifi nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

idf_component_register(
    SRCS "dashboard.c" "dashboard_wifi.c" "dashboard_screen.c" "dashboard_data.c" "dashboard_data_parse.c" "dashboard_energy_parse.c" "dashboard_wifi_web.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_event esp_http_client esp_http_server esp_hw_support esp_lcd esp_netif esp_timer esp_wifi heap mbedtls nvs_flash
)

idf_component_get_property(lvgl_dir lvgl__lvgl COMPONENT_DIR)
target_add_binary_data(${COMPONENT_LIB}
    "${lvgl_dir}/examples/libs/tiny_ttf/Ubuntu-Medium.ttf" BINARY
)

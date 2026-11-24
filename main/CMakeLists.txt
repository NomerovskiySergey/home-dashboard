idf_component_register(
    SRCS "dashboard.c" "dashboard_wifi.c" "dashboard_screen.c" "dashboard_data.c" "dashboard_location.c" "dashboard_data_parse.c" "dashboard_alert_details.c" "dashboard_map_view.c" "dashboard_energy_parse.c" "dashboard_wifi_web.c" "dashboard_display_mode.c" "dashboard_theme.c" "dashboard_pda.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_ledc esp_event esp_http_client esp_http_server esp_hw_support esp_lcd esp_netif esp_timer esp_wifi heap mbedtls nvs_flash
)

target_sources(${COMPONENT_LIB} PRIVATE "dashboard_pipboy.c")
target_sources(${COMPONENT_LIB} PRIVATE "dashboard_recovery.c")
target_sources(${COMPONENT_LIB} PRIVATE "dashboard_energy.c")

idf_component_get_property(lvgl_dir lvgl__lvgl COMPONENT_DIR)
target_add_binary_data(${COMPONENT_LIB}
    "${lvgl_dir}/examples/libs/tiny_ttf/Ubuntu-Medium.ttf" BINARY
)
target_add_binary_data(${COMPONENT_LIB} "assets/UbuntuMono-Regular.ttf" BINARY)

#pragma once

#include "esp_err.h"

typedef enum {
    DASH_WIFI_CONNECTING,
    DASH_WIFI_CONNECTED,
    DASH_WIFI_OFFLINE,
    DASH_WIFI_SETUP,
    DASH_WIFI_SETUP_FAILED,
    DASH_WIFI_ERROR,
} dashboard_wifi_state_t;

esp_err_t dashboard_wifi_start(void);
esp_err_t dashboard_wifi_request_setup(void);
dashboard_wifi_state_t dashboard_wifi_state(void);
const char *dashboard_wifi_setup_name(void);
const char *dashboard_wifi_setup_code(void);

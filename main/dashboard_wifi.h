#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    DASH_WIFI_CONNECTING,
    DASH_WIFI_CONNECTED,
    DASH_WIFI_OFFLINE,
    DASH_WIFI_SETUP,
    DASH_WIFI_SETUP_FAILED,
    DASH_WIFI_ERROR,
} dashboard_wifi_state_t;

typedef struct {
    char ssid[33];
    char ipv4[16];
    int rssi_dbm;
    bool associated;
} dashboard_wifi_connection_t;

esp_err_t dashboard_wifi_start(void);
esp_err_t dashboard_wifi_request_setup(void);
dashboard_wifi_state_t dashboard_wifi_state(void);
/* Current association only; empty fields when unavailable. Does not read credentials. */
void dashboard_wifi_connection(dashboard_wifi_connection_t *connection);
const char *dashboard_wifi_setup_name(void);
const char *dashboard_wifi_setup_code(void);

#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "dashboard_data_parse.h"

typedef enum {
    DASH_DATA_WAITING,
    DASH_DATA_FRESH,
    DASH_DATA_STALE,
    DASH_DATA_UNAVAILABLE,
    DASH_DATA_NO_TOKEN,
    DASH_DATA_AUTH_ERROR,
} dashboard_data_state_t;

typedef struct {
    dashboard_data_state_t weather_state;
    dashboard_data_state_t alerts_state;
    int temperature_tenths;
    int64_t weather_updated_us;
    int64_t alerts_updated_us;
    char region_alerts[DASHBOARD_REGION_COUNT];
    dashboard_data_state_t energy_state;
    int64_t energy_updated_us;
    dashboard_energy_schedule_t energy;
} dashboard_data_snapshot_t;

esp_err_t dashboard_data_start(void);
void dashboard_data_snapshot(dashboard_data_snapshot_t *out);

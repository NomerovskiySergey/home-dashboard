#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "dashboard_data_parse.h"
#include "dashboard_alert_details.h"
#include "dashboard_recovery.h"
#include "dashboard_location.h"
#include "dashboard_energy.h"

typedef enum {
    DASH_DATA_WAITING,
    DASH_DATA_FRESH,
    DASH_DATA_STALE,
    DASH_DATA_UNAVAILABLE,
    DASH_DATA_NO_TOKEN,
    DASH_DATA_AUTH_ERROR,
} dashboard_data_state_t;

typedef struct {
    const dashboard_city_t *location;
    uint32_t location_generation;
    dashboard_data_state_t weather_state;
    dashboard_data_state_t alerts_state;
    int temperature_tenths;
    int64_t weather_updated_us;
    int64_t alerts_updated_us;
    char region_alerts[DASHBOARD_REGION_COUNT];
    dashboard_data_state_t alert_details_state;
    int64_t alert_details_updated_us;
    dashboard_data_state_t energy_state;
    int64_t energy_updated_us;
    dashboard_energy_schedule_t energy;
    dashboard_energy_selection_t energy_selection;
    uint32_t energy_generation;
    bool energy_configured;
} dashboard_data_snapshot_t;

esp_err_t dashboard_data_start(void);
/* Called by the UI task; persist before publishing the new selection. */
esp_err_t dashboard_data_select_location(const dashboard_city_t *city);
esp_err_t dashboard_data_select_energy(const dashboard_energy_selection_t *selection);
/* UI requests; only the energy worker performs HTTPS. 0 means invalid/OOM. */
uint32_t dashboard_data_energy_catalog_request(dashboard_energy_catalog_kind_t kind,
                                              const char *area,const char *initiator);
bool dashboard_data_energy_catalog_copy(uint32_t generation,dashboard_energy_catalog_t *out,
                                       dashboard_data_state_t *state);
void dashboard_data_energy_catalog_cancel(void);
/* Nonblocking request to all three service workers; each owns its cleanup. */
bool dashboard_data_restart(void);
void dashboard_data_recovery(dashboard_recovery_t *out);
void dashboard_data_snapshot(dashboard_data_snapshot_t *out);
/* Separate copy avoids putting the bounded detail records on the LVGL task stack. */
bool dashboard_data_alert_details(dashboard_alert_details_t *out, const dashboard_data_snapshot_t *expected);

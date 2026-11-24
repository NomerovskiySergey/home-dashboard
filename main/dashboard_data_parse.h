#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <stdint.h>

enum { DASHBOARD_REGION_COUNT = 78, DASH_ENERGY_SLOTS = 48 };

typedef enum {
    DASH_ENERGY_WAITING,
    DASH_ENERGY_PLANNED,
    DASH_ENERGY_NO_SHUTDOWNS,
    DASH_ENERGY_EMERGENCY,
} dashboard_energy_day_state_t;

typedef enum { DASH_SLOT_CLEAR, DASH_SLOT_POSSIBLE, DASH_SLOT_OFF } dashboard_energy_slot_t;

typedef struct {
    char date[11];
    dashboard_energy_day_state_t state;
    uint8_t slots[DASH_ENERGY_SLOTS];
} dashboard_energy_day_t;

typedef struct {
    dashboard_energy_day_t today;
    dashboard_energy_day_t tomorrow;
} dashboard_energy_schedule_t;

bool dashboard_parse_weather(const char *body, size_t length, int *temperature_tenths);
bool dashboard_parse_alerts(const char *body, size_t length,
                            char out[DASHBOARD_REGION_COUNT]);

bool dashboard_parse_energy(const char *body, size_t length, dashboard_energy_schedule_t *out);
bool dashboard_parse_energy_selected(const char *body, size_t length, const char *initiator,
                                     const char *queue, dashboard_energy_schedule_t *out);

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    const char *id, *name;
    int32_t latitude_e4, longitude_e4;
    uint8_t region;
    int16_t map_x, map_y;
} dashboard_city_t;

typedef struct {
    uint8_t code, alerts_uid;
    const char *name;
} dashboard_region_t;

size_t dashboard_city_count(void);
const dashboard_city_t *dashboard_city_at(size_t index);
const dashboard_city_t *dashboard_city_find(const char *id);
const dashboard_city_t *dashboard_city_default(void);
size_t dashboard_region_count(void);
const dashboard_region_t *dashboard_region_at(size_t index);
const dashboard_region_t *dashboard_region_find(unsigned code);
bool dashboard_city_weather_url(const dashboard_city_t *city, char *out, size_t capacity);
const dashboard_city_t *dashboard_location_load(void);
esp_err_t dashboard_location_save(const dashboard_city_t *city);

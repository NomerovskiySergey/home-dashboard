#pragma once

#include "dashboard_data_parse.h"
#include "esp_err.h"

enum { DASH_ENERGY_ID_SIZE=80, DASH_ENERGY_NAME_SIZE=112, DASH_ENERGY_CATALOG_MAX=64 };
typedef struct {
    char area[DASH_ENERGY_ID_SIZE], area_name[DASH_ENERGY_NAME_SIZE];
    char initiator[DASH_ENERGY_ID_SIZE], supplier_name[DASH_ENERGY_NAME_SIZE];
    char queue[24];
} dashboard_energy_selection_t;

typedef enum { DASH_ENERGY_AREAS, DASH_ENERGY_SUPPLIERS, DASH_ENERGY_QUEUES } dashboard_energy_catalog_kind_t;
typedef struct {
    char id[DASH_ENERGY_ID_SIZE], name[DASH_ENERGY_NAME_SIZE], katottg[24];
} dashboard_energy_entry_t;
typedef struct {
    unsigned count;
    dashboard_energy_entry_t entries[DASH_ENERGY_CATALOG_MAX];
} dashboard_energy_catalog_t;

bool dashboard_energy_id_valid(const char *id, size_t capacity, bool queue);
bool dashboard_energy_selection_valid(const dashboard_energy_selection_t *selection);
bool dashboard_energy_load(dashboard_energy_selection_t *out);
esp_err_t dashboard_energy_save(const dashboard_energy_selection_t *selection);
/* Worker scratch only; discard output on false. Parses one regional record at a time. */
bool dashboard_energy_catalog_parse(const char *body,size_t length,
    dashboard_energy_catalog_kind_t kind,const char *initiator,dashboard_energy_catalog_t *out);
int dashboard_energy_area_hint(const dashboard_energy_catalog_t *catalog,const char *city_katottg);

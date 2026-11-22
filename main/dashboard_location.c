#include "dashboard_location.h"

#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "assets/ukraine_cities.h"

size_t dashboard_city_count(void) { return sizeof(ukraine_cities)/sizeof(ukraine_cities[0]); }
const dashboard_city_t *dashboard_city_at(size_t index)
{ return index<dashboard_city_count() ? &ukraine_cities[index] : NULL; }
const dashboard_city_t *dashboard_city_find(const char *id)
{
    if (id) for(size_t i=0;i<dashboard_city_count();++i)
        if (!strcmp(id,ukraine_cities[i].id)) return &ukraine_cities[i];
    return NULL;
}
const dashboard_city_t *dashboard_city_default(void)
{ return dashboard_city_find("UA12020010010037010"); }
size_t dashboard_region_count(void) { return sizeof(ukraine_regions)/sizeof(ukraine_regions[0]); }
const dashboard_region_t *dashboard_region_at(size_t index)
{ return index<dashboard_region_count() ? &ukraine_regions[index] : NULL; }
const dashboard_region_t *dashboard_region_find(unsigned code)
{
    for(size_t i=0;i<dashboard_region_count();++i)
        if(ukraine_regions[i].code==code) return &ukraine_regions[i];
    return NULL;
}
bool dashboard_city_weather_url(const dashboard_city_t *city,char *out,size_t capacity)
{
    if(!city || !out || !capacity) return false;
    const int length=snprintf(out,capacity,
        "https://api.open-meteo.com/v1/forecast?latitude=%ld.%04ld&longitude=%ld.%04ld"
        "&current=temperature_2m&temperature_unit=celsius",
        (long)(city->latitude_e4/10000),(long)(city->latitude_e4%10000),
        (long)(city->longitude_e4/10000),(long)(city->longitude_e4%10000));
    return length>0 && (size_t)length<capacity;
}
const dashboard_city_t *dashboard_location_load(void)
{
    nvs_handle_t handle;
    const dashboard_city_t *city=NULL;
    if(nvs_open("dash_location",NVS_READONLY,&handle)==ESP_OK) {
        char id[24]; size_t size=sizeof(id);
        if(nvs_get_str(handle,"city",id,&size)==ESP_OK) city=dashboard_city_find(id);
        nvs_close(handle);
    }
    return city ? city : dashboard_city_default();
}
esp_err_t dashboard_location_save(const dashboard_city_t *city)
{
    if(!city || dashboard_city_find(city->id)!=city) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t result=nvs_open("dash_location",NVS_READWRITE,&handle);
    if(result!=ESP_OK) return result;
    result=nvs_set_str(handle,"city",city->id);
    if(result==ESP_OK) result=nvs_commit(handle);
    nvs_close(handle);
    return result;
}

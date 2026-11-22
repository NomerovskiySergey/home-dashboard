#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dashboard_location.h"
#include "nvs.h"

static char stored[24], staged[24];
static int fail_open, fail_write, fail_commit, opens, closes;
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *handle)
{ assert(!strcmp(ns,"dash_location")); ++opens; *handle=1; return fail_open ? ESP_FAIL : ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size)
{
    assert(!strcmp(key,"city"));
    if(!*stored) return ESP_ERR_NVS_NOT_FOUND;
    if(strlen(stored)+1>*size) return ESP_FAIL;
    strcpy(out,stored); *size=strlen(stored)+1; return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *value)
{ assert(!strcmp(key,"city")); strcpy(staged,value); return fail_write ? ESP_FAIL : ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h)
{ if(fail_commit) return ESP_FAIL; strcpy(stored,staged); return ESP_OK; }
void nvs_close(nvs_handle_t h) { ++closes; }

int main(void)
{
    assert(dashboard_city_count()==463);
    assert(dashboard_region_count()==27);
    const dashboard_city_t *dnipro=dashboard_city_default(), *kyiv=NULL, *bila=NULL, *sev=NULL;
    assert(!strcmp(dnipro->name,"Дніпро") && dnipro->region==12);
    assert(dnipro->map_x==280 && dnipro->map_y==106);
    assert(dashboard_city_find("invalid")==NULL);
    unsigned counts[78]={0}; bool duplicates=false;
    for(size_t i=0;i<dashboard_city_count();++i) {
        const dashboard_city_t *city=dashboard_city_at(i);
        assert(city && dashboard_city_find(city->id)==city);
        assert(city->latitude_e4>=440000 && city->latitude_e4<=530000);
        assert(city->longitude_e4>=220000 && city->longitude_e4<=410000);
        assert(city->map_x>=0 && city->map_x<424 && city->map_y>=0 && city->map_y<222);
        assert(dashboard_region_find(city->region)); ++counts[city->region];
        for(size_t j=0;j<i;++j) {
            const dashboard_city_t *other=dashboard_city_at(j);
            assert(strcmp(city->id,other->id));
            if(!strcmp(city->name,other->name)&&city->region!=other->region) duplicates=true;
        }
        if(!strcmp(city->name,"Київ")) kyiv=city;
        if(!strcmp(city->name,"Біла Церква")) bila=city;
        if(!strcmp(city->name,"Севастополь")) sev=city;
    }
    assert(duplicates && kyiv && bila && sev);
    assert(kyiv->region==30 && bila->region==32 && sev->region==40);
    assert(counts[43]>=16);
    for(size_t i=0;i<dashboard_region_count();++i) assert(counts[dashboard_region_at(i)->code]);
    assert(!dashboard_city_at(dashboard_city_count()));
    assert(!dashboard_region_at(27));
    char url[192]; assert(dashboard_city_weather_url(kyiv,url,sizeof(url)));
    assert(strstr(url,"latitude=50.")&&strstr(url,"longitude=30.")&&strstr(url,"temperature_unit=celsius"));
    assert(!dashboard_city_weather_url(kyiv,url,8));
    assert(dashboard_location_load()==dnipro);
    strcpy(stored,"unknown"); assert(dashboard_location_load()==dnipro);
    assert(dashboard_location_save(kyiv)==ESP_OK && dashboard_location_load()==kyiv);
    fail_write=1; assert(dashboard_location_save(bila)!=ESP_OK); fail_write=0;
    assert(dashboard_location_load()==kyiv);
    fail_commit=1; assert(dashboard_location_save(bila)!=ESP_OK); fail_commit=0;
    assert(dashboard_location_load()==kyiv);
    fail_open=1; assert(dashboard_location_save(bila)!=ESP_OK && dashboard_location_load()==dnipro); fail_open=0;
    assert(dashboard_location_save(NULL)!=ESP_OK);
    assert(opens-closes==2); /* Two deliberately failed opens. */
    puts("city catalog, projection, URL and NVS: PASS"); return 0;
}

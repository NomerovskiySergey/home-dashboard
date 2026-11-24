#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dashboard_energy.h"
#include "nvs.h"

static char stored[1024], staged[1024];
static int fail_open, fail_write, fail_commit;
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h)
{ assert(!strcmp(ns,"dash_energy")); *h=1; return fail_open ? ESP_FAIL : ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size)
{ assert(!strcmp(key,"selection")); if(!*stored || strlen(stored)+1>*size) return ESP_FAIL; strcpy(out,stored); return ESP_OK; }
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *value)
{ assert(!strcmp(key,"selection")); strcpy(staged,value); return fail_write ? ESP_FAIL : ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h)
{ if(fail_commit) return ESP_FAIL; strcpy(stored,staged); return ESP_OK; }
void nvs_close(nvs_handle_t h) {}

static void catalog_tests(void)
{
    dashboard_energy_catalog_t cat;
    const char *areas="[{\"slug\":\"kyivska-oblast\",\"name\":\"Київська\",\"katottg\":\"UA32000000000030281\"},"
        "{\"slug\":\"kyiv\",\"name\":\"Київ\",\"katottg\":\"UA80000000000093317\"}]";
    assert(dashboard_energy_catalog_parse(areas,strlen(areas),DASH_ENERGY_AREAS,NULL,&cat));
    assert(cat.count==2 && strcmp(cat.entries[0].id,cat.entries[1].id));
    assert(dashboard_energy_area_hint(&cat,"UA80000000000093317")==0);
    assert(dashboard_energy_area_hint(&cat,"UA32000000000030281")==1);
    assert(dashboard_energy_area_hint(&cat,"UA12020010010037010")==-1);
    const char *ops="[{\"slug\":\"cek\",\"supplier_name\":\"ЦЕК\"},{\"slug\":\"dtek\",\"supplier_name\":\"ДТЕК\"}]";
    assert(dashboard_energy_catalog_parse(ops,strlen(ops),DASH_ENERGY_SUPPLIERS,NULL,&cat));
    assert(cat.count==2);
    const char *rows="[{\"initiator\":\"cek\",\"queue\":\"5.2\"},{\"initiator\":\"dtek\",\"queue\":\"1.1\"},"
        "{\"initiator\":\"cek\",\"queue\":\"1.1\"},{\"initiator\":\"cek\",\"queue\":\"5.2\"}]";
    assert(dashboard_energy_catalog_parse(rows,strlen(rows),DASH_ENERGY_QUEUES,"cek",&cat));
    assert(cat.count==2 && !strcmp(cat.entries[0].id,"1.1") && !strcmp(cat.entries[1].id,"5.2"));
    assert(dashboard_energy_catalog_parse(rows,strlen(rows),DASH_ENERGY_QUEUES,"dtek",&cat));
    assert(cat.count==1 && !strcmp(cat.entries[0].id,"1.1"));
    const char *named="[{\"initiator\":\"cek\",\"queue\":\"Черга 1\"}]";
    assert(dashboard_energy_catalog_parse(named,strlen(named),DASH_ENERGY_QUEUES,"cek",&cat));
    assert(cat.count==1 && !strcmp(cat.entries[0].id,"Черга 1"));
    assert(dashboard_energy_catalog_parse("[]",2,DASH_ENERGY_AREAS,NULL,&cat) && !cat.count);
    assert(!dashboard_energy_catalog_parse("[{}]",4,DASH_ENERGY_AREAS,NULL,&cat));
    assert(!dashboard_energy_catalog_parse("[]bad",5,DASH_ENERGY_AREAS,NULL,&cat));
    assert(!dashboard_energy_catalog_parse("[",1,DASH_ENERGY_AREAS,NULL,&cat));
    const char *bad="[{\"slug\":\"../../x\",\"name\":\"bad\"}]";
    assert(!dashboard_energy_catalog_parse(bad,strlen(bad),DASH_ENERGY_AREAS,NULL,&cat));
    char many[16000]="[";
    for(unsigned i=0;i<65;++i) { char row[100]; snprintf(row,sizeof(row),"%s{\"slug\":\"area-%u\",\"name\":\"Area %u\"}",i?",":"",i,i); strcat(many,row); }
    strcat(many,"]");
    assert(!dashboard_energy_catalog_parse(many,strlen(many),DASH_ENERGY_AREAS,NULL,&cat));
    const char *schedule="[{\"initiator\":\"cek\",\"queue\":\"5.2\",\"today_date\":\"2026-11-23\",\"today_status\":\"NoShutdowns\",\"tomorrow_date\":\"2026-11-24\",\"tomorrow_status\":\"EmergencyShutdowns\"}]";
    dashboard_energy_schedule_t plan;
    assert(dashboard_parse_energy_selected(schedule,strlen(schedule),"cek","5.2",&plan));
    assert(plan.today.state==DASH_ENERGY_NO_SHUTDOWNS && plan.tomorrow.state==DASH_ENERGY_EMERGENCY);
    assert(!dashboard_parse_energy_selected(schedule,strlen(schedule),"dtek","5.2",&plan));
    assert(!dashboard_parse_energy_selected(schedule,strlen(schedule),"cek","1.1",&plan));
}

int main(void)
{
    catalog_tests();
    dashboard_energy_selection_t a,b;
    dashboard_energy_load(&a);
    assert(!strcmp(a.area,"dnipropetrovska-oblast") && !strcmp(a.queue,"5.2"));
    strcpy(a.area,"kyiv"); strcpy(a.area_name,"Київ"); strcpy(a.initiator,"dtek"); strcpy(a.supplier_name,"ДТЕК"); strcpy(a.queue,"1.1");
    assert(dashboard_energy_save(&a)==ESP_OK);
    dashboard_energy_load(&b);
    assert(!strcmp(a.area,b.area) && !strcmp(a.area_name,b.area_name) && !strcmp(a.initiator,b.initiator) &&
           !strcmp(a.supplier_name,b.supplier_name) && !strcmp(a.queue,b.queue));
    strcpy(a.queue,"2.2");
    fail_write=1; assert(dashboard_energy_save(&a)!=ESP_OK); fail_write=0;
    dashboard_energy_load(&b); assert(!strcmp(b.queue,"1.1"));
    fail_commit=1; assert(dashboard_energy_save(&a)!=ESP_OK); fail_commit=0;
    dashboard_energy_load(&b); assert(!strcmp(b.queue,"1.1"));
    fail_open=1; assert(dashboard_energy_save(&a)!=ESP_OK); fail_open=0;
    strcpy(stored,"invalid"); dashboard_energy_load(&b); assert(!strcmp(b.queue,"5.2"));
    strcpy(a.area,"../../bad"); assert(dashboard_energy_save(&a)!=ESP_OK);
    puts("energy catalogs, selected schedule, boundaries and transactional NVS: PASS");
    return 0;
}

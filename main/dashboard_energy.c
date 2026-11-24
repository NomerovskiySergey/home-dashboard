#include "dashboard_energy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cJSON.h"
#include "nvs.h"

bool dashboard_energy_id_valid(const char *s,size_t capacity,bool queue)
{
    if(!s || !capacity) return false;
    for(size_t i=0;i<capacity;++i) {
        const unsigned char c=(unsigned char)s[i];
        if(!c) return i>0;
        /* Queue is an opaque display name, never a URL component. */
        if(queue) { if(c<32 || c==127) return false; else continue; }
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return false;
    }
    return false;
}

static bool name_valid(const char *s,size_t capacity)
{
    for(size_t i=0;i<capacity;++i) {
        if(!s[i]) return i>0;
        if((unsigned char)s[i]<32) return false;
    }
    return false;
}

bool dashboard_energy_selection_valid(const dashboard_energy_selection_t *s)
{
    return s && dashboard_energy_id_valid(s->area,sizeof(s->area),false) &&
        dashboard_energy_id_valid(s->initiator,sizeof(s->initiator),false) &&
        dashboard_energy_id_valid(s->queue,sizeof(s->queue),true) &&
        name_valid(s->area_name,sizeof(s->area_name)) && name_valid(s->supplier_name,sizeof(s->supplier_name));
}

static bool text(const cJSON *object,const char *key,char *out,size_t capacity)
{
    const cJSON *value=cJSON_GetObjectItemCaseSensitive(object,key);
    if(!cJSON_IsString(value) || !name_valid(value->valuestring,capacity)) return false;
    strcpy(out,value->valuestring);
    return true;
}

bool dashboard_energy_load(dashboard_energy_selection_t *out)
{
    *out=(dashboard_energy_selection_t){.area="dnipropetrovska-oblast",.area_name="Дніпропетровська",
        .initiator="dnipropetrovska_oblast_cek",.supplier_name="ЦЕК",.queue="5.2"};
    nvs_handle_t handle;
    if(nvs_open("dash_energy",NVS_READONLY,&handle)!=ESP_OK) return false;
    char stored[1024]; size_t size=sizeof(stored);
    const esp_err_t result=nvs_get_str(handle,"selection",stored,&size);
    nvs_close(handle);
    if(result!=ESP_OK) return false;
    stored[sizeof(stored)-1]=0;
    cJSON *root=cJSON_Parse(stored);
    dashboard_energy_selection_t parsed={0};
    const bool valid=root && text(root,"area",parsed.area,sizeof(parsed.area)) &&
       text(root,"area_name",parsed.area_name,sizeof(parsed.area_name)) &&
       text(root,"initiator",parsed.initiator,sizeof(parsed.initiator)) &&
       text(root,"supplier_name",parsed.supplier_name,sizeof(parsed.supplier_name)) &&
       text(root,"queue",parsed.queue,sizeof(parsed.queue)) && dashboard_energy_selection_valid(&parsed);
    if(valid) *out=parsed;
    cJSON_Delete(root);
    return valid;
}

esp_err_t dashboard_energy_save(const dashboard_energy_selection_t *s)
{
    if(!dashboard_energy_selection_valid(s)) return ESP_ERR_INVALID_ARG;
    cJSON *root=cJSON_CreateObject();
    if(!root) return ESP_ERR_NO_MEM;
    const bool added=cJSON_AddStringToObject(root,"area",s->area) &&
        cJSON_AddStringToObject(root,"area_name",s->area_name) &&
        cJSON_AddStringToObject(root,"initiator",s->initiator) &&
        cJSON_AddStringToObject(root,"supplier_name",s->supplier_name) &&
        cJSON_AddStringToObject(root,"queue",s->queue);
    char *stored=added ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if(!stored) return ESP_ERR_NO_MEM;
    nvs_handle_t handle;
    esp_err_t result=nvs_open("dash_energy",NVS_READWRITE,&handle);
    if(result==ESP_OK) {
        result=nvs_set_str(handle,"selection",stored);
        if(result==ESP_OK) result=nvs_commit(handle);
        nvs_close(handle);
    }
    free(stored);
    return result;
}

static int compare_entry(const void *a,const void *b)
{
    const dashboard_energy_entry_t *x=a,*y=b;
    /* Natural order for numeric queues; names otherwise. */
    if(isdigit((unsigned char)x->name[0]) && isdigit((unsigned char)y->name[0])) {
        const long nx=strtol(x->name,NULL,10),ny=strtol(y->name,NULL,10);
        if(nx!=ny) return nx<ny ? -1 : 1;
    }
    return strcmp(x->name,y->name);
}

bool dashboard_energy_catalog_parse(const char *body,size_t length,dashboard_energy_catalog_kind_t kind,
                                    const char *initiator,dashboard_energy_catalog_t *out)
{
    if(!body || !out || kind>DASH_ENERGY_QUEUES) return false;
    out->count=0;
    const char *p=body,*end=body+length;
    while(p<end && isspace((unsigned char)*p)) ++p;
    if(p==end || *p++!='[') return false;
    while(p<end && isspace((unsigned char)*p)) ++p;
    if(p<end && *p==']') goto finish;
    for(;;) {
        if(p==end || *p!='{') return false;
        const char *start=p;
        unsigned depth=0; bool quoted=false,escaped=false;
        for(;p<end;++p) {
            if(quoted) {
                if(!escaped && *p=='"') quoted=false;
                escaped=!escaped && *p=='\\';
            } else if(*p=='"') quoted=true;
            else if(*p=='{') ++depth;
            else if(*p=='}' && --depth==0) { ++p; break; }
        }
        if(depth || quoted || p-start>32768) return false;
        cJSON *row=cJSON_ParseWithLength(start,(size_t)(p-start));
        if(!cJSON_IsObject(row)) { cJSON_Delete(row); return false; }
        dashboard_energy_entry_t entry={0};
        bool include=true,valid=true;
        if(kind==DASH_ENERGY_QUEUES) {
            char op[DASH_ENERGY_ID_SIZE];
            valid=text(row,"initiator",op,sizeof(op));
            include=valid && initiator && !strcmp(op,initiator);
            if(include) {
                valid=text(row,"queue",entry.id,24) && dashboard_energy_id_valid(entry.id,24,true);
                if(valid) strcpy(entry.name,entry.id);
            }
        } else {
            valid=text(row,"slug",entry.id,sizeof(entry.id)) && dashboard_energy_id_valid(entry.id,sizeof(entry.id),false) &&
                  text(row,kind==DASH_ENERGY_AREAS ? "name" : "supplier_name",entry.name,sizeof(entry.name));
            const cJSON *kat=cJSON_GetObjectItemCaseSensitive(row,"katottg");
            if(kat && !cJSON_IsNull(kat)) valid=valid && text(row,"katottg",entry.katottg,sizeof(entry.katottg));
        }
        cJSON_Delete(row);
        if(!valid) return false;
        if(include) {
            unsigned i=0;
            while(i<out->count && strcmp(out->entries[i].id,entry.id)) ++i;
            if(i==out->count) {
                if(out->count==DASH_ENERGY_CATALOG_MAX) return false;
                out->entries[out->count++]=entry;
            }
        }
        while(p<end && isspace((unsigned char)*p)) ++p;
        if(p==end) return false;
        if(*p==']') break;
        if(*p++!=',') return false;
        while(p<end && isspace((unsigned char)*p)) ++p;
    }
finish:
    ++p;
    while(p<end && isspace((unsigned char)*p)) ++p;
    if(p!=end) return false;
    qsort(out->entries,out->count,sizeof(out->entries[0]),compare_entry);
    return true;
}

int dashboard_energy_area_hint(const dashboard_energy_catalog_t *catalog,const char *city)
{
    if(!catalog || !city || strlen(city)!=19) return -1;
    for(unsigned i=0;i<catalog->count;++i)
        if(strlen(catalog->entries[i].katottg)==19 && !strncmp(catalog->entries[i].katottg,city,4)) return (int)i;
    return -1;
}

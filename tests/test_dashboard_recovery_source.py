"""Exercise the real data worker against a fake ESP-IDF runtime (MSVC required)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

SDK = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define HTTP_EVENT_ON_HEADER 1
#define HTTP_EVENT_ON_DATA 2
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
typedef int nvs_handle_t;
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_set_str(nvs_handle_t,const char *,const char *);
esp_err_t nvs_commit(nvs_handle_t);
typedef struct {
    int event_id;
    void *user_data;
    char *header_key, *header_value;
    int data_len;
    char *data;
} esp_http_client_event_t;
typedef struct {
    const char *url;
    int timeout_ms;
    bool disable_auto_redirect, is_async;
    esp_err_t (*crt_bundle_attach)(void *);
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
typedef struct { bool start; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) { true }
esp_err_t esp_crt_bundle_attach(void *);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
void *heap_caps_malloc(size_t, int);
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *);
esp_err_t esp_netif_sntp_start(void);
int64_t esp_timer_get_time(void);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(unsigned);
esp_err_t nvs_flash_init_partition(const char *);
esp_err_t nvs_open_from_partition(const char *, const char *, int, nvs_handle_t *);
esp_err_t nvs_get_str(nvs_handle_t,const char *,char *,size_t *);
void nvs_close(nvs_handle_t);
const char *esp_err_to_name(esp_err_t);
"""

FIXTURE = r"""
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "sdk_stubs.h"

static void tracked_free(void *);
#define free tracked_free
#include "dashboard_data.c"
#undef free

static int64_t clock_us = 1000000;
static int creates, deletes, cleanups, performs, clients, fail_create;
static int mode, response_status = 200, delays, stop_after;
static bool drive_worker;
static bool fail_city_save;
static int conditional_headers;
static jmp_buf worker_exit;
static esp_http_client_config_t http_config;
static void *allocations[32];
static unsigned live_allocations;

int64_t esp_timer_get_time(void) { return clock_us; }
const char *esp_err_to_name(esp_err_t err) { return "fake"; }
esp_err_t esp_crt_bundle_attach(void *p) { return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(config->is_async);
    assert(clients == 0);
    clients++;
    http_config = *config;
    return &http_config;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char *k,const char *v)
{ if(!strcmp(k,"If-Modified-Since")) ++conditional_headers; return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t c)
{
    performs++;
    if (mode == 1) { assert(dashboard_data_restart()); return ESP_ERR_HTTP_EAGAIN; }
    if (mode == 2) return ESP_ERR_HTTP_EAGAIN; /* peer never completes */
    if (response_status == 429) {
        esp_http_client_event_t event = { .event_id=HTTP_EVENT_ON_HEADER,
            .header_key="Retry-After", .header_value="120", .user_data=http_config.user_data };
        http_config.event_handler(&event);
    }
    if (mode == 4 || mode == 5) {
        assert(strstr(http_config.url,"latitude=48.4647"));
        assert(dashboard_data_select_location(dashboard_city_find("UA80000000000093317"))==ESP_OK);
        if(mode==5) assert(dashboard_data_select_location(dashboard_city_default())==ESP_OK);
    }
    if(mode==6) {
        char expected[192];
        assert(dashboard_city_weather_url(latest.location,expected,sizeof(expected)));
        assert(!strcmp(http_config.url,expected));
    }
    if (mode >= 4 && mode <= 6) {
        const char *json="{\"current_units\":{\"temperature_2m\":\"°C\"},\"current\":{\"temperature_2m\":17}}";
        esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_DATA,.data=(char *)json,.data_len=(int)strlen(json),.user_data=http_config.user_data};
        http_config.event_handler(&e);
    }
    if(mode>=7) {
        if(mode==7 || mode==9)
            assert(dashboard_data_select_location(dashboard_city_find("UA80000000000093317"))==ESP_OK);
        const char *json="{\"alerts\":[{\"location_uid\":9,\"location_type\":\"oblast\",\"alert_type\":\"air_raid\"}]}";
        esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_DATA,.data=(char *)json,.data_len=(int)strlen(json),.user_data=http_config.user_data};
        http_config.event_handler(&e);
        e=(esp_http_client_event_t){.event_id=HTTP_EVENT_ON_HEADER,.header_key="Last-Modified",.header_value="v1",.user_data=http_config.user_data};
        http_config.event_handler(&e);
    }
    if (mode == 3) assert(dashboard_data_restart()); /* response completes as reset arrives */
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { return response_status; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{ assert(clients == 1); clients--; cleanups++; return ESP_OK; }
void *heap_caps_malloc(size_t size, int caps)
{
    void *p = malloc(size);
    assert(p && live_allocations < 32);
    allocations[live_allocations++] = p;
    return p;
}
static void tracked_free(void *p)
{
    if (!p) return;
    unsigned i;
    for (i=0; i<live_allocations; ++i) if (allocations[i]==p) break;
    assert(i < live_allocations);
    allocations[i] = allocations[--live_allocations];
    free(p);
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c) { return ESP_OK; }
esp_err_t esp_netif_sntp_start(void) { return ESP_OK; }
int xTaskCreate(void (*f)(void *),const char *n,unsigned stack,void *arg,unsigned prio,TaskHandle_t *h)
{ creates++; if(creates == fail_create) return 0; if(h) *h=(void *)1; return pdPASS; }
void vTaskDelete(TaskHandle_t task) { deletes++; }
void vTaskDelay(unsigned ms)
{
    clock_us += (int64_t)ms * 1000;
    if (drive_worker && ms == 1000) {
        delays++;
        for (int i=0; i<WORKER_COUNT; ++i) if (i!=WORKER_ALERTS) {
            dashboard_recovery_heartbeat(&worker_recovery[i],clock_us);
            if (worker_recovery[i].pending) dashboard_recovery_finish(&worker_recovery[i],clock_us);
        }
        dashboard_recovery_group_update(worker_recovery,WORKER_COUNT,&recovery,clock_us);
        assert(recovery.state != DASH_RECOVERY_FAILED);
        if (delays == stop_after) longjmp(worker_exit, 1);
        /* Repeated manual requests throughout 120 seconds offline. */
        dashboard_data_restart();
    }
}
esp_err_t nvs_flash_init_partition(const char *p) { return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *p,const char *ns,int mode,nvs_handle_t *h)
{ *h=1; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *out,size_t *n)
{ strcpy(out,"fake-key"); *n=9; return ESP_OK; }
void nvs_close(nvs_handle_t h) {}
esp_err_t nvs_open(const char *ns,int m,nvs_handle_t *h) { *h=1; return ESP_OK; }
esp_err_t nvs_set_str(nvs_handle_t h,const char *k,const char *v) { return fail_city_save ? ESP_FAIL : ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { return ESP_OK; }
dashboard_wifi_state_t dashboard_wifi_state(void) { return DASH_WIFI_OFFLINE; }

int main(void)
{
    assert(dashboard_data_start() == ESP_OK && creates == 4);
    assert(dashboard_data_start() == ESP_ERR_INVALID_STATE && creates == 4);
    char storage[256];
    http_body_t body = { .text=storage, .capacity=sizeof(storage) };
    /* Completion after A->B and A->B->A must never publish the old request. */
    for(int m=4;m<=5;++m) {
        assert(dashboard_data_select_location(dashboard_city_default())==ESP_OK);
        mode=m; update_weather(&body);
        assert(latest.weather_updated_us==0 && latest.weather_state==DASH_DATA_WAITING);
    }
    assert(dashboard_data_select_location(dashboard_city_find("UA80000000000093317"))==ESP_OK);
    assert(dashboard_data_select_location(dashboard_city_default())==ESP_OK);
    mode=6; update_weather(&body);
    assert(latest.weather_updated_us && latest.temperature_tenths==170);
    assert(dashboard_data_select_location(dashboard_city_find("UA80000000000093317"))==ESP_OK);
    mode=6; update_weather(&body);
    assert(latest.weather_updated_us && latest.weather_state==DASH_DATA_FRESH);
    assert(dashboard_data_select_location(dashboard_city_default())==ESP_OK);
    const uint32_t generation=latest.location_generation;
    fail_city_save=true;
    assert(dashboard_data_select_location(dashboard_city_find("UA80000000000093317"))!=ESP_OK);
    assert(latest.location==dashboard_city_default() && latest.location_generation==generation);
    fail_city_save=false;
    /* Old-location 200/304 cannot publish records or seed a matching cache. */
    for(int m=7;m<=9;m+=2) {
        assert(dashboard_data_select_location(dashboard_city_default())==ESP_OK);
        mode=8; response_status=200; update_alert_details("fake",&body);
        assert(latest.alert_details_updated_us && alert_details.count==1);
        response_status=m==9?304:200; mode=m;
        update_alert_details("fake",&body);
        assert(!latest.alert_details_updated_us && !details_cache.valid);
        conditional_headers=0; mode=8; response_status=304;
        update_alert_details("fake",&body);
        assert(!latest.alert_details_updated_us && !conditional_headers);
    }
    assert(dashboard_data_select_location(dashboard_city_default())==ESP_OK);
    mode=4; response_status=500;
    update_weather(&body);
    assert(retry_not_before[SOURCE_WEATHER]>=clock_us+SECONDS_US(60));
    mode=8; response_status=503;
    update_alert_details("fake",&body);
    assert(retry_not_before[SOURCE_ALERT_DETAILS]>=clock_us+SECONDS_US(60));
    retry_not_before[SOURCE_WEATHER]=retry_not_before[SOURCE_ALERT_DETAILS]=0;
    response_status=200; mode=0; performs=cleanups=0;
    latest.weather_updated_us=123;
    latest.temperature_tenths=170;
    latest.weather_state=DASH_DATA_FRESH;
    mode=1;
    update_weather(&body);
    assert(body.cancelled && performs==1 && cleanups==1 && clients==0);
    assert(latest.weather_updated_us==123 && latest.temperature_tenths==170);
    dashboard_data_snapshot_t snapshot;
    dashboard_data_snapshot(&snapshot);
    assert(snapshot.weather_state==DASH_DATA_STALE);
    assert(!dashboard_data_restart() && creates==4);
    for(int i=0;i<WORKER_COUNT;++i) {
        dashboard_recovery_begin(&worker_recovery[i],clock_us);
        dashboard_recovery_finish(&worker_recovery[i],clock_us);
    }
    dashboard_recovery_group_update(worker_recovery,WORKER_COUNT,&recovery,clock_us);
    mode=0;
    response_status=429;
    assert(fetch_json(SOURCE_ENERGY,"fake",&body)==429);
    const int64_t paused_until=retry_not_before[SOURCE_ENERGY];
    assert(paused_until==clock_us+120000000);
    response_status=200;
    mode=2;
    const int before=cleanups;
    char timeout_url[192];
    assert(dashboard_city_weather_url(dashboard_city_default(),timeout_url,sizeof(timeout_url)));
    body.url=timeout_url;
    fetch_json(SOURCE_WEATHER,NULL,&body);
    body.url=NULL;
    assert(!body.cancelled && cleanups==before+1 && clients==0);
    assert(clock_us>=21000000); /* absolute request deadline */
    /* Existing rate limits/cache remain through repeated worker-owned cleanup. */
    map_cache.valid=true;
    strcpy(map_cache.modified,"cached");
    alerts_rate.count=1;
    alerts_rate.requests[0]=clock_us/1000000;
    alerts_rate.blocked_until=clock_us/1000000+300;
    const int64_t blocked_until=alerts_rate.blocked_until;
    mode=0;
    drive_worker=true;
    stop_after=120;
    if (setjmp(worker_exit)==0) data_task(&workers[WORKER_ALERTS]);
    drive_worker=false;
    assert(recovery.restarts>10 && live_allocations==1);
    assert(creates==4 && deletes==0 && clients==0);
    assert(map_cache.valid && !strcmp(map_cache.modified,"cached"));
    assert(alerts_rate.count==1 && alerts_rate.blocked_until==blocked_until);
    assert(retry_not_before[SOURCE_ENERGY]==paused_until);
    assert(latest.weather_updated_us==123 && latest.temperature_tenths==170);
    while(live_allocations) tracked_free(allocations[0]);
    for(int i=0;i<WORKER_COUNT;++i)
        if(worker_recovery[i].pending) dashboard_recovery_finish(&worker_recovery[i],clock_us);
    dashboard_recovery_group_update(worker_recovery,WORKER_COUNT,&recovery,clock_us);
    clock_us += DASH_RECOVERY_COOLDOWN_US;
    alerts_rate=(dashboard_alert_rate_t){0};
    mode=3;
    response_status=429;
    assert(fetch_json(SOURCE_ALERTS,"fake",&body)==0 && body.cancelled);
    assert(alerts_rate.blocked_until>=clock_us/1000000+120);
    assert(!dashboard_alert_rate_take(&alerts_rate,clock_us/1000000));
    /* Failure to allocate a worker never leaves a supervisor or duplicates. */
    data_started=false;
    startup_state=START_NOT_STARTED; /* simulate a fresh boot in the mock runtime */
    fail_create=creates+2;
    assert(dashboard_data_start()==ESP_ERR_NO_MEM);
    assert(!data_started && deletes==0 && recovery.state==DASH_RECOVERY_FAILED);
    assert(!dashboard_data_restart());
    puts("real data worker recovery: PASS");
    return 0;
}
"""



UI_FIXTURE = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdk_stubs.h"
#include "dashboard_data.h"
typedef struct object object;
typedef struct { object *target; void *data; } lv_event_t;
struct object { object *children[128]; unsigned count; void *data; int x,y,width,height,scroll,long_mode,font_height; void (*scroll_cb)(lv_event_t *); void (*draw_cb)(lv_event_t *); char text[128]; };
typedef struct { int line_height; } lv_font_t;
typedef struct { int x,y; } lv_point_precise_t;
typedef struct { int x1,y1,x2,y2; } lv_area_t;
typedef struct { int width,color; bool round_start,round_end; lv_point_precise_t p1,p2; } lv_draw_line_dsc_t;
static lv_draw_line_dsc_t drawn[4];static unsigned drawn_count;
static lv_font_t standard_font={20},compact_font={25};
static lv_font_t *body_font=&standard_font,*pda_value_font=&compact_font;
static object title_label;static struct { object *city; } home={&title_label};
static int lv_font_get_line_height(const lv_font_t *font) { return font->line_height; }
static void lv_obj_set_style_text_font(object *o,const lv_font_t *f,int part) { o->font_height=f->line_height; }
static void lv_obj_set_size(object *o,int w,int h) { o->width=w;o->height=h; }
static void lv_obj_set_pos(object *o,int x,int y) { o->x=x;o->y=y; }
static void lv_obj_get_coords(object *o,lv_area_t *a) { *a=(lv_area_t){o->x,o->y,o->x+o->width-1,o->y+o->height-1}; }
static void lv_draw_line_dsc_init(lv_draw_line_dsc_t *d) { memset(d,0,sizeof(*d)); }
static void *lv_event_get_layer(lv_event_t *e) { return NULL; }
static int lv_obj_get_style_text_color(object *o,int part) { return 1; }
static void lv_draw_line(void *layer,lv_draw_line_dsc_t *d) { assert(drawn_count<4);drawn[drawn_count++]=*d; }
static void lv_obj_invalidate(object *o) {}
#define LV_PART_MAIN 0
#define LV_ALIGN_LEFT_MID 1
static void lv_obj_align(object *o,int align,int x,int y) { o->x=12+x; }
#define LV_EVENT_DRAW_POST 2
#define LV_LABEL_LONG_MODE_SCROLL_CIRCULAR 3
typedef object lv_obj_t;
#define LV_ANIM_OFF 0
#define LV_LABEL_LONG_DOT 0
#define LV_EVENT_SCROLL 1
#define DASH_COLOR_BACKGROUND 0
static object home_root,city_view,location_view,city_container,region_container,error_label,heading;
static object *location_list=&region_container,*location_rows[8],*location_extent;
static bool location_rows_updating;
static object *home_screen=&home_root,*city_screen=&city_view,*location_screen=&location_view;
static object *city_list=&city_container,*location_error=&error_label,*city_heading=&heading;
static const dashboard_city_t *selected_city;
static object *city_rows[8],*city_extent;
static const dashboard_region_t *city_region;
static unsigned city_count;
static bool city_rows_updating;
static object *active;
static const dashboard_city_t *saved_ui_city;
static void *lv_event_get_user_data(lv_event_t *e) { return e->data; }
static object *lv_event_get_target_obj(lv_event_t *e) { return e->target; }
static void *lv_obj_get_user_data(object *o) { return o->data; }
static void lv_obj_set_user_data(object *o,void *d) { o->data=d; }
static void lv_label_set_text(object *o,const char *t) { snprintf(o->text,sizeof(o->text),"%s",t); }
static void lv_screen_load(object *o) { active=o; }
static void update_remote_data(void *t) {}
static object *make(object *p) { assert(p->count<128); object *o=calloc(1,sizeof(*o)); assert(o);p->children[p->count++]=o;return o; }
static unsigned lv_obj_get_child_count(object *o) { return o->count; }
static void lv_obj_clean(object *o) { for(unsigned i=0;i<o->count;++i) { lv_obj_clean(o->children[i]);free(o->children[i]); } o->count=0; }
static void lv_obj_scroll_to_y(object *o,int y,int anim) { o->scroll=y; }
static int lv_obj_get_scroll_y(object *o) { return o->scroll; }
static void lv_obj_set_height(object *o,int h) { o->height=h; }
static void lv_obj_set_width(object *o,int w) { o->width=w; }
static void lv_obj_set_y(object *o,int y) { o->y=y; }
static object *lv_obj_get_child(object *o,unsigned n) { assert(n<o->count);return o->children[n]; }
static void lv_label_set_long_mode(object *o,int m) { o->long_mode=m; }
static void lv_obj_set_hidden(object *o,bool hidden) {}
static void lv_obj_add_event_cb(object *o,void (*cb)(lv_event_t *),int event,void *d) { if(event==LV_EVENT_SCROLL) o->scroll_cb=cb; else if(event==LV_EVENT_DRAW_POST) o->draw_cb=cb; }
static object *surface(object *p,int x,int y,int w,int h,int c,int radius) { object *o=make(p);o->height=h;return o; }
static object *button(object *p,const char *text,int x,int y,int w,void (*cb)(lv_event_t *),void *d)
{ object *o=make(p);o->data=d;o->y=y;o->width=w;object *caption=make(o);lv_label_set_text(caption,text);return o; }
esp_err_t dashboard_data_select_location(const dashboard_city_t *c) { saved_ui_city=c; return ESP_OK; }
esp_err_t nvs_open(const char *s,int m,nvs_handle_t *h) { return ESP_FAIL; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *o,size_t *n) { return ESP_FAIL; }
esp_err_t nvs_set_str(nvs_handle_t h,const char *k,const char *v) { return ESP_FAIL; }
esp_err_t nvs_commit(nvs_handle_t h) { return ESP_FAIL; }
void nvs_close(nvs_handle_t h) {}
"""
UI_MAIN = r"""
int main(void) {
 prepare_location_list();
 assert(location_list->count<=9);
 prepare_location_list();assert(location_list->count<=9);
 for(unsigned first=0;first<dashboard_region_count();++first) {
   location_list->scroll=(int)first*58;refresh_location_rows(NULL);
   for(unsigned i=0;i<location_list->count;++i) {
     object *row=location_list->children[i];
     if(!row->data) continue;
     const dashboard_region_t *expected=dashboard_region_at((size_t)(row->y/58));
     assert(row->data==expected && !strcmp(row->children[0]->text,expected->name));
     lv_event_t touch={.target=row};open_city_list(&touch);
     assert(city_region==expected && city_list->count<=9);
   }
 }
 lv_obj_clean(location_list);location_extent=NULL;
 prepare_location_list();assert(location_list->count<=9);
 selected_city=dashboard_city_default();
 for(size_t r=0;r<dashboard_region_count();++r) {
   const dashboard_region_t *region=dashboard_region_at(r);
   object region_button={.data=(void *)region};
   lv_event_t e={.target=&region_button,.data=(void *)region};open_city_list(&e);
   assert(city_list->count<=9); /* At most eight reusable rows plus scroll extent. */
   unsigned total=0;
   for(size_t i=0;i<dashboard_city_count();++i) if(dashboard_city_at(i)->region==region->code) ++total;
   for(unsigned first=0;first<total;++first) {
     city_list->scroll=(int)first*58;
     if(city_list->scroll_cb) city_list->scroll_cb(&e);
     for(unsigned i=0;i<city_list->count;++i) {
       object *row=city_list->children[i];
       if(row->data) {
         const dashboard_city_t *expected=NULL;unsigned ordinal=0;
         for(size_t c=0;c<dashboard_city_count();++c) if(dashboard_city_at(c)->region==region->code) {
           if(ordinal++==(unsigned)(row->y/58)) { expected=dashboard_city_at(c);break; }
         }
         assert(row->data==expected);
         assert(!strcmp(row->children[0]->text,expected->name));
         assert(row->children[0]->width<=row->width-40);
         assert(row->children[0]->x+row->children[0]->width<row->width-25);
         assert(row->draw_cb);
         const dashboard_city_t *previous=selected_city;
         lv_event_t paint={.target=row};
         selected_city=expected;drawn_count=0;row->draw_cb(&paint);
         assert(drawn_count==2);
         for(unsigned d=0;d<drawn_count;++d) {
           assert(drawn[d].p1.x>=row->x && drawn[d].p1.x<row->x+row->width);
           assert(drawn[d].p2.x>=row->x && drawn[d].p2.x<row->x+row->width);
           assert(drawn[d].p1.y>=row->y && drawn[d].p1.y<row->y+row->height);
           assert(drawn[d].p2.y>=row->y && drawn[d].p2.y<row->y+row->height);
         }
         selected_city=NULL;drawn_count=0;row->draw_cb(&paint);assert(!drawn_count);
         selected_city=previous;
         lv_event_t touch={.target=row,.data=(void *)region};select_city(&touch);
         assert(saved_ui_city==expected && active==home_screen);
       }
     }
     assert(city_list->count<=9);
   }
 }
 lv_obj_clean(city_list);puts("city list bounded allocations and recycled region bindings: PASS");return 0;
}
"""

class RecoveryIntegration(unittest.TestCase):
    def test_home_city_stays_on_one_line(self):
        screen = (ROOT / "main/dashboard_screen.c").read_text(encoding="utf-8")
        begin = screen.index("    lv_label_set_text(home.city,")
        end = screen.index("    lv_obj_set_style_text_font(home_clock,", begin)
        helper = ""
        if "static void layout_city_title(" in screen:
            helper = screen[screen.index("static void layout_city_title("):screen.index("static void apply_home_layout(")]
        snippet = helper + "\nstatic void title_layout(bool compact) {\n" + screen[begin:end] + "\n}\n"
        main = r"""
int main(void) {
 const char *names[]={"Дніпро","Могилів-Подільський","Івано-Франківськ"};
 for(unsigned compact=0;compact<2;++compact) for(unsigned n=0;n<3;++n) {
   dashboard_city_t city={.name=names[n]};selected_city=&city;
   title_layout(compact!=0);
   assert(!strcmp(home.city->text,names[n]));
   assert(home.city->height==home.city->font_height && home.city->height>0);
   assert(home.city->y+home.city->height<=58);
   assert(home.city->x+home.city->width<147);
   assert(home.city->long_mode==LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
 }
 puts("home city single-line bounds / standard and compact fonts: PASS");return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="city-title-", dir=ROOT / "build") as directory:
            path = Path(directory)
            (path / "sdk_stubs.h").write_text(SDK, encoding="utf-8")
            for header in ["esp_err.h", "nvs.h"]:
                (path / header).write_text('#include "sdk_stubs.h"\n', encoding="utf-8")
            (path / "title.c").write_text(UI_FIXTURE + snippet + main, encoding="utf-8")
            executable = path / "title.exe"
            compiled = subprocess.run(["cl", "/nologo", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                "/I" + str(path), "/I" + str(ROOT / "main"), str(path / "title.c"),
                str(ROOT / "main/dashboard_location.c"), "/Fe:" + str(executable)],
                cwd=path, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            tested = subprocess.run([str(executable)], cwd=path, capture_output=True, text=True, timeout=10)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
            print(tested.stdout.strip())

    def test_city_list_bounded_allocations(self):
        screen = (ROOT / "main/dashboard_screen.c").read_text(encoding="utf-8")
        snippet = screen[screen.index("static void select_city("):screen.index("static lv_obj_t *location_scroller(")]
        legacy_regions = ""
        if "static void prepare_location_list(" not in screen:
            begin = screen.index("    for(size_t i=0;i<dashboard_region_count();++i) {", screen.index("static void add_location_screens("))
            end = screen.index('    label(city_screen,"Місто"', begin)
            legacy_regions = "\nstatic void prepare_location_list(void) {\n" + screen[begin:end] + "\n}\n"
        with tempfile.TemporaryDirectory(prefix="location-ui-", dir=ROOT / "build") as directory:
            path = Path(directory)
            (path / "sdk_stubs.h").write_text(SDK, encoding="utf-8")
            for header in ["esp_err.h", "nvs.h"]:
                (path / header).write_text('#include "sdk_stubs.h"\n', encoding="utf-8")
            (path / "ui.c").write_text(UI_FIXTURE + snippet + legacy_regions + UI_MAIN, encoding="utf-8")
            executable = path / "ui.exe"
            compiled = subprocess.run(["cl", "/nologo", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                "/I" + str(path), "/I" + str(ROOT / "main"), str(path / "ui.c"),
                str(ROOT / "main/dashboard_location.c"), "/Fe:" + str(executable)],
                cwd=path, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            tested = subprocess.run([str(executable)], cwd=path, capture_output=True, text=True, timeout=10)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
            print(tested.stdout.strip())

    def test_city_catalog_and_persistence(self):
        with tempfile.TemporaryDirectory(prefix="location-test-", dir=ROOT / "build") as directory:
            path = Path(directory)
            (path / "sdk_stubs.h").write_text(SDK, encoding="utf-8")
            for header in ["esp_err.h", "nvs.h"]:
                (path / header).write_text('#include "sdk_stubs.h"\n', encoding="utf-8")
            executable = path / "location.exe"
            compiled = subprocess.run(["cl", "/nologo", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                "/I" + str(path), "/I" + str(ROOT / "main"),
                str(ROOT / "tests/dashboard_location_test.c"), str(ROOT / "main/dashboard_location.c"),
                "/Fe:" + str(executable)], cwd=path, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            tested = subprocess.run([str(executable)], cwd=path, capture_output=True, text=True, timeout=10)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
            print(tested.stdout.strip())

    def test_worker_cancellation_cleanup_backoff_and_offline(self):
        with tempfile.TemporaryDirectory(prefix="recovery-test-", dir=ROOT / "build") as directory:
            path = Path(directory)
            (path / "sdk_stubs.h").write_text(SDK, encoding="utf-8")
            headers = ["esp_err.h", "esp_crt_bundle.h", "esp_http_client.h",
                       "esp_heap_caps.h", "esp_log.h", "esp_netif_sntp.h", "esp_timer.h",
                       "nvs.h", "nvs_flash.h", "freertos/FreeRTOS.h", "freertos/task.h"]
            for header in headers:
                target = path / header
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('#include "sdk_stubs.h"\n', encoding="utf-8")
            (path / "strings.h").write_text(
                '#include <string.h>\n#define strcasecmp _stricmp\n', encoding="utf-8")
            fixture = path / "fixture.c"
            fixture.write_text(FIXTURE, encoding="utf-8")
            cjson = ROOT / "managed_components/espressif__cjson/cJSON"
            sources = [fixture, ROOT / "main/dashboard_recovery.c", ROOT / "main/dashboard_location.c",
                       ROOT / "main/dashboard_data_parse.c", ROOT / "main/dashboard_energy_parse.c",
                       ROOT / "main/dashboard_alert_details.c", cjson / "cJSON.c"]
            command = ["cl", "/nologo", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                       "/I" + str(path), "/I" + str(ROOT / "main"), "/I" + str(cjson),
                       *map(str, sources), "/Fe:" + str(path / "recovery_integration.exe")]
            compiled = subprocess.run(command, cwd=path, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            tested = subprocess.run([str(path / "recovery_integration.exe")],
                                    cwd=path, capture_output=True, text=True, timeout=20)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
            print(tested.stdout.strip())


if __name__ == "__main__":
    unittest.main()

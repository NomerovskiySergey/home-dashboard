#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dashboard_data.h"
#include "dashboard_theme.h"
#include "dashboard_wifi.h"

typedef struct object {
    struct object *children[64];
    unsigned count,writes;
    int x,color;
    bool hidden;
    char text[512];
} lv_obj_t;
typedef struct { int unused; } lv_timer_t;
typedef int lv_color_t;
static unsigned created,cleaned,label_writes;
static int body_font;
enum { CARD_WIDTH=448,MINUTES_PER_DAY=1440,LV_STATE_USER_1=1,LV_PART_MAIN=0 };
static lv_obj_t home_track,detail_track,intervals,home_note,detail_note,detail_date,detail_age;
static lv_obj_t home_view,schedule_view,other_view,heading,alert_card,value,note,threat;
static lv_obj_t *energy_home_track=&home_track,*energy_detail_track=&detail_track,*energy_intervals=&intervals;
static lv_obj_t *energy_home_note=&home_note,*energy_detail_note=&detail_note,*energy_detail_date=&detail_date,*energy_detail_age=&detail_age;
static lv_obj_t *home_screen=&home_view,*schedule_screen=&schedule_view,*active=&home_view;
static lv_obj_t *alert_value=&value,*alert_note=&note,*alert_threat=&threat;
static struct { lv_obj_t *power_heading,*alert_card; } home={&heading,&alert_card};
static bool energy_show_tomorrow;
static dashboard_alert_details_t screen_alert_details;
static dashboard_theme_t selected_theme;
static const dashboard_theme_t *theme=&selected_theme;
static dashboard_palette_t colors;
static const dashboard_palette_t *palette=&colors;
static time_t now;
static struct tm local_clock;
static time_t fake_time(time_t *out) { if(out) *out=now;return now; }
#define time fake_time
static time_t kyiv_clock(struct tm *local) { *local=local_clock;return now; }
int64_t esp_timer_get_time(void) { return (int64_t)now*1000000; }
dashboard_wifi_state_t dashboard_wifi_state(void) { return DASH_WIFI_CONNECTED; }
static lv_obj_t *lv_screen_active(void) { return active; }
static void lv_label_set_text(lv_obj_t *o,const char *text)
{ ++o->writes;++label_writes;snprintf(o->text,sizeof(o->text),"%s",text); }
static const char *lv_label_get_text(lv_obj_t *o) { return o->text; }
static void lv_label_set_text_fmt(lv_obj_t *o,const char *fmt,...)
{ char text[512];va_list args;va_start(args,fmt);vsnprintf(text,sizeof(text),fmt,args);va_end(args);lv_label_set_text(o,text); }
static void set_label_if_changed(lv_obj_t *,const char *);
static void lv_obj_clean(lv_obj_t *o)
{ ++cleaned;for(unsigned i=0;i<o->count;++i) free(o->children[i]);o->count=0; }
static lv_obj_t *surface(lv_obj_t *p,int x,int y,int w,int h,int color,int radius)
{ assert(p->count<64);lv_obj_t *o=calloc(1,sizeof(*o));assert(o);p->children[p->count++]=o;o->x=x;++created;return o; }
static lv_obj_t *label(lv_obj_t *p,const char *text,int font,int color,int x,int y)
{ lv_obj_t *o=surface(p,x,y,1,1,color,0);lv_label_set_text(o,text);return o; }
static void lv_obj_set_state(lv_obj_t *o,int state,bool enabled) {}
static void lv_obj_set_hidden(lv_obj_t *o,bool hidden) { o->hidden=hidden; }
static void lv_obj_set_x(lv_obj_t *o,int x) { o->x=x; }
static lv_color_t lv_color_hex(unsigned color) { return (int)color; }
static bool lv_color_eq(lv_color_t a,lv_color_t b) { return a==b; }
static lv_color_t lv_obj_get_style_text_color(lv_obj_t *o,int part) { return o->color; }
static void lv_obj_set_style_text_color(lv_obj_t *o,lv_color_t color,int part) { o->color=color; }
static void lv_obj_invalidate(lv_obj_t *o) {}

/* INSERT UI */

int main(int argc,char **argv)
{
    local_clock=(struct tm){.tm_year=126,.tm_mon=10,.tm_mday=24,.tm_hour=12};
    now=mktime(&local_clock);
    dashboard_city_t city={.region=12};
    dashboard_data_snapshot_t data={.location=&city,.alerts_state=DASH_DATA_FRESH,
        .alert_details_state=DASH_DATA_FRESH,.alerts_updated_us=(int64_t)now*1000000-1000000,
        .energy_state=DASH_DATA_FRESH,.energy_updated_us=1};
    if(!strcmp(argv[1],"alert")) {
        data.region_alerts[12]='R';
        screen_alert_details.count=1;
        screen_alert_details.records[0]=(dashboard_alert_record_t){.oblast=true,.level='R',.started_at=now-120,
            .threat_count=1,.threats={7}};
        update_alert_card(&data);
        assert(strstr(note.text,"Від ") && !strcmp(threat.text,"БпЛА"));
        label_writes=0;update_alert_card(&data);assert(!label_writes);
        data.alert_details_state=DASH_DATA_UNAVAILABLE;
        update_alert_card(&data);assert(!strcmp(threat.text,"Типи загроз недоступні"));
        label_writes=0;update_alert_card(&data);assert(!label_writes);
        data.region_alerts[12]='N';update_alert_card(&data);
        assert(!strcmp(threat.text,"Торкніться карти ›"));
        data.alerts_state=DASH_DATA_STALE;update_alert_card(&data);
        assert(strstr(note.text,"застар."));
        puts("alert refresh: unchanged labels produce zero writes; fallback/stale/clear preserved: PASS");
        return 0;
    }
    strcpy(data.energy.today.date,"2026-11-24");
    strcpy(data.energy.tomorrow.date,"2026-11-25");
    data.energy.today.state=data.energy.tomorrow.state=DASH_ENERGY_PLANNED;
    data.energy.today.slots[25]=DASH_SLOT_OFF;
    data.energy.tomorrow.slots[0]=DASH_SLOT_POSSIBLE;
    update_energy_cards(&data);
    assert(!intervals.count && !detail_track.count); /* Hidden details stay unpopulated. */
    unsigned allocations=created,cleans=cleaned;
    ++local_clock.tm_min;now+=60;update_energy_cards(&data);
    assert(created==allocations && cleaned==cleans); /* Move only the time marker. */
    active=schedule_screen;update_energy_cards(&data);
    assert(intervals.count==1 && strstr(intervals.children[0]->text,"12:30"));
    allocations=created;cleans=cleaned;
    for(unsigned i=0;i<5;++i) { ++local_clock.tm_min;now+=60;update_energy_cards(&data); }
    assert(created==allocations && cleaned==cleans);
    ++data.energy_updated_us;update_energy_cards(&data);
    assert(created==allocations && cleaned==cleans); /* Same schedule, new network revision. */
    active=home_screen;data.energy.today.slots[25]=DASH_SLOT_CLEAR;
    ++data.energy_updated_us;update_energy_cards(&data);
    assert(intervals.count==1); /* Defer hidden details until entering. */
    active=schedule_screen;update_energy_cards(&data);
    assert(intervals.count==1 && strstr(intervals.children[0]->text,"немає"));
    energy_show_tomorrow=true;update_energy_cards(&data);
    assert(strstr(intervals.children[0]->text,"00:00"));
    energy_show_tomorrow=false;local_clock.tm_mday=25;local_clock.tm_hour=0;local_clock.tm_min=0;
    now=mktime(&local_clock);update_energy_cards(&data);
    assert(strstr(intervals.children[0]->text,"00:00"));
    lv_obj_clean(&home_track);lv_obj_clean(&detail_track);lv_obj_clean(&intervals);
    puts("schedule refresh: hidden details deferred; minute/revision updates reuse objects; day changes preserved: PASS");
    return 0;
}

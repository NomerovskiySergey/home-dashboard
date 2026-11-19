#include "dashboard_alert_details.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

void dashboard_alert_copy_text(char *out, size_t capacity, const char *text)
{
    if (!capacity) return;
    if (!text) text = "";
    size_t length = strlen(text);
    if (length >= capacity) {
        length = capacity - 1;
        while (length && ((unsigned char)text[length] & 0xc0) == 0x80) --length;
    }
    memcpy(out, text, length);
    out[length] = 0;
}

static bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

int64_t dashboard_alert_parse_utc(const char *text)
{
    if (!text) return 0;
    const size_t n = strlen(text);
    if (n < 20 || text[4]!='-' || text[7]!='-' || text[10]!='T' || text[13]!=':' || text[16]!=':' || text[n-1]!='Z') return 0;
    if (n != 20) {
        if (n < 22 || text[19]!='.') return 0;
        for (size_t i=20; i<n-1; ++i) if (!isdigit((unsigned char)text[i])) return 0;
    }
    int values[6]={0};
    static const unsigned starts[]={0,5,8,11,14,17}, sizes[]={4,2,2,2,2,2};
    for (unsigned i=0;i<6;++i) for (unsigned j=0;j<sizes[i];++j) {
        const unsigned char c=(unsigned char)text[starts[i]+j];
        if (!isdigit(c)) return 0;
        values[i]=values[i]*10+c-'0';
    }
    const int y=values[0], m=values[1], d=values[2];
    static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if (y<1970 || y>2100 || m<1 || m>12 || d<1 || d>(int)days[m-1]+(m==2 && leap(y)) || values[3]>23 || values[4]>59 || values[5]>59) return 0;
    int64_t total=0;
    for (int year=1970;year<y;++year) total+=365+leap(year);
    for (int month=1;month<m;++month) total+=days[month-1]+(month==2 && leap(y));
    return (total+d-1)*86400+values[3]*3600+values[4]*60+values[5];
}

int64_t dashboard_alert_elapsed(int64_t started, int64_t now)
{ return started>0 && now>=started ? (now-started)/60 : -1; }

bool dashboard_alert_kyiv_clock(int64_t utc, char out[6])
{
    if (utc <= 0 || utc >= INT64_C(4133980800)) return false;
    /* Same last-Sunday EU rule as dashboard.c's EET-2EEST TZ configuration. */
    int year=1970;
    int64_t year_day=0;
    while ((year_day+365+leap(year))*86400<=utc) { year_day+=365+leap(year); ++year; }
    const int64_t march31=year_day+31+28+leap(year)+30;
    const int64_t october31=year_day+273+leap(year)+30;
    const int64_t summer_start=(march31-(march31+4)%7)*86400+3600;
    const int64_t summer_end=(october31-(october31+4)%7)*86400+3600;
    const int64_t local=utc+((utc>=summer_start && utc<summer_end) ? 10800 : 7200);
    const int hour=(int)(local/3600%24),minute=(int)(local/60%60);
    out[0]=(char)('0'+hour/10); out[1]=(char)('0'+hour%10); out[2]=':';
    out[3]=(char)('0'+minute/10); out[4]=(char)('0'+minute%10); out[5]=0;
    return true;
}

static const char *const threat_codes[]={"unknown","tactic_aircraft_activity","strategic_aircraft_activity","mig31k_departure","ballistic_missiles","cruise_missiles","unspecified_missiles","drones","guided_aerial_bombs","air_defense"};
static const char *const threat_names[]={"Інша загроза","Тактична авіація","Стратегічна авіація","Зліт МіГ-31К","Балістична загроза","Крилаті ракети","Ракетна загроза","БпЛА","Керовані авіабомби","Робота ППО"};
const char *dashboard_alert_threat_name(unsigned type)
{ return threat_names[type < sizeof(threat_names)/sizeof(threat_names[0]) ? type : 0]; }

static const char *field(const cJSON *item,const char *key)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(item,key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static void record(const cJSON *item,dashboard_alert_details_t *out)
{
    if (strcmp(field(item,"alert_type"),"air_raid") ||
        (strcmp(field(item,"location_uid"),"9") && strcmp(field(item,"location_oblast_uid"),"9"))) return;
    const cJSON *finished=cJSON_GetObjectItemCaseSensitive(item,"finished_at");
    if (finished && !cJSON_IsNull(finished)) return;
    if (out->count==DASH_ALERT_RECORDS) { ++out->omitted; return; }
    dashboard_alert_record_t *r=&out->records[out->count++];
    const char *location=field(item,"location_title");
    dashboard_alert_copy_text(r->location,sizeof(r->location),*location ? location : "Локацію не вказано");
    r->oblast=!strcmp(field(item,"location_type"),"oblast") && !strcmp(field(item,"location_uid"),"9");
    r->level=!strcmp(field(item,"alert_level"),"red") ? 'R' : !strcmp(field(item,"alert_level"),"yellow") ? 'Y' : 0;
    r->started_at=dashboard_alert_parse_utc(field(item,"started_at"));
    const char *message=field(item,"notes");
    const cJSON *threats=cJSON_GetObjectItemCaseSensitive(item,"threats"),*threat;
    cJSON_ArrayForEach(threat,threats) {
        if (!cJSON_IsObject(threat)) continue;
        if (!*message) message=field(threat,"source_message");
        unsigned type=0;
        for (unsigned i=1;i<sizeof(threat_codes)/sizeof(threat_codes[0]);++i)
            if (!strcmp(field(threat,"threat_type"),threat_codes[i])) type=i;
        if (r->threat_count<DASH_ALERT_THREATS) r->threats[r->threat_count++]=(uint8_t)type;
        else ++r->threats_omitted;
    }
    dashboard_alert_copy_text(r->message,sizeof(r->message),message);
    r->text_truncated=strlen(location)>=sizeof(r->location) || strlen(message)>=sizeof(r->message);
}

static const char *space(const char *p,const char *end)
{ while (p<end && isspace((unsigned char)*p)) ++p; return p; }

/* Locate a JSON value without allocating its siblings. cJSON validates each bounded value. */
static const char *value_end(const char *p,const char *end)
{
    unsigned depth=0; bool string=false,escape=false;
    for (;p<end;++p) {
        if (string) {
            if (!escape && *p=='"') string=false;
            escape=!escape && *p=='\\';
        } else if (*p=='"') string=true;
        else if (*p=='{' || *p=='[') { if (++depth>32) return NULL; }
        else if (*p=='}' || *p==']') { if (!depth) return p; if (!--depth) return p+1; }
        else if (!depth && *p==',') return p;
    }
    return string || depth ? NULL : p;
}

static cJSON *parse_value(const char *p,const char *end)
{
    if (!end || end<=p || end-p>8192) return NULL;
    const char *tail=NULL;
    cJSON *item=cJSON_ParseWithLengthOpts(p,(size_t)(end-p),&tail,false);
    if (!item || space(tail,end)!=end) { cJSON_Delete(item); return NULL; }
    return item;
}

bool dashboard_alert_details_parse(const char *body,size_t length,dashboard_alert_details_t *out)
{
    if (!body || !out || length>=DASH_ALERT_BODY_CAPACITY) return false;
    const char *end=body+length,*p=space(body,end);
    if (p==end || *p++!='{') return false;
    dashboard_alert_details_t *parsed=calloc(1,sizeof(*parsed));
    if (!parsed) return false;
    bool found=false,valid=false;
    for (;;) {
        p=space(p,end);
        if (p==end || *p!='"') break;
        const char *key_start=++p;
        while (p<end && *p!='"' && *p!='\\') ++p;
        if (p==end || *p!='"') break;
        const bool alerts=p-key_start==6 && !memcmp(key_start,"alerts",6);
        p=space(p+1,end);
        if (p==end || *p++!=':') break;
        p=space(p,end);
        if (alerts) {
            if (found || p==end || *p++!='[') break;
            found=true;
            p=space(p,end);
            if (p==end) break;
            if (*p!=']') for (;;) {
                const char *next=value_end(p,end);
                cJSON *item=parse_value(p,next);
                if (!cJSON_IsObject(item)) { cJSON_Delete(item); goto done; }
                record(item,parsed); cJSON_Delete(item);
                p=space(next,end);
                if (p==end) goto done;
                if (*p==']') break;
                if (*p++!=',') goto done;
                p=space(p,end);
            }
            if (p==end || *p++!=']') break;
        } else {
            const char *next=value_end(p,end);
            cJSON *item=parse_value(p,next);
            if (!item) break;
            cJSON_Delete(item);p=next;
        }
        p=space(p,end);
        if (p==end) break;
        if (*p=='}') { valid=found && space(p+1,end)==end; break; }
        if (*p++!=',') break;
    }
done:
    if (valid) *out=*parsed;
    free(parsed);
    return valid;
}

const dashboard_alert_record_t *dashboard_alert_primary(const dashboard_alert_details_t *details,char status)
{
    if ((status!='R' && status!='Y') || details->omitted) return NULL;
    const dashboard_alert_record_t *result=NULL;
    for (unsigned i=0;i<details->count;++i) if (details->records[i].oblast) {
        if (result) return NULL;
        result=&details->records[i];
    }
    return result && (!result->level || result->level==status) ? result : NULL;
}

bool dashboard_alert_cache_update(dashboard_alert_cache_t *cache,int status,bool parsed,const char *modified,int64_t now_s)
{
    if (!((status==200 && parsed) || (status==304 && cache->valid))) return false;
    if (status==200 || (modified && *modified)) dashboard_alert_copy_text(cache->modified,sizeof(cache->modified),modified);
    cache->valid=true;cache->checked_s=now_s;return true;
}

bool dashboard_alert_rate_take(dashboard_alert_rate_t *rate,int64_t now_s)
{
    if (now_s<rate->blocked_until) return false;
    unsigned kept=0;
    for (unsigned i=0;i<rate->count;++i) if (now_s-rate->requests[i]<60) rate->requests[kept++]=rate->requests[i];
    rate->count=kept;
    if (kept==6) return false;
    rate->requests[rate->count++]=now_s;return true;
}

void dashboard_alert_rate_pause(dashboard_alert_rate_t *rate,int64_t now_s,int status,long retry_after)
{
    int64_t delay=status==401 || status==403 ? 300 : status==429 ? (retry_after>0 ? retry_after : 90) : 0;
    if (now_s+delay>rate->blocked_until) rate->blocked_until=now_s+delay;
}

bool dashboard_alert_details_current(char map_status,char details_status,int64_t changed_s,int64_t checked_s)
{ return map_status!='N' && map_status==details_status && checked_s>=changed_s; }

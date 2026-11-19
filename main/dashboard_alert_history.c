#include "dashboard_alert_history.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static const char *field(const cJSON *item, const char *key)
{
    const cJSON *value=cJSON_GetObjectItemCaseSensitive(item,key);
    return cJSON_IsString(value) ? value->valuestring : "";
}

static int compare(const dashboard_alert_record_history_t *a, const dashboard_alert_record_history_t *b)
{
    if (a->started_at!=b->started_at) return a->started_at>b->started_at ? -1 : 1;
    int order=strcmp(a->id,b->id);
    if (!order) order=strcmp(a->location,b->location);
    if (!order && a->finished_at!=b->finished_at) order=a->finished_at>b->finished_at ? -1 : 1;
    if (!order && a->finish!=b->finish) order=a->finish>b->finish ? -1 : 1;
    return order;
}

static void describe_uid(const cJSON *item,const char *key,char out[32])
{
    const cJSON *value=cJSON_GetObjectItemCaseSensitive(item,key);
    if (cJSON_IsNumber(value)) snprintf(out,32,"number:%.5g",value->valuedouble);
    else if (cJSON_IsString(value)) {
        const char *text=value->valuestring;
        bool digits=*text && strlen(text)<=12;
        for (const char *p=text;*p;++p) if (!isdigit((unsigned char)*p)) digits=false;
        snprintf(out,32,"string:%s",digits ? text : "other");
    } else dashboard_alert_copy_text(out,32,"missing/non-scalar");
}

static void history_record(const cJSON *item, void *context)
{
    dashboard_alert_history_t *out=context;
    if (!out->scanned) {
        dashboard_alert_copy_text(out->first_type,sizeof(out->first_type),field(item,"alert_type"));
        describe_uid(item,"location_uid",out->first_uid);
        describe_uid(item,"location_oblast_uid",out->first_oblast_uid);
        dashboard_alert_copy_text(out->first_location,sizeof(out->first_location),field(item,"location_title"));
        dashboard_alert_copy_text(out->first_oblast,sizeof(out->first_oblast),field(item,"location_oblast"));
    }
    ++out->scanned;
    if (strcmp(field(item,"alert_type"),"air_raid")) return;
    ++out->air_raids;
    if (!dashboard_alert_is_local(item)) return;
    dashboard_alert_record_history_t row={0};
    const char *location=field(item,"location_title");
    dashboard_alert_copy_text(row.location,sizeof(row.location),*location ? location : "Локацію не вказано");
    row.location_truncated=strlen(location)>=sizeof(row.location);
    const cJSON *id=cJSON_GetObjectItemCaseSensitive(item,"id");
    if (cJSON_IsString(id) && strlen(id->valuestring)<sizeof(row.id))
        dashboard_alert_copy_text(row.id,sizeof(row.id),id->valuestring);
    else if (cJSON_IsNumber(id) && id->valuedouble>=0 && id->valuedouble<=9007199254740991.0)
        snprintf(row.id,sizeof(row.id),"%.0f",id->valuedouble);
    row.started_at=dashboard_alert_parse_utc(field(item,"started_at"));
    const cJSON *finished=cJSON_GetObjectItemCaseSensitive(item,"finished_at");
    if (cJSON_IsNull(finished)) row.finish=DASH_HISTORY_UNFINISHED;
    else if (cJSON_IsString(finished)) {
        row.finished_at=dashboard_alert_parse_utc(finished->valuestring);
        if (row.finished_at) row.finish=DASH_HISTORY_FINISHED;
    }
    for (unsigned i=0;i<out->count;++i) {
        const dashboard_alert_record_history_t *old=&out->records[i];
        const bool duplicate=*row.id && *old->id ? !strcmp(row.id,old->id) :
            row.started_at==old->started_at && row.finished_at==old->finished_at &&
            row.finish==old->finish && !strcmp(row.location,old->location);
        if (duplicate) {
            if (compare(&row,old)>=0) return;
            memmove(&out->records[i],&out->records[i+1],(out->count-i-1)*sizeof(row));
            --out->count;
            break;
        }
    }
    unsigned position=0;
    while (position<out->count && compare(&row,&out->records[position])>=0) ++position;
    if (position==DASH_HISTORY_RECORDS) return;
    if (out->count<DASH_HISTORY_RECORDS) ++out->count;
    memmove(&out->records[position+1],&out->records[position],(out->count-position-1)*sizeof(row));
    out->records[position]=row;
}

bool dashboard_alert_history_parse(const char *body,size_t length,dashboard_alert_history_t *out)
{
    if (!out) return false;
    dashboard_alert_history_t *parsed=calloc(1,sizeof(*parsed));
    if (!parsed) return false;
    const bool valid=dashboard_alert_visit(body,length,DASH_HISTORY_BODY_CAPACITY,history_record,parsed);
    if (valid) *out=*parsed;
    free(parsed);
    return valid;
}

int64_t dashboard_alert_history_minutes(const dashboard_alert_record_history_t *record,int64_t now)
{
    if (record->finish!=DASH_HISTORY_FINISHED || record->finished_at>now || record->started_at>now)
        return -1;
    return dashboard_alert_elapsed(record->started_at,record->finished_at);
}

void dashboard_alert_history_format(const dashboard_alert_record_history_t *record,int64_t now,
                                    char *text,size_t capacity)
{
    char start[17]="",finish[17]="",duration[64]="";
    if (record->started_at>now || !dashboard_alert_kyiv_datetime(record->started_at,start))
        dashboard_alert_copy_text(start,sizeof(start),"—");
    if (record->finish==DASH_HISTORY_FINISHED && record->finished_at<=now &&
        record->finished_at>=record->started_at)
        dashboard_alert_kyiv_datetime(record->finished_at,finish);
    const int64_t minutes=dashboard_alert_history_minutes(record,now);
    if (minutes>=0) snprintf(duration,sizeof(duration),"Тривалість: %lld хв",(long long)minutes);
    else dashboard_alert_copy_text(duration,sizeof(duration),"Тривалість невідома");
    snprintf(text,capacity,"%s%s\nПочаток: %s\n%s%s\n%s",
             record->location,record->location_truncated ? "…" : "",start,
             record->finish==DASH_HISTORY_UNFINISHED ? "Не завершена за даними історії" : "Відбій: ",
             record->finish==DASH_HISTORY_UNFINISHED ? "" : *finish ? finish : "час невідомий",duration);
}

bool dashboard_history_open(dashboard_history_request_t *request,int64_t now_s)
{
    if (request->pending || request->in_flight) return false;
    if (request->cache.valid && !request->failed && now_s-request->cache.checked_s<DASH_HISTORY_TTL_S)
        return false;
    if (request->attempted && now_s-request->last_attempt_s<30) return false;
    request->pending=true;
    return true;
}

bool dashboard_history_begin(dashboard_history_request_t *request,dashboard_alert_rate_t *shared,
                             int64_t now_s,bool online)
{
    if (!request->pending || request->in_flight || !online) return false;
    unsigned kept=0;
    for (unsigned i=0;i<request->attempt_count;++i)
        if (now_s-request->attempts[i]<60) request->attempts[kept++]=request->attempts[i];
    request->attempt_count=kept;
    if (kept==2 || !dashboard_alert_rate_take(shared,now_s)) return false;
    request->attempts[request->attempt_count++]=now_s;
    request->last_attempt_s=now_s;
    request->attempted=true;
    request->pending=false;
    request->in_flight=true;
    return true;
}

bool dashboard_history_complete(dashboard_history_request_t *request,int status,bool parsed,
                                const char *modified,int64_t now_s)
{
    const bool success=dashboard_alert_cache_update(&request->cache,status,parsed,modified,now_s);
    request->failed=!success;
    request->in_flight=false;
    return success;
}

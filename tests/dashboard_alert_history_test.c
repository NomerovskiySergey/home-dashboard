#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dashboard_alert_history.h"

static void parser(void)
{
    dashboard_alert_history_t out={0};
    const char body[]="{\"alerts\":["
      "{\"id\":2,\"location_uid\":\"9\",\"location_title\":\"Область\",\"alert_type\":\"air_raid\",\"started_at\":\"2026-11-18T23:30:00Z\",\"finished_at\":\"2026-11-19T00:30:00Z\"},"
      "{\"id\":3,\"location_oblast_uid\":\"9\",\"location_uid\":\"999\",\"location_title\":\"Окремий район\",\"alert_type\":\"air_raid\",\"started_at\":\"2026-11-19T02:00:00Z\",\"finished_at\":null},"
      "{\"id\":2,\"location_uid\":\"9\",\"location_title\":\"Область\",\"alert_type\":\"air_raid\",\"started_at\":\"2026-11-18T23:30:00Z\",\"finished_at\":\"2026-11-19T00:30:00Z\"},"
      "{\"location_uid\":\"14\",\"alert_type\":\"air_raid\"},{\"location_uid\":\"9\",\"alert_type\":\"chemical\"}]}";
    assert(dashboard_alert_history_parse(body,strlen(body),&out));
    assert(out.count==2 && !strcmp(out.records[0].location,"Окремий район"));
    assert(out.records[0].finish==DASH_HISTORY_UNFINISHED);
    assert(out.records[1].finish==DASH_HISTORY_FINISHED);
    int64_t now=dashboard_alert_parse_utc("2026-11-19T03:00:00Z");
    assert(dashboard_alert_history_minutes(&out.records[1],now)==60);
    assert(dashboard_alert_history_minutes(&out.records[0],now)==-1);
    char text[512];
    dashboard_alert_history_format(&out.records[0],now,text,sizeof(text));
    assert(strstr(text,"Не завершена за даними історії"));
    dashboard_alert_history_format(&out.records[1],now,text,sizeof(text));
    assert(strstr(text,"19.11.2026 01:30") && strstr(text,"19.11.2026 02:30"));
    dashboard_alert_history_t old=out;
    assert(!dashboard_alert_history_parse(body,strlen(body)-1,&out));
    assert(!memcmp(&old,&out,sizeof(out)));
    assert(!dashboard_alert_history_parse(body,DASH_HISTORY_BODY_CAPACITY,&out));
    assert(!dashboard_alert_history_parse("{\"alerts\":{}}",13,&out));
    assert(dashboard_alert_history_parse("{\"alerts\":[]}",13,&out) && out.count==0);
    const char missing[]="{\"alerts\":[{\"location_uid\":\"9\",\"alert_type\":\"air_raid\",\"started_at\":\"bad\"}]}";
    assert(dashboard_alert_history_parse(missing,strlen(missing),&out));
    assert(out.count==1 && out.records[0].finish==DASH_HISTORY_UNKNOWN);
    dashboard_alert_history_format(&out.records[0],now,text,sizeof(text));
    assert(strstr(text,"час невідомий") && !strstr(text,"Не завершена"));
    const char numeric[]="{\"alerts\":[{\"id\":1,\"location_uid\":9,\"location_title\":\"Область\",\"alert_type\":\"air_raid\"},{\"id\":2,\"location_oblast_uid\":9,\"location_uid\":999,\"location_title\":\"Район\",\"alert_type\":\"air_raid\"}]}";
    if(!dashboard_alert_history_parse(numeric,strlen(numeric),&out) || out.count!=2){
        fprintf(stderr,"FAIL: numeric API UIDs must match the selected oblast\n");exit(1);
    }
    const char named[]="{\"alerts\":[{\"id\":1,\"location_uid\":\"45\",\"location_oblast_uid\":45,\"location_oblast\":\"Дніпропетровська область\",\"location_title\":\"Дніпропетровська область\",\"location_type\":\"oblast\",\"alert_type\":\"air_raid\"},{\"id\":2,\"location_uid\":\"9\",\"location_oblast\":\"Київська область\",\"alert_type\":\"air_raid\"}]}";
    if(!dashboard_alert_history_parse(named,strlen(named),&out) || out.count!=1 || strcmp(out.records[0].id,"1")){
        fprintf(stderr,"FAIL: explicit oblast name must identify region independently of UID representation\n");exit(1);
    }
}

static void sorting(void)
{
    char body[10000]="{\"alerts\":[";
    for(int i=0;i<15;++i){
        char row[320];
        const int day=(i*7)%15+1;
        snprintf(row,sizeof(row),"%s{\"id\":%d,\"location_uid\":\"9\",\"alert_type\":\"air_raid\",\"started_at\":\"2026-11-%02dT00:00:00Z\"}",i?",":"",day,day);
        strcat(body,row);
    }
    strcat(body,"]}");
    dashboard_alert_history_t out;
    assert(dashboard_alert_history_parse(body,strlen(body),&out) && out.count==10);
    assert(out.records[0].started_at==dashboard_alert_parse_utc("2026-11-15T00:00:00Z"));
    assert(out.records[9].started_at==dashboard_alert_parse_utc("2026-11-06T00:00:00Z"));
    for(unsigned i=1;i<out.count;++i)assert(out.records[i-1].started_at>=out.records[i].started_at);
    dashboard_alert_history_t saved=out;
    body[strlen(body)-1]=0;
    assert(!dashboard_alert_history_parse(body,strlen(body),&out) && !memcmp(&saved,&out,sizeof(out)));
    dashboard_alert_record_history_t row={0};
    row.started_at=1000;row.finished_at=900;row.finish=DASH_HISTORY_FINISHED;
    assert(dashboard_alert_history_minutes(&row,2000)==-1);
    row.finished_at=3000;assert(dashboard_alert_history_minutes(&row,2000)==-1);
    char date[17];
    assert(dashboard_alert_kyiv_datetime(dashboard_alert_parse_utc("2026-12-31T23:30:00Z"),date) && !strcmp(date,"01.01.2027 01:30"));
    assert(dashboard_alert_kyiv_datetime(dashboard_alert_parse_utc("2026-03-29T01:00:00Z"),date) && !strcmp(date,"29.03.2026 04:00"));
    assert(dashboard_alert_kyiv_datetime(dashboard_alert_parse_utc("2026-10-25T01:00:00Z"),date) && !strcmp(date,"25.10.2026 03:00"));
    const char ties[]="{\"alerts\":[{\"id\":\"b\",\"location_uid\":\"9\",\"alert_type\":\"air_raid\",\"started_at\":\"2026-11-01T00:00:00Z\"},{\"id\":\"a\",\"location_uid\":\"9\",\"alert_type\":\"air_raid\",\"started_at\":\"2026-11-01T00:00:00Z\"}]}";
    assert(dashboard_alert_history_parse(ties,strlen(ties),&out));
    assert(out.count==2 && !strcmp(out.records[0].id,"a") && !strcmp(out.records[1].id,"b"));
    const char duplicates[]="{\"alerts\":[{\"id\":1,\"location_uid\":\"9\",\"alert_type\":\"air_raid\",\"started_at\":\"2026-11-01T00:00:00Z\"},{\"id\":1,\"location_uid\":\"9\",\"alert_type\":\"air_raid\",\"started_at\":\"2026-11-02T00:00:00Z\"}]}";
    assert(dashboard_alert_history_parse(duplicates,strlen(duplicates),&out));
    assert(out.count==1 && out.records[0].started_at==dashboard_alert_parse_utc("2026-11-02T00:00:00Z"));
}

static void requests(void)
{
    dashboard_history_request_t p={0};dashboard_alert_rate_t shared={0};
    assert(dashboard_history_open(&p,100));
    assert(!dashboard_history_open(&p,100));
    assert(!dashboard_history_begin(&p,&shared,100,false));
    assert(dashboard_history_begin(&p,&shared,100,true));
    assert(!dashboard_history_open(&p,101));
    assert(dashboard_history_complete(&p,200,true,"modified",102));
    assert(!dashboard_history_open(&p,1001));
    assert(dashboard_history_open(&p,1002));
    assert(dashboard_history_begin(&p,&shared,1002,true));
    assert(dashboard_history_complete(&p,304,false,"",1003));
    assert(!dashboard_history_open(&p,1004));
    dashboard_history_request_t bad={0};
    assert(dashboard_history_open(&bad,2000));assert(dashboard_history_begin(&bad,&shared,2000,true));
    assert(!dashboard_history_complete(&bad,304,false,"",2001));
    assert(!dashboard_history_open(&bad,2029));
    assert(dashboard_history_open(&bad,2030));assert(dashboard_history_begin(&bad,&shared,2030,true));
    assert(!dashboard_history_complete(&bad,429,false,"",2031));
    dashboard_alert_rate_pause(&shared,2031,429,120);
    assert(dashboard_history_open(&bad,2060));
    assert(!dashboard_history_begin(&bad,&shared,2150,true));
    assert(dashboard_history_begin(&bad,&shared,2151,true));
    assert(!dashboard_history_complete(&bad,500,false,"",2152));
    assert(!dashboard_history_begin(&bad,&shared,2200,true)); /* no automatic retry */
    dashboard_history_request_t capped={0};dashboard_alert_rate_t budget={0};
    assert(dashboard_history_open(&capped,100));assert(dashboard_history_begin(&capped,&budget,100,true));
    assert(!dashboard_history_complete(&capped,500,false,"",100));
    assert(dashboard_history_open(&capped,130));assert(dashboard_history_begin(&capped,&budget,130,true));
    assert(!dashboard_history_complete(&capped,500,false,"",130));
    capped.pending=true;
    assert(!dashboard_history_begin(&capped,&budget,159,true));
    assert(dashboard_history_begin(&capped,&budget,160,true));
    dashboard_history_request_t queued={0};dashboard_alert_rate_t full={0};
    for(int i=0;i<6;++i)assert(dashboard_alert_rate_take(&full,100+i));
    assert(dashboard_history_open(&queued,106));
    assert(!dashboard_history_begin(&queued,&full,159,true));
    assert(dashboard_history_begin(&queued,&full,160,true));
}

static bool large_history(void)
{
    const size_t capacity=2*1024*1024;
    char *body=malloc(capacity);
    if(!body)return false;
    size_t used=(size_t)snprintf(body,capacity,"{\"alerts\":[");
    for(unsigned i=0;i<9000;++i){
        const int written=snprintf(body+used,capacity-used,
          "%s{\"id\":%u,\"location_uid\":\"9\",\"location_title\":\"Область\",\"alert_type\":\"air_raid\",\"started_at\":\"2026-11-18T23:30:00Z\",\"finished_at\":\"2026-11-19T00:30:00Z\",\"notes\":\"large history regression fixture\"}",i?",":"",i);
        if(written<0 || (size_t)written>=capacity-used){free(body);return false;}
        used+=(size_t)written;
    }
    memcpy(body+used,"]}",3);used+=2;
    dashboard_alert_history_t out={0};
    const bool valid=used>1735836 && dashboard_alert_history_parse(body,used,&out) && out.count==10;
    if(!valid)fprintf(stderr,"FAIL: valid history of %zu bytes must retain ten records\n",used);
    free(body);return valid;
}

int main(void){parser();sorting();requests();if(!large_history())return 1;puts("Alert history tests passed");return 0;}

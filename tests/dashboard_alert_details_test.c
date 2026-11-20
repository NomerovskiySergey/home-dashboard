#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dashboard_alert_details.h"

static void parse_cases(void)
{
    dashboard_alert_details_t out = {0};
    const char json[] = "{\"alerts\":["
        "{\"location_uid\":\"14\",\"location_type\":\"oblast\",\"alert_type\":\"air_raid\"},"
        "{\"location_uid\":\"9\",\"location_type\":\"oblast\",\"location_title\":\"Дніпропетровська область\","
        "\"alert_type\":\"air_raid\",\"started_at\":\"2024-01-01T00:00:00.000Z\",\"alert_level\":\"red\","
        "\"threats\":[{\"threat_type\":\"drones\",\"source_message\":\"Загроза БпЛА\"},{\"threat_type\":\"future_type\"}]},"
        "{\"location_uid\":\"999\",\"location_oblast_uid\":\"9\",\"location_type\":\"raion\","
        "\"location_title\":\"Окремий район\",\"alert_type\":\"air_raid\"}]}";
    assert(dashboard_alert_details_parse(json, strlen(json), &out));
    assert(out.count == 2 && out.records[0].oblast && !out.records[1].oblast);
    assert(out.records[0].started_at == INT64_C(1704067200));
    assert(out.records[0].threat_count == 2);
    assert(strcmp(dashboard_alert_threat_name(out.records[0].threats[0]), "БпЛА") == 0);
    assert(strcmp(dashboard_alert_threat_name(out.records[0].threats[1]), "Інша загроза") == 0);
    assert(strstr(out.records[0].message, "Загроза"));
    const dashboard_alert_record_t *primary = dashboard_alert_primary(&out, 'R');
    assert(primary == &out.records[0]);
    assert(!dashboard_alert_primary(&out, 'P') && !dashboard_alert_primary(&out, 'N'));
    dashboard_alert_details_t previous = out;
    assert(!dashboard_alert_details_parse(json, strlen(json)-1, &out));
    assert(memcmp(&previous, &out, sizeof(out)) == 0);
    assert(!dashboard_alert_details_parse("{\"message\":\"error\"}", 19, &out));
    assert(!dashboard_alert_details_parse("{\"alerts\":[],\"bad\":}", strlen("{\"alerts\":[],\"bad\":}"), &out));
    assert(dashboard_alert_details_parse("{\"alerts\":[]}", 13, &out) && out.count == 0);
    assert(!dashboard_alert_details_parse("{\"alerts\":[1]}", 14, &out));
    const char numeric[]="{\"alerts\":[{\"location_uid\":9,\"location_type\":\"oblast\",\"alert_type\":\"air_raid\"}]}";
    assert(dashboard_alert_details_parse(numeric,strlen(numeric),&out));
    assert(out.count==1 && out.records[0].oblast);
}

static void bounds_and_time(void)
{
    char text[5];
    dashboard_alert_copy_text(text, sizeof(text), "АБВ");
    assert(strcmp(text, "АБ") == 0);
    dashboard_alert_copy_text(text, 4, "АБВ");
    assert(strcmp(text, "А") == 0);
    assert(!dashboard_alert_parse_utc("2024-02-30T00:00:00Z"));
    assert(!dashboard_alert_parse_utc("2024-01-01T24:00:00Z"));
    assert(!dashboard_alert_parse_utc("2024-01-01T00:00:00+02:00"));
    assert(dashboard_alert_parse_utc("2024-02-29T23:59:59Z") == INT64_C(1709251199));
    assert(dashboard_alert_elapsed(1704067200, 1704070020) == 47);
    assert(dashboard_alert_elapsed(1704067200, 1704067100) == -1);
    assert(dashboard_alert_elapsed(0, 1704067100) == -1);
    char clock[6];
    assert(dashboard_alert_kyiv_clock(dashboard_alert_parse_utc("2026-01-01T23:30:00Z"), clock) && !strcmp(clock,"01:30"));
    assert(dashboard_alert_kyiv_clock(dashboard_alert_parse_utc("2026-07-01T23:30:00Z"), clock) && !strcmp(clock,"02:30"));
    assert(dashboard_alert_kyiv_clock(dashboard_alert_parse_utc("2026-03-29T00:59:00Z"), clock) && !strcmp(clock,"02:59"));
    assert(dashboard_alert_kyiv_clock(dashboard_alert_parse_utc("2026-03-29T01:00:00Z"), clock) && !strcmp(clock,"04:00"));
    assert(dashboard_alert_kyiv_clock(dashboard_alert_parse_utc("2026-10-25T01:00:00Z"), clock) && !strcmp(clock,"03:00"));
    assert(!dashboard_alert_kyiv_clock(0,clock));
    char json[12000] = "{\"alerts\":[";
    for (int i=0;i<10;++i) strcat(json, i ? ",{\"location_uid\":\"9\",\"location_type\":\"oblast\",\"alert_type\":\"air_raid\"}" : "{\"location_uid\":\"9\",\"location_type\":\"oblast\",\"alert_type\":\"air_raid\"}");
    strcat(json, "]}");
    dashboard_alert_details_t out;
    assert(dashboard_alert_details_parse(json, strlen(json), &out));
    assert(out.count == DASH_ALERT_RECORDS && out.omitted == 2);
    assert(!dashboard_alert_primary(&out, 'R'));
    const char many_threats[] = "{\"alerts\":[{\"location_uid\":\"9\",\"location_type\":\"oblast\",\"alert_type\":\"air_raid\",\"threats\":[{},{},{},{},{},{}]}]}";
    assert(dashboard_alert_details_parse(many_threats, strlen(many_threats), &out));
    assert(out.records[0].threat_count==4 && out.records[0].threats_omitted==2);
    assert(!dashboard_alert_details_parse(many_threats,DASH_ALERT_BODY_CAPACITY,&out));
    const char metadata[]="{\"last_updated\":\"2026-11-19T10:00:00Z\",\"alerts\":[],\"meta\":{\"escaped\":\"a \\\" } \\\" b\"}}";
    assert(dashboard_alert_details_parse(metadata,strlen(metadata),&out));
    const char finished[]="{\"alerts\":[{\"location_uid\":\"9\",\"alert_type\":\"air_raid\",\"finished_at\":\"2026-11-19T10:00:00Z\"}]}";
    assert(dashboard_alert_details_parse(finished,strlen(finished),&out) && out.count==0);
}

static void cache_and_rate(void)
{
    dashboard_alert_cache_t map={0}, details={0};
    assert(!dashboard_alert_cache_update(&map,304,false,"",100));
    assert(dashboard_alert_cache_update(&map,200,true,"Thu, 19 Nov 2026 10:00:00 GMT",100));
    assert(!dashboard_alert_cache_update(&map,200,false,"bad",120));
    assert(map.checked_s==100 && strstr(map.modified,"Thu"));
    assert(dashboard_alert_cache_update(&map,304,false,"",125) && map.checked_s==125);
    assert(!details.valid && !details.modified[0]);
    dashboard_alert_rate_t rate={0};
    for(int i=0;i<6;++i) assert(dashboard_alert_rate_take(&rate,100+i));
    assert(!dashboard_alert_rate_take(&rate,159));
    assert(dashboard_alert_rate_take(&rate,160));
    dashboard_alert_rate_pause(&rate,160,429,120);
    assert(!dashboard_alert_rate_take(&rate,279));
    assert(dashboard_alert_rate_take(&rate,280));
    dashboard_alert_rate_pause(&rate,280,401,0);
    assert(!dashboard_alert_rate_take(&rate,579));
    assert(dashboard_alert_rate_take(&rate,580));
    dashboard_alert_rate_pause(&rate,580,429,90000);
    assert(!dashboard_alert_rate_take(&rate,90579));
    assert(dashboard_alert_rate_take(&rate,90580));
    assert(dashboard_alert_details_current('R','R',100,100));
    assert(!dashboard_alert_details_current('R','Y',100,100));
    assert(!dashboard_alert_details_current('R','R',101,100));
    assert(!dashboard_alert_details_current('N','N',100,100));
    puts("Alert details, UTC time, cache and shared rate tests passed");
}

static void nationwide_threats(void)
{
    dashboard_alert_details_t out = {0};
    const char json[] = "{\"alerts\":["
        "{\"location_uid\":\"22\",\"location_type\":\"oblast\",\"alert_type\":\"air_raid\","
        "\"threats\":[{\"threat_type\":\"drones\"},{\"threat_type\":\"cruise_missiles\"}]},"
        "{\"location_uid\":124,\"location_oblast_uid\":22,\"location_type\":\"city\",\"alert_type\":\"air_raid\","
        "\"threats\":[{\"threat_type\":\"drones\"},{\"threat_type\":\"future\"}]},"
        "{\"location_uid\":\"9\",\"location_type\":\"oblast\",\"alert_type\":\"air_raid\","
        "\"finished_at\":\"2026-11-20T00:00:00Z\",\"threats\":[{\"threat_type\":\"drones\"}]}]}";
    assert(dashboard_alert_details_parse(json, strlen(json), &out));
    assert(out.region_threats[63] == ((1u << 7) | (1u << 5) | 1u));
    assert(out.region_threats[12] == 0);
    assert(out.count == 0);
    const char city[] = "{\"alerts\":[{\"location_uid\":31,\"location_type\":\"city\","
        "\"alert_type\":\"air_raid\",\"threats\":[{\"threat_type\":\"drones\"}]}]}";
    assert(dashboard_alert_details_parse(city, strlen(city), &out));
    assert(out.region_threats[30] == (1u << 7) && out.region_threats[32] == 0);
    const char named[] = "{\"alerts\":[{\"location_uid\":9,\"location_type\":\"oblast\","
        "\"location_oblast\":\"Харківська область\",\"alert_type\":\"air_raid\","
        "\"threats\":[{\"threat_type\":\"drones\"}]}]}";
    assert(dashboard_alert_details_parse(named, strlen(named), &out));
    assert(out.region_threats[63] == (1u << 7) && out.region_threats[12] == 0);
    const char kyiv[] = "{\"alerts\":[{\"location_uid\":999,\"location_type\":\"city\","
        "\"location_title\":\"м. Київ\",\"location_oblast\":\"Київська область\",\"alert_type\":\"air_raid\","
        "\"threats\":[{\"threat_type\":\"drones\"}]}]}";
    assert(dashboard_alert_details_parse(kyiv, strlen(kyiv), &out));
    assert(out.region_threats[30] == (1u << 7) && out.region_threats[32] == 0);
    const char empty[] = "{\"alerts\":[]}";
    assert(dashboard_alert_details_parse(empty, strlen(empty), &out));
    assert(out.region_threats[63] == 0);
}

int main(void) { parse_cases(); bounds_and_time(); cache_and_rate(); nationwide_threats(); return 0; }

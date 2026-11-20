#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DASH_ALERT_RECORDS = 8, DASH_ALERT_THREATS = 4, DASH_ALERT_BODY_CAPACITY = 65536 };
typedef struct {
    char location[96];
    char message[256];
    int64_t started_at;
    uint8_t threats[DASH_ALERT_THREATS];
    uint8_t threat_count;
    unsigned threats_omitted;
    char level;
    bool oblast;
    bool text_truncated;
} dashboard_alert_record_t;
typedef struct {
    dashboard_alert_record_t records[DASH_ALERT_RECORDS];
    unsigned count;
    unsigned omitted;
    /* ISO numeric suffix -> unique threat types for that territory, never target positions. */
    uint16_t region_threats[78];
} dashboard_alert_details_t;
typedef struct {
    char modified[80];
    int64_t checked_s;
    bool valid;
} dashboard_alert_cache_t;
typedef struct {
    int64_t requests[6];
    unsigned count;
    int64_t blocked_until;
} dashboard_alert_rate_t;

bool dashboard_alert_details_parse(const char *body, size_t length, dashboard_alert_details_t *out);
void dashboard_alert_copy_text(char *out, size_t capacity, const char *text);
int64_t dashboard_alert_parse_utc(const char *text);
int64_t dashboard_alert_elapsed(int64_t started, int64_t now);
bool dashboard_alert_kyiv_clock(int64_t utc, char out[6]);
bool dashboard_alert_kyiv_datetime(int64_t utc, char out[17]);
struct cJSON;
typedef void (*dashboard_alert_visitor_t)(const struct cJSON *item, void *context);
bool dashboard_alert_uid_is(const struct cJSON *item, const char *key, unsigned uid);
bool dashboard_alert_is_local(const struct cJSON *item);
bool dashboard_alert_visit(const char *body, size_t length, size_t capacity,
                           dashboard_alert_visitor_t visitor, void *context);
const char *dashboard_alert_threat_name(unsigned type);
const dashboard_alert_record_t *dashboard_alert_primary(const dashboard_alert_details_t *details, char status);
bool dashboard_alert_cache_update(dashboard_alert_cache_t *cache, int status, bool parsed,
                                  const char *modified, int64_t now_s);
bool dashboard_alert_rate_take(dashboard_alert_rate_t *rate, int64_t now_s);
void dashboard_alert_rate_pause(dashboard_alert_rate_t *rate, int64_t now_s, int status, long retry_after);
bool dashboard_alert_details_current(char map_status, char details_status,
                                     int64_t changed_s, int64_t checked_s);

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DASH_RECOVERY_STALL_US INT64_C(45000000)
#define DASH_RECOVERY_GRACE_US INT64_C(15000000)
#define DASH_RECOVERY_COOLDOWN_US INT64_C(5000000)

typedef enum {
    DASH_RECOVERY_RUNNING,
    DASH_RECOVERY_REQUESTED,
    DASH_RECOVERY_RESTARTING,
    DASH_RECOVERY_FAILED,
} dashboard_recovery_state_t;

/* Pure lifecycle; the caller serializes access. Network success is not liveness. */
typedef struct {
    dashboard_recovery_state_t state;
    bool pending;
    bool automatic;
    unsigned requests;
    unsigned restarts;
    int64_t heartbeat_us;
    int64_t requested_us;
    int64_t restarted_us;
} dashboard_recovery_t;

void dashboard_recovery_init(dashboard_recovery_t *recovery, int64_t now);
bool dashboard_recovery_request(dashboard_recovery_t *recovery, int64_t now, bool automatic);
void dashboard_recovery_heartbeat(dashboard_recovery_t *recovery, int64_t now);
void dashboard_recovery_poll(dashboard_recovery_t *recovery, int64_t now);
void dashboard_recovery_begin(dashboard_recovery_t *recovery, int64_t now);
void dashboard_recovery_finish(dashboard_recovery_t *recovery, int64_t now);

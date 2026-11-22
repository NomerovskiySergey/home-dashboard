#include "dashboard_recovery.h"

void dashboard_recovery_init(dashboard_recovery_t *recovery, int64_t now)
{
    *recovery = (dashboard_recovery_t){ .heartbeat_us = now };
}

bool dashboard_recovery_request(dashboard_recovery_t *recovery, int64_t now, bool automatic)
{
    if (recovery->pending ||
        (recovery->restarts && now - recovery->restarted_us < DASH_RECOVERY_COOLDOWN_US)) return false;
    recovery->pending = true;
    recovery->automatic = automatic;
    recovery->state = DASH_RECOVERY_REQUESTED;
    recovery->requested_us = now;
    ++recovery->requests;
    return true;
}

void dashboard_recovery_heartbeat(dashboard_recovery_t *recovery, int64_t now)
{
    recovery->heartbeat_us = now;
}

void dashboard_recovery_poll(dashboard_recovery_t *recovery, int64_t now)
{
    if (recovery->pending) {
        if (now - recovery->requested_us >= DASH_RECOVERY_GRACE_US)
            recovery->state = DASH_RECOVERY_FAILED;
    } else if (now - recovery->heartbeat_us >= DASH_RECOVERY_STALL_US) {
        dashboard_recovery_request(recovery, now, true);
    }
}

void dashboard_recovery_begin(dashboard_recovery_t *recovery, int64_t now)
{
    recovery->state = DASH_RECOVERY_RESTARTING;
    recovery->heartbeat_us = now;
}

void dashboard_recovery_finish(dashboard_recovery_t *recovery, int64_t now)
{
    recovery->pending = false;
    recovery->state = DASH_RECOVERY_RUNNING;
    recovery->heartbeat_us = recovery->restarted_us = now;
    ++recovery->restarts;
}

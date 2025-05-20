#ifndef TTCAN_SCHEDULER_H
#define TTCAN_SCHEDULER_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "private/ttcan_scheduler_types.h"

    ttcan_scheduler_status_t ttcan_scheduler_init(ttcan_scheduler_ctx_t *ctx);

    ttcan_scheduler_status_t ttcan_scheduler_start(ttcan_scheduler_ctx_t *ctx);

    ttcan_scheduler_status_t ttcan_scheduler_stop(ttcan_scheduler_ctx_t *ctx);
    ttcan_scheduler_status_t ttcan_scheduler_deinit(ttcan_scheduler_ctx_t *ctx);

    ttcan_data_timeslot_t ttcan_scheduler_timer_cb_get_action(ttcan_scheduler_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
#endif /* TTCAN_SCHEDULER_H */
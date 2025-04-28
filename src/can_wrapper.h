#ifndef CAN_WRAPPER_H
#define CAN_WRAPPER_H

#include <zephyr/drivers/gpio.h>
#include "ttcan_scheduler.h"
#ifdef __cplusplus
extern "C"
{
#endif

    extern ttcan_scheduler_ctx_t ctx;
    extern ttcan_schedule_t ttcan_schedule;

    void ttcan_timer_trigger(struct k_timer *timer_id);

    void ttcan_timer_stop(struct k_timer *timer_id);

    int init_can();
#ifdef __cplusplus
}
#endif

#endif

#include "ttcan_scheduler.h"

static uint8_t ref_slot_data;

ttcan_scheduler_status_t
ttcan_scheduler_init (ttcan_scheduler_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return TTCAN_SCHEDULER_STATUS_ERR_NULL_PARAM;
    }

    ctx->curr_timeslot  = 0;
    ctx->curr_sched_idx = 0;
    ctx->curr_window    = 0;
    return TTCAN_SCHEDULER_STATUS_OK;
}

ttcan_scheduler_status_t
ttcan_scheduler_start (ttcan_scheduler_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return TTCAN_SCHEDULER_STATUS_ERR_NULL_PARAM;
    }

    ctx->curr_timeslot  = 0;
    ctx->curr_sched_idx = 0;
    ctx->curr_window    = 0;
    return TTCAN_SCHEDULER_STATUS_OK;
}

ttcan_scheduler_status_t
ttcan_scheduler_stop (ttcan_scheduler_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return TTCAN_SCHEDULER_STATUS_ERR_NULL_PARAM;
    }
    return TTCAN_SCHEDULER_STATUS_OK;
}

ttcan_scheduler_status_t
ttcan_scheduler_deinit (ttcan_scheduler_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return TTCAN_SCHEDULER_STATUS_ERR_NULL_PARAM;
    }
    return TTCAN_SCHEDULER_STATUS_OK;
}

static void
ttcan_prepare_ref_msg (ttcan_scheduler_ctx_t *ctx, ttcan_data_timeslot_t *data)
{
    if (ctx->master_mode_en)
    {
        ref_slot_data      = ctx->curr_timeslot & 0x7F;
        data->message_type = TTCAN_MSG_REF_TRANSMIT;
    }
    else
    {
        ref_slot_data      = 0;
        data->message_type = TTCAN_MSG_REF_RECEIVE;
    }
    data->node_id      = 0;
    data->data_ptr     = &ref_slot_data;
    data->num_of_bytes = sizeof(ref_slot_data);
    data->window_num   = ctx->curr_timeslot;
}

static void
ttcan_process_ref_msg (ttcan_scheduler_ctx_t *ctx)
{
    if (ctx->curr_timeslot != (ref_slot_data & 0x7f))
    {
        /* Do something with timer */
    }
}

static void
ttcan_process_next_timeslot (ttcan_scheduler_ctx_t *ctx, ttcan_data_timeslot_t *data)
{
    *data = ctx->schedule->messages[ctx->curr_sched_idx];
}

ttcan_data_timeslot_t
ttcan_scheduler_timer_cb_get_action (ttcan_scheduler_ctx_t *ctx)
{
    ttcan_data_timeslot_t timer_action = { 0 };
    uint8_t               is_ref_slot;
    uint8_t               was_ref_slot;
    uint8_t               tm_idx;
    uint16_t              next_tm_window;

    /* More thorough validation to avoid null pointer issues */
    if (ctx == NULL || ctx->schedule == NULL || ctx->schedule->messages == NULL || 
        ctx->schedule->num_of_messages == 0)
    {
        return timer_action;
    }

    is_ref_slot  = (ctx->curr_timeslot == 0U
                   || ((ctx->curr_timeslot % (1000 / ctx->schedule->ref_tick_frequency))) == 0U)
                       ? 1U
                       : 0U;
    was_ref_slot = (ctx->curr_timeslot != 0U)
                       ? ((ctx->curr_timeslot % (1000 / ctx->schedule->ref_tick_frequency)) == 0U)
                       : 0U;
    if (was_ref_slot && (ctx->master_mode_en == 0U))
    {
        ttcan_process_ref_msg(ctx);
    }
    if (is_ref_slot)
    {
        ttcan_prepare_ref_msg(ctx, &timer_action);
    }
    else
    {
        /* Additional bounds checking */
        if (ctx->curr_sched_idx >= ctx->schedule->num_of_messages) {
            ctx->curr_sched_idx = 0;
        }
        
        tm_idx = (uint8_t)ctx->curr_sched_idx;
        next_tm_window = ctx->schedule->messages[tm_idx].window_num;

        if (next_tm_window == ctx->curr_window)
        {
            ttcan_process_next_timeslot(ctx, &timer_action);
            ctx->curr_sched_idx++;
        }
        else if (ctx->curr_timeslot > next_tm_window)
        {
            ctx->curr_sched_idx++;
        }
        ctx->curr_window++;
    }
    
    /* Add additional bounds check before accessing array elements */
    if (ctx->schedule->num_of_messages > 0) {
        const uint16_t last_window_idx = ctx->schedule->num_of_messages - 1;
        if(ctx->curr_timeslot < ctx->schedule->messages[last_window_idx].window_num+5) {
            ctx->curr_timeslot++;
        } else {
            ctx->curr_timeslot = 0;
            ctx->curr_window = 0;
            ctx->curr_sched_idx = 0;
        }
    } else {
        /* No messages in schedule, just increment counter */
        ctx->curr_timeslot++;
        if (ctx->curr_timeslot >= 1000) {  /* arbitrary reset value */
            ctx->curr_timeslot = 0;
        }
    }

    return timer_action;
}

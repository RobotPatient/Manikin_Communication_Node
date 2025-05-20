#ifndef TTCAN_SCHEDULER_TYPES_H
#define TTCAN_SCHEDULER_TYPES_H
#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>
#include <stddef.h>
#include <ttcan_software_conf.h>

    typedef enum
    {
        TTCAN_SCHEDULER_STATUS_OK,
        TTCAN_SCHEDULER_STATUS_ERR_NULL_PARAM,
    } ttcan_scheduler_status_t;

    typedef enum
    {
        TTCAM_MSG_WAIT,
        TTCAN_MSG_REF_TRANSMIT,
        TTCAN_MSG_REF_RECEIVE,
        TTCAN_MSG_READ,
        TTCAN_MSG_WRITE
    } ttcan_message_type_t;

    typedef struct
    {
        uint8_t              node_id;
        uint16_t             window_num;
        ttcan_message_type_t message_type;
        uint8_t             *data_ptr;
        uint8_t              num_of_bytes;
    } ttcan_data_timeslot_t;

    typedef struct
    {
        uint8_t                node_id;
        ttcan_data_timeslot_t *messages;
        uint16_t               num_of_messages;
        uint16_t               tick_frequency;
        uint16_t               ref_tick_frequency;
        uint16_t               tick_window_size;
        uint8_t                free_tick_window_size;
    } ttcan_schedule_t;

    typedef struct
    {
        ttcan_timer_type_t timer;
        ttcan_schedule_t  *schedule;
        uint8_t            master_mode_en;
        uint16_t           curr_timeslot;
        uint16_t           curr_window;
        uint16_t           curr_sched_idx;
    } ttcan_scheduler_ctx_t;

#ifdef __cplusplus
}
#endif
#endif /* TTCAN_SCHEDULER_TYPES_H */
#ifndef CAN_TRANSPORT_H
#define CAN_TRANSPORT_H
#include "can_rx_types.h"
#include <zephyr/sys/ring_buffer.h>

extern struct ring_buf bhi_ring;
extern struct ring_buf vl_ring;
extern struct ring_buf sdp_ring;
extern struct ring_buf ads_ring;

int can_transport_init();

void can_transmit_start_msg();
void can_transmit_stop_msg();

#endif /* CAN_TRANSPORT_H */
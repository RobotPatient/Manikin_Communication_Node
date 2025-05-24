#ifndef CAN_ADDR_DECL_H
#define CAN_ADDR_DECL_H
#include <zephyr/canbus/isotp.h>
#include <zephyr/drivers/can.h>

const struct isotp_fc_opts fc_opts_sensorhub1_sensor1 = {.bs = 0, .stmin = 0};
const struct isotp_fc_opts fc_opts_sensorhub1_sensor2 = {.bs = 0, .stmin = 0};
const struct isotp_fc_opts fc_opts_sensorhub1_sensor3 = {.bs = 0, .stmin = 0};
const struct isotp_fc_opts fc_opts_sensorhub1_cmd = {.bs = 0, .stmin = 0};
const struct isotp_fc_opts fc_opts_sensorhub2_sensor1 = {.bs = 0, .stmin = 0};
const struct isotp_fc_opts fc_opts_sensorhub2_cmd = {.bs = 0, .stmin = 0};
#define BROADCAST_CAN_ID 0x000

const struct isotp_msg_id rx_sensorhub2_sensor1 = {
    .std_id = 0x160,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id tx_sensorhub2_sensor1 = {
    .std_id = 0x60,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .dl = 64,
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};

const struct isotp_msg_id rx_sensorhub2_cmd = {
    .std_id = 0x121,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};

const struct isotp_msg_id tx_sensorhub2_cmd = {
    .std_id = 0x120,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .dl = 64,
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};

const struct isotp_msg_id rx_sensorhub1_sensor1 = {
    .std_id = 0x180,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id tx_sensorhub1_sensor1 = {
    .std_id = 0x080,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .dl = 64,
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id rx_sensorhub1_sensor2 = {
    .std_id = 0x101,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id tx_sensorhub1_sensor2 = {
    .std_id = 0x01,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .dl = 64,
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id rx_sensorhub1_sensor3 = {
    .std_id = 0x150,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id tx_sensorhub1_sensor3 = {
    .std_id = 0x50,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .dl = 64,
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id rx_sensorhub1_cmd = {
    .std_id = 0x010,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};

const struct isotp_msg_id tx_sensorhub1_cmd = {
    .std_id = 0x201,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
    .dl = 64,
    .flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};

#endif /* CAN_ADDR_DECL_H */
#ifndef CAN_RX_TYPES_H
#define CAN_RX_TYPES_H
#include <stdint.h>

typedef struct __attribute__((__packed__))
{
    char sensor_name[8];
    uint32_t frame_id;
    struct
    {
        uint8_t distance_mm;
    } data;
} sample_sensor1_t;

typedef struct __attribute__((__packed__))
{
    char sensor_name[8];
    uint32_t frame_id;
    struct
    {
        uint16_t ch1_mv;
        uint16_t ch2_mv;
        uint16_t ch3_mv;
        uint16_t ch4_mv;
        uint16_t ch5_mv;
        uint16_t ch6_mv;
        uint16_t ch7_mv;
        uint16_t ch8_mv;
    } data;
} sample_sensor2_t;


typedef struct __attribute__((__packed__))
{
    char sensor_name[8];
    uint32_t frame_id;
    struct
    {
        float pressure;
        float temp;
    } data;
} sample_sensor3_t;

typedef struct __attribute__((__packed__))
{
    char sensor_name[8];
    uint32_t frame_id;
    struct
    {
        float pitch_deg;
        float roll_deg;
        float yaw_deg;
    } data;
} sample_sensor4_t;

typedef struct
{
    // Byte 0
    uint8_t id : 4;
    uint8_t startup_ok : 1;
    uint8_t flash_ok : 1;
    uint8_t state : 2;

    // Byte 1
    uint8_t sensor1_sr : 7;
    uint8_t sensor1_health : 2;

    // Byte 2
    uint8_t sensor1_faultcnt : 3;
    uint8_t sensor2_sr : 7;

    // Byte 3
    uint8_t sensor2_health : 2;
    uint8_t sensor2_faultcnt : 3;
    char sensor1_name[8];
    char sensor2_name[8];
} system_status_t;

#define SAMPLE2_BUFFER_SIZE sizeof(sample_sensor2_t)
#define SAMPLE_BUFFER_SIZE sizeof(sample_sensor1_t)

#endif /* CAN_RX_TYPES_H */
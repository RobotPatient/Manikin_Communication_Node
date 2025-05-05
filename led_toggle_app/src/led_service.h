/* led_service.h - LED BLE Service */

#ifndef LED_SERVICE_H_
#define LED_SERVICE_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Function to initialize the LED hardware */
int led_init(void);

/* Function to toggle the LED state */
void led_toggle(void);

/* Function to initialize the LED BLE service */
void led_service_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_SERVICE_H_ */
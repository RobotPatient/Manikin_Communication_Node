/*
 * Copyright (c) 2022 Benjamin Björnsson <benjamin.bjornsson@gmail.com>.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <zephyr/init.h>
 #include <zephyr/drivers/gpio.h>
 #include "stm32h7xx_ll_bus.h"
 #include "stm32h7xx_ll_gpio.h"
 
 static int early_oscen_enable(void)
 {
     /* Enable GPIOH clock */
     LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOH);
 
     /* Configure PH1 as output */
     LL_GPIO_SetPinMode(GPIOH, LL_GPIO_PIN_1, LL_GPIO_MODE_OUTPUT);
     LL_GPIO_SetPinOutputType(GPIOH, LL_GPIO_PIN_1, LL_GPIO_OUTPUT_PUSHPULL);
     LL_GPIO_SetPinSpeed(GPIOH, LL_GPIO_PIN_1, LL_GPIO_SPEED_FREQ_LOW);
     LL_GPIO_SetPinPull(GPIOH, LL_GPIO_PIN_1, LL_GPIO_PULL_UP);
 
     /* Set PH1 high */
     LL_GPIO_SetOutputPin(GPIOH, LL_GPIO_PIN_1);
 
     /* Short delay to stabilize the oscillator if needed */
     for (volatile int i = 0; i < 100000; i++) {
         __NOP();
     }
 
     return 0;
 }
 
 static int board_init(void)
 {
     /* Set led1 inactive since the Arduino bootloader leaves it active */
     const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
 
     if (!gpio_is_ready_dt(&led1)) {
         return -ENODEV;
     }
 
     return gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
 }
 
 SYS_INIT(early_oscen_enable, PRE_KERNEL_1, 0);
 SYS_INIT(board_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
 
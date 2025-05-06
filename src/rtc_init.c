#include <stm32h7xx.h>
#include <stm32h7xx_hal.h>
#include <stm32h7xx_hal_conf.h>
#include <stm32h7xx_hal_rcc.h>
#include <stm32h7xx_hal_pwr.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

static int rtc_hw_init(void)
{
    /* Enable write access to Backup domain */
    HAL_PWR_EnableBkUpAccess();

    /* Optional: Enable LSE if not done via devicetree/clock driver */
    // __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
    // while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET);

    return 0;
}

/* Register the function to run at pre-kernel init (before clock subsystem) */
SYS_INIT(rtc_hw_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

#include <string.h>
#include <zephyr/kernel.h>
#include <session/session.h>
#include <can/can_transport.h>

#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

/**
 * @brief Main application entry point.
 *
 */
int main(void)
{
    k_sleep(K_MSEC(1000));
    can_transport_init();
    session_init();
    // ENABLE PE3


    while (1)
    {
        k_msleep(1000);
    }
}

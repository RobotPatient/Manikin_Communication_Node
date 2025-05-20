#include <string.h>
#include <zephyr/kernel.h>
#include <session/session.h>
#include <can/can_transport.h>

/**
 * @brief Main application entry point.
 *
 */
int main(void)
{
	session_init();
	can_transport_init();
	while (1) {
		k_msleep(1000);
	}
}

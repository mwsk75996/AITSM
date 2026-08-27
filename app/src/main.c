#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <led_status.h>
#include <network.h>

int main(void)
{
	int err;

	err = led_status_init();
	if (err != 0) {
		printk("RGB LED initialization failed, error: %d\n", err);
		return err;
	}

	err = network_init();
	if (err != 0) {
		return err;
	}

	while (true) {
		k_sleep(K_FOREVER);
	}

	return 0;
}

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <data_transmission.h>
#include <led_status.h>
#include <measurement_service.h>
#include <network.h>

int main(void)
{
	int err;

	err = led_status_init();
	if (err != 0) {
		printk("RGB LED initialization failed, error: %d\n", err);
		return err;
	}

	err = aitsm_data_transmission_init();
	if (err != 0) {
		printk("Data transmission initialization failed, error: %d\n", err);
		return err;
	}

	err = aitsm_measurement_service_init();
	if (err != 0) {
		printk("Measurement service initialization failed, error: %d\n", err);
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

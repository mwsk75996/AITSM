#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <data_transmission.h>
#include <led_status.h>
#include <measurement_service.h>
#include <network.h>

LOG_MODULE_REGISTER(main, CONFIG_AITSM_LOG_LEVEL);

int main(void)
{
	int err;

	LOG_INF("AITSM firmware starting");

	err = led_status_init();
	if (err != 0) {
		LOG_ERR("RGB LED initialization failed, error: %d", err);
		return err;
	}

	err = aitsm_data_transmission_init();
	if (err != 0) {
		LOG_ERR("Data transmission initialization failed, error: %d",
			err);
		return err;
	}

	err = aitsm_measurement_service_init();
	if (err != 0) {
		LOG_ERR("Measurement service initialization failed, error: %d",
			err);
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

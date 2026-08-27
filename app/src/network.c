#include <zephyr/sys/printk.h>

#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>

#include <led_status.h>
#include <network.h>

static void lte_event_handler(const struct lte_lc_evt *const event)
{
	switch (event->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		switch (event->nw_reg_status) {
		case LTE_LC_NW_REG_REGISTERED_HOME:
			printk("LTE registered on home network\n");
			/* Use green as the connected fallback until the mode event arrives. */
			(void)led_status_set(LED_STATUS_CONNECTED_LTE_M);
			break;
		case LTE_LC_NW_REG_REGISTERED_ROAMING:
			printk("LTE registered while roaming\n");
			(void)led_status_set(LED_STATUS_CONNECTED_LTE_M);
			break;
		case LTE_LC_NW_REG_SEARCHING:
			printk("Searching for LTE network\n");
			(void)led_status_set(LED_STATUS_CONNECTING);
			break;
		default:
			printk("LTE not registered, status: %d\n",
			       event->nw_reg_status);
			(void)led_status_set(LED_STATUS_DISCONNECTED);
			break;
		}
		break;
	case LTE_LC_EVT_LTE_MODE_UPDATE:
		switch (event->lte_mode) {
		case LTE_LC_LTE_MODE_LTEM:
			printk("LTE mode: LTE-M\n");
			(void)led_status_set(LED_STATUS_CONNECTED_LTE_M);
			break;
		case LTE_LC_LTE_MODE_NBIOT:
			printk("LTE mode: NB-IoT\n");
			(void)led_status_set(LED_STATUS_CONNECTED_NB_IOT);
			break;
		default:
			printk("LTE mode: unknown (%d)\n", event->lte_mode);
			break;
		}
		break;
	default:
		break;
	}
}

int network_init(void)
{
	int err;

	printk("Initializing nRF modem\n");
	err = nrf_modem_lib_init();
	if (err != 0) {
		printk("Modem initialization failed, error: %d\n", err);
		(void)led_status_set(LED_STATUS_ERROR);
		return err;
	}

	/* Register before connecting so the first network event is not missed. */
	lte_lc_register_handler(lte_event_handler);
	(void)led_status_set(LED_STATUS_CONNECTING);

	printk("Connecting to LTE network; this may take a few minutes\n");
	err = lte_lc_connect_async(NULL);
	if (err != 0) {
		printk("LTE connection start failed, error: %d\n", err);
		(void)led_status_set(LED_STATUS_ERROR);
		return err;
	}

	/* lte_lc keeps the connection alive and reports changes through the handler. */
	return 0;
}

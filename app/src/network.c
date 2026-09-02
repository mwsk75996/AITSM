#include <zephyr/sys/printk.h>

#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>

#include <led_status.h>
#include <app_controller.h>
#include <mqtt_client.h>
#include <network.h>

static void lte_event_handler(const struct lte_lc_evt *const event)
{
	switch (event->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		switch (event->nw_reg_status) {
		case LTE_LC_NW_REG_REGISTERED_HOME:
			printk("LTE registered on home network (NB-IoT)\n");
			(void)aitsm_app_post_event(AITSM_APP_EVENT_LTE_CONNECTED, 0);
			break;
		case LTE_LC_NW_REG_REGISTERED_ROAMING:
			printk("LTE registered while roaming (NB-IoT)\n");
			(void)aitsm_app_post_event(AITSM_APP_EVENT_LTE_CONNECTED, 0);
			break;
		case LTE_LC_NW_REG_SEARCHING:
			printk("Searching for LTE network\n");
			(void)aitsm_app_post_event(AITSM_APP_EVENT_LTE_SEARCHING, 0);
			break;
		default:
			printk("LTE not registered, status: %d\n",
			       event->nw_reg_status);
			(void)aitsm_app_post_event(AITSM_APP_EVENT_LTE_DISCONNECTED,
					   event->nw_reg_status);
			break;
		}
		break;
	case LTE_LC_EVT_LTE_MODE_UPDATE:
		/* The modem is configured for NB-IoT only, so any other mode
		 * reported here is unexpected and worth a warning in the log.
		 */
		switch (event->lte_mode) {
		case LTE_LC_LTE_MODE_NBIOT:
			printk("LTE mode: NB-IoT\n");
			break;
		case LTE_LC_LTE_MODE_NONE:
			printk("LTE mode: none\n");
			break;
		default:
			printk("Warning: unexpected LTE mode %d; "
			       "NB-IoT only is configured\n", event->lte_mode);
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

	err = aitsm_mqtt_init();
	if (err != 0) {
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

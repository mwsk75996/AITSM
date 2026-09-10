#include <zephyr/logging/log.h>

#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>

#include <led_status.h>
#include <app_controller.h>
#include <mqtt_client.h>
#include <network.h>

LOG_MODULE_REGISTER(network, CONFIG_AITSM_LOG_LEVEL);

static void lte_event_handler(const struct lte_lc_evt *const event)
{
	switch (event->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		switch (event->nw_reg_status) {
		case LTE_LC_NW_REG_REGISTERED_HOME:
			LOG_INF("LTE registered on home network (NB-IoT)");
			(void)aitsm_app_post_event(AITSM_APP_EVENT_LTE_CONNECTED, 0);
			break;
		case LTE_LC_NW_REG_REGISTERED_ROAMING:
			LOG_INF("LTE registered while roaming (NB-IoT)");
			(void)aitsm_app_post_event(AITSM_APP_EVENT_LTE_CONNECTED, 0);
			break;
		case LTE_LC_NW_REG_SEARCHING:
			LOG_INF("Searching for LTE network");
			(void)aitsm_app_post_event(AITSM_APP_EVENT_LTE_SEARCHING, 0);
			break;
		default:
			LOG_WRN("LTE not registered, status: %d",
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
			LOG_DBG("LTE mode: NB-IoT");
			break;
		case LTE_LC_LTE_MODE_NONE:
			LOG_DBG("LTE mode: none");
			break;
		default:
			LOG_WRN("Unexpected LTE mode %d; NB-IoT only is configured",
				event->lte_mode);
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

	LOG_INF("Initializing nRF modem");
	err = nrf_modem_lib_init();
	if (err != 0) {
		LOG_ERR("Modem initialization failed, error: %d", err);
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
	(void)led_status_set(LED_STATUS_SEARCHING);

	LOG_INF("Connecting to LTE network; this may take a few minutes");
	err = lte_lc_connect_async(NULL);
	if (err != 0) {
		LOG_ERR("LTE connection start failed, error: %d", err);
		(void)led_status_set(LED_STATUS_ERROR);
		return err;
	}

	/* lte_lc keeps the connection alive and reports changes through the handler. */
	return 0;
}

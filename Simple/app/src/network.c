#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>

#include "network.h"

LOG_MODULE_REGISTER(network, LOG_LEVEL_INF);

static aitsm_network_handler_t network_handler;
/* Written by the LTE callback and read by the application thread. */
static atomic_t registered;

static void lte_event_handler(const struct lte_lc_evt *const event)
{
	switch (event->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		switch (event->nw_reg_status) {
		case LTE_LC_NW_REG_REGISTERED_HOME:
		case LTE_LC_NW_REG_REGISTERED_ROAMING:
			LOG_INF("Registreret på NB-IoT-netværket");
			atomic_set(&registered, 1);
			break;
		case LTE_LC_NW_REG_SEARCHING:
			LOG_INF("Søger efter netværk");
			atomic_set(&registered, 0);
			break;
		default:
			LOG_WRN("Ikke registreret på netværket, status: %d", event->nw_reg_status);
			atomic_set(&registered, 0);
			break;
		}
		network_handler();
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		/* The network decides the values; active_time is -1 when PSM is refused. */
		LOG_INF("PSM tildelt: TAU %d s, aktiv tid %d s", event->psm_cfg.tau,
			event->psm_cfg.active_time);
		break;
	default:
		break;
	}
}

int aitsm_network_init(aitsm_network_handler_t handler)
{
	int err;

	network_handler = handler;

	err = nrf_modem_lib_init();
	if (err != 0) {
		LOG_ERR("Modemmet kunne ikke initialiseres: %d", err);
	}
	return err;
}

int aitsm_network_connect(void)
{
	int err;

	/* Register before connecting so the first network event is not missed. */
	lte_lc_register_handler(lte_event_handler);

	LOG_INF("Forbinder til NB-IoT-netværket; det kan tage et par minutter");
	err = lte_lc_connect_async(NULL);
	if (err != 0) {
		LOG_ERR("LTE-forbindelsen kunne ikke startes: %d", err);
	}
	return err;
}

bool aitsm_network_is_connected(void)
{
	return atomic_get(&registered) != 0;
}

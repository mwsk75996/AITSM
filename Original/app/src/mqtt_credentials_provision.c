#include <zephyr/logging/log.h>

#include <modem/modem_key_mgmt.h>
#include <modem/nrf_modem_lib.h>

LOG_MODULE_REGISTER(mqtt_credentials_provision, CONFIG_AITSM_LOG_LEVEL);

static const unsigned char ca_certificate[] = {
#if __has_include("ca-cert.pem")
#include "ca-cert.pem"
#else
	""
#endif
};

static void on_modem_lib_init(int ret, void *ctx)
{
	ARG_UNUSED(ctx);

	if (ret != 0) {
		LOG_ERR("Modem library did not initialize: %d", ret);
		return;
	}

	if (sizeof(ca_certificate) <= 1) {
		LOG_ERR("MQTT CA certificate is missing");
		return;
	}

	ret = modem_key_mgmt_write(CONFIG_MQTT_HELPER_SEC_TAG,
				   MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
				   ca_certificate,
				   sizeof(ca_certificate) - 1);
	if (ret != 0) {
		LOG_ERR("MQTT CA certificate provisioning failed: %d", ret);
	} else {
		LOG_DBG("MQTT CA certificate provisioned");
	}
}

NRF_MODEM_LIB_ON_INIT(mqtt_credentials_init_hook, on_modem_lib_init, NULL);

#include <zephyr/sys/printk.h>

#include <modem/modem_key_mgmt.h>
#include <modem/nrf_modem_lib.h>

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
		printk("Modem library did not initialize: %d\n", ret);
		return;
	}

	if (sizeof(ca_certificate) <= 1) {
		printk("MQTT CA certificate is missing\n");
		return;
	}

	ret = modem_key_mgmt_write(CONFIG_MQTT_HELPER_SEC_TAG,
				   MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
				   ca_certificate,
				   sizeof(ca_certificate) - 1);
	if (ret != 0) {
		printk("MQTT CA certificate provisioning failed: %d\n", ret);
	}
}

NRF_MODEM_LIB_ON_INIT(mqtt_credentials_init_hook, on_modem_lib_init, NULL);

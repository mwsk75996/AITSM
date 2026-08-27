#include <zephyr/kernel.h>

/* The device must use NB-IoT only (issue #25). The build fails if the
 * application configuration ever enables LTE-M or one of the combined
 * LTE-M/NB-IoT modes again.
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_LTE_NETWORK_MODE_NBIOT),
	     "CONFIG_LTE_NETWORK_MODE_NBIOT must be enabled");
BUILD_ASSERT(!IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M),
	     "LTE-M must not be enabled");
BUILD_ASSERT(!IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M_GPS),
	     "LTE-M must not be enabled");
BUILD_ASSERT(!IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M_NBIOT),
	     "The LTE-M/NB-IoT combination mode must not be enabled");
BUILD_ASSERT(!IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M_NBIOT_GPS),
	     "The LTE-M/NB-IoT combination mode must not be enabled");
BUILD_ASSERT(!IS_ENABLED(CONFIG_LTE_NETWORK_MODE_NTN_NBIOT),
	     "NTN NB-IoT must not be enabled");

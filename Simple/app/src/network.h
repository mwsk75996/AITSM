#ifndef AITSM_NETWORK_H_
#define AITSM_NETWORK_H_

#include <stdbool.h>

/** Called from the LTE callback when the device joins or leaves the network. */
typedef void (*aitsm_network_handler_t)(void);

/** Initialize the nRF modem. Must be called before the other modules use the modem. */
int aitsm_network_init(aitsm_network_handler_t handler);

/** Start connecting to the NB-IoT network; the result is reported to the handler. */
int aitsm_network_connect(void);

/** True while the device is registered on the network. */
bool aitsm_network_is_connected(void);

#endif /* AITSM_NETWORK_H_ */

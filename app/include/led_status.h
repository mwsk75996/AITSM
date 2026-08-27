#ifndef AITSM_LED_STATUS_H_
#define AITSM_LED_STATUS_H_

/** LED indication for the cellular connection state. */
enum led_status {
	/** The modem is searching for a network. */
	LED_STATUS_CONNECTING,
	/** The modem is registered using LTE-M. */
	LED_STATUS_CONNECTED_LTE_M,
	/** The modem is registered using NB-IoT. */
	LED_STATUS_CONNECTED_NB_IOT,
	/** The modem is not registered on a network. */
	LED_STATUS_DISCONNECTED,
	/** A modem or LED initialization error occurred. */
	LED_STATUS_ERROR,
};

/** Initialize the RGB LED and set it to the disconnected state. */
int led_status_init(void);

/** Set the RGB LED to the requested connection state. */
int led_status_set(enum led_status status);

#endif /* AITSM_LED_STATUS_H_ */

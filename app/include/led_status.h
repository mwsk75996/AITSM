#ifndef AITSM_LED_STATUS_H_
#define AITSM_LED_STATUS_H_

#include <stdbool.h>
#include <stdint.h>

/** LED indication for cellular, cloud and publish state. */
enum led_status {
	/** Searching for an LTE network: blue, blinking. */
	LED_STATUS_SEARCHING,
	/** Registered on the LTE network: green, solid. */
	LED_STATUS_LTE_CONNECTED,
	/** Connected to the cloud MQTT broker: cyan, solid. */
	LED_STATUS_MQTT_CONNECTED,
	/** Not connected: red, solid. */
	LED_STATUS_DISCONNECTED,
	/** Connection or modem error: red, solid. */
	LED_STATUS_ERROR,
	/** Transient: measurement published successfully (short white flash). */
	LED_STATUS_PUBLISH_OK,
	/** Transient: publish failed (short red flash). */
	LED_STATUS_PUBLISH_ERROR,
};

/** RGB colour with one 8-bit value per channel. */
struct led_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

/** How a status colour is shown. */
enum led_pattern {
	LED_PATTERN_SOLID,
	LED_PATTERN_BLINK,
};

/** Static description of how a status is displayed. */
struct led_indication {
	struct led_color color;
	enum led_pattern pattern;
};

/** Initialize the RGB LED and show the initial status. */
int led_status_init(void);

/**
 * Show a status.
 *
 * Stable statuses stay until replaced. Transient publish statuses are shown
 * briefly and then automatically return to the last stable status, so a
 * success or failure flash never overwrites the connection state permanently.
 */
int led_status_set(enum led_status status);

/** Return the colour and pattern associated with a status. */
struct led_indication led_status_indication(enum led_status status);

/** True if the status is a transient publish indication. */
bool led_status_is_transient(enum led_status status);

/** The last stable (non-transient) status. */
enum led_status led_status_stable(void);

/** The status currently shown, including any transient indication. */
enum led_status led_status_displayed(void);

#endif /* AITSM_LED_STATUS_H_ */

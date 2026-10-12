#ifndef AITSM_LED_H_
#define AITSM_LED_H_

/** Colours shown on the RGB LED. Each colour has one meaning; see main.c. */
enum aitsm_led_color {
	AITSM_LED_OFF,
	AITSM_LED_RED,
	AITSM_LED_GREEN,
	AITSM_LED_BLUE,
	AITSM_LED_CYAN,
	AITSM_LED_WHITE,
};

/** Check that the three PWM channels of the RGB LED are ready. */
int aitsm_led_init(void);

/** Show a colour until the next call. Does nothing when CONFIG_AITSM_LED=n. */
void aitsm_led_set(enum aitsm_led_color color);

#endif /* AITSM_LED_H_ */

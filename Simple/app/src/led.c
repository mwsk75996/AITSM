#include <errno.h>
#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "led.h"

LOG_MODULE_REGISTER(led, LOG_LEVEL_INF);

/* The LED is dimmed to 1/8 of full brightness to save power. */
#define BRIGHTNESS_DIVIDER 8

static const struct pwm_dt_spec red_led = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
static const struct pwm_dt_spec green_led = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1));
static const struct pwm_dt_spec blue_led = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2));

static void set_channel(const struct pwm_dt_spec *led, bool on)
{
	(void)pwm_set_pulse_dt(led, on ? led->period / BRIGHTNESS_DIVIDER : 0);
}

int aitsm_led_init(void)
{
	if (!pwm_is_ready_dt(&red_led) || !pwm_is_ready_dt(&green_led) ||
	    !pwm_is_ready_dt(&blue_led)) {
		LOG_ERR("RGB-LED'en er ikke klar");
		return -ENODEV;
	}

	aitsm_led_set(AITSM_LED_OFF);
	return 0;
}

void aitsm_led_set(enum aitsm_led_color color)
{
	if (!IS_ENABLED(CONFIG_AITSM_LED)) {
		color = AITSM_LED_OFF;
	}

	/* Yellow is red and green together; white is all three. */
	set_channel(&red_led, color == AITSM_LED_RED || color == AITSM_LED_YELLOW ||
				      color == AITSM_LED_WHITE);
	set_channel(&green_led, color == AITSM_LED_GREEN || color == AITSM_LED_YELLOW ||
					color == AITSM_LED_WHITE);
	set_channel(&blue_led, color == AITSM_LED_BLUE || color == AITSM_LED_WHITE);
}

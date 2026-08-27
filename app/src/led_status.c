#include <errno.h>
#include <stdint.h>

#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include <led_status.h>

#define RED_LED_NODE DT_ALIAS(pwm_led0)
#define GREEN_LED_NODE DT_ALIAS(pwm_led1)
#define BLUE_LED_NODE DT_ALIAS(pwm_led2)

#if !DT_NODE_HAS_STATUS(RED_LED_NODE, okay) || \
	!DT_NODE_HAS_STATUS(GREEN_LED_NODE, okay) || \
	!DT_NODE_HAS_STATUS(BLUE_LED_NODE, okay)
#error "This board requires pwm_led0, pwm_led1 and pwm_led2 aliases"
#endif

static const struct pwm_dt_spec red_led = PWM_DT_SPEC_GET(RED_LED_NODE);
static const struct pwm_dt_spec green_led = PWM_DT_SPEC_GET(GREEN_LED_NODE);
static const struct pwm_dt_spec blue_led = PWM_DT_SPEC_GET(BLUE_LED_NODE);

static struct k_mutex led_mutex;

static uint32_t brightness_to_pulse(const struct pwm_dt_spec *led,
					    uint8_t brightness)
{
	return (uint32_t)(((uint64_t)led->period * brightness) / UINT8_MAX);
}

static int set_color(uint8_t red, uint8_t green, uint8_t blue)
{
	int ret;

	ret = pwm_set_pulse_dt(&red_led, brightness_to_pulse(&red_led, red));
	if (ret != 0) {
		return ret;
	}

	ret = pwm_set_pulse_dt(&green_led,
				       brightness_to_pulse(&green_led, green));
	if (ret != 0) {
		return ret;
	}

	return pwm_set_pulse_dt(&blue_led,
				       brightness_to_pulse(&blue_led, blue));
}

int led_status_init(void)
{
	if (!pwm_is_ready_dt(&red_led) ||
	    !pwm_is_ready_dt(&green_led) ||
	    !pwm_is_ready_dt(&blue_led)) {
		return -ENODEV;
	}

	k_mutex_init(&led_mutex);
	return led_status_set(LED_STATUS_DISCONNECTED);
}

int led_status_set(enum led_status status)
{
	uint8_t red = 0;
	uint8_t green = 0;
	uint8_t blue = 0;
	int ret;

	switch (status) {
	case LED_STATUS_CONNECTING:
		blue = UINT8_MAX;
		break;
	case LED_STATUS_CONNECTED_LTE_M:
		green = UINT8_MAX;
		break;
	case LED_STATUS_CONNECTED_NB_IOT:
		red = UINT8_MAX;
		green = UINT8_MAX;
		break;
	case LED_STATUS_DISCONNECTED:
	case LED_STATUS_ERROR:
		red = UINT8_MAX;
		break;
	default:
		return -EINVAL;
	}

	ret = k_mutex_lock(&led_mutex, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	ret = set_color(red, green, blue);
	k_mutex_unlock(&led_mutex);

	return ret;
}

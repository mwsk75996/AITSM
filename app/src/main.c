#include <stdint.h>

#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#define RED_LED_NODE DT_ALIAS(pwm_led0)
#define GREEN_LED_NODE DT_ALIAS(pwm_led1)
#define BLUE_LED_NODE DT_ALIAS(pwm_led2)

#if !DT_NODE_HAS_STATUS(RED_LED_NODE, okay) || \
    !DT_NODE_HAS_STATUS(GREEN_LED_NODE, okay) || \
    !DT_NODE_HAS_STATUS(BLUE_LED_NODE, okay)
#error "This board requires led0, led1 and led2 aliases"
#endif

static const struct pwm_dt_spec red_led =
	PWM_DT_SPEC_GET(RED_LED_NODE);
static const struct pwm_dt_spec green_led =
	PWM_DT_SPEC_GET(GREEN_LED_NODE);
static const struct pwm_dt_spec blue_led =
	PWM_DT_SPEC_GET(BLUE_LED_NODE);

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

int main(void)
{
	if (!pwm_is_ready_dt(&red_led) ||
	    !pwm_is_ready_dt(&green_led) ||
	    !pwm_is_ready_dt(&blue_led)) {
		return 0;
	}

	while (1) {
		/* Fade continuously through green -> blue -> red -> green. */
		for (uint16_t step = 0; step <= UINT8_MAX; step++) {
			set_color(0, UINT8_MAX - step, step);
			k_sleep(K_MSEC(10));
		}

		for (uint16_t step = 0; step <= UINT8_MAX; step++) {
			set_color(step, 0, UINT8_MAX - step);
			k_sleep(K_MSEC(10));
		}

		for (uint16_t step = 0; step <= UINT8_MAX; step++) {
			set_color(UINT8_MAX - step, step, 0);
			k_sleep(K_MSEC(10));
		}
	}
}

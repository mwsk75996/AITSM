#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include <led_status.h>

/*
 * The RGB LED is driven by PWM on the Thingy:91 X, but the aliases are not
 * present when the module is compiled for native_sim unit tests. Guard the
 * hardware access so the status and blink logic can be tested without PWM.
 */
#define LED_STATUS_HAS_PWM                                  \
	(DT_HAS_ALIAS(pwm_led0) && DT_HAS_ALIAS(pwm_led1) && \
	 DT_HAS_ALIAS(pwm_led2))

#if LED_STATUS_HAS_PWM
#include <zephyr/drivers/pwm.h>

#define RED_LED_NODE DT_ALIAS(pwm_led0)
#define GREEN_LED_NODE DT_ALIAS(pwm_led1)
#define BLUE_LED_NODE DT_ALIAS(pwm_led2)

static const struct pwm_dt_spec red_led = PWM_DT_SPEC_GET(RED_LED_NODE);
static const struct pwm_dt_spec green_led = PWM_DT_SPEC_GET(GREEN_LED_NODE);
static const struct pwm_dt_spec blue_led = PWM_DT_SPEC_GET(BLUE_LED_NODE);
#endif /* LED_STATUS_HAS_PWM */

/** Half period for a blinking status, in milliseconds. */
#define LED_STATUS_BLINK_PERIOD_MS 250

/** Number of short blinks for a transient publish indication. */
#define LED_STATUS_TRANSIENT_BLINKS 5

/** On-time for each transient publish blink, in milliseconds. */
#define LED_STATUS_TRANSIENT_ON_MS 150

/** Off-time between transient publish blinks, in milliseconds. */
#define LED_STATUS_TRANSIENT_OFF_MS 150

static struct k_mutex led_mutex;
static struct k_work_delayable blink_work;
static struct k_work_delayable transient_work;

static enum led_status stable_status = LED_STATUS_DISCONNECTED;
static enum led_status displayed_status = LED_STATUS_DISCONNECTED;
static bool blink_visible;
static bool transient_on;
static uint8_t transient_blinks_left;

static void blink_work_handler(struct k_work *work);
static void transient_work_handler(struct k_work *work);

#if LED_STATUS_HAS_PWM
static uint32_t brightness_to_pulse(const struct pwm_dt_spec *led,
				    uint8_t brightness)
{
	return (uint32_t)(((uint64_t)led->period * brightness) / UINT8_MAX);
}
#endif

static int set_color(uint8_t red, uint8_t green, uint8_t blue)
{
#if LED_STATUS_HAS_PWM
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
#else
	ARG_UNUSED(red);
	ARG_UNUSED(green);
	ARG_UNUSED(blue);

	return 0;
#endif
}

struct led_indication led_status_indication(enum led_status status)
{
	switch (status) {
	case LED_STATUS_SEARCHING:
		return (struct led_indication){
			.color = { .red = 0, .green = 0, .blue = UINT8_MAX },
			.pattern = LED_PATTERN_BLINK,
		};
	case LED_STATUS_LTE_CONNECTED:
		return (struct led_indication){
			.color = { .red = 0, .green = UINT8_MAX, .blue = 0 },
			.pattern = LED_PATTERN_SOLID,
		};
	case LED_STATUS_MQTT_CONNECTED:
		return (struct led_indication){
			.color = { .red = 0, .green = UINT8_MAX, .blue = UINT8_MAX },
			.pattern = LED_PATTERN_SOLID,
		};
	case LED_STATUS_DISCONNECTED:
	case LED_STATUS_ERROR:
		return (struct led_indication){
			.color = { .red = UINT8_MAX, .green = 0, .blue = 0 },
			.pattern = LED_PATTERN_SOLID,
		};
	case LED_STATUS_PUBLISH_OK:
		return (struct led_indication){
			.color = { .red = UINT8_MAX,
				   .green = UINT8_MAX,
				   .blue = UINT8_MAX },
			.pattern = LED_PATTERN_SOLID,
		};
	case LED_STATUS_PUBLISH_ERROR:
		return (struct led_indication){
			.color = { .red = UINT8_MAX, .green = 0, .blue = 0 },
			.pattern = LED_PATTERN_SOLID,
		};
	default:
		return (struct led_indication){
			.color = { .red = 0, .green = 0, .blue = 0 },
			.pattern = LED_PATTERN_SOLID,
		};
	}
}

bool led_status_is_transient(enum led_status status)
{
	return status == LED_STATUS_PUBLISH_OK ||
	       status == LED_STATUS_PUBLISH_ERROR;
}

static int apply_status(enum led_status status, bool visible)
{
	struct led_indication indication = led_status_indication(status);

	if (indication.pattern == LED_PATTERN_BLINK && !visible) {
		return set_color(0, 0, 0);
	}

	return set_color(indication.color.red, indication.color.green,
			 indication.color.blue);
}

static void blink_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (k_mutex_lock(&led_mutex, K_NO_WAIT) != 0) {
		(void)k_work_reschedule(&blink_work,
					K_MSEC(LED_STATUS_BLINK_PERIOD_MS));
		return;
	}

	if (led_status_indication(displayed_status).pattern ==
	    LED_PATTERN_BLINK) {
		blink_visible = !blink_visible;
		(void)apply_status(displayed_status, blink_visible);
		(void)k_work_reschedule(&blink_work,
					K_MSEC(LED_STATUS_BLINK_PERIOD_MS));
	} else {
		blink_visible = false;
	}

	k_mutex_unlock(&led_mutex);
}

/* Return to the last stable status and resume blinking if it requires it. */
static void restore_stable(void)
{
	displayed_status = stable_status;
	blink_visible =
		led_status_indication(displayed_status).pattern ==
		LED_PATTERN_BLINK;
	(void)apply_status(displayed_status, blink_visible);

	if (blink_visible) {
		(void)k_work_reschedule(&blink_work,
					K_MSEC(LED_STATUS_BLINK_PERIOD_MS));
	}
}

static void transient_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (k_mutex_lock(&led_mutex, K_NO_WAIT) != 0) {
		(void)k_work_reschedule(&transient_work,
					K_MSEC(LED_STATUS_TRANSIENT_ON_MS));
		return;
	}

	if (transient_on) {
		/* Turn off between two blinks. */
		transient_on = false;
		(void)set_color(0, 0, 0);
		(void)k_work_reschedule(&transient_work,
					K_MSEC(LED_STATUS_TRANSIENT_OFF_MS));
	} else if (transient_blinks_left > 1) {
		transient_blinks_left--;
		transient_on = true;
		(void)apply_status(displayed_status, true);
		(void)k_work_reschedule(&transient_work,
					K_MSEC(LED_STATUS_TRANSIENT_ON_MS));
	} else {
		transient_blinks_left = 0;
		transient_on = false;
		restore_stable();
	}

	k_mutex_unlock(&led_mutex);
}

int led_status_init(void)
{
#if LED_STATUS_HAS_PWM
	if (!pwm_is_ready_dt(&red_led) || !pwm_is_ready_dt(&green_led) ||
	    !pwm_is_ready_dt(&blue_led)) {
		return -ENODEV;
	}
#endif

	k_mutex_init(&led_mutex);
	k_work_init_delayable(&blink_work, blink_work_handler);
	k_work_init_delayable(&transient_work, transient_work_handler);
	blink_visible = false;
	transient_on = false;
	transient_blinks_left = 0;
	stable_status = LED_STATUS_DISCONNECTED;
	displayed_status = LED_STATUS_DISCONNECTED;

	return apply_status(LED_STATUS_DISCONNECTED, true);
}

int led_status_set(enum led_status status)
{
	int ret;

	switch (status) {
	case LED_STATUS_SEARCHING:
	case LED_STATUS_LTE_CONNECTED:
	case LED_STATUS_MQTT_CONNECTED:
	case LED_STATUS_DISCONNECTED:
	case LED_STATUS_ERROR:
	case LED_STATUS_PUBLISH_OK:
	case LED_STATUS_PUBLISH_ERROR:
		break;
	default:
		return -EINVAL;
	}

	ret = k_mutex_lock(&led_mutex, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	if (led_status_is_transient(status)) {
		/* Blink a few times without changing the stable status. */
		displayed_status = status;
		blink_visible = false;
		transient_on = true;
		transient_blinks_left = LED_STATUS_TRANSIENT_BLINKS;
		(void)k_work_cancel_delayable(&blink_work);
		ret = apply_status(status, true);

		if (ret == 0) {
			(void)k_work_reschedule(&transient_work,
						K_MSEC(LED_STATUS_TRANSIENT_ON_MS));
		}
	} else {
		stable_status = status;
		displayed_status = status;
		(void)k_work_cancel_delayable(&transient_work);

		if (led_status_indication(status).pattern ==
		    LED_PATTERN_BLINK) {
			blink_visible = true;
			ret = apply_status(status, true);

			if (ret == 0) {
				(void)k_work_reschedule(
					&blink_work,
					K_MSEC(LED_STATUS_BLINK_PERIOD_MS));
			}
		} else {
			blink_visible = false;
			(void)k_work_cancel_delayable(&blink_work);
			ret = apply_status(status, true);
		}
	}

	k_mutex_unlock(&led_mutex);

	return ret;
}

enum led_status led_status_stable(void)
{
	return stable_status;
}

enum led_status led_status_displayed(void)
{
	return displayed_status;
}

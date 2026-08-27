#ifndef AITSM_LED_FADER_H_
#define AITSM_LED_FADER_H_

/**
 * Run the RGB LED fade loop.
 *
 * This function only returns when one or more configured PWM devices are not
 * ready.
 */
void led_fader_run(void);

#endif

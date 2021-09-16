/** \file payg_led_display.h
 * \brief Visualize PAYG state with onboard LED
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Basic demonstration to show PAYG state on the onboard LED of
 * the STM32F103RB Nucleo-64 dev board (LD2).
 *
 * States:
 * - PAYG DISABLED == LED Off, Solid
 * - PAYG ENABLED == LED On, Blinking
 * - PAYG UNLOCKED == LED On, Solid
 *
 * This module just provides the APIs to enter each display state,
 * `product_payg_state_manager.h` handles updating the LED display
 * when the stored credit changes.
 */

#ifndef PAYG_LED_DISPLAY__H
#define PAYG_LED_DISPLAY__H

#ifdef __cplusplus
extern "C" {
#endif

// Blink duration for 'enabled' state in milliseconds
#define PAYG_LED_DISPLAY_BLINK_MSEC K_MSEC(250)

/** @brief payg_led_display_init
 *
 * Initialize PAYG LED display GPIO and state machine.
 * Must be called before any other `payg_led_display`
 * functions.
 */
void payg_led_display_init(void);

/**
 * @brief payg_led_display_begin_blinking
 *
 * Cause the PAYG LED to begin blinking at
 * the rate defined by `PAYG_LED_DISPLAY_BLINK_MSEC`
 */
void payg_led_display_begin_blinking(void);

/**
 * @brief payg_led_display_solid_on
 *
 * Turn PAYG LED on (solid, no blinking).
 */
void payg_led_display_solid_on(void);

/**
 * @brief payg_led_display_solid_off
 *
 * Turn PAYG LED display off (solid, no blinking).
 */
void payg_led_display_solid_off(void);

#ifdef __cplusplus
}
#endif
#endif // PAYG_LED_DISPLAY__H
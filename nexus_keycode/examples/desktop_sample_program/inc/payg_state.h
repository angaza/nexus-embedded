/**
 * @file payg_state.h
 * @author Angaza
 * @date 25 February 2020
 * @brief File containing PAYG state and credit management.
 *
 * This file is an example of the product-side code required to track and
 * report the PAYG state and remaining credit. The Nexus Keycode library assumes
 * that the system/product has some way of reliably keeping track of the
 * remaining credit and also the state. The library will call
 * `port_payg_state_get_current` to get the current state when processing
 * keycodes. It will also call `port_payg_credit_add`,
 * `port_payg_credit_set`, and `port_payg_credit_unlock` when processing
 * keycodes of the matching types.
 */

#include "nexus_keycode_port.h"
#ifndef PAYG_STATE_H
#define PAYG_STATE_H

#include <stdbool.h>
#include <stdint.h>

// Naive implementation of tracking credit.
NEXUS_PACKED_STRUCT payg_state_struct
{
    uint32_t credit;
    // 0 = not unlocked, 1 = unlocked
    // uint16_t to pad to half-word alignment on 4-byte architecture
    uint16_t is_unlocked;
};

#define PROD_PAYG_STATE_BLOCK_LENGTH sizeof(struct payg_state_struct)

/* @brief Initializes the internal state of the PAYG state.
 */
void payg_state_init(void);

/**
 * Inform the product code that some credit has been consumed. Periodically call
 * this function when credit is used up.
 *
 * Credit is most often defined in terms of wall-clock time. In that case, this
 * function should be called periodically according to the passage of time. It
 * should be passed the number of seconds elapsed since the last time the
 * function was called. The simplest approach is simply to call this function
 * one per second, every second, always passing amount `1`. Equally simple
 * would be to call this function once per minute, every minute, always passing
 * amount `60`. Regardless, the most straightforward approach will depend on
 * the product.
 *
 * \par Example Scenario 1:\n
 * (For reference only.  Actual implementation will differ based on product)
 * -# Product implements an RTC
 * -# Every minute, product RTC code calls payg_state_consume_credit()
 *     - @code
 *      if (min != last_minuteInHour)
 *      {
 *          last_minuteInHour = min;
 *          // Consume one minute of time (typically, passed as seconds)
 *          payg_state_consume_credit(60);
 *      }
 *      @endcode
 * \par Example Scenario 2:\n
 * (For reference only.  Actual implementation will differ based on product)
 * -# Product consumes credit in discrete usage-based 'chunks' (e.g. 'pump X
 * gallons')
 * -# When product use exceeds a given 'chunk', consume credit.
 *      - @code
 *      uint8_t gallons_pumped;
 *      while (pump_is_running)
 *      {
 *          gallons_pumped = update_gallons_pumped();
 *      }
 *      // consume a 'chunk' of credit every 15 gallons pumped
 *      if (gallons_pumped % 15 == 0)
 *      {
 *          payg_state_consume_credit(1);
 *      }
 *      @endcode
 *
 * \note May be called at interrupt time.
 * \param amount amount of credit (e.g., number of seconds) used since the last
 * call
 */
void payg_state_consume_credit(uint32_t amount);

/**
 * Retrieve the number of remaining PAYG credit 'units'.
 *
 * This function returns a positive number if any PAYG credit is remaining,
 * and '0' otherwise.  For time-based units, this value is in 'seconds',
 * for usage-based units, the units returned are product-dependent.
 *
 * \return number of seconds of PAYG credit remaining
 */
uint32_t payg_state_get_remaining_credit(void);

/*! \brief Call to store the current PAYG state in nonvolatile storage.
 *
 * Convenience wrapper to update the PAYG state in nonvolatile.
 *
 * Provided here so that `processing.c` can periodically update the PAYG
 * state without needing to know many details.
 *
 * \return void
 */
void payg_state_update_nv(void);

#endif

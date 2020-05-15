
#include <stdint.h>

/* Initialize the values of the battery resource.
 *
 * The battery resource is exposed via Nexus Channel, in the following manner:
 *
 * * GET requests are unsecured - any device may GET the current battery state
 * * POST requests are secured - a Nexus channel link must exist to POST
 *
 * \return void
 */
void battery_resource_init(void);

/* Convenience function to print certain battery resource properties.
 *
 * Battery charge, threshold, and low battery alert will be printed
 * to stdout.
 */
void battery_resource_print_status(void);

/* Simulate a GET which prints out the state of this battery resource.
 */
void battery_resource_simulate_get(void);

/* Simulate a POST which updates the state of this battery resource.
 *
 * \param battery_thteshold 0-20% low battery threshold
 */
void battery_resource_simulate_post_update_properties(
    uint8_t battery_threshold);

/* Update the battery model with the latest charge percentage.
 *
 * \param charge_percent Percentage of charge remaining
 */
void battery_resource_update_charge(uint8_t charge_percent);

/* Update the 'low battery' threshold
 *
 * The battery should report 'low' when the charge percentage is below
 * this threshold. For example, if charge percentage is '10%' and the
 * low battery threshold is '20%', the 'lowbattery' property of the battery
 * will be true.
 *
 * \param threshold_percent low battery threshold in percent.
 */
void battery_resource_update_low_threshold(uint8_t threshold_percent);

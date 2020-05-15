/**
 * @file screen.h
 * @author Angaza
 * @date 13 August 2018
 * @brief Contains product-side screen/display headers.
 *
 * A production program will not use these files, this is simply an example
 * allowing demonstration of the Nexus Keycode library using a terminal.
 *
 * The library depends on the product to make feedback interfaces available
 * to display feedback to the user about various functions. For example,
 * when a user enters a keycode, he or she should receive some notification as
 * to whether the keycode was valid. Another common use case is to show the
 * the current PAYG state.
 */

#ifndef SCREEN_H
#define SCREEN_H

/* @brief Display the PAYG status to the user.
 *
 * Periodically called by the application, as required, to retrieve and
 * display the number of remaining credit seconds to the user.
*/
void screen_display_status(void);

/* @brief Display the Nexus Channel link status to user.
 *
 * Currently this is the count of active links.
 */
void screen_display_nexus_channel_state(void);

#endif

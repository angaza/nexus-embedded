/**
 * @file menu.h
 * @author Angaza
 * @date 25 February 2020
 * @brief file containing example Nexus menu for demonstration program.
 *
 * This file provides menu functionality to the main program loop, which, once
 * initialized, will continue to enter this menu. The menu gives the user
 * options like 'entering a keycode' (for Nexus Keycode), or 'simulating
 * Nexus Channel communications' (for Nexus Channel), and can be expanded
 * to other demonstration cases in the future.
 */

#ifndef MENU_H
#define MENU_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* @brief Prompt the user for an action to perform.
 *
 * This is essentially a thin layer allowing the user to call other functions
 * that will handle further processing (see `keyboard.h` for instance).
 *
 * @param instream the input stream to receive keys from, e.g. `stdin`
 */
void menu_prompt(void);

#ifdef __cplusplus
}
#endif

#endif

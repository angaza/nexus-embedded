/** \file keyboard.c
 * \brief Keyboard input handling.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keyboard.h"
#include "nexus_batt_resource.h"

// include 'nx_common' to get configuration values
#include "nx_common.h"
#include "nx_keycode.h"
#include "screen.h"
#if CONFIG_NEXUS_KEYCODE_USE_FULL_KEYCODE_PROTOCOL
    #define MAX_KEYCODE_LENGTH                                                 \
        (14u + 2u /* * & # */ + 4u /* hypens or spaces */)
    #define PROTOCOL_NAME "Full"
#elif CONFIG_NEXUS_KEYCODE_USE_SMALL_KEYCODE_PROTOCOL
    #define MAX_KEYCODE_LENGTH (15u + 4u /* hypens or spaces */)
    #define PROTOCOL_NAME "Small"
#else
    #define MAX_KEYCODE_LENGTH 0
    #error "Undefined keycode protocol. Is Nexus Keycode enabled in config?"
#endif

static struct
{
    char keycode_buffer[MAX_KEYCODE_LENGTH + 1];
    bool keycode_to_process;
} _this;

void clear_input_buffer()
{
    int c = getchar();
    while (c != '\n' && c != EOF)
    {
        c = getchar();
    }
}

void keyboard_init()
{
    clear_input_buffer();
    memset(&_this, 0x00, sizeof(_this));
}

// returns a character stream (max length 49). Caller may copy up to
// 50 bytes from `received_input` back into a local copy.
// Return 0 if length exceeds `max_length`
size_t keyboard_obtain_generic_input(FILE* instream,
                                     char* received_input,
                                     size_t max_length)
{
    char input_chars[50] = {0};

    if (fgets(input_chars, sizeof(input_chars), instream))
    {
        // delete trailing newline
        input_chars[strcspn(input_chars, "\n")] = '\0';
    }

    size_t length = strnlen(input_chars, 50);
    printf("\tInput (length=%zu): %s\n", length, input_chars);

    if (length <= max_length)
    {
        memcpy(received_input, input_chars, length);
    }
    else
    {
        length = 0;
    }
    clear_input_buffer();
    return length;
}

void keyboard_prompt_keycode(FILE* instream)
{
    clear_input_buffer();

    printf("Please input a %s keycode (%d digits maximum): ",
           PROTOCOL_NAME,
           MAX_KEYCODE_LENGTH);

    (void) keyboard_obtain_generic_input(
        instream, _this.keycode_buffer, MAX_KEYCODE_LENGTH);

// Pre-process the input.
#if CONFIG_NEXUS_KEYCODE_USE_FULL_KEYCODE_PROTOCOL
    char first_char = _this.keycode_buffer[0];
    char last_char =
        _this.keycode_buffer[strnlen(_this.keycode_buffer, MAX_KEYCODE_LENGTH) -
                             1];
    if (first_char != '*')
    {
        printf("\tInvalid input '%s'. Full keycodes must begin with '*'\n",
               _this.keycode_buffer);
    }
    else if (last_char != '#')
    {
        printf("\tInvalid input '%s'. Full keycodes must end with '#'\n",
               _this.keycode_buffer);
    }
#endif

    // Strip out hyphens and spaces.
    char processed_input[MAX_KEYCODE_LENGTH + 1] = {0};
    memset(processed_input, 0, sizeof(processed_input));
    uint8_t j = 0;
    for (uint8_t i = 0; i < strnlen(_this.keycode_buffer, MAX_KEYCODE_LENGTH);
         i++)
    {
        char* ptr = &_this.keycode_buffer[i];
        if (*ptr != '-' && *ptr != ' ')
        {
            processed_input[j] = *ptr;
            j++;
        }
    }
    strncpy(_this.keycode_buffer, processed_input, MAX_KEYCODE_LENGTH);
    _this.keycode_to_process = true;
}

void keyboard_prompt_update_battery_threshold(FILE* instream)
{
    clear_input_buffer();

    printf("Please input a new battery threshold (0-20)\n");

    char charge_threshold[2] = {0};

    (void) keyboard_obtain_generic_input(instream, charge_threshold, 2);

    const uint8_t threshold_int = atoi(charge_threshold);
    if (threshold_int > 20)
    {
        printf("Threshold value must be between 0-20%%\n");
    }

    battery_resource_simulate_post_update_properties(threshold_int);
}

void keyboard_process_keycode(void)
{
    if (!_this.keycode_to_process)
    {
        return;
    }
#ifndef NEXUS_KEYCODE_HANDLE_SINGLE_KEY
    // Pass the keycode into the Nexus Keycode library 'all at once'.
    size_t length = strnlen(_this.keycode_buffer, sizeof(_this.keycode_buffer));
    struct nx_keycode_complete_code keycode = {.keys = _this.keycode_buffer,
                                               .length = (uint8_t) length};
    printf("\npassing along key=[%s] len=%zu\n", _this.keycode_buffer, length);
    if (!nx_keycode_handle_complete_keycode(&keycode))
    {
        printf("\tUnable to parse the keycode %s.\n", _this.keycode_buffer);
    }
#else
    // Pass each entered key into the Nexus Keycode library 'key-by-key'.
    uint8_t i;
    for (i = 0; i < strnlen(_this.keycode_buffer, sizeof(_this.keycode_buffer));
         i++)
    {
        if (!nx_keycode_handle_single_key(_this.keycode_buffer[i]))
        {
            printf("\tUnable to parse the key %c.\n", _this.keycode_buffer[i]);
        }
    }
#endif
    memset(_this.keycode_buffer, 0, sizeof(_this.keycode_buffer));
    _this.keycode_to_process = false;
}

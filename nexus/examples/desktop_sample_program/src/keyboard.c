/** \file keyboard.c
 * \brief Keyboard input handling.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include <stdio.h>
#include <string.h>

#include "keyboard.h"
// include 'nx_core' to get core, including configuration values
#include "nx_core.h"
#include "nx_keycode.h"
#include "screen.h"
#if CONFIG_NEXUS_KEYCODE_USE_FULL_KEYCODE_PROTOCOL
#define MAX_KEYCODE_LENGTH (14u + 2u /* * & # */ + 4u /* hypens or spaces */)
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
    bool clear_on_next_prompt;
} _this;

void clear_input_buffer()
{
    if (!_this.clear_on_next_prompt)
    {
        _this.clear_on_next_prompt = true;
        return;
    }
    memset(_this.keycode_buffer, 0, sizeof(_this.keycode_buffer));
    int c = getchar();
    while (c != '\n' && c != EOF)
    {
        c = getchar();
    }
}

void keyboard_init()
{
    _this.clear_on_next_prompt = true;
    clear_input_buffer();
}

void keyboard_prompt_keycode(FILE* instream)
{
    clear_input_buffer();

    printf("Please input a %s keycode (%d digits maximum): ",
           PROTOCOL_NAME,
           MAX_KEYCODE_LENGTH);
    if (fgets(_this.keycode_buffer, sizeof(_this.keycode_buffer), instream))
    {
        // Delete any trailing newline.
        _this.keycode_buffer[strcspn(_this.keycode_buffer, "\n")] = '\0';
    }

    const size_t length = strnlen(_this.keycode_buffer, MAX_KEYCODE_LENGTH);
    printf("\tInput (length=%zu): %s\n", length, _this.keycode_buffer);
    if (length < MAX_KEYCODE_LENGTH)
    {
        _this.clear_on_next_prompt = false;
    }

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
}

void keyboard_process_keycode(void)
{
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
}

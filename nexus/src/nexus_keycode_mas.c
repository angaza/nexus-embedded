/** \file
 * Nexus Keycode Message Assembly Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_keycode_mas.h"

#if NEXUS_KEYCODE_ENABLED

#include "include/nxp_core.h"
#include "include/nxp_keycode.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_nv.h"

/** Internal struct of data persisted to NV.
 */
static struct
{
    NEXUS_PACKED_STRUCT
    {
        uint8_t graceperiod_keycodes;
        uint8_t pad[3];
    }
    stored;
    nexus_keycode_mas_message_handler handler;
    struct nexus_keycode_frame partial;
    bool max_length_exceeded;
    uint32_t rl_bucket; // rate limiting bucket
} _this_core;

//
// STATIC SANITY ASSERT
//
NEXUS_STATIC_ASSERT(
    sizeof(_this_core.stored) ==
        (NX_CORE_NV_BLOCK_0_LENGTH - NEXUS_NV_BLOCK_ID_WIDTH -
         NEXUS_NV_BLOCK_CRC_WIDTH),
    "nexus_keycode_mas: _this_core.stored invalid size for NV block.");

//
// FORWARD DECLARATIONS
//
#ifndef NEXUS_INTERNAL_IMPL_NON_STATIC
NEXUS_IMPL_STATIC uint32_t nexus_keycode_mas_bookend_process(void);
#endif

//
// UTILITY ROUTINES
//

NEXUS_IMPL_STATIC void
nexus_keycode_rate_limit_add_time(const uint32_t seconds_elapsed)
{

    NEXUS_STATIC_ASSERT(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX < 256,
        "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX is too large (> 255)");

    NEXUS_STATIC_ASSERT(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT < 3601,
        "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT is too "
        "large (> 3601)");

    static const uint32_t max_rate_limit_seconds =
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX *
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT;

    // Prevent overflow and enforce the maximum number of rate limit attempts
    // set by `NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX`
    if ((seconds_elapsed > UINT32_MAX - _this_core.rl_bucket) ||
        ((_this_core.rl_bucket + seconds_elapsed) >= max_rate_limit_seconds))
    {
        // enforce the max set by NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX
        _this_core.rl_bucket = max_rate_limit_seconds;
    }
    else
    {
        _this_core.rl_bucket += seconds_elapsed;
    }
}

bool nx_keycode_is_rate_limited(void)
{
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX == 0)
    {
        // Rate limiting is disabled.
        return false;
    }
#if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX > 0)
    return (_this_core.rl_bucket <
            NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT);
#endif
    NEXUS_ASSERT(0, "Should never reach here");
}

uint32_t nexus_keycode_rate_limit_attempts_remaining(void)
{
    uint32_t attempts_remaining = 0;

#if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX > 0)
    uint32_t seconds_remaining = _this_core.rl_bucket;
    while (seconds_remaining >=
           NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT)
    {
        seconds_remaining -=
            NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT;
        attempts_remaining++;
    }
#endif
    return attempts_remaining;
}

NEXUS_IMPL_STATIC void nexus_keycode_rate_limit_deduct_msg(void)
{
#if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX > 0)
    // Deduct one message from rate limiting bucket
    if (_this_core.rl_bucket >=
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT)
    {
        // Remove elapsed seconds from bucket
        _this_core.rl_bucket -=
            NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT;
    }
#endif
}

// ACSL for Frama-C analysis (https://frama-c.com/acsl.html)
/*@
    requires 0 <= cur_rl_bucket_seconds;
    assigns \nothing;

    behavior above_grace_period:
        assumes NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT *
   NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT
   <= cur_rl_bucket_seconds;
        ensures \result ==
   NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT;

    behavior below_grace_period:
        assumes NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT *
   NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT >
   cur_rl_bucket_seconds; ensures \result == cur_rl_bucket_seconds /
   NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT; ensures 0 <=
   \result <= NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT;

    complete behaviors;
    disjoint behaviors;
 */
NEXUS_IMPL_STATIC uint8_t nexus_keycode_mas_remaining_graceperiod_keycodes(
    const uint32_t cur_rl_bucket_seconds)
{
#if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX > 0)
    // Do not perform the divide operation unless necessary
    if (cur_rl_bucket_seconds >=
        (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT *
         NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT))
    {
        return NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT;
    }
    else
    {
        return (uint8_t) nexus_keycode_rate_limit_attempts_remaining();
    }
#else
    (void) cur_rl_bucket_seconds;
    return 0;
#endif
}

/*@
    requires 0 <= new_graceperiod_keycodes;

    behavior identical_keycode_count:
        assumes new_graceperiod_keycodes ==
   _this_core.stored.graceperiod_keycodes;
        assigns \nothing;
        ensures \result == false;
        ensures _this_core.stored.graceperiod_keycodes ==
   \old(_this_core.stored.graceperiod_keycodes);

    behavior updated_keycode_count:
        assumes new_graceperiod_keycodes !=
   _this_core.stored.graceperiod_keycodes;
        assigns _this_core.stored.graceperiod_keycodes;
        ensures _this_core.stored.graceperiod_keycodes ==
   new_graceperiod_keycodes;
        ensures \result == true;

    complete behaviors;
    disjoint behaviors;
 */
NEXUS_IMPL_STATIC bool nexus_keycode_mas_graceperiod_keycodes_update_nv(
    const uint8_t new_graceperiod_keycodes)
{
    // If we immediately know the value hasn't changed, return early.
    if (new_graceperiod_keycodes == _this_core.stored.graceperiod_keycodes)
    {
        return false;
    }
    _this_core.stored.graceperiod_keycodes = new_graceperiod_keycodes;
    nexus_nv_update(NX_NV_BLOCK_KEYCODE_MAS, (uint8_t*) &_this_core.stored);
    return true;
}

//
// MESSAGE ASSEMBLY CORE
//

void nexus_keycode_mas_init(const nexus_keycode_mas_message_handler handler)
{
    // Initialize grace period keycode counter
    _this_core.stored.graceperiod_keycodes =
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT;
    // Overwrite with value from NV, if available.
    (void) nexus_nv_read(NX_NV_BLOCK_KEYCODE_MAS,
                         (uint8_t*) &_this_core.stored);

    // Fill rate limiting bucket with grace keycodes upon power up
    _this_core.rl_bucket =
        _this_core.stored.graceperiod_keycodes *
        (uint32_t) NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT;
    _this_core.handler = handler;

    // reset frame counter variables
    nexus_keycode_mas_reset();
}

void nexus_keycode_mas_deinit(void)
{
    nexus_keycode_mas_reset();
}

uint32_t nexus_keycode_mas_process(const uint32_t seconds_elapsed)
{
    // Add time to rate limit
    nexus_keycode_rate_limit_add_time(seconds_elapsed);

    // Periodically update NV for grace period (don't do it in the interrupt)
    const uint8_t graceperiod_count =
        nexus_keycode_mas_remaining_graceperiod_keycodes(_this_core.rl_bucket);
    (void) nexus_keycode_mas_graceperiod_keycodes_update_nv(graceperiod_count);

    return nexus_keycode_mas_bookend_process();
}

void nexus_keycode_mas_reset(void)
{
    _this_core.partial.length = 0;
    _this_core.max_length_exceeded = false;
}

NEXUS_IMPL_STATIC void nexus_keycode_mas_push(const nx_keycode_key key)
{
    if (_this_core.partial.length < NEXUS_KEYCODE_MAX_MESSAGE_LENGTH)
    {
        _this_core.partial.keys[_this_core.partial.length++] = key;
    }
    else
    {
        _this_core.max_length_exceeded = true;
    }
}

NEXUS_IMPL_STATIC void nexus_keycode_mas_finish(void)
{
    if (_this_core.partial.length > 0 && !_this_core.max_length_exceeded)
    {
        (*_this_core.handler)(&_this_core.partial);
    }
    else
    {
        // message was either empty, too long, or product
        // is rate-limited; emit rejection feedback
        nxp_keycode_feedback_start(NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID);
    }

    // Deduct one message from rate limiting bucket
    // regardless of validity of the message
    nexus_keycode_rate_limit_deduct_msg();

    nexus_keycode_mas_reset();
}

//
// BOOKEND SCHEME
//

static struct
{
    nx_keycode_key start;
    nx_keycode_key end;
    uint8_t stop_length;
    bool start_seen;
    uint32_t latest_uptime;
} _this_bookend;

void nexus_keycode_mas_bookend_init(const nx_keycode_key start,
                                    const nx_keycode_key end,
                                    uint8_t stop_length)
{
    _this_bookend.start = start;
    _this_bookend.end = end;
    _this_bookend.stop_length = stop_length;

    nexus_keycode_mas_bookend_reset();
}

bool nexus_keycode_has_reached_stop_length(void)
{
    if (_this_bookend.stop_length == NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH)
    {
        return false;
    }
    return (_this_core.partial.length >= _this_bookend.stop_length);
}

void nexus_keycode_mas_bookend_reset()
{
    _this_bookend.start_seen = false;
}

// Used when processing a keycode key-by-key to implement timeout.
NEXUS_IMPL_STATIC uint32_t nexus_keycode_mas_bookend_process(void)
{
    // if we're receiving a message, did we time out?
    if (_this_bookend.start_seen)
    {
        // initialize the latest timestamp, if requested; this initialization
        // needs to happen in the main loop because we are otherwise unsure
        // that the uptime value reflects recent reality
        if (_this_bookend.latest_uptime == UINT32_MAX)
        {
            _this_bookend.latest_uptime = nexus_core_uptime();
        }

        // check for message-receipt timeout
        uint32_t elapsed = nexus_core_uptime() - _this_bookend.latest_uptime;

        if (elapsed > NEXUS_KEYCODE_PROTOCOL_ENTRY_TIMEOUT_SECONDS)
        {
            nexus_keycode_mas_bookend_reset();
            nexus_keycode_mas_reset();
        }
    }

    // if receiving a message, need frequent processing; otherwise don't care
    return _this_bookend.start_seen ?
               1 :
               NEXUS_CORE_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS;
}

NEXUS_IMPL_STATIC void nexus_keycode_mas_bookend_push(const nx_keycode_key key)
{
    // make sure we track elapsed time; not strictly necessary for rejected
    // keys, but should be harmless
    _this_bookend.latest_uptime = UINT32_MAX;
    nxp_core_request_processing();

    // process the actual keypress
    if (nx_keycode_is_rate_limited())
    {
        nxp_keycode_feedback_start(NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED);
    }
    else if (key == _this_bookend.start)
    {
        nxp_keycode_feedback_start(NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED);

        _this_bookend.start_seen = true;

        nexus_keycode_mas_reset();
    }
    else if (_this_bookend.start_seen)
    {
        if (key == _this_bookend.end)
        {

            nexus_keycode_mas_bookend_reset();
            nexus_keycode_mas_finish();
        }
        else
        {
            nexus_keycode_mas_push(key);

            if (nexus_keycode_has_reached_stop_length())
            {
                nexus_keycode_mas_bookend_reset();
                nexus_keycode_mas_finish();
            }
            else
            {
                // only display feedback pattern for the key in this case
                // (message already started) if the key is _not_ the end key
                // and if the key has not reached the stop length
                // (void)
                nxp_keycode_feedback_start(
                    NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED);
            }
        }
    }
    else
    {
        nxp_keycode_feedback_start(NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED);
    }
}

//
// INTERRUPTS
//

bool nx_keycode_handle_single_key(const nx_keycode_key key)
{
    if (!nexus_keycode_core_init_completed())
    {
        return false;
    }

    nexus_keycode_mas_bookend_push(key);
    return true;
}

bool nx_keycode_handle_complete_keycode(
    const struct nx_keycode_complete_code* keycode)
{
    if (!nexus_keycode_core_init_completed())
    {
        return false;
    }

    else if (nx_keycode_is_rate_limited())
    {
        nxp_keycode_feedback_start(NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID);
    }

    // Valid keycode must begin with a valid start key
    // If this one doesn't, return early.
    else if (keycode->keys[0] != _this_bookend.start)
    {
        nxp_keycode_feedback_start(NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID);
    }

    else
    {

        _this_bookend.start_seen = true;
        nexus_keycode_mas_reset();

// Small protocol does not have an 'end' character, length based
#if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_SMALL
        uint8_t last_keycode_char = keycode->length; // include last char
#else
        uint8_t last_keycode_char =
            (uint8_t)(keycode->length - 1); // omit last char
#endif

        // Start from one after the 'starting' character
        for (uint8_t i = 1; i < last_keycode_char; i++)
        {
            nexus_keycode_mas_push(keycode->keys[i]);
            // Process no further characters, too long for this protocol.
            if (nexus_keycode_has_reached_stop_length())
            {
                break;
            }
        }
        nexus_keycode_mas_bookend_reset();
        nexus_keycode_mas_finish();
    }
    return true;
}

#else
// provide empty stubs for interface if keycode is not enabled
bool nx_keycode_handle_single_key(const nx_keycode_key key)
{
    (void) key;
    return false;
}

bool nx_keycode_handle_complete_keycode(
    const struct nx_keycode_complete_code* keycode)
{
    (void) keycode;
    return false;
}
#endif /* if NEXUS_KEYCODE_ENABLED */

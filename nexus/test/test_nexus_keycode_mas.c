#include "src/nexus_core_internal.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
#include "src/nexus_nv.h"
#include "src/nexus_util.h"
#include "unity.h"
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

// Other support libraries
#include <mock_nexus_channel_core.h>
#include <mock_nxp_core.h>
#include <mock_nxp_keycode.h>
#include <string.h>

/********************************************************
 * DEFINITIONS
 *******************************************************/
/********************************************************
 * PRIVATE TYPES
 *******************************************************/

/********************************************************
 * PRIVATE DATA
 *******************************************************/
static struct
{
    struct nexus_keycode_frame frame;
    bool handled;
} _this;

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/
static void _test_handle_frame(const struct nexus_keycode_frame* frame)
{
    _this.frame = *frame;
    _this.handled = true;
}

// Helpers for testing and filling frames
void nexus_keycode_frame_fill(struct nexus_keycode_frame* frame,
                              const char* keys)
{
    frame->length = 0;

    for (uint32_t i = 0; keys[i] != '\0'; ++i)
    {
        NEXUS_ASSERT(i < NEXUS_KEYCODE_MAX_MESSAGE_LENGTH,
                     "too many keys for frame");

        frame->keys[i] = keys[i];
        ++frame->length;
    }
}

struct nexus_keycode_frame nexus_keycode_frame_filled(const char* keys)
{
    struct nexus_keycode_frame frame;

    nexus_keycode_frame_fill(&frame, keys);

    return frame;
}

// Assert that `key_chars` were handled and passed into the static frame
static void _assert_was_handled(const char* key_chars)
{
    TEST_ASSERT_TRUE(_this.handled);
    TEST_ASSERT_EQUAL_UINT(strlen(key_chars), _this.frame.length);

    for (uint16_t i = 0; key_chars[i] != '\0'; ++i)
    {
        TEST_ASSERT_EQUAL(_this.frame.keys[i], key_chars[i]);
    }
}

//
// MESSAGE ASSEMBLY TEST HELPERS
//
static void _push_key_sequence(const char* key_chars)
{
    for (uint16_t i = 0; key_chars[i] != '\0'; ++i)
    {
        nexus_keycode_mas_push(key_chars[i]);
        // nxp_keycode_feedback_start_ExpectAnyArgs();
    }

    nexus_keycode_mas_finish();
}

static void _each_mas_test_setup(void)
{
    nexus_keycode_mas_init(_test_handle_frame);

    _this.handled = false;
}

//
// BOOKEND-SCHEME TEST HELPERS
//

static void _mas_bookend_push_chars_check_feedback(
    const char* key_chars,
    const enum nxp_keycode_feedback_type scripts[32],
    bool prevent_rate_limit)
{
    for (uint16_t i = 0; key_chars[i] != '\0'; ++i)
    {
        // The order here matters - Cmock will confirm that
        // `port_request_processing` is called before feedback starts.
        nxp_core_request_processing_Expect();
        nxp_keycode_feedback_start_ExpectAndReturn(scripts[i], true);

        if (prevent_rate_limit)
        {
            nexus_keycode_rate_limit_add_time(
                NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT);
        }
        nexus_keycode_mas_bookend_push(key_chars[i]);
    }
}

static void _mas_bookend_push_chars_no_check_feedback(const char* key_chars)
{
    for (uint16_t i = 0; key_chars[i] != '\0'; ++i)
    {
        nxp_core_request_processing_Ignore();
        nxp_keycode_feedback_start_IgnoreAndReturn(true);

        nexus_keycode_rate_limit_add_time(
            NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT);
        nexus_keycode_mas_bookend_push(key_chars[i]);
    }
}

static void _bookend_test_init(uint8_t stop_length)
{
    _each_mas_test_setup();

    nexus_keycode_mas_bookend_init('*', '#', stop_length);
}

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    // Provide a 'dummy' handler and always note message as 'unhandled'
    // before each test
    // override the 'default' in nx_keycode_init with a custom handler
    nxp_core_nv_read_IgnoreAndReturn(true);
    nxp_core_nv_write_IgnoreAndReturn(true);
    nexus_keycode_mas_init(_test_handle_frame);
    nexus_channel_core_process_IgnoreAndReturn(0);
    _this.handled = false;
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
    nexus_keycode_mas_deinit();
}

void test_keycode_mas_rate_limiting_deduct_msg__rate_limiting_deducts_to_zero(
    void)
{
    // skip test if rate limiting is disabled
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX == 0)
    {
        return;
    }
    TEST_ASSERT_TRUE(nexus_keycode_mas_remaining_graceperiod_keycodes(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT *
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT));

    TEST_ASSERT_FALSE(nx_keycode_is_rate_limited());

    for (uint8_t i = 0;
         i <= NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT;
         i++)
    {
        nexus_keycode_rate_limit_deduct_msg();
    }
    TEST_ASSERT_TRUE(nx_keycode_is_rate_limited());
}

void test_keycode_mas_rate_limiting_add_time__rate_limiting_recovers_from_zero(
    void)
{
    // skip test if rate limiting is disabled
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX == 0)
    {
        return;
    }
    for (uint8_t i = 0;
         i <= NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT;
         i++)
    {
        nexus_keycode_rate_limit_deduct_msg();
    }
    TEST_ASSERT_TRUE(nx_keycode_is_rate_limited());

    nexus_keycode_rate_limit_add_time(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT);

    TEST_ASSERT_FALSE(nx_keycode_is_rate_limited());
}

void test_keycode_mas_rate_limiting__disabled_rate_limiting__not_rate_limited(
    void)
{
    // skip test if rate limiting is not disabled
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX != 0)
    {
        return;
    }
    TEST_ASSERT_FALSE(nx_keycode_is_rate_limited());

    for (uint8_t i = 0;
         i <= NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT;
         i++)
    {
        nexus_keycode_rate_limit_deduct_msg();
    }
    TEST_ASSERT_FALSE(nx_keycode_is_rate_limited());
}

void test_keycode_mas_rate_limiting__rate_limit_attempts_remaining__updates_correctly(
    void)
{
    // skip test if rate limiting is disabled
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX == 0)
    {
        return;
    }
    TEST_ASSERT_FALSE(nx_keycode_is_rate_limited());
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT,
        nexus_keycode_rate_limit_attempts_remaining());

    nexus_keycode_rate_limit_deduct_msg();
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT - 1,
        nexus_keycode_rate_limit_attempts_remaining());
    nexus_keycode_rate_limit_add_time(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT * 5);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT + 4,
        nexus_keycode_rate_limit_attempts_remaining());
}

void test_keycode_mas_rate_limiting__add_overflow__overflow_prevented(void)
{
    // skip test if rate limiting is disabled
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX == 0)
    {
        return;
    }
    TEST_ASSERT_FALSE(nx_keycode_is_rate_limited());
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT,
        nexus_keycode_rate_limit_attempts_remaining());

    nexus_keycode_rate_limit_add_time(0xFFFFFFFF);

    TEST_ASSERT_EQUAL_UINT(NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX,
                           nexus_keycode_rate_limit_attempts_remaining());
}

void test_keycode_mas_rate_limiting__add_large_not_overflow__set_to_max_seconds(
    void)
{
    // skip test if rate limiting is disabled
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX == 0)
    {
        return;
    }
    TEST_ASSERT_FALSE(nx_keycode_is_rate_limited());
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT,
        nexus_keycode_rate_limit_attempts_remaining());

    // Seconds in a month
    nexus_keycode_rate_limit_add_time(2592000);

    TEST_ASSERT_EQUAL_UINT(NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX,
                           nexus_keycode_rate_limit_attempts_remaining());
}

void test_keycode_mas_process__time_elapsed__rate_limiting_count_increments(
    void)
{
    // skip test if rate limiting is disabled
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX == 0)
    {
        return;
    }
    TEST_ASSERT_FALSE(nx_keycode_is_rate_limited());
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT,
        nexus_keycode_rate_limit_attempts_remaining());

    // Seconds in a month
    nexus_keycode_mas_process(2592000);

    TEST_ASSERT_EQUAL_UINT(NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX,
                           nexus_keycode_rate_limit_attempts_remaining());
}

void test_keycode_mas_process__grace_period_keycodes_below_max__updates_graceperiod_keycodes(
    void)
{
    // skip test if rate limiting is disabled
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX == 0)
    {
        return;
    }

    nexus_keycode_rate_limit_deduct_msg();

    nexus_keycode_mas_process(0);

    TEST_ASSERT_EQUAL_UINT(
        NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT - 1,
        nexus_keycode_rate_limit_attempts_remaining());
}

void test_keycode_mas_push__arbitrary_message_pushed__handler_call_correct_on_finish(
    void)
{
    const char* message_chars = "abcd";
    _push_key_sequence(message_chars);
    _assert_was_handled(message_chars);
}

void test_keycode_mas_push__sequences_long_then_valid__rejected_then_received(
    void)
{
    // Don't examine prod feedback calls in this test
    nxp_keycode_feedback_start_IgnoreAndReturn(true);
    // push a too-long message and verify its non-receipt
    const char* long_sequence = "123456789abcdefghio123456789abcdefghio12345678"
                                "9abcdefghio123456789abcdefghio";

    TEST_ASSERT_GREATER_THAN(
        NEXUS_KEYCODE_MAX_MESSAGE_LENGTH,
        strlen(long_sequence)); // ensure message is too long

    _push_key_sequence(long_sequence);

    // Cannot handle this message
    TEST_ASSERT_FALSE(_this.handled);

    // push a valid message and verify its receipt
    const char* valid_sequence = "abcd";

    _push_key_sequence(valid_sequence);
    _assert_was_handled(valid_sequence);
}

//
// BOOKEND-SCHEME TESTS
//

void test_keycode_mas_bookend_push__various_key_sequences__expected_end_states_reached(
    void)
{
    struct test_scenario
    {
        const char* input_chars;
        enum nxp_keycode_feedback_type expected_scripts[32];
        unsigned int expected_scripts_started;
        struct nexus_keycode_frame expected_frames[32];
        unsigned int expected_frame_count;
    };

    struct test_scenario scenarios[] = {
        // repeated start keys
        {"***",
         {
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED,
         },
         3,
         {nexus_keycode_frame_filled("")},
         0},
        // no start seen
        {"333",
         {
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
         },
         3,
         {nexus_keycode_frame_filled("")},
         0},
        // start-end-start
        {"*#*",
         {
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED,
             NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED,
         },
         3,
         {nexus_keycode_frame_filled("")},
         1}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // run the scenario
        const struct test_scenario scenario = scenarios[i];

        _bookend_test_init(NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH);

        _mas_bookend_push_chars_check_feedback(
            scenario.input_chars, scenario.expected_scripts, true);
    }
}

void test_keycode_mas_bookend_push__rate_limited__rejected_feedback(void)
{
    // skip test if rate limiting is disabled
    if (NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX == 0)
    {
        return;
    }
    struct test_scenario
    {
        const char* input_chars;
        enum nxp_keycode_feedback_type expected_scripts[32];
        unsigned int expected_scripts_started;
        struct nexus_keycode_frame expected_frames[32];
        unsigned int expected_frame_count;
    };

    struct test_scenario scenarios[] = {
        // repeated start keys
        {"***",
         {
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
         },
         3,
         {nexus_keycode_frame_filled("")},
         0},
        // no start seen
        {"333",
         {
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
         },
         3,
         {nexus_keycode_frame_filled("")},
         0},
        // start-end-start
        {"*#*",
         {
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
             NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED,
         },
         3,
         {nexus_keycode_frame_filled("")},
         1}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // run the scenario
        const struct test_scenario scenario = scenarios[i];

        _bookend_test_init(NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH);

        // after init, remove all tokens from bucket
        for (uint8_t j = 0;
             j <= NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT;
             j++)
        {
            nexus_keycode_rate_limit_deduct_msg();
        };
        TEST_ASSERT_TRUE(nx_keycode_is_rate_limited());
        _mas_bookend_push_chars_check_feedback(
            scenario.input_chars, scenario.expected_scripts, false);
    }
}

void test_keycode_mas_bookend_push__various_key_sequences__expected_messages_processed(
    void)
{
    struct test_scenario
    {
        const char* input_chars;
        const char* handled_message;
        const uint8_t stop_length;
    };

    struct test_scenario scenarios[] = {
        {"*45#", "45", NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH},
        {"*123", "123", 3},
    };
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // run the scenario
        const struct test_scenario scenario = scenarios[i];

        _bookend_test_init(scenario.stop_length);

        _mas_bookend_push_chars_no_check_feedback(scenario.input_chars);
        _assert_was_handled(scenario.handled_message);
    }
}

void test_keycode_mas_bookend_push__various_key_sequences_timeout__times_out(
    void)
{
    struct test_scenario
    {
        const char* input_chars;
        const char* handled_message;
        const uint8_t stop_length;
    };

    struct test_scenario scenarios[] = {
        {"*51#", "", NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH},
        {"*123", "", 3},
    };
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // run the scenario
        const struct test_scenario scenario = scenarios[i];
        _bookend_test_init(scenario.stop_length);

        uint32_t fake_system_uptime;
        for (uint16_t j = 0; scenario.input_chars[j] != '\0'; ++j)
        {
            nxp_core_request_processing_Ignore();
            nxp_keycode_feedback_start_IgnoreAndReturn(true);

            nexus_keycode_rate_limit_add_time(
                NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT);

            // push a single key, setting
            //  _this_bookend.latest_uptime = UINT32_MAX
            nexus_keycode_mas_bookend_push(scenario.input_chars[j]);

            // Call with 'current' uptime (no time elapsed since last call)
            // This will cause any internal calls to `nexus_core_uptime`
            // t will also set `_this_bookend.latest_uptime`
            // to the current uptime.
            // Note: In this test, core isn't actually initialized, so
            // uptime could be almost any value....
            nx_core_process(nexus_core_uptime() + 0);

            // simulate enough time elapsing between calls to exceed timeout
            fake_system_uptime = nexus_core_uptime() +
                                 NEXUS_KEYCODE_PROTOCOL_ENTRY_TIMEOUT_SECONDS +
                                 1;
            nx_core_process(fake_system_uptime);

            // Called after timeout elapses, next requested call to the
            // process function is at 'idle' value.
            uint32_t next_call_secs = nexus_keycode_mas_bookend_process();
            TEST_ASSERT_EQUAL_UINT(
                NEXUS_CORE_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS,
                next_call_secs);
        }
        TEST_ASSERT_FALSE(_this.handled);
    }
}

void test_keycode_mas_nx_keycode_handle_single_key__uninitialized_core__ignored(
    void)
{
    TEST_ASSERT_FALSE(nx_keycode_handle_single_key((const nx_keycode_key) '*'));
}

void test_keycode_mas_nx_keycode_handle_complete_keycode__uninitialized_core__ignored(
    void)
{
    struct nx_keycode_complete_code test_code = {.keys = "*123456789#",
                                                 .length = 11};
    TEST_ASSERT_FALSE(nx_keycode_handle_complete_keycode(&test_code));
}

void test_keycode_mas_nx_keycode_handle_single_key__initialized_core__start_key_processed(
    void)
{
    nxp_core_request_processing_Ignore();
    nexus_keycode_core_init();

    nexus_keycode_core_process(0); // complete internal init

    nxp_keycode_feedback_start_ExpectAndReturn(
        NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED, true);

    TEST_ASSERT_TRUE(nx_keycode_handle_single_key((const nx_keycode_key) '*'));
}

void test_keycode_mas_nx_keycode_handle_complete_keycode__initialized_core__keycode_processed(
    void)
{
    nxp_core_request_processing_Ignore();
    nexus_keycode_core_init();
    nexus_keycode_core_process(0); // complete internal init
    struct nx_keycode_complete_code test_code = {.keys = "*123456789#",
                                                 .length = 11};
    TEST_ASSERT_TRUE(nx_keycode_handle_complete_keycode(&test_code));
}

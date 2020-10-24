#include "src/nexus_common_internal.h"
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
#include <mock_nxp_common.h>
#include <mock_nxp_keycode.h>
#include <string.h>

// Eliminate warning: "warning: implicit declaration of function ‘resetTest’"
// function is provided by Cmock.
void resetTest(void);

/********************************************************
 * DEFINITIONS
 *******************************************************/

/********************************************************
 * PRIVATE TYPES
 *******************************************************/

/********************************************************
 * PRIVATE DATA
 *******************************************************/

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/
// convenience functions to fill a frame

struct nexus_keycode_frame nexus_keycode_frame_filled(const char* keys)
{
    struct nexus_keycode_frame frame = {0};

    for (uint32_t i = 0; keys[i] != '\0'; ++i)
    {
        NEXUS_ASSERT(i < NEXUS_KEYCODE_MAX_MESSAGE_LENGTH,
                     "too many keys for frame");
        frame.keys[i] = keys[i];
        ++frame.length;
    }
    return frame;
}

// Used to initialize protocol for testing the 'small' alphabet protocol
static void _small_fixture_reinit(const char start_char, const char* alphabet)
{
    const struct nexus_keycode_handling_config small_config = {
        nexus_keycode_pro_small_parse_and_apply,
        nexus_keycode_pro_small_init,
        NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH,
        start_char,
        '~', // no end char for small protocol, pick something arbitrary
        alphabet};

    _nexus_keycode_core_internal_init(&small_config);

    // most of these tests assume an all-zeros secret key
    // therefore, just mock the product returning that value
    // (any number of times). Also, pick an arbitrary 'fake' device ID to
    // use in tests that check the device ID for a 'match'.
    const uint32_t FAKE_DEVICE_ID = 0x1234567;
    nxp_keycode_get_secret_key_IgnoreAndReturn(
        NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);
    nxp_keycode_get_user_facing_id_IgnoreAndReturn(FAKE_DEVICE_ID);
}

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);
    _small_fixture_reinit('*', "0123");
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
    nexus_keycode_pro_deinit();
}

void test_nexus_keycode_pro_process__no_message_pending__idle_callback_returned(
    void)
{
    TEST_ASSERT_EQUAL_UINT(NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS,
                           nexus_keycode_pro_process());
}

void test_nexus_keycode_pro_process__various_messages_pending__messages_applied_feedback_started(
    void)
{
    struct test_scenario
    {
        bool reinit;
        const char* frame_body;
        enum nxp_keycode_feedback_type fb_type;
    };

    // interleaved, 'customer facing' small protocol keycodes
    // test a few 'valid' (already applied, duplicate message ID) as well
    const struct test_scenario scenarios[] = {
        {true, "1234567", NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID},
        {true,
         "30211130301021",
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED}, // 12
        {false,
         "30211130301021",
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_VALID}, // 12
        {false,
         "10210203303303",
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED}, // 13
        {false,
         "30123220313102",
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED}, // 14
        {false,
         "10210203303303",
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_VALID}, // 13
        {false,
         "33020121210023",
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED}, // WIPE_IDS_ALL
        {false,
         "30123220313102",
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED}, // 14
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const struct test_scenario scenario = scenarios[i];
        const struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(scenario.frame_body);

        if (scenario.reinit)
        {
            setUp();
        }

        // Enqueue will request processing
        nxp_common_request_processing_Expect();
        nexus_keycode_pro_enqueue(&frame);

        // Not testing credit interaction in this test
        nxp_common_payg_state_get_current_IgnoreAndReturn(
            NXP_COMMON_PAYG_STATE_ENABLED);
        nxp_keycode_payg_credit_add_IgnoreAndReturn(true);

        nxp_keycode_feedback_start_ExpectAndReturn(scenario.fb_type, true);
        nexus_keycode_pro_process();
    }
}

void test_nexus_keycode_pro_small_parse__valid_add_credit_messages__results_expected(
    void)
{
    struct test_scenario
    {
        const char* frame_body;
        uint8_t message_id;
        uint8_t type_code;
        uint8_t increment_id;
        uint16_t check;
        const char* alphabet;
    };

    const struct test_scenario scenarios[] = {
        {"32110323221113", 30, 0, 1, 0x0a57, "0123"}, // key "\xff" * 16
        {"02022022213121", 17, 0, 4, 0x09d9, "0123"}, // key "\x00" * 16
        {"13133133324232", 17, 0, 4, 0x09d9, "1234"}, // key "\x00" * 16
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // initialize the scenario
        const struct test_scenario scenario = scenarios[i];

        _small_fixture_reinit('*', scenario.alphabet);

        // execute it and verify outcome
        struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(scenario.frame_body);
        struct nexus_keycode_pro_small_message message;
        const bool parsed = nexus_keycode_pro_small_parse(&frame, &message);

        TEST_ASSERT_TRUE(parsed);
        TEST_ASSERT_EQUAL_UINT(message.full_message_id, scenario.message_id);
        TEST_ASSERT_EQUAL_UINT(message.type_code, scenario.type_code);
        TEST_ASSERT_EQUAL_UINT(message.body.activation.increment_id,
                               scenario.increment_id);
        TEST_ASSERT_EQUAL_UINT(message.check, scenario.check);
    }
}

void test_nexus_keycode_pro_small_parse__valid_maintenance_test_messages__results_expected(
    void)
{
    struct test_scenario
    {
        const char* frame_body;
        uint8_t message_id;
        enum nexus_keycode_pro_small_type_codes type_code;
        uint8_t function_id;
        uint16_t check;
        const char* alphabet;
    };

    const struct test_scenario scenarios[] = {
        {"32023320110033",
         0,
         NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE,
         // maintenance (0x80), "WIPE_IDS_ALL"
         NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_MASK | 0x80,
         0x050f, // key "\xfe" * 16
         "0123"},
        {"21031000211022",
         0,
         NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE,
         NEXUS_KEYCODE_PRO_SMALL_ENABLE_SHORT_TEST,
         0x094a, // key "\xff" * 16 (same with all test messages)
         "0123"}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // initialize the scenario
        const struct test_scenario scenario = scenarios[i];

        _small_fixture_reinit('*', scenario.alphabet);

        // execute it and verify outcome
        struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(scenario.frame_body);
        struct nexus_keycode_pro_small_message message;
        const bool parsed = nexus_keycode_pro_small_parse(&frame, &message);

        TEST_ASSERT_TRUE(parsed);
        TEST_ASSERT_EQUAL_UINT(message.full_message_id, scenario.message_id);
        TEST_ASSERT_EQUAL_UINT(message.body.maintenance_test.function_id,
                               scenario.function_id);
        TEST_ASSERT_EQUAL_UINT(message.check, scenario.check);
    }
}

void test_nexus_keycode_pro_small_parse__invalid_messages__parse_failures_graceful(
    void)
{
    struct test_scenario
    {
        const char* frame_body;
        const char* alphabet;
    };

    const struct test_scenario scenarios[] = {
        {"", "1234"}, // way too short
        {"2102132331130", "0123"}, // slightly too short
        {"021323311301231", "0123"}, // slightly too long
        {"11101122110022", "1357"}, // outside the alphabet
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // initialize the scenario
        const struct test_scenario scenario = scenarios[i];

        _small_fixture_reinit('*', scenario.alphabet);

        // execute it and verify outcome
        struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(scenario.frame_body);
        struct nexus_keycode_pro_small_message message;
        const bool parsed = nexus_keycode_pro_small_parse(&frame, &message);

        TEST_ASSERT_FALSE(parsed);
    }
}

void test_nexus_keycode_pro_small_apply__valid_non_duplicate__message_is_applied(
    void)
{
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(17), 0);
    struct nexus_keycode_pro_small_message message = {
        17, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{5}}, // increment id (6 days)
        0x03ab, // check
    };

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    // Add 6 days
    nxp_keycode_payg_credit_add_ExpectAndReturn(6 * 24 * 3600, true);

    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(17), 1);
}

void test_nexus_keycode_pro_small_apply__valid_large_inc_id__message_is_applied(
    void)
{
    struct nexus_keycode_pro_small_message message = {
        5, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{196}}, // increment id
        0x0cd8, // check
    };
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(231 * 24 * 3600, true);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(4), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(5), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(6), 0);
}

void test_nexus_keycode_pro_small_apply__valid_duplicate__message_not_applied(
    void)
{
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(17), 0);
    struct nexus_keycode_pro_small_message message = {
        17, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{5}}, // increment id (6 days)
        0x03ab, // check
    };

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(6 * 24 * 3600, true);
    enum nexus_keycode_pro_response response_one =
        nexus_keycode_pro_small_apply(&message);

    // Do not attempt to add credit on second entry of same code
    enum nexus_keycode_pro_response response_two =
        nexus_keycode_pro_small_apply(&message);

    TEST_ASSERT_EQUAL_UINT(response_one,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(response_two,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(17), 1);
}

void test_nexus_keycode_pro_small_apply__valid_unlock__unit_is_unlocked(void)
{
    struct nexus_keycode_pro_small_message message = {
        45, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{255}}, // increment id (unlock)
        0x0bd3, // check
    };
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_unlock_ExpectAndReturn(true);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(23), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(45), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 45);

    struct nexus_keycode_pro_small_message too_large_id = {
        28, // same LSB as '92' (0b011100)
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{255}},
        0xc4f, // check for '92'
    };

    response = nexus_keycode_pro_small_apply(&too_large_id);

    // 'invalid', not duplicate
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 45);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(23), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(28), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(45), 1);
}

void test_nexus_keycode_pro_small_apply__add_credit_after_unlocked__credit_not_applied(
    void)
{
    struct nexus_keycode_pro_small_message unlock_msg = {
        51, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{255}}, // increment id (unlock)
        0x0ebe, // check
    };
    struct nexus_keycode_pro_small_message add_credit_msg = {
        52, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{143}}, // increment id (143 days)
        0x09ae, // check
    };

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_unlock_ExpectAndReturn(true);
    enum nexus_keycode_pro_response response_a =
        nexus_keycode_pro_small_apply(&unlock_msg);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);
    enum nexus_keycode_pro_response response_b =
        nexus_keycode_pro_small_apply(&add_credit_msg);

    TEST_ASSERT_EQUAL_UINT(response_a,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(response_b,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);

    // future applications of both are shown as duplicate
    response_a = nexus_keycode_pro_small_apply(&unlock_msg);
    TEST_ASSERT_EQUAL_UINT(response_a,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);

    response_b = nexus_keycode_pro_small_apply(&add_credit_msg);
    TEST_ASSERT_EQUAL_UINT(response_b,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);
}

void test_nexus_keycode_pro_small_apply__set_credit_valid__credit_applied(void)
{
    struct test_scenario
    {
        struct nexus_keycode_pro_small_message set_msg;
        uint16_t expected_num_days;
    };

    const struct test_scenario scenarios[] = {
        {{
             1, // message id
             NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
             {{0}}, // increment id
             0x0d31, // check
         },
         1},
        {{
             2, // message id
             NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
             {{0}}, // increment id
             0x0927, // check
         },
         1},
        {{
             5, // message id
             NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
             {{90}}, // increment id
             0x09a4, // check
         },
         92},
        {{
             10, // message id
             NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
             {{167}}, // increment id
             0x0144, // check
         },
         312},
        {{
             21, // message id
             NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
             {{196}}, // increment id
             0x0fb1, // check
         },
         496},
        {{
             23, // message id
             NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
             {{239}}, // increment id
             0x0125, // check
         },
         960}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {

        // initialize the scenario
        const struct test_scenario scenario = scenarios[i];

        setUp();
        nexus_keycode_pro_wipe_message_ids_in_window();
        nexus_keycode_pro_reset_pd_index();

        nxp_keycode_payg_credit_set_ExpectAndReturn(
            scenario.expected_num_days * 24 * 3600, true);
        enum nexus_keycode_pro_response response =
            nexus_keycode_pro_small_apply(&scenario.set_msg);

        TEST_ASSERT_EQUAL_UINT(response,
                               NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

        for (uint8_t j = 0; j <= scenario.set_msg.full_message_id; ++j)
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(j), 1);
        }
    }
}

void test_nexus_keycode_pro_small_process__custom_command_reset_restricted_flag__flag_is_reset_feedback_ok(
    void)
{
    struct test_scenario
    {
        const char* frame_body;
        enum nxp_keycode_feedback_type fb_type;
        bool set_restricted_flag; // manually set flag before accepting keycode
        bool flag_state_before_keycode;
        bool flag_state_after_keycode;
    };

    // interleaved, 'customer facing' small protocol keycodes
    // mirrors similar test for full protocol, tests product feedback call
    const struct test_scenario scenarios[] = {
        {"03033330201032", // mid = 30
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
         false,
         false,
         false},
        {"03033330201032", // mid = 30
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_VALID,
         true,
         true,
         true},
        {"11001021103212", // mid = 31
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
         false,
         true,
         false},
        {"33020121210023", // WIPE_IDS_ALL
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
         true,
         true,
         true},
        {"03033330201032", // mid = 30
         NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
         false,
         true,
         false}};

    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);
    // confirm that initially, the flag is set to 0
    TEST_ASSERT_FALSE(
        nx_keycode_get_custom_flag(NX_KEYCODE_CUSTOM_FLAG_RESTRICTED));
    // Not testing credit interaction in this test
    nxp_common_payg_state_get_current_IgnoreAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_keycode_payg_credit_set_IgnoreAndReturn(true);

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const struct test_scenario scenario = scenarios[i];
        const struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(scenario.frame_body);

        if (scenario.set_restricted_flag)
        {
            nxp_keycode_notify_custom_flag_changed_Expect(
                NX_KEYCODE_CUSTOM_FLAG_RESTRICTED, true);
            nx_keycode_set_custom_flag(NX_KEYCODE_CUSTOM_FLAG_RESTRICTED);
        }

        TEST_ASSERT_EQUAL_UINT(
            nx_keycode_get_custom_flag(NX_KEYCODE_CUSTOM_FLAG_RESTRICTED),
            scenario.flag_state_before_keycode);

        // Enqueue will request processing
        nxp_common_request_processing_Expect();
        nexus_keycode_pro_enqueue(&frame);

        // manually skip checking the scenario where we apply a wipe
        // state/target flags 0 code
        if (scenario.fb_type == NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED &&
            i != 3)
        {
            nxp_keycode_notify_custom_flag_changed_Expect(
                NX_KEYCODE_CUSTOM_FLAG_RESTRICTED, false);
        }

        nxp_keycode_feedback_start_ExpectAndReturn(scenario.fb_type, true);
        nexus_keycode_pro_process();

        TEST_ASSERT_EQUAL_UINT(
            nx_keycode_get_custom_flag(NX_KEYCODE_CUSTOM_FLAG_RESTRICTED),
            scenario.flag_state_after_keycode);
    }
}

void test_nexus_keycode_pro_apply__update_pd__window_and_pd_ok(void)
{
    struct test_scenario
    {
        uint32_t cur_pd;
        uint8_t pd_inc;
        uint32_t expected_min_id_before;
        uint32_t expected_max_id_after;
    };
    const struct test_scenario scenarios[] = {
        {23, 1, 1, 64},
        {23, 40, 40, 103},
        {127, 5, 109, 172},
        {255, 20, 252, 315},
        {4294963200, 1, 4294963178, 4294963241},
        {4294963200, 40, 4294963217, 4294963280}};

    struct nx_common_check_key secret_key = nxp_keycode_get_secret_key();
    enum nexus_keycode_pro_response response;

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const struct test_scenario scenario = scenarios[i];
        uint8_t mask_id_index;

        setUp();
        nexus_keycode_pro_wipe_message_ids_in_window();
        nexus_keycode_pro_reset_pd_index();

        nexus_keycode_pro_update_window_and_message_mask_id(scenario.pd_inc,
                                                            &mask_id_index);

        struct nexus_keycode_pro_small_message min_msg = {
            scenario.expected_min_id_before, // message id
            NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
            {{1}}, // increment id 1, days = 2
            0x0000};

        min_msg.check =
            nexus_keycode_pro_small_compute_check(&min_msg, &secret_key);

        nxp_common_payg_state_get_current_ExpectAndReturn(
            NXP_COMMON_PAYG_STATE_ENABLED);
        nxp_keycode_payg_credit_add_ExpectAndReturn(2 * 24 * 3600, true);
        response = nexus_keycode_pro_small_apply(&min_msg);
        TEST_ASSERT_EQUAL_UINT(response,
                               NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

        response = nexus_keycode_pro_small_apply(&min_msg);
        TEST_ASSERT_EQUAL_UINT(response,
                               NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);

        struct nexus_keycode_pro_small_message max_msg = {
            scenario.expected_max_id_after, // message id
            NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
            {{1}}, // increment id 1, days = 2
            0x0000};

        max_msg.check =
            nexus_keycode_pro_small_compute_check(&max_msg, &secret_key);

        nxp_common_payg_state_get_current_ExpectAndReturn(
            NXP_COMMON_PAYG_STATE_ENABLED);
        nxp_keycode_payg_credit_add_ExpectAndReturn(2 * 24 * 3600, true);
        response = nexus_keycode_pro_small_apply(&max_msg);
        TEST_ASSERT_EQUAL_UINT(response,
                               NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

        response = nexus_keycode_pro_small_apply(&max_msg);
        TEST_ASSERT_EQUAL_UINT(response,
                               NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);
    }
}

void test_nexus_keycode_pro_small_parse__infer_full_message_id__infer_ok(void)
{
    struct test_scenario
    {
        uint8_t message_id; // compressed
        uint32_t pd_index;
        uint16_t expected_message_id; // expanded
    };

    const struct test_scenario scenarios[] = {// near initial state
                                              {0, 23, 0},
                                              {1, 23, 1},
                                              {2, 23, 2},
                                              {23, 23, 23},
                                              {24, 23, 24},
                                              {63, 23, 63},
                                              {0, 24, 64},
                                              {1, 24, 1},
                                              {2, 24, 2},
                                              {23, 24, 23},
                                              {24, 24, 24},
                                              {63, 24, 63},
                                              {0, 25, 64},
                                              {1, 25, 65},
                                              {2, 25, 2},
                                              {23, 25, 23},
                                              {24, 25, 24},
                                              {63, 25, 63},

                                              // after larger PD shifts
                                              {0, 8623, 8640},
                                              {1, 8623, 8641},
                                              {23, 8623, 8663},
                                              {24, 8623, 8600}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // initialize the scenario
        const struct test_scenario scenario = scenarios[i];

        struct nexus_keycode_pro_small_message dummy_msg = {
            scenario.message_id, // message id
            NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
            {{0}},
            0x0000,
        };

        setUp();
        nexus_keycode_pro_wipe_message_ids_in_window();
        nexus_keycode_pro_reset_pd_index();

        dummy_msg.full_message_id = nexus_keycode_pro_infer_full_message_id(
            scenario.message_id, scenario.pd_index, 23, 40);

        TEST_ASSERT_EQUAL_UINT(dummy_msg.full_message_id,
                               scenario.expected_message_id);
    }
}

void test_nexus_keycode_pro_small_apply__set_credit_valid__unlock_lock(void)
{
    struct nexus_keycode_pro_small_message set_msg_unlock = {
        10, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
        {{255}}, // increment id (unlock)
        0x0010, // check
    };
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(5), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(10), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(11), 0);

    nxp_keycode_payg_credit_unlock_ExpectAndReturn(true);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&set_msg_unlock);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(5), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(10), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(11), 0);

    struct nexus_keycode_pro_small_message set_msg_lock = {
        63, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
        {{254}}, // increment id (0 days; lock)
        0x0138, // check
    };

    nxp_keycode_payg_credit_set_ExpectAndReturn(0, true);
    response = nexus_keycode_pro_small_apply(&set_msg_lock);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // all these message IDs are outside of the mask.
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(5), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(10), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(11), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(23), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(24), 0);

    for (uint8_t i = 63 - 23; i <= 63; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               1);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(64), 0);

    // future ADD_CREDIT messages can be applied (as if this was a refurb. lock)

    struct nexus_keycode_pro_small_message add_msg = {
        78, // message id 15 higher (should lose 15 bits in the mask)
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{14}}, // increment id 15 days
        0x0ccb, // check
    };

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(15 * 24 * 3600, true);
    response = nexus_keycode_pro_small_apply(&add_msg);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // all IDs below window are shown as 'not set'.
    for (uint8_t i = 0; i < 78 - 23; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }

    // everything is still set up to 63; from before.
    for (uint8_t i = 78 - 23; i <= 63; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               1);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(77), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(78), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(79), 0);
}

void test_nexus_keycode_pro_small_apply__wrong_id_same_lsb__message_rejected(
    void)
{
    // note that Pd initializes to 23, window [0, 63]
    struct nexus_keycode_pro_small_message add_msg_a = {
        14, // same LSB as '78'
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{14}}, // increment id 15 days
        0x0ccb, // check for '78'
    };

    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&add_msg_a);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);

    // no pd change
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);

    for (uint8_t i = 0; i <= 78; i++)
    {
        // nothing is set.
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }

    struct nexus_keycode_pro_small_message add_msg_b = {
        55, // 23 below '78'
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{0}}, // increment id (1 day)
        0x0d34, // check
    };

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(1 * 24 * 3600, true);
    response = nexus_keycode_pro_small_apply(&add_msg_b);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // pd updated
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 55);

    for (uint8_t i = 0; i <= 54; i++)
    {
        // nothing is set.
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }

    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(55), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(56), 0);
    // outside mask
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(1024), 0);

    struct nexus_keycode_pro_small_message add_msg_c = {
        78, // same LSB as '14'
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{14}}, // increment id 15 days
        0x0ccb, // check for '78'
    };

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(15 * 24 * 3600, true);
    response = nexus_keycode_pro_small_apply(&add_msg_c);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    // pd updated
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 78);

    // ensure flag for message ID 55 was preserved.
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(54), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(55), 1);

    for (uint8_t i = 56; i <= 118; i++)
    {
        if (i == 78)
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(i), 1);
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(i), 0);
        }
    }

    // confirm additional applications make no difference
    response = nexus_keycode_pro_small_apply(&add_msg_a);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);

    response = nexus_keycode_pro_small_apply(&add_msg_b);

    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);

    response = nexus_keycode_pro_small_apply(&add_msg_c);

    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);

    // pd did not change
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 78);

    for (uint8_t i = 0; i <= 54; i++)
    {
        // nothing is set.
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }

    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(54), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(55), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(56), 0);

    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(77), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(78), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(79), 0);
}

void test_nexus_keycode_pro_small_apply__maintenance_message__wipe_message_ids(
    void)
{
    // WIPE_IDS_ALL
    struct nexus_keycode_pro_small_message message;
    message.full_message_id = 0;
    message.type_code = NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE;
    message.body.maintenance_test.function_id =
        NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_MASK | 0x80;
    message.check = 0x90b;

    // set message IDs before applying wipe message.
    nexus_keycode_pro_set_full_message_id_flag(23);
    nexus_keycode_pro_set_full_message_id_flag(4);
    nexus_keycode_pro_set_full_message_id_flag(0);

    // confirm state before applying wipe
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(23), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(4), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 1);

    // apply wipe message
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&message);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(23), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(4), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 0);
}

void test_nexus_keycode_pro_small_apply_unit_high_pd__maintenance_test_messages_ok(
    void)
{
    // WIPE_IDS only (not credit)
    struct nexus_keycode_pro_small_message wipe_msg = {
        0,
        NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE,
        {{(NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_MASK | 0x80)}},
        0x090b};

    struct nexus_keycode_pro_small_message test_msg = {
        0,
        NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE,
        {{NEXUS_KEYCODE_PRO_SMALL_ENABLE_SHORT_TEST}},
        0x94a};

    // apply a message with ID 63 to move PD up (so message ID 0 is not rcvd)
    // and confirm msg ID 0 for maintenance message is still rcvd.
    //
    //
    struct nexus_keycode_pro_small_message add_msg_63_id = {
        63,
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{1}}, // increment id (2 days)
        0x0566};

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(3600 * 24 * 2, true);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&add_msg_63_id);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    for (uint8_t i = 0; i < 63; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 63);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(63), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(64), 0);

    // apply a test message (should have no impact on message IDs or PD)
    // also, credit should not be affected (as we were already enabled)
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    response = nexus_keycode_pro_small_apply(&test_msg);
    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);

    for (uint8_t i = 0; i < 63; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 63);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(63), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(64), 0);

    response = nexus_keycode_pro_small_apply(&wipe_msg);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // window is reset; and no receipt flags are set.
    for (uint8_t i = 0; i <= 63; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);
}

void test_nexus_keycode_pro_small_apply__maintenance_message__wipe_ids_and_credit(
    void)
{
    // WIPE_STATE_1 (ids and PAYG Credit)
    struct nexus_keycode_pro_small_message message;
    message.full_message_id = 0;
    message.type_code = NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE;
    message.body.maintenance_test.function_id =
        NEXUS_KEYCODE_PRO_FULL_WIPE_STATE_TARGET_CREDIT_AND_MASK | 0x80;
    message.check = 0x289;

    // apply a message with ID 24 to move PD up (so message ID 0 is not rcvd)
    // and confirm message ID 0 is not rcvd.
    //
    //
    struct nexus_keycode_pro_small_message add_msg_24_id = {
        24,
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{1}}, // increment id (2 days)
        0x00e1};

    struct nexus_keycode_pro_small_message add_msg_0_id = {
        0,
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{1}}, // increment id (2 days)
        0x0dd9};

    // also set an intermediate ID valid in both windows.
    nexus_keycode_pro_set_full_message_id_flag(4);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(4), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(3600 * 24 * 2, true);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&add_msg_24_id);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 24);

    // after application, only ID 4 and 24 are set;
    for (uint8_t i = 0; i <= 64; i++)
    {
        if (i != 4 && i != 24)
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(i), 0);
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(i), 1);
        }
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 24);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_is_message_id_within_window(0), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_is_message_id_within_window(1), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_is_message_id_within_window(64),
                           1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_is_message_id_within_window(65),
                           0);

    // we are skipping parse/infer, so manually set this to what it would be..
    // (applying a now 'invalid' message as the check is wrong)
    add_msg_0_id.full_message_id = 64;

    // message ID below window is not received
    response = nexus_keycode_pro_small_apply(&add_msg_0_id);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);

    // same mask/pd state as before.
    for (uint8_t i = 0; i <= 64; i++)
    {
        if (i != 4 && i != 24)
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(i), 0);
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(i), 1);
        }
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 24);

    // apply wipe message
    nxp_keycode_payg_credit_set_ExpectAndReturn(0, true);
    response = nexus_keycode_pro_small_apply(&message);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // same mask/pd state as before.
    for (uint8_t i = 0; i <= 64; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_is_message_id_within_window(0), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_is_message_id_within_window(63),
                           1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_is_message_id_within_window(64),
                           0);

    add_msg_0_id.full_message_id = 0;

    // message ID 0 is received (Pd was reset)
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(3600 * 24 * 2, true);
    response = nexus_keycode_pro_small_apply(&add_msg_0_id);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    // same mask/pd state as before.
    for (uint8_t i = 1; i <= 63; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);

    // message ID 24 can  be applied after 0.
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(3600 * 24 * 2, true);
    response = nexus_keycode_pro_small_apply(&add_msg_24_id);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    for (uint8_t i = 0; i <= 63; i++)
    {
        // 0 is now outside of window, and will show as 'not set'.
        if (i != 24)
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(i), 0);
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(i), 1);
        }
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 24);
}

void test_nexus_keycode_pro_small_apply__maintenance_message__wipe_credit(void)
{
    // WIPE_STATE_0 (credit only, no message IDs)
    struct nexus_keycode_pro_small_message message;
    message.full_message_id = 0;
    message.type_code = NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE;
    // 0x80 = 0b10000000; maintenance flag = true
    message.body.maintenance_test.function_id =
        NEXUS_KEYCODE_PRO_FULL_WIPE_STATE_TARGET_CREDIT | 0x80;
    message.check = 0x63b;

    nexus_keycode_pro_set_full_message_id_flag(23);
    nexus_keycode_pro_set_full_message_id_flag(4);
    nexus_keycode_pro_set_full_message_id_flag(0);

    // confirm state before applying wipe.
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(23), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(4), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 1);

    nxp_keycode_payg_credit_set_ExpectAndReturn(0, true);

    // apply wipe message
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&message);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // message IDs remain set
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(23), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(4), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 1);
}

void test_nexus_keycode_pro_small_apply__test_message__short_test(void)
{
    struct nexus_keycode_pro_small_message message;
    message.full_message_id = 0;
    message.type_code = NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE;
    message.body.maintenance_test.function_id =
        NEXUS_KEYCODE_PRO_SMALL_ENABLE_SHORT_TEST;
    message.check = 0x94a;

    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 0);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(
        NEXUS_KEYCODE_PRO_UNIVERSAL_SHORT_TEST_SECONDS, true);
    nexus_keycode_pro_small_apply(&message);

    // 'short test' doesn't set a message ID
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 0);
}

void test_nexus_keycode_pro_small_apply__test_message__oqc_test_lifetime_limit(
    void)
{
    struct nexus_keycode_pro_small_message message;
    message.full_message_id = 0;
    message.type_code = NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE;
    message.body.maintenance_test.function_id =
        NEXUS_KEYCODE_PRO_SMALL_ENABLE_QC_TEST;
    message.check = 0xc22;
    enum nexus_keycode_pro_response response;

    for (uint8_t i = 1; i <= NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX;
         ++i)
    {
        nxp_common_payg_state_get_current_ExpectAndReturn(
            NXP_COMMON_PAYG_STATE_ENABLED);

        nxp_keycode_payg_credit_add_ExpectAndReturn(
            NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS, true);
        response = nexus_keycode_pro_small_apply(&message);

        TEST_ASSERT_EQUAL_UINT(response,
                               NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0),
                               0);
    }

    // '11'th application here, should fail
    response = nexus_keycode_pro_small_apply(&message);
    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);

    // wipe flags, should allow another test code to be applied
    struct nexus_keycode_pro_small_message wipe_message;
    wipe_message.full_message_id = 0;
    wipe_message.type_code = NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE;
    wipe_message.body.maintenance_test.function_id =
        NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_MASK | 0x80;
    wipe_message.check = 0x90b;

    // nxp_keycode_payg_credit_set_ExpectAndReturn(0, true);
    response = nexus_keycode_pro_small_apply(&wipe_message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // Disabled due to previous 'wipe'
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(
        NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS, true);
    response = nexus_keycode_pro_small_apply(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 0);
}

void test_nexus_keycode_pro_small_apply__test_message__oqc_test_no_relock(void)
{
    struct nexus_keycode_pro_small_message unlock_message = {
        45, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{255}}, // increment id (unlock)
        0x0bd3, // check
    };

    struct nexus_keycode_pro_small_message oqc_message;
    oqc_message.full_message_id = 0;
    oqc_message.type_code = NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE;
    oqc_message.body.maintenance_test.function_id =
        NEXUS_KEYCODE_PRO_SMALL_ENABLE_QC_TEST;
    oqc_message.check = 0xc22;

    enum nexus_keycode_pro_response response;

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);

    nxp_keycode_payg_credit_unlock_ExpectAndReturn(true);
    response = nexus_keycode_pro_small_apply(&unlock_message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // 'unlocked' will prevent QC from being applied.
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);

    response = nexus_keycode_pro_small_apply(&oqc_message);
    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);
}

void test_nexus_keycode_pro_small_apply__test_message__short_test_lifetime_limit_removed(
    void)
{
    struct nexus_keycode_pro_small_message message;
    message.full_message_id = 0;
    message.type_code = NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE;
    message.body.maintenance_test.function_id =
        NEXUS_KEYCODE_PRO_SMALL_ENABLE_SHORT_TEST;
    message.check = 0x094a;
    enum nexus_keycode_pro_response response;

    // The original life-time limit was 255. If the code can be entered 256
    // times,
    // this will prove that the limit was successfully removed.
    for (uint16_t i = 1; i <= 256; ++i)
    {
        // Explicitly re-enable since we call resetTest after each iteration
        nxp_common_nv_read_IgnoreAndReturn(true);
        nxp_common_nv_write_IgnoreAndReturn(true);

        // must be disabled to apply QC test message
        nxp_common_payg_state_get_current_ExpectAndReturn(
            NXP_COMMON_PAYG_STATE_DISABLED);
        nxp_keycode_payg_credit_add_ExpectAndReturn(
            NEXUS_KEYCODE_PRO_UNIVERSAL_SHORT_TEST_SECONDS, true);
        response = nexus_keycode_pro_small_apply(&message);

        TEST_ASSERT_EQUAL_UINT(response,
                               NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0),
                               0);
        resetTest();
    }
}

void test_nexus_keycode_pro_small_apply__test_message_and_add_credit__near_cutoff_time_correct(
    void)
{
    struct nexus_keycode_pro_small_message test_msg;
    test_msg.full_message_id = 0;
    test_msg.type_code = NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE;
    test_msg.body.maintenance_test.function_id =
        NEXUS_KEYCODE_PRO_SMALL_ENABLE_SHORT_TEST;
    test_msg.check = 0x94a;

    // test message will only add credit if unit is currently 'disabled'
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);

    // test message causes unit to enter ENABLED state with SHORT_TEST credit
    nxp_keycode_payg_credit_add_ExpectAndReturn(
        NEXUS_KEYCODE_PRO_UNIVERSAL_SHORT_TEST_SECONDS, true);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&test_msg);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    struct nexus_keycode_pro_small_message credit_msg = {
        2, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{0}}, // increment id (1 day)
        0x0467, // check
    };

    // enabled by the previous 'short test' code
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(24 * 60 * 60, true);

    response = nexus_keycode_pro_small_apply(&credit_msg);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
}

void test_nexus_keycode_pro_small_apply__wrong_check_field__message_not_applied(
    void)
{
    struct nexus_keycode_pro_small_message message = {
        17, // message id
        NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE,
        {{5}}, // increment id (6 days)
        0x03dd, // check (invalid check field)
    };
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_small_apply(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);
}

void test_nexus_keycode_pro_small_compute_check__fixed_inputs__outputs_are_expected(
    void)
{
    const struct nexus_keycode_pro_small_message input_messages[] = {
        {0, 0, {{0}}, 0x00},
        {0, 0, {{0}}, 0x00},
        {5, 0, {{17}}, 0x00},
        {15, 0, {{120}}, 0x00},
    };
    const struct nx_common_check_key input_keys[] = {
        {{0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00}},
        {{0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1}},
        {{0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33,
          0x33}},
        {{0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1}},
    };
    const uint16_t expected_checks[] = {0x056b, 0x0277, 0x09b6, 0x0539};

    for (uint8_t i = 0; i < sizeof(input_messages) / sizeof(input_messages[0]);
         ++i)
    {
        const uint16_t check = nexus_keycode_pro_small_compute_check(
            &input_messages[i], &input_keys[i]);

        TEST_ASSERT_EQUAL_UINT(expected_checks[i], check);
    }
}

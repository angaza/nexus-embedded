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
static void _full_fixture_reinit(const char start_char,
                                 const char end_char,
                                 const char* alphabet,
                                 const struct nx_core_check_key device_key)
{
    const struct nexus_keycode_handling_config full_config = {
        nexus_keycode_pro_full_parse_and_apply,
        nexus_keycode_pro_full_init,
        NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH,
        start_char,
        end_char,
        alphabet};

    _nexus_keycode_core_internal_init(&full_config);

    // most of these tests assume an all-zeros secret key
    // therefore, just mock the product returning that value

    nxp_keycode_get_secret_key_IgnoreAndReturn(device_key);
}

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    nxp_core_nv_read_IgnoreAndReturn(true);
    nxp_core_nv_write_IgnoreAndReturn(true);
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
    nexus_keycode_pro_deinit();
}

void test_nexus_keycode_pro_full_parse__check_field_parse__check_field_parsed_ok(
    void)
{
    struct parseable_inputs
    {
        const char* input_characters;
        uint32_t check_expected;
    };

    const struct parseable_inputs test_inputs[] = {
        {"1234567890", 567890},
        {"000000", 0},
        {"112233", 112233},
        {"912", 0}, // '0' returned in the case of a too-short failed frame.
        {"000912", 912},
        {"7999999", 999999},
        {"1235649000049", 49}};

    for (uint8_t i = 0; i < sizeof(test_inputs) / sizeof(test_inputs[0]); ++i)
    {
        // parse the scenario input
        const struct parseable_inputs input = test_inputs[i];
        struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(input.input_characters);

        const uint32_t check_result =
            nexus_keycode_pro_full_check_field_from_frame(&frame);

        TEST_ASSERT_EQUAL_UINT(input.check_expected, check_result);
    }
}

void test_nexus_keycode_pro_full_parse__various_messages__parsed_type_code_correct(
    void)
{
    // prepare associated test scenarios
    struct test_scenario
    {
        const char* interleaved;
        enum nexus_keycode_pro_full_message_type_codes type_code;
        bool is_valid;
    };

    // all generated using nexus keycodev1 implementation
    const struct test_scenario scenarios[] = {
        {"96264378143903", NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT, true},
        {"67777637770920", NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT, true},
        {"61339720531363", NEXUS_KEYCODE_PRO_FULL_ACTIVATION_WIPE_STATE, true},
        {"61225288652186", NEXUS_KEYCODE_PRO_FULL_ACTIVATION_WIPE_STATE, true},
        {"40724795036413", NEXUS_KEYCODE_PRO_FULL_ACTIVATION_DEMO_CODE, true},
        {"4064983", NEXUS_KEYCODE_PRO_FULL_FACTORY_ALLOW_TEST, true},
        // last is unlock, message ID 80 (SET CREDIT, hours = 99999)
        {"96476769603431", NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT, true},
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // Initialize with 'bad' values to ensure they are overwritten
        struct nexus_keycode_pro_full_message parsed;
        memset(&parsed, 0x42, sizeof(parsed));

        // parse the scenario input
        const struct test_scenario scenario = scenarios[i];

        struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(scenario.interleaved);
        const bool success = nexus_keycode_pro_full_parse(&frame, &parsed);

        TEST_ASSERT_EQUAL_UINT(scenario.is_valid, success);
        TEST_ASSERT_EQUAL_UINT(scenario.type_code, parsed.type_code);
    }
}

void test_nexus_keycode_pro_full_parse_activation__various_frames__parsed_messages_match_expected(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);

    // prepare some example messages
    struct nexus_keycode_pro_full_message message_a;

    message_a.type_code = NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT;
    message_a.full_message_id = 63;
    message_a.body.add_set_credit.hours = 42;
    message_a.check = 550801;

    struct nexus_keycode_pro_full_message message_b;

    message_b.type_code = NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT;
    message_b.full_message_id = 0;
    message_b.body.add_set_credit.hours = 5012;
    message_b.check = 134571;

    struct nexus_keycode_pro_full_message message_c;

    message_c.type_code = NEXUS_KEYCODE_PRO_FULL_ACTIVATION_WIPE_STATE;
    message_c.full_message_id = 45;
    message_c.body.wipe_state.target =
        NEXUS_KEYCODE_PRO_FULL_WIPE_STATE_TARGET_UART_READLOCK;
    message_c.check = 802585;

    // prepare associated test scenarios
    struct test_scenario
    {
        const char* interleaved;
        struct nexus_keycode_pro_full_message expected;
    };

    const struct test_scenario scenarios[] = {{"97024027550801", message_a},
                                              {"67015827134571", message_b},
                                              {"92312722802585", message_c}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct nexus_keycode_pro_full_message parsed;

        // Fill with 'bad' values to ensure parsing clears the message
        memset(&parsed, 0x42, sizeof(parsed));

        // parse the scenario input;
        const struct test_scenario scenario = scenarios[i];
        struct nexus_keycode_frame input =
            nexus_keycode_frame_filled(scenario.interleaved);
        const bool success =
            nexus_keycode_pro_full_parse_activation(&input, &parsed);

        TEST_ASSERT(success);

        // verify the result
        const struct nexus_keycode_pro_full_message expected =
            scenario.expected;

        TEST_ASSERT_EQUAL_UINT(expected.type_code, parsed.type_code);
        TEST_ASSERT_EQUAL_UINT(expected.check, parsed.check);
        TEST_ASSERT_EQUAL_UINT(expected.full_message_id,
                               parsed.full_message_id);

        switch (parsed.type_code)
        {
            case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT:
                TEST_ASSERT_EQUAL_UINT(parsed.body.add_set_credit.hours,
                                       expected.body.add_set_credit.hours);

                break;

            case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT:
                TEST_ASSERT_EQUAL_UINT(parsed.body.add_set_credit.hours,
                                       expected.body.add_set_credit.hours);

                break;

            case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_WIPE_STATE:
                TEST_ASSERT_EQUAL_UINT(parsed.body.wipe_state.target,
                                       expected.body.wipe_state.target);
                break;

            default:
                TEST_ASSERT(false);

                break;
        }
    }
}

void test_nexus_keycode_pro_full_apply__various_invalid_inputs__invalid_returned(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);
    // build several test scenarios
    struct test_scenario
    {
        const char* normalized;
        enum nexus_keycode_pro_full_message_type_codes type_code;
        uint32_t check;
    };

    const struct test_scenario scenarios[] = {
        {"4064981", NEXUS_KEYCODE_PRO_FULL_FACTORY_ALLOW_TEST, 64981},
        {"80294339379322", // id=45, hours=24, invalid ('\xff' secret key)
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT,
         379322},
        {"77273638195162", // id=16, hours=168, invalid ('\xff' secret key)
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         195162},
        {"13777794160692", // id=16, hours=168, invalid ('\x00' secret key)
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         160692},
        // invalid factory message
        {"4064984", NEXUS_KEYCODE_PRO_FULL_FACTORY_ALLOW_TEST, 64984}};

    // use a fixed non-default secret key
    const struct nx_core_check_key key_mixed = {{0x12,
                                                 0xff,
                                                 0x00,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xcd,
                                                 0xff,
                                                 0xab}};

    // run through each scenario
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // construct the scenario inputs
        const struct test_scenario scenario = scenarios[i];
        struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(scenario.normalized);
        struct nexus_keycode_pro_full_message message = {0};

        bool parsed = nexus_keycode_pro_full_parse(&frame, &message);

        TEST_ASSERT_EQUAL_UINT(parsed, true);

        // ensure check digits were read correctly
        TEST_ASSERT_EQUAL_UINT(scenario.check, message.check);

        // Override the 'default' secret key for this test
        nxp_keycode_get_secret_key_IgnoreAndReturn(key_mixed);

        // apply the message and verify that it is rejected
        const enum nexus_keycode_pro_response response =
            nexus_keycode_pro_full_apply(&message);

        TEST_ASSERT_EQUAL_UINT(NEXUS_KEYCODE_PRO_RESPONSE_INVALID, response);
    }
}

void test_nexus_keycode_pro_full_apply__various_valid_inputs__expected_responses_returned(
    void)
{
    // build several test scenarios
    struct test_scenario
    {
        const char* interleaved;
        enum nexus_keycode_pro_response expected_response;
        bool expect_payg_state;
        enum nxp_core_payg_state payg_state_before;
    };

    const struct test_scenario scenarios[] = {
        // universal short test
        {"4064983",
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED,
         true,
         NXP_CORE_PAYG_STATE_DISABLED},
        // add, id = 16, hours=168
        {"13777794160692",
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED,
         true,
         NXP_CORE_PAYG_STATE_ENABLED},
        // set, id = 63, hours=168
        {"63530515961148",
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED,
         false,
         NXP_CORE_PAYG_STATE_ENABLED}, // payg state not examined
        // same add as above first valid msg (now below window)
        {"13777794160692",
         NEXUS_KEYCODE_PRO_RESPONSE_INVALID,
         false,
         NXP_CORE_PAYG_STATE_ENABLED},
        // same set as above
        {"63530515961148",
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE,
         false,
         NXP_CORE_PAYG_STATE_ENABLED}, // payg state not examined
        // factory allow test (duplicate, since we aren't disabled)
        {"4064983",
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE,
         true,
         NXP_CORE_PAYG_STATE_ENABLED},
        // valid demo code, but generated for different key, so invalid
        {"33579266365784",
         NEXUS_KEYCODE_PRO_RESPONSE_INVALID,
         false,
         NXP_CORE_PAYG_STATE_ENABLED}, // payg state not examined
    };

    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);

    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);

    // not testing set or add credit here, ignore for this test
    nxp_keycode_payg_credit_set_IgnoreAndReturn(true);
    nxp_keycode_payg_credit_add_IgnoreAndReturn(true);

    // run through each scenario
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // construct the scenario inputs
        const struct test_scenario scenario = scenarios[i];
        struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(scenario.interleaved);
        struct nexus_keycode_pro_full_message message = {0};
        const bool parsed = nexus_keycode_pro_full_parse(&frame, &message);

        TEST_ASSERT(parsed);
        if (scenario.expect_payg_state)
        {
            nxp_core_payg_state_get_current_ExpectAndReturn(
                scenario.payg_state_before);
        }

        // apply the message and verify its response
        const enum nexus_keycode_pro_response response =
            nexus_keycode_pro_full_apply(&message);

        TEST_ASSERT_EQUAL_UINT(response, scenario.expected_response);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 63);
}

void test_nexus_keycode_pro_full_apply__messages_shift_window__application_correct(
    void)
{
    // build several test scenarios
    struct test_scenario
    {
        const char* interleaved;
        const enum nexus_keycode_pro_full_message_type_codes expected_type_code;
        const uint32_t expected_full_message_id;
        // 0xFFFFFFFF is 'invalid/skip' for this test
        const uint32_t expect_add_credit_amount;
        const uint32_t expect_set_credit_amount;
        const bool expect_unlock;
        enum nexus_keycode_pro_response expected_response;
    };

    const struct test_scenario scenarios[] = {
        // add ID 0; 1 day (applied)
        {"17512175671270",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         0,
         1 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // add ID 18; 1 day (applied)
        {"54351282878335",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         18,
         1 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Add ID 18; 1 day (duplicate)
        {"54351282878335",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         18,
         0xFFFFFFFF,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE},
        // *SET* ID 17; 1 day (applied)
        {"16661656430865",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT,
         17,
         0xFFFFFFFF,
         1 * 86400,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // add ID 16; 1 day (duplicate, SET sets all IDs below its own)
        {"18741480856587",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         16,
         0xFFFFFFFF,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE},
        // add ID 63; 1 day (applied)
        {"37812659533400",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         63,
         1 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Add ID 85; 1 day (applied)
        {"58409523890468",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         85,
         1 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Add ID 125; 1 day (applied)
        {"84961300121900",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         125,
         1 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Add ID 165; 1 day (applied)
        {"90216400698647",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         165,
         1 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Add ID 205; 1 day (applied)
        {"27843005971327",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         205,
         1 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Add ID 205; 1 day (duplicate)
        {"27843005971327",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         205,
         0xFFFFFFFF,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE},
        // Add ID 245; 1 week (applied)
        {"23815985837906",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         245,
         7 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Add ID 245; 1 week (duplicate)
        {"23815985837906",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         245,
         0xFFFFFFFF,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE},
        // Add ID 285; 1 month/30 days (applied)
        {"74837625389313",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         285,
         30 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Add ID 285; 1 month (duplicate)
        {"74837625389313",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         285,
         0xFFFFFFFF,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE},
        // Set ID 275; 1 day (applied)
        {"80226322507031",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT,
         275,
         0xFFFFFFFF,
         1 * 86400,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Add ID 274; 1 day (duplicate)
        {"74745234263745",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         274,
         0xFFFFFFFF,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE},
        // Set ID 300; 1 year (applied)
        {"97120210121779",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT,
         300,
         0xFFFFFFFF,
         365 * 86400,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Set ID 275; 1 day (Invalid, outside window)
        {"80226322507031",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT,
         339, // it will be inferred as 339; but is actually 275.D
         0xFFFFFFFF,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_INVALID},
        // Add ID 325; 1 day (applied)
        {"16008638417832",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT,
         325,
         1 * 86400,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED},
        // Set ID 400; 1 day (Invalid, too high)
        {"57297667770280",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT,
         400,
         0xFFFFFFFF,
         0xFFFFFFFF,
         false,
         NEXUS_KEYCODE_PRO_RESPONSE_INVALID},
        // unlock (SET CREDIT ID = 350, hours = 99999)
        {"21096794406802",
         NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT,
         350,
         0xFFFFFFFF,
         0xFFFFFFFF,
         true,
         NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED}};

    // use a fixed non-default secret key
    const struct nx_core_check_key key_mixed = {{0x12,
                                                 0xff,
                                                 0x00,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xcd,
                                                 0xff,
                                                 0xab}};

    _full_fixture_reinit('*', '#', "0123456789", key_mixed);
    // Confirm mock has changed to mixed key
    struct nx_core_check_key test_key = nxp_keycode_get_secret_key();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        &test_key.bytes, &key_mixed.bytes, sizeof(struct nx_core_check_key));

    // run through each scenario
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // construct the scenario inputs
        const struct test_scenario scenario = scenarios[i];
        struct nexus_keycode_frame frame =
            nexus_keycode_frame_filled(scenario.interleaved);
        struct nexus_keycode_pro_full_message message = {0};
        const bool parsed = nexus_keycode_pro_full_parse(&frame, &message);

        TEST_ASSERT(parsed);

        // 0xFFFFFFFF is just used as a sentinel in these test scenarios.
        if (scenario.expect_add_credit_amount != 0xFFFFFFFF)
        {
            // Need to return some state, doesn't matter for this test (except
            // that it must not be 'unlocked')
            nxp_core_payg_state_get_current_ExpectAndReturn(
                NXP_CORE_PAYG_STATE_ENABLED);
            nxp_keycode_payg_credit_add_ExpectAndReturn(
                scenario.expect_add_credit_amount, true);
        }
        else if (scenario.expect_set_credit_amount != 0xFFFFFFFF)
        {
            nxp_keycode_payg_credit_set_ExpectAndReturn(
                scenario.expect_set_credit_amount, true);
        }
        else if (scenario.expect_unlock)
        {
            nxp_keycode_payg_credit_unlock_ExpectAndReturn(true);
        }

        // apply the message and verify its response
        const enum nexus_keycode_pro_response response =
            nexus_keycode_pro_full_apply(&message);

        TEST_ASSERT_EQUAL_UINT(response, scenario.expected_response);
        TEST_ASSERT_EQUAL_UINT(message.type_code, scenario.expected_type_code);

        // only check if it was valid; otherwise these are invalid.
        if (response != NEXUS_KEYCODE_PRO_RESPONSE_INVALID)
        {
            TEST_ASSERT_EQUAL_UINT(message.full_message_id,
                                   scenario.expected_full_message_id);
        }
    }
}

void nexus_keycode_pro_full_apply_factory__test_message__short_test_no_lifetime_limit(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);
    const char* short_test = "4064983";
    struct nexus_keycode_frame short_test_frame =
        nexus_keycode_frame_filled(short_test);

    struct nexus_keycode_pro_full_message short_test_message;
    const bool short_test_parsed =
        nexus_keycode_pro_full_parse(&short_test_frame, &short_test_message);

    TEST_ASSERT(short_test_parsed);

    enum nexus_keycode_pro_response response;

    // Ensure the SHORT_TEST max entry is effectively unlimited
    for (uint32_t i = 0; i <= UINT16_MAX + 1; ++i)
    {
        // must be disabled to apply short test message
        // product reports 'disabled' on each loop through this test
        nxp_core_payg_state_get_current_ExpectAndReturn(
            NXP_CORE_PAYG_STATE_DISABLED);
        nxp_keycode_payg_credit_add_ExpectAndReturn(
            NEXUS_KEYCODE_PRO_UNIVERSAL_SHORT_TEST_SECONDS, true);

        response = nexus_keycode_pro_full_apply_factory(&short_test_message);

        TEST_ASSERT_EQUAL_UINT(response,
                               NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    }
}

void test_nexus_keycode_pro_full_apply_factory__qc_test_message__adds_ok(void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);
    const char* qc_test = "500060694509"; // "Long" QC factory code
    struct nexus_keycode_frame qc_test_frame =
        nexus_keycode_frame_filled(qc_test);

    struct nexus_keycode_pro_full_message qc_test_message;
    const bool qc_test_parsed =
        nexus_keycode_pro_full_parse(&qc_test_frame, &qc_test_message);

    TEST_ASSERT(qc_test_parsed);

    enum nexus_keycode_pro_response response;

    for (uint16_t i = 1; i <= NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX;
         ++i)
    {
        // confirm that credit may add in both enabled/disabled states
        if (i % 2 == 0)
        {
            nxp_core_payg_state_get_current_ExpectAndReturn(
                NXP_CORE_PAYG_STATE_ENABLED);
        }
        else
        {
            nxp_core_payg_state_get_current_ExpectAndReturn(
                NXP_CORE_PAYG_STATE_DISABLED);
        }
        nxp_keycode_payg_credit_add_ExpectAndReturn(
            NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS, true);
        response = nexus_keycode_pro_full_apply_factory(&qc_test_message);

        TEST_ASSERT_EQUAL_UINT(response,
                               NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
        // no message ID flag is set
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0),
                               0);
    }

    // 11th application here, should fail
    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_ENABLED);
    response = nexus_keycode_pro_full_apply_factory(&qc_test_message);
    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);

    // wipe message IDs, should be able to re-apply test code
    const char* wipe_ids = "65765292553198";
    struct nexus_keycode_frame wipe_ids_frame =
        nexus_keycode_frame_filled(wipe_ids);
    struct nexus_keycode_pro_full_message wipe_ids_message;
    nexus_keycode_pro_full_parse(&wipe_ids_frame, &wipe_ids_message);

    response = nexus_keycode_pro_full_apply_activation(&wipe_ids_message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_ENABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(
        NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS, true);

    response = nexus_keycode_pro_full_apply_factory(&qc_test_message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 0);
}

void test_nexus_keycode_pro_full_apply_factory__10_minute_oqc__doesnt_stack(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);
    const char* qc_test = "500010494931"; // 10 minutes
    struct nexus_keycode_frame qc_test_frame =
        nexus_keycode_frame_filled(qc_test);

    struct nexus_keycode_pro_full_message qc_test_message;
    const bool qc_test_parsed =
        nexus_keycode_pro_full_parse(&qc_test_frame, &qc_test_message);

    TEST_ASSERT(qc_test_parsed);

    enum nexus_keycode_pro_response response;

    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(
        NEXUS_KEYCODE_PRO_QC_SHORT_TEST_MESSAGE_SECONDS, true);
    response = nexus_keycode_pro_full_apply_factory(&qc_test_message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_ENABLED);
    // apply again
    response = nexus_keycode_pro_full_apply_factory(&qc_test_message);

    // No credit change, was already enabled
    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);
}

void test_nexus_keycode_pro_full_apply_factory__qc_test_message__no_relock(void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);
    // 510494931 for 10 minute code
    const char* qc_test = "500060694509"; // 1 hour
    struct nexus_keycode_frame qc_test_frame =
        nexus_keycode_frame_filled(qc_test);

    struct nexus_keycode_pro_full_message qc_test_message;
    const bool qc_test_parsed =
        nexus_keycode_pro_full_parse(&qc_test_frame, &qc_test_message);

    TEST_ASSERT(qc_test_parsed);

    enum nexus_keycode_pro_response response;

    // unit is 'unlocked' prior to entering this code
    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_UNLOCKED);
    response = nexus_keycode_pro_full_apply_factory(&qc_test_message);

    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);
}

void test_nexus_keycode_pro_full_apply_factory__can_unit_accept_qc_code__returns_correctly(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);
    // Can't perform short QC when unlocked
    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_UNLOCKED);
    TEST_ASSERT_FALSE(nexus_keycode_pro_can_unit_accept_qc_code(
        NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS));

    // Can't perform short QC when unlocked
    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_UNLOCKED);
    TEST_ASSERT_FALSE(nexus_keycode_pro_can_unit_accept_qc_code(
        NEXUS_KEYCODE_PRO_QC_SHORT_TEST_MESSAGE_SECONDS));

    for (int i = 0; i < NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX; ++i)
    {
        nxp_core_payg_state_get_current_ExpectAndReturn(
            NXP_CORE_PAYG_STATE_DISABLED);
        TEST_ASSERT_TRUE(nexus_keycode_pro_can_unit_accept_qc_code(
            NEXUS_KEYCODE_PRO_QC_SHORT_TEST_MESSAGE_SECONDS));
        nexus_keycode_pro_increment_short_qc_test_message_count();
    }

    for (int i = 0; i < NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX; ++i)
    {
        nxp_core_payg_state_get_current_ExpectAndReturn(
            NXP_CORE_PAYG_STATE_DISABLED);
        TEST_ASSERT_TRUE(nexus_keycode_pro_can_unit_accept_qc_code(
            NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS));
        nexus_keycode_pro_increment_long_qc_test_message_count();
    }

    // disabled, but cannot accept due to being over limit
    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_DISABLED);
    TEST_ASSERT_FALSE(nexus_keycode_pro_can_unit_accept_qc_code(
        NEXUS_KEYCODE_PRO_QC_SHORT_TEST_MESSAGE_SECONDS));

    // disabled, but cannot accept due to being over limit
    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_DISABLED);
    TEST_ASSERT_FALSE(nexus_keycode_pro_can_unit_accept_qc_code(
        NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS));
}

void test_nexus_keycode_pro_full_apply_factory__increment_long_qc_test_message_count__result_correct(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_long_qc_code_count(), 0);

    for (int i = 0; i < NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX; ++i)
    {
        nexus_keycode_pro_increment_long_qc_test_message_count();
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_long_qc_code_count(),
                               i + 1);
    }
}

void test_nexus_keycode_pro_full_apply_factory__increment_short_qc_test_message_count__result_correct(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_short_qc_code_count(), 0);

    for (int i = 0; i < NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX; ++i)
    {
        nexus_keycode_pro_increment_short_qc_test_message_count();
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_short_qc_code_count(),
                               i + 1);
    }
}

void test_nexus_keycode_full_apply_factory__increment_short_qc_test_message_count__short_and_long_increment_correctly(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);

    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_short_qc_code_count(), 0);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_long_qc_code_count(), 0);

    for (int i = 0; i < NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX; ++i)
    {
        nexus_keycode_pro_increment_short_qc_test_message_count();
        nexus_keycode_pro_increment_long_qc_test_message_count();

        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_short_qc_code_count(),
                               i + 1);
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_long_qc_code_count(),
                               i + 1);
    }
}

void test_nexus_keycode_pro_full_apply_factory__display_payg_id_message__result_correct(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);

    const char* display_payg_id = "6347765";
    struct nexus_keycode_frame display_payg_id_frame =
        nexus_keycode_frame_filled(display_payg_id);

    struct nexus_keycode_pro_full_message display_payg_id_message;
    const bool display_payg_id_parsed = nexus_keycode_pro_full_parse(
        &display_payg_id_frame, &display_payg_id_message);

    TEST_ASSERT(display_payg_id_parsed);

    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_apply_factory(&display_payg_id_message);

    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_DISPLAY_DEVICE_ID);
}

void test_nexus_keycode_pro_full_apply_factory__confirm_payg_id_message__result_correct(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);

    // NOMAC_PAYG_ID_CONFIRMATION Keycode testing PAYG ID '12345678'
    nxp_keycode_get_user_facing_id_IgnoreAndReturn(0xBC614E);

    const char* confirm_payg_id = "712345678";
    struct nexus_keycode_frame confirm_payg_id_frame =
        nexus_keycode_frame_filled(confirm_payg_id);

    struct nexus_keycode_pro_full_message confirm_payg_id_message;
    const bool confirm_payg_id_parsed = nexus_keycode_pro_full_parse(
        &confirm_payg_id_frame, &confirm_payg_id_message);

    TEST_ASSERT(confirm_payg_id_parsed);

    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_apply_factory(&confirm_payg_id_message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // test against device ID 123456789
    nxp_keycode_get_user_facing_id_IgnoreAndReturn(0x075bcd15);

    const char* confirm_payg_id_2 = "7123456789";
    struct nexus_keycode_frame confirm_payg_id_frame_2 =
        nexus_keycode_frame_filled(confirm_payg_id_2);

    struct nexus_keycode_pro_full_message confirm_payg_id_message_2;
    const bool confirm_payg_id_parsed_2 = nexus_keycode_pro_full_parse(
        &confirm_payg_id_frame_2, &confirm_payg_id_message_2);

    TEST_ASSERT(confirm_payg_id_parsed_2);

    enum nexus_keycode_pro_response response_2 =
        nexus_keycode_pro_full_apply_factory(&confirm_payg_id_message_2);

    TEST_ASSERT_EQUAL_UINT(response_2,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // Testing 10-digit PAYG ID '1234567890'
    nxp_keycode_get_user_facing_id_IgnoreAndReturn(0x499602d2);

    const char* confirm_payg_id_3 = "71234567890";
    struct nexus_keycode_frame confirm_payg_id_frame_3 =
        nexus_keycode_frame_filled(confirm_payg_id_3);

    struct nexus_keycode_pro_full_message confirm_payg_id_message_3;
    const bool confirm_payg_id_parsed_3 = nexus_keycode_pro_full_parse(
        &confirm_payg_id_frame_3, &confirm_payg_id_message_3);

    TEST_ASSERT(confirm_payg_id_parsed_3);

    enum nexus_keycode_pro_response response_3 =
        nexus_keycode_pro_full_apply_factory(&confirm_payg_id_message_3);

    TEST_ASSERT_EQUAL_UINT(response_3,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
}

void test_nexus_keycode_pro_full_apply_factory__confirm_payg_id_message_mismatched_id__feedback_duplicate(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);
    uint32_t hwid = 87654321; //

    // NOMAC_PAYG_ID_CONFIRMATION Keycode testing PAYG ID '12345678'
    const char* confirm_payg_id = "712345678";
    struct nexus_keycode_frame confirm_payg_id_frame =
        nexus_keycode_frame_filled(confirm_payg_id);

    struct nexus_keycode_pro_full_message confirm_payg_id_message;
    const bool confirm_payg_id_parsed = nexus_keycode_pro_full_parse(
        &confirm_payg_id_frame, &confirm_payg_id_message);

    TEST_ASSERT(confirm_payg_id_parsed);

    nxp_keycode_get_user_facing_id_ExpectAndReturn(hwid);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_apply_factory(&confirm_payg_id_message);
    // applied for 'matches device ID'
    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);
}

void test_nexus_keycode_pro_full_apply_factory__confirm_payg_id_message_too_long__feedback_duplicate(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);

    /* This test confirms that if a 10-digit value which is larger than the
     * max allowed uint32_t value is entered as a keycode, the response function
     * will return DUPLICATE */
    uint32_t hwid = 0xD2029649; // Equal to '1234567890' in decimal, but
    // essentially arbitrary.

    // NOMAC_PAYG_ID_CONFIRMATION Keycode simulating a clumsy user entering
    // an unreal PAYG ID which is much larger than the max allowed uint32.
    const char* confirm_payg_id = "79999999999";
    struct nexus_keycode_frame confirm_payg_id_frame =
        nexus_keycode_frame_filled(confirm_payg_id);

    struct nexus_keycode_pro_full_message confirm_payg_id_message;
    const bool confirm_payg_id_parsed = nexus_keycode_pro_full_parse(
        &confirm_payg_id_frame, &confirm_payg_id_message);
    // Should successfully parse, but write a nonsense value to body.
    TEST_ASSERT(confirm_payg_id_parsed);

    nxp_keycode_get_user_facing_id_ExpectAndReturn(hwid);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_apply_factory(&confirm_payg_id_message);
    // Returns DUPLICATE when the entered PAYG_ID does not equal the real HWID
    TEST_ASSERT_EQUAL_UINT(response,
                           NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE);
}

void test_nexus_keycode_pro_full_apply_factory__confirm_payg_id_message__result_invalid(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);

    /* This test confirms that if a user enters a CONFIRM_PAYG_ID keycode with
     * a length greater than
     * ANGAZA_KEYCODE_PRO_FULL_PAYG_ID_MAX_CHARACTER_COUNT
     * (10), the keycode will be rejected. */

    const char* confirm_payg_id = "712345678901"; // 11-digit message body
    struct nexus_keycode_frame confirm_payg_id_frame =
        nexus_keycode_frame_filled(confirm_payg_id);

    struct nexus_keycode_pro_full_message confirm_payg_id_message;
    const bool confirm_payg_id_parsed = nexus_keycode_pro_full_parse(
        &confirm_payg_id_frame, &confirm_payg_id_message);
    // Should not successfully parse
    TEST_ASSERT(!confirm_payg_id_parsed);
    // Checks if it correctly returns invalid, for good measure.
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_parse_and_apply(&confirm_payg_id_frame);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);

    /* And confirms that if a CONFIRM_DEVICE_ID keycode with length less than
     * the minimum number of characters, it is rejected.
     */

    const char* confirm_payg_id_short = "71234567"; // 7-digit message body
    struct nexus_keycode_frame confirm_payg_id_frame_short =
        nexus_keycode_frame_filled(confirm_payg_id_short);

    struct nexus_keycode_pro_full_message confirm_payg_id_message_short;
    const bool confirm_payg_id_parsed_short = nexus_keycode_pro_full_parse(
        &confirm_payg_id_frame_short, &confirm_payg_id_message_short);
    // Should not successfully parse
    TEST_ASSERT_FALSE(confirm_payg_id_parsed_short);
    // Checks if it correctly returns invalid, for good measure.
    enum nexus_keycode_pro_response response_short =
        nexus_keycode_pro_full_parse_and_apply(&confirm_payg_id_frame_short);
    TEST_ASSERT_EQUAL_UINT(response_short, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);
}

void test_nexus_keycode_pro_full_parse_and_apply__valid_extension_command__no_response(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);

    // Non-interleaved extension command, length 13
    struct nexus_keycode_frame frame =
        nexus_keycode_frame_filled("8412345678902");
    struct nexus_keycode_pro_full_message message;

    nxp_keycode_passthrough_keycode_IgnoreAndReturn(
        NXP_KEYCODE_PASSTHROUGH_ERROR_NONE);
    bool parsed = nexus_keycode_pro_full_parse(&frame, &message);
    TEST_ASSERT(parsed);

    nxp_keycode_passthrough_keycode_IgnoreAndReturn(
        NXP_KEYCODE_PASSTHROUGH_ERROR_NONE);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_parse_and_apply(&frame);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_NONE);

    // Length 3, minimum body length
    struct nexus_keycode_frame frame_b = nexus_keycode_frame_filled("810");

    nxp_keycode_passthrough_keycode_IgnoreAndReturn(
        NXP_KEYCODE_PASSTHROUGH_ERROR_NONE);
    parsed = nexus_keycode_pro_full_parse(&frame_b, &message);
    TEST_ASSERT(parsed);

    nxp_keycode_passthrough_keycode_IgnoreAndReturn(
        NXP_KEYCODE_PASSTHROUGH_ERROR_NONE);
    response = nexus_keycode_pro_full_parse_and_apply(&frame_b);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_NONE);
}

void test_nexus_keycode_pro_full_parse_and_apply__too_short_extension_command__invalid_response(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);

    // Length 2, invalid, needs a body digit
    struct nexus_keycode_frame frame = nexus_keycode_frame_filled("81");
    struct nexus_keycode_pro_full_message message;

    bool parsed = nexus_keycode_pro_full_parse(&frame, &message);
    TEST_ASSERT(!parsed);

    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_parse_and_apply(&frame);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);
}

void test_nexus_keycode_pro_full_parse_and_apply__extension_command_no_body__invalid(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);
    // Non-interleaved extension command, no body
    struct nexus_keycode_frame frame = nexus_keycode_frame_filled("8");
    struct nexus_keycode_pro_full_message message;

    const bool parsed = nexus_keycode_pro_full_parse(&frame, &message);
    TEST_ASSERT(!parsed); // Will fail parsing

    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_parse_and_apply(&frame);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);
}

void test_nexus_keycode_pro_full_parse_and_apply__passthrough_command_wrong_length__invalid(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);

    // Non-interleaved extension command, length 14 (not allowed)
    struct nexus_keycode_frame frame =
        nexus_keycode_frame_filled("84123456789028");
    struct nexus_keycode_pro_full_message message;
    bool parsed = nexus_keycode_pro_full_parse(&frame, &message);
    TEST_ASSERT_FALSE(parsed);

    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_parse_and_apply(&frame);
    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);

    // 15 digits (longer than 14), allowed
    struct nexus_keycode_frame frame_2 =
        nexus_keycode_frame_filled("841234567890281");

    struct nx_keycode_complete_code pass_keycode;
    pass_keycode.keys = &frame_2.keys[1];
    pass_keycode.length = frame_2.length - 1;

    // comparison of struct in CMock may be flaky
    nxp_keycode_passthrough_keycode_IgnoreAndReturn(
        NXP_KEYCODE_PASSTHROUGH_ERROR_NONE);
    parsed = nexus_keycode_pro_full_parse(&frame_2, &message);
    TEST_ASSERT_TRUE(parsed);

    nxp_keycode_passthrough_keycode_IgnoreAndReturn(
        NXP_KEYCODE_PASSTHROUGH_ERROR_NONE);
    response = nexus_keycode_pro_full_parse_and_apply(&frame_2);
    TEST_ASSERT_EQUAL_UINT(NEXUS_KEYCODE_PRO_RESPONSE_NONE, response);
}

void test_nexus_keycode_pro_full_apply_activation__valid_wipe_state_message__result_correct(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);
    // Wipe credit only
    // construct the scenario inputs
    // Fill frame with *interleaved* message
    // protocol.ActivationMessage.wipe_state(0,
    // protocol.ActivationWipeFlags.TARGET_FLAGS_0, '\x00' * 16).to_keycode()
    // 27854061048455
    struct nexus_keycode_frame frame =
        nexus_keycode_frame_filled("27854061048455");
    struct nexus_keycode_pro_full_message message;

    nexus_keycode_pro_full_parse(&frame, &message);

    // should trigger a credit reset to 0
    nxp_keycode_payg_credit_set_ExpectAndReturn(0, true);
    // apply the message and verify the result
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_apply_activation(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(0), 1);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(23), 0);

    // move PD forward , simulating a unit in use for almost a year with
    // daily keycodes
    nexus_keycode_pro_set_full_message_id_flag(301);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 301);

    // Wipe credit and IDS both (ID = 303, TARGET_FLAGS_1 (wipe both credit and
    // IDs)
    frame = nexus_keycode_frame_filled("19469685968779");

    nexus_keycode_pro_full_parse(&frame, &message);

    // should trigger a credit reset to 0
    nxp_keycode_payg_credit_set_ExpectAndReturn(0, true);
    // apply the message and verify the result
    response = nexus_keycode_pro_full_apply_activation(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    for (uint8_t i = 0; i < 23; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);

    nexus_keycode_pro_set_full_message_id_flag(301);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 301);

    // wipe IDs only (preserve ACE); WIPE_IDS_ALL ID = 303
    frame = nexus_keycode_frame_filled("45299993090378");

    nexus_keycode_pro_full_parse(&frame, &message);
    // apply the message and verify the result
    response = nexus_keycode_pro_full_apply_activation(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    // Ensure message IDs were reset
    for (uint8_t i = 0; i < 23; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);
}

void test_nexus_keycode_pro_full_apply_activation__demo_code_accepted__demo_behavior_ok(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);

    /* Scenario input 10 minutes of demo time:
    * protocol.ActivationMessage.demo_code(15, 10, '\xff' * 16)
    * Out[26]: nexus.protocols.keycodev1.ActivationMessage('315',
    * '00010',
    * '\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff',
    * is_factory=False))
    * 56022601917455
    */
    struct nexus_keycode_frame frame =
        nexus_keycode_frame_filled("56022601917455");
    struct nexus_keycode_pro_full_message message;
    nexus_keycode_pro_full_parse(&frame, &message);

    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_DISABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(60 * 10, true);

    // apply the message and verify the result
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_apply(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    /* Scenario input 30 minutes of demo time (same index, demo codes don't set
     * index):
     * In [28]: protocol.ActivationMessage.demo_code(
     * 15, 30, '\xff' * 16).to_keycode()
     * Out[28]: '06944198907301'
     */
    uint32_t hwid = 0x12345678;
    frame = nexus_keycode_frame_filled("06944198907301");

    nexus_keycode_pro_full_parse(&frame, &message);

    // Demo for 30 minutes
    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_ENABLED);
    nxp_keycode_payg_credit_add_ExpectAndReturn(60 * 30, true);
    response = nexus_keycode_pro_full_apply(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);
}

void test_nexus_keycode_pro_full_apply_activation__demo_code_rejected__demo_behavior_ok(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);
    /* Finally, send a message for a different secret key, it should be
     * rejected.
     *
     * Note that secret key is defaulted to 'all 0s' per the setup functions in
     * this set of tests..
     * In [30]: protocol.ActivationMessage.demo_code(15,
     * 30, '\xfa' * 16).to_keycode()
     * Out[30]: '37447047416988'
     */
    struct nexus_keycode_frame frame =
        nexus_keycode_frame_filled("37447047416988");
    struct nexus_keycode_pro_full_message message;

    const bool parsed = nexus_keycode_pro_full_parse(&frame, &message);
    TEST_ASSERT_TRUE(parsed);

    // apply the message and verify the result
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_apply(&message);

    TEST_ASSERT_EQUAL_UINT(response, NEXUS_KEYCODE_PRO_RESPONSE_INVALID);
}

void test_nexus_keycode_pro_full_deinterleave__various_inputs__outputs_correct(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);
    struct test_scenario
    {
        const char* deinterleaved;
        const char* interleaved;
        const uint32_t check_value;
    };

    const struct test_scenario scenarios[] = {
        // reference examples taken from protocol spec
        {"00000000524232", "57396884524232", 524232},
        {"12345678901241", "05094833901241", 901241},
        {"12345678901250", "57458927901250", 901250},
        {"00000000445755", "03605158445755", 445755}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // deinterleave the example input
        const struct test_scenario scenario = scenarios[i];
        struct nexus_keycode_frame output =
            nexus_keycode_frame_filled(scenario.interleaved);

        nexus_keycode_pro_full_deinterleave(&output, scenario.check_value);

        // verify the result
        const struct nexus_keycode_frame expected =
            nexus_keycode_frame_filled(scenario.deinterleaved);

        TEST_ASSERT_EQUAL_UINT(expected.length, output.length);

        for (uint8_t i = 0; i < output.length; i++)
        {
            TEST_ASSERT_EQUAL_UINT(expected.keys[i], output.keys[i]);
        }
    }
}

void test_nexus_keycode_pro_full_compute_check__various_inputs__outputs_correct(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);
    struct test_scenario
    {
        const char* message;
        const struct nx_core_check_key* key;
        uint32_t check;
    };

    const struct nx_core_check_key key_all1s = {{0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff}};
    const struct nx_core_check_key key_mixed = {{0x12,
                                                 0xff,
                                                 0x00,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xff,
                                                 0xcd,
                                                 0xff,
                                                 0xab}};
    const struct test_scenario scenarios[] = {
        // reference examples generated via Python library
        {"33217306036264", &key_all1s, 36264}, // add; id=1, hours=168
        {"32857330049677", &key_all1s, 49677}, // set; id=63, hours=500
        {"29015288972919", &key_mixed, 972919}, // demo; id=20, minutes=20
        {"94922693472577", &key_mixed, 472577}, // wipe_0; id=45
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const struct test_scenario scenario = scenarios[i];
        struct nexus_keycode_frame input =
            nexus_keycode_frame_filled(scenario.message);

        struct nexus_keycode_pro_full_message parsed_message;
        const bool parsed =
            nexus_keycode_pro_full_parse(&input, &parsed_message);
        TEST_ASSERT_TRUE(parsed);

        const uint32_t result =
            nexus_keycode_pro_full_compute_check(&parsed_message, scenario.key);

        TEST_ASSERT_EQUAL_UINT(result, scenario.check);
    }
}

void test_nexus_keycode_pro_full_apply_activation__add_credit_to_unlocked__no_credit_change(
    void)
{
    _full_fixture_reinit(
        '*', '#', "0123456789", NEXUS_INTEGRITY_CHECK_FIXED_00_KEY);

    struct nexus_keycode_pro_full_message add_credit_msg = {
        40, // msg ID
        0, // ADD_CREDIT type code
        {{24}}, // 24 hours to add
        303072, // check
    };

    // simulate unlocked device
    nxp_core_payg_state_get_current_ExpectAndReturn(
        NXP_CORE_PAYG_STATE_UNLOCKED);

    // 'add credit' message ID is not yet set
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(40), 0);

    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_full_apply(&add_credit_msg);

    TEST_ASSERT_EQUAL_UINT(NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE,
                           response);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(40), 1);

    // future applications recognized as 'duplicate'
    response = nexus_keycode_pro_full_apply(&add_credit_msg);

    TEST_ASSERT_EQUAL_UINT(NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE,
                           response);
}

#include "include/nx_common.h"
#include "src/nexus_common_internal.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
#include "src/nexus_keycode_pro_extended.h"
#include "src/nexus_nv.h"
#include "src/nexus_util.h"
#include "unity.h"
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

// Other support libraries
#include <mock_nxp_common.h>
#include <mock_nxp_keycode.h>
// we don't use channel, but some common init code imports it
#include <mock_nexus_channel_core.h>

#include <stdbool.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"

/********************************************************
 * DEFINITIONS
 *******************************************************/
/********************************************************
 * PRIVATE TYPES
 *******************************************************/

/********************************************************
 * PRIVATE DATA
 *******************************************************/

struct nx_common_check_key TEST_KEY = {{
    0xFE,
    0xFE,
    0xFE,
    0xFE,
    0xFE,
    0xFE,
    0xFE,
    0xFE,
    0xA2,
    0xA2,
    0xA2,
    0xA2,
    0xA2,
    0xA2,
    0xA2,
    0xA2,
}};

// All smallpad bit commands commands validated against TEST_KEY
uint8_t bitstream_bytes_set_wipe[4] = {0};
uint8_t bitstream_bytes_unsupported_cmd[4] = {0};
uint8_t bitstream_bytes_unsupported_action[4] = {0};

struct nexus_bitstream VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5;
struct nexus_bitstream VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE;

// test message which is populated in some tests
struct nexus_keycode_pro_extended_small_message message;

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/

// convenience function used to fill a smallpad keycode frame
struct nexus_keycode_frame small_nexus_keycode_frame_filled(const char* keys)
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
}

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    // ignore NV read/writes
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);

    _small_fixture_reinit('*', "0123");

    // ensure we overwrite all fields, ensure parsers do populate them
    memset(&message, 0xBA, sizeof(message));
    memset(&bitstream_bytes_set_wipe, 0x00, sizeof(bitstream_bytes_set_wipe));
    memset(&bitstream_bytes_unsupported_cmd,
           0x00,
           sizeof(bitstream_bytes_unsupported_cmd));
    memset(&bitstream_bytes_unsupported_action,
           0x00,
           sizeof(bitstream_bytes_unsupported_action));

    nexus_bitstream_init(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5,
        bitstream_bytes_set_wipe,
        sizeof(bitstream_bytes_set_wipe) * 8,
        0);

    nexus_bitstream_init(&VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE,
                         bitstream_bytes_unsupported_cmd,
                         sizeof(bitstream_bytes_unsupported_cmd) * 8,
                         0);

    // app type, 1 = NXC (1 bit)
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    // 0 = SET + WIPE RESTRICTED (3 bits)
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    // upper two bits 0b01 (for 5)
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    // interval ID 105 (0b01101001)
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    // 12 MAC bits for MSG ID 5 with the above
    // 0b101101111110
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 0);

    NEXUS_ASSERT(
        VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5.length == 26,
        "Invalid initialized bitstream length");

    // app type, 1 = NXC (1 bit)
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE, 1);
    // 0b101 = 5 = unknown command type (3 bits)
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE, 1);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE, 0);
    nexus_bitstream_push_bit(
        &VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE, 1);
    for (uint8_t i = 0; i < 22; i++)
    {
        // remaining bit contents dont matter
        nexus_bitstream_push_bit(
            &VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE, 1);
    }
    NEXUS_ASSERT(VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE.length ==
                     26,
                 "Invalid initialized bitstream length");

    // skip first bit (passthrough app ID) as origin command functions expect
    // this bit to already be evaluated
    nexus_bitstream_set_bit_position(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, 1);
    nexus_bitstream_set_bit_position(
        &VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE, 1);
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
}

void test_smallpad_bitstream_parse_message__valid_types__returns_true(void)
{
    bool result = nexus_keycode_pro_extended_small_parse(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, &message);
    TEST_ASSERT_TRUE(result);
}

void test_smallpad_bitstream_parse_message__unsupported_messages__returns_false(
    void)
{
    bool result = nexus_keycode_pro_extended_small_parse(
        &VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE, &message);
    TEST_ASSERT_FALSE(result);
}

void test_smallpad_bitstream_infer_fields_compute_auth__valid_messages__validate_ok(
    void)
{
    bool result = nexus_keycode_pro_extended_small_parse(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, &message);
    TEST_ASSERT_TRUE(result);

    // no keycode IDs consumed in the window initially
    struct nexus_window keycode_window;
    nexus_keycode_pro_get_current_message_id_window(&keycode_window);

    result = nexus_keycode_pro_extended_small_infer_windowed_message_id(
        &message, &keycode_window, &TEST_KEY);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT(5, message.inferred_message_id);
}

void test_smallpad_bitstream_infer_fields_compute_auth__invalid_messages__doesnt_validate(
    void)
{
    bool result = nexus_keycode_pro_extended_small_parse(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, &message);
    TEST_ASSERT_TRUE(result);

    struct nexus_keycode_pro_extended_small_message invalid_mac_message;
    memcpy(&invalid_mac_message, &message, sizeof(message));
    // change the MAC field of the parsed message before proceeding by 1
    invalid_mac_message.check += 1;

    // no keycode IDs consumed in the window initially
    struct nexus_window keycode_window;
    nexus_keycode_pro_get_current_message_id_window(&keycode_window);

    result = nexus_keycode_pro_extended_small_infer_windowed_message_id(
        &invalid_mac_message, &keycode_window, &TEST_KEY);

    TEST_ASSERT_FALSE(result);

    // try an unsupported origin command type
    struct nexus_keycode_pro_extended_small_message unsupported_command_message;
    memcpy(&unsupported_command_message, &message, sizeof(message));
    unsupported_command_message.type_code =
        NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLOCK;

    result = nexus_keycode_pro_extended_small_infer_windowed_message_id(
        &unsupported_command_message, &keycode_window, &TEST_KEY);

    TEST_ASSERT_FALSE(result);
}

void test_smallpad_apply_message__valid_message__applied_feedback_correct(void)
{
    bool result = nexus_keycode_pro_extended_small_parse(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5, &message);
    TEST_ASSERT_TRUE(result);

    uint32_t expected_seconds =
        nexus_keycode_pro_small_get_set_credit_increment_days(
            (uint8_t) message.body.set_credit_wipe_flag.increment_id) *
        NEXUS_KEYCODE_PRO_SECONDS_IN_DAY;

    nxp_keycode_get_secret_key_ExpectAndReturn(TEST_KEY);
    nxp_keycode_payg_credit_set_ExpectAndReturn(expected_seconds, true);
    nxp_common_nv_write_StopIgnore();
    // once for set credit, once for restricted flag.
    nxp_common_nv_write_ExpectAnyArgsAndReturn(true);
    nxp_common_nv_write_ExpectAnyArgsAndReturn(true);
    nxp_keycode_notify_custom_flag_changed_Expect(
        NX_KEYCODE_CUSTOM_FLAG_RESTRICTED, false);

    nxp_keycode_feedback_start_ExpectAndReturn(
        NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED, true);

    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_extended_small_apply(&message);

    TEST_ASSERT_EQUAL_UINT(NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED, response);

    nxp_keycode_get_secret_key_ExpectAndReturn(TEST_KEY);
    // applying again fails, since the keycode message ID is already set.
    nxp_keycode_feedback_start_ExpectAndReturn(
        NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID, true);
    response = nexus_keycode_pro_extended_small_apply(&message);

    TEST_ASSERT_EQUAL(NEXUS_KEYCODE_PRO_RESPONSE_INVALID, response);
}

void test_smallpad_apply_message_end_to_end__set_credit_wipe_restricted__interacts_correctly_with_set_credit(
    void)
{
    struct test_scenario
    {
        const char* frame_body;
        uint8_t id;
        uint32_t expected_credit_seconds;
        enum nexus_keycode_pro_small_type_codes expected_type_code;
        enum nexus_keycode_pro_response expected_response;
        enum nxp_keycode_feedback_type expected_feedback;
        bool is_wipe_flag_keycode; // only wipe code, not set + wipe
        const char* alphabet;
    };

    nxp_common_nv_read_IgnoreAndReturn(true);

    const struct test_scenario scenarios[] = {
        {
            // ExtendedSmallMessageType.SET_CREDIT_WIPE_RESTRICTED_FLAG,
            // id_=0,
            // days=915,
            // secret_key=b'\xfe'*8 + b'\xa2' * 8).to_keycode()
            // 155 222 234 423 344
            "33000012201122",
            0,
            928 * 24 * 3600,
            NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH,
            NEXUS_KEYCODE_PRO_RESPONSE_INVALID, // unused
            NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
            false,
            "0123",
        },
        {
            // In [21]: SetCreditSmallMessage(id_=13, days=5,
            // secret_key=b'\xfe'*8 + b'\xa2' * 8).to_keycode() Out[21]: '124
            // 555 332 453 453'
            "02333110231231",
            13,
            5 * 24 * 3600,
            NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
            NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED,
            NXP_KEYCODE_FEEDBACK_TYPE_NONE, // unused
            false,
            "0123",
        },
        {
            // In [28]: code = ExtendedSmallMessage(id_=15, days=0,
            // type_=ExtendedSmallMessageType.SET_CREDIT_WIPE_RESTRICTED_FLAG,
            // secret_key=secret_key) In [29]: code.extended_message_id Out[29]:
            // 15 In [30]: code.to_keycode() Out[30]: '153 324 434 455 545'
            // 53324434455545
            "31102212233323",
            15,
            0,
            NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH,
            NEXUS_KEYCODE_PRO_RESPONSE_INVALID, // unused
            NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
            false,
            "0123",
        },
        {
            // same as above, should be 'invalid'
            "31102212233323",
            15,
            0,
            NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH,
            NEXUS_KEYCODE_PRO_RESPONSE_INVALID, // unused
            NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID,
            false,
            "0123",
        },
        {
            // ExtendedSmallMessageType.SET_CREDIT_WIPE_RESTRICTED_FLAG,
            // id_=60,
            // days=SmallMessage.UNLOCK_FLAG,
            // secret_key=b'\xfe'*8 + b'\xa2' * 8).to_keycode()
            // "123 245 222 535 225"
            "01023000313003",
            60,
            NEXUS_KEYCODE_PRO_SMALL_UNLOCK_INCREMENT,
            NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH,
            NEXUS_KEYCODE_PRO_RESPONSE_INVALID, // unused
            NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
            false,
            "0123",
        },
        {
            // In [25]: SetCreditSmallMessage(id_=63, days=200,
            // secret_key=b'\xfe'*8 + b'\xa2' * 8).to_keycode() Out[25]: '142
            // 223 242 233 324'
            "20001020011102",
            63,
            200 * 24 * 3600,
            NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
            NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED,
            NXP_KEYCODE_FEEDBACK_TYPE_NONE, // unused
            false,
            "0123",
        },
        {
            // In [30]: CustomCommandSmallMessage(78,
            // CustomCommandSmallMessageType.WIPE_RESTRICTED_FLAG, b'\xfe' * 8 +
            // b'\xa2' * 8).to_keycode() Out[30]: '143 455 425 525 232'
            "21233203303010",
            78,
            0, // unused
            NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
            NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED,
            NXP_KEYCODE_FEEDBACK_TYPE_NONE, // unused
            true,
            "0123",
        },
        {
            // In [31]: SetCreditSmallMessage(id_=80, days=33,
            // secret_key=b'\xfe'*8 + b'\xa2' * 8).to_keycode() Out[31]: '144
            // 433 335 332 243'
            "22211113110021",
            80,
            33 * 24 * 3600,
            NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE,
            NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED,
            NXP_KEYCODE_FEEDBACK_TYPE_NONE, // unused
            false,
            "0123",
        },
        {
            // In [18]: secret_key=b"\xfe" * 8 + b"\xa2" * 8
            // In [19]: code = ExtendedSmallMessage(id_=90,
            // days=365,
            // type_=ExtendedSmallMessageType.SET_CREDIT_WIPE_RESTRICTED_FLAG,
            // secret_key=secret_key).to_keycode()
            // '132 223 555 342 554'
            "10001333120332",
            90,
            368 * 24 * 3600,
            NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH,
            NEXUS_KEYCODE_PRO_RESPONSE_INVALID, // unused
            NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
            false,
            "0123",
        },

        {
            // In [18]: secret_key=b"\xfe" * 8 + b"\xa2" * 8
            // In [19]: code = ExtendedSmallMessage(id_=105,
            // days=ExtendedSmallMessage.UNLOCK_FLAG,
            // type_=ExtendedSmallMessageType.SET_CREDIT_WIPE_RESTRICTED_FLAG,
            // secret_key=secret_key) In [20]: code.to_keycode() Out[20]: '134
            // 542 222 342 444' In [21]: code.extended_message_id Out[21]: 105
            // 34542222342444
            "12320000120222",
            105,
            NEXUS_KEYCODE_PRO_SMALL_UNLOCK_INCREMENT,
            NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH,
            NEXUS_KEYCODE_PRO_RESPONSE_INVALID, // unused
            NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
            false,
            "0123",
        },
        {
            // In [25]: code = ExtendedSmallMessage(id_=136, days=90,
            // type_=ExtendedSmallMessageType.SET_CREDIT_WIPE_RESTRICTED_FLAG,
            // secret_key=secret_key) In [26]: code.extended_message_id Out[26]:
            // 136 In [27]: code.to_keycode() Out[27]: '144 433 453 232 344'
            // 44433453232344
            "22211231010122",
            136,
            90 * 24 * 3600,
            NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH,
            NEXUS_KEYCODE_PRO_RESPONSE_INVALID, // unused
            NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED,
            false,
            "0123",
        },
    };

    // no IDs set before the messages are applied
    for (uint8_t j = 0; j <= 200; j++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(j),
                               0);
    }

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // initialize the scenario
        const struct test_scenario scenario = scenarios[i];

        // execute it and verify outcome
        struct nexus_keycode_frame frame =
            small_nexus_keycode_frame_filled(scenario.frame_body);
        struct nexus_keycode_pro_small_message small_msg;

        // b'\xfe' * 8 + b'\xa2' * 8
        nxp_keycode_get_secret_key_ExpectAndReturn(TEST_KEY);
        // SET CREDIT + WIPE RESTRICTED
        if (scenario.expected_type_code ==
            NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH)
        {
            if (scenario.expected_feedback ==
                NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED)
            {
                if (scenario.expected_credit_seconds ==
                    NEXUS_KEYCODE_PRO_SMALL_UNLOCK_INCREMENT)
                {
                    nxp_keycode_payg_credit_unlock_ExpectAndReturn(true);
                }
                else
                {
                    nxp_keycode_payg_credit_set_ExpectAndReturn(
                        scenario.expected_credit_seconds, true);
                }
                nxp_keycode_notify_custom_flag_changed_Expect(
                    NX_KEYCODE_CUSTOM_FLAG_RESTRICTED, false);
            }
            nxp_keycode_feedback_start_ExpectAndReturn(
                scenario.expected_feedback, true);
        }

        // will automatically pass data to origin command handler if its
        // an origin command
        const bool parsed = nexus_keycode_pro_small_parse(&frame, &small_msg);
        TEST_ASSERT_TRUE(parsed);

        if (scenario.expected_type_code !=
            NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH)
        {
            if (!scenario.is_wipe_flag_keycode)
            {
                nxp_keycode_payg_credit_set_ExpectAndReturn(
                    scenario.expected_credit_seconds, true);
            }
            else
            {
                nxp_keycode_notify_custom_flag_changed_Expect(
                    NX_KEYCODE_CUSTOM_FLAG_RESTRICTED, false);
            }
            enum nexus_keycode_pro_response response =
                nexus_keycode_pro_small_apply(&small_msg);
            TEST_ASSERT_EQUAL_UINT(response, scenario.expected_response);
        }

        // as the window moves upwards, don't check below the minimum window ID
        uint16_t min_window_id;
        if (scenario.id >= 23)
        {
            min_window_id = scenario.id - 23;
        }
        else
        {
            min_window_id = 0;
        }
        for (uint16_t j = min_window_id; j <= scenario.id; j++)
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(j), 1);
        }
    }
}

void test_extended_small_parse_and_apply__valid_command__handled_applied(void)
{
    // assert that message IDs 0-5 are not set
    for (uint8_t i = 0; i <= 5; i++)
    {
        TEST_ASSERT_FALSE(nexus_keycode_pro_get_full_message_id_flag(i));
    }

    nxp_keycode_get_secret_key_ExpectAndReturn(TEST_KEY);
    // 122 days, fixed for SET_AND_WIPE_CREDIT_MSG_ID_5 bitstream
    nxp_keycode_payg_credit_set_ExpectAndReturn(10540800, true);
    nxp_keycode_notify_custom_flag_changed_Expect(
        NX_KEYCODE_CUSTOM_FLAG_RESTRICTED, false);
    nxp_keycode_feedback_start_ExpectAndReturn(
        NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED, true);
    bool result = nexus_keycode_pro_extended_small_parse_and_apply_keycode(
        &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5);
    TEST_ASSERT_TRUE(result);
    // assert that message IDs 0-5 (inclusive) were set.
    for (uint8_t i = 0; i <= 5; i++)
    {
        TEST_ASSERT_TRUE(nexus_keycode_pro_get_full_message_id_flag(i));
    }
}

void test_extended_small_parse_and_apply__invalid_commands__trigger_invalid_feedback(
    void)
{
    nxp_keycode_feedback_start_ExpectAndReturn(
        NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID, true);
    bool result = nexus_keycode_pro_extended_small_parse_and_apply_keycode(
        &VALID_SMALLPAD_BITS_EXTENDED_UNSUPPORTED_COMMAND_TYPE);
    TEST_ASSERT_FALSE(result);

    // corrupt bits 17-24 of the SET_AND_WIPE_CREDIT_MSG_ID_5 bitstream,
    // which should cause 'apply' to fail, and trigger keycode invalid feedback.
    bitstream_bytes_set_wipe[3] = 0xcc;
    nxp_keycode_get_secret_key_ExpectAndReturn(TEST_KEY);
    // 122 days, fixed for SET_AND_WIPE_CREDIT_MSG_ID_5 bitstream
    nxp_keycode_feedback_start_ExpectAndReturn(
        NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID, true);
    enum nexus_keycode_pro_response response =
        nexus_keycode_pro_extended_small_parse_and_apply_keycode(
            &VALID_SMALLPAD_BITS_EXTENDED_SET_AND_WIPE_CREDIT_MSG_5);
    TEST_ASSERT_EQUAL_UINT(NEXUS_KEYCODE_PRO_RESPONSE_INVALID, response);
}

#pragma GCC diagnostic pop

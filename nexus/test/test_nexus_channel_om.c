#include "include/nx_channel.h"

#include "include/nx_common.h"
#include "src/nexus_channel_om.h"
#include "src/nexus_nv.h"
#include "src/nexus_util.h"
#include "unity.h"
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

// Other support libraries
#include <mock_nxp_channel.h>
#include <mock_nxp_common.h>

// Hide channel OC dependencies from origin manager tests
#include <mock_nexus_channel_core.h>
#include <mock_nexus_channel_res_link_hs.h>
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
uint8_t dummy_data[15];
char* INVALID_ASCII_ORIGIN_COMMAND = "12944";

struct nx_common_check_key CONTROLLER_KEY = {{
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

// = b'\xc4\xb8@H\xcf\x04$\xa2]\xc5\xe9\xd3\xf0g@6'
struct nx_common_check_key ACCESSORY_KEY = {{0xC4,
                                             0xB8,
                                             0x40,
                                             0x48,
                                             0xCF,
                                             0x04,
                                             0x24,
                                             0xA2,
                                             0x5D,
                                             0xC5,
                                             0xE9,
                                             0xD3,
                                             0xF0,
                                             0x67,
                                             0x40,
                                             0x36}};

// generated using CONTROLLER_KEY, command count = 15
char* VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES =
    "000018783";
char* VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLOCK_ALL_ACCESSORIES =
    "001906394";

// accessory ID 0x0102948372A4 ('0' last decimal digit truncated)
// controller command count = 15 (this MAC), accessory command count = 312
char* VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLOCK_ACCESSORY = "10244210";
char* VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLINK_ACCESSORY = "20536545";

// generated using ACCESSORY_KEY and CONTROLLER_KEY
// controller command count 15
// accessory asp ID = 0x0102948372A4
// accessory command count 2
// controller/accessory sym keys from above
// controller command count 15, accessory command count 2
/* otoken =
 * protocol.LinkCommandToken.challenge_mode_3(accessory_asp_id=0x0102948372A4,
 * controller_command_count=15, accessory_command_count=2,
 * accessory_sym_key=b'\xc4\xb8@H\xcf\x04$\xa2]\xc5\xe9\xd3\xf0g@6',
 * controller_sym_key='\xfe' * 8 + '\xa2' * 8)
 */
char* VALID_ASCII_ORIGIN_CREATE_LINK_ACCESSORY_MODE_3 = "92382847582879";

// test message which is populated in some tests
struct nexus_channel_om_command_message message;

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    // ignore NV read/writes
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);

    nexus_channel_om_init();

    // ensure we overwrite all fields, ensure parsers do populate them
    memset(&message, 0xBA, sizeof(message));
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
}

void test_handle_origin_command__invalid_type__returns_error(void)
{
    nx_channel_error result = nx_channel_handle_origin_command(
        (enum nx_channel_origin_command_bearer_type) 555, // invalid type
        (void*) &dummy_data,
        sizeof(dummy_data));

    TEST_ASSERT_EQUAL_INT(NX_CHANNEL_ERROR_ACTION_REJECTED, result);
}

void test_handle_origin_command__invalid_ascii_type__returns_error(void)
{
    nx_channel_error result = nx_channel_handle_origin_command(
        NX_CHANNEL_ORIGIN_COMMAND_BEARER_TYPE_ASCII_DIGITS,
        (void*) &INVALID_ASCII_ORIGIN_COMMAND,
        (uint32_t) strlen(INVALID_ASCII_ORIGIN_COMMAND));

    TEST_ASSERT_EQUAL_INT(NX_CHANNEL_ERROR_ACTION_REJECTED, result);
}

void test_handle_origin_command__valid_message__returns_no_error(void)
{
    nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
    nexus_channel_core_apply_origin_command_IgnoreAndReturn(true);
    TEST_ASSERT_EQUAL_INT(
        NX_CHANNEL_ERROR_NONE,
        nx_channel_handle_origin_command(
            NX_CHANNEL_ORIGIN_COMMAND_BEARER_TYPE_ASCII_DIGITS,
            VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES,
            (uint32_t) strlen(
                VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES)));
}

void test_ascii_extract_command_type__invalid_type__returns_invalid(void)
{
    // invalid above last valid ID
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_OM_COMMAND_TYPE_INVALID,
        _nexus_channel_om_ascii_validate_command_type((const char) 100));

    // invalid between 2 and 9
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_OM_COMMAND_TYPE_INVALID,
                      _nexus_channel_om_ascii_validate_command_type('5'));
}

void test_handle_ascii_origin_command__too_long_command_length__returns_false(
    void)
{
    TEST_ASSERT_FALSE(
        _nexus_channel_om_handle_ascii_origin_command("324823472", 256));
}

void test_handle_ascii_origin_command__zero_length_command__returns_false(void)
{
    TEST_ASSERT_FALSE(
        _nexus_channel_om_handle_ascii_origin_command("324823472", 0));
}

void test_handle_ascii_origin_command__non_ascii_digits_in_command__returns_false(
    void)
{
    // less than 0x30
    TEST_ASSERT_FALSE(
        _nexus_channel_om_handle_ascii_origin_command("-10#++9173", 10));

    // way less than 0x30
    TEST_ASSERT_FALSE(
        _nexus_channel_om_handle_ascii_origin_command("\x01#++9173", 10));

    // More than 0x39
    TEST_ASSERT_FALSE(
        _nexus_channel_om_handle_ascii_origin_command("=#++9173", 10));
}

void test_handle_ascii_origin_command__message_structure_unparseable__rejects_message(
    void)
{
    TEST_ASSERT_FALSE(_nexus_channel_om_handle_ascii_origin_command(
        VALID_ASCII_ORIGIN_CREATE_LINK_ACCESSORY_MODE_3,
        (uint32_t) strlen(VALID_ASCII_ORIGIN_CREATE_LINK_ACCESSORY_MODE_3) -
            1));
}

void test_handle_ascii_origin_command__valid_message_not_already_used__handles_message(
    void)
{
    nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
    nexus_channel_core_apply_origin_command_IgnoreAndReturn(true);
    TEST_ASSERT_TRUE(_nexus_channel_om_handle_ascii_origin_command(
        VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES,
        (uint32_t) strlen(
            VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES)));
}

void test_handle_ascii_origin_command__valid_message_already_used__rejects_message(
    void)
{
    nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
    nexus_channel_core_apply_origin_command_IgnoreAndReturn(true);
    TEST_ASSERT_TRUE(_nexus_channel_om_handle_ascii_origin_command(
        VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES,
        (uint32_t) strlen(
            VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES)));

    nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
    // No need for another 'common' mock here, we don't attempt to apply to
    // Nexus common.
    TEST_ASSERT_FALSE(_nexus_channel_om_handle_ascii_origin_command(
        VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES,
        (uint32_t) strlen(
            VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES)));
}

void test_ascii_parse_message__generic_controller_action_unlink_all_accessories__parsed_ok(
    void)
{
    struct nexus_digits command_digits;
    nexus_digits_init(
        &command_digits,
        VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES,
        (uint16_t) strlen(
            VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES));

    // parsed successfully?
    TEST_ASSERT_TRUE(
        _nexus_channel_om_ascii_parse_message(&command_digits, &message));

    // parsed fields OK?
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_OM_COMMAND_TYPE_GENERIC_CONTROLLER_ACTION,
                      message.type);
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_ORIGIN_COMMAND_UNLINK_ALL_LINKED_ACCESSORIES,
        message.body.controller_action.action_type);
    TEST_ASSERT_EQUAL(18783, message.auth.six_int_digits);
}

void test_ascii_parse_message__generic_controller_action_unlock_all_accessories__parsed_ok(
    void)
{
    struct nexus_digits command_digits;
    nexus_digits_init(
        &command_digits,
        VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLOCK_ALL_ACCESSORIES,
        (uint16_t) strlen(
            VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLOCK_ALL_ACCESSORIES));

    // parsed successfully?
    TEST_ASSERT_TRUE(
        _nexus_channel_om_ascii_parse_message(&command_digits, &message));

    // parsed fields OK?
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_OM_COMMAND_TYPE_GENERIC_CONTROLLER_ACTION,
                      message.type);
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_ORIGIN_COMMAND_UNLOCK_ALL_LINKED_ACCESSORIES,
        message.body.controller_action.action_type);
    TEST_ASSERT_EQUAL(906394, message.auth.six_int_digits);
}

void test_ascii_parse_message__accessory_action_unlock__parsed_ok(void)
{
    struct nexus_digits command_digits;
    nexus_digits_init(
        &command_digits,
        VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLOCK_ACCESSORY,
        (uint16_t) strlen(
            VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLOCK_ACCESSORY));

    // parsed successfully?
    TEST_ASSERT_TRUE(
        _nexus_channel_om_ascii_parse_message(&command_digits, &message));

    // parsed fields OK?
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLOCK,
                      message.type);
    TEST_ASSERT_EQUAL(1,
                      message.body.accessory_action.trunc_acc_id.digits_count);
    TEST_ASSERT_EQUAL(0, message.body.accessory_action.trunc_acc_id.digits_int);
    TEST_ASSERT_EQUAL(244210, message.auth.six_int_digits);
}

void test_ascii_parse_message__accessory_action_unlink__parsed_ok(void)
{
    struct nexus_digits command_digits;
    nexus_digits_init(
        &command_digits,
        VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLINK_ACCESSORY,
        (uint16_t) strlen(
            VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLINK_ACCESSORY));

    // parsed successfully?
    TEST_ASSERT_TRUE(
        _nexus_channel_om_ascii_parse_message(&command_digits, &message));

    // parsed fields OK?
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK,
                      message.type);
    TEST_ASSERT_EQUAL(1,
                      message.body.accessory_action.trunc_acc_id.digits_count);
    TEST_ASSERT_EQUAL(0, message.body.accessory_action.trunc_acc_id.digits_int);
    TEST_ASSERT_EQUAL(536545, message.auth.six_int_digits);
}

void test_ascii_parse_message__create_link_accessory_mode_3__parsed_ok(void)
{
    struct nexus_digits command_digits;
    nexus_digits_init(
        &command_digits,
        VALID_ASCII_ORIGIN_CREATE_LINK_ACCESSORY_MODE_3,
        (uint16_t) strlen(VALID_ASCII_ORIGIN_CREATE_LINK_ACCESSORY_MODE_3));

    // parsed successfully?
    TEST_ASSERT_TRUE(
        _nexus_channel_om_ascii_parse_message(&command_digits, &message));

    // parsed fields OK?
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3,
        message.type);
    TEST_ASSERT_EQUAL(
        382847, message.body.create_link.accessory_challenge.six_int_digits);
    TEST_ASSERT_EQUAL(582879, message.auth.six_int_digits);
}

void test_ascii_parse_message__create_link_accessory_mode_3_too_short_command__parsing_fails(
    void)
{
    struct nexus_digits command_digits;
    nexus_digits_init(
        &command_digits,
        VALID_ASCII_ORIGIN_CREATE_LINK_ACCESSORY_MODE_3,
        (uint16_t) strlen(VALID_ASCII_ORIGIN_CREATE_LINK_ACCESSORY_MODE_3) - 1);

    // parsed successfully?
    TEST_ASSERT_FALSE(
        _nexus_channel_om_ascii_parse_message(&command_digits, &message));

    // parsed fields OK? (even though parsing failed, it still tried to populate
    // something..)
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3,
        message.type);
    TEST_ASSERT_EQUAL(
        382847, message.body.create_link.accessory_challenge.six_int_digits);

    // sentinel failure value from util pull
    TEST_ASSERT_EQUAL(UINT32_MAX, message.auth.six_int_digits);
}

void test_ascii_parse_message__invalid_message_type__parsing_fails(void)
{
    // '5' not currently implemented
    char* invalid_msg = "5589373";
    struct nexus_digits command_digits;
    nexus_digits_init(
        &command_digits, invalid_msg, (uint16_t) strlen(invalid_msg));
    TEST_ASSERT_FALSE(
        _nexus_channel_om_ascii_parse_message(&command_digits, &message));
}

void test_nexus_channel_om_ascii_infer_fields_compute_auth__generic_controller__successful(
    void)
{
    // create a window that has default settings, no IDs set
    struct nexus_window window;
    uint8_t flag_array[4] = {0, 0, 0, 0};
    nexus_util_window_init(&window, flag_array, sizeof(flag_array), 31, 31, 8);

    // parsed representation of
    // VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES
    // computed ID is given a nonsense value (should be overwritten)
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_GENERIC_CONTROLLER_ACTION;
    message.body.controller_action.action_type =
        NEXUS_CHANNEL_ORIGIN_COMMAND_UNLOCK_ALL_LINKED_ACCESSORIES;
    message.auth.six_int_digits = 906394;
    message.computed_command_id = 0xFFFFFFFF;

    // give valid message with ID 15
    const bool valid = _nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY);
    TEST_ASSERT_EQUAL(15, message.computed_command_id);
    TEST_ASSERT_TRUE(valid);

    // attempt to infer with a different auth field
    message.auth.six_int_digits = 123456;
    TEST_ASSERT_FALSE(_nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY));
}

void test_nexus_channel_om_ascii_infer_fields_compute_auth__replay_command__not_reapplied(
    void)
{
    // create a window that has default settings, no IDs set
    struct nexus_window window;
    uint8_t flag_array[4] = {0, 0, 0, 0};
    nexus_util_window_init(&window, flag_array, sizeof(flag_array), 31, 31, 8);

    // parsed representation of
    // VALID_ASCII_ORIGIN_GENERIC_CONTROLLER_ACTION_UNLINK_ALL_ACCESSORIES
    // computed ID is given a nonsense value (should be overwritten)
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_GENERIC_CONTROLLER_ACTION;
    message.body.controller_action.action_type =
        NEXUS_CHANNEL_ORIGIN_COMMAND_UNLOCK_ALL_LINKED_ACCESSORIES;
    message.auth.six_int_digits = 906394;
    message.computed_command_id = 0xFFFFFFFF;

    // give valid message with ID 15
    const bool valid = _nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY);
    TEST_ASSERT_EQUAL(15, message.computed_command_id);
    TEST_ASSERT_TRUE(valid);

    // attempt valid message with ID already set
    nexus_util_window_set_id_flag(&window, 15);
    TEST_ASSERT_FALSE(_nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY));
}

void test_nexus_channel_om_ascii_infer_fields_compute_auth__invalid_type__not_valid(
    void)
{
    // create a window that has default settings, no IDs set
    struct nexus_window window;
    uint8_t flag_array[4] = {0, 0, 0, 0};
    nexus_util_window_init(&window, flag_array, sizeof(flag_array), 31, 31, 8);

    // valid body and MAC for a GENERIC_CONTROLLER_ACTION
    // type is wrong, though, so should be invalid
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_INVALID;
    message.body.controller_action.action_type =
        NEXUS_CHANNEL_ORIGIN_COMMAND_UNLOCK_ALL_LINKED_ACCESSORIES; // nonsense
    message.auth.six_int_digits = 906394;
    message.computed_command_id = 0xFFFFFFFF;

    // give valid message with ID 15
    const bool valid = _nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY);

    // unable to successfully infer ID, so message remains at 'top of window'
    // +1 (40) after running through entire valid window of 0-39 and failing.
    TEST_ASSERT_EQUAL(40, message.computed_command_id);
    TEST_ASSERT_FALSE(valid);
}

void test_nexus_channel_om_ascii_infer_fields_compute_auth__unlock_specific_accessory__successful(
    void)
{
    // create a window that has default settings, no IDs set
    struct nexus_window window;
    uint8_t flag_array[4] = {0, 0, 0, 0};
    nexus_util_window_init(&window, flag_array, sizeof(flag_array), 31, 31, 8);

    // XXX test relies on hard-coded representation of an accessory link
    // already existing.
    // TODO modify if we dynamically provide linked accessory info
    // parsed representation of
    // VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLOCK_ACCESSORY
    // computed ID is given a nonsense value (should be overwritten)
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLOCK;
    message.body.accessory_action.trunc_acc_id.digits_count = 1;
    message.body.accessory_action.trunc_acc_id.digits_int = 0;
    message.auth.six_int_digits = 244210;
    message.computed_command_id = 0xFFFFFFFF;
    memset(&message.body.accessory_action.computed_accessory_id,
           0,
           sizeof(struct nx_id));

    // give valid message with ID 15
    const bool valid = _nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY);
    TEST_ASSERT_EQUAL(15, message.computed_command_id);
    TEST_ASSERT_TRUE(valid);

    // attempt to infer with a different auth field
    message.auth.six_int_digits = 123456;
    TEST_ASSERT_FALSE(_nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY));
}

void test_nexus_channel_om_ascii_infer_fields_compute_auth_invalid_truncated_digits__fails_to_infer_message(
    void)
{
    // create a window that has default settings, no IDs set
    struct nexus_window window;
    uint8_t flag_array[4] = {0, 0, 0, 0};
    nexus_util_window_init(&window, flag_array, sizeof(flag_array), 31, 31, 8);

    // XXX test relies on hard-coded representation of an accessory link
    // already existing.
    // TODO modify if we dynamically provide linked accessory info
    // parsed representation of
    // VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLOCK_ACCESSORY
    // computed ID is given a nonsense value (should be overwritten)
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK;
    message.auth.six_int_digits = 536545;
    message.computed_command_id = 0xFFFFFFFF;
    memset(&message.body.accessory_action,
           0,
           sizeof(struct nexus_channel_om_accessory_action_body));

    // accessory truncated ID = 10, unsupported (3 digits)
    message.body.accessory_action.trunc_acc_id.digits_count = 3;
    message.body.accessory_action.trunc_acc_id.digits_int = 102;

    // give valid message with ID 15
    TEST_ASSERT_FALSE(_nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY));
}

void test_nexus_channel_om_ascii_infer_fields_compute_auth__unlink_specific_accessory__successful(
    void)
{
    // create a window that has default settings, no IDs set
    struct nexus_window window;
    uint8_t flag_array[4] = {0, 0, 0, 0};
    nexus_util_window_init(&window, flag_array, sizeof(flag_array), 31, 31, 8);

    // XXX test relies on hard-coded representation of an accessory link
    // already existing.
    // TODO modify if we dynamically provide linked accessory info
    // parsed representation of
    // VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLOCK_ACCESSORY
    // computed ID is given a nonsense value (should be overwritten)
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK;
    message.auth.six_int_digits = 536545;
    message.computed_command_id = 0xFFFFFFFF;
    memset(&message.body.accessory_action,
           0,
           sizeof(struct nexus_channel_om_accessory_action_body));

    // accessory truncated ID = 0
    message.body.accessory_action.trunc_acc_id.digits_count = 1;
    message.body.accessory_action.trunc_acc_id.digits_int = 0;

    // give valid message with ID 15
    const bool valid = _nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY);
    TEST_ASSERT_EQUAL(15, message.computed_command_id);
    TEST_ASSERT_TRUE(valid);

    // attempt to infer with a different auth field
    message.auth.six_int_digits = 123456;
    TEST_ASSERT_FALSE(_nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY));
}

void test_nexus_channel_om_ascii_infer_fields_compute_auth__unlink_specific_accessory_invalid_truncated_id__unsuccessful(
    void)
{
    // create a window that has default settings, no IDs set
    struct nexus_window window;
    uint8_t flag_array[4] = {0, 0, 0, 0};
    nexus_util_window_init(&window, flag_array, sizeof(flag_array), 31, 31, 8);

    // XXX test relies on hard-coded representation of an accessory link
    // already existing.
    // TODO modify if we dynamically provide linked accessory info
    // parsed representation of
    // VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLOCK_ACCESSORY
    // computed ID is given a nonsense value (should be overwritten)
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK;
    message.auth.six_int_digits = 536545;
    message.computed_command_id = 0xFFFFFFFF;
    memset(&message.body.accessory_action,
           0,
           sizeof(struct nexus_channel_om_accessory_action_body));

    // accessory truncated ID actually 0, 6 should not find a match
    message.body.accessory_action.trunc_acc_id.digits_count = 1;
    message.body.accessory_action.trunc_acc_id.digits_int = 6;

    // give valid message with ID 15
    const bool valid = _nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY);
    TEST_ASSERT_FALSE(valid);
}

void test_nexus_channel_om_ascii_infer_fields_compute_auth__unlink_specific_accessory_invalid_truncated_id__missing_digits_count__unsuccessful(
    void)
{
    // create a window that has default settings, no IDs set
    struct nexus_window window;
    uint8_t flag_array[4] = {0, 0, 0, 0};
    nexus_util_window_init(&window, flag_array, sizeof(flag_array), 31, 31, 8);

    // XXX test relies on hard-coded representation of an accessory link
    // already existing.
    // TODO modify if we dynamically provide linked accessory info
    // parsed representation of
    // VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLOCK_ACCESSORY
    // computed ID is given a nonsense value (should be overwritten)
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK;
    message.body.controller_action.action_type = 0;
    message.auth.six_int_digits = 536545;
    message.computed_command_id = 0xFFFFFFFF;

    // truncated ID is correct, but digits count is corrupt/invalid.
    message.body.accessory_action.trunc_acc_id.digits_count = 0;
    message.body.accessory_action.trunc_acc_id.digits_int = 0;

    // give valid message with ID 15
    const bool valid = _nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY);
    TEST_ASSERT_FALSE(valid);
}

void test_nexus_channel_om_ascii_infer_fields_compute_auth__link_command_mode_3__successful(
    void)
{
    // create a window that has default settings, no IDs set
    struct nexus_window window;
    uint8_t flag_array[4] = {0, 0, 0, 0};
    nexus_util_window_init(&window, flag_array, sizeof(flag_array), 31, 31, 8);

    // XXX test relies on hard-coded representation of an accessory link
    // already existing.
    // TODO modify if we dynamically provide linked accessory info
    // parsed representation of
    // VALID_ASCII_ORIGIN_ACCESSORY_ACTION_UNLOCK_ACCESSORY
    // computed ID is given a nonsense value (should be overwritten)
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3;
    message.body.create_link.trunc_acc_id.digits_count = 1;
    message.body.create_link.trunc_acc_id.digits_int = 0;
    message.body.create_link.accessory_challenge.six_int_digits = 382847;
    message.computed_command_id = 0xFFFFFFFF;
    message.auth.six_int_digits = 429307;

    // give valid message with ID 15
    const bool valid = _nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY);
    TEST_ASSERT_EQUAL(15, message.computed_command_id);
    TEST_ASSERT_TRUE(valid);

    // attempt with a different auth field (should be rejected)
    message.auth.six_int_digits = 123456;
    TEST_ASSERT_FALSE(_nexus_channel_om_ascii_infer_fields_compute_auth(
        &message, &window, &CONTROLLER_KEY));
}

void test__nexus_channel_om_ascii_apply_message__common_rejects_command__return_false(
    void)
{
    // valid message, but will be 'rejected' for unrelated reasons by Nexus
    // common
    struct nexus_channel_om_command_message input_msg = {
        // LinkCommandToken(9, '2382847', '173346',))
        NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
        // om_command_body (union)
        {.create_link = {.trunc_acc_id = {2, 1},
                         .accessory_challenge = {382847}}},
        {339665}, // auth
        0,
    };

    // ensure that ID 0 is always in the window (haven't exceeded center)
    TEST_ASSERT_TRUE(_nexus_channel_om_is_command_index_in_window(0));
    // Id should not be set yet
    TEST_ASSERT_TRUE(_nexus_channel_om_is_command_index_in_window(
        input_msg.computed_command_id));

    nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
    // assume that handshake manager always accepts the handshake, focus
    // on origin manager behavior here.
    nexus_channel_core_apply_origin_command_ExpectAndReturn(&input_msg, false);

    // accepts message, sets ID
    TEST_ASSERT_FALSE(_nexus_channel_om_ascii_apply_message(&input_msg));
}

void test__nexus_channel_om_ascii_apply_message__fill_left_window_and_move_one__first_id_no_longer_valid(
    void)
{
    struct test_scenario
    {
        struct nexus_channel_om_command_message input_msg;
        uint32_t command_id;
    };
    // generated using CONTROLLER_KEY and ACCESSORY_KEY at top of test module,
    // controller command count increments each time.
    // accessory command count fixed at 2 (so accessory challenge should
    // be the same in each case...)
    struct test_scenario scenarios[] = {
        {{
             // LinkCommandToken(9, '2382847', '173346',))
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {382847}}},
             {339665}, // auth
             0,
         },
         4},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {382847}}},
             {632168}, // auth
             0,
         },
         2},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {382847}}},
             {411721}, // auth
             0,
         },
         1},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {382847}}},
             {470303}, // auth
             0,
         },
         9},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {382847}}},
             {279227}, // auth
             0,
         },
         22},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {382847}}},
             {245606}, // auth
             0,
         },
         8},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {382847}}},
             {472745}, // auth
             0,
         },
         30},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {382847}}},
             {502818}, // auth
             29,
         },
         29},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {382847}}},
             {26217}, // auth
             31, // center index
         },
         31}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // ensure that ID 0 is always in the window (haven't exceeded center)
        TEST_ASSERT_TRUE(_nexus_channel_om_is_command_index_in_window(0));
        struct test_scenario scenario = scenarios[i];
        // Id should not be set yet
        TEST_ASSERT_TRUE(_nexus_channel_om_is_command_index_in_window(
            scenario.input_msg.computed_command_id));
        TEST_ASSERT_FALSE(
            _nexus_channel_om_is_command_index_set(scenario.command_id));

        nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
        // assume that handshake manager always accepts the handshake, focus
        // on origin manager behavior here.
        nexus_channel_core_apply_origin_command_ExpectAndReturn(
            &scenario.input_msg, true);

        // accepts message, sets ID
        TEST_ASSERT_TRUE(
            _nexus_channel_om_ascii_apply_message(&scenario.input_msg));

        // applying the message also 'infers' the message ID, ensure it matches
        // and is applied
        TEST_ASSERT_EQUAL(scenario.command_id,
                          scenario.input_msg.computed_command_id);
        TEST_ASSERT_TRUE(
            _nexus_channel_om_is_command_index_set(scenario.command_id));

        // should fail if reapplied
        nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
        TEST_ASSERT_FALSE(
            _nexus_channel_om_ascii_apply_message(&scenario.input_msg));
    }

    // ensure we can still reach ID 0
    TEST_ASSERT_TRUE(_nexus_channel_om_is_command_index_in_window(0));
    TEST_ASSERT_FALSE(_nexus_channel_om_is_command_index_set(0));

    // move by 1 to 32, 0 is out of range, 1 is in range (but still set)
    struct nexus_channel_om_command_message msg_32 = {
        NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
        // om_command_body (union)
        {.create_link = {.trunc_acc_id = {2, 1},
                         .accessory_challenge = {382847}}},
        {525252}, // auth
        0};

    nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
    // assume that handshake manager always accepts the handshake, focus
    // on origin manager behavior here.
    nexus_channel_core_apply_origin_command_ExpectAndReturn(&msg_32, true);
    // accepts message, sets ID
    TEST_ASSERT_TRUE(_nexus_channel_om_ascii_apply_message(&msg_32));

    // no longer in window
    TEST_ASSERT_FALSE(_nexus_channel_om_is_command_index_in_window(0));
    TEST_ASSERT_FALSE(_nexus_channel_om_is_command_index_set(0));

    // all previous test scenarios are still set, but cannot be reapplied
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // 0 is no longer in the window
        TEST_ASSERT_FALSE(_nexus_channel_om_is_command_index_in_window(0));
        struct test_scenario scenario = scenarios[i];

        // ID should already be set, and still in window (for previous
        // test IDs between 1 and 32)
        TEST_ASSERT_TRUE(
            _nexus_channel_om_is_command_index_in_window(scenario.command_id));
        TEST_ASSERT_TRUE(
            _nexus_channel_om_is_command_index_set(scenario.command_id));

        nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);

        // can't apply message (already applied)
        TEST_ASSERT_FALSE(
            _nexus_channel_om_ascii_apply_message(&scenario.input_msg));

        // ensure the command IDs are still set
        TEST_ASSERT_TRUE(
            _nexus_channel_om_is_command_index_set(scenario.command_id));
    }
}

void test__nexus_channel_om_ascii_apply_message__move_window_over_hundred__right_edge_accepted_correctly(
    void)
{
    struct test_scenario
    {
        struct nexus_channel_om_command_message input_msg;
        uint32_t command_id;
    };
    // generated using CONTROLLER_KEY and ACCESSORY_KEY at top of test module,
    // controller command count increments each time.
    // accessory command count fixed at 17 (so accessory challenge should
    // be the same in each case...)
    struct test_scenario scenarios[] = {
        {{
             // LinkCommandToken(9, '2382847', '173346',))
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {724871}}},
             {900378}, // auth
             0,
         },
         39},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {724871}}},
             {290601}, // auth
             0,
         },
         47},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {724871}}},
             {169248}, // auth
             0, // center index
         },
         55},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {724871}}},
             {466213}, // auth
             0,
         },
         63},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {724871}}},
             {739934}, // auth
             0,
         },
         71},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {724871}}},
             {40877}, // auth
             0,
         },
         79},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {724871}}},
             {958743}, // auth
             0,
         },
         87},
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {724871}}},
             {960262}, // auth
             0,
         },
         95},
        // shouldn't matter, but throw in a changed accessory challenge with
        // leading zeroes. (generated with accessory command count = 18)
        {{
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3, // type
                                                                         // om_command_body
                                                                         // (union)
             {.create_link = {.trunc_acc_id = {2, 1},
                              .accessory_challenge = {9616}}},
             {935755}, // auth
             0,
         },
         103},
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct test_scenario scenario = scenarios[i];
        // Id should not be set yet
        TEST_ASSERT_TRUE(
            _nexus_channel_om_is_command_index_in_window(scenario.command_id));
        TEST_ASSERT_FALSE(
            _nexus_channel_om_is_command_index_set(scenario.command_id));

        nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
        // assume that handshake manager always accepts the handshake, focus
        // on origin manager behavior here.
        nexus_channel_core_apply_origin_command_ExpectAndReturn(
            &scenario.input_msg, true);

        // accepts message, sets ID
        TEST_ASSERT_TRUE(
            _nexus_channel_om_ascii_apply_message(&scenario.input_msg));

        // applying the message also 'infers' the message ID, ensure it matches
        // and is applied
        TEST_ASSERT_EQUAL(scenario.command_id,
                          scenario.input_msg.computed_command_id);
        TEST_ASSERT_TRUE(
            _nexus_channel_om_is_command_index_set(scenario.command_id));

        // should fail if reapplied
        nxp_channel_symmetric_origin_key_ExpectAndReturn(CONTROLLER_KEY);
        TEST_ASSERT_FALSE(
            _nexus_channel_om_ascii_apply_message(&scenario.input_msg));
    }
}

#pragma GCC diagnostic pop

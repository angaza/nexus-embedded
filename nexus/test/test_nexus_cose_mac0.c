#include "include/nx_common.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/engine.h"
#include "messaging/coap/transactions.h"
#include "oc/api/oc_main.h"
#include "oc/include/oc_api.h"
#include "oc/include/oc_buffer.h"
#include "oc/include/oc_core_res.h"
#include "oc/include/oc_endpoint.h"
#include "oc/include/oc_helpers.h"
#include "oc/include/oc_network_events.h"
#include "oc/include/oc_rep.h"
#include "oc/include/oc_ri.h"
#include "oc/util/oc_etimer.h"
#include "oc/util/oc_memb.h"
#include "oc/util/oc_mmem.h"
#include "oc/util/oc_process.h"
#include "oc/util/oc_timer.h"
#include "src/nexus_channel_core.h"
#include "src/nexus_channel_res_link_hs.h"
#include "src/nexus_channel_res_lm.h"
#include "src/nexus_channel_sm.h"
#include "src/nexus_common_internal.h"
#include "src/nexus_cose_mac0_common.h"
#include "src/nexus_cose_mac0_sign.h"
#include "src/nexus_cose_mac0_verify.h"
#include "src/nexus_nv.h"
#include "src/nexus_oc_wrapper.h"
#include "src/nexus_security.h"
#include "src/nexus_util.h"
#include "unity.h"
#include "utils/crc_ccitt.h"
#include "utils/oc_list.h"
#include "utils/oc_uuid.h"
#include "utils/siphash_24.h"

// Other support libraries
#include <mock_nexus_channel_om.h>
#include <mock_nexus_channel_res_payg_credit.h>
#include <mock_nexus_keycode_core.h>
#include <mock_nxp_channel.h>
#include <mock_nxp_common.h>
#include <mock_nxp_keycode.h>
#include <stdbool.h>
#include <string.h>

/********************************************************
 * DEFINITIONS
 *******************************************************/

// added for Nexus testing, in `oc_mmem.c`
extern void oc_nexus_testing_reinit_mmem_lists(void);
/********************************************************
 * PRIVATE TYPES
 *******************************************************/

/********************************************************
 * PRIVATE DATA
 *******************************************************/

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/
// pull in source file from IoTivity without changing its name
// https://github.com/ThrowTheSwitch/Ceedling/issues/113
TEST_FILE("oc/api/oc_server_api.c")
TEST_FILE("oc/api/oc_client_api.c")
TEST_FILE("oc/deps/tinycbor/cborencoder.c")
TEST_FILE("oc/deps/tinycbor/cborparser.c")

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    // no setup required
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
}

void test_nexus_cose_mac0___nexus_cose_mac0_encode_protected_header_map__valid_nonces__cbor_map_expected(
    void)
{
    struct test_scenario
    {
        const uint32_t nonce;
        const char* expect_cbor;
        const uint8_t expect_len;
    };

    const struct test_scenario scenarios[] = {
        {0, "\xA1\x05\x00", 3},
        {0xFFFFFFFF, "\xA1\x05\x1A\xFF\xFF\xFF\xFF", 7},
        {65, "\xA1\x05\x18\x41", 4},
        {12345678, "\xA1\x05\x1A\x00\xBC\x61\x4E", 7},
        {0x00FA00FD, "\xA1\x05\x1A\x00\xFA\x00\xFD", 7},
        {0xFA00FD00, "\xA1\x05\x1A\xFA\x00\xFD\x00", 7},
        {0xFAFB, "\xA1\x05\x19\xFA\xFB", 5}};

    uint8_t result_buffer[NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE];

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        memset(result_buffer, 0xFA, sizeof(result_buffer));
        struct test_scenario scenario = scenarios[i];
        uint8_t length = nexus_cose_mac0_encode_protected_header_map(
            scenario.nonce,
            result_buffer,
            NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE);

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            scenario.expect_cbor, result_buffer, scenario.expect_len);
        TEST_ASSERT_EQUAL_UINT(scenario.expect_len, length);
    }
}

void test_nexus_cose_mac0___nexus_cose_mac0_compute_tag__expected_mac_for_given_inputs(
    void)
{
    struct test_scenario
    {
        const struct nexus_cose_mac0_cbor_data_t mac_struct;
        const struct nx_common_check_key key;
        const struct nexus_check_value expected_tag;
    };

    const struct test_scenario scenarios[] = {
        // scenario
        {
            {
                // ["MAC0", h'A10500', h'022F746573742F757269', h'987654FF00AB']
                {0x84, 0x64, 0x4D, 0x41, 0x43, 0x30, 0x43, 0xA1, 0x05, 0x00,
                 0x4A, 0x02, 0x2F, 0x74, 0x65, 0x73, 0x74, 0x2F, 0x75, 0x72,
                 0x69, 0x46, 0x98, 0x76, 0x54, 0xFF, 0x00, 0xAB},
                28,
            },
            NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
            {{0xd1, 0x3c, 0x8b, 0x4e, 0xe7, 0x39, 0x78, 0x72}},
        },
        // different key -> different tag
        {
            {
                // ["MAC0", h'A10500', h'022F746573742F757269', h'987654FF00AB']
                {0x84, 0x64, 0x4D, 0x41, 0x43, 0x30, 0x43, 0xA1, 0x05, 0x00,
                 0x4A, 0x02, 0x2F, 0x74, 0x65, 0x73, 0x74, 0x2F, 0x75, 0x72,
                 0x69, 0x46, 0x98, 0x76, 0x54, 0xFF, 0x00, 0xAB},
                28,
            },
            NEXUS_INTEGRITY_CHECK_FIXED_00_KEY,
            {{0x6b, 0xab, 0x52, 0x35, 0x81, 0xfb, 0xad, 0xf1}},
        },
        // different nonce -> different tag
        {
            {
                // ["MAC0", h'A10501', h'022F746573742F757269', h'987654FF00AB']
                {0x84, 0x64, 0x4D, 0x41, 0x43, 0x30, 0x43, 0xA1, 0x05, 0x01,
                 0x4A, 0x02, 0x2F, 0x74, 0x65, 0x73, 0x74, 0x2F, 0x75, 0x72,
                 0x69, 0x46, 0x98, 0x76, 0x54, 0xFF, 0x00, 0xAB},
                28,
            },
            NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
            {{0xe0, 0xf6, 0xa9, 0x66, 0x8b, 0xb5, 0x4c, 0x1e}},
        }};

    struct nexus_check_value result;

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        memset(&result, 0xFA, sizeof(result));
        struct test_scenario scenario = scenarios[i];

        result = nexus_cose_mac0_common_compute_tag(&scenario.mac_struct,
                                                    &scenario.key);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(scenario.expected_tag.bytes,
                                      result.bytes,
                                      sizeof(struct nexus_check_value));
    }
}

void test_nexus_cose_mac0___nexus_cose_mac0_encode_protected_header_map__valid_nonces__buffer_too_small__fails(
    void)
{
    struct test_scenario
    {
        const uint32_t nonce;
        const char* expect_cbor;
        const uint8_t expect_len;
    };

    const struct test_scenario scenarios[] = {
        {0, "\xA1\x05\x00", 3},
        {0xFFFFFFFF, "\xA1\x05\x1A\xFF\xFF\xFF\xFF", 7},
        {65, "\xA1\x05\x18\x41", 4},
        {12345678, "\xA1\x05\x1A\x00\xBC\x61\x4E", 7},
        {0x00FA00FD, "\xA1\x05\x1A\x00\xFA\x00\xFD", 7},
        {0xFA00FD00, "\xA1\x05\x1A\xFA\x00\xFD\x00", 7},
        {0xFAFB, "\xA1\x05\x19\xFA\xFB", 5}};

    uint8_t result_buffer[NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE];

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // fill with nonsense data
        memset(result_buffer, 0xFA, sizeof(result_buffer));

        struct test_scenario scenario = scenarios[i];
        uint8_t length = nexus_cose_mac0_encode_protected_header_map(
            scenario.nonce,
            result_buffer,
            NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE - 1);

        TEST_ASSERT_EQUAL_UINT(0, length);
    }
}

void test_nexus_cose_mac0____nexus_cose_mac0_payload_ctx_to_mac_structure__valid_input_mac_structure_ok(
    void)
{
    struct test_scenario
    {
        const nexus_cose_mac0_common_macparams_t input;
        const struct nexus_cose_mac0_cbor_data_t expect_mac_struct;
    };

    uint8_t dummy_payload[6] = {0x98, 0x76, 0x54, 0xFF, 0x00, 0xAB};
    uint8_t too_big_payload[200] = {0};

    struct test_scenario scenarios[] = {
        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    1, // GET
                    (uint8_t*) "/test/uri",
                    9,
                },
                // no payload (zero length GET)
                &dummy_payload[0],
                0,
            },
            // expect_mac_struct
            {
                // ["MAC0", h'A10500', h'012F746573742F757269', h'']
                {0x84, 0x64, 0x4D, 0x41, 0x43, 0x30, 0x43, 0xA1,
                 0x05, 0x00, 0x4A, 0x01, 0x2F, 0x74, 0x65, 0x73,
                 0x74, 0x2F, 0x75, 0x72, 0x69, 0x40},
                22,
            },
        },
        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    255, // unknown large method code, however, this layer
                    // (COSE MAC0) doesn't care or check if the codes are
                    // valid - this is still a valid message
                    (uint8_t*) "/test/uri",
                    9,
                },
                // no payload
                &dummy_payload[0],
                0,
            },
            // expect_mac_struct
            {
                // ["MAC0", h'A10500', h'FF2F746573742F757269', h'']
                {0x84, 0x64, 0x4D, 0x41, 0x43, 0x30, 0x43, 0xA1,
                 0x05, 0x00, 0x4A, 0xFF, 0x2F, 0x74, 0x65, 0x73,
                 0x74, 0x2F, 0x75, 0x72, 0x69, 0x40},
                22,
            },
        },

        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    1, // GET
                    (uint8_t*) "/this/uri/too/long/wont/x",
                    NEXUS_CHANNEL_MAX_HUMAN_READABLE_URI_LENGTH + 1,
                },
                // no payload (zero length GET)
                &dummy_payload[0],
                0,
            },
            // expect_mac_struct
            {
                // fails to encode
                {0},
                0,
            },
        },
        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    2, // POST
                    (uint8_t*) "/test/uri",
                    9,
                },
                // valid payload
                &dummy_payload[0],
                sizeof(dummy_payload),
            },
            // expect_mac_struct
            {
                // ["MAC0", h'A10500', h'022F746573742F757269', h'987654FF00AB']
                {0x84, 0x64, 0x4D, 0x41, 0x43, 0x30, 0x43, 0xA1, 0x05, 0x00,
                 0x4A, 0x02, 0x2F, 0x74, 0x65, 0x73, 0x74, 0x2F, 0x75, 0x72,
                 0x69, 0x46, 0x98, 0x76, 0x54, 0xFF, 0x00, 0xAB},
                28,
            },
        },
        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    2, // POST
                    (uint8_t*) "/test/uri",
                    9,
                },
                &too_big_payload[0],
                sizeof(too_big_payload),
            },
            // expect_mac_struct
            {
                // fails, payload too big
                {0},
                0,
            },
        }

    };

    struct nexus_cose_mac0_cbor_data_t result_mac_struct;

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct test_scenario scenario = scenarios[i];
        memset(&result_mac_struct, 0xFA, sizeof(result_mac_struct));

        nexus_cose_error result =
            nexus_cose_mac0_common_mac_params_to_mac_structure(
                &scenario.input, &result_mac_struct);

        if (scenario.expect_mac_struct.len > 0)
        {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(scenario.expect_mac_struct.buf,
                                          result_mac_struct.buf,
                                          scenario.expect_mac_struct.len);
            TEST_ASSERT_EQUAL(result, NEXUS_COSE_ERROR_NONE);
        }
        else
        {
            TEST_ASSERT_NOT_EQUAL(result, NEXUS_COSE_ERROR_NONE);
        }
    }
}

void test_nexus_cose_mac0__nexus_cose_mac0_verify_deserialize_protected_message__various_scenarios_expected_results(
    void)
{
    struct test_scenario
    {
        const struct nexus_cose_mac0_cbor_data_t secured_message;
        const nexus_cose_error expect_result;

        // Represented CBOR bytes
        const uint32_t expect_nonce;
        const char* expect_payload;
        const struct nexus_check_value expect_tag;
    };

    struct test_scenario scenarios[] = {
        // scenario
        {
            // secured message (0 length payload)
            {
                // [h'A10500', {}, h'', h'D13C8B4EE7397872']
                {0x84,
                 0x43,
                 0xA1,
                 0x05,
                 0x00,
                 0xA0,
                 0x40,
                 0x48,
                 0xD1,
                 0x3C,
                 0x8B,
                 0x4E,
                 0xE7,
                 0x39,
                 0x78,
                 0x72},
                16,
            },
            NEXUS_COSE_ERROR_NONE,
            0,
            "",
            {{0xD1, 0x3C, 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72}},
        },
        // scenario
        {
            // secured message (100 length payload, nonce 54)
            /* [h'A1051836', {},
                       h'0102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE00009119',
                       h'21CFE26730ADCA3C']
           */
            {
                {0x84, 0x44, 0xa1, 0x05, 0x18, 0x36, 0xa0, 0x58, 0x64, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x48,
                 0x21, 0xcf, 0xe2, 0x67, 0x30, 0xad, 0xca, 0x3c},
                118,
            },
            NEXUS_COSE_ERROR_NONE,
            54,
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91\x19"
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91\x19"
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91\x19"
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91\x19"
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91\x19",
            {{0x21, 0xcf, 0xe2, 0x67, 0x30, 0xad, 0xca, 0x3c}},
        },
        // scenario (large payload with largest possible nonce)
        {
            // secured message (99 length payload, nonce 0xFFFFFFFF)
            /* [h'A1051AFFFFFFFF', {},
             * h'0102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091',
             * h'8008AB062FDC761D']
             */
            {
                {0x84, 0x47, 0xa1, 0x05, 0x1a, 0xff, 0xff, 0xff, 0xff, 0xa0,
                 0x58, 0x63, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00,
                 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00,
                 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00,
                 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00,
                 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00,
                 0x91, 0x48, 0x80, 0x08, 0xAB, 0x06, 0x2F, 0xDC, 0x76, 0x1D},
                120,
            },
            NEXUS_COSE_ERROR_NONE,
            0xFFFFFFFF,
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91\x19"
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91\x19"
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91\x19"
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91\x19"
            "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\xff\xaa\xbb\xcc\xdd\xee"
            "\x00\x00\x91",
            {{0x80, 0x08, 0xAB, 0x06, 0x2F, 0xDC, 0x76, 0x1D}},
        },
        {
            {
                // [h'A10500', {}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x43, 0xA1, 0x05, 0x00, 0xA0, 0x46, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                22,
            },
            NEXUS_COSE_ERROR_NONE,
            0,
            "\x98\x76\x54\xFF\x00\xAB",
            {{0xD1, 0x3C, 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72}},

        },

        {
            {
                // [h'A10500', {}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x43, 0xA1, 0x05, 0x00, 0xA0, 0x46, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                22,
            },
            NEXUS_COSE_ERROR_NONE,
            0,
            "\x98\x76\x54\xFF\x00\xAB",
            {{0xD1, 0x3C, 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72}},

        },
        // incorrect length (5), parser will reach EOF before running out of
        // bytes
        {
            {
                // [h'A10500', {}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x43, 0xA1, 0x05, 0x00, 0xA0, 0x46, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                5, // should be 22
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // Invalid length (0x59 instead of 0x43) for first array element
        {
            {
                // [h'A10500', {}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x59, 0xA1, 0x05, 0x00, 0xA0, 0x46, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                22,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // first array element has the wrong length (0x44 4 bytes instead of 3
        // bytes...)
        {
            {
                {0x84, 0x44, 0xA1, 0x05, 0x00, 0xA0, 0x46, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                22,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // protected header bstr length is too large
        // (exceeds `NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE` so we
        // fail to parse)
        {
            {
                // [h'0102030405060708090A0102030405060708090A0102030405060708090ACC',
                // {}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x58, 0x1f, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                 0x07, 0x08, 0x09, 0x0a, 0x01, 0x02, 0x03, 0x04, 0x05,
                 0x06, 0x07, 0x08, 0x09, 0x0a, 0x01, 0x02, 0x03, 0x04,
                 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xcc, 0xa0, 0x46,
                 0x98, 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                51,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // protected header bytestring is not parseable as a map
        {
            {
                // [h'010203', {}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x43, 0x01, 0x02, 0x03, 0xA0, 0x46, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                22,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // second element is not a map (unprotected header)
        {
            {
                // [h'A10500', 17, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x43, 0xA1, 0x05, 0x00, 0x11, 0x46, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                22,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // third element is not a bytestring (payload)
        {
            {
                // [h'A10500', {}, 17, h'D13C8B4EE7397872']
                {0x84,
                 0x43,
                 0xA1,
                 0x05,
                 0x00,
                 0xA0,
                 0x11,
                 0x48,
                 0xD1,
                 0x3C,
                 0x8B,
                 0x4E,
                 0xE7,
                 0x39,
                 0x78,
                 0x72},
                16,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // Invalid length (0x59 instead of 0x46) for third array element
        // third element is not a bytestring (payload)
        {
            {
                // [h'A10500', {}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x43, 0xA1, 0x05, 0x00, 0xA0, 0x59, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                22,
            },
            NEXUS_COSE_ERROR_CBOR_PARSER,
            0,
            "",
            {{0}},
        },
        // fourth element is not a bytestring
        {
            {
                // [h'A10500', {}, h'987654FF00AB', 17]
                {0x84,
                 0x43,
                 0xA1,
                 0x05,
                 0x00,
                 0xA0,
                 0x46,
                 0x98,
                 0x76,
                 0x54,
                 0xFF,
                 0x00,
                 0xAB,
                 0x11},
                14,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // fourth element is zero-length bytestring (tag/MAC missing...)
        {
            {
                // [h'A10500', {}, h'987654FF00AB', h'']
                {0x84,
                 0x43,
                 0xA1,
                 0x05,
                 0x00,
                 0xA0,
                 0x46,
                 0x98,
                 0x76,
                 0x54,
                 0xFF,
                 0x00,
                 0xAB,
                 0x40},
                14,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // array too few elements (3), fails
        {
            {
                // [{}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x83,
                 0xA0,
                 0x46,
                 0x98,
                 0x76,
                 0x54,
                 0xFF,
                 0x00,
                 0xAB,
                 0x48,
                 0xD1,
                 0x3C,
                 0x8B,
                 0x4E,
                 0xE7,
                 0x39,
                 0x78,
                 0x72},
                22,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        },
        // missing nonce/protected header contents, fails
        {
            {
                // [h'', {}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x40, 0xA1, 0x05, 0x00, 0xA0, 0x46, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                22,
            },
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
            0,
            "",
            {{0}},
        }};

    nexus_cose_mac0_extracted_cose_params_t result;

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        memset(&result, 0xFA, sizeof(result));
        struct test_scenario scenario = scenarios[i];

        nexus_cose_error deser_result =
            nexus_cose_mac0_verify_deserialize_protected_message(
                scenario.secured_message.buf,
                scenario.secured_message.len,
                &result);

        TEST_ASSERT_EQUAL(scenario.expect_result, deser_result);

        if (deser_result == NEXUS_COSE_ERROR_NONE)
        {
            TEST_ASSERT_EQUAL_UINT(scenario.expect_nonce, result.nonce);
            if (result.payload_len > 0)
            {
                TEST_ASSERT_EQUAL_UINT8_ARRAY(scenario.expect_payload,
                                              result.payload,
                                              result.payload_len);
            }
            else
            {
                TEST_ASSERT_EQUAL(0, strlen(scenario.expect_payload));
            }

            TEST_ASSERT_EQUAL_UINT8_ARRAY(scenario.expect_tag.bytes,
                                          result.tag.bytes,
                                          sizeof(struct nexus_check_value));
        }
        else
        {
            TEST_ASSERT_EQUAL(scenario.expect_result, deser_result);
        }
    }
}

void test_nexus_cose_mac0___nexus_cose_mac0_input_and_tag_to_nexus_cose_mac0_message_t__various_inputs_expected_result(
    void)
{
    struct test_scenario
    {
        const nexus_cose_mac0_common_macparams_t input;
        const struct nexus_check_value tag;
        const struct nexus_cose_mac0_cbor_data_t expect_secured_message;
    };

    uint8_t dummy_payload[6] = {0x98, 0x76, 0x54, 0xFF, 0x00, 0xAB};
    uint8_t too_big_payload[200] = {0};

    struct test_scenario scenarios[] = {
        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    1, // GET
                    (uint8_t*) "/test/uri",
                    9,
                },
                // no payload (zero length GET)
                &dummy_payload[0],
                0,
            },
            {
                {0xd1, 0x3c, 0x8b, 0x4e, 0xe7, 0x39, 0x78, 0x72},
            },
            // expect_secured_message
            {
                // [h'A10500', {}, h'', h'D13C8B4EE7397872']
                {0x84,
                 0x43,
                 0xA1,
                 0x05,
                 0x00,
                 0xA0,
                 0x40,
                 0x48,
                 0xD1,
                 0x3C,
                 0x8B,
                 0x4E,
                 0xE7,
                 0x39,
                 0x78,
                 0x72},
                16,
            },
        },
        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    2, // POST
                    (uint8_t*) "/test/uri",
                    9,
                },
                &dummy_payload[0],
                sizeof(dummy_payload),
            },
            {
                {0xd1, 0x3c, 0x8b, 0x4e, 0xe7, 0x39, 0x78, 0x72},
            },
            // expect_secured_message
            {
                // [h'A10500', {}, h'987654FF00AB', h'D13C8B4EE7397872']
                {0x84, 0x43, 0xA1, 0x05, 0x00, 0xA0, 0x46, 0x98,
                 0x76, 0x54, 0xFF, 0x00, 0xAB, 0x48, 0xD1, 0x3C,
                 0x8B, 0x4E, 0xE7, 0x39, 0x78, 0x72},
                22,
            },
        },
        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    2, // POST
                    (uint8_t*) "/test/uri",
                    9,
                },
                &too_big_payload[0],
                sizeof(too_big_payload),
            },
            {
                {0xd1, 0x3c, 0x8b, 0x4e, 0xe7, 0x39, 0x78, 0x72},
            },
            // expect_secured_message
            {
                // too large payload, didn't create output
                {0},
                0,
            },
        },
    };

    uint8_t secured_message_buf[200];
    size_t bytes_copied;

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        memset(&secured_message_buf, 0xFA, sizeof(secured_message_buf));
        bytes_copied = 0;
        struct test_scenario scenario = scenarios[i];

        nexus_cose_error result =
            _nexus_cose_mac0_sign_input_and_tag_to_nexus_cose_mac0_message_t(
                &scenario.input,
                &scenario.tag,
                secured_message_buf,
                sizeof(secured_message_buf) / sizeof(uint8_t),
                &bytes_copied);

        if (scenario.expect_secured_message.len > 0)
        {
            TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, result);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(scenario.expect_secured_message.buf,
                                          secured_message_buf,
                                          scenario.expect_secured_message.len);
            TEST_ASSERT_EQUAL(scenario.expect_secured_message.len,
                              bytes_copied);
        }
        else
        {
            TEST_ASSERT_NOT_EQUAL(NEXUS_COSE_ERROR_NONE, result);
        }
    }
}

void test_nexus_cose_mac0__nexus_cose_mac0_sign_encode_message__various_inputs_expected_result(
    void)
{
    struct test_scenario
    {
        const nexus_cose_mac0_common_macparams_t input;
        // caller doesn't use this struct but we can when writing test
        const struct nexus_cose_mac0_cbor_data_t expect_secured_message;
    };

    uint8_t dummy_payload[6] = {0x98, 0x76, 0x54, 0xFF, 0x00, 0xAB};

    uint8_t large_payload[100] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa,
        0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
        0x00, 0x00, 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa,
        0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
        0x00, 0x00, 0x91, 0x19,
    };

    uint8_t too_big_payload[200] = {0};

    struct test_scenario scenarios[] = {
        // scenario
        {
            // input
            {

                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    1, // GET
                    (uint8_t*) "/test/uri",
                    9,
                },
                // no payload (zero length GET)
                &dummy_payload[0],
                0,
            },
            // expect_secured_message
            {
                // [h'A10500', {}, h'', h'833CEE6839909431']
                {0x84,
                 0x43,
                 0xA1,
                 0x05,
                 0x00,
                 0xa0,
                 0x40,
                 0x48,
                 0x83,
                 0x3c,
                 0xee,
                 0x68,
                 0x39,
                 0x90,
                 0x94,
                 0x31},
                16,
            },
        },
        // scenario
        {
            // input
            {

                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                // nonce = 54
                54,
                // aad
                {
                    2, // POST
                    (uint8_t*) "/test/uri",
                    9,
                },
                // 80 byte payload
                &large_payload[0],
                80,
            },
            // expect_secured_message
            {
                /*
                 * [h'A1051836', {},
                 * h'0102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE00009119',
                 * h'9EE8CC770FCB8C84']
                 */
                {0x84, 0x44, 0xa1, 0x05, 0x18, 0x36, 0xa0, 0x58, 0x50, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x48,
                 0x9E, 0xE8, 0xCC, 0x77, 0x0F, 0xCB, 0x8C, 0x84},
                98,
            },
        },
        // scenario (largest possible nonce and subsequent largest payload)
        {
            // input
            {

                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                // nonce = UINT32_MAX
                0xFFFFFFFF,
                // aad
                {
                    2, // POST
                    (uint8_t*) "/test/uri",
                    9,
                },
                // 77 byte payload
                &large_payload[0],
                77,
            },
            // expect_secured_message
            {
                /*
                 * [h'A1051AFFFFFFFF', {},
                 * h'0102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE00',
                 * h'8310FBEE10BF2BAE']
                 */
                {0x84, 0x47, 0xa1, 0x05, 0x1a, 0xff, 0xff, 0xff, 0xff, 0xa0,
                 0x58, 0x4d, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00,
                 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00,
                 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00,
                 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x48,
                 0xCE, 0x0E, 0x3A, 0x30, 0xE5, 0x34, 0x0E, 0x9B},
                98,
            },
        },
        // scenario (largest possible nonce and payload too large by 1 byte)
        {
            // input
            {

                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                // nonce = UINT32_MAX
                0xFFFFFFFF,
                // aad
                {
                    2, // POST
                    (uint8_t*) "/test/uri",
                    9,
                },
                // 78 byte payload (77 is largest 'worst case' payload to
                // secure)
                &large_payload[0],
                78,
            },
            {
                // too large payload, could not create output
                {0},
                0,
            },
        },

        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                0,
                // aad
                {
                    2, // POST
                    (uint8_t*) "/test/uri",
                    9,
                },
                &too_big_payload[0],
                sizeof(too_big_payload),
            },
            // expect_secured_message
            {
                // too large payload, didn't create output
                {0},
                0,
            },
        }};

    uint8_t output_buf[200];
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        memset(&output_buf, 0xFA, sizeof(output_buf));
        struct test_scenario scenario = scenarios[i];

        size_t bytes_copied;
        nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
            &scenario.input, output_buf, sizeof(output_buf), &bytes_copied);

        if (scenario.expect_secured_message.len > 0)
        {
            TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(scenario.expect_secured_message.buf,
                                          output_buf,
                                          scenario.expect_secured_message.len);
            TEST_ASSERT_EQUAL_UINT(scenario.expect_secured_message.len,
                                   bytes_copied);
        }
        else
        {
            TEST_ASSERT_NOT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);
        }

        // confirm that if output buffer is too small, encode fails (arbitrarily
        // small '4' here). We don't have any guarantee of the value of
        // `bytes_copied` if the reutnr code is not an error.
        encode_result = nexus_cose_mac0_sign_encode_message(
            &scenario.input, output_buf, 4, &bytes_copied);
        TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_BUFFER_TOO_SMALL, encode_result);
    }
}

void test_nexus_cose_mac0__nexus_cose_mac0_verify_message__various_inputs_expected_result(
    void)
{
    struct test_scenario
    {
        nexus_cose_mac0_verify_ctx_t input;
        // caller doesn't use this struct but we can when writing test
        const nexus_cose_error expect_verified;
        const uint32_t expected_nonce;
        const struct nexus_cose_mac0_cbor_data_t expect_unsecured_message;
    };
    // secured GET message (no payload, nonce 0, secured with FIXED_FF_KEY)
    // [h'A10500', {}, h'', h'833CEE6839909431']
    uint8_t scenario_1_cose_bytes[] = {0x84,
                                       0x43,
                                       0xA1,
                                       0x05,
                                       0x00,
                                       0xa0,
                                       0x40,
                                       0x48,
                                       0x83,
                                       0x3c,
                                       0xee,
                                       0x68,
                                       0x39,
                                       0x90,
                                       0x94,
                                       0x31};

    // secured POST message (98 length payload, nonce 54, secured with
    // FIXED_FF_KEY)
    /* [h'A1051836', {},
               h'0102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE0019',
               h'BE92F3546935ED88']
   */
    uint8_t scenario_2_cose_bytes[] = {
        0x84, 0x44, 0xa1, 0x05, 0x18, 0x36, 0xa0, 0x58, 0x62, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd,
        0xee, 0x00, 0x00, 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91,
        0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd,
        0xee, 0x00, 0x00, 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x19, 0x48,
        0xbe, 0x92, 0xf3, 0x54, 0x69, 0x35, 0xed, 0x88};

    struct test_scenario scenarios[] = {
        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                // aad
                {
                    1, // GET
                    (uint8_t*) "/test/uri",
                    9,
                },
                scenario_1_cose_bytes,
                sizeof(scenario_1_cose_bytes),
            },
            NEXUS_COSE_ERROR_NONE,
            // nonce
            0,
            // expect_unsecured_message (0 length, was GET - no payload)
            {
                {0},
                0,
            },
        },
        // 2 - same as 1, with wrong key for parsing (fails)
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_00_KEY,
                // aad
                {
                    1, // GET
                    (uint8_t*) "/test/uri",
                    9,
                },
                scenario_1_cose_bytes,
                sizeof(scenario_1_cose_bytes),
            },
            NEXUS_COSE_ERROR_MAC_TAG_INVALID,
            // nonce
            0,
            // expect_unsecured_message (0 length, was GET - no payload)
            {
                {0},
                0,
            },
        },
        // scenario
        {
            // input
            {
                &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
                // aad
                {
                    2, // POST
                    (uint8_t*) "/test/uri",
                    9,
                },
                scenario_2_cose_bytes,
                sizeof(scenario_2_cose_bytes),
            },
            NEXUS_COSE_ERROR_NONE,
            // nonce
            54,
            // expect_unsecured_message (98 length POST payload)
            {
                {
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                    0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19,
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                    0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19,
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                    0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19,
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                    0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19,
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                    0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x19,
                },
                98,
            },
        },

    };

    uint8_t* unsecured_payload;
    size_t unsecured_payload_len;
    uint32_t output_nonce;
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        unsecured_payload = NULL;
        unsecured_payload_len = 0xFA;
        output_nonce = 0xFA;
        struct test_scenario scenario = scenarios[i];

        nexus_cose_error verify_error =
            nexus_cose_mac0_verify_message(&scenario.input,
                                           &output_nonce,
                                           &unsecured_payload,
                                           &unsecured_payload_len);

        TEST_ASSERT_EQUAL(scenario.expect_verified, verify_error);

        if (scenario.expect_verified == NEXUS_COSE_ERROR_NONE)
        {
            TEST_ASSERT_EQUAL_UINT(scenario.expected_nonce, output_nonce);
            if (scenario.expect_unsecured_message.len > 0)
            {
                TEST_ASSERT_EQUAL_UINT8_ARRAY(
                    scenario.expect_unsecured_message.buf,
                    unsecured_payload,
                    scenario.expect_unsecured_message.len);
                TEST_ASSERT_EQUAL_UINT(scenario.expect_unsecured_message.len,
                                       unsecured_payload_len);
            }
        }
    }
}

void test_nexus_cose_mac0__nexus_cose_mac0_verify_deserialize_protected_header__error_cases_handled(
    void)
{
    struct test_scenario
    {
        struct nexus_cose_mac0_cbor_data_t cbor_data;
        const nexus_cose_error expected_error;
    };

    struct test_scenario scenarios[] = {
        {
            // invalid map length of 0
            {{0xA0, 0x05, 0x18, 0x36}, 4},
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
        },
        {
            // map key is a bytestring, not integer
            // {h'05': 54}
            {{0xA1, 0x41, 0x05, 0x18, 0x36}, 5},
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
        },

        {
            // map value is a bytestring, not integer
            // {5: h'54'}
            {{0xA1, 0x05, 0x41, 0x54}, 4},
            NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
        },

        {
            // invalid map length of 2
            {{0xA2, 0x05, 0x18, 0x36}, 4},
            NEXUS_COSE_ERROR_CBOR_PARSER,
        },
    };

    // encode, should yield `scenario.expect_secured_message`
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct test_scenario scenario = scenarios[i];
        uint32_t nonce;
        nexus_cose_error result =
            _nexus_cose_mac0_verify_deserialize_protected_header(
                &nonce, scenario.cbor_data.buf, scenario.cbor_data.len);
        TEST_ASSERT_EQUAL(scenario.expected_error, result);
    }
}

void test_nexus_cose_mac0__nexus_cose_mac0_encode_then_verify_then_reencode__same_result_as_input(
    void)
{
    struct test_scenario
    {
        // key used by both encode and verify
        const struct nx_common_check_key* key;
        // CoAP method, URI, URI length used by both encode and verify
        const nexus_cose_mac0_common_external_aad_t aad;
        // nonce used by encode and later expected in verify result
        const uint32_t nonce;
        uint8_t* unprotected_payload;
        size_t unprotected_payload_len;

        // result after input is encoded
        struct nexus_cose_mac0_cbor_data_t expect_secured_message;
        // result after `expect_secured_message` is verified should be
        // `unprotected_payload` above
    };

    uint8_t test_payload[100] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa,
        0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
        0x00, 0x00, 0x91, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa,
        0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
        0x00, 0x00, 0x91, 0x19,
    };

    struct test_scenario scenarios[] = {
        // scenario
        {
            // key
            &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
            // aad
            {
                2, // POST
                (uint8_t*) "/test/uri",
                9,
            },
            // nonce = 54
            54,
            // payload
            &test_payload[0],
            80,

            // expect_secured_message
            {
                /* [h'A1051836', {},
                   h'0102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE000091190102030405060708090AFFAABBCCDDEE00009119',
                   h'9EE8CC770FCB8C84'] */
                {0x84, 0x44, 0xa1, 0x05, 0x18, 0x36, 0xa0, 0x58, 0x50, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x01,
                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x00, 0x00, 0x91, 0x19, 0x48,
                 0x9e, 0xe8, 0xcc, 0x77, 0x0f, 0xcb, 0x8c, 0x84},
                98,
            },
        },
    };
    // for encoding/signing step
    uint8_t output_buf[200];
    size_t bytes_copied;

    // for verification step
    uint8_t* unsecured_payload;
    size_t unsecured_payload_len;
    uint32_t output_nonce;

    // encode, should yield `scenario.expect_secured_message`
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct test_scenario scenario = scenarios[i];

        nexus_cose_mac0_common_macparams_t encode_params =
            // input
            {
                scenario.key,
                scenario.nonce,
                // aad
                {
                    scenario.aad.coap_method,
                    scenario.aad.coap_uri,
                    scenario.aad.coap_uri_len,
                },
                scenario.unprotected_payload,
                scenario.unprotected_payload_len,
            };

        memset(&output_buf, 0xFA, sizeof(output_buf));

        nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
            &encode_params, output_buf, sizeof(output_buf), &bytes_copied);

        TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(scenario.expect_secured_message.buf,
                                      output_buf,
                                      scenario.expect_secured_message.len);
        TEST_ASSERT_EQUAL_UINT(scenario.expect_secured_message.len,
                               bytes_copied);

        // now verify...
        unsecured_payload = NULL;
        unsecured_payload_len = 0xFA;
        output_nonce = 0xFA;

        nexus_cose_mac0_verify_ctx_t verify_ctx = {
            scenario.key,
            // aad
            {
                scenario.aad.coap_method,
                scenario.aad.coap_uri,
                scenario.aad.coap_uri_len,
            },
            scenario.expect_secured_message.buf,
            scenario.expect_secured_message.len,
        };

        nexus_cose_error verify_error =
            nexus_cose_mac0_verify_message(&verify_ctx,
                                           &output_nonce,
                                           &unsecured_payload,
                                           &unsecured_payload_len);

        TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, verify_error);
        TEST_ASSERT_EQUAL_UINT(scenario.nonce, output_nonce);
        if (scenario.unprotected_payload_len > 0)
        {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(scenario.unprotected_payload,
                                          unsecured_payload,
                                          scenario.unprotected_payload_len);
            TEST_ASSERT_EQUAL_UINT(scenario.unprotected_payload_len,
                                   unsecured_payload_len);
        }
    }
}

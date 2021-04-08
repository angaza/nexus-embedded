/** \file nexus_channel_res_link_hs.c
 * Nexus Channel Link Handshake OCF Resource (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_channel_res_link_hs.h"
#include "include/nxp_channel.h"
#include "include/nxp_common.h"
#include "src/nexus_channel_res_lm.h" // for link security data
#include "src/nexus_common_internal.h"
#include "src/nexus_nv.h"
#include "src/nexus_oc_wrapper.h"
#include "src/nexus_security.h"

#include "oc/include/oc_api.h"
#include "oc/include/oc_rep.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

// Abbreviated property names
const char* CHAL_DATA_SHORT_PROP_NAME = "cD";
const char* RESP_DATA_SHORT_PROP_NAME = "rD";
const char* CHAL_MODE_SHORT_PROP_NAME = "cM";
const char* LINK_SEC_MODE_SHORT_PROP_NAME = "lS";
const char* STATE_SHORT_PROP_NAME = "st";
const char* TIME_SINCE_INIT_SHORT_PROP_NAME = "tI";
const char* TIMEOUT_CONFIGURED_SHORT_PROP_NAME = "tT";
const char* SUPPORTED_LINK_SECURITY_MODES_SHORT_PROP_NAME = "sL";
const char* SUPPORTED_CHALLENGE_MODES_SHORT_PROP_NAME = "sC";

extern oc_event_callback_retval_t oc_ri_remove_client_cb(void* data);

    #define KEY_DERIVATION_MATERIAL_LENGTH_BYTES                               \
        (CHALLENGE_MODE_3_SALT_LENGTH_BYTES + 4)

    #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
// modes accessory will expose when performing link handshake
static const enum nexus_channel_link_security_mode
    supported_link_security_modes[1] = {
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24};

static const enum nexus_channel_link_handshake_challenge_mode
    supported_challenge_modes[1] = {
        NEXUS_CHANNEL_LINK_HANDSHAKE_CHALLENGE_MODE_0_CHALLENGE_RESULT};
    #endif /* NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE */

// RAM representation of this link handshake resource
// Handshakes are not persisted in NV, only an established link.
static struct
{
    #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
    // may be initiating handshakes with multiple accessories at once
    nexus_link_hs_controller_t
        clients[NEXUS_CHANNEL_SIMULTANEOUS_LINK_HANDSHAKES];
    #endif /* NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE */
    #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    nexus_link_hs_accessory_t server;

        // Stored/NV parameters specific to accessory follow
        // number of flags stored [16] / CHAR_BIT [8]
        #define NEXUS_CHANNEL_LINK_HS_MAX_RECEIVE_FLAG_BYTE 2
        // recognize up to 15 'link handshake counts/indexes' behind the current
        // center
        #define NEXUS_CHANNEL_LINK_HS_RECEIVE_WINDOW_BEFORE_CENTER_INDEX 15
        // and 8 ahead of the current index.
        #define NEXUS_CHANNEL_LINK_HS_RECEIVE_WINDOW_AFTER_CENTER_INDEX 8
    NEXUS_PACKED_STRUCT
    {
        // Used to prevent replay attacks with old handshakes. Specific
        // use of this field varies based on handshake challenge mode, but
        // it is expected that any successful handshake *will* increment
        // this field.
        uint32_t handshake_index;
        // store a history of 'previous' handshake count values,
        // to enable out-of-order handshake creation.
        uint8_t received_ids[NEXUS_CHANNEL_LINK_HS_MAX_RECEIVE_FLAG_BYTE];
    }
    stored_accessory;
    #endif /* NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE */
} _this;

// forward declarations
bool _nexus_channel_res_link_hs_link_mode_3_send_post(
    const nexus_link_hs_controller_t* client_hs);

NEXUS_IMPL_STATIC void _nexus_channel_res_link_hs_reset_server_state(void)
{
    #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    // Relies on fact that 'idle' is 0 for all values in the struct.
    memset(&_this.server, 0x00, sizeof(_this.server));
    #endif /* NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE */
}

void nexus_channel_res_link_hs_init(void)
{
    #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    _nexus_channel_res_link_hs_reset_server_state();
    // Accessories also load the `handshake_count` from NV, if present.
    memset(&_this.stored_accessory.received_ids,
           0x00,
           sizeof(_this.stored_accessory.received_ids));
    _this.stored_accessory.handshake_index =
        NEXUS_CHANNEL_LINK_HS_RECEIVE_WINDOW_BEFORE_CENTER_INDEX;

    // compile time checks
    NEXUS_STATIC_ASSERT(sizeof(_this.stored_accessory) ==
                            NX_COMMON_NV_BLOCK_2_LENGTH -
                                NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES,
                        "link manager: Invalid size for NV block");

    NEXUS_STATIC_ASSERT(
        NEXUS_CHANNEL_LINK_HS_RECEIVE_WINDOW_BEFORE_CENTER_INDEX + 1 ==
            NEXUS_CHANNEL_LINK_HS_MAX_RECEIVE_FLAG_BYTE * 8,
        "Receive flag window improperly sized");

    NEXUS_STATIC_ASSERT(
        (NEXUS_CHANNEL_LINK_HS_RECEIVE_WINDOW_AFTER_CENTER_INDEX +
         NEXUS_CHANNEL_LINK_HS_RECEIVE_WINDOW_BEFORE_CENTER_INDEX + 1) %
                8 ==
            0,
        "Channel link handshake window not divisible by 8, is window size "
        "incorrect?");

    NEXUS_STATIC_ASSERT(
        sizeof(_this.stored_accessory) % 2 == 0,
        "Packed struct for storage does not have a size divisible by 2.");

    (void) nexus_nv_read(NX_NV_BLOCK_CHANNEL_LINK_HS_ACCESSORY,
                         (uint8_t*) &(_this.stored_accessory));

    // Only accessories serve a handshake resource
    const oc_interface_mask_t if_mask_arr[] = {OC_IF_RW, OC_IF_BASELINE};
    const struct nx_channel_resource_props link_hs_props = {
        .uri = "/h",
        .resource_type = "angaza.com.nexus.link.hs",
        .rtr = 65001,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        .get_handler = nexus_channel_res_link_hs_server_get,
        .get_secured = false,
        .post_handler = nexus_channel_res_link_hs_server_post,
        .post_secured = false};

        #ifdef NEXUS_DEFINED_DURING_TESTING
    nx_channel_error result =
        #endif // ifdef NEXUS_DEFINED_DURING_TESTING
        nx_channel_register_resource(&link_hs_props);
        #ifdef NEXUS_DEFINED_DURING_TESTING
    NEXUS_ASSERT(result == NX_CHANNEL_ERROR_NONE,
                 "Unexpected error registering resource");
        #endif // ifdef NEXUS_DEFINED_DURING_TESTING
    #endif // NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
    memset(&_this.clients, 0x00, sizeof(_this.clients));
    #endif // NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
}

    // only used in unit tests
    #ifdef NEXUS_DEFINED_DURING_TESTING
        // Used internally in unit tests
        #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
NEXUS_IMPL_STATIC void _nexus_channel_res_link_hs_set_server_state(
    const nexus_link_hs_accessory_t* server_state)
{
    NEXUS_STATIC_ASSERT(sizeof(_this.server) ==
                            sizeof(nexus_link_hs_accessory_t),
                        "Invalid handshake server struct size");
    memcpy(&_this.server, server_state, sizeof(nexus_link_hs_accessory_t));
}
        #endif /* if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE */

        #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
NEXUS_IMPL_STATIC void _nexus_channel_res_link_hs_set_client_state(
    const nexus_link_hs_controller_t* client_state, uint8_t index)
{
    NEXUS_STATIC_ASSERT(sizeof(_this.clients) ==
                            sizeof(nexus_link_hs_controller_t) *
                                NEXUS_CHANNEL_SIMULTANEOUS_LINK_HANDSHAKES,
                        "Invalid handshake client struct size");
    NEXUS_ASSERT(index < NEXUS_CHANNEL_SIMULTANEOUS_LINK_HANDSHAKES,
                 "Invalid index to set");
    memcpy(&_this.clients[index],
           client_state,
           sizeof(nexus_link_hs_controller_t));
}

NEXUS_IMPL_STATIC nexus_link_hs_controller_t*
_nexus_channel_res_link_hs_get_client_state(uint8_t index)
{
    return &_this.clients[index];
}
        #endif /* if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE */
    #endif // NEXUS_DEFINED_DURING_TESTING

uint32_t nexus_channel_res_link_hs_process(uint32_t seconds_elapsed)
{
    uint32_t next_call_secs =
        NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS;

    // process pending accessory/server tasks
    #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    const enum nexus_channel_link_handshake_state server_state =
        _this.server.state;
    if (server_state != LINK_HANDSHAKE_STATE_IDLE)
    {
        // calculate time since we started the link handshake
        // Note: handshakes aren't expected to last more than a few minutes.
        NEXUS_ASSERT(seconds_elapsed < UINT16_MAX,
                     "unexpected time since last call");
        _this.server.seconds_since_init =
            (uint16_t)(_this.server.seconds_since_init + seconds_elapsed);

        if (_this.server.seconds_since_init >
            NEXUS_CHANNEL_LINK_HANDSHAKE_ACCESSORY_TIMEOUT_SECONDS)
        {
            // Go inactive, clear data. timed out
            _nexus_channel_res_link_hs_reset_server_state();
            nxp_channel_notify_event(
                NXP_CHANNEL_EVENT_LINK_HANDSHAKE_TIMED_OUT);
        }
        else
        {
            // We are active and did not time out, call back in 1 second
            next_call_secs = 1;
        }
    }
    #endif
    #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
    for (uint8_t i = 0; i < NEXUS_CHANNEL_SIMULTANEOUS_LINK_HANDSHAKES; i++)
    {
        // process any pending activity for each link handshake
        nexus_link_hs_controller_t* client_hs = &_this.clients[i];

        // Skip any idle/inactive handshake elements
        if (client_hs->state == LINK_HANDSHAKE_STATE_IDLE)
        {
            continue;
        }

        NEXUS_ASSERT(seconds_elapsed < UINT16_MAX,
                     "unexpected time since last call");
        client_hs->seconds_since_init =
            (uint16_t)(client_hs->seconds_since_init + seconds_elapsed);

        // Set any handshakes that have timed out to idle
        if (client_hs->seconds_since_init >
            NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_TIMEOUT_SECONDS)
        {
            PRINT("Timed out attempting to link to accessory.\n");
            // reset this specific client state to idle
            memset(client_hs, 0x00, sizeof(nexus_link_hs_controller_t));
        }
        else if (client_hs->state == LINK_HANDSHAKE_STATE_ACTIVE)
        {
            const uint16_t seconds_since_post = (uint16_t)(
                client_hs->seconds_since_init - client_hs->last_post_seconds);
            if (seconds_since_post >=
                NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_RETRY_SECONDS)
            {
                // we've started this handshake, but haven't got a response.
                // Try sending out the multicast message again.
                client_hs->last_post_seconds = client_hs->seconds_since_init;
                _nexus_channel_res_link_hs_link_mode_3_send_post(client_hs);
            }
        }

        // if we reach here, at least one client handshake is not idle,
        // so call back in 5 seconds. Allow previous value set by accessory
        // processing (if present) to override if smaller.
        next_call_secs =
            u32min(NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_RETRY_SECONDS,
                   next_call_secs);
    }
    #endif
    return next_call_secs;
}

// internal, used in handshake mode 0.
// Eventually, can move this to its own module with other challenge/handshake
// computation logic. While we have one mode implemented, keep it here for now.
// Warning: Assumes `salt` is CHALLENGE_MODE_3_SALT_LENGTH_BYTES in length.
static struct nexus_check_value _res_link_hs_mode0_compute_inverted_salt_mac(
    const uint8_t* salt, const struct nx_common_check_key* link_key)
{
    uint8_t inverted_salt[CHALLENGE_MODE_3_SALT_LENGTH_BYTES];
    for (uint8_t i = 0; i < CHALLENGE_MODE_3_SALT_LENGTH_BYTES; i++)
    {
        inverted_salt[i] = (*(salt + i)) ^ 0xFF;
    }
    const struct nexus_check_value result = nexus_check_compute(
        link_key, &inverted_salt, CHALLENGE_MODE_3_SALT_LENGTH_BYTES);
    return result;
}

// internal, may branch into separate security manager
// takes key derivation key and challenge data, returns derived link key
NEXUS_IMPL_STATIC struct nx_common_check_key _res_link_hs_generate_link_key(
    uint32_t challenge_int,
    const uint8_t* salt_bytes,
    uint8_t salt_len,
    const struct nx_common_check_key* derivation_key_a,
    const struct nx_common_check_key* derivation_key_b)
{
    NEXUS_STATIC_ASSERT(4 == sizeof(uint32_t), "Invalid size for uint32_t!");
    NEXUS_ASSERT(salt_len % 2 == 0, "Invalid salt length, cannot proceed");

    // 64-bits of salt to expand the challenge int, 32 bits of challenge int
    // initialize to 0 on startup, will be cleared at end of this function
    static uint8_t
        key_derivation_material[KEY_DERIVATION_MATERIAL_LENGTH_BYTES] = {0};

    // Obtain challenge int in little endian (for consistent computation,
    // always order its bytes in little endian)
    const uint32_t little_end_challenge = nexus_endian_htole32(challenge_int);
    // append bytes of the challenge result to the key derivation material

    // Fill key derivation material as:
    // [0..7] = Salt
    // [8..11] = challenge integer (from origin)
    memcpy(&key_derivation_material[0], salt_bytes, salt_len);
    memcpy(&key_derivation_material[8],
           (const void*) &little_end_challenge,
           sizeof(uint32_t));

    // Compute and return the link key using the key derivation key, done
    // by computing two separate Siphash 2-4 results and concatenating.
    // Note: `nexus_check_value` is a packed struct.
    struct nexus_check_value key_part_a =
        nexus_check_compute(derivation_key_a,
                            key_derivation_material,
                            sizeof(key_derivation_material));

    struct nexus_check_value key_part_b =
        nexus_check_compute(derivation_key_b,
                            key_derivation_material,
                            sizeof(key_derivation_material));

    struct nx_common_check_key derived_link_key;
    (void) memcpy(&derived_link_key.bytes[0],
                  &key_part_a,
                  sizeof(struct nexus_check_value));
    (void) memcpy(&derived_link_key.bytes[0] + 8,
                  &key_part_b,
                  sizeof(struct nexus_check_value));

    // In future, caller may be able to retrieve link key by reference
    // and clear it securely after storing it elsewhere.
    nexus_secure_memclr((void*) key_derivation_material,
                        sizeof(key_derivation_material),
                        sizeof(key_derivation_material));
    nexus_secure_memclr(
        (void*) &key_part_a, sizeof(key_part_a), sizeof(key_part_a));
    nexus_secure_memclr(
        (void*) &key_part_b, sizeof(key_part_b), sizeof(key_part_b));

    return derived_link_key;
}

    #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE

NEXUS_IMPL_STATIC void
_nexus_channel_res_link_hs_get_current_window(struct nexus_window* window)
{
    nexus_util_window_init(
        window,
        _this.stored_accessory.received_ids,
        NEXUS_CHANNEL_LINK_HS_MAX_RECEIVE_FLAG_BYTE,
        _this.stored_accessory.handshake_index, // center on current index
        NEXUS_CHANNEL_LINK_HS_RECEIVE_WINDOW_BEFORE_CENTER_INDEX,
        NEXUS_CHANNEL_LINK_HS_RECEIVE_WINDOW_AFTER_CENTER_INDEX);
}

NEXUS_IMPL_STATIC bool _nexus_channel_res_link_hs_server_validate_challenge(
    const uint8_t* salt,
    const struct nexus_check_value* rcvd_mac,
    const struct nexus_window* window,
    uint32_t* matched_handshake_index,
    struct nx_common_check_key* derived_key)
{
    // use the key which only the origin and this device know
    const struct nx_common_check_key origin_key =
        nxp_channel_symmetric_origin_key();

    bool mac_valid = false;

    // variables updated on each loop iteration
    uint32_t count_le;
    struct nexus_check_value challenge_hash;
    uint32_t six_digit_int_challenge;
    struct nx_common_check_key computed_link_key;
    struct nexus_check_value computed_mac;

    // should be true due if window is valid.
    NEXUS_ASSERT(window->center_index >= window->flags_below,
                 "Invalid window size!");
    NEXUS_ASSERT(window->center_index < (UINT32_MAX - window->flags_above),
                 "Invalid window size!");

    const uint32_t start_index = window->center_index - window->flags_below;
    const uint32_t end_index = window->center_index + window->flags_above;

    // each loop iteration involves a key derivation step
    for (uint32_t i = start_index; i <= end_index; i++)
    {
        NEXUS_ASSERT(nexus_util_window_id_within_window(window, i),
                     "ID unexpectedly out of window.");
        if (nexus_util_window_id_flag_already_set(window, i))
        {
            OC_DBG("Skipping already used ID %u", i);
            continue;
        }

        // first, calculate a possible 'challenge int' using the accessory
        // link handshake 'count' and the origin key. For consistency in
        // computation ensure that the count is in little endian.
        count_le = nexus_endian_htole32(i);
        challenge_hash =
            nexus_check_compute(&origin_key, &count_le, sizeof(count_le));

        // obtain lower 32 bits of check
        six_digit_int_challenge =
            nexus_check_value_as_uint64(&challenge_hash) & 0xffffffff;

        // obtain the 'decimal representation' of the lowest 6 decimal digits
        // of the check.  Note that leading zeros are *ignored* as the check is
        // now
        // computed over the numeric value represented by the 6 decimal check
        // digits, not the individual digits themselves.
        six_digit_int_challenge = six_digit_int_challenge % 1000000;

        // Now, we can attempt to compute a key to use to check the MAC
        computed_link_key = _res_link_hs_generate_link_key(
            six_digit_int_challenge,
            salt,
            CHALLENGE_MODE_3_SALT_LENGTH_BYTES,
            &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_1,
            &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_2);

        // use the computed key to check the MAC for the provided salt. We
        // can then determine if this computed key (and thus this 'handshake
        // index') is the right one to use.
        computed_mac = nexus_check_compute(
            &computed_link_key, salt, CHALLENGE_MODE_3_SALT_LENGTH_BYTES);
        if (memcmp(&computed_mac, rcvd_mac, sizeof(struct nexus_check_value)) ==
            0)
        {
            // will be persisted later by caller
            *matched_handshake_index = i;
            mac_valid = true;
            memcpy(derived_key,
                   &computed_link_key,
                   sizeof(struct nx_common_check_key));
            nexus_secure_memclr(&computed_link_key,
                                sizeof(struct nx_common_check_key),
                                sizeof(struct nx_common_check_key));
            break;
        }
    }

    return mac_valid;
}

/** GET handler for incoming requests (server).
 */
void nexus_channel_res_link_hs_server_get(oc_request_t* request,
                                          oc_interface_mask_t if_mask,
                                          void* data)
{
    OC_DBG("Handling Link Handshake GET");
    // No payload data is used on a GET
    (void) data;

    // OC resource model building expects 1x root object at a time.
    oc_rep_begin_root_object();

    switch (if_mask)
    {
        case OC_IF_BASELINE:
            OC_DBG("Interface: Baseline");
            oc_process_baseline_interface(request->resource);
        /* fall through */
        case OC_IF_RW:
            OC_DBG("Interface: RW");
        /* fall through */
        default:
            oc_rep_set_byte_string(
                root, cD, _this.server.chal_data, _this.server.chal_data_len);
            oc_rep_set_byte_string(
                root, rD, _this.server.resp_data, _this.server.resp_data_len);
            oc_rep_set_uint(root, cM, _this.server.chal_mode);
            oc_rep_set_uint(root, lS, _this.server.link_security_mode);
            oc_rep_set_uint(root, st, _this.server.state);
            oc_rep_set_uint(root, tI, _this.server.seconds_since_init);
            oc_rep_set_uint(
                root,
                tT,
                NEXUS_CHANNEL_LINK_HANDSHAKE_ACCESSORY_TIMEOUT_SECONDS);

            oc_rep_set_int_array(
                root,
                sL,
                supported_link_security_modes,
                (int) (sizeof(supported_link_security_modes) /
                       sizeof(supported_link_security_modes[0])));
            oc_rep_set_int_array(root,
                                 sC,
                                 supported_challenge_modes,
                                 (int) (sizeof(supported_challenge_modes) /
                                        sizeof(supported_challenge_modes[0])));
            break;
    }
    oc_rep_end_root_object();
    OC_DBG("Sending GET response");

    // OC_STATUS_OK => CONTENT_2_05
    oc_send_response(request, OC_STATUS_OK);
}

/* Internal, used to determine if incoming challenge data is valid.
 *
 * Used by accessory when processing POST requests.
 * Checks the OC_REP_NAME, assumes type is already checked for
 * `OC_REP_BYTE_STRING`
 *
 * \param rep pointer to representation to check
 * \param chal_data challenge data extracted from rep (if length != 0)
 * \return length of data if valid, 0 otherwise
 */
static uint8_t
_nexus_channel_res_link_hs_challenge_data_length(const oc_rep_t* rep,
                                                 uint8_t** chal_data)
{
    NEXUS_ASSERT(rep->type == OC_REP_BYTE_STRING,
                 "Expected type to be prevalidated");
    uint8_t length = 0;
    if (strncmp(oc_string(rep->name), CHAL_DATA_SHORT_PROP_NAME, 2) == 0)
    {
        // Note: oc_string_len returns an unsigned integer
        length = (uint8_t) oc_string_len(rep->value.string);

        // only accept incoming challenge bytes that don't exceed the
        // max bytes acceptable to this accessory.
        if (length > NEXUS_CHANNEL_LINK_MAX_CHAL_DATA_BYTES)
        {
            length = 0;
            OC_WRN("chal_data length too long, unsupported.");
        }
        else
        {
            *chal_data = oc_cast(rep->value.string, uint8_t);
        }
    }
    return length;
}

static bool _nexus_channel_res_link_hs_challenge_mode_supported(
    enum nexus_channel_link_handshake_challenge_mode requested_chal_mode)
{
    for (uint8_t i = 0; i < (sizeof(supported_challenge_modes) /
                             sizeof(supported_challenge_modes[0]));
         i++)
    {
        if (requested_chal_mode == supported_challenge_modes[i])
        {
            return true;
        }
    }
    return false;
}

static bool _nexus_channel_res_link_hs_link_security_mode_supported(
    enum nexus_channel_link_security_mode requested_security_mode)
{
    for (uint8_t i = 0; i < (sizeof(supported_link_security_modes) /
                             sizeof(supported_link_security_modes[0]));
         i++)
    {
        if (requested_security_mode == supported_link_security_modes[i])
        {
            return true;
        }
    }
    return false;
}

static void
_nexus_channel_res_link_hs_server_send_error_response(oc_request_t* request)
{
    // send error response, return early
    // clear internal handshake state, send error
    _nexus_channel_res_link_hs_reset_server_state();
    oc_send_response(request, OC_STATUS_BAD_REQUEST);
    return;
}

// returns true if challenge data is valid, false otherwise
static bool _nexus_channel_res_link_hs_server_post_parse_payload_chal_data(
    const oc_rep_t* rep)
{
    uint8_t* rcvd_chal_data = NULL;
    const uint8_t chal_data_len =
        _nexus_channel_res_link_hs_challenge_data_length(rep, &rcvd_chal_data);
    if ((chal_data_len == (CHALLENGE_MODE_3_SALT_LENGTH_BYTES +
                           sizeof(struct nexus_check_value))) &&
        rcvd_chal_data != 0)
    {
        memcpy(_this.server.chal_data, rcvd_chal_data, chal_data_len);
        _this.server.chal_data_len = chal_data_len;
        return true;
    }
    return false;
}

// returns true if the requested modes are valid, false otherwise
static bool
_nexus_channel_res_link_hs_server_post_parse_payload_requested_modes(
    const uint8_t received_value, const char* received_name)
{
    bool valid = true;
    if (memcmp(received_name, CHAL_MODE_SHORT_PROP_NAME, 2) == 0)
    {
        if (_nexus_channel_res_link_hs_challenge_mode_supported(
                (enum nexus_channel_link_handshake_challenge_mode)
                    received_value))
        {
            _this.server.chal_mode =
                (enum nexus_channel_link_handshake_challenge_mode)
                    received_value;
        }
        else
        {
            valid = false;
        }
    }
    else if (memcmp(received_name, LINK_SEC_MODE_SHORT_PROP_NAME, 2) == 0)
    {
        if (_nexus_channel_res_link_hs_link_security_mode_supported(
                (enum nexus_channel_link_security_mode) received_value))
        {
            _this.server.link_security_mode =
                (enum nexus_channel_link_security_mode) received_value;
        }
        else
        {
            valid = false;
        }
    }
    else
    {
        valid = false;
    }
    return valid;
}

// internal - will return true if the incoming payload was valid and parsed
// into the appropriate `_this.server` fields, false otherwise.
static bool _nexus_channel_res_link_hs_server_post_parse_payload(
    const oc_request_t* request)
{
    const oc_rep_t* rep = request->request_payload;
    bool payload_error = false;
    uint8_t properties_missing = 3;

    if (rep == NULL)
    {
        OC_WRN("Missing request payload, unexpected error.");
        payload_error = true;
    }

    bool valid_prop;
    while (rep != NULL && !payload_error)
    {
        valid_prop = false;
        switch (rep->type)
        {
            case OC_REP_BYTE_STRING:
                valid_prop =
                    _nexus_channel_res_link_hs_server_post_parse_payload_chal_data(
                        rep);
                break;

            case OC_REP_INT:
                valid_prop =
                    _nexus_channel_res_link_hs_server_post_parse_payload_requested_modes(
                        (uint8_t) rep->value.integer, oc_string(rep->name));
                break;

            default:
                payload_error = true;
                OC_WRN("Unexpected rep type");
                break;
        }
        if (valid_prop)
        {
            properties_missing--;
        }
        else
        {
            payload_error = true;
        }
        rep = rep->next;
    }

    // true if payload is valid and all data extracted, false otherwise
    return (!payload_error && (properties_missing == 0));
}

// internal, to reduce cognitive load of `nexus_channel_res_link_hs_server_post`
NEXUS_IMPL_STATIC void
_nexus_channel_res_link_hs_server_post_finalize_success_state(
    const uint32_t matched_handshake_index,
    struct nexus_window* window,
    const struct nx_common_check_key* derived_link_key)
{
    // challenge is valid and ID is unused -- respond with the MAC over the
    // inverted salt, and 'use' this ID.
    if (_this.stored_accessory.handshake_index < matched_handshake_index)
    {
        // Update the 'handshake index' / window center if it should increase
        _this.stored_accessory.handshake_index = matched_handshake_index;
    }
    (void) nexus_util_window_set_id_flag(window, matched_handshake_index);
    (void) nexus_nv_update(NX_NV_BLOCK_CHANNEL_LINK_HS_ACCESSORY,
                           (uint8_t*) &_this.stored_accessory);

    // Note: assumes _this.server.chal_data_len is valid.
    const struct nexus_check_value computed_mac =
        _res_link_hs_mode0_compute_inverted_salt_mac(&_this.server.chal_data[0],
                                                     derived_link_key);
    memcpy(&_this.server.resp_data,
           computed_mac.bytes,
           sizeof(struct nexus_check_value));
    _this.server.resp_data_len = sizeof(struct nexus_check_value);

    // From this point on, the link exists from the accessory
    // standpoint, so we can set the state of the handshake back to 'idle'.
    _this.server.state = LINK_HANDSHAKE_STATE_IDLE;
}

/** POST handler for incoming requests (server/accessory)
 */
void nexus_channel_res_link_hs_server_post(oc_request_t* request,
                                           oc_interface_mask_t if_mask,
                                           void* data)
{
    // ignore interface mask and user data for POST
    (void) if_mask;
    (void) data;

    // mark the handshake state as in progress/active
    _this.server.state = LINK_HANDSHAKE_STATE_ACTIVE;

    // Extract the payload if it is present and valid
    if (!_nexus_channel_res_link_hs_server_post_parse_payload(request))
    {
        OC_WRN("Received challenge data invalid");
        _nexus_channel_res_link_hs_server_send_error_response(request);
        return;
    }

    // Next, see if the payload represents a valid challenge
    bool challenge_validated;
    uint32_t matched_handshake_index;
    struct nx_common_check_key derived_link_key;
    struct nexus_check_value received_mac;

    memcpy(&received_mac.bytes[0],
           &_this.server.chal_data[8],
           sizeof(struct nexus_check_value));

    // Window used to determine if an ID is already set, and skip checking it
    // if so.
    struct nexus_window window;
    _nexus_channel_res_link_hs_get_current_window(&window);

    // validate_challenge will examine first 8 bytes from chal_data
    challenge_validated = _nexus_channel_res_link_hs_server_validate_challenge(
        &_this.server.chal_data[0],
        &received_mac,
        &window, // will be searched, not modified
        &matched_handshake_index, // will be populated if valid index found
        &derived_link_key);

    if (!challenge_validated)
    {
        OC_WRN("Unable to validate challenge; no link will be created.");
        _nexus_channel_res_link_hs_server_send_error_response(request);
        return;
    }

    // mode0.sym_key will be overwritten by memcpy
    union nexus_channel_link_security_data security_data;
    security_data.mode0.nonce = 0;

    memcpy(&security_data.mode0.sym_key,
           &derived_link_key,
           sizeof(struct nx_common_check_key));

    struct nx_id controller_id;
    nexus_oc_wrapper_oc_endpoint_to_nx_id(request->origin, &controller_id);

    // Attempt to create a new link to the controller
    const bool link_created = nexus_channel_link_manager_create_link(
        &controller_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY, // this device is an accessory
        _this.server.link_security_mode,
        &security_data);

    if (!link_created)
    {
        OC_WRN("Security data valid but unable to create link...");
        _nexus_channel_res_link_hs_server_send_error_response(request);
        return;
    }

    // finalize will set the link handshake state back to 'idle'
    _nexus_channel_res_link_hs_server_post_finalize_success_state(
        matched_handshake_index, &window, &derived_link_key);
    nexus_secure_memclr(&derived_link_key,
                        sizeof(struct nx_common_check_key),
                        sizeof(struct nx_common_check_key));
    nexus_secure_memclr(&security_data,
                        sizeof(union nexus_channel_link_security_data),
                        sizeof(union nexus_channel_link_security_data));

    oc_rep_begin_root_object();
    // only send back MAC computed over inverted salt
    oc_rep_set_byte_string(
        root, rD, _this.server.resp_data, _this.server.resp_data_len);
    oc_rep_end_root_object();

    // CREATED_2_01
    oc_send_response(request, OC_STATUS_CREATED);
}

    #endif /* ifdef NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE */

    #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE

bool _nexus_channel_res_link_hs_link_mode_3_send_post(
    const nexus_link_hs_controller_t* client_hs)
{
    PRINT("res_link_hs: Preparing multicast POST to URI 'h'...\n");

    // send broadcast request out to 'all devices' to attempt to link
    // relevant accessory will validate and respond

    // For user data callbacks, See:
    // https://github.com/iotivity/iotivity-lite/blob/e083944174b61111ed70de927c9557cfa0cb5ed2/apps/smart_lock_linux.c#L175
    // In that example, 'user data' was set in `post_init`, so that when
    // handling the response, the handler can figure out what lock was
    // requested, so the response updates the appropriate lock state

    OC_DBG("Initializing Nexus Channel Handshake POST");

    // exactly one callback for this handshake POST at any given time
    // Only allocate a new callback if one does not already exist
    oc_client_cb_t* cb = oc_ri_get_client_cb(
        "/h", &NEXUS_OC_WRAPPER_MULTICAST_OC_ENDPOINT_T_ADDR, OC_POST);

    // Free if one exists, prevent multiple duplicate callbacks
    if (cb)
    {
        (void) oc_ri_remove_client_cb(cb);
    }
    if (!oc_init_post("/h",
                      &NEXUS_OC_WRAPPER_MULTICAST_OC_ENDPOINT_T_ADDR,
                      NULL,
                      nexus_channel_res_link_hs_client_post,
                      LOW_QOS,
                      (void*) client_hs))
    {
        OC_WRN("Unable to initialize POST (link handshake)!");
        return false;
    }

    oc_rep_begin_root_object();

    // Challenge data is the salt *and* a MAC.
    oc_rep_set_byte_string(
        root, cD, client_hs->send_chal_data, client_hs->send_chal_data_len);
    oc_rep_set_uint(root, cM, client_hs->requested_chal_mode);
    oc_rep_set_uint(root, lS, client_hs->requested_security_mode);
    oc_rep_end_root_object();

    OC_DBG("Sending Nexus Channel Handshake POST");
    // 'false' as handshakes are unsecured
    if (!oc_do_post(false))
    {
        OC_WRN("Error: Unable to perform POST");
        return false;
    }

    PRINT("res_link_hs: Challenge data to send: ");
    // From clang scan-build 10:
    // "warning: Out of bound memory access (access exceeds upper limit of
    // memory block)"
    // PRINTbytes(client_hs->send_chal_data, client_hs->send_chal_data_len);
    PRINT("res_link_hs: Requesting link handshake *challenge* mode %u\n",
          client_hs->requested_chal_mode);
    PRINT("res_link_hs: Requesting link *security* mode %u\n",
          client_hs->requested_security_mode);

    // request processing for IoTivity core
    nxp_common_request_processing();
    return true;
}

// Called once, when we get an origin command. Will start a link handshake
// from the controller to try and reach accessories.
bool nexus_channel_res_link_hs_link_mode_3(
    const struct nexus_channel_om_create_link_body* om_body)
{
    // find first inactive handshake, use it
    nexus_link_hs_controller_t* client_hs;
    for (uint8_t i = 0; i < NEXUS_CHANNEL_SIMULTANEOUS_LINK_HANDSHAKES; i++)
    {
        client_hs = &_this.clients[i];
        if (client_hs->state == LINK_HANDSHAKE_STATE_IDLE)
        {
            break;
        }
    }
    if (client_hs->state != LINK_HANDSHAKE_STATE_IDLE)
    {
        OC_ERR("All handshakes are active, cannot accept origin command");
        return false;
    }

    // TODO future: filter for accessories using this ID if we know of them
    // using om_body->trunc_acc_id

    // compute random salt, fits 2x uint32_t into 8-byte salt length
    for (uint8_t i = 0; i < 2; i++)
    {
        // endianness does not matter, the sequence of bytes must be
        // consistent but is arbitrary
        const uint32_t rand = nxp_channel_random_value();
        memcpy(&client_hs->salt[i * 4], (const void*) &rand, 4);
    }
    PRINT("res_link_hs: Generating link key using salt: ");
    PRINTbytes(client_hs->salt, CHALLENGE_MODE_3_SALT_LENGTH_BYTES);
    PRINT("\nres_link_hs: Challenge int digits: %u\n",
          om_body->accessory_challenge.six_int_digits);

    // Compute link key using salt, copy into local handshake state
    const struct nx_common_check_key link_key = _res_link_hs_generate_link_key(
        om_body->accessory_challenge.six_int_digits,
        client_hs->salt,
        CHALLENGE_MODE_3_SALT_LENGTH_BYTES,
        &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_1,
        &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_2);
    PRINT("res_link_hs: Generated link key: \n");
    PRINTbytes(link_key.bytes, sizeof(struct nx_common_check_key));

    memcpy(&client_hs->link_key.bytes[0],
           &link_key.bytes[0],
           sizeof(struct nx_common_check_key));

    // Also compute the MAC and store it in the local handshake state
    client_hs->salt_mac = nexus_check_compute(
        &client_hs->link_key, client_hs->salt, sizeof(client_hs->salt));

    // waiting for a response from a connected accessory
    // This timing could be improved by moving the counter initialization
    // into `process`, this will work for now.
    client_hs->state = LINK_HANDSHAKE_STATE_ACTIVE;
    client_hs->seconds_since_init = 0;
    client_hs->last_post_seconds = 0;

    // update the challenge data - this is the only location it is updated
    NEXUS_STATIC_ASSERT(NEXUS_CHANNEL_LINK_MAX_CHAL_DATA_BYTES <=
                            CHALLENGE_MODE_3_SALT_LENGTH_BYTES +
                                sizeof(struct nexus_check_value),
                        "Cannot fit SALT + MAC in challenge payload");
    NEXUS_STATIC_ASSERT(sizeof(struct nexus_check_value) == 8,
                        "Unexpected check value size in bytes");

    // challenge data is not explicitly cleared, but will be updated here.
    // Relies on CHALLENGE_MODE_3_SALT_LENGTH_BYTES being 8 bytes, and the
    // salt_mac.bytes also being 8 bytes.
    memcpy(client_hs->send_chal_data,
           client_hs->salt,
           CHALLENGE_MODE_3_SALT_LENGTH_BYTES);
    memcpy(&client_hs->send_chal_data[CHALLENGE_MODE_3_SALT_LENGTH_BYTES],
           client_hs->salt_mac.bytes,
           8);
    client_hs->send_chal_data_len = CHALLENGE_MODE_3_SALT_LENGTH_BYTES + 8;

    // set requested challenge mode and link security mode to 0
    client_hs->requested_chal_mode =
        NEXUS_CHANNEL_LINK_HANDSHAKE_CHALLENGE_MODE_0_CHALLENGE_RESULT;
    client_hs->requested_security_mode =
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24;

    // will construct and send POST on next processing loop
    nxp_channel_notify_event(NXP_CHANNEL_EVENT_LINK_HANDSHAKE_STARTED);
    nxp_common_request_processing();
    return true;
}

/** Handler for responses to GET requests (as client/controller).
 */
void nexus_channel_res_link_hs_client_get(oc_client_response_t* data)
{
    (void) data;
    // Do not expect to make GET requests for handshake at this time
}
/** Handler for responses to POST requests (as client/controller).
 */
void nexus_channel_res_link_hs_client_post(oc_client_response_t* data)
{
    // return early in various error cases
    if (data == NULL)
    {
        OC_ERR("Callback received null response data");
        return;
    }
    // OC_STATUS_CREATED == 2.01
    else if (data->code != OC_STATUS_CREATED)
    {
        // ignore any response that is not 2.01/created
        OC_WRN("Invalid/unexpected message received from accessory");
        return;
    }
    else if (data->user_data == NULL)
    {
        // Should never occur in production, but if it does, return.
        NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(
            0,
            "User data is null, but required to adjust the "
            "appropriate handshake object.");
        return;
    }

    PRINT("res_link_hs: Handling response to handshake challenge\n");

    OC_DBG("Received status code %u from endpoint:", data->code);
    NEXUS_ASSERT(data->endpoint != NULL, "Endpoint should never be null.");
    OC_LOGipaddr(*data->endpoint);

    struct nx_id accessory_id;
    nexus_oc_wrapper_oc_endpoint_to_nx_id(data->endpoint, &accessory_id);

    // Determine which handshake this response refers to. User data
    // is populated for this callback when the initial POST request is made.
    nexus_link_hs_controller_t* client_hs =
        (nexus_link_hs_controller_t*) data->user_data;

    oc_rep_t* rep = data->payload;
    while (rep != NULL)
    {
        // we want to see the response data
        if (memcmp(oc_string(rep->name), RESP_DATA_SHORT_PROP_NAME, 2) == 0)
        {
            if (rep->type != OC_REP_BYTE_STRING)
            {
                // OC_WRN("Received non-bytestring " resp_data " in response.");
                return;
            }
            // Note: oc_string_len returns unsigned integer
            const uint8_t length = (uint8_t) oc_string_len(rep->value.string);
            const uint8_t* rep_data = oc_cast(rep->value.string, uint8_t);
            // we only expect to receive a MAC, nothing else
            if (length != sizeof(struct nexus_check_value))
            {
                OC_WRN("resp_data length is invalid, expected MAC");
                return;
            }

            NEXUS_STATIC_ASSERT(sizeof(client_hs->salt) ==
                                    CHALLENGE_MODE_3_SALT_LENGTH_BYTES,
                                "Salt sizes do not match...");
            const struct nexus_check_value computed_mac =
                _res_link_hs_mode0_compute_inverted_salt_mac(
                    &client_hs->salt[0], &client_hs->link_key);

            if (memcmp(computed_mac.bytes, rep_data, length) != 0)
            {
                OC_WRN("Transmitted MAC does not match, returning.");
                return;
            }

            // nonce is set to 0 by zero-initialization of entire union
            union nexus_channel_link_security_data security_data;
            memset(&security_data,
                   0x00,
                   sizeof(union nexus_channel_link_security_data));
            memcpy(&security_data.mode0.sym_key,
                   &client_hs->link_key,
                   sizeof(struct nx_common_check_key));

            // Attempt to create a new link
            const bool success = nexus_channel_link_manager_create_link(
                &accessory_id,
                CHANNEL_LINK_OPERATING_MODE_CONTROLLER, // this device is a
                // controller
                client_hs->requested_security_mode,
                &security_data);

            if (!success)
            {
                OC_WRN("Security data valid but unable to create link...");
                return;
            }

            PRINT("-------------------------------------------\n");
            PRINT("res_link_hs: Handshake completed successfully!\n");
            PRINT("-------------------------------------------\n");

            // here, we have confirmed MAC is valid and created a link,
            // clear the handshake data.
            OC_DBG("Handshake complete, clearing handshake data.");
            memset(client_hs, 0x00, sizeof(nexus_link_hs_controller_t));
        }
        OC_DBG("next item in payload");
        rep = rep->next;
    }
    // request processing for IoTivity core
    nxp_common_request_processing();
}

    #endif /* if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE */

#endif /* if NEXUS_CHANNEL_LINK_SECURITY_ENABLED */

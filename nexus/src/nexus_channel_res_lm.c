/** \file nexus_channel_res_lm.c
 * Nexus Channel Link OCF Resource (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_channel_res_lm.h"
#include "include/nxp_channel.h"
#include "include/nxp_common.h"
#include "src/nexus_common_internal.h"
#include "src/nexus_security.h"
#include "src/nexus_util.h"

#include "oc/include/oc_api.h"
#include "oc/include/oc_rep.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

// Abbreviated property names
const char* L_LINKED_DEVICE_ID_SHORT_PROP_NAME = "lD";
const char* L_CHAL_MODE_SHORT_PROP_NAME = "cM";
const char* L_LINK_SEC_MODE_SHORT_PROP_NAME = "lS";
const char* L_TIME_SINCE_INIT_SHORT_PROP_NAME = "tI";
const char* L_TIME_SINCE_ACTIVITY_SHORT_PROP_NAME = "tA";
const char* L_TIMEOUT_CONFIGURED_SHORT_PROP_NAME = "tT";

// forward declarations
static void _nexus_channel_link_manager_clear_link_internal(uint8_t link_id);
static void _nexus_channel_link_manager_clear_links_internal(void);

static NEXUS_PACKED_STRUCT
{
    NEXUS_PACKED_STRUCT
    {
        nexus_channel_link_t links[NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS];
    }
    stored;
    nexus_channel_link_t pending_link_to_create;
    uint8_t link_count;
    bool link_idx_in_use[NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS];
    bool pending_add_link;
    bool pending_clear_all_links;
}
_this;

NEXUS_STATIC_ASSERT(sizeof(_this.stored) ==
                        36 * NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS,
                    "Invalid size of stored links struct");

// Look up an NV metadata block based on ID
NEXUS_IMPL_STATIC bool _nexus_channel_link_manager_index_to_nv_block(
    uint8_t index, struct nx_common_nv_block_meta** dest_block_meta_ptr)
{
    bool success = true;
    switch (index)
    {
        case 0:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_1;
            break;
        case 1:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_2;
            break;
        case 2:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_3;
            break;
        case 3:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_4;
            break;
        case 4:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_5;
            break;
        case 5:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_6;
            break;
        case 6:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_7;
            break;
        case 7:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_8;
            break;
        case 8:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_9;
            break;
        case 9:
            *dest_block_meta_ptr = &NX_NV_BLOCK_CHANNEL_LM_LINK_10;
            break;
        default:
    #ifndef DEBUG
            // need to conditionally include, as in debug mode, the function
            // will never return and thus this value is 'never read'.
            success = false;
    #endif
            NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(
                0, "Error looking up NV block metadata, should never occur.");
            break;
    }
    return success;
}

bool nexus_channel_link_manager_init(void)
{
    memset(&_this, 0x00, sizeof(_this));

    // Must initialize tmp_block_meta for CWE-457
    struct nx_common_nv_block_meta* tmp_block_meta = {0};
    nexus_channel_link_t tmp_link;
    // load data for each link from nonvolatile
    for (uint8_t i = 0; i < NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS; i++)
    {
        _nexus_channel_link_manager_index_to_nv_block(i, &tmp_block_meta);
        // note: packed struct
        if (nexus_nv_read(*tmp_block_meta, (uint8_t*) &tmp_link))
        {
            // check that link represents a valid device (skip '0' device IDs)
            if (tmp_link.linked_device_id.device_id == 0)
            {
                continue;
            }

            // valid link - copy and increment link index/count
            memcpy(&_this.stored.links[i],
                   &tmp_link,
                   sizeof(nexus_channel_link_t));
            _this.link_count++;
            _this.link_idx_in_use[i] = true;

            // existing links update nonce by 16 on every re-init to protect
            // against replay attacks
            _this.stored.links[i].security_data.mode0.nonce += 16;
        }
    }

    const oc_interface_mask_t if_mask_arr[] = {OC_IF_RW, OC_IF_BASELINE};
    const struct nx_channel_resource_props lm_props = {
        .uri = "/l",
        .resource_type = "angaza.com.nexus.link",
        .rtr = 65002,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        .get_handler = nexus_channel_res_lm_server_get,
        .get_secured = false,
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error result = nx_channel_register_resource(&lm_props);

    NEXUS_ASSERT(result == NX_CHANNEL_ERROR_NONE,
                 "Unexpected error registering resource");

    return (result == NX_CHANNEL_ERROR_NONE);
}

uint32_t nexus_channel_link_manager_process(uint32_t seconds_elapsed)
{
    OC_DBG("res_lm: inside process\n");
    // increment activity time for any active links, and delete any timed out
    // links
    struct nexus_channel_link_t* cur_link;
    for (uint8_t i = 0; i < NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS; i++)
    {
        if (!_this.link_idx_in_use[i])
        {
            // skip inactive link slots
            continue;
        }
        cur_link = &_this.stored.links[i];
        cur_link->seconds_since_init += seconds_elapsed;

        // 'seconds since active' is incremented here, and reset to 0 when
        // the link is detected as 'used' by security manager.
        cur_link->seconds_since_active += seconds_elapsed;

        if (cur_link->seconds_since_active > NEXUS_CHANNEL_LINK_TIMEOUT_SECONDS)
        {
            // TODO future, notify the other device of link erasure.
            // Practically, timeout is only expected if the other device is
            // absent (since we define timeout as 'time since any successful
            // communication with other party on this link).
            _nexus_channel_link_manager_clear_link_internal(i);
        }
        else if (
            (enum nexus_channel_link_security_mode) cur_link->security_mode ==
            NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24)
        {
            // Persist updated nonce to NV periodically
            if (cur_link->security_data.mode0.nonce %
                    NEXUS_CHANNEL_LINK_SECURITY_NONCE_NV_STORAGE_INTERVAL_COUNT ==
                0)
            {
                // increment to avoid updating again on next `process` call
                cur_link->security_data.mode0.nonce++;

                // Write the update to NV, clearing this link block. On the next
                // read, if the block is all 0x00, it will be considered a
                // meaningless link and ignored.
                struct nx_common_nv_block_meta* tmp_block_meta = 0;
                (void) _nexus_channel_link_manager_index_to_nv_block(
                    i, &tmp_block_meta);
                NEXUS_ASSERT(tmp_block_meta != 0, "Block ID not found");
                nexus_nv_update(*tmp_block_meta, (uint8_t*) cur_link);
            }
        }
    }
    if (_this.pending_add_link)
    {
        PRINT("res_lm: Attempting to persist new link data\n");
        for (uint8_t i = 0; i < NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS; i++)
        {
            if (_this.link_idx_in_use[i] == false)
            {
                _this.link_count++;
                NEXUS_ASSERT(_this.link_count <=
                                 NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS,
                             "Links exceed limit, unexpected.");

                _this.link_idx_in_use[i] = true;

                nexus_channel_link_t* new_link = &_this.stored.links[i];

                memcpy(new_link,
                       &_this.pending_link_to_create,
                       sizeof(nexus_channel_link_t));
                // Write the update to NV
                // The new link is at the *current* next link index, before we
                // increment
                struct nx_common_nv_block_meta* tmp_block_meta = 0;
                (void) _nexus_channel_link_manager_index_to_nv_block(
                    i, &tmp_block_meta);
                NEXUS_ASSERT(tmp_block_meta != 0, "Block ID not found");
                nexus_nv_update(*tmp_block_meta, (uint8_t*) new_link);

                PRINT("\nres_lm: New link persisted! Total link count %u\n",
                      _this.link_count);
                PRINT(
                    "res_lm: Linked to Nexus ID authority ID=%u, device ID=%u",
                    new_link->linked_device_id.authority_id,
                    new_link->linked_device_id.device_id);
                PRINT("\nres_lm: Link security mode: %u\n",
                      new_link->security_mode);
                PRINT("res_lm: Persisting link key: ");
                PRINTbytes(new_link->security_data.mode0.sym_key.bytes,
                           sizeof(struct nx_common_check_key));
                if (new_link->operating_mode ==
                    CHANNEL_LINK_OPERATING_MODE_CONTROLLER)
                {
                    nxp_channel_notify_event(
                        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
                }
                else
                {
                    nxp_channel_notify_event(
                        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
                }
                break;
            }
        }

        // Prepare for another pending link
        memset(
            &_this.pending_link_to_create, 0x00, sizeof(nexus_channel_link_t));
        _this.pending_add_link = false;
    }
    else if (_this.pending_clear_all_links)
    {

        PRINT("res_lm: attempting to clear/delete all existing channel links "
              "(count = %u) \n",
              _this.link_count);
        _nexus_channel_link_manager_clear_links_internal();
        _this.pending_clear_all_links = false;
        PRINT("res_lm: all channel links are now deleted (count = %u)\n",
              _this.link_count);
    }

    // no urgent callbacks required
    return NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS;
}

// internal use, return pointer to the link
static bool
_nexus_channel_link_manager_link_index_from_nxid(const struct nx_id* id,
                                                 uint8_t* link_index)
{
    for (uint8_t i = 0; i < NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS; i++)
    {
        if (!_this.link_idx_in_use[i])
        {
            continue;
        }

        const struct nx_id* comp_id = &_this.stored.links[i].linked_device_id;
        if (comp_id->device_id == id->device_id &&
            comp_id->authority_id == id->authority_id)
        {
            *link_index = i;
            return true;
        }
    }
    return false;
}

NEXUS_IMPL_STATIC bool
_nexus_channel_link_manager_link_from_nxid(const struct nx_id* id,
                                           nexus_channel_link_t* retrieved_link)
{
    uint8_t link_index;
    bool found =
        _nexus_channel_link_manager_link_index_from_nxid(id, &link_index);
    if (found)
    {
        memcpy(retrieved_link,
               &_this.stored.links[link_index],
               sizeof(nexus_channel_link_t));
    }

    return found;
}

bool nexus_channel_link_manager_create_link(
    const struct nx_id* linked_device_id,
    enum nexus_channel_link_operating_mode operating_mode,
    enum nexus_channel_link_security_mode security_mode,
    const union nexus_channel_link_security_data* security_data)
{
    if (_this.link_count >= NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS)
    {
        return false;
    }

    struct nexus_channel_link_t matching_link;
    // first, check if a link already exists with this NXID
    if (_nexus_channel_link_manager_link_from_nxid(linked_device_id,
                                                   &matching_link))
    {
        OC_WRN("Cannot create new link, already exists");
        return false;
    }

    // Not a true mutex, but we don't expect `create_link` to be called
    // multiple times without processing (this would indicate an error).
    if (_this.pending_add_link)
    {
        NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(0, "Already modifying list of links");
        return false;
    }

    PRINT("res_lm: Identified new link to persist\n");
    _this.pending_add_link = true;

    memset(&_this.pending_link_to_create, 0x00, sizeof(nexus_channel_link_t));
    _this.pending_link_to_create.operating_mode = (uint8_t) operating_mode;
    _this.pending_link_to_create.security_mode = (uint8_t) security_mode;
    memcpy(&_this.pending_link_to_create.linked_device_id,
           linked_device_id,
           sizeof(struct nx_id));
    memcpy(&_this.pending_link_to_create.security_data,
           security_data,
           sizeof(union nexus_channel_link_security_data));

    nxp_common_request_processing();

    // will try to add link on next `_process` call
    return true;
}

// clear a single link
static void _nexus_channel_link_manager_clear_link_internal(uint8_t link_id)
{
    if (!_this.link_idx_in_use[link_id])
    {
        // skip already idle links
        return;
    }
    memset(&_this.stored.links[link_id], 0x00, sizeof(nexus_channel_link_t));
    _this.link_idx_in_use[link_id] = false;

    // Write the update to NV, clearing this link block. On the next
    // read, if the block is all 0x00, it will be considered a
    // meaningless link and ignored.
    struct nx_common_nv_block_meta* tmp_block_meta = 0;
    (void) _nexus_channel_link_manager_index_to_nv_block(link_id,
                                                         &tmp_block_meta);
    NEXUS_ASSERT(tmp_block_meta != 0, "Block ID not found");
    nexus_nv_update(*tmp_block_meta, (uint8_t*) &_this.stored.links[link_id]);

    _this.link_count--;
    nxp_channel_notify_event(NXP_CHANNEL_EVENT_LINK_DELETED);
}

// called from main process loop, not in interrupt
static void _nexus_channel_link_manager_clear_links_internal(void)
{
    for (uint8_t i = 0; i < NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS; i++)
    {
        _nexus_channel_link_manager_clear_link_internal(i);
    }
    NEXUS_ASSERT(_this.link_count == 0,
                 "All links are not cleared, but should be.");
}

void nexus_channel_link_manager_clear_all_links(void)
{
    // defer actual link deletion to
    // `_nexus_channel_link_manager_clear_links_internal`
    // The only place `pending_clear_all_links` is reset to false is in
    // `process`
    _this.pending_clear_all_links = true;
    nxp_common_request_processing();
}

enum nexus_channel_link_operating_mode
nexus_channel_link_manager_operating_mode(void)
{
    // if this device only supports one mode or the other, return that mode
    #if (NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE &&                              \
         !NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE)
    return CHANNEL_LINK_OPERATING_MODE_CONTROLLER;
    #elif (!NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE &&                           \
           NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE)
    return CHANNEL_LINK_OPERATING_MODE_ACCESSORY;
    #else
    NEXUS_STATIC_ASSERT(NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE &&
                            NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE,
                        "Neither controller nor accessory mode is supported, "
                        "but device is not dual mode - unexpected");

    const bool is_accessory =
        nexus_channel_link_manager_has_linked_controller();
    const bool is_controller =
        nexus_channel_link_manager_has_linked_accessory();

    enum nexus_channel_link_operating_mode op_mode =
        CHANNEL_LINK_OPERATING_MODE_DUAL_MODE_IDLE;

    if (is_controller && is_accessory)
    {
        op_mode = CHANNEL_LINK_OPERATING_MODE_DUAL_MODE_ACTIVE;
    }
    else if (is_controller && !is_accessory)
    {
        op_mode = CHANNEL_LINK_OPERATING_MODE_CONTROLLER;
    }
    else if (!is_controller && is_accessory)
    {
        op_mode = CHANNEL_LINK_OPERATING_MODE_ACCESSORY;
    }

    return op_mode;
    #endif
}

static bool _nexus_channel_link_manager_has_link_with_role(
    enum nexus_channel_link_operating_mode mode)
{
    bool found = false;
    for (uint8_t i = 0; i < NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS; i++)
    {
        if (!_this.link_idx_in_use[i])
        {
            continue;
        }

        const nexus_channel_link_t* link = &_this.stored.links[i];

        if (link->operating_mode == mode)
        {
            found = true;
            break;
        }
    }
    return found;
}

bool nexus_channel_link_manager_has_linked_controller(void)
{
    return _nexus_channel_link_manager_has_link_with_role(
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY);
}

bool nexus_channel_link_manager_has_linked_accessory(void)
{
    return _nexus_channel_link_manager_has_link_with_role(
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER);
}

bool nexus_channel_link_manager_security_data_from_nxid(
    const struct nx_id* id,
    struct nexus_channel_link_security_mode0_data* security_data)
{
    // need an actual link to copy data into
    // Warning: assumes `link_from_nxid` copies over the entire link.
    nexus_channel_link_t tmp_link;
    bool result = _nexus_channel_link_manager_link_from_nxid(id, &tmp_link);

    if (result)
    {
        memcpy(security_data,
               &tmp_link.security_data.mode0,
               sizeof(struct nexus_channel_link_security_mode0_data));
    }
    nexus_secure_memclr(
        &tmp_link, sizeof(nexus_channel_link_t), sizeof(nexus_channel_link_t));
    return result;
}

bool nexus_channel_link_manager_set_security_data_auth_nonce(
    const struct nx_id* id, uint32_t new_nonce)
{
    uint8_t link_index;
    // if no link exists, return early
    if (!_nexus_channel_link_manager_link_index_from_nxid(id, &link_index))
    {
        return false;
    }

    nexus_channel_link_t* found_link = &_this.stored.links[link_index];
    if (found_link->security_mode !=
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24)
    {
        // only support mode0 for now
        return false;
    }

    found_link->security_data.mode0.nonce = new_nonce;
    return true;
}

bool nexus_channel_link_manager_reset_link_secs_since_active(
    const struct nx_id* id)
{
    uint8_t link_index;
    // if no link exists, return early
    if (!_nexus_channel_link_manager_link_index_from_nxid(id, &link_index))
    {
        return false;
    }

    nexus_channel_link_t* found_link = &_this.stored.links[link_index];

    found_link->seconds_since_active = 0;
    return true;
}

uint8_t nx_channel_link_count(void)
{
    // starts at '0'
    return _this.link_count;
}

// internal. Warning, assumes `oc` root object is already open and will be
// closed outside of this function!
void _nexus_channel_res_link_server_get_populate_links(void)
{
    // possible concurrency, should avoid creating/deleting links
    // while populating this response. Practically, unlikely a
    // concern.
    uint16_t authority_id_be;
    uint32_t device_id_be;
    uint8_t linked_nexus_id_transmit[6];
    const nexus_channel_link_t* cur_link;

    oc_rep_open_array(root, reps);

    for (uint8_t i = 0; i < NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS; i++)
    {
        if (!_this.link_idx_in_use[i])
        {
            // don't populate a missing link
            continue;
        }

        cur_link = &_this.stored.links[i];

        // future revision may allow ASCII hex representation for
        // easier debugging of this endpoint
        authority_id_be =
            nexus_endian_htobe16(cur_link->linked_device_id.authority_id);
        device_id_be =
            nexus_endian_htobe32(cur_link->linked_device_id.device_id);
        memcpy(&linked_nexus_id_transmit[0], &authority_id_be, 2);
        memcpy(&linked_nexus_id_transmit[2], &device_id_be, 4);

        oc_rep_object_array_begin_item(reps);
        oc_rep_set_byte_string(reps,
                               lD,
                               linked_nexus_id_transmit,
                               sizeof(linked_nexus_id_transmit));
        oc_rep_set_uint(reps, oM, cur_link->operating_mode);
        oc_rep_set_uint(reps, sM, cur_link->security_mode);
        oc_rep_set_uint(reps, tI, cur_link->seconds_since_init);
        oc_rep_set_uint(reps, tA, cur_link->seconds_since_active);
        oc_rep_set_uint(reps, tT, NEXUS_CHANNEL_LINK_TIMEOUT_SECONDS);
        oc_rep_object_array_end_item(reps);
    }

    oc_rep_close_array(root, reps);
}

/** GET handler for incoming requests (server).
 */
void nexus_channel_res_lm_server_get(oc_request_t* request,
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
            _nexus_channel_res_link_server_get_populate_links();
            break;
    }
    oc_rep_end_root_object();
    OC_DBG("Sending GET response");

    // OC_STATUS_OK => CONTENT_2_05
    oc_send_response(request, OC_STATUS_OK);
}

    #ifdef NEXUS_DEFINED_DURING_TESTING
uint32_t
_nexus_channel_link_manager_secs_since_link_active(const struct nx_id* id)
{
    nexus_channel_link_t found_link = {0};
    const bool found =
        _nexus_channel_link_manager_link_from_nxid(id, &found_link);

    if (found)
    {
        return found_link.seconds_since_active;
    }
    else
    {
        // sentinel value to indicate no link found
        return UINT32_MAX;
    }
}
    #endif

#endif // NEXUS_CHANNEL_LINK_SECURITY_ENABLED

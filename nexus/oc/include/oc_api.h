/*
// Copyright (c) 2016-2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Modifications (c) 2020 Angaza, Inc.
*/

/**
  @brief Main API of IoTivity-Lite for client and server.
  @file
*/

/**
  \mainpage IoTivity-Lite API

  The file \link oc_api.h \endlink is the main entry for all
  server and client related OCF functions.
*/

#ifndef OC_API_H
#define OC_API_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"

#include "messaging/coap/oc_coap.h"

#include "oc_rep.h"
#include "oc_ri.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Call back handlers that are invoked in response to oc_main_init()
 *
 * @see oc_main_init
 */
typedef struct
{
  /**
   * Device initialization callback that is invoked to initialize the platform
   * and device(s).
   *
   * At a minimum the platform should be initialized and at least one device
   * added.
   *
   *  - oc_init_platform()
   *  - oc_add_device()
   *
   * Multiple devices can be added by making multiple calls to oc_add_device().
   *
   * Other actions may be taken in the init handler
   *  - The immutable device identifier can be set `piid`
   *    (a.k.a Protocol Independent ID) oc_set_immutable_device_identifier()
   *  - Set introspection data oc_set_introspection_data()
   *  - Set up an interrupt handler oc_activate_interrupt_handler()
   *  - Initialize application specific variables
   *
   * @return
   *  - 0 to indicate success initializing the application
   *  - value less than zero to indicate failure initializing the application
   *
   * @see oc_activate_interrupt_handler
   * @see oc_add_device
   * @see oc_init_platform
   * @see oc_set_immutable_device_identifier
   * @see oc_set_introspection_data
   */
  int (*init)(void);
  // signal event loop currently removed/unused
  void (*signal_event_loop)(void);

#ifdef OC_SERVER
  /**
   * Resource registration callback.
   *
   * Callback is invoked after the device initialization callback.
   *
   * Use this callback to add resources to the devices added during the device
   * initialization.  This where the properties and callbacks associated with
   * the resources are typically done.
   *
   * Note: Callback is only invoked when OC_SERVER macro is defined.
   *
   * Example:
   * ```
   * static void register_resources(void)
   * {
   *   oc_resource_t *bswitch = oc_new_resource(NULL, "/switch", 1, 0);
   *   oc_resource_bind_resource_type(bswitch, "oic.r.switch.binary");
   *   oc_resource_bind_resource_interface(bswitch, OC_IF_A);
   *   oc_resource_set_default_interface(bswitch, OC_IF_A);
   *   oc_resource_set_discoverable(bswitch, true);
   *   oc_resource_set_request_handler(bswitch, OC_GET, get_switch, NULL);
   *   oc_resource_set_request_handler(bswitch, OC_PUT, put_switch, NULL);
   *   oc_resource_set_request_handler(bswitch, OC_POST, post_switch, NULL);
   *   oc_add_resource(bswitch);
   * }
   * ```
   *
   * @see init
   * @see oc_new_resource
   * @see oc_resource_bind_resource_interface
   * @see oc_resource_set_default_interface
   * @see oc_resource_bind_resource_type
   * @see oc_resource_make_public
   * @see oc_resource_set_discoverable
   * @see oc_resource_set_observable
   * @see oc_resource_set_periodic_observable
   * @see oc_resource_set_properties_cbs
   * @see oc_resource_set_request_handler
   * @see oc_add_resource
   */
  void (*register_resources)(void);
#endif /* OC_SERVER */

#ifdef OC_CLIENT
  /**
   * Callback invoked when the stack is ready to issue discovery requests.
   *
   * Callback is invoked after the device initialization callback.
   *
   * Example:
   * ```
   * static void issue_requests(void)
   * {
   *   oc_do_ip_discovery("oic.r.switch.binary", &discovery, NULL);
   * }
   * ```
   *
   * @see init
   * @see oc_do_ip_discovery
   * @see oc_do_ip_discovery_at_endpoint
   * @see oc_do_site_local_ipv6_discovery
   * @see oc_do_realm_local_ipv6_discovery
   */
  void (*requests_entry)(void);
#endif /* OC_CLIENT */
} oc_handler_t;

/**
 * Callback invoked during oc_init_platform(). The purpose is to add any
 * additional platform properties that are not supplied to oc_init_platform()
 * function call.
 *
 * Example:
 * ```
 * static void set_additional_platform_properties(void *data)
 * {
 *   (void)data;
 *   // Manufactures Details Link
 *   oc_set_custom_platform_property(mnml,
 * "http://www.example.com/manufacture");
 *   // Model Number
 *   oc_set_custom_platform_property(mnmo, "Model No1");
 *   // Date of Manufacture
 *   oc_set_custom_platform_property(mndt,"2020/01/17");
 *   //Serial Number
 *   oc_set_custom_platform_property(mnsel, "1234567890");
 * }
 *
 * static int app_init(void)
 * {
 *   int ret = oc_init_platform("My Platform",
 * set_additional_platform_properties, NULL); ret |= oc_add_device("/oic/d",
 * "oic.d.light", "My light", "ocf.1.0.0", "ocf.res.1.0.0", NULL, NULL); return
 * ret;
 * }
 * ```
 *
 * @param data context pointer that comes from the oc_add_device() function
 *
 * @see oc_add_device
 * @see oc_set_custom_device_property
 */
typedef void (*oc_init_platform_cb_t)(void *data);

/**
 * Callback invoked during oc_add_device(). The purpose is to add any additional
 * device properties that are not supplied to oc_add_device() function call.
 *
 * Example:
 * ```
 * static void set_device_custom_property(void *data)
 * {
 *   (void)data;
 *   oc_set_custom_device_property(purpose, "desk lamp");
 * }
 *
 * static int app_init(void)
 * {
 *   int ret = oc_init_platform("My Platform", NULL, NULL);
 *   ret |= oc_add_device("/oic/d", "oic.d.light", "My light", "ocf.1.0.0",
 *                        "ocf.res.1.0.0", set_device_custom_property, NULL);
 *   return ret;
 * }
 * ```
 *
 * @param[in] data context pointer that comes from the oc_init_platform()
 * function
 *
 * @see oc_add_device
 * @see oc_set_custom_device_property
 */
typedef void (*oc_add_device_cb_t)(void *data);

/**
 * Register and call handler functions responsible for controlling the
 * IoTivity-lite stack.
 *
 * This will initialize the IoTivity-lite stack.
 *
 * Before initializing the stack, a few setup functions may need to be called
 * before calling oc_main_init those functions are:
 *
 * - oc_set_con_res_announced()
 * - oc_set_factory_presets_cb()
 * - oc_set_max_app_data_size()
 * - oc_set_random_pin_callback()
 * - oc_storage_config()
 *
 * Not all of the listed functions must be called before calling oc_main_init.
 *
 * @param[in] handler struct containing pointers callback handler functions
 *                    responsible for controlling the IoTivity-lite application
 * @return
 *  - `0` if stack has been initialized successfully
 *  - a negative number if there is an error in stack initialization
 *
 * @see oc_set_con_res_announced
 * @see oc_set_factory_presets_cb
 * @see oc_set_max_app_data_size
 * @see oc_set_random_pin_callback
 * @see oc_storage_config
 */
int oc_main_init(const oc_handler_t *handler);
oc_clock_time_t oc_main_poll(void);

/**
 * Shutdown and free all stack related resources
 */
void oc_main_shutdown(void);

/**
 * Callback invoked by the stack initialization to perform any
 * "factory settings", e.g., this may be used to load a manufacturer
 * certificate.
 *
 * The following example illustrates the method of loading a manufacturer
 * certificate chain (end-entity certificate, intermediate CA certificate, and
 * root CA certificate) using oc_pki_xxx APIs.
 *
 * Example:
 * ```
 * void factory_presets_cb(size_t device, void *data)
 * {
 *   (void)device;
 *   (void)data;
 * #if defined(OC_SECURITY) && defined(OC_PKI)
 *   char cert[8192];
 *   size_t cert_len = 8192;
 *   if (read_pem("pki_certs/ee.pem", cert, &cert_len) < 0) {
 *     PRINT("ERROR: unable to read certificates\n");
 *     return;
 *   }
 *
 *   char key[4096];
 *   size_t key_len = 4096;
 *   if (read_pem("pki_certs/key.pem", key, &key_len) < 0) {
 *     PRINT("ERROR: unable to read private key");
 *     return;
 *   }
 *
 *   int ee_credid = oc_pki_add_mfg_cert(0, (const unsigned char *)cert,
 * cert_len, (const unsigned char *)key, key_len);
 *
 *   if (ee_credid < 0) {
 *     PRINT("ERROR installing manufacturer EE cert\n");
 *     return;
 *   }
 *
 *   cert_len = 8192;
 *   if (read_pem("pki_certs/subca1.pem", cert, &cert_len) < 0) {
 *     PRINT("ERROR: unable to read certificates\n");
 *     return;
 *   }
 *
 *   int subca_credid = oc_pki_add_mfg_intermediate_cert(
 *     0, ee_credid, (const unsigned char *)cert, cert_len);
 *
 *   if (subca_credid < 0) {
 *     PRINT("ERROR installing intermediate CA cert\n");
 *     return;
 *   }
 *
 *   cert_len = 8192;
 *   if (read_pem("pki_certs/rootca1.pem", cert, &cert_len) < 0) {
 *     PRINT("ERROR: unable to read certificates\n");
 *     return;
 *   }
 *
 *   int rootca_credid =
 *     oc_pki_add_mfg_trust_anchor(0, (const unsigned char *)cert, cert_len);
 *   if (rootca_credid < 0) {
 *     PRINT("ERROR installing root cert\n");
 *     return;
 *   }
 *
 *   oc_pki_set_security_profile(0, OC_SP_BLACK, OC_SP_BLACK, ee_credid);
 * #endif // OC_SECURITY && OC_PKI
 * }
 * ```
 * @param[in] device number of the device
 * @param[in] data context pointer that comes from the
 *                 oc_set_factory_presets_cb() function
 *
 * @see oc_set_factory_presets_cb
 * @see oc_pki_add_mfg_cert
 * @see oc_pki_add_mfg_intermediate_cert
 * @see oc_pki_add_mfg_trust_anchor
 * @see oc_pki_set_security_profile
 */
//typedef void (*oc_factory_presets_cb_t)(size_t device, void *data);

/**
 * Set the factory presets callback.
 *
 * The factory presets callback is called by the stack to enable per-device
 * presets.
 *
 * @note oc_set_factory_presets_cb() must be called before oc_main_init().
 *
 * @param[in] cb oc_factory_presets_cb_t function pointer to be called
 * @param[in] data context pointer that is passed to the oc_factory_presets_cb_t
 *                 the pointer must be a valid point till after oc_main_init()
 *                 call completes.
 */
//void oc_set_factory_presets_cb(oc_factory_presets_cb_t cb, void *data);

/**
 * Add an ocf device to the the stack.
 *
 * This function is typically called as part of the stack initialization
 * process from inside the `init` callback handler.
 *
 * The `oc_add_device` function may be called as many times as needed.
 * Each call will add a new device to the stack with its own port address.
 * Each device is automatically assigned a number starting with zero and
 * incremented by one each time the function is called. This number is not
 * returned therefore it is important to know the order devices are added.
 *
 * Example:
 * ```
 * //app_init is an instance of the `init` callback handler.
 * static int app_init(void)
 * {
 *   int ret = oc_init_platform("Refrigerator", NULL, NULL);
 *   ret |= oc_add_device("/oic/d", "oic.d.refrigeration", "My fridge",
 *                        "ocf.2.0.5", "ocf.res.1.0.0,ocf.sh.1.0.0",
 *                        NULL, NULL);
 *   ret |= oc_add_device("/oic/d", "oic.d.thermostat", "My thermostat",
 *                        "ocf.2.0.5", "ocf.res.1.0.0,ocf.sh.1.0.0",
 *                        NULL, NULL);
 *   return ret;
 * }
 * ```
 *
 * @param uri the The device URI.  The wellknown default URI "/oic/d" is hosted
 *            by every server. Used to device specific information.
 * @param rt the resource type
 * @param name the user readable name of the device
 * @param spec_version The version of the OCF Server.  This is the "icv" device
 *                     property
 * @param data_model_version Spec version of the resource and device
 * specifications to which this device data model is implemtned. This is the
 * "dmv" device property
 * @param add_device_cb callback function invoked during oc_add_device(). The
 *                      purpose is to add additional device properties that are
 *                      not supplied to oc_add_device() function call.
 * @param data context pointer that is passed to the oc_add_device_cb_t
 *
 * @return
 *   - `0` on success
 *   - `-1` on failure
 *
 * @see init
 */
int oc_add_device(const char *uri, const char *rt, const char *name,
                  const char *spec_version, const char *data_model_version,
                  oc_add_device_cb_t add_device_cb, void *data);

/**
 * Set custom device property
 *
 * The purpose is to add additional device properties that are not supplied to
 * oc_add_device() function call. This function will likely only be used inside
 * the oc_add_device_cb_t().
 *
 * @param[in] prop the name of the custom property being added to the device
 * @param[in] value the value of the custom property being added to the device
 *
 * @see oc_add_device_cb_t for example code using this function
 * @see oc_add_device
 */
//#define oc_set_custom_device_property(prop, value)                             \
//  oc_rep_set_text_string(root, prop, value)

/**
 * Initialize the platform.
 *
 * This function is typically called as part of the stack initialization
 * process from inside the `init` callback handler.
 *
 * @param[in] mfg_name the name of the platform manufacture
 * @param[in] init_platform_cb callback function invoked during
 * oc_init_platform(). The purpose is to add additional device properties that
 * are not supplied to oc_init_platform() function call.
 * @param[in] data context pointer that is passed to the oc_init_platform_cb_t
 *
 * @return
 *   - `0` on success
 *   - `-1` on failure
 *
 * @see init
 * @see oc_init_platform_cb_t
 */
int oc_init_platform(const char *mfg_name,
                     oc_init_platform_cb_t init_platform_cb, void *data);

/**
 * Set custom platform property.
 *
 * The purpose is to add additional platfrom properties that are not supplied to
 * oc_init_platform() function call. This function will likely only be used
 * inside the oc_init_platform_cb_t().
 *
 * @param[in] prop the name of the custom property being added to the platform
 * @param[in] value the value of the custom property being added to the platform
 *
 * @see oc_init_platform_cb_t for example code using this function
 * @see oc_init_platform
 */
//#define oc_set_custom_platform_property(prop, value)                           \
//  oc_rep_set_text_string(root, prop, value)

/**
 * Returns whether the oic.wk.con resource is advertised.
 *
 * @return
 *  - true if advertised (default)
 *  - false if not
 *
 * @see oc_set_con_res_announced
 * @see oc_set_con_write_cb
 */
bool oc_get_con_res_announced(void);

/**
  @brief Sets whether the oic.wk.con res is announced.
  @note This should be set before invoking \c oc_main_init().
  @param[in] announce true to announce (default) or false if not
  @see oc_get_con_res_announced
  @see oc_set_con_write_cb
*/
void oc_set_con_res_announced(bool announce);

/* Server side */
/**
  @defgroup doc_module_tag_server_side Server side
  Optional group of functions OCF server support.
  @{
*/
/**
 * Allocate and populate a new oc_resource_t.
 *
 * Resources are the primary interface between code and real world devices.
 *
 * Each resource has a Uniform Resource Identifier (URI) that identifies it.
 * All resources **must** specify one or more Resource Types to be considered a
 * valid resource. The number of Resource Types is specified by the
 * `num_resource_types` the actual Resource Types are added later using the
 * oc_resource_bind_resource_type() function.
 *
 * The resource is populated with a default interface OC_IF_BASELINE.
 *
 * Many properties associated with a resource are set or modified after the
 * new resource has been created.
 *
 * The resource is not added to the device till oc_add_resource() is called.
 *
 * Example:
 * ```
 * static void register_resources(void)
 * {
 *   oc_resource_t *bswitch = oc_new_resource("light switch", "/switch", 1, 0);
 *   oc_resource_bind_resource_type(bswitch, "oic.r.switch.binary");
 *   oc_resource_bind_resource_interface(bswitch, OC_IF_A);
 *   oc_resource_set_default_interface(bswitch, OC_IF_A);
 *   oc_resource_set_observable(bswitch, true);
 *   oc_resource_set_discoverable(bswitch, true);
 *   oc_resource_set_request_handler(bswitch, OC_GET, get_switch, NULL);
 *   oc_resource_set_request_handler(bswitch, OC_POST, post_switch, NULL);
 *   oc_resource_set_request_handler(bswitch, OC_PUT, put_switch, NULL);
 *   oc_add_resource(bswitch);
 * }
 * ```
 *
 * @param[in] name the name of the new resource this will set the property `n`
 * @param[in] uri the Uniform Resource Identifier for the resource
 * @param[in] num_resource_types the number of Resource Types that will be
 *                               added/bound to the resource
 * @param[in] device index of the logical device the resource will be added to
 *
 * @see oc_resource_bind_resource_interface
 * @see oc_resource_set_default_interface
 * @see oc_resource_bind_resource_type
 * @see oc_process_baseline_interface
 * @see oc_resource_set_discoverable
 * @see oc_resource_set_periodic_observable
 * @see oc_resource_set_request_handler
 */
oc_resource_t *oc_new_resource(const char *name, const char *uri,
                               uint8_t num_resource_types, size_t device);

/**
 * Add the supported interface(s) to the resource.
 *
 * Resource interfaces specify how the code is able to interact with the
 * resource
 *
 * The `iface_mask` is bitwise OR of the following interfaces:
 *  - `OC_IF_BASELINE` ("oic.if.baseline") baseline interface allow GET,
 *                      PUT/POST, and notify/observe operations.
 *  - `OC_IF_LL` ("oic.if.ll") The links list interface is a specifically
 *               designed to provide a list of links pointing to other
 * resources. Links list interfaces allow GET, and notify/observe operations.
 *  - `OC_IF_B` ("oic.if.b") batch interface. The batch interface is used to
 *              interact with a collection of resources at the same time.
 *  - `OC_IF_R` ("oic.if.r") a read-only interface.  A read-only interface
 * allows GET, and notify/observe operations.
 *  - `OC_IF_RW` ("oir.if.rw") a read-write interface.  A read-write interface
 *                allows GET, PUT/POST, and notify/observe operations.
 *  - `OC_IF_A` ("oic.if.a") an actuator interface. An actuator interface allows
 *              GET, PUT/POST, and notify/observe operations.
 *  - `OC_IF_S` ("oic.if.s") a sensor interface.  A sensor interface allows GET,
 *              and notify/observe operations.
 *  - `OC_IC_CREATE` ("oic.if.create") used to create new resources in a
 *                   collection.
 *
 * The read-write and actuator interfaces are very similar and sometimes hard to
 * differentiate when one should be used over another.  In general an actuator
 * interface is used when it modifies the real world value. e.g. turn on light,
 * increase temperature, open vent.
 *
 * The read-only and sensor are also very similar in general a sensor value is
 * read directly or indirectly from a real world sensor.
 *
 * @param[in] resource the resource that the interface(s) will be added to
 * @param[in] iface_mask a bitwise ORed list of all interfaces supported by the
 *                       resource.
 * @see oc_interface_mask_t
 * @see oc_resource_set_default_interface
 */
void oc_resource_bind_resource_interface(oc_resource_t *resource,
                                         oc_interface_mask_t iface_mask);

/**
 * Select the default interface.
 *
 * The default interface must be one of the resources specified in the
 * oc_resource_bind_resource_interface() function.
 *
 * If a request to the resource comes in and the interface is not specified
 * then the default interface will be used to service the request.
 *
 * If the default interface is not set then the OC_IF_BASELINE will be used
 * by the stack.
 *
 * @param[in] resource the resource that the default interface will be set on
 * @param[in] iface_mask a single interface that will will be used as the
 *                       default interface
 */
void oc_resource_set_default_interface(oc_resource_t *resource,
                                       oc_interface_mask_t iface_mask);
/**
 * Add a Resource Type "rt" property to the resource.
 *
 * All resources require at least one Resource Type. The number of Resource
 * Types the resource contains is declared when the resource it created using
 * oc_new_resource() function.
 *
 * Resource Types use a dot "." naming scheme e.g. `oic.r.switch.binary`.
 * Resource Types starting with `oic` are reserved for a OCF defined Resource
 * Types.  Developers are strongly encouraged to try and use an OCF defined
 * Resource Type vs. creating their own. A repository of OCR defined resources
 * can be found on oneiota.org.
 *
 * Multi-value "rt" Resource means a resource with multiple Resource Types. i.e.
 * oc_resource_bind_resource_type is called multiple times for a single
 * resource. When using a Mulit-value Resource the different resources
 * properties must not conflict.
 *
 * @param[in] resource the resource that the Resource Type will be set on
 * @param[in] type the Resource Type to add to the Resource Type "rt" property
 *
 * @see oc_new_resource
 * @see oc_device_bind_resource_type
 */
void oc_resource_bind_resource_type(oc_resource_t *resource, const char *type);

/**
 * Add a Resource Type "rt" property to the an /oic/d resource.
 *
 * This function can be used to bind a new Resource Type to a logical device's
 * /oic/d resource.
 *
 * @param[in] device index of a logical device
 * @param[in] type the Resource type to add to the Resource Type "rt" property
 */
//void oc_device_bind_resource_type(size_t device, const char *type);

/**
 * Helper function used when responding to a GET request to add Common
 * Properties to a GET response.
 *
 * This add Common Properties name ("n"), Interface ("if"), and Resource Type
 * ("rt") to a GET response.
 *
 * Example:
 * ```
 * bool bswitch_state = false;
 *
 * void get_bswitch(oc_resource_t *resource, oc_interface_mask_t iface_mask,
 *                  void *data)
 * {
 *   oc_rep_start_root_object();
 *   switch (iface_mask) {
 *   case OC_IF_BASELINE:
 *     oc_process_baseline_interface(resource);
 *   // fall through
 *   case OC_IF_A:
 *     oc_rep_set_boolean(root, value, bswitch_state);
 *     break;
 *   default:
 *     break;
 *   }
 *   oc_rep_end_root_object();
 *   oc_send_response(request, OC_STATUS_OK);
 * }
 * ```
 * @param[in] resource the resource the baseline Common Properties will be read
 *            from to respond to the GET request
 */
void oc_process_baseline_interface(oc_resource_t *resource);

/**
  @defgroup doc_module_tag_collections Collection Support
  Optional group of functions to support OCF compliant collections.
  @{
*/

/**
  @brief Creates a new empty collection.

  The collection is created with interfaces \c OC_IF_BASELINE,
  \c OC_IF_LL (also default) and \c OC_IF_B. Initially it is neither
  discoverable nor observable.

  The function only allocates the collection. Use
  \c oc_add_collection() after the setup of the collection
  is complete.
  @param name name of the collection
  @param uri Unique URI of this collection. Must not be NULL.
  @param num_resource_types Number of resources the caller will
   bind with this resource (e.g. by invoking
   \c oc_resource_bind_resource_type(col, OIC_WK_COLLECTION)). Must
   be 1 or higher.
  @param device The internal device that should carry this collection.
   This is typically 0.
  @return A pointer to the new collection (actually oc_collection_t*)
   or NULL if out of memory.
  @see oc_add_collection
  @see oc_collection_add_link
*/
//oc_resource_t *oc_new_collection(const char *name, const char *uri,
//                                 uint8_t num_resource_types, size_t device);

/**
  @brief Deletes the specified collection.

  The function removes the collection from the internal list of collections
  and releases all direct resources and links associated with this collection.

  @note The function does not delete the resources set in the links.
   The caller needs to do this on her/his own in case these are
   no longer required.

  @param collection The pointer to the collection to delete.
   If this is NULL, the function does nothing.
  @see oc_collection_get_links
  @see oc_delete_link
*/
//void oc_delete_collection(oc_resource_t *collection);

/**
  @brief Creates a new link for collections with the specified resource.
  @param resource Resource to set in the link. The resource is not copied.
   Must not be NULL.
  @return The created link or NULL if out of memory or \c resource is NULL.
  @see oc_delete_link
  @see oc_collection_add_link
  @see oc_new_resource
*/
//oc_link_t *oc_new_link(oc_resource_t *resource);

/**
  @brief Deletes the link.
  @note The function neither removes the resource set on this link
   nor does it remove it from any collection.
  @param link The link to delete. The function does nothing, if
   the parameter is NULL.
*/
//void oc_delete_link(oc_link_t *link);

/**
  @brief Adds a relation to the link.
  @param link Link to add the relation to. Must not be NULL.
  @param rel Relation to add. Must not be NULL.
*/
//void oc_link_add_rel(oc_link_t *link, const char *rel);

/**
  @brief Adds a link parameter with specified key and value.
  @param link Link to which to add a link parameter. Must not be NULL.
  @param key Key to identify the link parameter. Must not be NULL.
  @param value Link parameter value. Must not be NULL.
*/
//void oc_link_add_link_param(oc_link_t *link, const char *key,
//                            const char *value);

/**
  @brief Adds the link to the collection.
  @param collection Collection to add the link to. Must not be NULL.
  @param link Link to add to the collection. The link is not copied.
   Must not be NULL. Must not be added again to this or a different
   collection or a list corruption will occur. To re-add it, remove
   the link first.
  @see oc_new_link
  @see oc_collection_remove_link
*/
//void oc_collection_add_link(oc_resource_t *collection, oc_link_t *link);

/**
  @brief Removes a link from the collection.
  @param collection Collection to remove the link from. Does nothing
   if this is NULL.
  @param link The link to remove. Does nothing if this is NULL or not
   part of the collection. The link and its resource are not freed.
*/
//void oc_collection_remove_link(oc_resource_t *collection, oc_link_t *link);

/**
  @brief Returns the list of links belonging to this collection.
  @param collection Collection to get the links from.
  @return All links of this collection. The links are not copied. Returns
   NULL if the collection is NULL or contains no links.
  @see oc_collection_add_link
*/
//oc_link_t *oc_collection_get_links(oc_resource_t *collection);

/**
  @brief Adds a collection to the list of collections.

  If the caller makes the collection discoverable, then it will
  be included in the collection discovery once it has been added
  with this function.
  @param collection Collection to add to the list of collections.
   Must not be NULL. Must not be added twice or a list corruption
   will occur. The collection is not copied.
  @see oc_resource_set_discoverable
  @see oc_new_collection
*/
//void oc_add_collection(oc_resource_t *collection);

/**
  @brief Gets all known collections.
  @return All collections that have been added via
   \c oc_add_collection(). The collections are not copied.
   Returns NULL if there are no collections. Collections created
   only via \c oc_new_collection() but not added will not be
   returned by this function.
*/
//oc_resource_t *oc_collection_get_collections(void);

//bool oc_collection_add_supported_rt(oc_resource_t *collection, const char *rt);

//bool oc_collection_add_mandatory_rt(oc_resource_t *collection, const char *rt);

//#ifdef OC_COLLECTIONS_IF_CREATE
//typedef oc_resource_t *(*oc_resource_get_instance_t)(const char *,
//                                                     oc_string_array_t *,
//                                                     oc_resource_properties_t,
//                                                     oc_interface_mask_t,
//                                                     size_t);

//typedef void (*oc_resource_free_instance_t)(oc_resource_t *);

//bool oc_collections_add_rt_factory(const char *rt,
//                                   oc_resource_get_instance_t get_instance,
//                                   oc_resource_free_instance_t free_instance);
//#endif    /* OC_COLLECTIONS_IF_CREATE */
/** @} */ // end of doc_module_tag_collections

/**
 * Expose unsecured coap:// endpoints (in addition to secured coaps://
 * endpoints) for this resource in /oic/res.
 *
 * @note While the resource may advertise unsecured endpoints, the resource
 *       shall remain inaccessible until the hosting device is configured with
 *       an anon-clear Access Control Entry (ACE).
 *
 * @param[in] resource the resource to make public
 *
 * @see oc_new_resource
 */
//void oc_resource_make_public(oc_resource_t *resource);

/**
 * Specify if a resource can be found using OCF discover mechanisms.
 *
 * @param[in] resource to specify as discoverable or non-discoverable
 * @param[in] state if true the resource will be discoverable if false the
 *                  resource will be non-discoverable
 *
 * @see oc_new_resource for example code using this function
 */
//void oc_resource_set_discoverable(oc_resource_t *resource, bool state);

/**
 * Specify that a resource should notify clients when a property has been
 * modified.
 *
 * @note this function can be used to make a periodic observable resource
 *       unobservable.
 *
 * @param[in] resource the resource to specify the observability
 * @param[in] state true to make resource observable, false to make resource
 *                  unobservable
 *
 * @see oc_new_resource to see example code using this function
 * @see oc_resource_set_periodic_observable
 */
//void oc_resource_set_observable(oc_resource_t *resource, bool state);

/**
 * The resource will periodically notify observing clients of is property
 * values.
 *
 * The oc_resource_set_observable() function can be used to turn off a periodic
 * observable resource.
 *
 * Setting a `seconds` frequency of zero `0` is invalid and will result in an
 * invalid resource.
 *
 * @param[in] resource the resource to specify the periodic observability
 * @param[in] seconds the frequency in seconds that the resource will send out
 *                    an notification of is property values.
 */
//void oc_resource_set_periodic_observable(oc_resource_t *resource,
//                                         uint16_t seconds);

/**
 * Specify a request_callback for GET, PUT, POST, and DELETE methods
 *
 * All resources must provide at least one request handler to be a valid
 * resource.
 *
 * method types:
 * - `OC_GET` the `oc_request_callback_t` is responsible for returning the
 * current value of all of the resource properties.
 * - `OC_PUT` the `oc_request_callback_t` is responsible for updating one or
 * more of the resource properties.
 * - `OC_POST` the `oc_request_callback_t` is responsible for updating one or
 * more of the resource properties. The callback may also be responsible for
 *         creating new resources.
 * - `OC_DELETE` the `oc_request_callback_t` is responsible for deleting a
 * resource
 *
 * @note Some methods may never by invoked based on the resources Interface as
 *       well as the provisioning permissions of the client.
 *
 * @param[in] resource the resource the callback handler will be registered to
 * @param[in] method specify if type method the callback is responsible for
 *                   handling
 * @param[in] callback the callback handler that will be invoked when a the
 *                     method is called on the resource.
 * @param[in] user_data context pointer that is passed to the
 *                      oc_request_callback_t. The pointer must remain valid as
 *                      long as the resource exists.
 *
 * @see oc_new_resource to see example code using this function
 */
void oc_resource_set_request_handler(oc_resource_t *resource,
                                     oc_method_t method,
                                     oc_request_callback_t callback,
                                     void *user_data);

//void oc_resource_set_properties_cbs(oc_resource_t *resource,
//                                    oc_get_properties_cb_t get_properties,
//                                    void *get_propr_user_data,
//                                    oc_set_properties_cb_t set_properties,
//                                    void *set_props_user_data);

/**
 * Add a resource to the IoTivity stack.
 *
 * The resource will be validated then added to the stack.
 *
 * @param[in] resource the resource to add to the stack
 *
 * @return
 *  - true: the resource was successfully added to the stack.
 *  - false: the resource can not be added to the stack.
 */
bool oc_add_resource(oc_resource_t *resource);

/**
 * Remove a resource from the IoTivity stack and delete the resource.
 *
 * Any resource observers will automatically be removed.
 *
 * This will free the memory associated with the resource.
 *
 * @param[in] resource the resource to delete
 *
 * @return
 *  - true: when the resource has been deleted and memory freed.
 *  - false: there was an issue deleting the resource.
 */
//bool oc_delete_resource(oc_resource_t *resource);

/**
  @brief Callback for change notifications from the oic.wk.con resource.

  This callback is invoked to notify a change of one or more properties
  on the oic.wk.con resource. The \c rep parameter contains all properties,
  the function is not invoked for each property.

  When the function is invoked, all properties handled by the stack are
  already updated. The callee can use the invocation to optionally store
  the new values persistently.

  Once the callback returns, the response will be sent to the client
  and observers will be notified.

  @note As of now only the attribute "n" is supported.
  @note The callee shall not block for too long as the stack is blocked
   during the invocation.

  @param device_index index of the device to which the change was
   applied, 0 is the first device
  @param rep list of properties and their new values
*/
//typedef void (*oc_con_write_cb_t)(size_t device_index, oc_rep_t *rep);

/**
  @brief Sets the callback to receive change notifications for
   the oic.wk.con resource.

  The function can be used to set or unset the callback. Whenever
  an attribute of the oic.wk.con resource is changed, the callback
  will be invoked.

  @param callback The callback to register or NULL to unset it.
   If the function is invoked a second time, then the previously
   set callback is simply replaced.
*/
/*
void oc_set_con_write_cb(oc_con_write_cb_t callback);

void oc_init_query_iterator(void);
int oc_iterate_query(oc_request_t *request, char **key, size_t *key_len,
                     char **value, size_t *value_len);
bool oc_iterate_query_get_values(oc_request_t *request, const char *key,
                                 char **value, int *value_len);
int oc_get_query_value(oc_request_t *request, const char *key, char **value);
*/
void oc_send_response(oc_request_t *request, oc_status_t response_code);
/*
void oc_ignore_request(oc_request_t *request);

void oc_indicate_separate_response(oc_request_t *request,
                                   oc_separate_response_t *response);
void oc_set_separate_response_buffer(oc_separate_response_t *handle);
void oc_send_separate_response(oc_separate_response_t *handle,
                               oc_status_t response_code);

int oc_notify_observers(oc_resource_t *resource);
*/
#ifdef __cplusplus
}
#endif

/** @} */ // end of doc_module_tag_server_side

/**
  @defgroup doc_module_tag_client_state Client side
  Client side support functions
  @{
*/
#include "oc_client_state.h"

#ifdef __cplusplus
extern "C" {
#endif

bool oc_do_get(const char *uri, bool nx_secure_request, oc_endpoint_t *endpoint, const char *query,
               oc_response_handler_t handler, oc_qos_t qos, void *user_data);

bool oc_init_post(const char *uri, oc_endpoint_t *endpoint, const char *query,
                  oc_response_handler_t handler, oc_qos_t qos, void *user_data);

bool oc_do_post(bool nx_secure_request);

#if NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS
bool oc_do_observe(const char *uri, oc_endpoint_t *endpoint, const char *query,
                   oc_response_handler_t handler, oc_qos_t qos,
                   void *user_data);

bool oc_stop_observe(const char *uri, oc_endpoint_t *endpoint);
#endif // NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS

/**
  @defgroup doc_module_tag_common_operations Common operations
  @{
*/
//void oc_set_immutable_device_identifier(size_t device, oc_uuid_t *piid);

void oc_set_delayed_callback(void *cb_data, oc_trigger_t callback,
                             uint16_t seconds);
void oc_remove_delayed_callback(void *cb_data, oc_trigger_t callback);

/** @} */ // end of doc_module_tag_common_operations
#ifdef __cplusplus
}
#endif

#pragma GCC diagnostic pop
#endif /* OC_API_H */

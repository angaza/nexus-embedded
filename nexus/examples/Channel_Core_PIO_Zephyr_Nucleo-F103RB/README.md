# Nexus Embedded (Zephyr) Example

Sample program demonstrating Nexus Library on an
embedded target, using Zephyr RTOS.

There are various builds, demonstrating different functionalities of the
Nexus Library (Nexus Keycode, Nexus Channel, Nexus Channel Core).

## Target

STM32 Nucleo-F103RB development board (STM32F103RB target), using Zephyr RTOS
for ease of demonstration and modification. This board is more capable than
many target devices for Nexus Channel Core and allows for rapid prototyping,
debugging, and development of new Nexus Channel Core resource types prior to
moving to more constrained hardware.

This project relies on features of the Nucleo development board,
specifically the virtual COM port provided by the ST-Link on that board.
This port is used to transfer data between the development board and a
host computer running a serial terminal at **115200 Baud**, 8N1 UART.

On Linux, this *typically* shows up as a serial port at `/dev/ttyACM0` after
the Nucleo-F103RB is connected via USB.

## Setup Steps

1. [Install PlatformIO IDE in VSCode](https://platformio.org/install/ide?install=vscode)
2. Clone this Github repository
3. Open the PlatformIO IDE and select "Open Project"
4. Open the project by selecting the directory named "Channel_Core_PIO_Zephyr_Nucleo-F103RB"
5. Connect your Nucleo-F103RB board to a USB port on your computer
6. Open the PlatformIO perspective in the IDE and expand "Project Tasks". Select a build and then click "Clean"
7. Wait until the Clean is completed, and then click "Build"
8. Wait until the Build is completed, and then click "Upload and Monitor"
9. Wait until the Upload is complete and observe the Terminal in the IDE. It should prompt for input
10. Go to the steps specific to the demonstration build you are running (below)

## Build/Run

This example is configured as a [PlatformIO](https://platformio.org/install) project.
You can optionally download PlatformIO, download this example project, and
import it using the "Add Existing" functionality in PlatformIO. This will
allow you to modify, build, and download the code onto your own
Nucleo-F103RB development board.

A minimal console is configured in all examples so that when the F103RB board
is initially booted up, `---Nexus Channel Core Demonstration---` will be
printed to the terminal (on the virtual COM port connected to USART2).
This can be used to confirm that the board is powered and connected correctly.

Additionally, some demo builds allow for user input via this console, a prompt
indicating `demo> ` will allow text input via the USART2 console. This functionality
is implemented by the `demo_console` module.

Functions required to be implemented by the product in order for Nexus to work
properly are contained in the `src/nxp_reference_implementations` folder.

Operationally, the demonstration program is structured into two primary Zephyr threads:

1. `main` thread. This executes the `main` function in `main.c`, and is used
to initialize product and Nexus functionality, as well as handle user input
from the `demo_console`.

2. `process_nexus` thread. This thread calls `nx_common_process`, and will
be woken up and called whenever `nxp_common_request_processing` is called by
the Nexus library to request processing. Any CPU intensive operations
performed by Nexus are done in this thread (to avoid long-running operations
in interrupts).

A background `logging` thread is also compiled in by default, which can be
disabled by setting `CONFIG_LOG=n` in `zephyr/prj.conf`. If this is disabled,
much of the demonstration functionality via USART2 console may be nonfunctional.
However, disabling logging will reduce RAM and flash use.

The default Zephyr `idle` thread is also present, which takes care of putting
the CPU into a low-power state when there is no work to process.

Development boards other than the ST Nucleo-F103RB may be supported
(but are not tested or formally supported), and may be used by modifying the
appropriate section of the `platformio.ini` file and relevant overlay files
in the `zephyr` folder. Please contact Angaza for more information on porting
the Zephyr example to other boards/MCUs.

## Demo: "Nexus Keycode" Demo

('nucleo_f103rb-keycode-only' PlatformIO build)

This build demonstrates Nexus Keycode running on a development board, with
keycode input from the demo console on USART2. It demonstrates the following
implementation details:

* A nonvolatile storage abstraction to write/read data to flash NV (`flash_filesystem`)
* Implementation of Nexus PAYG ID/key provisioning and storage (`product_nexus_identity`)
* Implementation of product-side PAYG credit storage (`product_payg_state_manager`)
* Implementation of `nxp_keycode` and `nxp_common` product-side functions

### Suggested Demonstration Steps (Nexus Keycode)

The `demo_console` is already configured to expect user input beginning with
`*` as a Nexus Keycode. The keycodes below assume a demonstration keycode
secret key of `0xDE, 0xAD, 0xBE, 0xEF, 0x10, 0x20, 0x30, 0x40, 0x04, 0x03, 0x02, 0x01, 0xFE, 0xEB, 0xDA, 0xED`
(already configured in this example program).

* Type `*10029054295608#` ("Add Credit", 24 hours, ID=1), notice "New/applied" keycode feedback.
* Type `pc`, see remaining PAYG credit is 86400 seconds (24 hours).
* Type `*10029054295608#`, notice "Old/duplicate" keycode feedback.
* Type `*31145334081050#` ("Wipe IDs+Credit", ID=61), notice "New/applied" keycode feedback.
* Type `pc`, see remaining PAYG credit is 0 seconds.
* Enter `*10029054295608#`, again notice "New/applied" keycode feedback.
* Type `pc`, see remaining PAYG credit is 86400 seconds (24 hours).
* Type `*09544754240514#` ("Unlock", ID=15), see "New/applied" keycode feedback.
* Type `pc`, see remaining credit is 'unlocked'.
* Cycle power, type `pc`, confirm PAYG credit is still unlocked.

Notice especially the `product_payg_state_manager` module, which *receives and stores*
changes to credit from Nexus via `nxp_keycode` functions, and *reports* the
current credit back to Nexus via `nxp_common` functions.

`product_payg_state_manager` also periodically updates PAYG credit (every 60
seconds), and stores the result in nonvolatile storage every hour.

**Note**: This keycode demo build also is configured to use 'Rate Limiting'.
The default configuration below is used:

* User gains 1 keycode attempt every 720 seconds (12 minutes)
* Initially, user has 6 keycode attempts (persists across resets)

If a valid keycode is rejected with the message, "Keycode rate limiting is active!",
every 720 seconds another keycode entry attempt will be allowed (up to 128).

## Demo: "Single Link" Controller/Accessory

XXX update

## Demo: Channel Core (no Channel Security or Keycode)

This demo refers to the 'nucleo_f103-channel-core-only' build.

The USART2 demo console is used to visualize *application layer* communication
between two Nexus Channel Core devices, where the host computer acts as a
'client', reading or updating the state of Nexus Channel Core resources hosted
on the F103RB development board.

This demonstration does not use keycode or "Nexus Security/Link" functionality.

This demonstration configures the Nucleo-F103RB as a Nexus Channel
Core device hosting the following resources:

* [Battery resource] (https://angaza.github.io/nexus-channel-models/resource_types/core/101-battery/redoc_wrapper.html)

### Suggested Demonstration Steps (GET/POST to battery resource)

This example includes a fully-implemented battery resource compliant to the 
[resource model specification](https://angaza.github.io/nexus-channel-models/resource_types/core/101-battery/redoc_wrapper.html).

* "Upload and Monitor" the "nucleo_f103rb-channel-core-only" build.
* Wait until the Upload is complete and observe the Terminal in the IDE. It should prompt for input (`demo> `)
* Type `get`. This will retrieve the current state of the battery resource. Notice the 'th' (threshold) key has a value of 20 or 35.
* Type `post20`. This will update the battery 'low capacity' threshold to "20 percent".
* Type `get`. Notice the `th` property of the battery resource is "20".
* Type `post35`. This will update the battery 'low capacity' threshold to "35 percent".
* Type `get`. Notice the `th` property of the battery resource is "35".

The console should appear similar to below:

[!get_post_gif](sample_console_get_post.gif)

## Implementing a Custom Resource / Adding New Attributes

This example can also be used to quickly prototype new resource type
implementations. Simply modify the existing `src/battery_res.c` example code,
replacing it with the resource instance you wish to test.

For example, to add a new 'hours runtime' parameter to the battery resource:

* Pick a two-character attribute name, e.g. "hr", to be the attribute key.
* Add `hr` to the payload for GET responses in `battery_res_get_handler`
by adding the following line below `oc_rep_set_int(root, vb, battery_mv);`

```
// Always report '45 hours' of runtime. Can use a variable if desired.
oc_rep_set_int(root, hr, 45);
```

Recompile and run the demonstration again. In `GET` responses, you will notice
the new 'hr' parameter present. E.g.

```
[00:00:04.364,000] <inf> demo_console: [GET Response Handler] Received response with code 0 from Nexus ID [Authority ID 0xffff, Device ID 0x00bc614e]
[00:00:04.377,000] <inf> demo_console: [GET Response Handler] Parsing payload
[00:00:04.383,000] <inf> demo_console: [GET Response Handler] Key vb
[00:00:04.389,000] <inf> demo_console: 13100
[00:00:04.393,000] <inf> demo_console: [GET Response Handler] Key hr  <<-- newly added attribute key
[00:00:04.399,000] <inf> demo_console: 45  << -- newly added attribute value
[00:00:04.403,000] <inf> demo_console: [GET Response Handler] Key cp
[00:00:04.409,000] <inf> demo_console: 31
[00:00:04.413,000] <inf> demo_console: [GET Response Handler] Key th
[00:00:04.419,000] <inf> demo_console: 20
```

If creating a completely new/different resource, also modify
`battery_res_init` to indicate the correct "RTR" (resource type) and URI
you wish to use (instead of `/batt`) to refer to this resource. See
[Nexus Channel Core Resource Models](https://angaza.github.io/nexus-channel-models/)
for more information on resource model generation, or contact Angaza.

# Nexus Channel Accessory Example

Sample program demonstrating the use of Nexus Channel on an "Accessory" device,
running on an STM32F103RB MCU, using Zephyr RTOS.

This demo project is not standalone, and is is intended for use with the
"Nexus Channel Controller Example" demonstration running on another, separate
board.

## Target

STM32 Nucleo-F103RB development board (STM32F103RB target), using Zephyr RTOS
for ease of demonstration and modification.

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
4. Open the project by selecting the directory named "Nexus_Channel_Accessory_F103RB"
5. Connect your Nucleo-F103RB board to a USB port on your computer
6. Open the PlatformIO perspective in the IDE, and then click "Clean"
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

A minimal console is configured so that when the F103RB board
is initially booted up, `---Nexus Embedded Demonstration Started (ACCESSORY)---` will be
printed to the terminal (on the virtual COM port connected to USART2).
This can be used to confirm that the board is powered and connected correctly.

Functions required to be implemented by the product in order for Nexus to work
properly are contained in the `src/nxp_reference_implementations` folder.

Operationally, the demonstration program is structured into two primary Zephyr threads:

1. `main` thread. This executes the `main` function in `main.c`, and is used
to initialize product and Nexus functionality, then exits.

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

## Demo: "Nexus Channel Accessory" Demonstration

('channel_accessory' PlatformIO build)

This build demonstrates a device acting as a "Nexus Channel Accessory",
which can be connected (via USART2) to a Nexus Channel Controller device.
Once physically connected, the two devices can be securely linked (by entering
a Nexus Channel "Link" Origin Command Keycode into the controller), and
this accessory device will then mirror the PAYG credit state of the controller.

This project demonstrates the following implementation details:

* A nonvolatile storage abstraction to write/read data to flash NV (`flash_filesystem`)
* Implementation of Nexus PAYG ID/key provisioning and storage (`product_nexus_identity`)
* Implementation of product-side PAYG credit storage (`product_payg_state_manager`)
* Implementation of `nxp_channel` and `nxp_common` product-side functions
* Sending/Receiving Nexus Channel messages on USART1

This demo is meant to be run alongside the "Nexus Channel Controller" demo,
where both boards are connected via their USART1 ports. 

### Suggested Demonstration Step

The Accessory board has no `demo_console`, and only uses USART2 for logging.
All interaction/commands sent to the board come from the "Nexus Channel
Controller" demo board (`Nexus_Channel_Controller_F103RB` project).

The tests assume that the accessory board has the following properties;

* Nexus ID = {0xFFFF, 0x0700AAAA} (Authority ID=0xFFFF, Device ID=0x0700AAAA)
* Channel Secret Key = `0xAA 0xBB 0xCC 0xDD 0xEE 0xFF 0x11 0x22 0x33 0x44 0x55 0x66 0x77 0x88 0x99 x00`

For demonstration instructions, see the [Controller Demo Board README](../Nexus_Channel_Controller_F103RB/README.md)

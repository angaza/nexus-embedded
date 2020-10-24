# Nexus Channel Core Embedded (Zephyr) Example

Sample program demonstrating the use of Nexus Channel Core on an
embedded target, using Zephyr RTOS.

This demonstration configures the Nucleo-F103RB as a Nexus Channel
Core device hosting the following resources:

* [Battery resource] (https://angaza.github.io/nexus-channel-models/resource_types/core/101-battery/redoc_wrapper.html)

## Target

STM32 Nucleo-F103RB development board (STM32F103RB target), using Zephyr
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

This serial communication is used to visualize *application layer* communication
between two Nexus Channel Core devices, where the host computer acts as a
'client', reading or updating the state of Nexus Channel Core resources hosted
on the F103RB development board.

## Build/Run

This example is configured as a [PlatformIO](https://platformio.org/install) project.
You can optionally download PlatformIO, download this example project, and
import it using the "Add Existing" functionality in PlatformIO. This will
allow you to modify, build, and download the code onto your own
Nucleo-F103RB development board.

A minimal console is configured so that when the F103RB board is initially
booted up, `---Nexus Channel Core Demonstration---` will be printed to
the terminal (on the virtual COM port). This can be used to confirm that
the board is powered and connected correctly.

Development boards other than the ST Nucleo-F103RB may be supported
(but are not tested or formally supported), and may be used by modifying the
appropriate section of the `platformio.ini` file.

## Suggested Demonstration Steps (GET/POST to battery resource)

This example includes a fully-implemented battery resource compliant to the 
[resource model specification](https://angaza.github.io/nexus-channel-models/resource_types/core/101-battery/redoc_wrapper.html).
Following the steps below will allow you to interact with a PlatformIO-supported
board running Nexus Channel Core and this resource as a server. You will be able
to act as a client and send requests to the server and get the responses. The link
layer has been abstracted away to the serial terminal on the PlatformIO IDE.

1. Install PlatformIO IDE in VSCode
2. Clone this project
3. Open the PlatformIO IDE and select "Open Project"
4. Open the project by selecting the directory named "Channel_Core_PIO_Zephyr_Nucleo-F103RB"
5. Connect your Nucleo-F103RB board to a USB port on your computer
6. Open the PlatformIO perspective in the IDE, and then click "Clean"
7. Wait until the Clean is completed, and then click "Build"
8. Wait until the Build is completed, and then click "Upload and Monitor"
9. Wait until the Upload is complete and observe the Terminal in the IDE. It should prompt for input
10. Interact with the program by typing "get", "post20", or "post35". These will GET the current
battery resource and update the low-state-of-charge threshold to 20% and 35%, respectively.

If you have a different board that is supported on PlatformIO, modify the platformio.ini file to match
your settings. 

The console should appear similar to below:

[!get_post_gif](sample_console_get_post.gif)

## Implementing a Custom Resource

This example can also be used to quickly prototype new resource type
implementations. Simply modify the existing `src/battery_res.c` example code,
replacing it with the resource instance you wish to test.

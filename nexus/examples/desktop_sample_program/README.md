# Sample Program using the Nexus C Decoder

Sample program demonstrating the use of the Nexus embedded libraries.
See "Using in Your Project" in the project's README for more details.

This sample project relies on POSIX functionality that is commonly available
on desktop-class systems (e.g., file access and `malloc`). These facilities
are generally **not** available on embedded platforms; implementers should use
this project as a guide to help them build platform-appropriate solutions.

This project will build with the default GCC toolchain in Ubuntu 18.04, and
presumably any other recent Linux distribution.

Build
```sh
$ make clean all
```

If you have the Clang Static Analyzer (`scan-build`), you can get additional
detailed analysis. You can easily get these dependencies on Ubuntu with the
command shown below.

Install Analysis Dependencies
```sh
make install-dependencies
```

Analyze
```sh
make all BUILD=sa
```

Run
```sh
$ ./sample_nexus_keycode_program
```

## To Reset Device State

When the sample program is started, you will be prompted to specify a file to
store nonvolatile data. If the same file is provided on successive launches,
device state, including PAYG credit timekeeping, keycodes, and Nexus Channel,
will be persisted.

To start "new", simply specify a new file or delete the old one.

## Suggested Demonstration Steps (Full Keycode Protocol)

- Build the project and run it
- Enter a serial number: 12345678
- Enter a secret key: DEADBEEF1020304004030201FEEBDAED
- Select "Enter Keycode" from the demonstration selection menu
- See that there is no credit.
- Enter in this 1 day keycode: *10029054295608#
- See that the credit has increased to one day (86400 seconds)
- Try re-entering the same keycode and see that it is rejected.
- Enter in this 1 day keycode: *47700886956615#
- See that the credit has increased to two days (slightly less than 172800 seconds due to the passage of time).
- Close the program and wait for a few minutes.
- Re-run the program, providing the same NV file.
- Note that the credit is approximately the same as when it was closed.

## Suggested Demonstration Steps (Nexus Channel, Create Link to Accessory)

- Build the project and run it
- Enter a serial number: 12345678
- Enter a secret key: DEADBEEF1020304004030201FEEBDAED
- Select "Enter Keycode" from the demonstration selection menu
- Select "Display Nexus Channel Status" to see that there are no linked devices
- Enter in an 'Origin Command' keycode to link accessory with Nexus ID "0x0020003322": \*8192798211668986#
- See that the system begins to initiate a link
- Select option 3 ("Enter processing loop") to complete the link (will also simulate an accessory response)
- Select "Display Nexus Channel Status" to see that there is 1 active link
- Try re-entering the same keycode and see that it is rejected.
- Close the program.
- Re-run the program, providing the same NV file.
- See that the same origin command is rejected (cannot be reused)

## Suggested Demonstration Steps (Nexus Channel, Delete Link to Accessory)

- Perform steps in above "Create Link to Acccessory" list
- Select "Display Nexus Channel Status" to see that there is 1 active link
- Select "Enter Keycode" from the demonstration selection menu
- Enter in an 'Origin Command' keycode to "Unlink All" accessories from this specific controller: \*81000856304#
    - (Generated with controller 'command count' = 4)
- Observe that all links are deleted (previous link count of 1 goes to 0)
- Select "Display Nexus Channel Status" to see that there are no linked devices
- Try re-entering the same keycode and see that it is rejected.

## Suggested Demonstration Steps for Custom Resource

Nexus Channel provides an easy way to expose 'product-specific' resources
via the same application layer data link. The example program includes an
implementation of the official OCF "Battery" resource (found here:
https://www.oneiota.org/revisions/5666). This resource is an example of how
to host a 'resource' on one Nexus Channel device, and allow other devices to
"GET" the state of that resource (battery charge, capacity, etc), and "POST"
updates to the state of that resource if authorized (change low battery
threshold).

- Select option 4 ("Simulate GET to Battery Resource")
- Observe the raw bytes (a valid CoAP message) received representing this
GET
- Select option 3 ("Enter processing loop")
- Observe the GET response
- Select option 5 ("Update Battery Resource")
- Enter a new value (e.g. 10)
- Observe the raw bytes (valid CoAP message) received representing this POST
- Select option 3 ("Enter processing loop")
- See logs indicating new battery resource is updated
- GET the resource again, see the new threshold is in place (note the timestamp is also updated)

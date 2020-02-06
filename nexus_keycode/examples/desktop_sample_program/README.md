# Sample Program using the Nexus C Decoder

Sample program demonstrating the use of the Nexus C protocol. See "Using in Your
Project" in the project's README for more details.

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

You can modify the behavior of the Nexus keycode protocol as used within this
sample program by modifying the `nexus_keycode/include/keycode_config.h` file.

## Suggested Demonstration Steps (Full Protocol)

- Build the project and run it
- Enter a serial number: 12345678
- Enter a secret key: DEADBEEF1020304004030201FEEBDAED
- See that there is no credit.
- Enter in this 1 day keycode: *100 290 542 956 08#
- See that the credit has increased to one day (86400 seconds)
- Try re-entering the same keycode and see that it is rejected.
- Enter in this 1 day keycode: *477 008 869 566 15#
- See that the credit has increased to two days (slightly less than 172800 seconds due to the passage of time).
- Close the program.
- Re-run the program, providing the same NV file.
- See that the credit is approximately the same as before.

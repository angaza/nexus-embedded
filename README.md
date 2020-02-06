# Nexus Keycode: C Implementation

A decoding-only implementation of the Nexus Keycode protocols, portable to
any platform supporting standard C99. No dynamic memory allocation required.

[![Build status](https://badge.buildkite.com/082d9802561b1880273c1cc570f98c39e00b79ea7dd99425d1.svg?branch=master)](https://buildkite.com/angaza/nexus-embedded-nexus-keycode)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=angaza_nexus-keycode-embedded-internal&metric=alert_status&token=3c0218f9fde1d544fd2060ec1075c15fefeffd4f)](https://sonarcloud.io/dashboard?id=angaza_nexus-keycode-embedded-internal)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=angaza_nexus-keycode-embedded-internal&metric=coverage&token=3c0218f9fde1d544fd2060ec1075c15fefeffd4f)](https://sonarcloud.io/dashboard?id=angaza_nexus-keycode-embedded-internal)

## Using in Your Project

1. Copy the `nexus_keycode` directory into your project
2. Add include paths for `nexus_keycode` and `nexus_keycode/include`
3. Adjust `nexus_keycode/include/keycode_config.h` parameters to suit your product needs
4. Implement the functions specified in `nexus_keycode/include/nexus_keycode_port.h`
5. Use the functions provided by `nexus_keycode/include/nx_keycode.h` and `nexus_keycode/include/nx_common.h`

The functions declared in `include/nexus_keycode_port.h` provide the Nexus
Keycode Protocol with the ability to store and retrieve data from nonvolatile
storage (flash), as well as determine the current system uptime. These are
platform dependent, which is why they must be implemented by your code.

The functions provided by `include/nx_keycode.h` are platform independent
and represent the core Nexus Keycode functionality, including the ability to
accept and process keycodes (key-by-key or all at once) and provide immediate
feedback to those keypresses.

Note that only the files in `nexus_keycode/src`, `nexus_keycode/include`, and
`nexus_keycode/include/common` must be included in your project. Other
folders are used for testing and support purposes.

## Project Structure

The C implementation of Nexus Keycode uses the [ceedling](https://www.throwtheswitch.org/ceedling)
framework to organize automated testing of this source code.

The source code to implement Nexus Keycode Protocol is within the
`nexus_keycode` folder.

**The only folders which must be copied to your own project when using the Nexus
Keycode protocol are `nexus_keycode/include` and `nexus_keycode/src`.**

The folders in this project are:

* `nexus_keycode/include` - Header files that must be included in a project using the
Nexus Keycode protocol
* `nexus_keycode/src` - Implementation of the Nexus Keycode protocol in C (decoder)
* `nexus_keycode/stub` - Stub functions used during static analysis
* `nexus_keycode/build` - temporary output artifacts related to unit tests and static
* `nexus_keycode/test` - Unit tests for the code contained in `src`
* `sample_program` - An example program using the Nexus Protocol
* `buildkite` - Scripts for continuous integration tests (on Buildkite)
* `support` - Scripts related to code formatting and analysis

### Configuration Options

The `include/keycode_config.h` file contains configuration options
to modify the Nexus keycode protocol behavior to match specific
product needs.

The configuration options include:

* Protocol Selection ('full' or 'small' protocols)
* Keycode Input Rate Limiting (Optional)
* Keycode Input Timeout (Optional)

## Static analysis

`ceedling release` will attempt to build the Nexus Keycode library against
a stub implementation (contained in `stub`), with high verbosity GCC warnings
and using the Clang static analyzer. This is used to detect potential problems
in the code that may be missed by unit tests.

## Unit tests
The unit tests themselves are found within the `nexus_keycode/test` folder. The
configuration of `ceedling` is contained within the `nexus_keycode/project.yml` file.

### Installing Ceedling

The system must have a version of Ruby newer than 1.8.6. Then, the following
steps will allow for unit tests to be run.

1. `gem install ceedling`
2. `apt-get install gcovr`
3. `apt-get install gcc-9` (may need to `add-apt-repository ppa:ubuntu-toolchain-r/test` and then `apt update` first)
4. `apt-get install clang-3.9`

### Using Ceedling (Running Tests)

* `ceedling clobber` - destroy all generated test files
* `ceedling test:all` - compile and execute all unit tests
* `ceedling gcov:all` - generate gcov test coverage reports

## Documentation

To regenerate the code documentation locally, execute:

`doxygen ./Doxyfile`

from the repository root directory.

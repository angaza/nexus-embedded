# Nexus Firmware Libraries

This repository contains the embedded implementations of Nexus technology.
These platform-independent libraries are standard, portable C99 requiring
no dynamic memory allocation, suitable for use on highly constrained
embedded platforms.

[![Build status](https://badge.buildkite.com/082d9802561b1880273c1cc570f98c39e00b79ea7dd99425d1.svg?branch=master)](https://buildkite.com/angaza/nexus-embedded-nexus-keycode)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=angaza_nexus-keycode-embedded-internal&metric=alert_status&token=3c0218f9fde1d544fd2060ec1075c15fefeffd4f)](https://sonarcloud.io/dashboard?id=angaza_nexus-keycode-embedded-internal)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=angaza_nexus-keycode-embedded-internal&metric=coverage&token=3c0218f9fde1d544fd2060ec1075c15fefeffd4f)](https://sonarcloud.io/dashboard?id=angaza_nexus-keycode-embedded-internal)

## Using in Your Project

1. Copy the `nexus` directory into your project
2. Add include paths for `nexus` and `nexus/include`
3. Run `cd nexus && python conf_nexus.py` and select configuration options (*Important*: Must run `python conf_nexus.py` from within the `nexus` folder).
4. Implement the functions specified in `nexus/include/nxp_core.h`
5. [Keycode only] Implement the functions specified in `nexus/include/nxp_keycode.h`
5. Use the functions provided by `nexus/include/nx_keycode.h` and `nexus/include/nx_core.h` to interact with Nexus

The functions declared in `include/nxp_core.h` provide the Nexus
Keycode Protocol with the ability to store and retrieve data from nonvolatile
storage (flash), as well as determine the current system uptime. These are
platform dependent, which is why they must be implemented by your code.

The functions provided by `include/nxp_keycode.h` are platform independent
and represent the core Nexus Keycode functionality, including the ability to
accept and process keycodes (key-by-key or all at once) and provide immediate
feedback to those keypresses.

Note that only the files in `nexus/src`, `nexus/include`, and
`nexus/utils` must be included in your project. Other
folders are used for testing and support purposes.

## Project Structure

The C implementation of Nexus Keycode uses the [ceedling](https://www.throwtheswitch.org/ceedling)
framework to organize automated testing of this source code.

The source code to implement Nexus Keycode Protocol is within the
`nexus` folder.

Note that files named `nxp` contain functions that *your code* must implement,
and files named `nx` expose functions and structures that the Nexus system
and modules provide.

**The only folders which must be copied to your own project when using the Nexus
Keycode protocol are `nexus/include`, `nexus/src`, and `nexus/utils`**.

The folders in this project are:

* `nexus/include` - Header files that must be included in a project using the
Nexus Keycode protocol
* `nexus/src` - Nexus module implementation files (do not modify)
* `nexus/utils` - Nexus support utilities and functions
* `nexus/stub` - Stub functions used during static analysis
* `nexus/build` - temporary output artifacts related to unit tests and static
* `nexus/test` - Unit tests for the code contained in `src`
* `nexus/examples` - Examples of the Nexus protocol in use
* `buildkite` - Scripts for continuous integration tests (on Buildkite)
* `support` - Scripts related to code formatting and analysis

### Configuration Options

To adjust configuration options (such as keycode protocol options), run
the configuration tool located at `nexus/conf_nexus.py`.

The tool can be run on any platform using Python 3, as below:

```
python conf_nexus.py
```

You may also need to install the package `python3-tk`.

This tool must be run from within the `nexus` directory to gain access to
the required `Kconfiglib` files.

This tool will launch an interactive configuration menu, where you may
modify the configuration of Nexus features to suit your application.
Afterwards, the tool automatically updates and saves your selections into
a header which is parsed by the Nexus code to determine what features to
expose to your application.

## Static Analysis

`ceedling release` will attempt to build the Nexus Keycode library against
a stub implementation (contained in `stub`), with high verbosity GCC warnings
and using the Clang static analyzer. This is used to detect potential problems
in the code that may be missed by unit tests.

## Unit Tests

The unit tests themselves are found within the `nexus/test` folder. The
configuration of `ceedling` is contained within the `nexus/project.yml` file.

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

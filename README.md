# Nexus Firmware Libraries

This repository contains the embedded implementations of two [Nexus](https://nexus.angaza.com/) technologies:
- [Nexus Keycode](https://nexus.angaza.com/keycode) (an interoperable PAYG token system deployed in millions of devices)
- [Nexus Channel](https://nexus.angaza.com/channel) (an application layer for secure device-to-device communication)
These platform-independent libraries are standard, portable C99 requiring
no dynamic memory allocation, suitable for use on highly constrained
embedded platforms.

[![Build status](https://badge.buildkite.com/082d9802561b1880273c1cc570f98c39e00b79ea7dd99425d1.svg?branch=master)](https://buildkite.com/angaza/nexus-embedded-nexus-keycode)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=angaza_nexus-keycode-embedded-internal&metric=alert_status&token=3c0218f9fde1d544fd2060ec1075c15fefeffd4f)](https://sonarcloud.io/dashboard?id=angaza_nexus-keycode-embedded-internal)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=angaza_nexus-keycode-embedded-internal&metric=coverage&token=3c0218f9fde1d544fd2060ec1075c15fefeffd4f)](https://sonarcloud.io/dashboard?id=angaza_nexus-keycode-embedded-internal)

## BEFORE YOU BEGIN:
1. Make sure you're familiar with Angaza's [Integration Process](https://nexus.angaza.com/mfg_home#integration-overview)
2. Review Angaza's PAYG requirements and tamper mitigation strategies to ensure your product will meet all requirements [PAYG Requirements](https://nexus.angaza.com/mfg_home#payg-requirements)


## PROJECT OVERVIEW
1. Download this reposity
2. Copy the `nexus` directory into your project. See [here](#nexus-directory-structure) for more information about the structure of the directory.
3. Decide which [Configuration Settings](#configuration-options) you want to use for your project.
4. Run the [Config Tool](#configuration-setup)
5. Integrate with your product firmware [Integration Info](#integration-details)
6. Test integration using our [Testing framework](#unit-tests)


## ADDITIONAL INFORMATION

### Configuration Options

This library contains an interactive tool to select which features and configuration options you want to use for your project. The tool will automatically update and saves your configuration into a header. This header is used by Nexus code to determine which features to expose to your product firmware.
- Nexus Keycode: turn on if you want to implement the nexus keycode (likely yes)
- Protocol Type: what digits are available on your product keypad?
    - Full Pad (0-9) OR Small Pad (0-5)
- Nexus Keycode Rate Limiting: feature to prevent brute force attacks
    - Enabled OR Disabled
    - See FAQ at bottom for more info on settings
- "Universal" Factory Test Codes: feature to give access to special tokens that can be used in a factory, manufacturing or test setting.
    - Enabled OR Disabled
    - See FAQ at bottom for more info on settings
- Keycode Entry
    - The number of seconds between user keypresses before nexus resets to accept a new keycode.
- Nexus Channel: turn on if you're looking for device to device communication 

### Configuration Setup

To set up this configuration:
1. Go to the `nexus` directory (required because the tools needs the `Kconfiglib` files are stored here)
2. Run the python tool located at `nexus/conf_nexus.py`. (any platform using python 3)

```
python conf_nexus.py
```
4) You may also need to install the package `python3-tk`.
5) Once the interactive menu opens, use "enter" to toggle each option on and off. Use arrow keys to move between options.
    Note: For Nexus Keycode only, turn off oc-logging and nexus-channel
6) Select options (see below for more info about each option). 
7) Save (shift+S)
8) Now the setup is done! You can proceed with development. 


### Nexus Directory Structure

The C implementation of Nexus uses the [ceedling](https://www.throwtheswitch.org/ceedling)
framework to organize automated testing of this source code.

The directory contains these folders

* `nexus/include` - Header files that must be included in a project using the
Nexus embedded solutions (do not modify)
* `nexus/src` - Nexus module implementation files (do not modify)
* `nexus/oc` - IoTivity-based files for Nexus Channel (do not modify)
* `nexus/utils` - Nexus support utilities and functions (do not modify)
* `nexus/stub` - Stub functions used during static analysis
* `nexus/build` - temporary output artifacts related to unit tests and static
* `nexus/test` - Unit tests for the code contained in `src`
* `nexus/examples` - Examples of the Nexus protocol in use
* `buildkite` - Scripts for continuous integration tests (on Buildkite)
* `support` - Scripts related to code formatting and analysis

You must add include paths in your project to the following subset:
* `nexus`
* `nexus/src`
* `nexus/include`
* `nexus/utils`
* `nexus/oc` (Required only for Nexus Channel or Nexus debug logs)

### Implementation Details

Warning: Do NOT modify any of the src code. 

The `nexus/include` file contains all of the information you need to integrate with nexus. 

Within `nexus/include` there are files that begin with:
- `nxp_` (i.e nxp_keycode.h) - these contain the functions that *your code* must implement. These are functions that nexus src code will call. 
- `nx_` (i.e nx_keycode.h) - these contain the functions available in the Nexus system and modules that *your code* must call and utilize at the appropriate times.  

**Additional Information**
The functions declared in `include/nxp_common.h` provide the Nexus
System with the ability to store and retrieve data from nonvolatile
storage (flash), as well as determine the current system uptime. These are
platform dependent, which is why they must be implemented by your code.
(This is a non-exhaustive list).

The functions declared in `include/nxp_keycode.h` are platform independent
and provide Nexus Keycode with a way to signal keycode feedback (rejected,
accepted, etc), methods to modify remaining PAYG credit based on keycode
receipt, and a way to retrieve the secret key (used for keycode validation)
from the implementing system. (This is a non-exhaustive list).

The functions declared in `include/nxp_channel.h` are platform independent
and provide Nexus Channel with a way to signal channel events (link established,
link handshake begun, etc), send outbound Nexus Channel messages to the network
hardware (dependent on the implementing platform), and retrieve unique keying
information used to validate Nexus Channel link communications.
(This is a non-exhaustive list).


## Static analysis
`ceedling release` will attempt to build a stub implementation of Nexus (
contained in `nexus/stub`) with Channel and Keycode featured enabled. This
build is used as a supplemental static analysis build (static analysis is also
performed against unit test builds).

## Unit tests
The unit tests themselves are found within the `nexus/test` folder. The
configuration of `ceedling` is contained within the `nexus/project.yml` file.

### Installing Tools for Unit Tests

First, install [Conda](https://docs.conda.io/en/latest/), which is used to
manage the packages for building and testing the `nexus-embedded` repository.

Conda ensures that these tools are installed and managed in an independent
environment that does not modify your host/system environment, and ensures
that `nexus-embedded` unit tests and static analysis can be run consistently
on almost any development system.

To install Conda on Linux:

1. `wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh`
2. `bash Miniconda3-latest-Linux-x86_64.sh`
3. Respond 'yes' to defaults during the installation process
4. Close and reopen your terminal or shell after installing
5. Reload your terminal or shell, and run `conda --version` to confirm conda installed successfully.

Next, set up a Conda environment with nexus-embedded specific tools:

6. `conda env create -f support/condaenv.yml` (from same directory as this README)

Now, the prerequisite tools are installed into a conda environment named
`nexusemb`. You can enter this environment (and gain access to the tools
used by the `nexus-embedded` project by typing):

* `conda activate nexusemb`.

7. If the `support/condaenv.yml` file changes (for example, due to new tools
being added in the future) run `conda env update -f=support/condaenv.yml` to
pick up the new changes.

8. Finally, check that GCC is installed with `gcc --version`. To run unit
tests and static analysis (e.g. `ceedling clobber test:all`), GCC-10 is
required. Check that GCC-10 is installed with `gcc-10 --version`.

### Using Ceedling (Running Tests)

After installing the Conda package as described above,
type `conda activate nexusemb`. Now, from the `nexus` folder, type the
following commands:

* `ceedling clobber` - destroy all generated test files
* `ceedling test:all` - compile and execute all unit tests
* `ceedling gcov:all` - generate gcov test coverage reports

## Documentation

To regenerate the code documentation locally, execute:

`doxygen ./Doxyfile`

from the repository root directory.  The documentation will be placed in a
`docs` folder, open `html/index.html` to view it.


## FAQ
**What is rate-limiting and the options available?**
Rate limiting is the ability to prevent and discourage users from trying a bruteforce attack to find an unlock code by randomly entering tokens. This functionality is implemented using a “rate limiting bucket” which tracks how many token attempts are allowed. Once this bucket is empty (0), rate-limiting is active and no keys will be accepted. 

(6) Initial number of tokens in rate limiting bucket - this is the # of tokens that a freshly-programmed device starts with. It is nonzero to allow keycodes to be entered immediately as part of factory testing. 
(128) Maximum number of tokens in rate limiting bucket. The maximum number of keycodes, defined as *<any number of digits># that can be accumulated in the rate-limiting bucket.
(720) Seconds per each token attempt -  this is the # of seconds required to add another keycode to the rate-limiting bucket, up to the maximum. If the device is rate-limited (0 keycodes in the rate-limiting bucket), the user will have to wait this number of seconds before they can enter another token

For example - if your device uses the default values, after ~1 day (720 seconds * 128 max tokens), the rate limiting bucket will have 128 tokens available. An attacker would only be able to enter 128 tokens in a brute force attack. After the 128 tokens are used, the attacker would have to wait 720 seconds before every new token entry. 

**What are the factory codes and options available?**
(5) Number of times a device may accept '10 minute' universal code (NEW)
(5) Number of times a device may accept '1 hour' universal code (NEW)
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
4. Implement the functions specified in `nexus/include/nxp_common.h`
5. [Keycode only] Implement the functions specified in `nexus/include/nxp_keycode.h`
6. [Channel only] Implement the functions specified in `nexus/include/nxp_channel.h`
7. Use the functions provided by `nexus/include/nx_keycode.h`, `nexus/include/nx_channel.h`, and `nexus/include/nx_common.h` to interact with Nexus

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

Please add the following folders to your project include paths:

* `nexus`
* `nexus/src`
* `nexus/include`
* `nexus/utils`
* `nexus/oc` (Required only for Nexus Channel or Nexus debug logs)

Other folders are used for automated testing or support, and are not required
to build a project using Nexus.

## Project Structure

The C implementation of Nexus uses the [ceedling](https://www.throwtheswitch.org/ceedling)
framework to organize automated testing of this source code.

All source code is contained under the `nexus` folder.

Note that files named `nxp` contain functions that *your code* must implement,
and files named `nx` expose functions and structures that the Nexus system
and modules provide.

**The only folders which must be copied to your own project when using the Nexus
Keycode protocol are `nexus/include`, `nexus/src`, and `nexus/utils`**.

The folders in this project are:

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

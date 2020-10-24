## Library Folder (`lib`)

This folder contains code from external which the project relies on, in
this example, this is just the 'nexus' code. Rather than maintaining
two copies of the same nexus reference code in this repository, the `lib`
folder is laid out as follows:

|--lib
|  |
|  |--nexus (actual folder)
|  |  |--include (symlink to folder ../../../../include)
|  |  |--src (symlink to folder ../../../../src)
|  |  |--oc (symlink to ../../../../oc)
|  |  |--utils (symlink to ../../../../utils)

If copying this example without the entire repository, just directly copy the
`include`, `src`, `oc`, and `utils` folders from the `nexus` folder in this
repository to `lib/nexus` in this project.

Due to the way certain files in the `oc` folder do not match their header
name, the `platformio.ini` file in this project also specifies that
folder as one containing src files that must be built via `src_filter`.

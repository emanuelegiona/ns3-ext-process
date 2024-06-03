# Installation guidelines

This module supports both ns-3 build systems (namely _Waf_, used until version 3.36, and _CMake_, from 3.36 onwards), and the following instructions apply to either.

1. Download or clone the contents of this repository

    ```
    git clone https://github.com/emanuelegiona/ns3-ext-process.git
    ```

2. Enter the cloned directory and copy the `ext-process` directory into your ns-3 source tree, under the `src` directory

    ```
    cd ns3-ext-process
    cp -r <path/to/ns3/installation>/src/
    ```

3. Configure & build ns-3

    From ns-3.36 and later versions (CMake)

    ```
    ./ns3 configure
    ./ns3 build
    ```

    Versions prior ns-3.36 (Waf)

    ```
    ./waf configure
    ./waf build
    ```

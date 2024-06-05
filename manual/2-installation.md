# Installation guidelines

## Requirements

This module has been developed on Ubuntu 18.04 LTS and Ubuntu 22.04 LTS with installed ns-3 versions respectively being ns-3.33 and ns-3.40.

System requirements:

- Default ns-3 requirements ([more details][ns3-reqs])

- OS supporting POSIX standard with GNU extensions

    - Dependencies: `pthread_timedjoin_np()` ([more details][pth_np])

- Boost libraries **1.66 or later** ([more details][boost-166-more])

    - Dependencies: ASIO (`io_context`), Boost.System

    - Please ensure that ns-3 is built against the correct version of the library _prior_ to installing `ExternalProcess`

## Installation

This module supports both ns-3 build systems (namely _Waf_, used until version 3.35, and _CMake_, from 3.36 onwards), and the following instructions apply to either.

1. Download or clone the contents of this repository

    ```bash
    git clone https://github.com/emanuelegiona/ns3-ext-process.git
    ```

2. Enter the cloned directory and copy the `ext-process` directory into your ns-3 source tree, under the `src` or `contrib` directory

    ```bash
    cd ns3-ext-process
    cp -r <path/to/ns3/installation>/contrib/
    ```

3. Configure & build ns-3

    From ns-3.36 and later versions (CMake)

    ```bash
    cd <path/to/ns3/installation>
    ./ns3 configure
    ./ns3 build
    ```

    Versions up to ns-3.35 (Waf)

    ```bash
    cd <path/to/ns3/installation>
    ./waf configure
    ./waf build
    ```

## Test your installation

This module includes an ns-3 test suite named `ext-process`, which also doubles as usage example.

The external process is a simple echo TCP _client_ implemented in Python.

**Test requirements**

- Correct path to the external process's launcher script in file `ext-process/test/ext-process-test-suite.cc`:

    ```cpp
    // Path to the launcher script handling external process's execution
    std::string launcherPath = "<path/to/ns3/installation>/contrib/ext-process/launcher-py.sh";
    ```

- Python 3

    Note that this requirement is only necessary due to implementation of `ext-process/echo.py`, _i.e._ the external process used in this example. Processes leveraging binaries of a different nature may not enforce this requirement.

**Launching the test**

The test suite can be executed by prompting the following commands into a terminal:

```bash
cd <path/to/ns3/installation>
./test.py -n -s ext-process
```

A correct installation would present the following output:

```
[0/1] PASS: TestSuite ext-process
1 of 1 tests passed (1 passed, 0 skipped, 0 failed, 0 crashed, 0 valgrind errors)
```

## Compatibility across OS and ns-3 versions

`ExternalProcess` is tested in development environments as well as within Docker images.

Current version has successfully passed tests in the following environments:

- Ubuntu 18.04, ns-3.35 (Docker)

    - Note: `libboost` is available in version 1.65.1; for testing purposes only, it has been upgraded to version 1.74 via unofficial repository `ppa:mhier/libboost-latest` ([more details][ppa-boost])

- Ubuntu 20.04, ns-3.40 (Docker)

- Ubuntu 22.04, ns-3.41 (Docker)

More details on Docker images used at [this page][docker-imgs].



[ns3-reqs]: https://www.nsnam.org/docs/release/3.42/tutorial/html/getting-started.html#prerequisites
[pth_np]: https://man7.org/linux/man-pages/man3/pthread_tryjoin_np.3.html
[boost-166-more]: https://www.boost.org/users/history/version_1_66_0.html
[ppa-boost]: https://launchpad.net/~mhier/+archive/ubuntu/libboost-latest
[docker-imgs]: https://github.com/emanuelegiona/ns3-base-docker

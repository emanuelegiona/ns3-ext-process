# Changelog

## Release v2.2.0

[Link to release][v220]

No new features implemented; bugfix for high-load external processes and other minor bugfixes.

**API changes**

None, internal implementation only.

**Features**

- Bugfix implemented for external processes providing a high data load into ns-3; in these cases, function [`read_until` from Boost::ASIO][boost_read_until] has been observed entering the edge case where excess data was found past the chosen delimiter (Thanks [@korovev][ghuser_korovev])

- A _high-load_ external process is broadly described as one where:

    1. predominant operations are `Write`s,

    2. messages are rapidly sent to ns-3 (> 200 msg/s), and

    3. each message is of considerable size (> 300 B)

    These figures do not mean to represent an exhaustive list of characteristics to reliably determine whether you need to upgrade. However, should you encounter enough cases of failed `Read`s in presence of any of the above aspects, you are advised to upgrade to this version.

- Minor improvements to function threads implementing cancelable `accept`, `connect`, `write`, and `read_until` operations, in particular regarding the thread outcome status

- Minor improvements to logging messages

## Release v2.1.0

[Link to release][v210]

Features implementation: full-remote processes and explicit TCP roles.

Launcher script is no longer mandatory, as a full-remote external process is expected on the given an IP address and port number. 
Moreover, `ExternalProcess` can now take the _client_ role within the TCP communication, with the other peer acting as _server_.

**API changes**

Retro-compatible changes w.r.t. v2.0.0 API.

- Attribute `Launcher` now accepts empty-string values (_i.e._ `""` indicates a full-remote external process); functionality unchanged otherwise

- Attribute `CliArgs` are ignored upon using a full-remote external process; functionality unchanged otherwise

- Attribute `Port` must be specified (_i.e._ `!= 0`) upon using a full-remote external process; functionality unchanged otherwise

- Attributes `CrashOnFailure`, `WatchdogPeriod`, and `GracePeriod` are ignored upon using a full-remote external process, as there is no PID to periodically check upon via the watchdog thread; functionality unchanged otherwise

**Features**

- Full-remote external processes support: an `ExternalProcess` instance can communicate with a process just via IP address and port number, without the need of a local launcher script

- `ExternalProcess` instances can be created with the TCP role being _client_, instead of only _server_ (which becomes the default behavior); a public `enum ExternalProcess::TcpRole` is introduced for this purpose

- When using `ExternalProcess` in TCP client role, attribute `TimedAccept` refers to socket operation `connect()`, thus applying a timeout for this operation in the same way as it would have on `accept()`

- Fixes: multiple attempts on socket operations are safely enforced even in presence of Boost::ASIO exceptions; occasional hangs on `pthread_timedjoin_np` resolved by combining `nanosleep` with `pthread_tryjoin_np`

- New ns-3 attribute available:

    - `TcpRole`: TCP role implemented by this instance.

        Default value: `UintegerValue((uint8_t)TcpRole::SERVER)`

    - `Address`: IP address for communicating with external process; this is mandatory for ns-3 in CLIENT role and full-remote process in SERVER role.

        Default value: `StringValue("")`

## Release v2.0.0

[Link to release][v200]

Major feature implementation, rewriting all IPC to TCP sockets instead of named pipes.

Most changes are transparent to users w.r.t. ns-3 API, with simulations and programs remaining mostly unchanged.
However, programs to be used as external processes may require some modifications, as described below.

**API changes**

Breaking changes to API:

- Attribute `ProcessLauncher` **renamed** to `Launcher`; functionality unchanged

- Attribute `ProcessExtraArgs` **renamed** to `CliArgs`; functionality unchanged, improved implementation

- Attribute `ReadHangsTimeout` **removed**; feature protecting from empty-reads has been removed as no longer needed

Breaking changes to compatible programs:

- CLI arguments: **at least 1** instead of at least 2

    - First positional argument is **mandatory** and represents the TCP port for connections of a _client_ socket

- Remaining arguments are passed **individually** instead of a single string (with value of previous attribute `ProcessExtraArgs`)

    - Value of `CliArgs` is split using the whitespace as delimiter, with each token passed individually to the external process's launcher

- TCP socket should stay open throughout the lifetime of the external process

- Changes to IPC protocol macros

    - `MSG_READY`, `MSG_WRITE_BEGIN`, `MSG_WRITE_END`, `MSG_READ_BEGIN`, `MSG_READ_END` removed

    - Line-based communication, with messages terminating in `MSG_EOL` (currently: `\n`)

    - `MSG_DELIM` introduced to split tokens from each line received via socket read

In the public ns-3 API, functions `Create()`, `Write()`, and `Read()` were changed in their implementation but not in their functionality.

**Features**

- Removing reliance on named pipes solves occasional deadlocks and/or empty-reads; as a result, simulations are no longer hanging on operations from `ExternalProcess`

- Inter-process communication implementation moved to TCP sockets

- Communication with external processes supports arbitrary timeouts and repeated attempts

- New ns-3 attributes available:

    - `WatchdogPeriod`: Time period spent sleeping by the watchdog thread at the beginning of the PID checking loop; lower values will allow detection of process errors quicker, longer values greatly reduce busy waits.

        Default value: `TimeValue(MilliSeconds(100))`

    - `GracePeriod`: Time period spent sleeping after killing a process, potentially allowing any temporary data on the process to be stored.

        Default value: `TimeValue(MilliSeconds(100))`

    - `Port`: Port number for communicating with external process; if 0, a free port will be automatically selected by the OS.

        Default value: `UintegerValue(0)`

    - `Timeout`: Maximum waiting time for socket operations (e.g. accept); if 0, no timeout is implemented.

        Default value: `TimeValue(MilliSeconds(0))`

    - `Attempts`: Maximum attempts for socket operations (e.g. accept); only if a non-zero timeout is specified.

        Default value: `UintegerValue(1)`

    - `TimedAccept`, `TimedWrite`, `TimedRead`: Flag indicating whether to apply a timeout on socket `accept()` / `write()` / `read_until()`, implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.

        Default value: `BooleanValue(false)`

- Introduced range of accepted values for attributes `ThrottleWrites` and `ThrottleReads`

- Automated testing via GitHub Actions and Docker images

## Release v1.0.3

[Link to release][v103]

No changes to v1.x.x API.

Documentation released.

## Release v1.0.2

[Link to release][v102]

Minor feature implementation.

**Features**

- Optional protection from empty-reads may be enabled.

- This feature allows early termination of a simulation whenever ns-3 receives consecutive empty-string results from `Read`.

- New ns-3 attribute available:

    - `ReadHangsTimeout`: Timeout for preventing a simulation from hanging on empty reads; only applied for consecutive reads only.

        Default value: `TimeValue(MilliSeconds(0))`.

- Graceful exit feature implemented: allows requesting the termination of all alive external processes launched by any instance.

## Release v1.0.1

[Link to release][v101]

Minor feature implementation.

**Features**

- Optional throttling may be enabled on `Write` and/or `Read` operations.

- This feature allows pacing the ns-3 process to potential slower speed of an external process.

- New ns-3 attributes available:

    - `ThrottleWrites`: Minimum time between a read and a subsequent write; this delay is applied before writing.

        Default value: `TimeValue(MilliSeconds(0))`.

    - `ThrottleReads`: Minimum time between a write and a subsequent read; this delay is applied before reading.

        Default value: `TimeValue(MilliSeconds(0))`.

## Release v1.0.0

[Link to release][v100]

Initial release.

**Features**

- Communication with external process based on named pipes.

- Watchdog thread for polling process status based on PID.



<!-- Releases -->
[v220]: https://github.com/emanuelegiona/ns3-ext-process/releases/tag/v2.2.0
[boost_read_until]: https://live.boost.org/doc/libs/release/doc/html/boost_asio/reference/read_until/overload13.html
[ghuser_korovev]: https://github.com/korovev

[v210]: https://github.com/emanuelegiona/ns3-ext-process/releases/tag/v2.1.0
[v200]: https://github.com/emanuelegiona/ns3-ext-process/releases/tag/v2.0.0
[v103]: https://github.com/emanuelegiona/ns3-ext-process/releases/tag/v1.0.3
[v102]: https://github.com/emanuelegiona/ns3-ext-process/releases/tag/v1.0.2
[v101]: https://github.com/emanuelegiona/ns3-ext-process/releases/tag/v1.0.1
[v100]: https://github.com/emanuelegiona/ns3-ext-process/releases/tag/v1.0.0

# Usage

## Handbook

This section will briefly present how to use `ExternalProcess` for your ns-3 simulation scripts or modules.

1. Creation of an `ExternalProcess` instance

    ```cpp
    Ptr<ExternalProcess> myExtProc = CreateObjectWithAttributes<ExternalProcess>(
    "Launcher", StringValue("<path/to/executable>"),                  // Mandatory (preferably an absolute path)
    "CliArgs", StringValue("--attempts 10 --debug True"),   // Optional: CLI arguments for launcher script (depend on the executable)
    "Port", UintegerValue(0),                               // Optional: default value (0) lets the OS pick a free port automatically
    "Timeout", TimeValue(MilliSeconds(150)),                // Optional: enables timeout on socket operations (e.g. accept, write, read)
    "Attempts", UintegerValue(10),                          // Optional: enables multiple attempts for socket operations (only if timeout is non-zero)
    "TimedAccept", BooleanValue(true),                      // Optional: enables timeout on socket accept operations (see above, 'Attempts')
    "TimedWrite", BooleanValue(true),                       // Optional: enables timeout on socket write operations (see above, 'Attempts')
    "TimedRead", BooleanValue(true)                         // Optional: enables timeout on socket read operations (see above, 'Attempts')
    );
    ```

    Explanation:

    - ns-3 attribute `Launcher` is **mandatory** at the time of invoking `ExternalProcess::Create(void)`: it should contain the path to an existing executable file (_e.g._ bash script or similar).

    - ns-3 attribute `CliArgs` is _optional_ and it represents a single string containing additional CLI arguments to the external process (_default:_ empty string).

        > The first argument passed to every executable is the TCP port used for communication (see below); the string hereby provided should contain arguments and their respective values considering that whitespace is used as a delimiter when splitting to tokens.

        > _e.g._ the string `"--attempts 10 --debug True"` results in 4 additional tokens passed to the executable: `--attempts`, `10`, `--debug`, and `True`

    - ns-3 attribute `Port` is _optional_ and it represents the port to use to accept communications via a TCP socket (_default:_ 0, _i.e._ allows the OS to pick a free port).

        > This value is automatically passed as the **first parameter** to your executable. The external process is thus expected to set up a TCP client and connect to IP address `127.0.0.1:<Port>` or `localhost:<port>`.

    - ns-3 attributes `TimedAccept`, `TimedWrite`, and `TimedRead` change the default behavior (_i.e._ blocking socket operations, also indefinitely) into a using a timeout and repeated attempts; respectively, they refer to socket's `accept()` (used in `ExternalProcess::Create()`), `write()`, and `read()`.

    - ns-3 attributes `Timeout` and `Attempts` allow customization of socket operations using timeout and repeated attempts.

        > Note: the same settings are used throughout socket operations; in necessity of specifying different settings for each operation, users may change these values using `Object::SetAttribute()`, which is allowed during ongoing simulations too.

2. Execution of the external process

    ```cpp
    bool ExternalProcess::Create(void);
    ```

    The return value should be checked for error handling in unsuccessful executions.

3. Communication _towards_ the external process

    ```cpp
    bool ExternalProcess::Write(
    const std::string &str,
    bool first = true,
    bool flush = true,
    bool last = true
    );
    ```

    This function sends `str` to the external process, with remaining arguments enabling some degree of optimization (_e.g._ in a series of `Write`s, only flushing at the last one).

4. Communication _from_ the external process

    ```cpp
    bool ExternalProcess::Read(std::string &str, bool &hasNext);
    ```

    This function attempts to read `str` from the external process: `str` should be ignored if the return value of this `Read` equals `false`.
    If multiple `Read`s are expected, the `hasNext` value indicates whether to continue reading or not, proving useful to its usage as exit condition in loops.

5. Termination of an external process

    Deletion of an `ExternalProcess` instance automatically takes of this task, but it is possible to explicitly perform it at any point of the simulation.

    ```cpp
    bool ExternalProcess::Teardown(void);
    bool ExternalProcess::Teardown(pid_t childPid);     // Discouraged
    ```

    The `childPid` value may be obtained from the same `ExternalProcess` instance by invoking the `ExternalProcess::GetPid(void)` function, as implemented by function `ExternalProcess::Teardown(void)`.

    > Note: while `ExternalProcess::Teardown(void)` is thread-safe with the rest of `ExternalProcess` mechanisms, `ExternalProcess::Teardown(pid_t childPid)` is not; users are advised to terminate external processes only through the first function, invoked on the appropriate `ExternalProcess` object. More details are provided below.

6. Termination of all external processes (_e.g._ simulation fatal errors)

    In order to prevent external processes from living on in cases of `NS_FATAL_ERROR` being invoked by the simulation, it is possible to explicitly kill all processes via a static function.

    ```cpp
    static void ExternalProcess::GracefulExit(void);
    ```

    Being a static function, there is no need to retrieve any instance of `ExternalProcess` for this instruction.

## Programs compatible with `ExternalProcess`

In order to properly execute external processes via this module, the following considerations should be taken:

- At least 1 CLI argument must be supported:

    1. TCP port for socket connection (**required**)

    2. Any number of additional arguments; see discussion above for attribute `CliArgs` (_optional_)

- Socket connectivity as a client (`ExternalProcess` acts as server)

    - The TCP socket should be opened at the beginning of execution and kept alive throughout the process lifetime

- Properly handle the following messaging prefixes:

    ```cpp
    // Macros for process messaging
    #define MSG_KILL "PROCESS_KILL"
    #define MSG_DELIM "<ENDSTR>"
    #define MSG_EOL '\n'
    ```

    In particular, `MSG_KILL` is sent by the ns-3 simulation towards the external process via `Write`s and indicates a gentle request to terminate execution.
    Ignoring this message results in abrupt termination using a `kill()` syscall.
    `MSG_DELIM` and `MSG_EOL` are used for implementing a line-based socket communication s.t.:

    - `MSG_DELIM` represents a delimiter between multiple tokens within the same line;

    - `MSG_EOL` represents the end-of-line delimiter, used any time a line is intended to be flushed through the socket.

## API Documentation

### Macros

**General utility**

```cpp
#define CURRENT_TIME Now().As(Time::S)
```

Retrieves the current simulation time; typically used for logging.

<hr/>

**Interprocess communication (IPC)**

```cpp
#define MSG_KILL "PROCESS_KILL"
```

Represents a gentle request to terminate process execution.
If properly handled by the external process, it may be useful to save any temporary data before exiting.

<hr/>

```cpp
#define MSG_DELIM "<ENDSTR>"
```

Represents a delimiter between multiple tokens contained within the same line sent through the socket.
This delimiter is added between any pair of strings resulting from invoking `ExternalProcess::Write()` consecutively twice.

_Example_

```cpp
Ptr<ExternalProcess> ep = [...];
ep->Create();
[...]
ep->Write("str1", true, false, false);
ep->Write("str2", false, false, true);
[...]
```

The effective string sent through the socket will be exactly 1, with value: `str1<ENDSTR>str2\n`.

<hr/>

```cpp
#define MSG_EOL '\n'
```

Represents a end-of-line delimiter, used any time a line is intended to be flushed through the socket.

_Example_

```cpp
Ptr<ExternalProcess> ep = [...];
ep->Create();
[...]
ep->Write("str1", true, false, false);
ep->Write("str2", false, true, false);
ep->Write("strX", false, false, false);
ep->Write("strY", false, false, true);
[...]
```

The effective strings sent through the socket will be exactly 2, with values:

- `str1<ENDSTR>str2\n`, and

- `strX<ENDSTR>strY\n`.

### Global functions

```cpp
void* WatchdogFunction(void* arg);
```

Function to use in the watchdog thread (POSIX). 
Its usage is typically transparent to the user.

<hr/>

```cpp
void* AcceptorFunction(void* arg);
```

Function to use in acceptor threads (POSIX). 
Its usage is typically transparent to the user.

<hr/>

```cpp
void* WriterFunction(void* arg);
```

Function to use in writer threads (POSIX). 
Its usage is typically transparent to the user.

<hr/>

```cpp
void* ReaderFunction(void* arg);
```

Function to use in reader threads (POSIX). 
Its usage is typically transparent to the user.

### Global variables

```cpp
static WatchdogSupport g_watchdogArgs;
```

Represents the global variable holding arguments for the watchdog thread.

<hr/>

```cpp
static pthread_t g_watchdog;
```

Watchdog thread for checking running instances.

<hr/>

```cpp
static bool g_watchdogExit = false;
```

Flag indicating the exit condition for the watchdog thread.

<hr/>

```cpp
static std::map<pid_t, ExternalProcess*> g_runnerMap;
```

Map associating PID to instances that spawned them.

<hr/>

```cpp
static pthread_mutex_t g_watchdogExitMutex = PTHREAD_MUTEX_INITIALIZER;
```

Mutex for exit condition for the watchdog thread.

<hr/>

```cpp
static pthread_mutex_t g_watchdogMapMutex = PTHREAD_MUTEX_INITIALIZER;
```

Mutex for runner map for the watchdog thread.

<hr/>

```cpp
static pthread_mutex_t g_watchdogTeardownMutex = PTHREAD_MUTEX_INITIALIZER;
```

Mutex for accessing ExternalProcess::Teardown() from multiple threads.

<hr/>

```cpp
static pthread_mutex_t g_gracefulExitMutex = PTHREAD_MUTEX_INITIALIZER;
```

Mutex for accessing ExternalProcess::GracefulExit() from multiple threads.

### Struct: WatchdogSupport

```cpp
struct WatchdogSupport {
  bool m_initialized = false;                         //!< Flag indicating whether the support variabled is initialized.
  ExternalProcess::WatchdogData* m_args = nullptr;    //!< Pointer to the watchdog arguments to use.
};
```

### Class: ExternalProcess

```cpp
class ExternalProcess: public Object {};
```

Class for handling an external side process interacting 
with ns-3. 

This class creates a new process upon initialization and sets up 
communication channels via a TCP socket. 
Employing a leader/follower classification of roles, ns-3 acts as 
a leader, with the external process taking the role of the follower. 
As such, objects of this class shall set up the TCP server, with the 
client necessarily implemented by the external process. 

A watchdog thread is spawned upon the first instance of this class 
being created, running until the last ExternalProcess object goes out 
of scope. 
This thread periodically checks whether external processes are still 
alive by means of their PID. 
Watchdog settings may be customized via attributes 'CrashOnFailure' and 
'WatchdogPeriod'.  
By default, socket operations are blocking, possibly for an indefinite time. 
Attributes 'TimedAccept', 'TimedWrite', and 'TimedRead' change the behavior of respective socket operations -- i.e. accept(), write(), and read() -- into using a timeout and repeated attempts. 
Attributes 'Timeout' and 'Attempts' allow customization of such behavior, but are applied to any enabled timed operation equally.

> All socket operations are blocking, even in their timed versions. 
Asynchronous mode using callbacks is out of the scope of the current implementation.

#### Attributes

`ExternalProcess` supports the following attributes within ns-3 `Object`'s attribute system.

```cpp
"Launcher"
```

Absolute path to the process launcher script.

- Default value: `StringValue("")`

- **Mandatory**

- Must be a non-empty string representing a valid path within the filesystem

<hr/>

```cpp
"CliArgs"
```

String containing command-line arguments for the launcher script; tokens will be split by whitespace first.

- Default value: `StringValue("")`

- _Optional_

- Command-line arguments specified here are passed _after_ the TCP port number the external process has to use for communicating with ns-3

<hr/>

```cpp
"CrashOnFailure"
```

Flag indicating whether to raise a fatal exeception if the external process fails.

- Default value: `BooleanValue(true)`

- _Optional_

<hr/>

```cpp
"WatchdogPeriod"
```

Time period spent sleeping by the watchdog thread at the beginning of the PID checking loop; lower values will allow detection of process errors quicker, longer values greatly reduce busy waits.

- Default value: `TimeValue(MilliSeconds(100))`

- _Optional_

- Range: min = `MilliSeconds(1)`, max = `Minutes(60)`

- The first instance of `ExternalProcess` invoking `Create()` will set the watchdog thread with this value; any subsequent `ExternalProcess` instance will not affect the watchdog polling period while there is a live watchdog thread

<hr/>

```cpp
"GracePeriod"
```

Time period spent sleeping after killing a process, potentially allowing any temporary data on the process to be stored.

- Default value: `TimeValue(MilliSeconds(100))`

- _Optional_

- Range: min = `MilliSeconds(0)`

<hr/>

```cpp
"Port"
```

Port number for communicating with external process; if 0, a free port will be automatically selected by the OS.

- Default value: `UintegerValue(0)`

- _Optional_

<hr/>

```cpp
"Timeout"
```

Maximum waiting time for socket operations (e.g. accept); if 0, no timeout is implemented.

- Default value: `TimeValue(MilliSeconds(0))`

- _Optional_

- Range: min = `MilliSeconds(0)`

<hr/>

```cpp
"Attempts"
```

Maximum attempts for socket operations (e.g. accept); only if a non-zero timeout is specified.

- Default value: `UintegerValue(1)`

- _Optional_

- Range: min = 1

<hr/>

```cpp
"TimedAccept"
```

Flag indicating whether to apply a timeout on socket accept(), implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.

- Default value: `BooleanValue(false)`

- _Optional_

<hr/>

```cpp
"TimedWrite"
```

Flag indicating whether to apply a timeout on socket write(), implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.

- Default value: `BooleanValue(false)`

- _Optional_

<hr/>

```cpp
"TimedRead"
```

Flag indicating whether to apply a timeout on socket read_until(), implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.

- Default value: `BooleanValue(false)`

- _Optional_

<hr/>

```cpp
"ThrottleWrites"
```

Minimum time between a read and a subsequent write; this delay is applied before writing.

- Default value: `TimeValue(MilliSeconds(0))`

- _Optional_

- Range: min = `MilliSeconds(0)`

<hr/>

```cpp
"ThrottleReads"
```

Minimum time between a write and a subsequent read; this delay is applied before reading.

- Default value: `TimeValue(MilliSeconds(0))`

- _Optional_

- Range: min = `MilliSeconds(0)`

#### Public API

```cpp
struct WatchdogData {
  bool m_crashOnFailure;    //!< [in] Flag indicating whether to raise a fatal exeception if the external process fails.
  Time m_period;            //!< [in] Time period spent sleeping by the watchdog thread at the beginning of the PID checking loop.
};
```

Represents the argument for a watchdog thread. 
Its usage is typically transparent to the user.

<hr/>

```cpp
struct BlockingArgs {
  pthread_t* m_threadId = nullptr;      //!< [inout] Pointer to the blocking thread ID to set to -1.
  bool* m_exitNormal = nullptr;         //!< [inout] Pointer to flag indicating normal exit from thread.
  pthread_mutex_t* m_mutex = nullptr;   //!< [in] Pointer to the mutex to use.
  pthread_cond_t* m_cond = nullptr;

  /** [Constructor] */
};
```

Represents additional arguments for implementing BlockingSocketOperation() in thread functions. 
Its usage is typically transparent to the user.

<hr/>

```cpp
struct AcceptorData {
  boost::asio::ip::tcp::acceptor* m_acceptor = nullptr;   //!< [in] Pointer to boost::asio acceptor.
  boost::asio::ip::tcp::socket* m_sock = nullptr;         //!< [in] Pointer to boost::asio socket.
  boost::system::error_code* m_errc = nullptr;            //!< [out] Pointer to boost::system error code.
  BlockingArgs* m_blockingArgs = nullptr;                 //!< [in] Pointer to additional args for blocking operations, if provided; if nullptr, timed operation is assumed.

  /** [Constructor] */
};
```

Represents the argument for an acceptor thread, if 'TimedAccept' is used. 
Its usage is typically transparent to the user.

<hr/>

```cpp
struct WriterData {
  boost::asio::ip::tcp::socket* m_sock = nullptr;   //!< [in] Pointer to boost::asio socket.
  boost::asio::mutable_buffer* m_buf = nullptr;     //!< [in] Pointer to boost::asio::streambuf to write data from.
  boost::system::error_code* m_errc = nullptr;      //!< [out] Pointer to boost::system error code.
  BlockingArgs* m_blockingArgs = nullptr;           //!< [in] Pointer to additional args for blocking operations, if provided; if nullptr, timed operation is assumed.

  /** [Constructor] */
};
```

Represents the argument for a writer thread, if 'TimedWrite' is used. 
Its usage is typically transparent to the user.

<hr/>

```cpp
struct ReaderData {
  boost::asio::ip::tcp::socket* m_sock = nullptr;   //!< [in] Pointer to boost::asio socket.
  boost::asio::streambuf* m_buf = nullptr;          //!< [out] Pointer to boost::asio::streambuf to read data to.
  boost::system::error_code* m_errc = nullptr;      //!< [out] Pointer to boost::system error code.
  BlockingArgs* m_blockingArgs = nullptr;           //!< [in] Pointer to additional args for blocking operations, if provided; if nullptr, timed operation is assumed.

  /** [Constructor] */
};
```

Represents the argument for a reader thread, if 'TimedRead' is used. 
Its usage is typically transparent to the user.

<hr/>

```cpp
static void ExternalProcess::GracefulExit(void);
```

Terminates all external processes spawned during this simulation.

> This function should be invoked whenever `NS_FATAL_ERROR` is used, preventing external processes to remain alive despite no chance of further communication.

> This function is thread-safe.

<hr/>

```cpp
ExternalProcess::ExternalProcess();
```

Default constructor.

<hr/>

```cpp
virtual ExternalProcess::~ExternalProcess();
```

Default destructor.

<hr/>

```cpp
static TypeId ExternalProcess::GetTypeId(void);
```

Registers this type.

Returns:

- The TypeId.

<hr/>

```cpp
bool ExternalProcess::Create(void);
```

Creates a side process given a launcher script, accepting connections from it.

Returns:

- True if the creation has been successful, False otherwise.

> This operation may be blocking.

<hr/>

```cpp
bool ExternalProcess::IsRunning(void) const;
```

Retrieves whether the side process is running or not.

Returns:

- True if the side process is running, False otherwise.

<hr/>

```cpp
pid_t ExternalProcess::GetPid(void) const;
```

Retrieves the PID of the side process.

Returns:

- The PID of the side process previously set up via `ExternalProcess::Create()`.

<hr/>

```cpp
bool ExternalProcess::Teardown(void);
```

Performs process teardown operations using the result of `ExternalProcess::GetPid()`.

Returns:

- True if this external process is no longer tracked by the watchdog, False otherwise.

> This function is thread-safe.

<hr/>

```cpp
bool ExternalProcess::Teardown(pid_t childPid);
```

Performs process teardown operations.

Parameters:

- `[in] childPid` PID of the child process associated with this teardown procedure. 
If different than `-1`, it will send a `SIGKILL` signal to the provided PID.

Returns:

- True if this external process is no longer tracked by the watchdog, False otherwise.

> This function is **NOT thread-safe**: a lock shall be acquired on `g_watchdogTeardownMutex` previously to the invocation of this function.

> Its usage is discouraged; users should invoke `ExternalProcess::Teardown(void)` instead, on the instance associated to the process intended to be shutdown.

<hr/>

```cpp
bool ExternalProcess::Write(const std::string &str, bool first = true, bool flush = true, bool last = true);
```

Writes a string to the external process through the socket.

Parameters:

- `[in] str` String to write.

- `[in] first` Whether the string is the first of a series of writes 
(Default: true).
- `[in] flush` Whether to flush after writing this string or not (Default: 
true).
- `[in] last` Whether the string is the last of a series of writes (Default: true).

Returns:

- True if the operation is successful, False otherwise.

> This operation may be blocking.

<hr/>

```cpp
bool ExternalProcess::Read(std::string &str, bool &hasNext);
```

Reads a string from the external process through the socket.

Parameters:

- `[out] str` String read (if return is True; discard otherwise).

- `[out] hasNext` Whether there is going to be a next line or not.

Returns:

- True if the operation is successful, False otherwise.

> This operation may be blocking.

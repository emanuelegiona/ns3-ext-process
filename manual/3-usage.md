# Usage

## Handbook

This section will briefly present how to use `ExternalProcess` for your ns-3 simulation scripts or modules.

1. Creation of an `ExternalProcess` instance

    ```
    Ptr<ExternalProcess> myExtProc = CreateObjectWithAttributes<ExternalProcess>(
        "ProcessLauncher", StringValue("<path/to/executable>"),
        "ProcessExtraArgs", StringValue("<optional CLI arguments to the executable>"),
        "CrashOnFailure", BooleanValue(true),
        "ThrottleWrites", MilliSeconds(0),
        "ThrottleReads", MilliSeconds(0)
    );
    ```

    - ns-3 attribute `ProcessLauncher` is **mandatory** at the time of invoking `ExternalProcess::Create(void)`: it should contain the path to an existing executable file (_e.g._ bash script or similar).

    - ns-3 attribute `ProcessExtraArgs` is _optional_ and it represents a single string containing additional CLI arguments to the external process (_default:_ empty string -- not even passed to the executable).

    - ns-3 attribute `CrashOnFailure` is _optional_ and it specifies whether to raise a fatal exception upon failure detection of the external process (_default:_ `true`).

    - ns-3 attribute `ThrottleWrites` is _optional_ and it specifies whether and, eventually, the amount of time to wait between a `Read()` and a subsequent `Write()` (_default:_ 0 ms -- no throttling).

    - ns-3 attribute `ThrottleReads` is _optional_ and it specifies whether and, eventually, the amount of time to wait between a `Write()` and a subsequent `Read()` (_default:_ 0 ms -- no throttling).

        > Throttling may be useful whenever the external process in not able to stay on par with ns-3's speed in reading/writing from/to named pipes.

    - ns-3 attribute `ReadHangsTimeout` is _optional_ and it specifies whether and, eventually, the amount of time to consider a simulation hanged on an empty-read loop (_default:_ 0 ms -- no timeout).

2. Execution of the external process

    ```
    bool ExternalProcess::Create(void);
    ```

    The return value should be checked for error handling in unsuccessful executions.

3. Communication _towards_ the external process

    ```
    bool ExternalProcess::Write(const std::string &str, bool first = true, bool flush = true, bool last = true);
    ```

    This function sends `str` to the external process, with remaining arguments enabling some degree of optimization (_e.g._ in a series of `Write`s, only flushing at the last one).

4. Communication _from_ the external process

    ```
    bool ExternalProcess::Read(std::string &str, bool &hasNext);
    ```

    This function attempts to read `str` from the external process: `str` should be ignored if the return value of this `Read` equals `false`.
    If multiple `Read`s are expected, the `hasNext` value indicates whether to continue reading or not, proving useful to its usage as exit condition in loops.

5. Termination of an external process

    Deletion of an `ExternalProcess` instance automatically takes of this task, but it is possible to explicitly perform it at any point of the simulation.

    ```
    void ExternalProcess::Teardown(pid_t childPid);
    ```

    The `childPid` value may be obtained from the same `ExternalProcess` instance by invoking the `ExternalProcess::GetPid(void)` function.

6. Termination of all external processes (_e.g._ simulation fatal errors)

    In order to prevent external processes from living on in cases of `NS_FATAL_ERROR` being invoked by the simulation, it is possible to explicitly kill all processes via a static function.

    ```
    static void ExternalProcess::GracefulExit(void);
    ```

    Being a static function, there is no need to retrieve any instance of `ExternalProcess` for this instruction.

## Programs compatible with `ExternalProcess`

In order to properly execute external processes via this module, the following considerations should be taken:

- At least 2 CLI arguments must be supported:
  
    1. Input (_i.e._ ns3-to-proc) named pipe (**required**)

    2. Output (_i.e._ proc-to-ns3) named pipe (**required**)
    
    3. Additional arguments (single string consisting of `ProcessExtraArgs` attribute value, _optional_)

- Leverage named pipes blocking operations

    - Open and close named pipes at need

    - In this way, the external process will be waiting for commands and/or data from the ns-3 simulation without requiring any synchronization mechanisms

- Properly handle the following messaging prefixes:

    ```
    // Macros for process messaging
    #define MSG_KILL "PROCESS_KILL"
    #define MSG_READY "PROCESS_READY"
    #define MSG_WRITE_BEGIN "NS3_WRITE_BEGIN"
    #define MSG_WRITE_END "NS3_WRITE_END"
    #define MSG_READ_BEGIN "NS3_READ_BEGIN"
    #define MSG_READ_END "NS3_READ_END"
    ```

    In particular, `MSG_KILL`, `MSG_WRITE_BEGIN`, and `MSG_WRITE_END` and sent by the ns-3 simulation towards the external process via `Write`s.
    `MSG_READY`, `MSG_READ_BEGIN`, and `MSG_READ_END` should be sent by the external process towards the ns-3 simulation in the following cases:

    - `MSG_READY`: as soon as the program is initialized and ready to receive ns-3 commands and data
    
    - `MSG_READ_BEGIN` and `MSG_READ_END`: the program should enclose any of its output within these two message prefixes for a correct interpretation by the ns-3 simulation

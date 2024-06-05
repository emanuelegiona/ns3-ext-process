/*
 * Copyright (c) 2023 Sapienza University of Rome
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Emanuele Giona <giona@di.uniroma1.it> <ORCID: 0000-0003-0871-7156>
 */

#ifndef EXT_PROCESS_H
#define EXT_PROCESS_H

#define CURRENT_TIME Now().As(Time::S)

// Macros for process messaging
#define MSG_KILL "PROCESS_KILL"
#define MSG_DELIM "<ENDSTR>"
#define MSG_EOL '\n'

#include "ns3/object.h"
#include "ns3/nstime.h"
#include <sys/types.h>
#include <pthread.h>
#include <fstream>
#include <map>
#include <ctime>
#include <boost/asio.hpp>
#include <list>

namespace ns3 {

/**
 * \brief Function to use in the watchdog thread.
 * 
 * \param [in] arg Pointer to process failure policy (True = crash on failues).
 * 
 * \return No return (constant nullptr).
*/
void* WatchdogFunction(void* arg);

/**
 * \brief Function to use in acceptor threads, if 'TimedAccept' is used.
 * 
 * \param [in] arg Pointer to instance of \ref ExternalProcess::AcceptorData.
 * 
 * \return Pointer to outcome status (True if a connection 
 * has been established prior to timeout, False otherwise; 
 * fatal errors are signaled by returning nullptr).
 * 
 * \see ExternalProcess::TimedSocketOperation()
*/
void* AcceptorFunction(void* arg);

/**
 * \brief Function to use in writer threads, if 'TimedWrite' is used.
 * 
 * \param [in] arg Pointer to instance of \ref ExternalProcess::WriterData.
 * 
 * \return Pointer to outcome status (True if all data has 
 * been written prior to timeout, False otherwise; fatal 
 * errors are signaled by returning nullptr).
 * 
 * \see ExternalProcess::TimedSocketOperation()
*/
void* WriterFunction(void* arg);

/**
 * \brief Function to use in reader threads, if 'TimedRead' is used.
 * 
 * \param [in] arg Pointer to instance of \ref ExternalProcess::ReaderData.
 * 
 * \return Pointer to outcome status (True if any data has 
 * been read prior to timeout, False otherwise; fatal 
 * errors are signaled by returning nullptr).
 * 
 * \see ExternalProcess::TimedSocketOperation()
*/
void* ReaderFunction(void* arg);



/**
 * \brief Class for handling an external side process interacting 
 * with ns-3. 
 * 
 * This class creates a new process upon initialization and sets up 
 * communication channels via a TCP socket. 
 * Employing a leader/follower classification of roles, ns-3 acts as 
 * a leader, with the external process taking the role of the follower. 
 * As such, objects of this class shall set up the TCP server, with the 
 * client necessarily implemented by the external process. 
 * 
 * A watchdog thread is spawned upon the first instance of this class 
 * being created, running until the last ExternalProcess object goes out 
 * of scope. 
 * This thread periodically checks whether external processes are still 
 * alive by means of their PID. 
 * Watchdog settings may be customized via attributes 'CrashOnFailure' and 
 * 'WatchdogPeriod'. 
 * 
 * By default, socket operations are blocking, possibly for an indefinite time. 
 * Attributes 'TimedAccept', 'TimedWrite', and 'TimedRead' change the behavior 
 * of respective socket operations -- i.e. accept(), write(), and read() -- into 
 * using a timeout and repeated attempts. 
 * Attributes 'Timeout' and 'Attempts' allow customization of such behavior, but 
 * are applied to any enabled timed operation equally.
 * 
 * \warning All socket operations are blocking, even in their timed versions. 
 * Asynchronous mode using callbacks is out of the scope of the current implementation.
*/
class ExternalProcess: public Object
{
public:
  /** \brief Represents the argument for a watchdog thread. */
  struct WatchdogData {
    bool m_crashOnFailure;    //!< [in] Flag indicating whether to raise a fatal exeception if the external process fails.
    Time m_period;            //!< [in] Time period spent sleeping by the watchdog thread at the beginning of the PID checking loop.
  };

  /** \brief Represents additional arguments for implementing BlockingSocketOperation() in thread functions. */
  struct BlockingArgs {
    pthread_t* m_threadId = nullptr;      //!< [inout] Pointer to the blocking thread ID to set to -1.
    bool* m_exitNormal = nullptr;         //!< [inout] Pointer to flag indicating normal exit from thread.
    pthread_mutex_t* m_mutex = nullptr;   //!< [in] Pointer to the mutex to use.
    pthread_cond_t* m_cond = nullptr;     //!< [in] Pointer to the conditional variable to use.

    /** \brief Constructor. */
    BlockingArgs(pthread_t* threadId = nullptr,
                 bool* exitNormal = nullptr,
                 pthread_mutex_t* mutex = nullptr,
                 pthread_cond_t* cond = nullptr)
      : m_threadId(threadId),
        m_exitNormal(exitNormal),
        m_mutex(mutex),
        m_cond(cond)
    {
    };
  };

  /** \brief Represents the argument for an acceptor thread, if 'TimedAccept' is used. */
  struct AcceptorData {
    boost::asio::ip::tcp::acceptor* m_acceptor = nullptr;   //!< [in] Pointer to boost::asio acceptor.
    boost::asio::ip::tcp::socket* m_sock = nullptr;         //!< [in] Pointer to boost::asio socket.
    boost::system::error_code* m_errc = nullptr;            //!< [out] Pointer to boost::system error code.
    BlockingArgs* m_blockingArgs = nullptr;                 //!< [in] Pointer to additional args for blocking operations, if provided; if nullptr, timed operation is assumed.

    /** \brief Constructor. */
    AcceptorData(boost::asio::ip::tcp::acceptor* acceptor = nullptr,
                 boost::asio::ip::tcp::socket* sock = nullptr,
                 boost::system::error_code* errc = nullptr,
                 BlockingArgs* bArgs = nullptr)
      : m_acceptor(acceptor),
        m_sock(sock),
        m_errc(errc),
        m_blockingArgs(bArgs)
    {
    };
  };

  /** \brief Represents the argument for a writer thread, if 'TimedWrite' is used. */
  struct WriterData {
    boost::asio::ip::tcp::socket* m_sock = nullptr;   //!< [in] Pointer to boost::asio socket.
    boost::asio::mutable_buffer* m_buf = nullptr;     //!< [in] Pointer to boost::asio::streambuf to write data from.
    boost::system::error_code* m_errc = nullptr;      //!< [out] Pointer to boost::system error code.
    BlockingArgs* m_blockingArgs = nullptr;           //!< [in] Pointer to additional args for blocking operations, if provided; if nullptr, timed operation is assumed.

    /** \brief Constructor. */
    WriterData(boost::asio::ip::tcp::socket* sock = nullptr,
               boost::asio::mutable_buffer* buf = nullptr,
               boost::system::error_code* errc = nullptr,
               BlockingArgs* bArgs = nullptr)
      : m_sock(sock),
        m_buf(buf),
        m_errc(errc),
        m_blockingArgs(bArgs)
    {
    };
  };

  /** \brief Represents the argument for a reader thread, if 'TimedRead' is used. */
  struct ReaderData {
    boost::asio::ip::tcp::socket* m_sock = nullptr;   //!< [in] Pointer to boost::asio socket.
    boost::asio::streambuf* m_buf = nullptr;          //!< [out] Pointer to boost::asio::streambuf to read data to.
    boost::system::error_code* m_errc = nullptr;      //!< [out] Pointer to boost::system error code.
    BlockingArgs* m_blockingArgs = nullptr;           //!< [in] Pointer to additional args for blocking operations, if provided; if nullptr, timed operation is assumed.

    /** \brief Constructor. */
    ReaderData(boost::asio::ip::tcp::socket* sock = nullptr,
               boost::asio::streambuf* buf = nullptr,
               boost::system::error_code* errc = nullptr,
               BlockingArgs* bArgs = nullptr)
      : m_sock(sock),
        m_buf(buf),
        m_errc(errc),
        m_blockingArgs(bArgs)
    {
    };
  };

  /**
   * \brief Terminates all external processes spawned during this simulation.
   * 
   * \note This function should be invoked whenever NS_FATAL_ERROR is used, preventing 
   * external processes to remain alive despite no chance of further communication.
   * 
   * \warning This function is thread-safe.
  */
  static void GracefulExit(void);

  /** \brief Default constructor. */
  ExternalProcess();

  /** \brief Default destructor. */
  virtual ~ExternalProcess();

  /**
   * \brief Registers this type.
   * 
   * \return The TypeId.
  */
  static TypeId GetTypeId(void);

  /**
   * \brief Creates a side process given a launcher script, accepting 
   * connections from it.
   * 
   * \return True if the creation has been successful, False otherwise.
   * 
   * \warning This operation may be blocking.
  */
  bool Create(void);

  /**
   * \brief Retrieves whether the side process is running or not.
   * 
   * \return True if the side process is running, False otherwise.
  */
  bool IsRunning(void) const;

  /**
   * \brief Retrieves the PID of the side process.
   * 
   * \return The PID of the side process previously set up via ExternalProcess::Create().
  */
  pid_t GetPid(void) const;

  /**
   * \brief Performs process teardown operations using the result of \ref ExternalProcess::GetPid().
   * 
   * \return True if this external process is no longer tracked by the watchdog, False otherwise.
   * 
   * \warning This function is thread-safe.
   * 
   * \see ExternalProcess::GetPid()
   * \see ExternalProcess::DoTeardown()
  */
  bool Teardown(void);

  /**
   * \brief Performs process teardown operations.
   * 
   * \param [in] childPid PID of the child process associated with this teardown procedure. 
   * If different than -1, it will send a SIGKILL signal to the provided PID.
   * 
   * \return True if this external process is no longer tracked by the watchdog, False otherwise.
   * 
   * \warning This function is NOT thread-safe: a lock shall be acquired on 'g_watchdogTeardownMutex' 
   * previously to the invocation of this function.
   * 
   * \see ExternalProcess::DoTeardown()
  */
  bool Teardown(pid_t childPid);

  /**
   * \brief Writes a string to the external process through the socket.
   * 
   * \param [in] str String to write.
   * \param [in] first Whether the string is the first of a series of writes (Default: true).
   * \param [in] flush Whether to flush after writing this string or not (Default: true).
   * \param [in] last Whether the string is the last of a series of writes (Default: true).
   * 
   * \return True if the operation is successful, False otherwise.
   * 
   * \warning This operation may be blocking.
  */
  bool Write(const std::string &str, bool first = true, bool flush = true, bool last = true);

  /**
   * \brief Reads a string from the external process through the socket.
   * 
   * \param [out] str String read (if return is True; discard otherwise).
   * \param [out] hasNext Whether there is going to be a next line or not.
   * 
   * \return True if the operation is successful, False otherwise.
   * 
   * \warning This operation may be blocking.
  */
  bool Read(std::string &str, bool &hasNext);

protected:
  /** \brief Initializes the object. */
  virtual void DoInitialize(void);

  /** \brief Frees all resources. */
  virtual void DoDispose(void);

private:
  static uint32_t m_counter;                    //!< Number of currently running instances of this class.
  static bool m_watchdogInit;                   //!< Flag indicating whether the watchdog thread has been created or not.
  bool m_processRunning;                        //!< Flag indicating whether the side process is running or not.
  pid_t m_processPid;                           //!< PID for side process.
  boost::asio::io_context m_ioCtx;              //!< I/O context used to create the underlying socket.
  boost::asio::ip::tcp::socket* m_sock;         //!< Socket for communicating with the external process.
  boost::asio::ip::tcp::acceptor* m_acceptor;   //!< TCP server setup utility for incoming connections.
  struct timespec m_lastWrite;                  //!< Timestamp of latest invocation of Write().
  struct timespec m_lastRead;                   //!< Timestamp of latest invocation of Read().

  // Internal buffers for DoWrite() and DoRead()
  std::list<std::pair<std::string, bool>> m_bufferWrite;    //!< Buffer supporting consecutive Write() invocations from depending scripts or modules; each string is associated with a "flush request" flag.
  std::list<std::string> m_bufferRead;                      //!< Buffer supporting consecutive Read() invocations from depending scripts or modules.

  // Support variables to prevent indefinitely blocking on socket operations (even without timeout/attempts enabled)
  pthread_t m_blockingThread;         //!< Thread ID of the ongoing blocking thread, if any; (pthread_t)-1 otherwise.
  bool m_blockingExitNormal;          //!< Flag indicating whether the blocking thread exited normally.
  pthread_mutex_t m_blockingMutex;    //!< Mutex for conditional variable used in BlockingSocketOperation().
  pthread_cond_t m_blockingCond;      //!< Conditional variable used in BlockingSocketOperation().

  // --- Attributes ---
  std::string m_processLauncher;    //!< Absolute path to the side process launcher script.
  std::string m_processArgs;        //!< String containing command-line arguments for the launcher script; tokens will be split by whitespace first.
  bool m_crashOnFailure;            //!< Flag indicating whether to raise a fatal exeception if the external process fails.
  Time m_watchdogPeriod;            //!< Time period spent sleeping by the watchdog thread at the beginning of the PID checking loop; lower values will allow detection of process errors quicker, longer values greatly reduce busy waits.
  Time m_gracePeriod;               //!< Time period spent sleeping after killing a process, potentially allowing any temporary data on the process to be stored.
  uint16_t m_processPort;           //!< Port number for communicating with external process; if 0, a free port will be automatically selected by the OS.
  Time m_sockTimeout;               //!< Maximum waiting time for socket operations (e.g. accept); if 0, no timeout is implemented.
  uint32_t m_sockAttempts;          //!< Maximum attempts for socket operations (e.g. accept); only if a timeout is specified.
  bool m_timedAccept;               //!< Flag indicating whether to apply a timeout on socket accept(), implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.
  bool m_timedWrite;                //!< Flag indicating whether to apply a timeout on socket write(), implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.
  bool m_timedRead;                 //!< Flag indicating whether to apply a timeout on socket read_until(), implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.
  Time m_throttleWrites;            //!< Minimum time between a read and a subsequent write; this delay is applied before writing.
  Time m_throttleReads;             //!< Minimum time between a write and a subsequent read; this delay is applied before reading.
  // --- ----- ----- ---

  /**
   * \brief Performs process teardown operations. 
   * 
   * This function supports \ref ExternalProcess::Teardown() and \ref ExternalProcess::GracefulExit(), 
   * enabling individual and mass external process teardown operations.
   * 
   * \param [in] childPid PID of the child process associated with this teardown procedure. 
   * If different than -1, it will send a SIGKILL signal to the provided PID.
   * \param [in] eraseRunner Flag indicating whether the runner map has to be updated or not.
   * 
   * \return True if this external process is no longer tracked by the watchdog, False otherwise.
  */
  bool DoTeardown(pid_t childPid, bool eraseRunner);

  /**
   * \brief This function supports \ref ExternalProcess::Write(), writing all strings that 
   * have been previously buffered through the socket.
   * 
   * \return True if the operation is successful, False otherwise.
   * 
   * \warning This operation may be blocking.
  */
  bool DoWrite(void);

  /**
   * \brief This function supports \ref ExternalProcess::Read(), storing all strings read 
   * from the socket until no more data is present.
   * 
   * \return True if the operation is successful, False otherwise.
   * 
   * \warning This operation may be blocking.
  */
  bool DoRead(void);

  /**
   * \brief Checks whether throttling is set up for the given operation, eventually 
   * enforcing it.
   * 
   * \param [in] isRead Flag indicating throttling is being checked for Read; default: False (Write).
   * 
   * \warning This function is blocking for the caller thread in case throttling is set up for this operation.
  */
  void ThrottleOperation(bool isRead = false);

  /**
   * \brief Wrapper for socket operations enabling timeout. 
   * 
   * This function spawns a POSIX thread executing the provided function, also 
   * passing additional arguments. 
   * The timeout is enforced via pthread_timedjoin_np() from GNU extensions to POSIX.
   * 
   * \param [in] pthreadFn C-style pointer to function to run in a separate thread. 
   * Thread functions should return bool* s.t. nullptr indicates a fatal failure, 
   * False indicates a failure, and True indicates a success.
   * \param [inout] pthreadArg C-style pointer to thread function arguments; their 
   * usage depends entirely on the specific function provided.
   * \param [out] fatalFailure Flag indicating whether a return value equal to False 
   * should be interpreted as a fatal failure.
   * 
   * \return True if the operation is successful within the timeout, False otherwise.
   * 
   * \see AcceptorFunction()
   * \see WriterFunction()
   * \see ReaderFunction()
  */
  bool TimedSocketOperation(void* (pthreadFn(void*)), void* pthreadArg, bool &fatalFailure);

  /**
   * \brief Wrapper for socket operations without timeout. 
   * 
   * This function spawns a POSIX thread executing the provided function, also 
   * passing additional arguments. 
   * This is necessary to enable \ref ExternalProcess::Teardown() to carry out 
   * resources cleanup, thus interrupting Boost ASIO's blocking operations.
   * 
   * \param [in] pthreadFn C-style pointer to function to run in a separate thread. 
   * Thread functions should return bool* s.t. nullptr indicates a fatal failure, 
   * False indicates a failure, and True indicates a success. Additionally, functions 
   * should signal a conditional variable guarding from this thread's join phase 
   * causing dereference of an uninitialized pointer.
   * \param [inout] pthreadArg C-style pointer to thread function arguments; their 
   * usage depends entirely on the specific function provided.
   * \param [out] fatalFailure Flag indicating whether a return value equal to False 
   * should be interpreted as a fatal failure.
   * 
   * \return True if the operation is successful, False otherwise.
   * 
   * \see AcceptorFunction()
   * \see WriterFunction()
   * \see ReaderFunction()
  */
  bool BlockingSocketOperation(void* (pthreadFn(void*)), void* pthreadArg, bool &fatalFailure);

}; // class ExternalProcess



/** \brief Represents the global variable holding arguments for the watchdog thread. */
static struct WatchdogSupport {
  bool m_initialized = false;                         //!< Flag indicating whether the support variabled is initialized.
  ExternalProcess::WatchdogData* m_args = nullptr;    //!< Pointer to the watchdog arguments to use.
} g_watchdogArgs;

static pthread_t g_watchdog;                                                  //!< Watchdog thread for checking running instances.
static bool g_watchdogExit = false;                                           //!< Flag indicating the exit condition for the watchdog thread.
static std::map<pid_t, ExternalProcess*> g_runnerMap;                         //!< Map associating PID to instances that spawned them.
static pthread_mutex_t g_watchdogExitMutex = PTHREAD_MUTEX_INITIALIZER;       //!< Mutex for exit condition for the watchdog thread.
static pthread_mutex_t g_watchdogMapMutex = PTHREAD_MUTEX_INITIALIZER;        //!< Mutex for runner map for the watchdog thread.
static pthread_mutex_t g_watchdogTeardownMutex = PTHREAD_MUTEX_INITIALIZER;   //!< Mutex for accessing ExternalProcess::Teardown() from multiple threads.
static pthread_mutex_t g_gracefulExitMutex = PTHREAD_MUTEX_INITIALIZER;       //!< Mutex for accessing ExternalProcess::GracefulExit() from multiple threads.

} // namespace ns3

#endif /* EXT_PROCESS_H */

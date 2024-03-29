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
#define PIPE_TRAIL_IN "pipe_proc_to_ns3"
#define PIPE_TRAIL_OUT "pipe_ns3_to_proc"

// Macros for process messaging
#define MSG_KILL "PROCESS_KILL"
#define MSG_READY "PROCESS_READY"
#define MSG_WRITE_BEGIN "NS3_WRITE_BEGIN"
#define MSG_WRITE_END "NS3_WRITE_END"
#define MSG_READ_BEGIN "NS3_READ_BEGIN"
#define MSG_READ_END "NS3_READ_END"

#include "ns3/object.h"
#include "ns3/nstime.h"
#include <sys/types.h>
#include <pthread.h>
#include <fstream>
#include <map>
#include <ctime>

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
 * \brief Class for handling an external side process interacting 
 * with ns-3. 
 * 
 * This class creates a new process upon initialization and sets up 
 * communication channels via named pipes. 
 * Streams regarding communication channels are opened and closed at 
 * need, in order to leverage named pipes' blocking on empty reads, 
 * thus avoiding busy waits. 
 * The external process should operate in the same way as well.
*/
class ExternalProcess: public Object
{
public:
  /**
   * \brief Terminates all external processes spawned during this simulation.
   * 
   * \note This function should be invoked whenever NS_FATAL_ERROR is used, preventing 
   * external processes to remain alive despite no chance of further communication.
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
   * \brief Creates a side process given a launcher script.
   * 
   * \return True if the creation has been successful, False otherwise.
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
   * \brief Performs process teardown operations (e.g. deleting named pipes, etc.).
   * 
   * \param [in] childPid PID of the child process associated with this teardown procedure. 
   * If different than -1, it will send a SIGKILL signal to the provided PID.
   * 
   * \see ExternalProcess::DoTeardown()
  */
  void Teardown(pid_t childPid);

  /**
   * \brief Writes a string as a line to the output named pipe (ns-3 --> process).
   * 
   * \param [in] str String to write to the named pipe.
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
   * \brief Reads a line from the input named pipe (process --> ns-3) and returns it as a string.
   * 
   * \param [out] str String read from the named pipe (if return is True; discard otherwise).
   * \param [out] hasNext Whether there is going to be a next line or not.
   * 
   * \return True if the operation is successful, False otherwise.
   * 
   * \warning This operation may be blocking.
  */
  bool Read(std::string &str, bool &hasNext);

protected:
  /** \brief Frees all resources. */
  virtual void DoDispose(void);

private:
  static uint32_t m_counter;      //!< Number of currently running instances of this class.
  static bool m_watchdogInit;     //!< Flag indicating whether the watchdog thread has been created or not.
  bool m_processRunning;          //!< Flag indicating whether the side process is running or not.
  pid_t m_processPid;             //!< PID for side process.
  std::string m_pipeInName;       //!< Filename associated with named pipe (process --> ns-3).
  std::string m_pipeOutName;      //!< Filename associated with named pipe (ns-3 --> process).
  std::fstream m_pipeInStream;    //!< Filestream associated with named pipe (process --> ns-3).
  std::fstream m_pipeOutStream;   //!< Filestream associated with named pipe (ns-3 --> process).
  struct timespec m_lastWrite;    //!< Timestamp of latest invocation of Write().
  struct timespec m_lastRead;     //!< Timestamp of latest invocation of Read().
  uint32_t m_emptyCount;          //!< Number of consequential empty Read() invocations.
  struct timespec m_firstRead;    //!< Timestamp of first empty Read() among consequential ones.

  // --- Attributes ---
  std::string m_processLauncher;    //!< Absolute path to the side process launcher script.
  std::string m_processExtraArgs;   //!< String containing additional arguments to side process launcher script.
  bool m_crashOnFailure;            //!< Flag indicating whether to raise a fatal exeception if the external process fails.
  Time m_throttleWrites;            //!< Minimum time between a read and a subsequent write; this delay is applied before writing.
  Time m_throttleReads;             //!< Minimum time between a write and a subsequent read; this delay is applied before reading.
  Time m_readHangsTimeout;          //!< Timeout for preventing a simulation from hanging on empty reads; only applied for consecutive reads only.
  // --- ----- ----- ---

  /**
   * \brief Retrieves a temporary filename to use for a named pipe.
   * 
   * \param [in] isInput True if the filename is for an input pipe (process --> ns-3), 
   * False otherwise (ns-3 --> process).
   * 
   * \return A string containing a unique temporary filename to use for a named pipe.
  */
  const std::string GetFifoTmpName(bool isInput) const;

  /**
   * \brief Creates a named pipe through the mkfifo POSIX syscall.
   * 
   * \param [in] fifoName Absolute path to use for named pipe creation.
   * 
   * \return True if the named pipe has been successfully created, False otherwise.
  */
  bool CreateFifo(const std::string fifoName) const;

  /**
   * \brief Performs process teardown operations (e.g. deleting named pipes, etc.). 
   * 
   * This function supports \ref ExternalProcess::Teardown() and \ref ExternalProcess::GracefulExit(), 
   * enabling individual and mass external process teardown operations.
   * 
   * \param [in] childPid PID of the child process associated with this teardown procedure. 
   * If different than -1, it will send a SIGKILL signal to the provided PID.
   * \param [in] eraseRunner Flag indicating whether the runner map has to be updated or not.
  */
  void DoTeardown(pid_t childPid, bool eraseRunner);

  /**
   * \brief Reads a line from the input named pipe (process --> ns-3) and returns it as a string. 
   * 
   * This function supports \ref ExternalProcess::Read(), enabling checking for cases in which 
   * simulations hang on consecutive empty reads.
   * 
   * \param [out] str String read from the named pipe (if return is True; discard otherwise).
   * \param [out] hasNext Whether there is going to be a next line or not.
   * 
   * \return True if the operation is successful, False otherwise.
   * 
   * \warning This operation may be blocking.
  */
  bool DoRead(std::string &str, bool &hasNext);

  /**
   * \brief Checks whether throttling is set up for the given operation, eventually 
   * enforcing it.
   * 
   * \param [in] isRead Flag indicating throttling is being checked for Read; default: False (Write).
   * 
   * \warning This function is blocking for the caller thread in case throttling is set up for this operation.
  */
  void ThrottleOperation(bool isRead = false);

}; // class ExternalProcess



static std::map<pid_t, ExternalProcess*> g_runnerMap;                 //!< Map associating PID to instances that spawned them.
static pthread_t g_watchdog;                                          //!< Watchdog thread for checking running instances.
static bool g_watchdogExit = false;                                   //!< Flag indicating the exit condition for the watchdog thread.
static pthread_mutex_t g_watchdogMutex = PTHREAD_MUTEX_INITIALIZER;   //!< Mutex for runner map and exit condition for the watchdog thread.

} // namespace ns3

#endif /* EXT_PROCESS_H */

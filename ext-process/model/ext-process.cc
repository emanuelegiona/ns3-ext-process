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

#include "ext-process.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <chrono>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(ExternalProcess);
NS_LOG_COMPONENT_DEFINE("ExternalProcess");

void*
WatchdogFunction(void* arg)
{
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): thread started");
  if(arg == nullptr)
  {
    ExternalProcess::GracefulExit();
    NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess (watchdog): invalid args");
    return nullptr;
  }

  // Fetch watchdog setup
  ExternalProcess::WatchdogData wd = *(ExternalProcess::WatchdogData*)arg;
  bool crashOnFailure = wd.m_crashOnFailure;
  Time watchdogPeriod = wd.m_period;
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): setup with " << (crashOnFailure ? "crash on failure, " : "") << "period = " << watchdogPeriod.As(Time::S));

  // Keep checking if instances are running
  while(true)
  {
    // Avoid busy waits
    struct timespec sleepDelay;
    sleepDelay.tv_sec = watchdogPeriod.GetNanoSeconds() / (int64_t)1000000000UL;
    sleepDelay.tv_nsec = watchdogPeriod.GetNanoSeconds() % (int64_t)1000000000UL;
    nanosleep(&sleepDelay, nullptr);

    bool unlockMap = false;
    bool prematureLoopExit = false;
    bool prematureThreadExit = false;

    // Attempt to acquire the runner map lock
    int lockOutcome = pthread_mutex_trylock(&g_watchdogMapMutex);
    if(lockOutcome == 0)
    {
      // Lock acquired, execute the runner map loop
    }
    else if(lockOutcome == EBUSY)
    {
      // Lock not acquired, runner map is being updated elsewhere
      continue;
    }
    else
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): could not acquire lock on runner map; reason = " << std::strerror(errno));
      break;
    }

    // Runner map loop checking whether external processes are still alive
    for(auto runnerIt = g_runnerMap.begin(); runnerIt != g_runnerMap.end(); runnerIt++)
    {
      unlockMap = false;
      prematureLoopExit = false;

      ExternalProcess* runner = runnerIt->second;
      if(runner == nullptr || !runner->IsRunning())
      {
        continue;
      }

      // Check if current PID is actually not running anymore
      pid_t childPid = runnerIt->first;
      pid_t retPid = -1;
      int status = -1;
      if((retPid = waitpid(childPid, &status, WNOHANG)) < 0)
      {
        prematureThreadExit = true;
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): error on checking PID " << childPid << " status; reason = " << std::strerror(errno));
        break;
      }
      else
      {
        // No status change, still running
        if(retPid == 0)
        {
          continue;
        }

        // Attempt at cleaning up the external process (must acquire lock for Teardown)
        lockOutcome = pthread_mutex_trylock(&g_watchdogTeardownMutex);
        if(lockOutcome == 0)
        {
          // Lock acquired, invoke Teardown() on this runner
          unlockMap = true;
        }
        else if(lockOutcome == EBUSY)
        {
          // Lock not acquired, Teardown() is already being executed from the runner itself
          unlockMap = true;
          prematureLoopExit = true;
        }
        else
        {
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): could not acquire lock on Teardown; reason = " << std::strerror(errno));
          prematureThreadExit = true;
          break;
        }

        // Runner map must be unlocked for ExternalProcess::Teardown() to remove the runner itself from the map
        if(unlockMap)
        {
          pthread_mutex_unlock(&g_watchdogMapMutex);
        }

        // Runner map loop must be canceled for other threads to perform their cleanup phases
        if(prematureLoopExit)
        {
          break;
        }

        // Prevent sending SIGKILL to an already terminated child process (PID = -1)
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): unexpected failure on PID " << childPid << "; initiating Teardown");
        prematureLoopExit = runner->Teardown(-1);
        pthread_mutex_unlock(&g_watchdogTeardownMutex);

        // Invalid runner map iterator after Teardown, new loop required
        if(prematureLoopExit)
        {
          break;
        }

        // Only crash on PID failures if specified so
        if(crashOnFailure)
        {
          prematureThreadExit = true;
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): unexpected failure on PID " << childPid << "; Teardown completed, exiting with FATAL ERROR");
          break;
        }
      }
    }

    // Mutex already released for Teardown()
    if(!unlockMap)
    {
      pthread_mutex_unlock(&g_watchdogMapMutex);
    }

    // Exiting from the watchdog thread loop due to fatal error
    if(prematureThreadExit)
    {
      break;
    }

    // Check for exit condition
    bool doExit = false;
    pthread_mutex_lock(&g_watchdogExitMutex);
    doExit = g_watchdogExit;
    pthread_mutex_unlock(&g_watchdogExitMutex);
    if(doExit)
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): thread exiting...");
      break;
    }
  }

  // Kill all remaining instances on watchdog exit
  if(!g_runnerMap.empty())
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): terminating all remaining instances");
    ExternalProcess::GracefulExit();
  }
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): thread exiting... completed");
  return nullptr;
}

void*
AcceptorFunction(void* arg)
{
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (acceptor): thread started");

  // Sanity thread check argument
  if(arg == nullptr)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (acceptor): invalid args");
    return nullptr;
  }
  ExternalProcess::AcceptorData ad = *(ExternalProcess::AcceptorData*)arg;

  // For blocking threads only: wait for this thread's ID storage
  if(ad.m_blockingArgs)
  {
    if((ad.m_blockingArgs->m_threadId == nullptr) || 
       (ad.m_blockingArgs->m_exitNormal == nullptr) || 
       (ad.m_blockingArgs->m_mutex == nullptr) || 
       (ad.m_blockingArgs->m_cond == nullptr))
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (acceptor): invalid pointers passed via blocking args");
      return nullptr;
    }

    // Do nothing else
    pthread_mutex_lock(ad.m_blockingArgs->m_mutex);
    while(*(ad.m_blockingArgs->m_threadId) == (pthread_t)-1)
    {
      pthread_cond_wait(ad.m_blockingArgs->m_cond, ad.m_blockingArgs->m_mutex);
    }
    pthread_mutex_unlock(ad.m_blockingArgs->m_mutex);
  }

  // Allow this thread to be canceled at any time
  if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (acceptor): could not set a cancelable thread; reason = " << std::strerror(errno));
    return nullptr;
  }
  if(pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != 0)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (acceptor): could not set an async cancelable thread; reason = " << std::strerror(errno));
    return nullptr;
  }

  // Sanity check acceptor data fields
  if((ad.m_threadOutcome == nullptr) || (ad.m_acceptor == nullptr) || (ad.m_sock == nullptr) || (ad.m_errc == nullptr))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (acceptor): invalid pointers passed via args");
    return nullptr;
  }

  // Initialize outcome to non-fatal failure
  *(ad.m_threadOutcome) = ExternalProcess::EP_THREAD_OUTCOME::FAILURE;

  // Blocking until a connection arrives
  ad.m_acceptor->accept(*(ad.m_sock), *(ad.m_errc));

  // For blocking threads only: signal outcome ready to waiting threads
  if(ad.m_blockingArgs)
  {
    // Unset thread ID: indicates thread is about to terminate
    pthread_mutex_lock(ad.m_blockingArgs->m_mutex);
    *(ad.m_blockingArgs->m_threadId) = (pthread_t)-1;
    *(ad.m_blockingArgs->m_exitNormal) = true;
    pthread_cond_broadcast(ad.m_blockingArgs->m_cond);
    pthread_mutex_unlock(ad.m_blockingArgs->m_mutex);
  }

  // Error occurred, signal non-fatal unsuccessful outcome (already set)
  if(*(ad.m_errc))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (acceptor): thread exiting (failure)");
    return nullptr;
  }

  // If no error occurs, signal so by clearing whather error code was previously set
  else
  {
    ad.m_errc->clear();
    *(ad.m_threadOutcome) = ExternalProcess::EP_THREAD_OUTCOME::SUCCESS;
  }

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (acceptor): thread exiting (success)");
  return nullptr;
}

void*
ConnectorFunction(void* arg)
{
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (connector): thread started");

  // Sanity check thread argument
  if(arg == nullptr)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (connector): invalid args");
    return nullptr;
  }
  ExternalProcess::ConnectorData cd = *(ExternalProcess::ConnectorData*)arg;

  // For blocking threads only: wait for this thread's ID storage
  if(cd.m_blockingArgs)
  {
    if((cd.m_blockingArgs->m_threadId == nullptr) || 
       (cd.m_blockingArgs->m_exitNormal == nullptr) || 
       (cd.m_blockingArgs->m_mutex == nullptr) || 
       (cd.m_blockingArgs->m_cond == nullptr))
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (connector): invalid pointers passed via blocking args");
      return nullptr;
    }

    // Do nothing else
    pthread_mutex_lock(cd.m_blockingArgs->m_mutex);
    while(*(cd.m_blockingArgs->m_threadId) == (pthread_t)-1)
    {
      pthread_cond_wait(cd.m_blockingArgs->m_cond, cd.m_blockingArgs->m_mutex);
    }
    pthread_mutex_unlock(cd.m_blockingArgs->m_mutex);
  }

  // Allow this thread to be canceled at any time
  if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (connector): could not set a cancelable thread; reason = " << std::strerror(errno));
    return nullptr;
  }
  if(pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != 0)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (connector): could not set an async cancelable thread; reason = " << std::strerror(errno));
    return nullptr;
  }

  // Sanity check connector data fields
  if((cd.m_threadOutcome == nullptr) || (cd.m_sock == nullptr) || (cd.m_endpoints == nullptr) || (cd.m_errc == nullptr))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (connector): invalid pointers passed via args");
    return nullptr;
  }

  // Initialize outcome to non-fatal failure
  *(cd.m_threadOutcome) = ExternalProcess::EP_THREAD_OUTCOME::FAILURE;

  // Blocking until a connection is established
  boost::asio::connect(*(cd.m_sock), *(cd.m_endpoints), *(cd.m_errc));

  // For blocking threads only: signal outcome ready to waiting threads
  if(cd.m_blockingArgs)
  {
    // Unset thread ID: indicates thread is about to terminate
    pthread_mutex_lock(cd.m_blockingArgs->m_mutex);
    *(cd.m_blockingArgs->m_threadId) = (pthread_t)-1;
    *(cd.m_blockingArgs->m_exitNormal) = true;
    pthread_cond_broadcast(cd.m_blockingArgs->m_cond);
    pthread_mutex_unlock(cd.m_blockingArgs->m_mutex);
  }

  // Error occurred, signal non-fatal unsuccessful outcome (already set)
  if(*(cd.m_errc))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (connector): thread exiting (failure)");
    return nullptr;
  }

  // If no error occurs, signal so by clearing whather error code was previously set
  else
  {
    cd.m_errc->clear();
    *(cd.m_threadOutcome) = ExternalProcess::EP_THREAD_OUTCOME::SUCCESS;
  }

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (connector): thread exiting (success)");
  return nullptr;
}

void*
WriterFunction(void* arg)
{
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (writer): thread started");

  // Sanity check thread argument
  if(arg == nullptr)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (writer): invalid args");
    return nullptr;
  }
  ExternalProcess::WriterData wd = *(ExternalProcess::WriterData*)arg;

  // For blocking threads only: wait for this thread's ID storage
  if(wd.m_blockingArgs)
  {
    if((wd.m_blockingArgs->m_threadId == nullptr) || 
       (wd.m_blockingArgs->m_exitNormal == nullptr) || 
       (wd.m_blockingArgs->m_mutex == nullptr) || 
       (wd.m_blockingArgs->m_cond == nullptr))
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (writer): invalid pointers passed via blocking args");
      return nullptr;
    }

    // Do nothing else
    pthread_mutex_lock(wd.m_blockingArgs->m_mutex);
    while(*(wd.m_blockingArgs->m_threadId) == (pthread_t)-1)
    {
      pthread_cond_wait(wd.m_blockingArgs->m_cond, wd.m_blockingArgs->m_mutex);
    }
    pthread_mutex_unlock(wd.m_blockingArgs->m_mutex);
  }

  // Allow this thread to be canceled at any time
  if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (writer): could not set a cancelable thread; reason = " << std::strerror(errno));
    return nullptr;
  }
  if(pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != 0)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (writer): could not set an async cancelable thread; reason = " << std::strerror(errno));
    return nullptr;
  }

  if((wd.m_threadOutcome == nullptr) || (wd.m_sock == nullptr) || (wd.m_buf == nullptr) || (wd.m_errc == nullptr))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (writer): invalid pointers passed via args");
    return nullptr;
  }

  // Blocking until data is all written
  size_t targetLen = wd.m_buf->size();
  size_t len = boost::asio::write(*(wd.m_sock), *(wd.m_buf), *(wd.m_errc));

  // For blocking threads only: signal outcome ready to waiting threads
  if(wd.m_blockingArgs)
  {
    // Unset thread ID: indicates thread is about to terminate
    pthread_mutex_lock(wd.m_blockingArgs->m_mutex);
    *(wd.m_blockingArgs->m_threadId) = (pthread_t)-1;
    *(wd.m_blockingArgs->m_exitNormal) = true;
    pthread_cond_broadcast(wd.m_blockingArgs->m_cond);
    pthread_mutex_unlock(wd.m_blockingArgs->m_mutex);
  }

  // Initialize outcome to non-fatal failure
  *(wd.m_threadOutcome) = ExternalProcess::EP_THREAD_OUTCOME::FAILURE;

  // Error occurred, signal non-fatal unsuccessful outcome (already set)
  if(*(wd.m_errc))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (writer): thread exiting (failure)");
    return nullptr;
  }

  // If no error occurs, signal so by clearing whather error code was previously set
  else
  {
    wd.m_errc->clear();

    // Double check written bytes despite no error
    if(len == targetLen)
    {
      *(wd.m_threadOutcome) = ExternalProcess::EP_THREAD_OUTCOME::SUCCESS;
    }
    else
    {
      wd.m_errc->assign(boost::system::errc::bad_message, boost::system::system_category());
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (writer): thread exiting (failure)");
      return nullptr;
    }
  }

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (writer): thread exiting (success)");
  return nullptr;
}

void*
ReaderFunction(void* arg)
{
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (reader): thread started");

  // Sanity check thread argument
  if(arg == nullptr)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (reader): invalid args");
    return nullptr;
  }
  ExternalProcess::ReaderData rd = *(ExternalProcess::ReaderData*)arg;

  // For blocking threads only: wait for this thread's ID storage
  if(rd.m_blockingArgs)
  {
    if((rd.m_blockingArgs->m_threadId == nullptr) || 
       (rd.m_blockingArgs->m_exitNormal == nullptr) || 
       (rd.m_blockingArgs->m_mutex == nullptr) || 
       (rd.m_blockingArgs->m_cond == nullptr))
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (reader): invalid pointers passed via blocking args");
      return nullptr;
    }

    // Do nothing else
    pthread_mutex_lock(rd.m_blockingArgs->m_mutex);
    while(*(rd.m_blockingArgs->m_threadId) == (pthread_t)-1)
    {
      pthread_cond_wait(rd.m_blockingArgs->m_cond, rd.m_blockingArgs->m_mutex);
    }
    pthread_mutex_unlock(rd.m_blockingArgs->m_mutex);
  }

  // Allow this thread to be canceled at any time
  if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (reader): could not set a cancelable thread; reason = " << std::strerror(errno));
    return nullptr;
  }
  if(pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != 0)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (reader): could not set an async cancelable thread; reason = " << std::strerror(errno));
    return nullptr;
  }

  // Sanity check reader data fields
  if((rd.m_threadOutcome == nullptr) || (rd.m_sock == nullptr) || (rd.m_buf == nullptr) || (rd.m_errc == nullptr))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (reader): invalid pointers passed via args");
    return nullptr;
  }

  // Blocking until data arrives
  boost::asio::read_until(*(rd.m_sock), *(rd.m_buf), MSG_EOL, *(rd.m_errc));

  // For blocking threads only: signal outcome ready to waiting threads
  if(rd.m_blockingArgs)
  {
    // Unset thread ID: indicates thread is about to terminate
    pthread_mutex_lock(rd.m_blockingArgs->m_mutex);
    *(rd.m_blockingArgs->m_threadId) = (pthread_t)-1;
    *(rd.m_blockingArgs->m_exitNormal) = true;
    pthread_cond_broadcast(rd.m_blockingArgs->m_cond);
    pthread_mutex_unlock(rd.m_blockingArgs->m_mutex);
  }

  // Initialize outcome to non-fatal failure
  *(rd.m_threadOutcome) = ExternalProcess::EP_THREAD_OUTCOME::FAILURE;

  // Error occurred, signal non-fatal unsuccessful outcome (already set)
  if(*(rd.m_errc))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (reader): thread exiting (failure)");
    return nullptr;
  }

  // If no error occurs, signal so by clearing whather error code was previously set
  else
  {
    rd.m_errc->clear();
    *(rd.m_threadOutcome) = ExternalProcess::EP_THREAD_OUTCOME::SUCCESS;
  }

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (reader): thread exiting");
  return nullptr;
}



uint32_t ExternalProcess::m_counter = 0;
bool ExternalProcess::m_watchdogInit = false;

void
ExternalProcess::GracefulExit(void)
{
  int lockOutcome = pthread_mutex_trylock(&g_gracefulExitMutex);
  if(lockOutcome == 0)
  {
    // Lock acquired, this thread will carry out cleanup operations
  }
  else if(lockOutcome == EBUSY)
  {
    // Lock not acquired, GracefulExit() has already been called
    return;
  }
  else
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::GracefulExit: could not acquire lock on GracefulExit; reason = " << std::strerror(errno));
    return;
  }

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::GracefulExit; PIDs marked with '*' were actually running on exit");
  uint32_t totRunners = 0;
  uint32_t totAlive = 0;
  std::string pidsAlive = "";

  // Perform teardown operations on each running process
  do
  {
    lockOutcome = pthread_mutex_trylock(&g_watchdogMapMutex);
    if(lockOutcome == 0)
    {
      // Lock acquired, this thread will carry out cleanup operations
    }
    else if(lockOutcome == EBUSY)
    {
      // Lock not acquired, try again next iteration
      continue;
    }
    else
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::GracefulExit: could not acquire lock on GracefulExit; reason = " << std::strerror(errno));
      return;
    }

    bool unlockMap = false;
    bool prematureLoopExit = false;
    for(auto runnerIt = g_runnerMap.begin(); runnerIt != g_runnerMap.end(); runnerIt++)
    {
      unlockMap = false;
      prematureLoopExit = false;

      ExternalProcess* runner = runnerIt->second;
      if(runner == nullptr)
      {
        continue;
      }

      // Original PID on process spawn
      pid_t childPid = runnerIt->first;

      // Actual PID (-1 if not running)
      pid_t actualPid = runner->IsRunning() ? childPid : -1;

      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::GracefulExit: killing PID " << childPid << (actualPid != -1 ? "*" : ""));

      // Attempt at cleaning up the external process (must acquire lock for Teardown)
      int innerLockOutcome = pthread_mutex_trylock(&g_watchdogTeardownMutex);
      if(lockOutcome == 0)
      {
        // Lock acquired, this thread will carry out cleanup operations
        unlockMap = true;
      }
      else if(lockOutcome == EBUSY)
      {
        // Lock not acquired, Teardown() is already being executed from the runner itself
        unlockMap = true;
        prematureLoopExit = true;
      }
      else
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::GracefulExit: could not acquire lock on Teardown; reason = " << std::strerror(errno));
        return;
      }

      // Runner map must be unlocked for ExternalProcess::Teardown() to remove the runner itself from the map
      if(unlockMap)
      {
        pthread_mutex_unlock(&g_watchdogMapMutex);
      }

      // Runner map loop must be canceled for other threads to perform their cleanup phases
      if(prematureLoopExit)
      {
        break;
      }

      prematureLoopExit = runner->Teardown(actualPid);
      pthread_mutex_unlock(&g_watchdogTeardownMutex);

      // Invalid runner map iterator after Teardown, new loop required
      if(prematureLoopExit)
      {
        break;
      }

      totRunners++;
      if(pidsAlive.length() > 0)
      {
        pidsAlive += ", ";
      }
      pidsAlive += std::to_string(childPid);
      if(actualPid != -1)
      {
        pidsAlive += "*";
        totAlive++;
      }
    }

    // Mutex already released for Teardown()
    if(!unlockMap)
    {
      pthread_mutex_unlock(&g_watchdogMapMutex);
    }
  } while (lockOutcome != 0);

  if(pidsAlive.length() > 0)
  {
    pidsAlive = " (" + pidsAlive + ")";
  }
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::GracefulExit: " << totRunners << " (" << totAlive << "*) alive on exit" << pidsAlive);
  pthread_mutex_unlock(&g_gracefulExitMutex);
}

ExternalProcess::ExternalProcess()
  : Object(),
    m_processRunning(false),
    m_isFullRemote(false),
    m_processPid(-1),
    m_ioCtx(),
    m_sock(nullptr),
    m_acceptor(nullptr),
    m_lastWrite(),
    m_lastRead(),
    m_bufferWrite(),
    m_bufferRead(),
    m_blockingThread((pthread_t)-1),
    m_blockingExitNormal(false),
    m_blockingMutex(PTHREAD_MUTEX_INITIALIZER),
    m_blockingCond(PTHREAD_COND_INITIALIZER)
{
  // Initialize latest timestamps for Write and Read; no need for real time since they are used for intervals only
  if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &m_lastWrite) == -1 || clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &m_lastRead) == -1)
  {
    ExternalProcess::GracefulExit();
    NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::ExternalProcess " << this << ": could not retrieve current CPU time; reason = " << std::strerror(errno));
  }
}

ExternalProcess::~ExternalProcess()
{
}

TypeId
ExternalProcess::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::ExternalProcess")
    .SetParent<Object>()
    .AddConstructor<ExternalProcess>()
    .AddAttribute("TcpRole",
                  "TCP role implemented by this instance.",
                  UintegerValue((uint8_t)TcpRole::SERVER),
                  MakeUintegerAccessor(&ExternalProcess::m_role),
                  MakeUintegerChecker<uint8_t>())
    .AddAttribute("Launcher",
                  "Absolute path to the side process launcher script; if empty, a full-remote external process is expected.",
                  StringValue(""),
                  MakeStringAccessor(&ExternalProcess::m_processLauncher),
                  MakeStringChecker())
    .AddAttribute("CliArgs",
                  "String containing command-line arguments for the launcher script; tokens will be split by whitespace first.",
                  StringValue(""),
                  MakeStringAccessor(&ExternalProcess::m_processArgs),
                  MakeStringChecker())
    .AddAttribute("CrashOnFailure",
                  "Flag indicating whether to raise a fatal exeception if the external process fails.",
                  BooleanValue(true),
                  MakeBooleanAccessor(&ExternalProcess::m_crashOnFailure),
                  MakeBooleanChecker())
    .AddAttribute("WatchdogPeriod",
                  "Time period spent sleeping by the watchdog thread at the beginning of the PID checking loop; lower values will allow detection of process errors quicker, longer values greatly reduce busy waits.",
                  TimeValue(MilliSeconds(100)),
                  MakeTimeAccessor(&ExternalProcess::m_watchdogPeriod),
                  MakeTimeChecker(MilliSeconds(1), Minutes(60)))
    .AddAttribute("GracePeriod",
                  "Time period spent sleeping after killing a process, potentially allowing any temporary data on the process to be stored.",
                  TimeValue(MilliSeconds(100)),
                  MakeTimeAccessor(&ExternalProcess::m_gracePeriod),
                  MakeTimeChecker(MilliSeconds(0)))
    .AddAttribute("Address",
                  "IP address for communicating with external process; this is mandatory for ns-3 in CLIENT role and full-remote process in SERVER role.",
                  StringValue(""),
                  MakeStringAccessor(&ExternalProcess::m_processAddr),
                  MakeStringChecker())
    .AddAttribute("Port",
                  "Port number for communicating with external process; if 0, a free port will be automatically selected by the OS.",
                  UintegerValue(0),
                  MakeUintegerAccessor(&ExternalProcess::m_processPort),
                  MakeUintegerChecker<uint16_t>())
    .AddAttribute("Timeout",
                  "Maximum waiting time for socket operations (e.g. accept); if 0, no timeout is implemented.",
                  TimeValue(MilliSeconds(0)),
                  MakeTimeAccessor(&ExternalProcess::m_sockTimeout),
                  MakeTimeChecker(MilliSeconds(0)))
    .AddAttribute("Attempts",
                  "Maximum attempts for socket operations (e.g. accept); only if a non-zero timeout is specified.",
                  UintegerValue(1),
                  MakeUintegerAccessor(&ExternalProcess::m_sockAttempts),
                  MakeUintegerChecker<uint32_t>(1))
    .AddAttribute("TimedAccept",
                  "Flag indicating whether to apply a timeout on socket accept() / connect(), implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.",
                  BooleanValue(false),
                  MakeBooleanAccessor(&ExternalProcess::m_timedAccept),
                  MakeBooleanChecker())
    .AddAttribute("TimedWrite",
                  "Flag indicating whether to apply a timeout on socket write(), implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.",
                  BooleanValue(false),
                  MakeBooleanAccessor(&ExternalProcess::m_timedWrite),
                  MakeBooleanChecker())
    .AddAttribute("TimedRead",
                  "Flag indicating whether to apply a timeout on socket read_until(), implementing 'Timeout' and 'Attempts' settings; only if a non-zero timeout is specified.",
                  BooleanValue(false),
                  MakeBooleanAccessor(&ExternalProcess::m_timedRead),
                  MakeBooleanChecker())
    .AddAttribute("ThrottleWrites",
                  "Minimum time between a read and a subsequent write; this delay is applied before writing.",
                  TimeValue(MilliSeconds(0)),
                  MakeTimeAccessor(&ExternalProcess::m_throttleWrites),
                  MakeTimeChecker(MilliSeconds(0)))
    .AddAttribute("ThrottleReads",
                  "Minimum time between a write and a subsequent read; this delay is applied before reading.",
                  TimeValue(MilliSeconds(0)),
                  MakeTimeAccessor(&ExternalProcess::m_throttleReads),
                  MakeTimeChecker(MilliSeconds(0)))
  ;

  return tid;
}

bool
ExternalProcess::Create(void)
{
  // Lazy initialization upon actual creation of the process
  if(!IsInitialized())
  {
    Initialize();
  }

  if(m_processRunning)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": process already running (PID: " << m_processPid << ")");
    return false;
  }

  // Only check launcher path for locally-launched processes
  if(!m_isFullRemote)
  {
    struct stat fbuf;
    if(m_processLauncher.length() == 0 || (stat(m_processLauncher.c_str(), &fbuf) != 0))
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": invalid path to process launcher script '" << m_processLauncher << "'");
      return false;
    }
  }

  std::string hostIp = "127.0.0.1";
  const TcpRole ns3Role = (TcpRole)m_role;
  switch(ns3Role)
  {
    case TcpRole::SERVER:
    {
      if(m_processAddr.length() > 0 && m_processAddr != hostIp)
      {
        NS_LOG_WARN(CURRENT_TIME << " ExternalProcess::Create " << this << ": ignoring 'Address' attribute value (" << m_processAddr << ") due to TCP SERVER role configuration; using " << hostIp << " instead");
      }
      break;
    }

    case TcpRole::CLIENT:
    {
      if(m_processAddr.length() == 0)
      {
        if(m_isFullRemote)
        {
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": 'Address' attribute value must be provided due to TCP CLIENT role & full-remote process configuration");
          return false;
        }

        NS_LOG_WARN(CURRENT_TIME << " ExternalProcess::Create " << this << ": unset 'Address' attribute value despite TCP CLIENT role configuration & locally-launched process; using " << hostIp << " instead");
        break;
      }

      hostIp = m_processAddr;
      break;
    }

    default:
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": unexpected 'TcpRole' attribute value (" << +m_role << "); supported configurations: SERVER (" << +TcpRole::SERVER << "), CLIENT (" << +TcpRole::CLIENT << ")");
      return false;
    }
  }

  // Sanity checks for host IP address before anything else
  try
  {
    boost::system::error_code errc;
    boost::asio::ip::address::from_string(hostIp, errc);
    if(errc)
    {
      throw boost::system::system_error(errc);
    }
  } catch (const boost::system::system_error &exc)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": invalid 'Address' attribute value provided; error (" << exc.code() << "): " << exc.what());
    return false;
  }

  try
  {
    boost::system::error_code errc;
    m_sock = new boost::asio::ip::tcp::socket(m_ioCtx);
    if(m_sock == nullptr)
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": could not allocate memory for socket pointer");
      errc.clear();
      errc.assign(boost::system::errc::not_enough_memory, boost::system::system_category());
      throw boost::system::system_error(errc);
    }
  } catch (const boost::system::system_error &exc)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": error during socket creation; error (" << exc.code() << "): " << exc.what());
    if(m_sock)
    {
      delete m_sock;
    }
    return false;
  }

  // Let OS pick a port for this connection, if necessary
  if(m_processPort == 0)
  {
    if(m_isFullRemote)
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": invalid 'Port' attribute value (" << +m_processPort << "); automatic port selection is unsupported for full-remote processes");
      return false;
    }

    try
    {
      boost::system::error_code errc;

      // Briefly open the socket to find out a random free port
      m_sock->open(boost::asio::ip::tcp::v4(), errc);
      if(errc)
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to open socket");
        throw boost::system::system_error(errc);
      }

      // Let OS pick a free port
      auto endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), m_processPort);
      m_sock->bind(endpoint, errc);
      if(errc)
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to bind address");
        throw boost::system::system_error(errc);
      }

      // Retrieve port selected by OS
      m_processPort = m_sock->local_endpoint().port();

      // Close the socket for later connection acceptance
      m_sock->close(errc);
      if(errc)
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to close socket");
        throw boost::system::system_error(errc);
      }
    } catch (const boost::system::system_error &exc)
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": error during automatic port selection; error (" << exc.code() << "): " << exc.what());
      if(m_sock)
      {
        delete m_sock;
      }
      return false;
    }
  }

  // Establish connection to peer either as a server or client
  std::string logMsg = "";
  boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp> endpoints;
  try
  {
    boost::system::error_code errc;
    switch(ns3Role)
    {
      // Prepare acceptor object
      case TcpRole::SERVER:
      {
        logMsg = "error acceptor creation";
        m_acceptor = new boost::asio::ip::tcp::acceptor(m_ioCtx,
                                                        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(),
                                                                                        m_processPort));
        if(m_acceptor == nullptr)
        {
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": could not allocate memory for acceptor pointer");
          errc.clear();
          errc.assign(boost::system::errc::not_enough_memory, boost::system::system_category());
          throw boost::system::system_error(errc);
        }

        logMsg = "TCP server socket will accept connections to port ";
        logMsg += std::to_string(+m_processPort);
        break;
      }

      // Check attribute values & resolve IP:PORT to connection endpoints
      case TcpRole::CLIENT:
      {
        logMsg = "resolving endpoints for ";
        logMsg += hostIp + ":" + std::to_string(+m_processPort);
        boost::asio::ip::tcp::resolver ipResolver(m_ioCtx);
        endpoints = ipResolver.resolve(hostIp, std::to_string(m_processPort));
        if(endpoints.empty())
        {
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": could not resolve endpoints for connection to " << m_processAddr << ":" << +m_processPort);
          errc.clear();
          errc.assign(boost::system::errc::invalid_argument , boost::system::system_category());
          throw boost::system::system_error(errc);
        }

        logMsg = "TCP client socket will try to connect to ";
        logMsg += hostIp + ":" + std::to_string(+m_processPort) + " (num. endpoints: " + std::to_string(endpoints.size()) + ")";
        break;
      }
    }
  } catch (const boost::system::system_error &exc)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": " << logMsg << "; error (" << exc.code() << "): " << exc.what());
    if(m_sock)
    {
      delete m_sock;
    }
    if(m_acceptor)
    {
      delete m_acceptor;
    }
    return false;
  }

  // Success (partial)
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": " << logMsg);

  // Full-remote processes do not need CLI arguments
  if(m_isFullRemote)
  {
    if(m_processArgs.length() > 0)
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": full-remote process detected; ignoring CLI arguments '" << m_processArgs << "'");
    }

    // Previous checks succeeded so far and no local process needs to be checked
    m_processRunning = true;
  }

  // Replace program in child process & pass TCP port plus CLI arguments
  else
  {
    std::vector<char*> argv;
    argv.push_back((char*)m_processLauncher.c_str());

    std::string portAsStr = std::to_string(m_processPort);
    argv.push_back((char*)portAsStr.c_str());

    uint32_t argvOffset = argv.size();
    if(m_processArgs.length() > 0)
    {
      std::istringstream iss(m_processArgs);
      std::string token = "\0";
      while(std::getline(iss, token, ' '))
      {
        size_t len = token.length() + 1;
        char* tmp = new char[len];
        if(tmp == nullptr)
        {
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to allocate memory for CLI argument '" << token << "'");
          if(m_sock)
          {
            delete m_sock;
          }
          if(m_acceptor)
          {
            delete m_acceptor;
          }
          return false;
        }
        std::memset(tmp, '\0', len);
        std::memcpy(tmp, token.c_str(), len);
        argv.push_back(tmp);
      }
    }
    argv.push_back(NULL);

    // Ensure correct parsing and improve logging msg
    std::string argsCheck = "";
    for(auto argsIt = argv.begin(); argsIt != argv.end(); argsIt++)
    {
      if(*argsIt == NULL)
      {
        continue;
      }
      if(argsCheck.length() > 0)
      {
        argsCheck += " ";
      }
      argsCheck += std::string(*argsIt);
    }

    m_processPid = fork();
    switch(m_processPid)
    {
      case -1:
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to create side process");
        m_processRunning = false;
        break;
      }

      case 0:
      {
        int outcome = execvp(argv[0], argv.data());
        if(outcome != 0)
        {
          // Free up allocated memory for string args (child process, before terminating ns-3)
          uint32_t argsIx = 0;
          for(auto argsIt = argv.begin(); argsIt != argv.end(); argsIt++, argsIx++)
          {
            if(*argsIt == NULL || argsIx < argvOffset)
            {
              continue;
            }
            delete[] *argsIt;
          }
          argv.clear();
          ExternalProcess::GracefulExit();
          NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to create side process (exec)");
        }
      }

      default:
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": side process PID = " << m_processPid << "; launcher args: '" << argsCheck << "'");
        m_processRunning = true;
        break;
      }
    }

    // Free up allocated memory for string args (parent process, in normal executions)
    uint32_t argsIx = 0;
    for(auto argsIt = argv.begin(); argsIt != argv.end(); argsIt++, argsIx++)
    {
      if(*argsIt == NULL || argsIx < argvOffset)
      {
        continue;
      }
      delete[] *argsIt;
    }
    argv.clear();

    // Add current instance to the runner map, associating it to the spawned process's PID
    pthread_mutex_lock(&g_watchdogMapMutex);
    bool overwritingPid = false;
    auto runnerIt = g_runnerMap.find(m_processPid);
    if(runnerIt != g_runnerMap.end() && runnerIt->second != nullptr)
    {
      overwritingPid = true;
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": overwriting runner for PID " << m_processPid);
    }
    if(!overwritingPid)
    {
      g_runnerMap[m_processPid] = this;
    }
    pthread_mutex_unlock(&g_watchdogMapMutex);
    if(overwritingPid)
    {
      ExternalProcess::GracefulExit();
      return m_processRunning;
    }

    if(!m_processRunning)
    {
      if(m_sock)
      {
        delete m_sock;
      }
      if(m_acceptor)
      {
        delete m_acceptor;
      }
      return m_processRunning;
    }
  }

  // Determine a successul connection to the other peer, depending on ns-3 TCP roles:
  // - SERVER: accepting a single connection
  // - CLIENT: connecting to server
  bool connected = false;

  // Set timeout, if specified
  if(m_timedAccept && m_sockTimeout.IsZero())
  {
    NS_LOG_WARN(CURRENT_TIME << " ExternalProcess::Create " << this << ": ignoring 'TimedAccept' attribute value (" << m_timedAccept << ") since 'Timeout' is set to 0");
  }
  bool useTimeout = m_timedAccept && m_sockTimeout.IsStrictlyPositive();
  if(useTimeout)
  {
    // Accept a connection from client / connect to server through multiple attempts, if necessary
    uint32_t attempt = 0;
    do
    {
      try
      {
        // Initialize thread execution outcome to fatal error
        int threadOutcome = EP_THREAD_OUTCOME::FATAL_ERROR;
        boost::system::error_code errc;

        // Avoid new attempts in case the watchdog thread cleans this process
        if(!m_processRunning || m_sock == nullptr || (m_role == TcpRole::SERVER && m_acceptor == nullptr))
        {
          break;
        }

        attempt++;
        bool fatalError = false;
        switch(ns3Role)
        {
          case TcpRole::SERVER:
          {
            // Run ASIO's blocking accept in a separate thread
            logMsg = "socket accept";
            AcceptorData ad{&threadOutcome, m_acceptor, m_sock, &errc};
            connected = TimedSocketOperation(AcceptorFunction, &ad, fatalError);
            break;
          }

          case TcpRole::CLIENT:
          {
            // Run ASIO's blocking connect in a separate thread
            logMsg = "socket connection";
            ConnectorData cd{&threadOutcome, m_sock, &endpoints, &errc};
            connected = TimedSocketOperation(ConnectorFunction, &cd, fatalError);
            break;
          }
        }

        if(connected)
        {
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": successful " << logMsg << " in attempt " << attempt << "/" << m_sockAttempts);
        }
        else
        {
          // Fatal error occurred, not just timeout
          if(fatalError)
          {
            ExternalProcess::GracefulExit();
            NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::Create " << this << ": fatal error during " << logMsg << "; reason = " << std::strerror(errno));
            break;
          }

          // Regular timeout
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": " << logMsg << " timed out in attempt " << attempt << "/" << m_sockAttempts);
          if(m_sock)
          {
            m_sock->cancel();
          }
          if(errc)
          {
            throw boost::system::system_error(errc);
          }
        }
      } catch (const boost::system::system_error &exc)
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": error during socket creation or " << logMsg << " (host:port = " << hostIp << ":" << +m_processPort << "); error (" << exc.code() << "): " << exc.what());
      }
    } while(!connected && attempt < m_sockAttempts);
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": attempts loop exited; connected: " << connected);
  }

  // Timeout not specified, block until connection request arrives from the external process (also indefinitely)
  else
  {
    if(m_sockAttempts > 1)
    {
      NS_LOG_WARN(CURRENT_TIME << " ExternalProcess::Create " << this << ": ignoring 'Attempts' attribute value (" << m_sockAttempts << ") since 'TimedAccept' is not enabled (" << m_timedAccept << ") and/or 'Timeout' is set to 0 (" << m_sockTimeout.As(Time::S) << ")");
    }

    try
    {
      // Initialize thread execution outcome to fatal error
      int threadOutcome = EP_THREAD_OUTCOME::FATAL_ERROR;
      boost::system::error_code errc;

      // Prepare arguments for blocking-mode execution of an acceptor thread
      BlockingArgs ba{&m_blockingThread, &m_blockingExitNormal, &m_blockingMutex, &m_blockingCond};
      bool fatalError = false;

      switch(ns3Role)
      {
        case TcpRole::SERVER:
        {
          logMsg = "socket accept";
          AcceptorData ad{&threadOutcome, m_acceptor, m_sock, &errc, &ba};
          connected = BlockingSocketOperation(AcceptorFunction, &ad, fatalError);
          break;
        }

        case TcpRole::CLIENT:
        {
          logMsg = "socket connection";
          ConnectorData cd{&threadOutcome, m_sock, &endpoints, &errc, &ba};
          connected = BlockingSocketOperation(ConnectorFunction, &cd, fatalError);
          break;
        }
      }

      // Upon reaching this point, the blocking call has returned
      if(errc)
      {
        throw boost::system::system_error(errc);
      }

      // Throw not_connected in case no other error shows up before
      if(!connected)
      {
        errc.clear();
        errc.assign(boost::system::errc::not_connected, boost::system::system_category());
        throw boost::system::system_error(errc);
      }
    } catch (const boost::system::system_error &exc)
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": error during socket creation or " << logMsg << " (host:port = " << hostIp << ":" << +m_processPort << "); error (" << exc.code() << "): " << exc.what());
    }
  }

  // No use in a side process we have no means of communication with
  if(!connected)
  {
    Teardown();
    return m_processRunning;
  }

  return m_processRunning;
}

bool
ExternalProcess::IsRunning(void) const
{
  return m_processRunning;
}

pid_t
ExternalProcess::GetPid(void) const
{
  return m_processPid;
}

bool
ExternalProcess::Teardown(void)
{
  // Attempt at acquiring the lock for this operation
  int lockOutcome = -1;
  do
  {
    lockOutcome = pthread_mutex_trylock(&g_watchdogTeardownMutex);
    if(lockOutcome == 0)
    {
      // Lock acquired, this thread will carry out cleanup operations
    }
    else if(lockOutcome == EBUSY)
    {
      // Lock not acquired, try again next iteration
      struct timespec sleepDelay;
      sleepDelay.tv_sec = m_watchdogPeriod.GetNanoSeconds() / (int64_t)1000000000UL;
      sleepDelay.tv_nsec = m_watchdogPeriod.GetNanoSeconds() % (int64_t)1000000000UL;
      nanosleep(&sleepDelay, nullptr);
      continue;
    }
    else
    {
      ExternalProcess::GracefulExit();
      NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::Teardown " << this << ": could not acquire lock on Teardown; reason = " << std::strerror(errno));
    }
  } while (lockOutcome != 0);

  bool erased = Teardown(GetPid());
  pthread_mutex_unlock(&g_watchdogTeardownMutex);
  return erased;
}

bool
ExternalProcess::Teardown(pid_t childPid)
{
  return DoTeardown(childPid, true);
}

bool
ExternalProcess::Write(const std::string &str, bool first, bool flush, bool last)
{
  bool outcome = false;
  if(!m_processRunning)
  {
    return outcome;
  }

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Write " << this << ": str = '" << str << "', first = " << first << ", flush = " << flush << ", last = " << last);

  // Store in write buffer until last token is provided
  m_bufferWrite.push_back(std::make_pair(str, (flush || last)));

  // Actually write through socket
  if(last)
  {
    outcome = DoWrite();
  }
  else
  {
    outcome = true;
  }

  return outcome;
}

bool
ExternalProcess::Read(std::string &str, bool &hasNext)
{
  bool outcome = false;
  if(!m_processRunning)
  {
    return outcome;
  }

  // Retrieve new strings, if necessary
  if(m_bufferRead.empty())
  {
    // Apply throttling, if set
    ThrottleOperation(true);

    // Actually read from external process
    outcome = DoRead();
    if(!outcome)
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Read " << this << ": empty read buffer and Read() failed");
    }
  }

  // Consume read buffer from the head
  outcome = false;
  if(m_bufferRead.empty())
  {
    str = "";
    hasNext = false;
  }
  else
  {
    outcome = true;
    str = *m_bufferRead.begin();
    m_bufferRead.pop_front();
    hasNext = !m_bufferRead.empty();
  }

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Read " << this << ": str = '" << str << "', hasNext = " << hasNext);
  return outcome;
}

void
ExternalProcess::DoInitialize(void)
{
  Object::DoInitialize();

  // Do not create a watchdog thread or update instance counter for full-remote processes
  m_isFullRemote = m_processLauncher == "";
  if(m_isFullRemote)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoInitialize " << this << ": full-remote process detected; watchdog thread NOT created");
    return;
  }

  // Update instance counter
  m_counter++;

  // Only create the watchdog thread once
  if(m_counter > 0 && !m_watchdogInit)
  {
    // Initialize watchdog arguments
    if(!g_watchdogArgs.m_initialized)
    {
      g_watchdogArgs.m_args = new WatchdogData();
      if(g_watchdogArgs.m_args == nullptr)
      {
        ExternalProcess::GracefulExit();
        NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::DoInitialize " << this << ": could not allocate memory for watchdog args");
      }

      // Watchdog copies settings from the first ExternalProcess instance
      g_watchdogArgs.m_args->m_crashOnFailure = m_crashOnFailure;
      g_watchdogArgs.m_args->m_period = m_watchdogPeriod;
      g_watchdogArgs.m_initialized = true;
    }

    // No need for mutex, watchdog thread is not running yet
    g_watchdogExit = false;
    if(pthread_create(&g_watchdog, NULL, WatchdogFunction, g_watchdogArgs.m_args) == -1)
    {
      ExternalProcess::GracefulExit();
      NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::DoInitialize " << this << ": could not create watchdog thread; reason = " << std::strerror(errno));
    }
    m_watchdogInit = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoInitialize " << this << ": watchdog thread created");
  }
}

void
ExternalProcess::DoDispose(void)
{
  // No need to cleanup if no initialization has occurred
  if(!IsInitialized())
  {
    return;
  }

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoDispose " << this);
  Teardown();

  // Cleanup blocking thread variables
  pthread_cond_destroy(&m_blockingCond);
  pthread_mutex_destroy(&m_blockingMutex);

  // Only act on watchdog thread or update instance counter for locally-launched processes
  if(!m_isFullRemote)
  {
    // Update instance counter
    m_counter--;

    // Wait for watchdog thread to terminate if this was the last instance
    if(m_counter == 0)
    {
      pthread_mutex_lock(&g_watchdogExitMutex);
      g_watchdogExit = true;
      pthread_mutex_unlock(&g_watchdogExitMutex);

      // Clear the watchdog process and its arguments
      if(pthread_join(g_watchdog, NULL) == -1)
      {
        ExternalProcess::GracefulExit();
        NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::DoDispose " << this << ": could not join watchdog thread; reason = " << std::strerror(errno));
      }
      if(g_watchdogArgs.m_initialized)
      {
        delete g_watchdogArgs.m_args;
        g_watchdogArgs.m_initialized = false;
      }

      m_watchdogInit = false;
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoDispose " << this << ": watchdog thread join completed");
    }
  }

  Object::DoDispose();
}

bool
ExternalProcess::DoTeardown(pid_t childPid, bool eraseRunner)
{
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": PID = " << childPid << ", eraseRunner = " << eraseRunner << ", full-remote = " << m_isFullRemote);
  bool erased = false;

  // Delete blocking-mode operation thread, if any
  pthread_mutex_lock(&m_blockingMutex);
  if(m_blockingThread != (pthread_t)-1)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": canceling blocking thread");
    if(pthread_cancel(m_blockingThread) != 0)
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": could not cancel blocking thread; reason = " << std::strerror(errno));
    }

    // Unset thread ID: indicates thread is about to terminate
    m_blockingThread = (pthread_t)-1;
    m_blockingExitNormal = false;
    pthread_cond_broadcast(&m_blockingCond);
  }
  pthread_mutex_unlock(&m_blockingMutex);

  // Try acquiring the lock on the runner map repeatedly
  int lockOutcome = -1;
  do
  {
    lockOutcome = pthread_mutex_trylock(&g_watchdogMapMutex);
    if(lockOutcome == 0)
    {
      // Lock acquired, continue with cleanup operations
    }
    else if(lockOutcome == EBUSY)
    {
      // Lock not acquired, try again next iteration
      struct timespec sleepDelay;
      sleepDelay.tv_sec = m_watchdogPeriod.GetNanoSeconds() / (int64_t)1000000000UL;
      sleepDelay.tv_nsec = m_watchdogPeriod.GetNanoSeconds() % (int64_t)1000000000UL;
      nanosleep(&sleepDelay, nullptr);
      continue;
    }
    else
    {
      NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": could not acquire lock on runner map; reason = " << std::strerror(errno));
      return erased;
    }

    if(m_processRunning)
    {
      if(childPid != -1 || m_isFullRemote)
      {
        // Send graceful kill message to process
        Write(MSG_KILL, true, true, true);

        // Wait a bit for the process to save temporary data, if any
        if(m_gracePeriod.IsStrictlyPositive())
        {
          struct timespec sleepDelay;
          sleepDelay.tv_sec = m_gracePeriod.GetNanoSeconds() / (int64_t)1000000000UL;
          sleepDelay.tv_nsec = m_gracePeriod.GetNanoSeconds() % (int64_t)1000000000UL;
          nanosleep(&sleepDelay, nullptr);
        }
      }

      // Close socket
      if(m_sock)
      {
        try
        {
          boost::system::error_code errc;
          if(m_sock->is_open())
          {
            m_sock->cancel(errc);
            if(errc)
            {
              throw boost::system::system_error(errc);
            }

            m_sock->shutdown(m_sock->shutdown_both, errc);
            if(errc)
            {
              throw boost::system::system_error(errc);
            }

            m_sock->close(errc);
            if(errc)
            {
              throw boost::system::system_error(errc);
            }
          }

          if(m_acceptor)
          {
            m_acceptor->cancel();
            delete m_acceptor;
          }

          delete m_sock;
        } catch (const boost::system::system_error &exc)
        {
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": error during socket disposal; error (" << exc.code() << "): " << exc.what());
          if(m_sock)
          {
            delete m_sock;
          }
          if(m_acceptor)
          {
            delete m_acceptor;
          }
        }
      }

      // Check if the process outlived the graceful MSG_KILL
      if(!m_isFullRemote)
      {
        if(childPid != -1 && childPid == m_processPid)
        {
          pid_t retPid = -1;
          int status = -1;
          if((retPid = waitpid(childPid, &status, WNOHANG)) < 0)
          {
            NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": error on checking PID " << childPid << " status; reason = " << std::strerror(errno));
            pthread_mutex_unlock(&g_watchdogMapMutex);
            return erased;
          }
          else if(retPid == 0)
          {
            // Process is still running, will send SIGKILL later
          }
          else
          {
            // Prevent sending SIGKILL to an already terminated child process (PID = -1)
            childPid = -1;
          }
        }

        // Send SIGKILL to side process (Ctrl+C)
        if(childPid != -1 && childPid == m_processPid)
        {
          int killOutcome = kill(childPid, SIGKILL);
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": sending SIGKILL to PID " << childPid << " (outcome: " << (killOutcome == 0 ? "OK" : std::strerror(errno)) << ")");

          killOutcome = waitpid(childPid, NULL, 0);
          if(killOutcome < 0)
          {
            NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": error on waiting PID " << childPid << " (outcome: " << std::strerror(errno) << ")");
          }
          else
          {
            NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": successfully killed PID " << childPid);
          }
        }

        // Invalidate runner from the map (prevents watchdog from checking it)
        auto runnerIt = g_runnerMap.find(m_processPid);
        if(runnerIt != g_runnerMap.end())
        {
          g_runnerMap[runnerIt->first] = nullptr;

          // Only update runner map if explicitly stated
          if(eraseRunner)
          {
            g_runnerMap.erase(runnerIt);
            erased = true;
          }
        }
      }

      m_processRunning = false;
      m_processPid = -1;
    }
  } while (lockOutcome != 0);
  pthread_mutex_unlock(&g_watchdogMapMutex);

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoTeardown " << this << ": completed on PID = " << childPid << ", eraseRunner = " << eraseRunner << ", erased = " << erased << ", full-remote = " << m_isFullRemote);
  return erased;
}

bool
ExternalProcess::DoWrite(void)
{
  bool outcome = true;

  // Apply throttling, if set
  ThrottleOperation();

  // Concatenate into single string until flush flag is set
  std::string tmpData = "";

  // Write all buffered strings through the socket
  for(auto dataIt = m_bufferWrite.begin(); dataIt != m_bufferWrite.end(); dataIt++)
  {
    if(tmpData.length() > 0)
    {
      tmpData += MSG_DELIM;
    }
    tmpData += dataIt->first;

    // Flush flag is ON or last string
    if(dataIt->second || std::next(dataIt) == m_bufferWrite.end())
    {
      bool writeCompleted = false;
      tmpData += MSG_EOL;

      // Initialize thread execution outcome to fatal error
      int threadOutcome = EP_THREAD_OUTCOME::FATAL_ERROR;
      boost::system::error_code errc;
      boost::asio::mutable_buffer buf = boost::asio::buffer(tmpData);
      size_t len = buf.size();

        // Set timeout, if specified
        if(m_timedWrite && m_sockTimeout.IsZero())
        {
          NS_LOG_WARN(CURRENT_TIME << " ExternalProcess::DoWrite " << this << ": ignoring 'TimedWrite' attribute value (" << m_timedWrite << ") since 'Timeout' is set to 0");
        }
        bool useTimeout = m_timedWrite && m_sockTimeout.IsStrictlyPositive();
        if(useTimeout)
        {
          // Write to client through multiple attempts, if necessary
          uint32_t attempt = 0;
          do
          {
            try
            {
              // Avoid new attempts in case the watchdog thread cleans this process
              if(!m_processRunning || m_sock == nullptr)
              {
                break;
              }
              attempt++;
              threadOutcome = EP_THREAD_OUTCOME::FATAL_ERROR;
              errc.clear();

              // Run ASIO's blocking write in a separate thread
              WriterData wd{&threadOutcome, m_sock, &buf, &errc};
              bool fatalError = false;
              writeCompleted = TimedSocketOperation(WriterFunction, &wd, fatalError);
              if(writeCompleted)
              {
                NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoWrite " << this << ": socket write completed in attempt " << attempt << "/" << m_sockAttempts);
              }
              else
              {
                // Fatal error occurred, not just timeout
                if(fatalError)
                {
                  ExternalProcess::GracefulExit();
                  NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::DoWrite " << this << ": fatal error during socket write; reason = " << std::strerror(errno));
                  break;
                }

                // Regular timeout
                NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoWrite " << this << ": socket write timed out in attempt " << attempt << "/" << m_sockAttempts);
                if(m_sock)
                {
                  m_sock->cancel();
                }
                if(errc)
                {
                  throw boost::system::system_error(errc);
                }
              }
            } catch (const boost::system::system_error &exc)
            {
              NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoWrite " << this << ": error during socket write; error (" << exc.code() << "): " << exc.what());
            }
          } while(!writeCompleted && attempt < m_sockAttempts);
        }

        // Timeout not specified, block until data is written to the external process (also indefinitely)
        else
        {
          if(m_sockAttempts > 1)
          {
            NS_LOG_WARN(CURRENT_TIME << " ExternalProcess::DoWrite " << this << ": ignoring 'Attempts' attribute value (" << m_sockAttempts << ") since 'TimedWrite' is not enabled (" << m_timedWrite << ") and/or 'Timeout' is set to 0 (" << m_sockTimeout.As(Time::S) << ")");
          }

          try
          {
            // Prepare arguments for blocking-mode execution of a writer thread
            BlockingArgs ba{&m_blockingThread, &m_blockingExitNormal, &m_blockingMutex, &m_blockingCond};
            WriterData wd{&threadOutcome, m_sock, &buf, &errc, &ba};
            bool fatalError = false;
            writeCompleted = BlockingSocketOperation(WriterFunction, &wd, fatalError);

            // Upon reaching this point, the blocking call has returned
            if(errc)
            {
              throw boost::system::system_error(errc);
            }
          } catch (const boost::system::system_error &exc)
          {
            NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoWrite " << this << ": error during socket write; error (" << exc.code() << "): " << exc.what());
          }
        }

        // Check buffer for write outcome (works in both a/sync modes)
        size_t residualLen = len - buf.size();
        // Boost's write consumes the buffer, thus all data is written when there are no residual bytes
        len -= residualLen;
        writeCompleted = len == tmpData.length();
        outcome = outcome && writeCompleted;

      if(!writeCompleted)
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoWrite " << this << ": only " << len << " B written out of " << tmpData.length() << " B; error (" << errc.value() << "): " << boost::system::system_error(errc).what());
      }
      tmpData = "";
    }
  }

  // Clear write buffer
  m_bufferWrite.clear();
  return outcome;
}

bool
ExternalProcess::DoRead(void)
{
  bool outcome = false;
  if(!m_processRunning)
  {
    return outcome;
  }

  // Apply throttling, if set
  ThrottleOperation(true);

  bool readCompleted = false;
  size_t len = 0;
  boost::asio::streambuf buf;
  try
  {
    // Initialize thread execution outcome to fatal error
    int threadOutcome = EP_THREAD_OUTCOME::FATAL_ERROR;
    boost::system::error_code errc;

    // Set timeout, if specified
    if(m_timedRead && m_sockTimeout.IsZero())
    {
      NS_LOG_WARN(CURRENT_TIME << " ExternalProcess::DoRead " << this << ": ignoring 'TimedRead' attribute value (" << m_timedRead << ") since 'Timeout' is set to 0");
    }
    bool useTimeout = m_timedRead && m_sockTimeout.IsStrictlyPositive();
    if(useTimeout)
    {
      // Read from client through multiple attempts, if necessary
      uint32_t attempt = 0;
      do
      {
        // Avoid new attempts in case the watchdog thread cleans this process
        if(!m_processRunning || m_sock == nullptr)
        {
          break;
        }
        attempt++;
        threadOutcome = EP_THREAD_OUTCOME::FATAL_ERROR;
        errc.clear();

        try
        {
          // Run ASIO's blocking read_until in a separate thread
          ReaderData rd{&threadOutcome, m_sock, &buf, &errc};
          bool fatalError = false;
          readCompleted = TimedSocketOperation(ReaderFunction, &rd, fatalError);
          if(readCompleted)
          {
            NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoRead " << this << ": socket read completed in attempt " << attempt << "/" << m_sockAttempts);
          }
          else
          {
            // Fatal error occurred, not just timeout
            if(fatalError)
            {
              ExternalProcess::GracefulExit();
              NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::DoRead " << this << ": fatal error during socket read; reason = " << std::strerror(errno));
              break;
            }

            // Regular timeout
            NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoRead " << this << ": socket read timed out in attempt " << attempt << "/" << m_sockAttempts);
            if(m_sock)
            {
              m_sock->cancel();
            }
            if(errc)
            {
              throw boost::system::system_error(errc);
            }
          }
        } catch (const boost::system::system_error &exc)
        {
          NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoRead " << this << ": error during socket read; error (" << exc.code() << "): " << exc.what());
        }
      } while(!readCompleted && attempt < m_sockAttempts);
    }

    // Timeout not specified, block until data arrives from the external process (also indefinitely)
    else
    {
      if(m_sockAttempts > 1)
      {
        NS_LOG_WARN(CURRENT_TIME << " ExternalProcess::DoRead " << this << ": ignoring 'Attempts' attribute value (" << m_sockAttempts << ") since 'TimedRead' is not enabled (" << m_timedRead << ") and/or 'Timeout' is set to 0 (" << m_sockTimeout.As(Time::S) << ")");
      }

      try
      {
        // Prepare arguments for blocking-mode execution of a reader thread
        BlockingArgs ba{&m_blockingThread, &m_blockingExitNormal, &m_blockingMutex, &m_blockingCond};
        ReaderData rd{&threadOutcome, m_sock, &buf, &errc, &ba};
        bool fatalError = false;
        readCompleted = BlockingSocketOperation(ReaderFunction, &rd, fatalError);

        // Upon reaching this point, the blocking call has returned
        if(errc)
        {
          throw boost::system::system_error(errc);
        }
      } catch (const boost::system::system_error &exc)
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoRead " << this << ": error during socket read; error (" << exc.code() << "): " << exc.what());
      }
    }

    // Check buffer for read outcome (works in both a/sync modes)
    len = buf.size();
    readCompleted = len > 0;

    // Throw no_message_available in case no other error shows up before
    if(!readCompleted)
    {
      errc.clear();
      errc.assign(boost::system::errc::no_message_available, boost::system::system_category());
      throw boost::system::system_error(errc);
    }

    // Check buffer for read outcome (works in both a/sync modes)
    len = buf.size();
    readCompleted = len > 0;

    // Throw no_message_available in case no other error shows up before
    if(!readCompleted)
    {
      errc.clear();
      errc.assign(boost::system::errc::no_message_available, boost::system::system_category());
      throw boost::system::system_error(errc);
    }
  } catch (const boost::system::system_error &exc)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoRead " << this << ": error during socket read; error (" << exc.code() << "): " << exc.what());
  }

  // Split into separate tokens & place them into the read buffer for later consumption
  size_t newStrings = m_bufferRead.size();
  if(len > 0)
  {
    std::string innerDelim = MSG_DELIM;
    std::istream iss(&buf);
    std::string token = "\0";
    while(std::getline(iss, token, MSG_EOL))
    {
      size_t delimIx = 0;
      std::string innerToken = "\0";
      while((delimIx = token.find(innerDelim)) != std::string::npos)
      {
        innerToken = token.substr(0, delimIx);
        token.erase(0, delimIx + innerDelim.length());

        // Append to read buffer
        if(innerToken.length() > 0)
        {
          m_bufferRead.push_back(innerToken);
        }
      }
      if(token.length() > 0)
      {
        m_bufferRead.push_back(token);
      }
    }
    outcome = true;
  }
  newStrings = m_bufferRead.size() - newStrings;
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::DoRead " << this << ": " << +len << " B read from socket, corresponding to " << +newStrings << " new strings");

  return outcome;
}

void
ExternalProcess::ThrottleOperation(bool isRead)
{
  if(m_throttleWrites.IsZero() && m_throttleReads.IsZero())
  {
    // No throttling is set up
    return;
  }

  // Retrieve current time
  struct timespec currTime;
  if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &currTime) == -1)
  {
    ExternalProcess::GracefulExit();
    NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::ThrottleOperation " << this << ": could not retrieve current CPU time; reason = " << std::strerror(errno));
  }

  // Compute interval between operations
  int64_t intSecs = 0;
  int64_t intNsecs = 0;
  Time target = MilliSeconds(0);
  if(isRead)
  {
    target = m_throttleReads;
    intSecs = (int64_t)(currTime.tv_sec - m_lastWrite.tv_sec);
    intNsecs = (int64_t)(currTime.tv_nsec - m_lastWrite.tv_nsec);
    m_lastRead = currTime;
  }
  else
  {
    target = m_throttleWrites;
    intSecs = (int64_t)(currTime.tv_sec - m_lastRead.tv_sec);
    intNsecs = (int64_t)(currTime.tv_nsec - m_lastRead.tv_nsec);
    m_lastWrite = currTime;
  }

  // Check seconds first
  bool avoidThrottling = intSecs > target.GetSeconds();
  if(avoidThrottling)
  {
    return;
  }

  // Check at nanoseconds granularity otherwise
  avoidThrottling = intNsecs > target.GetNanoSeconds();
  if(avoidThrottling)
  {
    return;
  }

  // Enforce throttling at nanoseconds granularity in both cases
  struct timespec sleepDelay;
  sleepDelay.tv_sec = (target.GetNanoSeconds() / (int64_t)1000000000UL) - intSecs;
  sleepDelay.tv_nsec = (target.GetNanoSeconds() % (int64_t)1000000000UL) - intNsecs;
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::ThrottleOperation " << this << ": throttling " << (isRead ? "Read" : "Write") << " operation for " << sleepDelay.tv_sec << "s"<< sleepDelay.tv_nsec << "ns (target: " << target.As(Time::MS) << ")");
  nanosleep(&sleepDelay, nullptr);
}

bool
ExternalProcess::TimedSocketOperation(void* (pthreadFn(void*)), void* pthreadArg, bool &fatalFailure)
{
  bool outcome = false;
  fatalFailure = false;

  pthread_t separateThread;
  if(pthread_create(&separateThread, NULL, *pthreadFn, pthreadArg) == -1)
  {
    fatalFailure = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": could not create separate thread; reason = " << std::strerror(errno));
    return outcome;
  }

  // Retrieve current time
  struct timespec currTime;
  if(clock_gettime(CLOCK_REALTIME, &currTime) == -1)
  {
    fatalFailure = true;
    NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": could not retrieve current CPU time; reason = " << std::strerror(errno));
    return outcome;
  }

  // Allow thread to join before timeout, otherwise cancel it
  struct timespec threadTimeout = currTime;
  threadTimeout.tv_sec = (m_sockTimeout.GetNanoSeconds() / (int64_t)1000000000UL);
  threadTimeout.tv_nsec = (m_sockTimeout.GetNanoSeconds() % (int64_t)1000000000UL);
  nanosleep(&threadTimeout, nullptr);

  // Upon reaching this point, a blocking join indicates the thread is still running thus a timeout must be reported
  int joinStatus = pthread_tryjoin_np(separateThread, NULL);

  // Retrieve thread outcome from the first field in its function argument
  int** threadOutcomePtr = new int*;
  if(threadOutcomePtr == nullptr)
  {
    fatalFailure = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": could not allocate memory for thread outcome retrieval");
    return outcome;
  }
  std::memcpy(threadOutcomePtr, pthreadArg, sizeof(int*));
  int* threadOutcome = *threadOutcomePtr;
  if(threadOutcome == nullptr)
  {
    fatalFailure = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": could not copy a valid pointer to thread outcome");
    delete threadOutcomePtr;
    return outcome;
  }

  // Avoid any status/outcome parsing in case of fatal errors
  if(*threadOutcome == EP_THREAD_OUTCOME::FATAL_ERROR)
  {
    fatalFailure = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": fatal error encountered by separate thread");
    delete threadOutcomePtr;
    return outcome;
  }

  // Parse thread outcome
  switch(joinStatus)
  {
    // Thread terminated before timeout, check outcome
    case 0:
    {
      if(*threadOutcome == EP_THREAD_OUTCOME::SUCCESS)
      {
        outcome = true;
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": operation successful within timeout");
      }
      else
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": operation failed within timeout");
      }
      break;
    }

    // Thread timed out, cancel it
    case EBUSY:
    {
      if(pthread_cancel(separateThread) != 0)
      {
        fatalFailure = true;
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": could not cancel separate thread; reason = " << std::strerror(errno));
      }

      // Discard any return value from the thread, it would be a failure outcome anyway
      if(pthread_join(separateThread, NULL) == -1)
      {
        fatalFailure = true;
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": could not join separate thread; reason = " << std::strerror(errno));
      }

      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": operation timed out");
      break;
    }

    default:
    {
      fatalFailure = true;
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::TimedSocketOperation " << this << ": could not tryjoin separate thread; reason = " << std::strerror(errno));
      break;
    }
  }

  delete threadOutcomePtr;
  return outcome;
}

bool
ExternalProcess::BlockingSocketOperation(void* (pthreadFn(void*)), void* pthreadArg, bool &fatalFailure)
{
  bool outcome = false;
  fatalFailure = false;

  // Prevent overwriting an ongoing blocking thread already (should never happen)
  pthread_mutex_lock(&m_blockingMutex);
  bool alreadyBlockingThread = m_blockingThread != (pthread_t)-1;
  pthread_mutex_unlock(&m_blockingMutex);
  if(alreadyBlockingThread)
  {
    fatalFailure = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::BlockingSocketOperation " << this << ": could not create separate thread; another blocking operation is already ongoing");
    return outcome;
  }

  pthread_t separateThread;
  if(pthread_create(&separateThread, NULL, *pthreadFn, pthreadArg) == -1)
  {
    fatalFailure = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::BlockingSocketOperation " << this << ": could not create separate thread; reason = " << std::strerror(errno));
    return outcome;
  }

  // Store separate thread's ID
  pthread_mutex_lock(&m_blockingMutex);
  m_blockingThread = separateThread;
  m_blockingExitNormal = false;
  pthread_cond_broadcast(&m_blockingCond);
  pthread_mutex_unlock(&m_blockingMutex);

  // Prevent deferencing an uninitialized pointer: wait until the thread is still running
  pthread_mutex_lock(&m_blockingMutex);
  while(m_blockingThread != (pthread_t)-1)
  {
    pthread_cond_wait(&m_blockingCond, &m_blockingMutex);
  }
  pthread_mutex_unlock(&m_blockingMutex);

  // Upon reaching this point, it is safe to join the thread
  int joinStatus = pthread_join(separateThread, NULL);

  // Retrieve thread outcome from the first field in its function argument
  int** threadOutcomePtr = new int*;
  if(threadOutcomePtr == nullptr)
  {
    fatalFailure = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::BlockingSocketOperation " << this << ": could not allocate memory for thread outcome retrieval");
    return outcome;
  }
  std::memcpy(threadOutcomePtr, pthreadArg, sizeof(int*));
  int* threadOutcome = *threadOutcomePtr;
  if(threadOutcome == nullptr)
  {
    fatalFailure = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::BlockingSocketOperation " << this << ": could not copy a valid pointer to thread outcome");
    delete threadOutcomePtr;
    return outcome;
  }

  // Avoid any status/outcome parsing in case of fatal errors
  if(*threadOutcome == EP_THREAD_OUTCOME::FATAL_ERROR || !m_blockingExitNormal)
  {
    fatalFailure = true;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::BlockingSocketOperation " << this << ": fatal error encountered by separate thread");
    delete threadOutcomePtr;
    return outcome;
  }

  // Parse thread outcome
  switch(joinStatus)
  {
    case 0:
    {
      if(*threadOutcome == EP_THREAD_OUTCOME::SUCCESS)
      {
        outcome = true;
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::BlockingSocketOperation " << this << ": operation successful");
      }
      else
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::BlockingSocketOperation " << this << ": operation failed");
      }
      break;
    }

    default:
    {
      fatalFailure = true;
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::BlockingSocketOperation " << this << ": could not join separate thread; reason = " << std::strerror(errno));
      break;
    }
  }

  // Unset blocking thread variables (should have already been done by either separate thread or watchdog thread)
  pthread_mutex_lock(&m_blockingMutex);
  m_blockingThread = (pthread_t)-1;
  m_blockingExitNormal = false;
  pthread_cond_broadcast(&m_blockingCond);
  pthread_mutex_unlock(&m_blockingMutex);

  delete threadOutcomePtr;
  return outcome;
}

} // namespace ns3

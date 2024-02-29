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
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(ExternalProcess);
NS_LOG_COMPONENT_DEFINE("ExternalProcess");

void*
WatchdogFunction(void *arg)
{
  bool crashOnFailure = *(bool*)arg;

  // Keep checking if instances are running
  while(true)
  {
    pthread_mutex_lock(&g_watchdogMutex);
    for(auto runnerIt = g_runnerMap.begin(); runnerIt != g_runnerMap.end(); runnerIt++)
    {
      ExternalProcess *runner = runnerIt->second;
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
        NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess (watchdog): error on checking PID " << childPid << " status; reason = " << std::strerror(errno));
      }
      else
      {
        // No status change, still running
        if(retPid == 0)
        {
          continue;
        }

        // Prevent sending SIGKILL to an already terminated child process
        runner->Teardown(-1);

        // Prevent child processes from becoming zombies
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): waiting on PID " << childPid);
        if(waitpid(childPid, NULL, 0) < 0)
        {
          NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess (watchdog): error on waiting PID " << childPid << " to terminate; reason = " << std::strerror(errno));
        }
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): PID " << childPid << " terminated correctly");

        // Only crash on PID failures if specified so
        if(crashOnFailure)
        {
          NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess (watchdog): unexpected failure on PID " << childPid);
        }
      }
    }
    pthread_mutex_unlock(&g_watchdogMutex);

    // Check for exit condition
    bool doExit = false;
    pthread_mutex_lock(&g_watchdogMutex);
    doExit = g_watchdogExit;
    pthread_mutex_unlock(&g_watchdogMutex);
    if(doExit)
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): terminating all remaining instances");
      break;
    }
  }

  // Kill all remaining instances on watchdog exit
  pthread_mutex_lock(&g_watchdogMutex);
  for(auto runnerIt = g_runnerMap.begin(); runnerIt != g_runnerMap.end(); runnerIt++)
  {
    ExternalProcess *runner = runnerIt->second;
    if(runner == nullptr)
    {
      continue;
    }

    pid_t childPid = runnerIt->first;
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): killing PID " << childPid);
    runner->Teardown(childPid);

    // Prevent child processes from becoming zombies
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): waiting on PID " << childPid);
    if(waitpid(childPid, NULL, 0) < 0)
    {
      NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess (watchdog): error on waiting PID " << childPid << " to terminate; reason = " << std::strerror(errno));
    }
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess (watchdog): PID " << childPid << " terminated correctly");
  }
  g_runnerMap.clear();
  pthread_mutex_unlock(&g_watchdogMutex);

  return nullptr;
}



uint32_t ExternalProcess::m_counter = 0;
bool ExternalProcess::m_watchdogInit = false;

ExternalProcess::ExternalProcess()
  : Object(),
    m_processRunning(false),
    m_processPid(-1),
    m_pipeInName(""),
    m_pipeOutName(""),
    m_lastWrite(),
    m_lastRead()
{
  // Update instance counter
  m_counter++;

  // Only create the watchdog thread once
  if(m_counter > 0 && !m_watchdogInit)
  {
    // No need for mutex, watchdog thread is not running yet
    g_watchdogExit = false;
    if(pthread_create(&g_watchdog, NULL, WatchdogFunction, &m_crashOnFailure) == -1)
    {
      NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess " << this << ": could not create watchdog thread; reason = " << std::strerror(errno));
    }
    m_watchdogInit = true;
  }

  // Initialize latest timestamps for Write and Read; no need for real time since they are used for intervals only
  if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &m_lastWrite) == -1 || clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &m_lastRead) == -1)
  {
    NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess " << this << ": could not retrieve current CPU time; reason = " << std::strerror(errno));
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
    .AddAttribute("ProcessLauncher",
                  "Absolute path to the process launcher script",
                  StringValue(""),
                  MakeStringAccessor(&ExternalProcess::m_processLauncher),
                  MakeStringChecker())
    .AddAttribute("ProcessExtraArgs",
                  "String containing additional arguments to process launcher script",
                  StringValue(""),
                  MakeStringAccessor(&ExternalProcess::m_processExtraArgs),
                  MakeStringChecker())
    .AddAttribute("CrashOnFailure",
                  "Flag indicating whether to raise a fatal exeception if the external process fails.",
                  BooleanValue(true),
                  MakeBooleanAccessor(&ExternalProcess::m_crashOnFailure),
                  MakeBooleanChecker())
    .AddAttribute("ThrottleWrites",
                  "Minimum time between a read and a subsequent write; this delay is applied before writing.",
                  TimeValue(MilliSeconds(0)),
                  MakeTimeAccessor(&ExternalProcess::m_throttleWrites),
                  MakeTimeChecker())
    .AddAttribute("ThrottleReads",
                  "Minimum time between a write and a subsequent read; this delay is applied before reading.",
                  TimeValue(MilliSeconds(0)),
                  MakeTimeAccessor(&ExternalProcess::m_throttleReads),
                  MakeTimeChecker())
  ;

  return tid;
}

bool
ExternalProcess::Create(void)
{
  if(m_processRunning)
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": process already running (PID: " << m_processPid << ")");
    return false;
  }

  struct stat fbuf;
  if(m_processLauncher.length() == 0 || (stat(m_processLauncher.c_str(), &fbuf) != 0))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": invalid path to process launcher script '" << m_processLauncher << "'");
    return false;
  }

  // Create pipes
  m_pipeInName = GetFifoTmpName(true);
  m_pipeOutName = GetFifoTmpName(false);
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": pipes '" << m_pipeInName << "', '" << m_pipeOutName << "'");

  if(!CreateFifo(m_pipeInName))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to create pipe '" << m_pipeInName << "'");
    Teardown(m_processPid);
    return false;
  }
  if(!CreateFifo(m_pipeOutName))
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to create pipe '" << m_pipeOutName << "'");
    Teardown(m_processPid);
    return false;
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
      int outcome = 1;

      // Replace program in child process & pass INPUT and OUTPUT pipes (in reverse order for side process)
      if(m_processExtraArgs.length() == 0)
      {
        char *args_exc[] = {(char*)m_processLauncher.c_str(),
                            (char*)m_pipeOutName.c_str(), (char*)m_pipeInName.c_str(),
                            NULL};
        outcome = execvp(args_exc[0], args_exc);
      }
      else
      {
        std::string wrappedExtraArgs = "\"";
        wrappedExtraArgs += m_processExtraArgs + "\"";
        char *args_exc[] = {(char*)m_processLauncher.c_str(),
                            (char*)m_pipeOutName.c_str(), (char*)m_pipeInName.c_str(),
                            (char*)wrappedExtraArgs.c_str(), NULL};
        outcome = execvp(args_exc[0], args_exc);
      }

      if(outcome != 0)
      {
        NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to create side process (exec)");
      }
    }

    default:
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": side process PID = " << m_processPid);
      m_processRunning = true;
      break;
    }
  }

  // Open pipes
  m_pipeInStream.open(m_pipeInName, std::ios::in);
  if(!m_pipeInStream.is_open())
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to open input pipe '" << m_pipeInName << "'");
    Teardown(m_processPid);
  }
  else
  {
    m_pipeOutStream.open(m_pipeOutName, std::ios::out);
    if(!m_pipeOutStream.is_open())
    {
      NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": failed to open output pipe '" << m_pipeOutName << "'");
      Teardown(m_processPid);
    }
    else
    {
      // Wait for process inizialization to be done
      m_pipeInStream.clear();
      std::string processOutput = "";
      std::getline(m_pipeInStream, processOutput);
      if(processOutput == MSG_READY)
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": side process ready");
      }
      else
      {
        NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Create " << this << ": side process failed to get ready; shutting down");
        Teardown(m_processPid);
      }
    }
  }
  NS_ASSERT_MSG(m_processRunning == (m_pipeInStream.is_open() && m_pipeOutStream.is_open()), "Process running but pipes not both open");

  // Add current instance to the runner map, associating it to the spawned process's PID
  pthread_mutex_lock(&g_watchdogMutex);
  auto runnerIt = g_runnerMap.find(m_processPid);
  if(runnerIt != g_runnerMap.end() && runnerIt->second != nullptr)
  {
    NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess::Create " << this << ": overwriting runner for PID " << m_processPid);
  }
  g_runnerMap[m_processPid] = this;
  pthread_mutex_unlock(&g_watchdogMutex);

  // Close pipes to avoid busy wait: leveraging blocking read() on empty pipes
  m_pipeInStream.close();
  m_pipeOutStream.close();
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

void
ExternalProcess::Teardown(pid_t childPid)
{
  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Teardown " << this);

  // Send SIGKILL to side process (Ctrl+C)
  if(m_processRunning)
  {
    m_processRunning = false;
    if(childPid != -1 && childPid == m_processPid)
    {
      kill(childPid, SIGKILL);
    }

    // Only remove from runner map if there is no watchdog thread (prevent race conditions)
    if(!m_watchdogInit)
    {
      auto runnerIt = g_runnerMap.find(m_processPid);
      if(runnerIt != g_runnerMap.end())
      {
        g_runnerMap[runnerIt->first] = nullptr;
        g_runnerMap.erase(runnerIt);
      }
    }

    m_processPid = -1;
  }

  // Close streams, if open
  if(m_pipeInStream.is_open())
  {
    m_pipeInStream.close();
  }
  if(m_pipeOutStream.is_open())
  {
    m_pipeOutStream.close();
  }

  // Remove named pipes, if any
  if(m_pipeInName.length() > 0)
  {
    unlink(m_pipeInName.c_str());
    m_pipeInName = "";
  }
  if(m_pipeOutName.length() > 0)
  {
    unlink(m_pipeOutName.c_str());
    m_pipeOutName = "";
  }
}

bool
ExternalProcess::Write(const std::string &str, bool first, bool flush, bool last)
{
  bool ret = false;
  if(!m_processRunning)
  {
    return ret;
  }

  // Apply throttling, if set
  ThrottleOperation();

  if(!m_pipeOutStream.is_open())
  {
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Write " << this << ": opening output pipe");
    m_pipeOutStream.open(m_pipeOutName, std::ios::out);

    // First message flushed right away to unblock side process stuck on empty pipe read()
    flush = true;
  }
  NS_ASSERT_MSG(m_pipeOutStream.is_open(), "Failed to open output pipe");

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Write " << this << ": str = '" << str << "', first = " << first << ", flush = " << flush << ", last = " << last);
  if(first)
  {
    m_pipeOutStream << MSG_WRITE_BEGIN << "\n";
  }

  // Write to the output pipe
  m_pipeOutStream << str;

  if(flush)
  {
    m_pipeOutStream << std::endl;
  }
  else
  {
    m_pipeOutStream << "\n";
  }

  if(last)
  {
    m_pipeOutStream << MSG_WRITE_END << std::endl;
    m_pipeOutStream.close();
  }

  ret = true;
  return ret;
}

bool
ExternalProcess::Read(std::string &str, bool &hasNext)
{
  bool ret = false;
  if(!m_processRunning)
  {
    return ret;
  }

  // Apply throttling, if set
  ThrottleOperation(true);

  if(!m_pipeInStream.is_open()){
    NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Read " << this << ": opening input pipe");
    m_pipeInStream.open(m_pipeInName, std::ios::in);
  }
  NS_ASSERT_MSG(m_pipeInStream.is_open(), "Input pipe is not open");

  std::string processOutput = "";
  std::getline(m_pipeInStream, processOutput);
  if(processOutput == MSG_READ_BEGIN)
  {
    str = "";
    hasNext = true;
    ret = true;
  }
  else if(processOutput == MSG_READ_END)
  {
    str = "";
    hasNext = false;
    ret = true;

    // No more side process output to parse at this point
    m_pipeInStream.close();
  }
  else
  {
    str = processOutput;
    hasNext = true;
    ret = true;
  }

  NS_LOG_DEBUG(CURRENT_TIME << " ExternalProcess::Read " << this << ": str = '" << str << "', hasNext = " << hasNext);
  return ret;
}

void
ExternalProcess::DoDispose(void)
{
  if(m_processRunning)
  {
    Write(MSG_KILL, true, true, true);
  }

  Teardown(m_processPid);

  // Update instance counter
  m_counter--;

  // Wait for watchdog thread to terminate if this was the last instance
  if(m_counter == 0)
  {
    pthread_mutex_lock(&g_watchdogMutex);
    g_watchdogExit = true;
    pthread_mutex_unlock(&g_watchdogMutex);

    if(pthread_join(g_watchdog, NULL) == -1)
    {
      NS_FATAL_ERROR(CURRENT_TIME << " ExternalProcess " << this << ": could not join watchdog thread; reason = " << std::strerror(errno));
    }
    m_watchdogInit = false;
  }

  Object::DoDispose();
}

const std::string
ExternalProcess::GetFifoTmpName(bool isInput) const
{
  std::string ret = "/tmp/ns3_extproc_";
  ret += std::to_string((long)this) + "_";
  if(isInput)
  {
    ret += PIPE_TRAIL_IN;
  }
  else
  {
    ret += PIPE_TRAIL_OUT;
  }
  return ret;
}

bool
ExternalProcess::CreateFifo(const std::string fifoName) const
{
  int outcome = mkfifo(fifoName.c_str(), 0660);
  return outcome == 0;
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

} // namespace ns3

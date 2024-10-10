/*
 * Copyright (c) 2024 Sapienza University of Rome
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

#include "ns3/ext-process.h"
#include "ns3/core-module.h"
#include "ns3/test.h"
#include <unistd.h>
#include <sys/wait.h>
#include <ctime>

using namespace ns3;

/**
 * \defgroup ext-process-tests Tests for ext-process
 * \ingroup ext-process
 * \ingroup tests
 */

// Path to the launcher script handling external process's execution
const std::string g_launcherPath = "<path/to/ns3/installation>/contrib/ext-process/launcher-py.sh";

/**
 * \ingroup ext-process-tests
 * \brief Test case for an external process implemented in Python.
 */
class ExtProcessTestPython: public TestCase
{
public:
  ExtProcessTestPython();
  virtual ~ExtProcessTestPython();

private:
  void DoRun() override;

}; // class ExtProcessTestPython



/**
 * \ingroup ext-process-tests
 * \brief Test case for a full-remote external process (i.e. no launcher path) 
 * and ns-3 taking the TCP client role.
 */
class ExtProcessTestRemote: public TestCase
{
public:
  ExtProcessTestRemote();
  virtual ~ExtProcessTestRemote();

private:
  void DoRun() override;

}; // class ExtProcessTestRemote



/**
 * \ingroup ext-process-tests
 * \brief Test case for high-load Read() operations. 
 * 
 * This case is designed to identify issues revolving around 
 * excess data returned by Boost::ASIO sockets' read_until() 
 * function (i.e. data past the delimiter).
*/
class ExtProcessHighLoadRead: public TestCase
{
public:
  ExtProcessHighLoadRead();
  virtual ~ExtProcessHighLoadRead();

private:
  // High load parameters: many larg-ish messages in rapid succession
  static uint32_t m_msgSize;
  static uint32_t m_totMsgs;

  void DoRun() override;

  /**
   * \brief Thread function representing the high-load Write() 
   * thread.
   * 
   * \param [in] arg Pointer to port number (uint16_t*).
   * 
   * \return No return (constant nullptr).
  */
  static void* HighLoadThread(void* arg);

}; // class ExtProcessHighLoadRead



ExtProcessTestPython::ExtProcessTestPython()
  : TestCase("ExternalProcess test for a Python process")
{
}

ExtProcessTestPython::~ExtProcessTestPython()
{
}

void
ExtProcessTestPython::DoRun()
{
  LogComponentEnable("ExternalProcess", LOG_LEVEL_ALL);

  // ExternalProcess instance creation
  Ptr<ExternalProcess> ep = CreateObjectWithAttributes<ExternalProcess>(
    "Launcher", StringValue(g_launcherPath),            // Mandatory (locally-launcher processes only)
    "CliArgs", StringValue("--attempts 10 --debug"),    // Optional: CLI arguments for launcher script
    "Port", UintegerValue(0),                           // Optional: default value (0) lets the OS pick a free port automatically
    "Timeout", TimeValue(MilliSeconds(150)),            // Optional: enables timeout on socket operations (e.g. accept, write, read)
    "Attempts", UintegerValue(10),                      // Optional: enables multiple attempts for socket operations (only if timeout is non-zero)
    "TimedAccept", BooleanValue(true),                  // Optional: enables timeout on socket accept operations (see above, 'Attempts')
    "TimedWrite", BooleanValue(true),                   // Optional: enables timeout on socket write operations (see above, 'Attempts')
    "TimedRead", BooleanValue(true)                     // Optional: enables timeout on socket read operations (see above, 'Attempts')
  );

  // Actual process creation & awaiting connection through socket
  bool outcome = ep->Create();
  NS_TEST_ASSERT_MSG_EQ(outcome, true, "External process Create() failed");

  std::vector<std::string> dataToSend, dataRecv;
  dataToSend.push_back("this");
  dataToSend.push_back("is");
  dataToSend.push_back("a");
  dataToSend.push_back("simple");
  dataToSend.push_back("test");

  // Typical protocol: ns-3 initiates communication, then awaits for result/outcome
  // 1. Writing phase
  uint32_t tokenCounter = 0;
  uint32_t flushCounter = 0;
  for(auto dataIt = dataToSend.begin(); dataIt != dataToSend.end(); dataIt++, tokenCounter++, flushCounter++)
  {
    bool firstToken = dataIt == dataToSend.begin();
    bool flushNow = flushCounter >= (dataToSend.size() / 2);
    if(flushNow)
    {
      // Reset to avoid flushing at every token past the half of them all
      flushCounter = 0;
    }
    bool lastToken = std::next(dataIt, 1) == dataToSend.end();
    bool tmpOutcome = ep->Write(*dataIt, firstToken, flushNow, lastToken);
    outcome = outcome && tmpOutcome;
    NS_TEST_ASSERT_MSG_EQ(tmpOutcome, true, "External process Write() failed on token " << tokenCounter);
  }
  NS_TEST_ASSERT_MSG_EQ(outcome, true, "External process Write() failed overall");

  // 2. Reading phase
  tokenCounter = 0;
  std::string inToken = "";
  bool moreTokens = false;
  do
  {
    bool tmpOutcome = ep->Read(inToken, moreTokens);
    if(tmpOutcome)
    {
      dataRecv.push_back(inToken);
    }
    outcome = outcome && tmpOutcome;
    NS_TEST_ASSERT_MSG_EQ(tmpOutcome, true, "External process Read() failed on token " << tokenCounter);
    tokenCounter++;
  } while (moreTokens);
  NS_TEST_ASSERT_MSG_EQ(outcome, true, "External process Read() failed overall");

  // Check correct reception of data tokens
  NS_TEST_ASSERT_MSG_EQ(dataRecv.size(), dataToSend.size(), "Data tokens number mismatch");
  if(dataRecv.size() == dataToSend.size())
  {
    for(uint32_t i=0; i<dataToSend.size(); i++)
    {
      NS_TEST_ASSERT_MSG_EQ(dataRecv[i], dataToSend[i], "Data token " << i << " mismatch");
    }
  }

  // Cleanup
  ep = 0;
}



ExtProcessTestRemote::ExtProcessTestRemote()
  : TestCase("ExternalProcess test for a full-remote process and ns-3 as TCP client")
{
}

ExtProcessTestRemote::~ExtProcessTestRemote()
{
}

void
ExtProcessTestRemote::DoRun()
{
  LogComponentEnable("ExternalProcess", LOG_LEVEL_ALL);

  // Randomize port according to system time
  std::time_t sysTime = std::time(0);
  std::tm* nowTime = std::localtime(&sysTime);
  uint32_t seed = (uint32_t)nowTime->tm_sec;
  RngSeedManager::SetSeed(seed);
  RngSeedManager::SetRun(0);
  Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
  uint16_t port = 10000 + rand->GetInteger(0, 55535);
  rand = 0;

  // ExternalProcess instance creation
  Ptr<ExternalProcess> ep = CreateObjectWithAttributes<ExternalProcess>(
    "TcpRole", UintegerValue(ExternalProcess::TcpRole::CLIENT),   // Optional: this instance will act as a TCP client, instead of server (default)
    "Launcher", StringValue(""),                                  // Optional: empty-string indicates a full-remote process (i.e. no launcher needed)
    "CliArgs", StringValue(""),                                   // Ignored: CLI arguments do not apply to full-remote processes
    "Address", StringValue("127.0.0.1"),                          // Mandatory (full-remote processes & CLIENT role only)
    "Port", UintegerValue(port),                                  // Mandatory (full-remote processes only)
    "Timeout", TimeValue(MilliSeconds(150)),                      // Optional: enables timeout on socket operations (e.g. accept, write, read)
    "Attempts", UintegerValue(10),                                // Optional: enables multiple attempts for socket operations (only if timeout is non-zero)
    "TimedAccept", BooleanValue(true),                            // Optional: enables timeout on socket accept operations (see above, 'Attempts')
    "TimedWrite", BooleanValue(true),                             // Optional: enables timeout on socket write operations (see above, 'Attempts')
    "TimedRead", BooleanValue(true)                               // Optional: enables timeout on socket read operations (see above, 'Attempts')
  );

  // Launch external process as TCP server
  std::vector<char*> argv;
  argv.push_back((char*)g_launcherPath.c_str());

  // Launcher arguments
  std::string portStr = std::to_string(port);
  std::string preServer = "--server";
  std::string preDebug = "--debug";
  argv.push_back((char*)portStr.c_str());
  argv.push_back((char*)preServer.c_str());
  argv.push_back((char*)preDebug.c_str());
  argv.push_back(NULL);

  pid_t childPid = fork();
  switch(childPid)
  {
    case -1:
    {
      NS_TEST_ASSERT_MSG_EQ(false, true, "Failed to start external process");
      return;
    }

    case 0:
    {
      // Substitute current process with external process
      if(execvp(argv[0], argv.data()) != 0)
      {
        NS_TEST_ASSERT_MSG_EQ(false, true, "Failed to start external process");
        argv.clear();
        return;
      }
    }

    default:
    {
      NS_TEST_ASSERT_MSG_EQ(true, true, "External process started successfully");
      Time sleepS = Seconds(5);
      struct timespec sleepDelay;
      sleepDelay.tv_sec = sleepS.GetNanoSeconds() / (int64_t)1000000000UL;
      sleepDelay.tv_nsec = sleepS.GetNanoSeconds() % (int64_t)1000000000UL;
      nanosleep(&sleepDelay, nullptr);
    }
  }

  // Actual process creation & awaiting connection through socket
  bool outcome = ep->Create();
  NS_TEST_ASSERT_MSG_EQ(outcome, true, "External process Create() failed");

  std::vector<std::string> dataToSend, dataRecv;
  dataToSend.push_back("this");
  dataToSend.push_back("is");
  dataToSend.push_back("a");
  dataToSend.push_back("simple");
  dataToSend.push_back("test");

  // Typical protocol: ns-3 initiates communication, then awaits for result/outcome
  // 1. Writing phase
  uint32_t tokenCounter = 0;
  uint32_t flushCounter = 0;
  for(auto dataIt = dataToSend.begin(); dataIt != dataToSend.end(); dataIt++, tokenCounter++, flushCounter++)
  {
    bool firstToken = dataIt == dataToSend.begin();
    bool flushNow = flushCounter >= (dataToSend.size() / 2);
    if(flushNow)
    {
      // Reset to avoid flushing at every token past the half of them all
      flushCounter = 0;
    }
    bool lastToken = std::next(dataIt, 1) == dataToSend.end();
    bool tmpOutcome = ep->Write(*dataIt, firstToken, flushNow, lastToken);
    outcome = outcome && tmpOutcome;
    NS_TEST_ASSERT_MSG_EQ(tmpOutcome, true, "External process Write() failed on token " << tokenCounter);
  }
  NS_TEST_ASSERT_MSG_EQ(outcome, true, "External process Write() failed overall");

  // 2. Reading phase
  tokenCounter = 0;
  std::string inToken = "";
  bool moreTokens = false;
  do
  {
    bool tmpOutcome = ep->Read(inToken, moreTokens);
    if(tmpOutcome)
    {
      dataRecv.push_back(inToken);
    }
    outcome = outcome && tmpOutcome;
    NS_TEST_ASSERT_MSG_EQ(tmpOutcome, true, "External process Read() failed on token " << tokenCounter);
    tokenCounter++;
  } while (moreTokens);
  NS_TEST_ASSERT_MSG_EQ(outcome, true, "External process Read() failed overall");

  // Check correct reception of data tokens
  NS_TEST_ASSERT_MSG_EQ(dataRecv.size(), dataToSend.size(), "Data tokens number mismatch");
  if(dataRecv.size() == dataToSend.size())
  {
    for(uint32_t i=0; i<dataToSend.size(); i++)
    {
      NS_TEST_ASSERT_MSG_EQ(dataRecv[i], dataToSend[i], "Data token " << i << " mismatch");
    }
  }

  // Cleanup
  ep = 0;
  int status = -1;
  pid_t retPid = waitpid(childPid, &status, WNOHANG);
  if(retPid == 0)
  {
    kill(childPid, SIGKILL);
    waitpid(childPid, NULL, 0);
  }
  argv.clear();
}



uint32_t ExtProcessHighLoadRead::m_msgSize = 512;
uint32_t ExtProcessHighLoadRead::m_totMsgs = 10000;

ExtProcessHighLoadRead::ExtProcessHighLoadRead()
  : TestCase("ExternalProcess test for high-load Read() operations")
{
}

ExtProcessHighLoadRead::~ExtProcessHighLoadRead()
{
}

void
ExtProcessHighLoadRead::DoRun()
{
  LogComponentEnable("ExternalProcess", LOG_LEVEL_ALL);

  // Randomize port according to system time
  std::time_t sysTime = std::time(0);
  std::tm* nowTime = std::localtime(&sysTime);
  uint32_t seed = (uint32_t)nowTime->tm_sec;
  RngSeedManager::SetSeed(seed);
  RngSeedManager::SetRun(0);
  Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
  uint16_t port = 10000 + rand->GetInteger(0, 55535);
  rand = 0;

  // Simulate a full-remote process in TCP server role
  pthread_t hlThread;
  if(pthread_create(&hlThread, NULL, ExtProcessHighLoadRead::HighLoadThread, &port) == -1)
  {
    NS_TEST_ASSERT_MSG_EQ(false, true, "HighLoadThread creation failed");
    return;
  }

  // Let this thread start TCP server before connecting / reading from socket
  Time sleepS = MilliSeconds(50);
  struct timespec sleepDelay;
  sleepDelay.tv_sec = sleepS.GetNanoSeconds() / (int64_t)1000000000UL;
  sleepDelay.tv_nsec = sleepS.GetNanoSeconds() % (int64_t)1000000000UL;
  nanosleep(&sleepDelay, nullptr);

  // ExternalProcess instance creation
  Ptr<ExternalProcess> ep = CreateObjectWithAttributes<ExternalProcess>(
    "TcpRole", UintegerValue(ExternalProcess::TcpRole::CLIENT),   // TCP client on this thread
    "Launcher", StringValue(""),                                  // No launcher, ExternalProcess only used as Boost::ASIO wrapper in this case
    "Address", StringValue("127.0.0.1"),                          // Mandatory (full-remote processes & CLIENT role only)
    "Port", UintegerValue(port)                                   // Mandatory (full-remote processes only)
  );
  bool outcome = ep->Create();
  NS_TEST_ASSERT_MSG_EQ(outcome, true, "External process Create() failed");
  if(!outcome)
  {
    return;
  }

  // Keep reading until a MSG_KILL arrives
  uint32_t msgCounter = 0;
  bool threadKilled = false;
  do
  {
    std::string inToken = "";
    bool moreTokens = false;
    do
    {
      outcome = ep->Read(inToken, moreTokens);
      NS_TEST_ASSERT_MSG_EQ(outcome, true, "External process Read() failed");

      // Test reception of entire message
      if(outcome && inToken.length() > 1)
      {
        // Avoid marking MSG_KILL as test failure
        if(inToken == MSG_KILL)
        {
          threadKilled = true;
          break;
        }

        char firstCh = inToken[0];
        char lastCh = inToken[inToken.length()-1];
        outcome = firstCh != '0' && firstCh == lastCh;
        NS_TEST_ASSERT_MSG_EQ(outcome, true, "External process Read() returned a partial message ('" << inToken << "')");
        if(outcome)
        {
          msgCounter++;
        }
        else
        {
          NS_FATAL_ERROR("ExtProcessHighLoadRead::DoRun(): early exit due to partial message read");
          return;
        }
      }
    } while(moreTokens);
  } while(!threadKilled);
  NS_TEST_ASSERT_MSG_EQ(msgCounter, ExtProcessHighLoadRead::m_totMsgs, "Message number mismatch");

  // Cleanup
  pthread_join(hlThread, NULL);
  ep = 0;
}

void*
ExtProcessHighLoadRead::HighLoadThread(void* arg)
{
  // Retrieve port number from argument
  if(arg == nullptr)
  {
    std::cout << "HighLoadThread | Invalid argument provided (nullptr)" << std::endl;
    return nullptr;
  }
  const uint16_t port = *(uint16_t*)arg;
  std::string largeMsg = "";
  for(uint32_t i=0; i<m_msgSize; i++)
  {
    largeMsg += "0";
  }

  // ExternalProcess instance (TCP server role, blocking operations)
  Ptr<ExternalProcess> ep = CreateObjectWithAttributes<ExternalProcess>(
    "TcpRole", UintegerValue(ExternalProcess::TcpRole::SERVER),   // TCP server on this thread, for implicit synchronization with main thread
    "Launcher", StringValue(""),                                  // No launcher, ExternalProcess only used as Boost::ASIO wrapper in this case
    "Port", UintegerValue(port)                                   // Mandatory (full-remote processes only)
  );
  if(!ep->Create())
  {
    std::cout << "HighLoadThread | Failed to create full-remote external process" << std::endl;
    return nullptr;
  }

  // Connection established, send high load data
  for(uint32_t i=0; i<m_totMsgs; i++)
  {
    // Replace first & last characters only (fast operations & easy inspection)
    // e.g. for size = 10, first message sent: largeMsg = "A00000000A"
    char currCh = (char)(65 + (i % 10));
    largeMsg[0] = currCh;
    largeMsg[m_msgSize-1] = currCh;

    bool outcome = false;

    // Batch send messages from 205th to 500th
    if(i == 250)
    {
      outcome = ep->Write(largeMsg, true, false, false);
    }
    else if(i > 250 && i < 499)
    {
      outcome = ep->Write(largeMsg, false, false, false);
    }
    else if(i == 499)
    {
      outcome = ep->Write(largeMsg, false, false, true);
    }

    // Send message & flush right away
    else
    {
      outcome = ep->Write(largeMsg);
    }

    if(!outcome)
    {
      std::cout << "HighLoadThread | Failed to Write() to external process; exiting" << std::endl;
      break;
    }
  }
  std::cout << "HighLoadThread | Finished data to write; exiting" << std::endl;

  // Cleanup
  ep = 0;
  return nullptr;
}



/**
 * \ingroup ext-process-tests
 * \brief Test suite for module ext-process.
 */
class ExtProcessTestSuite: public TestSuite
{
public:
  ExtProcessTestSuite();
}; // class ExtProcessTestSuite

ExtProcessTestSuite::ExtProcessTestSuite()
  : TestSuite("ext-process", UNIT)
{
  AddTestCase(new ExtProcessTestPython, TestCase::QUICK);
  AddTestCase(new ExtProcessTestRemote, TestCase::QUICK);
  AddTestCase(new ExtProcessHighLoadRead, TestCase::QUICK);
}

/**
 * \ingroup ext-process-tests
 * \brief Static variable for test initialization.
 */
static ExtProcessTestSuite sextProcessTestSuite;

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

using namespace ns3;

/**
 * \defgroup ext-process-tests Tests for ext-process
 * \ingroup ext-process
 * \ingroup tests
 */

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

  // Path to the launcher script handling external process's execution
  std::string launcherPath = "<path/to/ns-3/installation>/contrib/ext-process/launcher-py.sh";

  // ExternalProcess instance creation
  Ptr<ExternalProcess> ep = CreateObjectWithAttributes<ExternalProcess>(
    "Launcher", StringValue(launcherPath),                  // Mandatory
    "CliArgs", StringValue("--attempts 10 --debug True"),   // Optional: CLI arguments for launcher script
    "Port", UintegerValue(0),                               // Optional: default value (0) lets the OS pick a free port automatically
    "Timeout", TimeValue(MilliSeconds(150)),                // Optional: enables timeout on socket operations (e.g. accept, write, read)
    "Attempts", UintegerValue(10),                          // Optional: enables multiple attempts for socket operations (only if timeout is non-zero)
    "TimedAccept", BooleanValue(true),                      // Optional: enables timeout on socket accept operations (see above, 'Attempts')
    "TimedWrite", BooleanValue(true),                       // Optional: enables timeout on socket write operations (see above, 'Attempts')
    "TimedRead", BooleanValue(true)                         // Optional: enables timeout on socket read operations (see above, 'Attempts')
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
}

/**
 * \ingroup ext-process-tests
 * \brief Static variable for test initialization.
 */
static ExtProcessTestSuite sextProcessTestSuite;

#include "pch.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

// vstest reports "no tests found" as a pass, so an empty suite is a green tick over nothing (AGENTS.md
// §3). Delete this when the first real test lands, never before.
TEST_CLASS(SuiteSmoke)
{
public:
  TEST_METHOD(SuiteRuns)
  {
    Assert::IsTrue(true);
  }
};

} // namespace MachineTests

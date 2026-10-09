// CpuConformance/TestRunner.h
#pragma once

#include "Cpu.h"
#include "Memory.h"
#include "OpenBus.h"
#include "TestCase.h"

#include <string>
#include <vector>

namespace CpuConformance
{

/// Runs SingleStepTests cases against Machine::Cpu, one instruction each.
///
/// A test sets the registers and the bytes it names, executes one Step(), and then compares:
/// every register; the flags under the test's mask, which leaves out only the flags the suite's
/// metadata calls undefined for that opcode; every byte the test names, against its final value;
/// and the rest of the 1 MiB, which must still be zero, so that a write to the wrong address is
/// caught as well as a missing one. The prefetch queue and the cycle traces are not compared.
class TestRunner
{
public:
  TestRunner();

  /// Runs one test and returns what did not match: empty when it passed.
  [[nodiscard]] std::vector<std::string> Run(const TestCase& _test);

private:
  Machine::Memory m_memory;
  OpenBus m_ports;
  Machine::Cpu m_cpu;
  std::vector<std::uint8_t> m_zeros;
};

} // namespace CpuConformance

// CpuConformance/TestReader.h
#pragma once

#include "TestCase.h"

#include <cstdint>
#include <fstream>
#include <string>

namespace CpuConformance
{

/// Reads the line-based form of a SingleStepTests file that Tools/CpuConformance.py writes:
///
///   t <index> <flags mask> <name...>     starts a test
///   b <instruction bytes in hex>
///   i <14 registers>                     initial: ax bx cx dx cs ss ds es sp bp si di ip flags
///   m <address> <byte> ...               initial memory
///   f <14 registers>                     final, complete
///   n <address> <byte> ...               final memory, complete
///
/// Every number is hexadecimal. A test ends at its `n` line. Lines starting with `#` are comments.
class TestReader
{
public:
  explicit TestReader(const std::string& _path);

  [[nodiscard]] bool IsOpen() const noexcept
  {
    return m_stream.is_open();
  }

  /// Reads the next test into _test. Returns false at the end of the file or on a malformed line,
  /// in which case Error() says which.
  [[nodiscard]] bool Next(TestCase& _test);

  [[nodiscard]] const std::string& Error() const noexcept
  {
    return m_error;
  }

private:
  std::ifstream m_stream;
  std::string m_line;
  std::string m_error;
  std::uint64_t m_lineNumber = 0;
};

} // namespace CpuConformance

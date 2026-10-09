// ReferenceRunner/TraceWriter.h
#pragma once

#include "InstructionObserver.h"

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace ReferenceRunner
{

/// Writes the trace file Tools/ReferenceTrace.py writes for DOSBox-X, so that Tools/CompareTrace.py
/// can compare the two (ADR-003): the 8 bytes "XTRACE1\0", then per instruction the fourteen
/// little-endian words CS IP AX BX CX DX SI DI BP SP DS ES SS FLAGS, as they are before it executes.
///
/// The trace ends just before the first instruction at the stop address, which for the boot trace is
/// the first call of GetKey. After that the writer ignores what it is told.
class TraceWriter final : public Machine::InstructionObserver
{
public:
  TraceWriter(const std::filesystem::path& _path, std::uint16_t _stopSegment, std::uint16_t _stopOffset);

  [[nodiscard]] bool IsOpen() const noexcept
  {
    return m_out.is_open() && m_out.good();
  }

  [[nodiscard]] bool Finished() const noexcept
  {
    return m_finished;
  }

  [[nodiscard]] std::uint64_t Records() const noexcept
  {
    return m_records;
  }

  void BeforeInstruction(const Machine::Registers& _registers) override;

private:
  std::ofstream m_out;
  std::uint16_t m_stopSegment;
  std::uint16_t m_stopOffset;
  std::uint64_t m_records = 0;
  bool m_finished = false;
};

} // namespace ReferenceRunner

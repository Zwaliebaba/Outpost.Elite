// CpuConformance/TestCase.h
#pragma once

#include "Registers.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CpuConformance
{

/// One byte of the 1 MiB address space, by linear address.
struct MemoryByte
{
  std::uint32_t address = 0;
  std::uint8_t value = 0;
};

/// One SingleStepTests case, as Tools/CpuConformance.py writes it. The final state is complete:
/// every register, and every byte the test names before or after, with its value afterwards.
struct TestCase
{
  std::uint32_t index = 0;
  std::uint16_t flagsMask = 0xFFFF; // flags the suite's metadata does not call undefined
  std::string name;
  std::string bytes; // the instruction bytes in hex, for reports
  Machine::Registers initial{};
  std::vector<MemoryByte> initialMemory;
  Machine::Registers expected{};
  std::vector<MemoryByte> expectedMemory;
};

/// The register order of the converted format, which is the suite's own.
inline constexpr std::array<std::uint16_t Machine::Registers::*, 14> REGISTER_ORDER = {
  &Machine::Registers::ax, &Machine::Registers::bx, &Machine::Registers::cx, &Machine::Registers::dx,   &Machine::Registers::cs,
  &Machine::Registers::ss, &Machine::Registers::ds, &Machine::Registers::es, &Machine::Registers::sp,   &Machine::Registers::bp,
  &Machine::Registers::si, &Machine::Registers::di, &Machine::Registers::ip, &Machine::Registers::flags};

inline constexpr std::array<std::string_view, 14> REGISTER_NAMES = {"ax", "bx", "cx", "dx", "cs", "ss", "ds",
                                                                    "es", "sp", "bp", "si", "di", "ip", "flags"};

} // namespace CpuConformance

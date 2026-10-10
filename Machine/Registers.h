// Machine/Registers.h
#pragma once

#include <cstdint>

namespace Machine
{

// The bits of the FLAGS register.
inline constexpr std::uint16_t FLAG_CARRY = 0x0001;
inline constexpr std::uint16_t FLAG_PARITY = 0x0004;
inline constexpr std::uint16_t FLAG_AUXILIARY = 0x0010;
inline constexpr std::uint16_t FLAG_ZERO = 0x0040;
inline constexpr std::uint16_t FLAG_SIGN = 0x0080;
inline constexpr std::uint16_t FLAG_TRAP = 0x0100;
inline constexpr std::uint16_t FLAG_INTERRUPT = 0x0200;
inline constexpr std::uint16_t FLAG_DIRECTION = 0x0400;
inline constexpr std::uint16_t FLAG_OVERFLOW = 0x0800;

// On the 8086 and 8088, FLAGS bits 1 and 12-15 always read as one and bits 3 and 5 as zero. Every
// write of the whole register (POPF, IRET, SAHF for the low byte) goes through these two masks.
inline constexpr std::uint16_t FLAGS_WRITABLE = 0x0FD5;
inline constexpr std::uint16_t FLAGS_FIXED_ONES = 0xF002;

/// The programmer-visible state of an 8086/8088. A plain aggregate (R8): the CPU owns one, and a
/// test, a loader or a debugger reads and writes it directly. `flags` is expected to carry the
/// fixed bits above; the CPU keeps it that way, and so must anyone who writes it.
struct Registers
{
  std::uint16_t ax = 0;
  std::uint16_t bx = 0;
  std::uint16_t cx = 0;
  std::uint16_t dx = 0;
  std::uint16_t si = 0;
  std::uint16_t di = 0;
  std::uint16_t bp = 0;
  std::uint16_t sp = 0;
  std::uint16_t cs = 0;
  std::uint16_t ds = 0;
  std::uint16_t es = 0;
  std::uint16_t ss = 0;
  std::uint16_t ip = 0;
  std::uint16_t flags = FLAGS_FIXED_ONES;

  [[nodiscard]] bool operator==(const Registers&) const noexcept = default;
};

} // namespace Machine

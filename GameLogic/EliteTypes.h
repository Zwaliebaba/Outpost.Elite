#pragma once

#include <cstdint>

/*
 * The numeric vocabulary the port is written in (ADR-002).
 *
 * Elite is 8-bit integer arithmetic and it depends on the details: bytes wrap, the carry flag
 * carries meaning across a routine boundary, and coordinates are sign-magnitude rather than
 * two's complement. Widening any of that to int or float would produce a different game --
 * wrong prices in a system three galaxies away -- so the helpers below make the original's
 * semantics say their name instead of hiding behind C++ integer promotion.
 *
 * Nothing here allocates, throws, or touches the platform.
 */

namespace Elite
{

  /// The processor flags a ported routine actually hands back to its caller. The original
  /// returns information in C and V often enough that a hidden global would be a bug farm; a
  /// routine that sets one names it in its return type instead.
  struct Flags
  {
    bool c = false;
    bool z = false;
    bool v = false;
    bool n = false;
  };

  /// Byte addition with an explicit carry in and out -- the 6502's ADC, without decimal mode
  /// (the game clears D at startup and never sets it).
  struct AddResult
  {
    std::uint8_t value = 0;
    bool carry = false;
    bool overflow = false;
  };

  [[nodiscard]] constexpr AddResult AddWithCarry(std::uint8_t _value, std::uint8_t _addend, bool _carryIn) noexcept
  {
    const std::uint16_t sum = static_cast<std::uint16_t>(_value) + _addend + (_carryIn ? 1u : 0u);
    const std::uint8_t result = static_cast<std::uint8_t>(sum);

    // Signed overflow: the operands agreed about their sign and the result disagreed with them.
    const bool overflow = ((~(static_cast<unsigned>(_value) ^ _addend) & (static_cast<unsigned>(_value) ^ result)) & 0x80u) != 0u;

    return AddResult{result, sum > 0xFFu, overflow};
  }

  /*
   * Subtract with borrow, where the carry means "no borrow" both going in and coming
   * out, so a subtraction that has not borrowed leaves it SET.
   *
   * It lives here beside `AddWithCarry` rather than in one .cpp file because the two are the same
   * primitive and the sign-magnitude arithmetic in `ShipMove.cpp` needs it as much as the line
   * drawing in `Lines.cpp` does -- and a second private copy is how the two quietly stop agreeing.
   */
  struct SubResult
  {
    std::uint8_t value = 0;
    bool carry = false;
  };

  [[nodiscard]] constexpr SubResult SubtractWithCarry(std::uint8_t _value, std::uint8_t _amount, bool _carryIn) noexcept
  {
    const std::uint16_t difference = static_cast<std::uint16_t>(_value) - _amount - (_carryIn ? 0u : 1u);
    return SubResult{static_cast<std::uint8_t>(difference), difference < 0x100u};
  }

  /// The 6502's ROL on a byte: shift left, carry in at bit 0, old bit 7 out.
  struct ShiftResult
  {
    std::uint8_t value = 0;
    bool carry = false;
  };

  [[nodiscard]] constexpr ShiftResult RotateLeft(std::uint8_t _value, bool _carryIn) noexcept
  {
    return ShiftResult{static_cast<std::uint8_t>((_value << 1) | (_carryIn ? 1u : 0u)), (_value & 0x80u) != 0u};
  }

  [[nodiscard]] constexpr ShiftResult RotateRight(std::uint8_t _value, bool _carryIn) noexcept
  {
    return ShiftResult{static_cast<std::uint8_t>((_value >> 1) | (_carryIn ? 0x80u : 0u)), (_value & 0x01u) != 0u};
  }

  /// The same rotate as RotateLeft, named for use where the caller cares about the value rather
  /// than about it being a shift step. Kept separate so call sites read as what they are doing.
  [[nodiscard]] constexpr ShiftResult RotateLeftValue(std::uint8_t _value, bool _carryIn) noexcept
  {
    return RotateLeft(_value, _carryIn);
  }

  /*
   * INWK+9/10 and kin, and the register pairs (A P), (S R) and (A X) the arithmetic kernel
   * takes and returns -- a sixteen-bit sign-magnitude value: fifteen bits of magnitude and the sign
   * in bit 7 of the high byte. One component of an orientation vector is one of these, and so is
   * every operand of `ADD` (M2-b), which is why the type lives here and not with the ship.
   */
  struct SignMag16
  {
    std::uint8_t lo = 0;
    std::uint8_t hi = 0;

    [[nodiscard]] constexpr bool operator==(const SignMag16&) const noexcept = default;
  };

  /// Elite stores a coordinate as three bytes: a 16-bit magnitude and a separate sign byte whose
  /// bit 7 is the sign. It is not two's complement, so negative zero exists and comparisons are
  /// on magnitude -- which is exactly why this is a type rather than an int.
  struct SignMag24
  {
    std::uint8_t lo = 0;
    std::uint8_t hi = 0;
    std::uint8_t sgn = 0;

    [[nodiscard]] constexpr bool operator==(const SignMag24&) const noexcept = default;

    [[nodiscard]] constexpr bool Negative() const noexcept
    {
      return (sgn & 0x80u) != 0u;
    }
    [[nodiscard]] constexpr std::uint16_t Magnitude() const noexcept
    {
      return static_cast<std::uint16_t>(lo | (hi << 8));
    }
  };

} // namespace Elite

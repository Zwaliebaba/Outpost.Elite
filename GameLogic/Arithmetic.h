// GameLogic/Arithmetic.h
#pragma once

#include <cstdint>

namespace Elite
{

// The 8086's arithmetic as the reference's instructions do it, for the native routines (ADR-010): the
// halves of a register, sign extension, two's complement, shifts and loop counts with the 8088's own
// edges. Each is what one instruction, or one idiom, of the original computes.

/// AL of AX, BL of BX, and so on.
[[nodiscard]] constexpr std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word);
}

/// AH of AX, BH of BX, and so on.
[[nodiscard]] constexpr std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

/// The word whose high byte is _high and low byte _low.
[[nodiscard]] constexpr std::uint16_t Join(std::uint8_t _high, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_high << 8) | _low);
}

/// _word with its low byte replaced: MOV AL,_low.
[[nodiscard]] constexpr std::uint16_t WithLow(std::uint16_t _word, std::uint8_t _low) noexcept
{
  return Join(High(_word), _low);
}

/// _word with its high byte replaced: MOV AH,_high.
[[nodiscard]] constexpr std::uint16_t WithHigh(std::uint16_t _word, std::uint8_t _high) noexcept
{
  return Join(_high, Low(_word));
}

/// MOV AL,_low on the register _word.
constexpr void SetLow(std::uint16_t& _word, std::uint8_t _low) noexcept
{
  _word = WithLow(_word, _low);
}

/// MOV AH,_high on the register _word.
constexpr void SetHigh(std::uint16_t& _word, std::uint8_t _high) noexcept
{
  _word = WithHigh(_word, _high);
}

/// XCHG AH,AL.
[[nodiscard]] constexpr std::uint16_t Swap(std::uint16_t _word) noexcept
{
  return Join(Low(_word), High(_word));
}

/// CBW: the byte, sign-extended to a word.
[[nodiscard]] constexpr std::uint16_t SignExtend(std::uint8_t _byte) noexcept
{
  return static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::int8_t>(_byte)));
}

/// CWD: the DX it leaves for _word in AX, FFFFh for a negative word and 0 otherwise.
[[nodiscard]] constexpr std::uint16_t SignWord(std::uint16_t _word) noexcept
{
  return (_word & 0x8000) != 0 ? std::uint16_t{0xFFFF} : std::uint16_t{0};
}

/// NEG r/m16: two's complement, with 8000h its own negation.
[[nodiscard]] constexpr std::uint16_t Negate(std::uint16_t _value) noexcept
{
  return static_cast<std::uint16_t>(0u - _value);
}

/// NEG r/m8.
[[nodiscard]] constexpr std::uint8_t Negate(std::uint8_t _value) noexcept
{
  return static_cast<std::uint8_t>(0u - _value);
}

/// SAR r/m16,CL. The 8088 does not mask the count, so from 16 on every bit is the sign.
[[nodiscard]] constexpr std::uint16_t Sar(std::uint16_t _value, unsigned _count) noexcept
{
  return static_cast<std::uint16_t>(static_cast<std::int16_t>(_value) >> (_count < 16 ? _count : 15));
}

/// SAR r/m8,CL, likewise unmasked.
[[nodiscard]] constexpr std::uint8_t Sar(std::uint8_t _value, unsigned _count) noexcept
{
  return static_cast<std::uint8_t>(static_cast<std::int8_t>(_value) >> (_count < 8 ? _count : 7));
}

/// The turns LOOP makes from CX = _count: a count of 0 runs 65,536 times.
[[nodiscard]] constexpr std::uint32_t LoopCount(std::uint16_t _count) noexcept
{
  return _count == 0 ? 0x10000u : _count;
}

/// _base + _bytes, wrapping at 64K as an offset does: [BX+disp], LEA.
[[nodiscard]] constexpr std::uint16_t Offset(std::uint16_t _base, std::uint16_t _bytes) noexcept
{
  return static_cast<std::uint16_t>(_base + _bytes);
}

} // namespace Elite

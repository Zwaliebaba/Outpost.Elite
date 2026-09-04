#pragma once

#include "EliteTypes.h"

#include <array>
#include <cstdint>

namespace Elite
{

  /// What a call to the generator hands back. The original returns a random byte in A and the
  /// previous call's byte in X, and leaves C and V randomly set -- callers use all four, so all
  /// four are in the return type rather than in hidden state.
  struct RngResult
  {
    std::uint8_t value = 0;
    std::uint8_t previous = 0;
    bool carry = false;
    bool overflow = false;
  };

  /*
   * Elite's random number generator: four bytes of state driving two coupled sequences, a
   * "feeder" that stirs itself and a "main" one that produces the number. It is not a good
   * generator by modern standards and that is beside the point -- ship positions, explosion
   * clouds and system contents all fall out of this exact bit pattern, so the port reproduces
   * the arithmetic rather than improving it (ADR-002).
   */
  class Rng
  {
  public:
    /// 6502: DORND -- the next random byte. The carry flag on entry participates, which is why
    /// the caller has to say what it was.
    [[nodiscard]] RngResult Next(bool _carryIn) noexcept;

    /// 6502: DORND2 -- the same, with carry forced clear so a sequence repeats regardless of
    /// what the caller happened to leave in C. Used where a cloud has to look the same twice.
    [[nodiscard]] RngResult NextRepeatable() noexcept
    {
      return Next(false);
    }

    [[nodiscard]] const std::array<std::uint8_t, 4>& State() const noexcept
    {
      return m_state;
    }
    void SetState(const std::array<std::uint8_t, 4>& _state) noexcept
    {
      m_state = _state;
    }

  private:
    // 6502: RAND (four bytes of zero page).
    std::array<std::uint8_t, 4> m_state{};
  };

} // namespace Elite

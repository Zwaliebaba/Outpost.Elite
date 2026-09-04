#include "pch.h"

#include "Rng.h"

namespace Elite
{

  /*
   * 6502: DORND.
   *
   * Two chained additions over the four state bytes. The first stirs the feeder pair and leaves
   * a carry; the second consumes that carry to produce the number. The carry the caller arrives
   * with feeds the initial rotate, so it is a genuine input -- DORND2 exists precisely because
   * some callers need it not to be.
   *
   * The order of the writes matters: each half stores the rotated or previous value into the
   * partner byte, so the old value has to be read before the store that overwrites it.
   */
  RngResult Rng::Next(bool _carryIn) noexcept
  {
    // Feeder half.
    const ShiftResult rotated = RotateLeft(m_state[0], _carryIn);
    const AddResult feeder = AddWithCarry(rotated.value, m_state[2], rotated.carry);

    m_state[0] = feeder.value;
    m_state[2] = rotated.value;

    // Main half. Both operands are read before either store lands on them.
    const std::uint8_t previous = m_state[1];
    const std::uint8_t partner = m_state[3];
    const AddResult main = AddWithCarry(previous, partner, feeder.carry);

    m_state[1] = main.value;
    m_state[3] = previous;

    return RngResult{main.value, previous, main.carry, main.overflow};
  }

} // namespace Elite

#include "pch.h"

#include "Universe.h"

#include "Arith.h"
#include "EliteTypes.h"

namespace Elite
{

void TwistSeeds(SystemSeeds& _seeds) noexcept
{
  std::array<std::uint8_t, 6>& q = _seeds.bytes;

  // 6502: LDA QQ15 / CLC / ADC QQ15+2 / TAX, then the high halves with the carry between them.
  // X and Y hold s0 + s1 across the shuffle below, which is why they are computed first.
  const AddResult low = AddWithCarry(q[0], q[2], false);
  const AddResult high = AddWithCarry(q[1], q[3], low.carry);

  /*
   * The shuffle, in the original's order. s1's high byte is written before its low byte, and
   * both are read again two lines later -- so a port that reordered these for tidiness would be
   * adding the old s2 rather than the new s1, and would generate a different universe that still
   * looked entirely plausible.
   */
  q[0] = q[2];
  q[1] = q[3];
  q[3] = q[5];
  q[2] = q[4];

  const AddResult sumLow = AddWithCarry(low.value, q[2], false);
  q[4] = sumLow.value;
  q[5] = AddWithCarry(high.value, q[3], sumLow.carry).value;
}

void NextSystem(SystemSeeds& _seeds) noexcept
{
  // 6502: TT20 -- JSR into a JSR that falls through, so four twists.
  for (int twist = 0; twist < 4; ++twist)
  {
    TwistSeeds(_seeds);
  }
}

void NextGalaxy(SystemSeeds& _seeds) noexcept
{
  for (std::uint8_t& byte : _seeds.bytes)
  {
    // 6502: ASL A / ROL QQ21,X -- the byte's top bit comes back round into its own bottom bit.
    byte = static_cast<std::uint8_t>((byte << 1) | (byte >> 7));
  }
}

SystemData GenerateSystemData(const SystemSeeds& _seeds) noexcept
{
  const std::array<std::uint8_t, 6>& q = _seeds.bytes;
  SystemData data;

  // 6502: LDA QQ15+1 / AND #7 -- the economy is three bits of the first seed's high byte.
  data.economy = static_cast<std::uint8_t>(q[1] & 0x07u);

  // 6502: LDA QQ15+2 / LSR / LSR / LSR / AND #7 -- and the government is three bits of the
  // second seed's low byte, shifted down.
  data.government = static_cast<std::uint8_t>((q[2] >> 3) & 0x07u);

  /*
   * 6502: LSR A / BNE TT77 -- anarchies and feudal states (government 0 and 1) are forced to a
   * poor economy. The LSR that tests it also SETS THE CARRY from government's bit 0, and that
   * carry is still there four instructions later; the explicit CLC at TT77 is what stops it
   * reaching the first addition, so this branch is arithmetically invisible and the next one
   * is not.
   */
  if (RotateRight(data.government, false).value == 0)
  {
    data.economy = static_cast<std::uint8_t>(data.economy | 0x02u);
  }

  // 6502: LDA QQ3 / EOR #7 / CLC -- a rich economy makes for a high tech level, so it inverts.
  std::uint8_t tech = static_cast<std::uint8_t>(data.economy ^ 0x07u);

  // 6502: LDA QQ15+3 / AND #3 / ADC QQ5 -- carry clear, from the CLC above.
  AddResult step = AddWithCarry(static_cast<std::uint8_t>(q[3] & 0x03u), tech, false);
  tech = step.value;

  /*
   * 6502: LDA QQ4 / LSR A / ADC QQ5.
   *
   * Here the carry is NOT clear: the LSR immediately before sets it from government's bit 0, and
   * this ADC consumes it. So an odd government adds one to the technology level, through a flag
   * rather than through anything that looks like arithmetic. Write this as tech + government / 2
   * and half the galaxy is one technology level too low.
   */
  const ShiftResult governmentBit = RotateRight(data.government, false);
  step = AddWithCarry(governmentBit.value, tech, governmentBit.carry);
  tech = step.value;
  data.techLevel = tech;

  /*
   * 6502: ASL A / ASL A / ADC QQ3 / ADC QQ4 / ADC #1 -- population is four times the technology
   * level plus the economy plus the government plus one, and every one of those additions takes
   * the carry from the one before it. The two shifts contribute a carry as well, from bit 7 of
   * the technology level.
   */
  const ShiftResult once = RotateLeft(tech, false);
  const ShiftResult twice = RotateLeft(once.value, false);

  step = AddWithCarry(twice.value, data.economy, twice.carry);
  step = AddWithCarry(step.value, data.government, step.carry);
  step = AddWithCarry(step.value, 1, step.carry);
  data.population = step.value;

  /*
   * 6502: LDA QQ3 / EOR #7 / ADC #3 / STA P / LDA QQ4 / ADC #4 / STA Q -- productivity is
   * (inverted economy + 3) * (government + 4) * population, shifted up three places. Both
   * constants are reached through the carry the population's last ADC left, so they are not
   * really 3 and 4.
   */
  MathWorkspace work;
  step = AddWithCarry(static_cast<std::uint8_t>(data.economy ^ 0x07u), 3, step.carry);
  work.p = step.value;

  step = AddWithCarry(data.government, 4, step.carry);
  work.q = step.value;

  // 6502: JSR MULTU twice -- (A P) = P * Q, then that product times the population.
  std::uint8_t productHigh = MultiplyUnsigned(work);
  work.q = data.population;
  productHigh = MultiplyUnsigned(work);

  // 6502: ASL P / ROL A, three times -- a multiply by eight across the sixteen-bit product.
  for (int shift = 0; shift < 3; ++shift)
  {
    const ShiftResult lowHalf = RotateLeft(work.p, false);
    work.p = lowHalf.value;
    productHigh = RotateLeft(productHigh, lowHalf.carry).value;
  }

  data.productivity = static_cast<std::uint16_t>((static_cast<std::uint16_t>(productHigh) << 8) | work.p);
  return data;
}

namespace
{
/// 6502: the EOR #255 / ADC #1 that follows a borrow -- a negate reached with carry clear.
[[nodiscard]] std::uint8_t AbsoluteDifference(std::uint8_t _a, std::uint8_t _b) noexcept
{
  const std::uint16_t difference = static_cast<std::uint16_t>(_a) - _b;
  const std::uint8_t value = static_cast<std::uint8_t>(difference);
  if (difference < 0x100u)
  {
    return value;
  }
  return AddWithCarry(static_cast<std::uint8_t>(value ^ 0xFFu), 1, false).value;
}
} // namespace

NearestSystem FindNearestSystem(const SystemSeeds& _galaxy, std::uint8_t _crosshairX, std::uint8_t _crosshairY,
                                std::uint8_t _currentX, std::uint8_t _currentY) noexcept
{
  // 6502: JSR TT81 -- the search always starts from the galaxy's own seeds, not from wherever
  // the seeds happen to be.
  SystemSeeds seeds = _galaxy;

  NearestSystem best;
  std::uint8_t bestMetric = 0x7F; // 6502: LDY #127 / STY T
  std::uint8_t index = 0;

  for (;;)
  {
    /*
     * 6502: TT130. A system's galactic coordinates are two of its seed bytes: x is byte 3 and y
     * is byte 1. Nothing computes them -- they simply are the seed, which is why moving one
     * system along moves you across the galaxy.
     */
    const std::uint8_t dx = AbsoluteDifference(seeds.bytes[3], _crosshairX) >> 1;
    const std::uint8_t dy = AbsoluteDifference(seeds.bytes[1], _crosshairY) >> 1;

    // 6502: CLC / ADC S / CMP T / BCS TT135 -- nearer than the best so far, and strictly so.
    const std::uint8_t metric = AddWithCarry(dy, dx, false).value;
    if (metric < bestMetric)
    {
      bestMetric = metric;
      best.seeds = seeds;
      best.index = index;
    }

    NextSystem(seeds);

    // 6502: INC U / BNE TT130 -- 256 systems, counted by a byte that wraps to zero.
    ++index;
    if (index == 0)
    {
      break;
    }
  }

  best.x = best.seeds.bytes[3];
  best.y = best.seeds.bytes[1];

  /*
   * 6502: TT139 onwards -- the real distance, which is a different measurement from the one the
   * search just used. dx is squared whole; dy is HALVED first, then squared.
   */
  MathWorkspace work;
  const std::uint8_t high = SquareUnsigned(work, AbsoluteDifference(best.x, _currentX));
  const std::uint8_t squaredHigh = high;
  const std::uint8_t squaredLow = work.p;

  const std::uint8_t halfDy = static_cast<std::uint8_t>(AbsoluteDifference(best.y, _currentY) >> 1);
  const std::uint8_t secondHigh = SquareUnsigned(work, halfDy);

  // 6502: CLC / ADC K / STA Q / PLA / ADC K+1 / BCC / LDA #255 -- the sum saturates rather than
  // wrapping, because a distance that wrapped would read as very close indeed.
  const AddResult sumLow = AddWithCarry(work.p, squaredLow, false);
  const AddResult sumHigh = AddWithCarry(secondHigh, squaredHigh, sumLow.carry);

  work.q = sumLow.value;
  work.r = sumHigh.carry ? std::uint8_t{ 255 } : sumHigh.value;

  // 6502: JSR LL5 -- Q becomes the square root of (R Q).
  SquareRoot(work);

  // 6502: ASL A / ROL QQ8+1 twice -- the answer times four, as a sixteen-bit value.
  std::uint8_t distanceLow = work.q;
  std::uint8_t distanceHigh = 0;
  for (int shift = 0; shift < 2; ++shift)
  {
    const ShiftResult shifted = RotateLeft(distanceLow, false);
    distanceLow = shifted.value;
    distanceHigh = RotateLeft(distanceHigh, shifted.carry).value;
  }
  best.distance = static_cast<std::uint16_t>((static_cast<std::uint16_t>(distanceHigh) << 8) | distanceLow);

  // 6502: JMP TT24 -- the routine does not return, it continues into the data generator.
  best.data = GenerateSystemData(best.seeds);
  return best;
}

void PrintSystemName(TokenPrinter& _printer, SystemSeeds& _seeds) noexcept
{
  // 6502: TT53 -- the seeds are saved to QQ19 and put back at the end, because printing a name
  // must not move the universe on.
  const SystemSeeds saved = _seeds;

  /*
   * 6502: LDY #3 / BIT QQ15 / BVS / DEY -- bit 6 of the first seed byte decides whether the name
   * has four letter-pairs or three. BIT tests it without loading it, which is why the test reads
   * as an overflow branch and has nothing to do with arithmetic.
   */
  int pairs = ((_seeds.bytes[0] & 0x40u) != 0u) ? 4 : 3;

  for (int pair = 0; pair < pairs; ++pair)
  {
    // 6502: LDA QQ15+5 / AND #%00011111 / BEQ -- a zero means this pair is simply skipped, which
    // is how the game gets names of odd length out of a fixed number of twists.
    const std::uint8_t token = static_cast<std::uint8_t>(_seeds.bytes[5] & 0x1Fu);
    if (token != 0)
    {
      // 6502: ORA #%10000000 -- the high bit tells TT27 this is a letter pair, not a character.
      _printer.Print(static_cast<std::uint8_t>(token | 0x80u));
    }

    TwistSeeds(_seeds);
  }

  _seeds = saved;
}

} // namespace Elite

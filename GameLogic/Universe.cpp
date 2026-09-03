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

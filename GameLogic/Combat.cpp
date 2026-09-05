#include "pch.h"

#include "Combat.h"

#include "Arith.h"
#include "EliteTypes.h"
#include "Messages.h"
#include "ShipBlueprint.h"

namespace Elite
{
  namespace
  {
    /*
     * 6502: the shape both explosions share -- `LDX #11`, four compares, an `INX` after each.
     *
     * Written once because it is written twice: `EXNO` and `EXNO2` differ only in their four
     * thresholds and in what they hand `NOISE2`, and the arithmetic below them is instruction for
     * instruction the same. The result runs 11 to 15 and the nearest hit is the loudest.
     */
    [[nodiscard]] std::uint8_t Volume(std::uint8_t _distance, std::uint8_t _first, std::uint8_t _second, std::uint8_t _third,
                                      std::uint8_t _fourth) noexcept
    {
      std::uint8_t level = 11u;

      if (_distance < _first)
      {
        ++level;
        if (_distance < _second)
        {
          ++level;
          if (_distance < _third)
          {
            ++level;
            if (_distance < _fourth)
            {
              ++level;
            }
          }
        }
      }

      // 6502: TXA / ASL A / ASL A / ASL A / ASL A / ORA #3.
      return static_cast<std::uint8_t>((level << 4) | 3u);
    }

    /// 6502: INWK+7 -- the z high byte, which is how far away the thing being hit is.
    inline constexpr std::size_t SHIP_Z_HIGH = 7;

    /// 6502: INWK+8 -- the z sign, which is what `OOPS` reads to pick a shield.
    inline constexpr std::size_t SHIP_Z_SIGN = 8;
  } // namespace

  std::uint8_t ExplosionVolume(std::uint8_t _distance) noexcept
  {
    // 6502: CMP #8 / CMP #4 / CMP #3 / CMP #2.
    return Volume(_distance, 8u, 4u, 3u, 2u);
  }

  std::uint8_t KillVolume(std::uint8_t _distance) noexcept
  {
    // 6502: CMP #16 / CMP #8 / CMP #6 / CMP #3 -- a kill is heard from twice as far as a hit.
    return Volume(_distance, 16u, 8u, 6u, 3u);
  }

  std::uint8_t PlayHitSound(const ShipBlock& _work, DashboardEffects& _effects) noexcept
  {
    const std::uint8_t sustain = ExplosionVolume(_work[SHIP_Z_HIGH]);

    // 6502: LDY #sfxhit / LDX #208 / JMP NOISE2.
    (void)_effects.PlaySoundPitched(SOUND_SHIP_EXPLODING, sustain, EXPLOSION_PITCH_HIT);
    return sustain;
  }

  std::uint8_t RecordKill(FlightScreen& _screen, DashboardEffects& _effects, std::uint8_t _type) noexcept
  {
    CommanderBlock& commander = _screen.commander;

    /*
     * 6502: LDA TALLYL / CLC / ADC KWL%-1,X / STA TALLYL / LDA TALLY / ADC KWH%-1,X / STA TALLY /
     * BCC davidscockup / INC TALLY+1.
     *
     * Twenty-four bits with the bottom eight a fraction, so most kills add nothing visible: a
     * Sidewinder is worth a fraction and it takes several of them to move the number the status
     * screen prints. The carry out of the middle byte is what reaches the top one, and the label
     * the original gives that branch says what its author thought of the arrangement.
     */
    const std::uint16_t table = static_cast<std::uint16_t>(SHIP_KILL_FRACTION + _type - 1u);
    const AddResult fraction = AddWithCarry(commander.At(Field::KillsLow), ShipByte(table), false);
    commander.At(Field::KillsLow) = fraction.value;

    const AddResult whole =
      AddWithCarry(commander.At(Field::Kills), ShipByte(static_cast<std::uint16_t>(table + SHIP_TYPE_COUNT)), fraction.carry);
    commander.At(Field::Kills) = whole.value;

    if (whole.carry)
    {
      commander.bytes[static_cast<std::size_t>(Field::Kills) + 1u] =
        static_cast<std::uint8_t>(commander.bytes[static_cast<std::size_t>(Field::Kills) + 1u] + 1u);

      // 6502: LDA #101 / JSR MESS -- "RIGHT ON COMMANDER", once every 256 whole kills.
      ShowMessage(_screen.canvas, _screen.printer, _screen.text, _screen.extended, _screen.message, MESSAGE_RIGHT_ON_COMMANDER,
                  _screen.view);
    }

    // 6502: davidscockup -- and the noise is the same shape as EXNO's with wider thresholds.
    const std::uint8_t sustain = KillVolume(_screen.work[SHIP_Z_HIGH]);
    (void)_effects.PlaySoundPitched(SOUND_EXPLOSION, sustain, EXPLOSION_PITCH_KILL);
    return sustain;
  }

  bool TakeDamage(FlightScreen& _screen, DashboardEffects& _effects, const ShipBlock& _target, std::uint8_t _damage, bool _carryIn) noexcept
  {
    FlightStatus& status = _screen.status;

    /*
     * 6502: STA T / LDX #0 / LDY #8 / LDA (INF),Y / BMI OO1.
     *
     * AND THE `SBC` BELOW RUNS ON THE CALLER'S CARRY. Neither entry sets it: part 10 arrives at
     * `.MA63` from `MA67` with the carry clear and from `MA58` with the carry holding bit 0 of the
     * ship's own energy, because `LDA INWK+35 / SEC / ROR A` is what computed the damage. So the
     * shield loses `_damage` or one more than `_damage` depending on a bit of the thing that hit
     * it (§6.87). `LDX #0` is dead -- both paths that read X load it again first.
     */
    const bool fromBehind = (_target[SHIP_Z_SIGN] & 0x80u) != 0u;
    std::uint8_t& shield = fromBehind ? status.aftShield : status.forwardShield;

    const SubResult left = SubtractWithCarry(shield, _damage, _carryIn);
    if (left.carry)
    {
      // 6502: STA FSH / RTS -- the shield absorbed it and the banks are untouched.
      shield = left.value;
      return true;
    }

    // 6502: OO2 and OO5 -- LDX #0 / STX FSH, then OO3 with the negative remainder still in A.
    shield = 0u;

    /*
     * 6502: OO3 -- ADC ENERGY / STA ENERGY / BEQ P%+4 / BCS P%+5 / JMP DEATH.
     *
     * The `BEQ` is the trap: it jumps FORWARD past the `BCS`, onto the `JMP DEATH`, so energy that
     * lands exactly on zero kills the player even though the addition carried. Two conditions,
     * one of them counter-intuitive, in five bytes.
     */
    const AddResult banks = AddWithCarry(left.value, status.energy, false);
    status.energy = banks.value;

    if (banks.value == 0u || !banks.carry)
    {
      return false; // 6502: JMP DEATH
    }

    /*
     * 6502: JSR EXNO3 / JMP OUCH -- and `EXNO3` is `LDY #sfxexpl / BNE NOISE`, so the carry `OUCH`
     * opens its `JSR DORND` on is the sound routine's answer (§6.88).
     *
     * The carry going IN is SET, and it is the `BCS P%+5` four instructions above that sets it:
     * this line is only reached when the energy addition carried. A silent build passes it
     * straight through (§6.99), so with sound off the roll below is the one a carry of 1 gives.
     */
    const bool heard = _effects.PlaySound(SOUND_EXPLOSION, true);
    DamageEquipment(_screen, heard);
    return true;
  }

  void DamageEquipment(FlightScreen& _screen, bool _carryIn) noexcept
  {
    // 6502: JSR DORND / BMI out -- half the hits break nothing at all.
    const RngResult roll = _screen.rng.Next(_carryIn);
    if ((roll.value & 0x80u) != 0u)
    {
      return;
    }

    // 6502: CPX #22 / BCS out -- X is the generator's other byte, and 22 is where the block ends.
    const std::uint8_t slot = roll.previous;
    if (slot >= 22u)
    {
      return;
    }

    CommanderBlock& commander = _screen.commander;
    const std::size_t byte = static_cast<std::size_t>(Field::CargoHold) + slot;

    // 6502: LDA QQ20,X / BEQ out -- nothing there to break.
    if (commander.bytes[byte] == 0u)
    {
      return;
    }

    // 6502: LDA DLY / BNE out -- a message already up suppresses this one, and the routine with it.
    if (_screen.message.delay != 0u)
    {
      return;
    }

    _screen.message.append = 3u; // 6502: LDY #3 / STY de -- "... DESTROYED"

    // 6502: STA QQ20,X -- and A is `DLY`, which is zero because that is how we got here.
    commander.bytes[byte] = 0u;

    /*
     * 6502: CPX #17 / BCS ou1 / TXA / ADC #208 -- and the carry the compare left is part of the
     * sum. Below seventeen it is clear and the token is X + 208, a cargo name; at nineteen and
     * above it is set and the token is X + 94, an equipment name. Seventeen and eighteen have
     * messages of their own because their names are not in either run.
     */
    std::uint8_t token = 0;
    if (slot < 17u)
    {
      token = static_cast<std::uint8_t>(slot + 208u);
    }
    else if (slot == 17u)
    {
      token = MESSAGE_ECM_DESTROYED; // 6502: ou2 -- LDA #108
    }
    else if (slot == 18u)
    {
      token = MESSAGE_SCOOPS_DESTROYED; // 6502: ou3 -- LDA #111
    }
    else
    {
      token = static_cast<std::uint8_t>(slot + 94u);
    }

    ShowMessage(_screen.canvas, _screen.printer, _screen.text, _screen.extended, _screen.message, token, _screen.view);
  }

  void StopEnergyBomb(ScreenState& _screen) noexcept
  {
    _screen.upperBitmapMode = 0xC0u; // 6502: LDA #%11000000 / STA moonflower
    _screen.backgroundFlash = 0u;    // 6502: LDA #0 / STA welcome
  }

} // namespace Elite

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
     * The shape both explosions share -- a level starting at 11 and stepped up once for each
     * threshold the distance comes under.
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

      // The level shifted into the high nibble with 3 in the low one.
      return static_cast<std::uint8_t>((level << 4) | 3u);
    }

  } // namespace

  std::uint8_t ExplosionVolume(std::uint8_t _distance) noexcept
  {
    // EXNO's four thresholds.
    return Volume(_distance, 8u, 4u, 3u, 2u);
  }

  std::uint8_t KillVolume(std::uint8_t _distance) noexcept
  {
    // EXNO2's four thresholds -- a kill is heard from twice as far as a hit.
    return Volume(_distance, 16u, 8u, 6u, 3u);
  }

  std::uint8_t PlayHitSound(const Ship& _work, SoundBuffer& _sound) noexcept
  {
    const std::uint8_t sustain = ExplosionVolume(_work.z.hi);

    /*
     * The hit sound at pitch 208 -- AND THE CARRY IS ALWAYS CLEAR (M3-b-2a).
     *
     * `NOISE2` runs on the caller's flag and the seam had no way to carry one, so the port passed
     * false and the plan recorded the gap. The four shifts that build the byte above are what set
     * it: they shift a value `quiet`'s ladder leaves between 11 and 15, so it is under 128 before
     * the last one and the carry that comes out is zero every time. The OR touches no flag.
     */
    return PlaySoundEffectPitched(_sound, SoundEffect::ShipExploding, sustain, EXPLOSION_PITCH_HIT, false).a;
  }

  std::uint8_t RecordKill(Universe& _universe, Ports& _ports, ShipType _type) noexcept
  {
    Commander& commander = _universe.commander;

    /*
     * The kill worth added into the tally, fraction first, carrying up through three bytes.
     *
     * Twenty-four bits with the bottom eight a fraction, so most kills add nothing visible: a
     * Sidewinder is worth a fraction and it takes several of them to move the number the status
     * screen prints. The carry out of the middle byte is what reaches the top one, and the label
     * the original gives that branch says what its author thought of the arrangement.
     */
    const KillWorth worth = KillWorthFor(_type);
    const AddResult fraction = AddWithCarry(commander.killsFraction, worth.fraction, false);
    commander.killsFraction = fraction.value;

    const AddResult whole =
      AddWithCarry(commander.kills.lo, worth.whole, fraction.carry);
    commander.kills.lo = whole.value;

    if (whole.carry)
    {
      commander.kills.hi =
        static_cast<std::uint8_t>(commander.kills.hi + 1u);

      // Token 101 through `MESS` -- "RIGHT ON COMMANDER", once every 256 whole kills.
      ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, MESSAGE_RIGHT_ON_COMMANDER,
                  _universe.view, &_universe.backdrop);
    }

    // `davidscockup` -- the same shape as `EXNO`'s noise with wider thresholds, down to the carry:
    // `quiet2`'s ladder leaves the level between 11 and 15 and four shifts cannot carry out of
    // that, so `NOISE2` is reached with the flag clear here as well.
    const std::uint8_t sustain = KillVolume(_universe.work.z.hi);
    return PlaySoundEffectPitched(_universe.sound, SoundEffect::Explosion, sustain, EXPLOSION_PITCH_KILL, false).a;
  }

  bool TakeDamage(Universe& _universe, Ports& _ports, const Ship& _target, std::uint8_t _damage,
                  bool _carryIn) noexcept
  {
    FlightStatus& status = _universe.status;

    /*
     * The damage stashed, then byte 8 of the ship's own block decides which shield -- a
     * negative sign branches to `OO1` and the aft one.
     *
     * AND THE `SBC` BELOW RUNS ON THE CALLER'S CARRY. Neither entry sets it: part 10 arrives at
     * `.MA63` from `MA67` with the carry clear and from `MA58` with the carry holding bit 0 of the
     * ship's own energy, because the damage is that energy rotated right with the carry set, and
     * bit 0 falls out into the carry. So the shield loses `_damage` or one more than `_damage`
     * depending on a bit of the thing that hit it (§6.87). The zero loaded into X at the top is
     * dead -- both paths that read X load it again first.
     */
    const bool fromBehind = (_target.z.sgn & 0x80u) != 0u;
    std::uint8_t& shield = fromBehind ? status.aftShield : status.forwardShield;

    const SubResult left = SubtractWithCarry(shield, _damage, _carryIn);
    if (left.carry)
    {
      // The shield keeps what is left and the banks are untouched.
      shield = left.value;
      return true;
    }

    // OO2 and OO5 -- the shield to zero, and on into OO3 with the negative remainder still
    // in the accumulator.
    shield = 0u;

    /*
     * The remainder added to the energy banks, then two branches over a jump to
     * `DEATH`.
     *
     * The zero branch is the trap: it jumps FORWARD past the carry branch, onto the jump to
     * `DEATH`, so energy that lands exactly on zero kills the player even though the addition
     * carried. Two conditions, one of them counter-intuitive, in five bytes.
     */
    const AddResult banks = AddWithCarry(left.value, status.energy, false);
    status.energy = banks.value;

    if (banks.value == 0u || !banks.carry)
    {
      return false;
    }

    /*
     * EXNO3 then OUCH -- and `EXNO3` is nothing but the explosion sound, so the carry `OUCH`
     * opens its first random roll on is the sound routine's answer (§6.88).
     *
     * The carry going IN is SET, and it is the carry branch four instructions above that sets it:
     * this line is only reached when the energy addition carried. A silent build passes it
     * straight through (§6.99), so with sound off the roll below is the one a carry of 1 gives.
     */
    const bool heard = PlaySoundEffect(_universe.sound, SoundEffect::Explosion, true).carry;
    DamageEquipment(_universe, _ports, heard);
    return true;
  }

  void DamageEquipment(Universe& _universe, Ports& _ports, bool _carryIn) noexcept
  {
    // A random byte, and a negative one leaves -- half the hits break nothing at all.
    const RngResult roll = _universe.rng.Next(_carryIn);
    if ((roll.value & 0x80u) != 0u)
    {
      return;
    }

    // The generator's OTHER byte is the slot, and 22 is where the block ends.
    const std::uint8_t slot = roll.previous;
    if (slot >= 22u)
    {
      return;
    }

    // QQ20,X -- and X runs to 21, past the seventeen goods into the five fittings after
    // them. `cargoHold[slot]` was that until M1-d typed the hold, and then it was an out-of-range
    // subscript for every fitting `OUCH` could break (plan §6.158).
    Commander& commander = _universe.commander;
    std::uint8_t& held = commander.HoldOrFitting(slot);

    // Nothing in the slot, nothing to break.
    if (held == 0u)
    {
      return;
    }

    // A message already up suppresses this one, and the routine with it.
    if (_universe.message.delay != 0u)
    {
      return;
    }

    _universe.message.append = 3u; // "... DESTROYED"

    // The slot cleared with `DLY`, which is zero because that is how we got here.
    held = 0u;

    /*
     * The slot compared against seventeen, branching to `ou1` above it, and the CARRY that
     * compare leaves is part of the sum. Below seventeen it is clear and the token is the slot plus
     * 208, a cargo name; at nineteen and above it is set and the token is the slot plus 94, an
     * equipment name. Seventeen and eighteen have messages of their own because their names are
     * not in either run.
     */
    std::uint8_t token = 0;
    if (slot < 17u)
    {
      token = static_cast<std::uint8_t>(slot + 208u);
    }
    else if (slot == 17u)
    {
      token = MESSAGE_ECM_DESTROYED; // Token 108
    }
    else if (slot == 18u)
    {
      token = MESSAGE_SCOOPS_DESTROYED; // Token 111
    }
    else
    {
      token = static_cast<std::uint8_t>(slot + 94u);
    }

    ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, token, _universe.view, &_universe.backdrop);
  }

  void StopEnergyBomb(ScreenState& _screen) noexcept
  {
    _screen.upperBitmapMode = 0xC0u; // moonflower
    _screen.backgroundFlash = 0u;    // welcome
  }

} // namespace Elite

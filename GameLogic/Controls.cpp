#include "pch.h"

#include "Controls.h"

#include "EliteTypes.h"
#include "FlightLoop.h" // for the KY12..KY20 offsets `RDKEY`'s `QQ11` tail clears
#include "LookupTables.h"
#include "Ports.h"
#include "Tactics.h" // for DOCKIT, which `DOKEY`'s autopilot path calls (M6-0-h-3)
#include "Universe.h"

namespace Elite
{

  namespace
  {
    /// The shared tail both routines fall into when the new rate has crossed the
    /// centre. The branch that leaves it is unconditional: 128 is negative.
    [[nodiscard]] std::uint8_t Recentre(std::uint8_t _value, std::uint8_t _recentreDisabled) noexcept
    {
      return (_recentreDisabled != 0u) ? _value : std::uint8_t{128u};
    }
  } // namespace

  std::uint8_t BumpControl(std::uint8_t _value, std::uint8_t _amount, std::uint8_t _recentreDisabled) noexcept
  {
    // An eight-bit addition that saturates at 255 rather than wrapping.
    const std::uint16_t sum = static_cast<std::uint16_t>(_value) + _amount;
    if (sum > 0xFFu)
    {
      return 0xFFu;
    }

    const std::uint8_t bumped = static_cast<std::uint8_t>(sum);

    // .RE2 BPL djd1 -- and the flag is from the `TAX`, so it is the new rate's bit 7. A
    // clamped 255 has it set, which is why the overflow path never re-centres.
    return ((bumped & 0x80u) == 0u) ? Recentre(bumped, _recentreDisabled) : bumped;
  }

  std::uint8_t ReduceControl(std::uint8_t _value, std::uint8_t _amount, std::uint8_t _recentreDisabled) noexcept
  {
    // A subtraction that clamps at 1 on a borrow -- and the carry is set when there was no
    // borrow, so an exact match escapes the clamp and leaves zero behind.
    if (_amount > _value)
    {
      return 1u;
    }

    const std::uint8_t reduced = static_cast<std::uint8_t>(_value - _amount);

    // The opposite half of the slider from `BUMP2`'s test, and it falls into `djd1`
    // rather than branching to it.
    return ((reduced & 0x80u) != 0u) ? Recentre(reduced, _recentreDisabled) : reduced;
  }

  CrosshairStep ReadCrosshairKeys(const KeyLogger& _keys) noexcept
  {
    // Either SHIFT, and the entries are &FF while held, so folding one in turns the step
    // from 1 into &FF, which is -1.
    const std::uint8_t shifted = static_cast<std::uint8_t>(_keys[KEY_SHIFT_LEFT] | _keys[KEY_SHIFT_RIGHT]);

    // RETURN held is four times the step, and the test is on bit 7 because that is what the
    // scan writes.
    const bool fast = (_keys[KEY_CROSSHAIR_FAST] & 0x80u) != 0u;

    auto step = [fast](std::uint8_t _value) noexcept { return fast ? static_cast<std::uint8_t>(_value << 2u) : _value; };

    CrosshairStep moved;

    // Nothing held leaves the accumulator as the zero the load produced, which is
    // why the branch goes to the shift with it already right.
    moved.x = step((_keys[KEY_CURSOR_X] != 0u) ? static_cast<std::uint8_t>(1u | shifted) : std::uint8_t{0});

    /*
     * The same again for y, with a fold against %11111110 at the end.
     *
     * That fold is INSIDE the branch, so it runs only when the key is held: a released key falls
     * to `noymove` with A = 0 and stays zero, while a held one becomes &FF or 1. Applying it to the
     * zero as well -- which is what writing this as one expression invites -- would move the
     * crosshairs by &FE every pass with nothing pressed.
     */
    moved.y = step((_keys[KEY_CURSOR_Y] != 0u) ? static_cast<std::uint8_t>((1u | shifted) ^ 0xFEu) : std::uint8_t{0});

    return moved;
  }

  namespace
  {
    /*
     * The keys `DOKEY` ignores on every screen but the space view -- `RDKEY`'s answer to
     * `QQ11 <> 0`.
     *
     * The bomb, the pod, the missiles, the E.C.M., the warp and the docking computer are the keys
     * that DO something rather than steer; with a chart or a market on screen the scan reports them
     * as unheld regardless of the keyboard. `DOKEY`'s six steering keys are deliberately not here.
     */
    constexpr std::array<std::size_t, 9> NON_STEERING_KEYS = {
      KEY_ENERGY_BOMB, KEY_ESCAPE_POD, KEY_ARM_MISSILE,      KEY_UNARM_MISSILE,  KEY_FIRE_MISSILE,
      KEY_ECM,         KEY_WARP,       KEY_DOCKING_COMPUTER, KEY_CANCEL_DOCKING,
    };

    /// RDKEY masks sprite 1 off while the matrix is scanned, and it is not one of the four
    /// the sights use.
    constexpr std::uint8_t RDKEY_SPRITE_MASK = 0b11111101;
  } // namespace

  TitleKey ScanKeyboard(KeyLogger& _keys, VideoState& _video, MemoryMap& _map, std::uint8_t _view, Keyboard& _keyboard) noexcept
  {
    SetMemoryMap(_map, MEMORY_MAP_IO);           // SETL1 with the I/O map
    ApplyMaskSprites(_video, RDKEY_SPRITE_MASK); // Sprite 1 off
    _keys.fill(0u);

    // The matrix walked from &40 downwards, one entry a key.
    TitleKey answer;
    for (std::uint8_t key = static_cast<std::uint8_t>(_keys.size()); key-- > 0u;)
    {
      if (_keyboard.Held(key))
      {
        _keys[key] = 0xFFu; // The entry stepped down, on a byte that has just been zeroed
        answer.pressed = true;
        answer.key = key;
      }
    }

    // With anything but the space view up, the nine keys that act rather than
    // steer are forgotten.
    if (_view != 0u)
    {
      for (const std::size_t index : NON_STEERING_KEYS)
      {
        _keys[index] = 0u;
      }
    }

    /*
     * AND THE STEERING KEYS GO ON A CHART, which is the port's rule and NOT `RDKEY`'s.
     *
     * On a C64 the two sets never collide: `<`, `>`, `X` and `S` steer and the cursor keys move the
     * crosshairs, so `RDKEY` has no reason to drop the steering entries and does not. This port's
     * map is a modern one (ADR-005 §4) and the arrows do both jobs, so one of them has to give way
     * while a chart is up -- and it is the steering, because a chart is the one screen where the
     * arrows are what you aim with. The alternative is a ship that rolls while you read the map.
     *
     * It is here rather than in `KeyMap` because this is where the game itself sorts keys by view,
     * one statement above; and it is marked as the port's own so nobody looks for it in `RDKEY`.
     */
    if (IsChartView(_view))
    {
      for (const std::size_t index : {KEY_ROLL_LEFT, KEY_ROLL_RIGHT, KEY_PITCH_UP, KEY_PITCH_DOWN})
      {
        _keys[index] = 0u;
      }
    }

    SetMemoryMap(_map, MEMORY_MAP_RAM); // SETL1 with the RAM map
    return answer;
  }

  std::uint8_t ReadKey(Universe& _universe, Ports& _ports) noexcept
  {
    for (;;)
    {
      // Two frames of `DELAY`, the debounce.
      _ports.present.WaitFrames(TT217_DEBOUNCE_FRAMES);

      // Back to t -- a key already down when the prompt appeared is not the answer.
      if (ScanKeyboard(_universe.keys, _universe.video, _universe.memoryMap, _universe.view, _ports.keyboard).pressed)
      {
        continue;
      }

      // Now wait for one.
      for (;;)
      {
        const TitleKey scan = ScanKeyboard(_universe.keys, _universe.video, _universe.memoryMap, _universe.view, _ports.keyboard);
        if (scan.pressed)
        {
          // Through `TRANTABLE` to the character, which is what every caller compares against.
          return KEY_TRANSLATION[scan.key];
        }
        _ports.present.Present(); // the port's: the matrix is the window's table, and a present is what fills it
      }
    }
  }

  void ReadFlightControls(Universe& _universe, Ports& _ports) noexcept
  {
    KeyLogger& keys = _universe.keys;
    ControlState& control = _universe.control;
    const ControlOptions& options = _universe.options;
    Ship& work = _universe.work;
    FlightState& flight = _universe.flight;

    // RDKEY, whose answer `DOKEY` does not read.
    static_cast<void>(ScanKeyboard(keys, _universe.video, _universe.memoryMap, _universe.view, _ports.keyboard));

    // With the docking computer off, what is held down is what the player is
    // holding down.
    if (control.dockingComputer != 0u)
    {
      ClearShip(work);

      /*
       * 96 into the nose's z, and 96 with its top bit into the side's x and into `TYPE`.
       *
       * `ZINF` has just set `INWK+14` to 96 WITH the sign bit and `INWK+22` to 96 without it, and
       * this puts them back the other way round -- so the block the autopilot is handed is not the
       * one `ZINF` makes, and the two instructions that differ are easy to read as a repeat.
       */
      work.nose.z.hi = 96u;
      work.side.x.hi = static_cast<std::uint8_t>(96u | 0x80u);
      flight.type = TypeOf(static_cast<std::uint8_t>(96u | 0x80u));

      work.speed = flight.speed; // DELTA into the block's speed byte

      // DOCKIT over the block `auton` just built in `INWK`, whose slot is 0: `INF`
      // points at the player's own block on this path, which is what `DOCKIT` steers.
      RunDockingComputer(_universe, _ports, 0u);

      // The autopilot is not allowed to fly faster than 22, whatever it asked for.
      flight.speed = (work.speed < 22u) ? work.speed : std::uint8_t{22u};

      // The acceleration becomes "?" held down or Space held down, and neither if it is
      // zero -- the sign picks which, and DK11 is where a zero goes.
      if (work.acceleration != 0u)
      {
        const std::size_t slot = ((work.acceleration & 0x80u) != 0u) ? KEY_SLOW_DOWN : KEY_SPEED_UP;
        keys[slot] = 0xFFu;
      }

      // ---- .DK11: the roll ------------------------------------------------------------------
      //
      // The roll counter doubled, and a zero result goes to DK12.
      const ShiftResult roll = RotateLeftValue(work.rollCounter, false);
      work.rollCounter = roll.value;

      if (roll.value == 0u)
      {
        control.roll = 128u; // DK12 with the accumulator still 128 -- nothing asked, so centred
      }
      else
      {
        // The carry out of that doubling is the OLD sign, so it picks the direction.
        const std::size_t slot = roll.carry ? KEY_ROLL_RIGHT : KEY_ROLL_LEFT;

        // DK14 -- and this test is on the NEW sign, so it asks whether doubling the request
        // overflowed, which is what "a big request" means here.
        if ((roll.value & 0x80u) != 0u)
        {
          control.roll = 64u; // 64 into JSTX
          keys[slot] = 0u;    // DK14 stores it -- and the key is released
        }
        else
        {
          keys[slot] = 128u; // DK14 stores it with the accumulator still 128
        }
        // DK12 writes `JSTX` back over what is already there.
      }

      // ---- .DK13: the pitch, and it is not the same shape ------------------------------------
      //
      // The same shape on the pitch counter, and the carry picks the key.
      //
      // No second sign test and no direct write: the pitch has no large-request path, and the
      // carry test is
      // the other way round from the roll's.
      const ShiftResult pitch = RotateLeftValue(work.pitchCounter, false);
      work.pitchCounter = pitch.value;

      if (pitch.value == 0u)
      {
        control.pitch = 128u;
      }
      else
      {
        keys[pitch.carry ? KEY_PITCH_UP : KEY_PITCH_DOWN] = 128u;
      }
    }

    // ---- .DK15: the keys, however they came to be pressed -------------------------------------

    // `BUMP2` for one key and `REDU2` for the other, by 14 -- and the accumulator survives
    // both calls, which is why the 14 is loaded once.
    std::uint8_t roll = control.roll;
    if (keys[KEY_ROLL_LEFT] != 0u)
    {
      roll = BumpControl(roll, CONTROL_STEP, options.recentreDisabled);
    }
    if (keys[KEY_ROLL_RIGHT] != 0u)
    {
      roll = ReduceControl(roll, CONTROL_STEP, options.recentreDisabled);
    }
    control.roll = roll;

    // The same pair on `JSTY`, and the pitch keys are the other way round from the roll's.
    std::uint8_t pitch = control.pitch;
    if (keys[KEY_PITCH_UP] != 0u)
    {
      pitch = ReduceControl(pitch, CONTROL_STEP, options.recentreDisabled);
    }
    if (keys[KEY_PITCH_DOWN] != 0u)
    {
      pitch = BumpControl(pitch, CONTROL_STEP, options.recentreDisabled);
    }
    control.pitch = pitch;

    /*
     * The joystick's own re-centring, which the keyboard does not get.
     *
     * A stick springs back to the middle on its own and the game copies that: with no direction
     * held on an axis, the rate is put back to 128 outright rather than damped towards it. Off
     * while the docking computer is flying, because the autopilot's synthetic presses would be
     * cancelled by it.
     */
    if (options.joystick != 0u && control.dockingComputer == 0u)
    {
      // Neither roll key held, so the roll goes back to centre.
      if ((keys[KEY_ROLL_LEFT] | keys[KEY_ROLL_RIGHT]) == 0u)
      {
        control.roll = 128u;
      }

      // termite -- and the same for the pitch.
      if ((keys[KEY_PITCH_UP] | keys[KEY_PITCH_DOWN]) == 0u)
      {
        control.pitch = 128u;
      }
    }

    // .ant, and no RTS -- it falls into `DK4`, which is the docked dispatcher and not this
    // unit's. A caller of `DOKEY` gets that as well, and the call site does not say so.
  }

  void DrawLaserSights(Canvas& _canvas, const Commander& _commander, TrumbleSprites& _trumbles, std::uint8_t _view, VideoState& _video,
                       MemoryMap& _map) noexcept
  {
    SetMemoryMap(_map, MEMORY_MAP_IO); // SETL1 with the I/O map

    // No laser in this view, and nothing to draw. `SIGHT` tests for the pulse, beam
    // and military
    // powers in that order; the mining laser is not tested for at all and gets the fourth sprite
    // by elimination.
    const Laser laser = _commander.lasers[_view];

    if (laser.Fitted())
    {
      /*
       * Three comparisons, each stepping the sprite index on when it misses.
       *
       * A chain of three tests and a fall-through, which is the same "anything else is the last
       * one" shape the status screen names lasers with -- so a laser power the game does not have
       * gets the mining laser's sprite and the mining laser's colour rather than none.
       */
      std::uint8_t pointer = SPRITE_POINTER_BASE;
      if (laser != LASER_PULSE)
      {
        ++pointer;
        if (laser != LASER_BEAM)
        {
          ++pointer;
          if (laser != LASER_MILITARY)
          {
            ++pointer;
          }
        }
      }

      // The same pointer written into both blocks of screen RAM.
      _canvas.Write(SIGHT_SPRITE_CELL, pointer);
      _canvas.Write(SIGHT_SPRITE_CELL_2, pointer);

      // The colour out of `sightcol`, into the VIC-II's register.
      // `sightcol` is an extracted table of bytes and the register takes four bits, so the
      // conversion is the VIC-II's latch and belongs here (slice 5a).
      ApplySightColour(_video, ColourOf(LASER_SIGHT_COLOUR_TABLE[static_cast<std::size_t>(pointer - SPRITE_POINTER_BASE)]));
    }

    // One if a laser was found, and the zero the table read left if not, which is
    // the whole of how the sights get switched off. `SIGHT`'s own since M2-c-3.
    const std::uint8_t sightsBit = laser.Fitted() ? std::uint8_t{1u} : std::uint8_t{0u};

    // The population's high byte, sign off and shifted four times, as a table index.
    const std::uint8_t population = _commander.tribbles.hi;
    const std::size_t index = static_cast<std::size_t>((population & 0x7Fu) >> 4u);

    _trumbles.count = TRUMBLE_COUNT_TABLE[index]; // Through `TRIBTA` into `TRIBCT`

    // `TRIBMA` ORed with the sights bit -- both in one register write.
    ApplySpritesEnabled(_video, static_cast<std::uint8_t>(TRUMBLE_SPRITE_TABLE[index] | sightsBit));

    SetMemoryMap(_map, MEMORY_MAP_RAM); // SETL1 with the RAM map, as a tail call
  }

  void ClearFlightKeys(KeyLogger& _keys) noexcept
  {
    // The loop stops one short, so `KLO+0` is never stored -- and the store after it is
    // NOT that byte: `KL` is a separate address with no C64 reader, so the port has nothing to
    // clear for it (§6.117).
    for (std::size_t index = 1; index <= FLIGHT_KEYS_CLEARED; ++index)
    {
      _keys[index] = 0u;
    }
  }

} // namespace Elite

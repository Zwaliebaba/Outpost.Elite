#include "pch.h"

#include "Controls.h"

#include "EliteTypes.h"
#include "LookupTables.h"

namespace Elite
{

  namespace
  {
    /// 6502: `.djd1 LDA DJD / BNE RE2+2 / LDX #128 / BMI RE2+2` -- the shared tail both routines fall
    /// into when the new rate has crossed the centre. `BMI` after `LDX #128` is unconditional.
    [[nodiscard]] std::uint8_t Recentre(std::uint8_t _value, std::uint8_t _recentreDisabled) noexcept
    {
      return (_recentreDisabled != 0u) ? _value : std::uint8_t{128u};
    }
  } // namespace

  std::uint8_t BumpControl(std::uint8_t _value, std::uint8_t _amount, std::uint8_t _recentreDisabled) noexcept
  {
    // 6502: STA T / TXA / CLC / ADC T / TAX / BCC RE2 / LDX #255.
    const std::uint16_t sum = static_cast<std::uint16_t>(_value) + _amount;
    if (sum > 0xFFu)
    {
      return 0xFFu;
    }

    const std::uint8_t bumped = static_cast<std::uint8_t>(sum);

    // 6502: .RE2 BPL djd1 -- and the flag is from the `TAX`, so it is the new rate's bit 7. A
    // clamped 255 has it set, which is why the overflow path never re-centres.
    return ((bumped & 0x80u) == 0u) ? Recentre(bumped, _recentreDisabled) : bumped;
  }

  std::uint8_t ReduceControl(std::uint8_t _value, std::uint8_t _amount, std::uint8_t _recentreDisabled) noexcept
  {
    // 6502: STA T / TXA / SEC / SBC T / TAX / BCS RE3 / LDX #1 -- and the carry is set when there
    // was no borrow, so an exact match escapes the clamp and leaves zero behind.
    if (_amount > _value)
    {
      return 1u;
    }

    const std::uint8_t reduced = static_cast<std::uint8_t>(_value - _amount);

    // 6502: .RE3 BPL RE2+2 -- the opposite half of the slider from `BUMP2`'s test, and it falls
    // into `djd1` rather than branching to it.
    return ((reduced & 0x80u) != 0u) ? Recentre(reduced, _recentreDisabled) : reduced;
  }

  CrosshairStep ReadCrosshairKeys(const KeyLogger& _keys) noexcept
  {
    // 6502: LDA KLO+&31 / ORA KLO+&C -- either SHIFT, and the entries are &FF while held, so the
    // OR turns the step from 1 into &FF, which is -1.
    const std::uint8_t shifted = static_cast<std::uint8_t>(_keys[KEY_SHIFT_LEFT] | _keys[KEY_SHIFT_RIGHT]);

    // 6502: BIT KLO+&3F / BPL P%+4 / ASL A / ASL A -- RETURN held is four times the step, and the
    // test is on bit 7 because that is what the scan writes.
    const bool fast = (_keys[KEY_CROSSHAIR_FAST] & 0x80u) != 0u;

    auto step = [fast](std::uint8_t _value) noexcept { return fast ? static_cast<std::uint8_t>(_value << 2u) : _value; };

    CrosshairStep moved;

    // 6502: LDA KLO+&3E / BEQ noxmove / LDA #1 / ORA ... -- nothing held leaves A as the zero the
    // load produced, which is why the branch goes to the shift with A already right.
    moved.x = step((_keys[KEY_CURSOR_X] != 0u) ? static_cast<std::uint8_t>(1u | shifted) : std::uint8_t{0});

    /*
     * 6502: LDA KLO+&39 / BEQ noymove / LDA #1 / ORA ... / EOR #%11111110.
     *
     * The `EOR` is INSIDE the branch, so it runs only when the key is held: a released key falls
     * to `noymove` with A = 0 and stays zero, while a held one becomes &FF or 1. Applying it to the
     * zero as well -- which is what writing this as one expression invites -- would move the
     * crosshairs by &FE every pass with nothing pressed.
     */
    moved.y = step((_keys[KEY_CURSOR_Y] != 0u) ? static_cast<std::uint8_t>((1u | shifted) ^ 0xFEu) : std::uint8_t{0});

    return moved;
  }

  void ReadFlightControls(KeyLogger& _keys, ControlState& _control, const ControlOptions& _options, ShipBlock& _work, FlightState& _flight,
                          ControlEffects& _effects) noexcept
  {
    _effects.ScanKeyboard(); // 6502: JSR RDKEY

    // 6502: LDA auto / BEQ DK15 -- with the docking computer off, what is held down is what the
    // player is holding down.
    if (_control.dockingComputer != 0u)
    {
      ClearShipBlock(_work); // 6502: JSR ZINF

      /*
       * 6502: LDA #96 / STA INWK+14 / ORA #%10000000 / STA INWK+22 / STA TYPE.
       *
       * `ZINF` has just set `INWK+14` to 96 WITH the sign bit and `INWK+22` to 96 without it, and
       * this puts them back the other way round -- so the block the autopilot is handed is not the
       * one `ZINF` makes, and the two instructions that differ are easy to read as a repeat.
       */
      _work[14] = 96u;
      _work[22] = static_cast<std::uint8_t>(96u | 0x80u);
      _flight.type = static_cast<std::uint8_t>(96u | 0x80u);

      _work[27] = _flight.delta;          // 6502: LDA DELTA / STA INWK+27
      _effects.RunDockingComputer(_work); // 6502: JSR DOCKIT

      // 6502: LDA INWK+27 / CMP #22 / BCC P%+4 / LDA #22 / STA DELTA -- the autopilot is not
      // allowed to fly faster than 22, whatever it asked for.
      _flight.delta = (_work[27] < 22u) ? _work[27] : std::uint8_t{22u};

      // 6502: LDA #&FF / LDX #(KY1-KLO) / LDY INWK+28 / BEQ DK11 / BMI P%+4 / LDX #(KY2-KLO) /
      // STA KLO,X -- the acceleration becomes "?" held down or Space held down, and neither if it
      // is zero.
      if (_work[28] != 0u)
      {
        const std::size_t slot = ((_work[28] & 0x80u) != 0u) ? KEY_SLOW_DOWN : KEY_SPEED_UP;
        _keys[slot] = 0xFFu;
      }

      // ---- .DK11: the roll ------------------------------------------------------------------
      //
      // 6502: LDA #128 / LDX #(KY3-KLO) / ASL INWK+29 / BEQ DK12.
      const ShiftResult roll = RotateLeftValue(_work[29], false);
      _work[29] = roll.value;

      if (roll.value == 0u)
      {
        _control.roll = 128u; // 6502: BEQ DK12 with A still 128 -- nothing asked for, so centred
      }
      else
      {
        // 6502: BCC P%+4 / LDX #(KY4-KLO) -- the carry is the OLD sign, so it picks the direction.
        const std::size_t slot = roll.carry ? KEY_ROLL_RIGHT : KEY_ROLL_LEFT;

        // 6502: BIT INWK+29 / BPL DK14 -- and this is the NEW sign, so it asks whether doubling
        // the request overflowed, which is what "a big request" means here.
        if ((roll.value & 0x80u) != 0u)
        {
          _control.roll = 64u; // 6502: LDA #64 / STA JSTX
          _keys[slot] = 0u;    // 6502: LDA #0 / .DK14 STA KLO,X -- and the key is released
        }
        else
        {
          _keys[slot] = 128u; // 6502: .DK14 STA KLO,X with A still 128
        }
        // 6502: LDA JSTX / .DK12 STA JSTX -- which writes back what is already there.
      }

      // ---- .DK13: the pitch, and it is not the same shape ------------------------------------
      //
      // 6502: LDA #128 / LDX #(KY5-KLO) / ASL INWK+30 / BEQ DK13 / BCS P%+4 / LDX #(KY6-KLO) /
      // STA KLO,X / LDA JSTY / .DK13 STA JSTY.
      //
      // No `BIT` and no direct write: the pitch has no large-request path, and the carry test is
      // the other way round from the roll's.
      const ShiftResult pitch = RotateLeftValue(_work[30], false);
      _work[30] = pitch.value;

      if (pitch.value == 0u)
      {
        _control.pitch = 128u;
      }
      else
      {
        _keys[pitch.carry ? KEY_PITCH_UP : KEY_PITCH_DOWN] = 128u;
      }
    }

    // ---- .DK15: the keys, however they came to be pressed -------------------------------------

    // 6502: LDX JSTX / LDA #14 / LDY KY3 / BEQ P%+5 / JSR BUMP2 / LDY KY4 / BEQ P%+5 / JSR REDU2 /
    // STX JSTX -- and A survives both calls, which is why the 14 is loaded once.
    std::uint8_t roll = _control.roll;
    if (_keys[KEY_ROLL_LEFT] != 0u)
    {
      roll = BumpControl(roll, CONTROL_STEP, _options.recentreDisabled);
    }
    if (_keys[KEY_ROLL_RIGHT] != 0u)
    {
      roll = ReduceControl(roll, CONTROL_STEP, _options.recentreDisabled);
    }
    _control.roll = roll;

    // 6502: LDX JSTY / LDY KY5 / BEQ P%+5 / JSR REDU2 / LDY KY6 / BEQ P%+5 / JSR BUMP2 / STX JSTY
    // -- the pitch keys are the other way round from the roll's.
    std::uint8_t pitch = _control.pitch;
    if (_keys[KEY_PITCH_UP] != 0u)
    {
      pitch = ReduceControl(pitch, CONTROL_STEP, _options.recentreDisabled);
    }
    if (_keys[KEY_PITCH_DOWN] != 0u)
    {
      pitch = BumpControl(pitch, CONTROL_STEP, _options.recentreDisabled);
    }
    _control.pitch = pitch;

    /*
     * 6502: LDA JSTK / BEQ ant / LDA auto / BNE ant / LDX #128 / ... -- the joystick's own
     * re-centring, which the keyboard does not get.
     *
     * A stick springs back to the middle on its own and the game copies that: with no direction
     * held on an axis, the rate is put back to 128 outright rather than damped towards it. Off
     * while the docking computer is flying, because the autopilot's synthetic presses would be
     * cancelled by it.
     */
    if (_options.joystick != 0u && _control.dockingComputer == 0u)
    {
      // 6502: LDA KY3 / ORA KY4 / BNE termite / STX JSTX.
      if ((_keys[KEY_ROLL_LEFT] | _keys[KEY_ROLL_RIGHT]) == 0u)
      {
        _control.roll = 128u;
      }

      // 6502: .termite LDA KY5 / ORA KY6 / BNE ant / STX JSTY.
      if ((_keys[KEY_PITCH_UP] | _keys[KEY_PITCH_DOWN]) == 0u)
      {
        _control.pitch = 128u;
      }
    }

    // 6502: .ant, and no RTS -- it falls into `DK4`, which is the docked dispatcher and not this
    // unit's. A caller of `DOKEY` gets that as well, and the call site does not say so.
  }

  void DrawLaserSights(Canvas& _canvas, MathWorkspace& _math, const CommanderBlock& _commander, TrumbleSprites& _trumbles,
                       std::uint8_t _view, SightEffects& _effects) noexcept
  {
    _effects.SetRasterMode(0x05u); // 6502: LDA #%101 / JSR SETL1

    // 6502: LDY VIEW / LDA LASER,Y / BEQ SIG3.
    const std::uint8_t laser = _commander.bytes[static_cast<std::size_t>(Field::Lasers) + _view];

    if (laser != 0u)
    {
      /*
       * 6502: LDY #SPOFF% / CMP #POW / BEQ SIG1 / INY / CMP #POW+128 / BEQ SIG1 / INY /
       * CMP #Armlas / BEQ SIG1 / INY.
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

      // 6502: STY &63F8 / STY &67F8 -- the same pointer in both blocks of screen RAM.
      _canvas.Write(SIGHT_SPRITE_CELL, pointer);
      _canvas.Write(SIGHT_SPRITE_CELL_2, pointer);

      // 6502: LDA sightcol-SPOFF%,Y / STA VIC+&27.
      _effects.SetSightColour(LASER_SIGHT_COLOUR_TABLE[static_cast<std::size_t>(pointer - SPRITE_POINTER_BASE)]);
    }

    // 6502: LDA #1 / .SIG3 STA T -- one if a laser was found, and the zero `LDA LASER,Y` left if
    // not, which is the whole of how the sights get switched off.
    _math.t = (laser != 0u) ? std::uint8_t{1u} : std::uint8_t{0u};

    // 6502: LDA TRIBBLE+1 / AND #%01111111 / LSR A x4 / TAX.
    const std::uint8_t population = _commander.bytes[static_cast<std::size_t>(Field::Tribbles) + 1u];
    const std::size_t index = static_cast<std::size_t>((population & 0x7Fu) >> 4u);

    _trumbles.count = TRUMBLE_COUNT_TABLE[index]; // 6502: LDA TRIBTA,X / STA TRIBCT

    // 6502: LDA TRIBMA,X / ORA T / STA VIC+&15 -- the sights and the Trumbles in one byte.
    _effects.SetSpritesEnabled(static_cast<std::uint8_t>(TRUMBLE_SPRITE_TABLE[index] | _math.t));

    _effects.SetRasterMode(0x04u); // 6502: LDA #%100 / JMP SETL1, a tail call
  }

  void ClearFlightKeys(KeyLogger& _keys) noexcept
  {
    // 6502: the loop ends on `BNE`, so `KLO+0` is never stored -- and the `STA KL` after it is
    // NOT that byte: `KL` is a separate address with no C64 reader, so the port has nothing to
    // clear for it (§6.117).
    for (std::size_t index = 1; index <= FLIGHT_KEYS_CLEARED; ++index)
    {
      _keys[index] = 0u;
    }
  }

} // namespace Elite

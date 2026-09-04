#include "pch.h"

#include "Dashboard.h"

#include "Arith.h"
#include "EliteTypes.h"
#include "LookupTables.h"

namespace Elite
{

DangerColours DangerColour(std::uint8_t _mainLoopCounter, std::uint8_t _damageFlash) noexcept
{
  // 6502: LDX #YELLOW / LDA MCNT / AND #%00001000 / AND FLH / BEQ P%+4 / TXA / EQUB &2C / LDA #RED
  const std::uint8_t flashing =
    static_cast<std::uint8_t>(_mainLoopCounter & 0x08u & _damageFlash);

  return { (flashing != 0u) ? DIAL_NORMAL : DIAL_DANGER, DIAL_NORMAL };
}

void DrawBar(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, std::uint8_t _value,
             int _shifts) noexcept
{
  // 6502: DILX -- four `LSR A`, and the entry point decides how many of them run (§6.63).
  std::uint8_t value = _value;
  for (int shift = 0; shift < _shifts; ++shift)
  {
    value = static_cast<std::uint8_t>(value >> 1);
  }

  _math.q = value; // 6502: DIL -- STA Q

  // 6502: LDX #&FF / STX R -- a full block of pixels, which the partial block below shifts down.
  std::uint8_t bits = 0xFFu;

  /*
   * 6502: CMP T1 / BCS DL30 / LDA K+1 / BNE DL31 / .DL30 LDA K / .DL31 STA COL.
   *
   * Above the threshold takes `K` and below takes `K+1` -- unless `K+1` is ZERO, in which case
   * the `BNE` falls through and it takes `K` as well. `DIALS` part 1 stores `PZW`'s two colours
   * as (K, K+1) and part 3 stores them the other way round, so the same test means the opposite
   * thing for the energy bars.
   */
  if (value >= _math.t1)
  {
    _draw.col = _math.k[0];
  }
  else
  {
    _draw.col = (_math.k[1] != 0u) ? _math.k[1] : _math.k[0];
  }

  // 6502: LDY #2 / LDX #3 -- rows 2 to 4 of four character cells, so a bar is three pixels tall.
  std::uint8_t row = 2u;

  for (int block = 3; block >= 0; --block)
  {
    std::uint8_t pattern = 0;

    if (_math.q >= 4u) // 6502: LDA Q / CMP #4 / BCC DL2
    {
      // 6502: SBC #4 / STA Q / LDA R -- a whole block lit, and the carry the CMP left makes the
      // subtraction exact.
      _math.q = static_cast<std::uint8_t>(_math.q - 4u);
      pattern = bits;
    }
    else
    {
      /*
       * 6502: DL2 -- EOR #3 / STA Q / LDA R / .DL3 ASL A / ASL A / DEC Q / BPL DL3.
       *
       * `EOR #3` is `3 - Q` for a Q below four, and the loop shifts the full block left twice for
       * each step of it -- so a Q of three lights three pixels and a Q of zero lights none.
       */
      std::uint8_t remaining = static_cast<std::uint8_t>(_math.q ^ 3u);
      pattern = bits;
      do
      {
        pattern = static_cast<std::uint8_t>(pattern << 2);
        remaining = static_cast<std::uint8_t>(remaining - 1u);
      } while ((remaining & 0x80u) == 0u); // 6502: DEC Q / BPL DL3

      // 6502: LDA #0 / STA R / LDA #99 / STA Q -- everything past the partial block is empty, and
      // 99 is how the loop is told there is nothing left: it can never fall below four again.
      bits = 0;
      _math.q = 99u;
    }

    // 6502: DL5 -- AND COL / STA (SC),Y three times over. It STORES rather than EORs, which is
    // why the dashboard needs no erase and the space view does.
    const std::uint8_t byte = static_cast<std::uint8_t>(pattern & _draw.col);
    for (std::uint8_t within = 0; within < 3u; ++within)
    {
      _canvas.Write(static_cast<std::uint16_t>(_draw.sc + row + within), byte);
    }

    /*
     * 6502: TYA / CLC / ADC #6 / BCC P%+4 / INC SC+1 / TAY -- Y is left on the block's last row,
     * so this advances it by eight in all: one character cell to the right.
     *
     * The `INC SC+1` cannot fire. Y runs 2, 10, 18, 26 across four blocks and the add is on the
     * row after the third store, so the largest value it ever sees is 34.
     */
    row = static_cast<std::uint8_t>(row + 2u + 6u);
  }

  // 6502: DL6 -- SC += 320, one character row down, ready for the next dial.
  _draw.sc = static_cast<std::uint16_t>(_draw.sc + 0x140u);
}

void DrawIndicator(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math,
                   std::uint8_t _value) noexcept
{
  std::uint8_t row = 1u; // 6502: LDY #1 -- rows 1 to 4, so this bar is four pixels tall
  _math.q = _value;

  do
  {
    std::uint8_t byte = 0;

    // 6502: SEC / LDA Q / SBC #4 / BCS DLL11
    const SubResult step = SubtractWithCarry(_math.q, 4u, true);
    if (step.carry)
    {
      _math.q = step.value; // 6502: DLL11 -- STA Q / LDA #0, an empty block
    }
    else
    {
      /*
       * 6502: LDA #&FF / LDX Q / STA Q / LDA CTWOS,X / AND #YELLOW.
       *
       * The lit pixel, and then `Q` is set to 255 so that no later block can match -- a loop exit
       * written as data rather than as a branch.
       */
      byte = static_cast<std::uint8_t>(DASHBOARD_PIXEL_TABLE[_math.q & 3u] & DIAL_NORMAL);
      _math.q = 0xFFu;
    }

    // 6502: DLL12 -- four stores down the character cell.
    for (std::uint8_t within = 0; within < 4u; ++within)
    {
      _canvas.Write(static_cast<std::uint16_t>(_draw.sc + row + within), byte);
    }

    // 6502: TYA / CLC / ADC #5 / TAY -- Y is on the block's last row, so this is eight in all.
    row = static_cast<std::uint8_t>(row + 3u + 5u);
  } while (row < 30u); // 6502: CPY #30 / BCC DLL10

  /*
   * 6502: LDA SC / ADC #&3F / STA SC / LDA SC+1 / ADC #&01 / STA SC+1.
   *
   * No `CLC`, and none is needed: the only way out of the loop is a `CPY #30` that did not
   * branch, so the carry is set and this adds 320 rather than 64.
   */
  _draw.sc = static_cast<std::uint16_t>(_draw.sc + 0x140u);
}

void SetMissileIndicator(Canvas& _canvas, std::uint8_t _missile, std::uint8_t _colour) noexcept
{
  // 6502: DEX / TXA / INX / EOR #3 -- missile 1 to 4 becomes cell 3 down to 0, so they fill from
  // the right. `STY SC / TAY / LDA SC` is a register shuffle and not a use of the screen pointer.
  const std::uint8_t cell = static_cast<std::uint8_t>(static_cast<std::uint8_t>(_missile - 1u) ^ 3u);
  _canvas.Write(static_cast<std::uint16_t>(MISSILE_CELL + cell), _colour);
}

void ResetMissileIndicators(Canvas& _canvas, std::uint8_t _missiles) noexcept
{
  // 6502: LDX #4 / .ss CPX NOMSL / BEQ SAL8 / LDY #BLACK2 / JSR MSBAR / DEX / BNE ss.
  std::uint8_t indicator = 4u;
  while (indicator != _missiles && indicator != 0u)
  {
    SetMissileIndicator(_canvas, indicator, MISSILE_NONE);
    --indicator;
  }

  // 6502: .SAL8 LDY #GREEN2 / JSR MSBAR / DEX / BNE SAL8 -- the same X, carrying on downwards.
  while (indicator != 0u)
  {
    SetMissileIndicator(_canvas, indicator, MISSILE_READY);
    --indicator;
  }
}

void SetMissileTarget(Canvas& _canvas, Bubble& _bubble, std::uint8_t& _missileSeeking,
                      std::uint8_t _missiles, std::uint8_t _target, std::uint8_t _colour) noexcept
{
  _bubble.missileTarget = _target;                          // 6502: STX MSTG
  SetMissileIndicator(_canvas, _missiles, _colour);         // 6502: LDX NOMSL / JSR MSBAR

  // 6502: STY MSAR -- and Y is the ZERO `MSBAR` ended on, not the colour that went in.
  _missileSeeking = 0;
}

void AbortMissileLock(Canvas& _canvas, Bubble& _bubble, std::uint8_t& _missileSeeking,
                      std::uint8_t _missiles, std::uint8_t _colour) noexcept
{
  // 6502: ABORT -- LDX #&FF, and no RTS: it runs straight into ABORT2.
  SetMissileTarget(_canvas, _bubble, _missileSeeking, _missiles, 0xFFu, _colour);
}

void ToggleEcmIndicator(Canvas& _canvas) noexcept
{
  // 6502: ECBLB -- two cells, one above the other, EORed in and out.
  _canvas.ExclusiveOr(ECM_CELL, BULB_COLOUR);
  _canvas.ExclusiveOr(static_cast<std::uint16_t>(ECM_CELL + 40u), BULB_COLOUR);
}

void ToggleStationIndicator(Canvas& _canvas) noexcept
{
  _canvas.ExclusiveOr(STATION_CELL, BULB_COLOUR);
  _canvas.ExclusiveOr(static_cast<std::uint16_t>(STATION_CELL + 40u), BULB_COLOUR);
}

void StartEcm(Canvas& _canvas, FlightStatus& _status, DashboardEffects& _effects) noexcept
{
  _status.ecmCountdown = 32u;     // 6502: LDA #32 / STA ECMA
  (void)_effects.PlaySound(SOUND_ECM);  // 6502: LDY #sfxecm / JSR NOISE
  ToggleEcmIndicator(_canvas);    // 6502: and no RTS -- it falls into ECBLB
}

void StopEcm(Canvas& _canvas, FlightStatus& _status, DashboardEffects& _effects) noexcept
{
  _status.ecmCountdown = 0u;   // 6502: LDA #0 / STA ECMA
  _status.ecmOurs = 0u;        // 6502: STA ECMP
  ToggleEcmIndicator(_canvas); // 6502: JSR ECBLB
  _effects.StopSound(SOUND_ECM); // 6502: LDY #sfxecm / JMP NOISEOFF -- a tail call, so this ends it
}

void DrawDials(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math,
               GeometryWorkspace& _geometry, const FlightState& _flight,
               const FlightStatus& _status, std::uint8_t _fuel, Compass& _compass,
               const Bubble& _bubble) noexcept
{
  // ---- part 1: the speed bar ------------------------------------------------------------------

  // 6502: LDA #LO(DLOC%+8*30) / STA SC / ... -- thirty character cells into the dashboard.
  _draw.sc = static_cast<std::uint16_t>(DASHBOARD_BITMAP + 8u * 30u);

  // 6502: JSR PZW / STX K+1 / STA K -- the danger colour in K and yellow in K+1.
  const DangerColours danger = DangerColour(_flight.mainLoopCounter, _status.damageFlash);
  _math.k[1] = danger.x;
  _math.k[0] = danger.a;

  _math.t1 = 14u;                                  // 6502: LDA #14 / STA T1
  DrawBar(_canvas, _draw, _math, _flight.delta, 1); // 6502: LDA DELTA / JSR DIL-1

  // ---- part 2: roll and pitch -----------------------------------------------------------------

  // 6502: LDA #0 / STA R / STA P / LDA #8 / STA S -- `ADD` adds a positive eight to whatever
  // sign-magnitude byte it is handed, which is what centres both indicators.
  _math.r = 0;
  _math.p = 0;
  _math.s = 8u;

  /*
   * 6502: LDA ALP1 / LSR A / LSR A / ORA ALP2 / EOR #%10000000 / JSR ADD / JSR DIL2.
   *
   * The roll magnitude quartered, its sign put back, and then the sign FLIPPED -- because the
   * indicator moves the other way from the roll.
   */
  const std::uint8_t roll = static_cast<std::uint8_t>(((_flight.alp1 >> 2) | _flight.alp2) ^ 0x80u);
  DrawIndicator(_canvas, _draw, _math, AddSigned(_math, roll).high);

  /*
   * 6502: LDA BETA / LDX BET1 / BEQ P%+4 / SBC #1 / JSR ADD / JSR DIL2.
   *
   * `SBC #1` HAS NO `SEC`, so it runs on the carry `DIL2` left -- and `DIL2` ends
   * `LDA SC+1 / ADC #&01 / STA SC+1` on a screen-address high byte, which cannot carry out. So
   * the carry is always CLEAR and the pitch indicator is offset by TWO rather than by one.
   *
   * The fourteenth uncleared flag, and UNLIKE the thirteenth it is load-bearing: `SP2`'s
   * `ADC #195` could not see an always-clear carry and this `SBC` borrows because of it, so the
   * mutation that assumes a set carry moves the indicator and the mutation that assumed one in
   * `SP2` was equivalent. Constant does not mean invisible, and which of the two it is depends on
   * the instruction rather than on the flag (§6.65).
   */
  std::uint8_t pitch = _flight.beta;
  if (_flight.bet1 != 0u)
  {
    pitch = SubtractWithCarry(pitch, 1u, false).value;
  }
  DrawIndicator(_canvas, _draw, _math, AddSigned(_math, pitch).high);

  // ---- part 3: the four energy bars, on one pass in four --------------------------------------

  /*
   * 6502: LDA MCNT / AND #3 / BNE dec27.
   *
   * `dec27` is `TT26`'s own `RTS` borrowed as a branch target, so this does not skip part 3 -- it
   * RETURNS FROM `DIALS`. Three passes in four draw the speed, the roll and the pitch and stop
   * there: the energy bars, the shields, the fuel, the two temperatures, the altitude and the
   * compass are all one pass in four (§6.64).
   */
  if ((_flight.mainLoopCounter & 3u) != 0u)
  {
    return;
  }

  {
    // 6502: JSR PZW / STX K / STA K+1 -- the OTHER way round from part 1, so the same threshold
    // test in `DIL` picks the opposite colour.
    const DangerColours bars = DangerColour(_flight.mainLoopCounter, _status.damageFlash);
    _math.k[0] = bars.x;
    _math.k[1] = bars.a;

    _math.t1 = 3u; // 6502: LDX #3 / STX T1

    // 6502: LDY #0 / .DLL23 STY XX12,X / DEX / BPL DLL23 -- all four cleared before any is read.
    for (std::size_t bar = 0; bar < 4u; ++bar)
    {
      _geometry.xx12[bar] = 0;
    }

    /*
     * 6502: LDA ENERGY / LSR A / LSR A / STA Q / .DLL24 SEC / SBC #16 / BCC DLL26 / ...
     *
     * The energy quartered and then dealt out sixteen at a time from the TOP bar downwards, so a
     * full bank fills bar 3 first and the remainder lands in whichever bar the subtraction ran
     * out on.
     */
    _math.q = static_cast<std::uint8_t>(_status.energy >> 2);
    int bar = 3;
    for (;;)
    {
      const SubResult left = SubtractWithCarry(_math.q, 16u, true);
      if (!left.carry)
      {
        _geometry.xx12[static_cast<std::size_t>(bar)] = _math.q; // 6502: DLL26
        break;
      }

      _math.q = left.value;
      _geometry.xx12[static_cast<std::size_t>(bar)] = 16u;
      --bar;
      if (bar < 0)
      {
        break; // 6502: DEX / BPL DLL24 / BMI DLL9
      }
    }

    // 6502: DLL9 -- LDA XX12,Y / STY P / JSR DIL / LDY P / INY / CPY #4 / BNE DLL9. `DIL` and not
    // `DILX`, so the bars are drawn unshifted.
    for (std::uint8_t which = 0; which < 4u; ++which)
    {
      _math.p = which;
      DrawBar(_canvas, _draw, _math, _geometry.xx12[which], 0);
    }
  }

  // ---- part 4: the shields, the fuel, the temperatures and the altitude ------------------------

  // 6502: LDA #LO(DLOC%+8*6) / STA SC / ... -- back to the left-hand column.
  _draw.sc = static_cast<std::uint16_t>(DASHBOARD_BITMAP + 8u * 6u);

  // 6502: LDA #YELLOW / STA K / STA K+1 -- both colours the same, so the shields and the fuel do
  // not flash whatever `T1` says.
  _math.k[0] = DIAL_NORMAL;
  _math.k[1] = DIAL_NORMAL;

  DrawBar(_canvas, _draw, _math, _status.forwardShield, 4); // 6502: LDA FSH / JSR DILX
  DrawBar(_canvas, _draw, _math, _status.aftShield, 4);     // 6502: LDA ASH / JSR DILX
  DrawBar(_canvas, _draw, _math, _fuel, 2);                 // 6502: LDA QQ14 / JSR DILX+2

  // 6502: JSR PZW / STX K+1 / STA K -- part 1's order again, so the temperatures flash.
  const DangerColours heat = DangerColour(_flight.mainLoopCounter, _status.damageFlash);
  _math.k[1] = heat.x;
  _math.k[0] = heat.a;

  _math.t1 = 11u; // 6502: LDX #11 / STX T1
  DrawBar(_canvas, _draw, _math, _status.cabinTemperature, 4); // 6502: LDA CABTMP / JSR DILX
  DrawBar(_canvas, _draw, _math, _status.laserTemperature, 4); // 6502: LDA GNTMP / JSR DILX

  // 6502: LDA #240 / STA T1 -- the altitude never reaches its threshold, so it never flashes.
  _math.t1 = 240u;
  DrawBar(_canvas, _draw, _math, _status.altitude, 4); // 6502: LDA ALTIT / JSR DILX

  UpdateCompass(_canvas, _draw, _math, _compass, _bubble); // 6502: JMP COMPAS
}

} // namespace Elite

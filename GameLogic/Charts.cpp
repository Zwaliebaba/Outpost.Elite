#include "pch.h"

#include "Charts.h"

#include "EliteTypes.h"

#include <array>

/*
 * The galactic charts (slice 2b).
 *
 * Almost everything here is a coordinate mapping and a saturating add, and almost every one of
 * those adds takes its carry from the instruction before it. The two charts map the same galaxy
 * with what look like the same three lines of arithmetic, and they are not the same: the
 * short-range chart's shifts feed their carry into the addition that follows and the crosshair
 * routine's do not. Writing either as plain arithmetic moves half the galaxy by one pixel.
 */

namespace Elite
{

  namespace
  {
    /// 6502: the short-range chart's origin, which is the middle of the drawing area.
    constexpr std::uint8_t SHORT_RANGE_CENTRE_X = 104;
    constexpr std::uint8_t SHORT_RANGE_CENTRE_Y = 90;

    /// 6502: the long-range chart sits 24 rows down the screen; the short-range one starts at the top.
    constexpr std::uint8_t LONG_RANGE_TOP = 24;

    /// 6502: CMP #152 / LDA #151 -- the lowest row the long-range chart's crosshair may reach.
    constexpr std::uint8_t LONG_RANGE_BOTTOM = 152;

    /// 6502: the tests at TT184 and TT186 -- how far a system may be and still appear.
    constexpr std::uint8_t SHORT_RANGE_SPAN_X = 20;
    constexpr std::uint8_t SHORT_RANGE_SPAN_Y = 38;

    /// 6502: XX1 -- 25 bytes of the ship workspace, borrowed as one flag per character row.
    constexpr std::size_t LABEL_ROWS = 25;

    /// 6502: CPY #3 / BCC TT187 -- a system whose row is above this gets neither name nor disc.
    constexpr std::uint8_t FIRST_LABELLED_ROW = 3;

    /*
     * The rows the two charts rule at, which are three separate constants in the original and are
     * kept separate here.
     *
     * NLIN loads 23 and NLIN4 loads 19; the long-range chart's lower rule is written 152 at the
     * call. That last one happens to equal the clamp above, and nothing in the source says the two
     * are the same number on purpose -- so they are not folded together.
     */
    constexpr std::uint8_t LONG_RANGE_RULE_TOP = 23;
    constexpr std::uint8_t LONG_RANGE_RULE_BOTTOM = 152;
    constexpr std::uint8_t SHORT_RANGE_RULE = 19;

    /// 6502: the tokens the two charts print as their titles.
    constexpr std::uint8_t TITLE_LONG_RANGE = 199;
    constexpr std::uint8_t TITLE_SHORT_RANGE = 190;

    /// 6502: the tokens hyp prints -- "HYPERSPACE ", " TO " and "RANGE", and the extended one that
    /// says you are docked.
    constexpr std::uint8_t HYPERSPACE_TOKEN = 189;
    constexpr std::uint8_t TO_TOKEN = 45;
    constexpr std::uint8_t RANGE_TOKEN = 202;
    constexpr std::uint8_t DOCKED_TOKEN = 205;

    /// 6502: LDA #15 -- fifteen counts, in both bytes of QQ22.
    constexpr std::uint8_t COUNTDOWN_START = 15;

    /// 6502: bit 7 of QQ11.
    [[nodiscard]] constexpr bool ShortRange(std::uint8_t _view) noexcept
    {
      return (_view & 0x80u) != 0u;
    }

    /// 6502: SEC / SBC / BCS / EOR #255 / ADC #1 -- the difference, made positive. The carry is
    /// clear when the negate is reached, which is what makes the ADC add exactly one.
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

  std::uint8_t StepCoordinate(std::uint8_t _value, std::uint8_t _step) noexcept
  {
    // 6502: CLC / ADC QQ19+3 -- the move, and the carry it produces is the whole test.
    const AddResult moved = AddWithCarry(_value, _step, false);

    /*
     * 6502: LDX QQ19+3 / BMI TT124 / BCC TT125 / RTS, then TT124: BCC TT180.
     *
     * A positive step that carried has run off the top of the galaxy; a negative one that did NOT
     * carry has run off the bottom. Either way the original returns without storing, so the
     * crosshairs simply do not move. Clamping instead would let a held key crawl along the edge.
     */
    const bool stepIsNegative = (_step & 0x80u) != 0u;
    if (stepIsNegative == moved.carry)
    {
      return moved.value;
    }

    return _value;
  }

  void DrawCrosshairs(Canvas& _canvas, DrawWorkspace& _work, const Crosshairs& _at, std::uint8_t _view) noexcept
  {
    // 6502: LDA #24 / LDX QQ11 / BPL TT178 / LDA #0 -- the long-range chart is 24 rows down.
    const std::uint8_t top = ShortRange(_view) ? std::uint8_t{0} : LONG_RANGE_TOP;

    // 6502: TT84 / TT85 -- the horizontal stroke, saturating at both ends of the screen.
    const std::uint16_t left = static_cast<std::uint16_t>(_at.x) - _at.size;
    _work.x1 = (left < 0x100u) ? static_cast<std::uint8_t>(left) : std::uint8_t{0};

    const AddResult right = AddWithCarry(_at.x, _at.size, false);
    _work.x2 = right.carry ? std::uint8_t{255} : right.value;

    _work.y1 = AddWithCarry(_at.y, top, false).value;
    _work.y2 = _work.y1;
    DrawLine(_canvas, _work);

    // 6502: TT86 -- the vertical stroke's top.
    const std::uint16_t above = static_cast<std::uint16_t>(_at.y) - _at.size;
    const std::uint8_t clippedTop = (above < 0x100u) ? static_cast<std::uint8_t>(above) : std::uint8_t{0};
    _work.y1 = AddWithCarry(clippedTop, top, false).value;

    /*
     * 6502: LDA QQ19+1 / CLC / ADC QQ19+2 / ADC QQ19+5.
     *
     * The second addition has no CLC of its own, so a crosshair whose bottom edge wrapped past
     * 255 arrives one row further down than the sum says. That carry is the difference between a
     * crosshair that meets the fuel circle and one that misses it by a pixel.
     */
    const AddResult below = AddWithCarry(_at.y, _at.size, false);
    const AddResult bottom = AddWithCarry(below.value, top, below.carry);

    // 6502: CMP #152 / BCC TT87 / LDX QQ11 / BMI TT87 / LDA #151 -- the clamp is the long-range
    // chart's only, because the short-range chart has nothing printed below it.
    _work.y2 = (bottom.value >= LONG_RANGE_BOTTOM && !ShortRange(_view)) ? std::uint8_t{LONG_RANGE_BOTTOM - 1} : bottom.value;

    _work.x1 = _at.x;
    _work.x2 = _at.x;
    DrawLine(_canvas, _work);
  }

  void DrawTargetCrosshairs(Canvas& _canvas, DrawWorkspace& _work, const ChartView& _view) noexcept
  {
    Crosshairs at;

    if (!ShortRange(_view.view))
    {
      // 6502: TT103 -- the long-range chart, where y is halved and the crosshair is four wide.
      at.x = _view.cursorX;
      at.y = static_cast<std::uint8_t>(_view.cursorY >> 1);
      at.size = 4;
      DrawCrosshairs(_canvas, _work, at, _view.view);
      return;
    }

    /*
     * 6502: TT105 -- the short-range chart, where the crosshair is drawn only if the selection is
     * near enough to be on screen. Both tests accept a difference under 38 one way; the other way
     * x accepts down to -26 and y down to -36, so the visible window is off-centre by different
     * amounts on each axis.
     */
    const std::uint8_t dx = static_cast<std::uint8_t>(_view.cursorX - _view.homeX);
    if (dx >= 38u && dx < 230u)
    {
      return;
    }

    // 6502: ASL A / ASL A / CLC / ADC #104 -- four times the scale, and the CLC is present here.
    at.x = AddWithCarry(static_cast<std::uint8_t>(dx << 2), SHORT_RANGE_CENTRE_X, false).value;

    const std::uint8_t dy = static_cast<std::uint8_t>(_view.cursorY - _view.homeY);
    if (dy >= 38u && dy < 220u)
    {
      return;
    }

    at.y = AddWithCarry(static_cast<std::uint8_t>(dy << 1), SHORT_RANGE_CENTRE_Y, false).value;
    at.size = 8;
    DrawCrosshairs(_canvas, _work, at, _view.view);
  }

  void MoveCrosshairs(Canvas& _canvas, DrawWorkspace& _work, ChartView& _view, std::uint8_t _stepX, std::uint8_t _stepY) noexcept
  {
    // 6502: JSR TT103 -- the lines are drawn by EOR, so this erases the crosshair that is there.
    DrawTargetCrosshairs(_canvas, _work, _view);

    /*
     * 6502: DEY / TYA / EOR #255 -- the vertical step arrives negated, because a key that means
     * "up" on the chart means "down the screen". The port takes both steps as the routine finally
     * uses them and leaves the negation to the caller that reads the keyboard.
     */
    _view.cursorY = StepCoordinate(_view.cursorY, _stepY);
    _view.cursorX = StepCoordinate(_view.cursorX, _stepX);

    // 6502: falls through into TT103 again, which redraws at the new place.
    DrawTargetCrosshairs(_canvas, _work, _view);
  }

  void DrawFuelRange(Canvas& _canvas, DrawWorkspace& _work, const ChartView& _view, ChartShapes* _shapes) noexcept
  {
    Crosshairs at;
    RangeCircle circle;

    if (ShortRange(_view.view))
    {
      // 6502: TT126 -- the short-range chart is centred on you, so the circle is centred on the
      // screen and the radius is the fuel itself rather than a quarter of it.
      at.x = SHORT_RANGE_CENTRE_X;
      at.y = SHORT_RANGE_CENTRE_Y;
      at.size = 16;
      DrawCrosshairs(_canvas, _work, at, _view.view);

      circle.x = at.x;
      circle.y = at.y;
      circle.radius = _view.fuel;
    }
    else
    {
      // 6502: TT14 -- on the long-range chart the circle is where you are, at half vertical scale.
      at.x = _view.homeX;
      at.y = static_cast<std::uint8_t>(_view.homeY >> 1);
      at.size = 7;
      DrawCrosshairs(_canvas, _work, at, _view.view);

      circle.x = at.x;

      // 6502: LDA QQ19+1 / CLC / ADC #24 -- the circle is drawn against the chart's own origin,
      // which the crosshair above reached through a separate addition.
      circle.y = AddWithCarry(at.y, LONG_RANGE_TOP, false).value;
      circle.radius = static_cast<std::uint8_t>(_view.fuel >> 2);
    }

    // 6502: LDX #2 / STX STP -- the circle is walked in steps of two, which is what makes it
    // sixty-four segments rather than the smoother sixteen the planets use.
    circle.step = 2;

    if (_shapes != nullptr)
    {
      _shapes->DrawRangeCircle(circle);
    }
  }

  void DrawSeparator(Canvas& _canvas, DrawWorkspace& _work, std::uint8_t _y) noexcept
  {
    // 6502: NLIN2 -- LDX #0 / STX X1 / DEX / STX X2, so the line runs to 255 rather than to the
    // edge of the drawing area, and its right end lands in the margin.
    _work.y1 = _y;
    _work.y2 = _y;
    _work.x1 = 0;
    _work.x2 = 255;
    DrawLine(_canvas, _work);
  }

  void DrawLongRangeChart(Canvas& _canvas, DrawWorkspace& _work, TokenPrinter& _printer, TextState& _text, const ChartView& _view,
                          const SystemSeeds& _galaxy, ChartShapes* _shapes) noexcept
  {
    // 6502: LDA #7 / JSR DOXC / LDA #199 / JSR TT27 -- the title, seven cells in.
    _text.column = 7;
    _printer.Print(TITLE_LONG_RANGE);

    /*
     * 6502: JSR NLIN, which is LDA #23 / JSR INCYC / NLIN2.
     *
     * The rule goes at row 23 and the CURSOR moves down one, in that order -- the increment is
     * INCYC's and has nothing to do with the 23. Then a second rule at 152, under the chart.
     */
    ++_text.row;
    DrawSeparator(_canvas, _work, LONG_RANGE_RULE_TOP);
    DrawSeparator(_canvas, _work, LONG_RANGE_RULE_BOTTOM);

    // 6502: JSR TT14 -- the fuel circle, before the dots rather than after.
    DrawFuelRange(_canvas, _work, _view, _shapes);

    /*
     * 6502: TT83 -- 256 systems, and each one is a single PIXEL call.
     *
     * The x coordinate is the seed byte itself, the y is another seed byte halved, and the SIZE
     * comes from a third: `ORA #%01010000` turns QQ15+4 into something PIXEL reads as a distance,
     * so a system's dot is large or small according to a byte that means nothing else.
     */
    SystemSeeds seeds = _galaxy;
    for (int system = 0; system < 256; ++system)
    {
      _work.zz = static_cast<std::uint8_t>(seeds.bytes[4] | 0x50u);
      const std::uint8_t y = AddWithCarry(static_cast<std::uint8_t>(seeds.bytes[1] >> 1), LONG_RANGE_TOP, false).value;
      PlotPixel(_canvas, _work, seeds.bytes[3], y);
      NextSystem(seeds);
    }

    // 6502: the fall-through into TT15 with QQ19 set from QQ9 and QQ10.
    DrawTargetCrosshairs(_canvas, _work, _view);
  }

  void DrawShortRangeChart(Canvas& _canvas, DrawWorkspace& _work, TokenPrinter& _printer, TextState& _text, const ChartView& _view,
                           const SystemSeeds& _galaxy, ChartShapes* _shapes) noexcept
  {
    /*
     * 6502: LDA #7 / JSR DOXC / LDA #190 / JSR NLIN3.
     *
     * The 190 is the TITLE TOKEN, not a row. NLIN3 prints whatever is in A and then falls into
     * NLIN4, which loads 19 for itself -- so this chart's rule is at 19 and the long-range one's
     * is at 23, and neither number appears at the call site. NLIN4 also skips the INCYC that NLIN
     * does, so the cursor does not move here.
     */
    _text.column = 7;
    _printer.Print(TITLE_SHORT_RANGE);
    DrawSeparator(_canvas, _work, SHORT_RANGE_RULE);

    DrawFuelRange(_canvas, _work, _view, _shapes);
    DrawTargetCrosshairs(_canvas, _work, _view);

    /*
     * 6502: EE3 -- LDX #24 / STA XX1,X, counting down.
     *
     * Twenty-five bytes of the SHIP workspace, one per character row, marking which rows already
     * carry a name. It is scratch and nothing else: the flight model has not started and will
     * zero the workspace again before it does.
     */
    std::array<std::uint8_t, LABEL_ROWS> rowUsed{};

    SystemSeeds seeds = _galaxy;
    for (int system = 0; system < 256; ++system)
    {
      const std::uint8_t x = seeds.bytes[3];
      const std::uint8_t y = seeds.bytes[1];

      // 6502: TT184 and TT186 -- near enough across, and near enough up and down.
      if (AbsoluteDifference(x, _view.homeX) < SHORT_RANGE_SPAN_X && AbsoluteDifference(y, _view.homeY) < SHORT_RANGE_SPAN_Y)
      {
        /*
         * 6502: ASL A / ASL A / ADC #104, and below ASL A / ADC #90.
         *
         * NEITHER of these has a CLC, unlike the pair in TT105 that map the same galaxy onto the
         * same screen. So the carry the last shift produced is added in, and a system far enough
         * to the left or high enough up lands one pixel from where the crosshair routine would
         * put it. The two mappings are thirty instructions apart and look identical.
         */
        const std::uint8_t dx = static_cast<std::uint8_t>(x - _view.homeX);
        const ShiftResult dx1 = RotateLeft(dx, false);
        const ShiftResult dx2 = RotateLeft(dx1.value, false);
        const std::uint8_t screenX = AddWithCarry(dx2.value, SHORT_RANGE_CENTRE_X, dx2.carry).value;

        const std::uint8_t dy = static_cast<std::uint8_t>(y - _view.homeY);
        const ShiftResult dy1 = RotateLeft(dy, false);
        const std::uint8_t screenY = AddWithCarry(dy1.value, SHORT_RANGE_CENTRE_Y, dy1.carry).value;

        // 6502: LSR / LSR / LSR / CLC / ADC #1 -- the cell the name starts in.
        _text.column = AddWithCarry(static_cast<std::uint8_t>(screenX >> 3), 1, false).value;

        /*
         * 6502: LDX XX1,Y / BEQ EE4 / INY / LDX XX1,Y / BEQ EE4 / DEY / DEY / LDX XX1,Y / BNE ee1.
         *
         * The name goes on its own row if that is free, else the row below, else the row above.
         * If all three are taken the system still gets its disc but no name -- which is why a
         * crowded chart has anonymous systems rather than overlapping text.
         */
        /*
         * 6502: LSR / LSR / LSR / TAY.
         *
         * The third shift leaves bit 2 of the row's pixel coordinate in the carry, and on the path
         * where no name is printed nothing clears it before the addition that sizes the disc. So a
         * system that is too crowded to name is drawn from a different flag than one that is not.
         */
        const ShiftResult rowShift = RotateRight(RotateRight(RotateRight(screenY, false).value, false).value, false);
        int row = rowShift.value;
        bool carry = rowShift.carry;
        bool named = false;

        /*
         * The original reads XX1,Y with nothing to stop it, and does not need anything: the
         * visibility test above bounds the row to 2..20, so the twenty-five bytes are always
         * enough. The port bounds it anyway, because a row this array cannot hold would be a
         * silent read into whatever follows rather than a visible fault.
         */
        const auto free = [&rowUsed](int _row) noexcept
        { return _row >= 0 && static_cast<std::size_t>(_row) < rowUsed.size() && rowUsed[static_cast<std::size_t>(_row)] == 0; };

        if (free(row))
        {
          named = true;
        }
        else if (free(row + 1))
        {
          ++row;
          named = true;
        }
        else if (free(row - 1))
        {
          --row;
          named = true;
        }

        bool drawDisc = true;

        if (named)
        {
          _text.row = static_cast<std::uint8_t>(row);

          // 6502: CPY #3 / BCC TT187 -- too near the top, so the system is skipped ENTIRELY. The
          // branch goes past the disc as well as past the name.
          if (row < FIRST_LABELLED_ROW)
          {
            drawDisc = false;
          }
          else
          {
            rowUsed[static_cast<std::size_t>(row)] = 0xFF;
            _printer.SetCaseFlags(0x80);

            /*
             * 6502: JSR cpl, and the carry it returns is the one the ADC below consumes. The CPY
             * that guarded this branch set the carry, and cpl's last seed twist then overwrote it.
             */
            SystemSeeds naming = seeds;
            carry = PrintSystemName(_printer, naming);
          }
        }

        if (drawDisc && _shapes != nullptr)
        {
          /*
           * 6502: LDA QQ15+5 / AND #1 / ADC #2.
           *
           * AND does not touch the carry, and nothing between here and the branch that arrived
           * clears it -- so the disc's size is a masked seed bit PLUS two PLUS a carry left over
           * from somewhere else entirely: from cpl's last seed twist if the system was named, and
           * from bit 2 of its own screen row if it was not.
           *
           * That is why a system can be three pixels across on one chart and two on another
           * without its seed changing, and it is worth a paragraph because the arithmetic reads
           * as though it could only ever produce two or three from one bit.
           */
          const std::uint8_t radius = AddWithCarry(static_cast<std::uint8_t>(seeds.bytes[5] & 0x01u), 2, carry).value;
          _shapes->DrawSystemDisc(screenX, screenY, radius);
        }
      }

      NextSystem(seeds);
    }
  }

  void PrintRangeError(TokenPrinter& _printer) noexcept
  {
    // 6502: TT147 -- LDA #202 / JSR TT27 / LDA #'?' / JMP TT27.
    _printer.Print(RANGE_TOKEN);
    _printer.Print('?');
  }

  void PrintCountdown(TextSink& _sink, TextState& _text, std::uint8_t _count) noexcept
  {
    /*
     * 6502: ee3 -- LDA #1 / JSR DOXC / JSR DOYC.
     *
     * One load feeding two calls: DOXC takes the accumulator and DOYC takes it again, unchanged.
     * So the countdown always sits at (1, 1) and neither cursor is the caller's to choose.
     */
    _text.column = 1;
    _text.row = 1;

    // 6502: LDY #0 / CLC / LDA #3 / JMP TT11 -- three digits, no point, and the value arrives in
    // X with Y as its high byte, which is always zero here.
    PrintValue(_sink, _count, 3, false);
  }

  NearestSystem SelectNearestSystem(Canvas& _canvas, DrawWorkspace& _work, ChartView& _view, const SystemSeeds& _galaxy,
                                    ChartEffects* _effects) noexcept
  {
    // 6502: hm -- JSR TT103 / JSR TT111 / JSR TT103 / JMP CLYNS. The first call rubs the crosshair
    // out, because LOIN draws by EOR and drawing it twice is how it moves.
    DrawTargetCrosshairs(_canvas, _work, _view);

    const NearestSystem nearest = FindNearestSystem(_galaxy, _view.cursorX, _view.cursorY, _view.homeX, _view.homeY);
    _view.cursorX = nearest.x;
    _view.cursorY = nearest.y;

    DrawTargetCrosshairs(_canvas, _work, _view);

    if (_effects != nullptr)
    {
      _effects->ClearBottomRows();
    }

    return nearest;
  }

  JumpOutcome RequestHyperspace(Canvas& _canvas, DrawWorkspace& _work, TokenPrinter& _printer, ExtendedTokenPrinter& _extended,
                                TextState& _text, ChartView& _view, JumpState& _jump, const SystemSeeds& _galaxy,
                                ChartEffects* _effects) noexcept
  {
    if (_jump.docked != 0)
    {
      /*
       * 6502: dockEd -- JSR CLYNS / LDA #15 / JSR DOXC / LDA #205 / JMP DETOK.
       *
       * The message is an EXTENDED token, which is why this routine needs both printers: the rest
       * of hyp prints recursive ones.
       */
      if (_effects != nullptr)
      {
        _effects->ClearBottomRows();
      }
      _text.column = 15;
      _extended.Print(DOCKED_TOKEN);
      return JumpOutcome::Docked;
    }

    // 6502: LDA QQ22+1 / BEQ / RTS -- a countdown already running swallows the key.
    if (_jump.countdown != 0)
    {
      return JumpOutcome::Busy;
    }

    // 6502: JSR CTRL / BMI Ghy -- the galactic hyperdrive, which reads the equipment the commander
    // is carrying and so lands with slice 2d.
    if (_jump.controlHeld)
    {
      return JumpOutcome::Galactic;
    }

    if (_view.view == 0)
    {
      // 6502: TTX110 -- from the space view there are no crosshairs to move, so the search runs
      // without the two TT103 calls that bracket it on a chart.
      const NearestSystem nearest = FindNearestSystem(_galaxy, _view.cursorX, _view.cursorY, _view.homeX, _view.homeY);
      _view.cursorX = nearest.x;
      _view.cursorY = nearest.y;
      _jump.distance = nearest.distance;
      _jump.target = nearest.seeds;
    }
    else if ((_view.view & 0xC0u) == 0u)
    {
      // 6502: AND #%11000000 / BNE / RTS -- neither chart is showing, so there is nothing selected.
      return JumpOutcome::Busy;
    }
    else
    {
      const NearestSystem nearest = SelectNearestSystem(_canvas, _work, _view, _galaxy, _effects);
      _jump.distance = nearest.distance;
      _jump.target = nearest.seeds;
    }

    // 6502: TTX111 -- LDA QQ8 / ORA QQ8+1 / BNE / RTS. A distance of zero is the system you are
    // already in, and the key does nothing at all -- not even a message.
    if (_jump.distance == 0)
    {
      return JumpOutcome::AlreadyThere;
    }

    // 6502: LDA #7 / JSR DOXC, then row 23 on a chart and 17 in space.
    _text.column = 7;
    _text.row = (_view.view != 0) ? std::uint8_t{23} : std::uint8_t{17};

    _printer.SetCaseFlags(0);
    _printer.Print(HYPERSPACE_TOKEN);

    /*
     * 6502: LDA QQ8+1 / BNE goTT147 / LDA QQ14 / CMP QQ8 / BCS.
     *
     * Two tests, not one. Anything 256 tenths or further fails on its HIGH byte before the fuel is
     * looked at, so a system 25.6 light years away is out of range with a full tank -- and says the
     * same thing it says when the tank is empty.
     */
    if ((_jump.distance >> 8) != 0 || _view.fuel < static_cast<std::uint8_t>(_jump.distance))
    {
      PrintRangeError(_printer);
      return JumpOutcome::OutOfRange;
    }

    _printer.Print(TO_TOKEN);

    SystemSeeds naming = _jump.target;
    PrintSystemName(_printer, naming);

    // 6502: wW / wW2 -- LDA #15 / STA QQ22+1 / STA QQ22 / TAX / JMP ee3. Both bytes of the
    // countdown take the same value, and the one that is printed is the one in X.
    _jump.countdown = COUNTDOWN_START;
    PrintCountdown(_extended.Characters(), _text, COUNTDOWN_START);
    return JumpOutcome::CountingDown;
  }

  bool FindSystemByName(TokenPrinter& _printer, CharacterPrinter& _characters, ChartView& _view, const SystemSeeds& _galaxy,
                        std::span<const std::uint8_t> _typed) noexcept
  {
    SystemSeeds seeds = _galaxy;

    for (int system = 0; system < 256; ++system)
    {
      // 6502: JSR MT14 -- justification on, so the name goes into the buffer rather than the
      // screen. This is the whole trick, and it is why the search needed slice 1c-c-b.
      _characters.state.justify = 0x80;
      _characters.state.bufferLength = 0;

      SystemSeeds naming = seeds;
      PrintSystemName(_printer, naming);

      const std::size_t length = _characters.state.bufferLength;

      /*
       * 6502: LDX DTW5 / LDA INWK+5,X / CMP #13 -- the typed name must END where the printed one
       * does. A shorter or longer entry fails on this one comparison rather than on the letters.
       */
      if (length < _typed.size() && _typed[length] == 13)
      {
        /*
         * 6502: HME4 -- backwards, with the typed character's bit 5 forced on and the buffer's
         * left alone. So the match depends on the case `cpl` printed in, which this routine does
         * not set: see the header.
         */
        bool matched = true;
        for (std::size_t index = length; index-- > 0;)
        {
          if (static_cast<std::uint8_t>(_typed[index] | 0x20u) != _characters.buffer[index])
          {
            matched = false;
            break;
          }
        }

        if (matched)
        {
          // 6502: HME5 -- the crosshairs move to the system that matched.
          _view.cursorX = seeds.bytes[3];
          _view.cursorY = seeds.bytes[1];

          // 6502: JSR MT15 -- justification off again, and the buffer thrown away.
          _characters.state.justify = 0;
          _characters.state.bufferLength = 0;
          return true;
        }
      }

      NextSystem(seeds);
    }

    _characters.state.justify = 0;
    _characters.state.bufferLength = 0;
    return false;
  }

} // namespace Elite

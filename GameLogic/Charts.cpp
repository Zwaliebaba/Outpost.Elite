#include "pch.h"

#include "Charts.h"

#include "Lines2x.h"
#include "Picture.h"

#include "EliteTypes.h"

#include <array>
#include "PlanetDraw.h"
#include "Ports.h"
#include "Universe.h"
#include "TextPrint.h"

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

    /// 6502: the lowest row the long-range chart's crosshair may reach, clamped to one below.
    constexpr std::uint8_t LONG_RANGE_BOTTOM = 152;

    /// 6502: the tests at TT184 and TT186 -- how far a system may be and still appear.
    constexpr std::uint8_t SHORT_RANGE_SPAN_X = 20;
    constexpr std::uint8_t SHORT_RANGE_SPAN_Y = 38;

    /// 6502: XX1 -- 25 bytes of the ship workspace, borrowed as one flag per character row.
    constexpr std::size_t LABEL_ROWS = 25;

    /// 6502: TT187 -- a system whose row is above this gets neither name nor disc.
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

    /// 6502: fifteen counts, in both bytes of `QQ22`.
    constexpr std::uint8_t COUNTDOWN_START = 15;

    /// 6502: bit 7 of QQ11.
    [[nodiscard]] constexpr bool ShortRange(std::uint8_t _view) noexcept
    {
      return (_view & 0x80u) != 0u;
    }

    /// 6502: the difference, made positive by complementing and adding one when it borrowed. The
    /// carry is clear when that negation is reached, which is what makes the addition add one.
    [[nodiscard]] std::uint8_t AbsoluteDifference(std::uint8_t _first, std::uint8_t _second) noexcept
    {
      const std::uint16_t difference = static_cast<std::uint16_t>(_first) - _second;
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
    // 6502: the move, and the carry it produces is the whole test.
    const AddResult moved = AddWithCarry(_value, _step, false);

    /*
     * 6502: TT124, TT125 and TT180 -- the step's sign first, then the addition's carry.
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

  void DrawCrosshairs(Canvas& _canvas, const Crosshairs& _at, std::uint8_t _view, Picture* _picture) noexcept
  {
    // 6502: TT178 -- the long-range chart is 24 rows down and the short-range one is not.
    const std::uint8_t top = ShortRange(_view) ? std::uint8_t{0} : LONG_RANGE_TOP;

    // 6502: TT84 / TT85 -- the horizontal stroke, saturating at both ends of the screen.
    Line stroke;
    const std::uint16_t left = static_cast<std::uint16_t>(_at.x) - _at.size;
    stroke.x1 = (left < 0x100u) ? static_cast<std::uint8_t>(left) : std::uint8_t{0};

    const AddResult right = AddWithCarry(_at.x, _at.size, false);
    stroke.x2 = right.carry ? std::uint8_t{255} : right.value;

    stroke.y1 = AddWithCarry(_at.y, top, false).value;
    stroke.y2 = stroke.y1;
    (void)DrawLine(_canvas, stroke);
    if (_picture != nullptr)
    {
      DrawLine2x(*_picture, stroke);
    }

    // 6502: TT86 -- the vertical stroke's top.
    const std::uint16_t above = static_cast<std::uint16_t>(_at.y) - _at.size;
    const std::uint8_t clippedTop = (above < 0x100u) ? static_cast<std::uint8_t>(above) : std::uint8_t{0};
    stroke.y1 = AddWithCarry(clippedTop, top, false).value;

    /*
     * 6502: the y, the half-height and the chart's top, added in that order.
     *
     * The second addition has no CLC of its own, so a crosshair whose bottom edge wrapped past
     * 255 arrives one row further down than the sum says. That carry is the difference between a
     * crosshair that meets the fuel circle and one that misses it by a pixel.
     */
    const AddResult below = AddWithCarry(_at.y, _at.size, false);
    const AddResult bottom = AddWithCarry(below.value, top, below.carry);

    // 6502: TT87 -- the clamp is the long-range
    // chart's only, because the short-range chart has nothing printed below it.
    stroke.y2 = (bottom.value >= LONG_RANGE_BOTTOM && !ShortRange(_view)) ? std::uint8_t{LONG_RANGE_BOTTOM - 1} : bottom.value;

    stroke.x1 = _at.x;
    stroke.x2 = _at.x;
    (void)DrawLine(_canvas, stroke);
    if (_picture != nullptr)
    {
      DrawLine2x(*_picture, stroke);
    }
  }

  void DrawTargetCrosshairs(Canvas& _canvas, const ChartView& _view, Picture* _picture) noexcept
  {
    Crosshairs at;

    if (!ShortRange(_view.view))
    {
      // 6502: TT103 -- the long-range chart, where y is halved and the crosshair is four wide.
      at.x = _view.cursorX;
      at.y = static_cast<std::uint8_t>(_view.cursorY >> 1);
      at.size = 4;
      DrawCrosshairs(_canvas, at, _view.view, _picture);
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

    // 6502: four times the scale, and the carry IS cleared here.
    at.x = AddWithCarry(static_cast<std::uint8_t>(dx << 2), SHORT_RANGE_CENTRE_X, false).value;

    const std::uint8_t dy = static_cast<std::uint8_t>(_view.cursorY - _view.homeY);
    if (dy >= 38u && dy < 220u)
    {
      return;
    }

    at.y = AddWithCarry(static_cast<std::uint8_t>(dy << 1), SHORT_RANGE_CENTRE_Y, false).value;
    at.size = 8;
    DrawCrosshairs(_canvas, at, _view.view, _picture);
  }

  void MoveCrosshairs(Canvas& _canvas, ChartView& _view, std::uint8_t _stepX, std::uint8_t _stepY, Picture* _picture) noexcept
  {
    // 6502: TT103 -- the lines are drawn by EOR, so this erases the crosshair that is there.
    DrawTargetCrosshairs(_canvas, _view, _picture);

    /*
     * 6502: the vertical step arrives negated, because a key that means
     * "up" on the chart means "down the screen". The port takes both steps as the routine finally
     * uses them and leaves the negation to the caller that reads the keyboard.
     */
    _view.cursorY = StepCoordinate(_view.cursorY, _stepY);
    _view.cursorX = StepCoordinate(_view.cursorX, _stepX);

    // 6502: falls through into TT103 again, which redraws at the new place.
    DrawTargetCrosshairs(_canvas, _view, _picture);
  }

  void DrawFuelRange(Universe& _universe, const ChartView& _view) noexcept
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
      DrawCrosshairs(_universe.canvas, at, _view.view, &_universe.picture);

      circle.x = at.x;
      circle.y = at.y;
      circle.radius = _view.fuel.tenths;
    }
    else
    {
      // 6502: TT14 -- on the long-range chart the circle is where you are, at half vertical scale.
      at.x = _view.homeX;
      at.y = static_cast<std::uint8_t>(_view.homeY >> 1);
      at.size = 7;
      DrawCrosshairs(_universe.canvas, at, _view.view, &_universe.picture);

      circle.x = at.x;

      // 6502: the chart's top added -- the circle is drawn against the chart's own origin,
      // which the crosshair above reached through a separate addition.
      circle.y = AddWithCarry(at.y, LONG_RANGE_TOP, false).value;
      circle.radius = static_cast<std::uint8_t>(_view.fuel.tenths >> 2);
    }

    // 6502: a step of two -- the circle is walked in twos, which is what makes it
    // sixty-four segments rather than the smoother sixteen the planets use.
    circle.step = 2;

    {
      /*
       * 6502: TT128 -- the centre, the heap pointer and the step, then `CIRCLE2`; and it is a
       * CALL now rather than a seam (M3-b-1b).
       *
       * `LSP` goes to ONE rather than to zero: the ball heap's first byte is not a line, so an
       * empty heap is a pointer of 1 and a `LSP` of 0 would make `BLINE`'s first segment
       * overwrite it.
       */
      _universe.heaps.ballHeapTop = 1u;
      _universe.heaps.circleStep = circle.step;
      const Projection centre{circle.x, 0u, circle.y, 0u};
      DrawBall(_universe.canvas, _universe.heaps, _universe.geometry, _universe.math, _universe.clip, centre, circle.radius, false,
               &_universe.picture);
    }
  }

  void DrawTitleRule(Canvas& _canvas, TextState& _text, Picture* _picture) noexcept
  {
    // 6502: INCYC and then the fall into NLIN2 -- the cursor moves down one line FIRST, and
    // the increment is INCYC's own; the 23 is where the rule goes and nothing else.
    ++_text.row;
    DrawSeparator(_canvas, LONG_RANGE_RULE_TOP, _picture);
  }

  void DrawSeparator(Canvas& _canvas, std::uint8_t _y, Picture* _picture) noexcept
  {
    // 6502: NLIN2 -- the ends are 0 and 255, so the line runs to 255 rather than to the
    // edge of the drawing area, and its right end lands in the margin.
    const Line rule{0u, _y, 255u, _y};
    (void)DrawLine(_canvas, rule);
    if (_picture != nullptr)
    {
      DrawLine2x(*_picture, rule);
    }
  }

  void DrawLongRangeChart(Universe& _universe, Ports& _ports, const ChartView& _view, const SystemSeeds& _galaxy) noexcept
  {
    // 6502: column 7, then token 199 -- the title, seven cells in.
    _universe.text.column = 7;
    _ports.printer.Print(TITLE_LONG_RANGE);

    // 6502: NLIN -- the rule under the title, and then a second rule at 152, under the chart.
    DrawTitleRule(_universe.canvas, _universe.text, &_universe.picture);
    DrawSeparator(_universe.canvas, LONG_RANGE_RULE_BOTTOM, &_universe.picture);

    // 6502: TT14 -- the fuel circle, before the dots rather than after.
    DrawFuelRange(_universe, _view);

    /*
     * 6502: TT83 -- 256 systems, and each one is a single PIXEL call.
     *
     * The x coordinate is the seed byte itself, the y is another seed byte halved, and the SIZE
     * comes from a third: forcing two bits on turns `QQ15+4` into something `PIXEL` reads as a
     * distance, so a system's dot is large or small according to a byte that means nothing else.
     */
    SystemSeeds seeds = _galaxy;
    for (int system = 0; system < 256; ++system)
    {
      const std::uint8_t distance = static_cast<std::uint8_t>(seeds.bytes[4] | 0x50u); // 6502: into ZZ
      const std::uint8_t y = AddWithCarry(static_cast<std::uint8_t>(seeds.bytes[1] >> 1), LONG_RANGE_TOP, false).value;
      PlotPixel(_universe.canvas, seeds.bytes[3], y, distance);
      // The map's dots are eight-bit chart coordinates with nothing under them, so the wide dot is
      // those numbers doubled. RS-5 re-flows the map itself; this puts it on the surface.
      PlotPixel2x(_universe.picture, 2 * static_cast<int>(seeds.bytes[3]), 2 * static_cast<int>(y), distance);
      NextSystem(seeds);
    }

    // 6502: the fall-through into TT15 with QQ19 set from QQ9 and QQ10.
    DrawTargetCrosshairs(_universe.canvas, _view, &_universe.picture);
  }

  void DrawShortRangeChart(Universe& _universe, Ports& _ports, const ChartView& _view, const SystemSeeds& _galaxy,
                           TextPrinter* _wideLabels) noexcept
  {
    /*
     * 6502: column 7, then token 190 through `NLIN3`.
     *
     * The 190 is the TITLE TOKEN, not a row. NLIN3 prints whatever is in A and then falls into
     * NLIN4, which loads 19 for itself -- so this chart's rule is at 19 and the long-range one's
     * is at 23, and neither number appears at the call site. NLIN4 also skips the INCYC that NLIN
     * does, so the cursor does not move here.
     */
    // 6502: TT23 opens by pushing `Yx2M1` and `dontclip` to 199 -- the clipper's limits, lifted
    // for the length of the routine because the discs go below the space view's floor.
    _universe.heaps.lowestVisibleRow = CHART_SCREEN_BOTTOM;
    _universe.clip.clippingOff = CHART_SCREEN_BOTTOM;

    _universe.text.column = 7;
    _ports.printer.Print(TITLE_SHORT_RANGE);
    DrawSeparator(_universe.canvas, SHORT_RANGE_RULE, &_universe.picture);

    DrawFuelRange(_universe, _view);
    DrawTargetCrosshairs(_universe.canvas, _view, &_universe.picture);

    /*
     * 6502: EE3 -- twenty-five bytes cleared, counting down from 24.
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
         * 6502: four times the x plus 104, and below twice the y plus 90.
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

        // 6502: the x divided by eight plus one -- the cell the name starts in.
        _universe.text.column = AddWithCarry(static_cast<std::uint8_t>(screenX >> 3), 1, false).value;

        /*
         * 6502: EE4 -- this row, then the one below it, then the one above.
         *
         * The name goes on its own row if that is free, else the row below, else the row above.
         * If all three are taken the system still gets its disc but no name -- which is why a
         * crowded chart has anonymous systems rather than overlapping text.
         */
        /*
         * 6502: the row's pixel coordinate divided by eight, into Y.
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
          _universe.text.row = static_cast<std::uint8_t>(row);

          // 6502: TT187 -- too near the top, so the system is skipped ENTIRELY. The
          // branch goes past the disc as well as past the name.
          if (row < FIRST_LABELLED_ROW)
          {
            drawDisc = false;
          }
          else
          {
            rowUsed[static_cast<std::size_t>(row)] = 0xFF;
            _ports.printer.SetCaseFlags(0x80);

            /*
             * Where the name goes on the 640x400 surface (Resolution.md section 6.3, slice RS-5-e).
             *
             * `TT23` puts a name beside its disc by dividing the disc's x by eight; the disc is
             * drawn at twice that x plus the space view's margin, so its wide cell is
             * `(2 * screenX + SPACE_VIEW_MARGIN) / 8`, and the name sits one cell right of it as it
             * does on the canvas. This is the only place that knows both numbers, which is why it
             * is the place that says so -- a layout maps one cell at a time and cannot move a run's
             * origin without moving its letters apart.
             */
            if (_wideLabels != nullptr)
            {
              const int discCell = (2 * static_cast<int>(screenX) + Picture::SPACE_VIEW_MARGIN) / 8;
              _wideLabels->SetLabelRun(static_cast<std::uint8_t>(row),
                                       static_cast<std::uint8_t>(TEXT_FIRST_COLUMN + _universe.text.column),
                                       discCell + 1);
            }

            /*
             * 6502: JSR cpl, and the carry it returns is the one the ADC below consumes. The CPY
             * that guarded this branch set the carry, and cpl's last seed twist then overwrote it.
             */
            SystemSeeds naming = seeds;
            carry = PrintSystemName(_ports.printer, naming);
          }
        }

        if (drawDisc)
        {
          /*
           * 6502: one bit of a seed byte added to 2 -- a radius of two or three.
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
          /*
           * 6502: TT23's ee1 -- the heap cleared, the sun drawn, the heap cleared again; a call
           * since M3-b-1b.
           *
           * The sun is drawn and then FORGOTTEN, twice over: the heap is cleared before so that
           * `SUN` has nothing to erase, and cleared after so that the next disc does not rub this
           * one out. A chart's discs are the one place the game draws suns it never intends to move.
           */
          ClearSunHeap(_universe.heaps);
          const Projection centre{screenX, 0u, screenY, 0u};
          DrawSun(_universe.canvas, _universe.heaps, _universe.math, _universe.rng, centre, radius, &_universe.picture);
          ClearSunHeap(_universe.heaps);
        }
      }

      NextSystem(seeds);
    }

    // The other half of `CHART_SCREEN_BOTTOM`'s note: the clipper back where the space view wants
    // it, which the routine does on its way out.
    _universe.clip.clippingOff = 0u;
    _universe.heaps.lowestVisibleRow = SPACE_VIEW_BOTTOM;
  }

  void PrintRangeError(TokenPrinter& _printer) noexcept
  {
    // 6502: TT147 -- token 202 and a question mark.
    _printer.Print(RANGE_TOKEN);
    _printer.Print('?');
  }

  void PrintCountdown(TextSink& _sink, TextState& _text, std::uint8_t _count) noexcept
  {
    /*
     * 6502: ee3 -- one loaded, then both cursor calls.
     *
     * One load feeding two calls: DOXC takes the accumulator and DOYC takes it again, unchanged.
     * So the countdown always sits at (1, 1) and neither cursor is the caller's to choose.
     */
    _text.column = 1;
    _text.row = 1;

    // 6502: `TT11` with three digits and no point, and the value arrives in
    // X with Y as its high byte, which is always zero here.
    PrintValue(_sink, _count, 3, false);
  }

  NearestSystem SelectNearestSystem(Canvas& _canvas, TokenPrinter& _printer, TextState& _text, ExtendedTextState& _sentences,
                                    MessageState& _message, ChartView& _view, const SystemSeeds& _galaxy, Picture* _picture) noexcept
  {
    // 6502: hm -- the crosshair off, the nearest system found, the crosshair on, then `CLYNS`. The
    // first call rubs it out, because `LOIN` draws by EOR and drawing it twice is how it moves.
    DrawTargetCrosshairs(_canvas, _view, _picture);

    const NearestSystem nearest = FindNearestSystem(_galaxy, _view.cursorX, _view.cursorY, _view.homeX, _view.homeY);
    _view.cursorX = nearest.x;
    _view.cursorY = nearest.y;

    DrawTargetCrosshairs(_canvas, _view, _picture);

    // 6502: CLYNS, as a tail call; it is `ClearMessageRows` and was a seam until M3-b-3b.
    ClearMessageRows(_canvas, _printer, _text, _sentences, _message, _picture, LayoutForView(_view.view));

    return nearest;
  }

  JumpOutcome RequestHyperspace(Canvas& _canvas, TokenPrinter& _printer, ExtendedTokenPrinter& _extended, TextState& _text,
                                ExtendedTextState& _sentences, MessageState& _message, ChartView& _view, JumpState& _jump,
                                const SystemSeeds& _galaxy, Picture* _picture) noexcept
  {
    if (_jump.docked != 0)
    {
      /*
       * 6502: dockEd -- the rows cleared, column 15, then token 205 through `DETOK`.
       *
       * The message is an EXTENDED token, which is why this routine needs both printers: the rest
       * of hyp prints recursive ones.
       */
      ClearMessageRows(_canvas, _printer, _text, _sentences, _message, _picture, LayoutForView(_view.view)); // 6502: CLYNS
      _text.column = 15;
      _extended.Print(DOCKED_TOKEN);
      return JumpOutcome::Docked;
    }

    // 6502: a countdown already running swallows the key.
    if (_jump.countdown != 0)
    {
      return JumpOutcome::Busy;
    }

    // 6502: Ghy -- the galactic hyperdrive, which reads the equipment the commander
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
      // 6502: the view's top two bits -- neither chart is showing, so nothing is selected.
      return JumpOutcome::Busy;
    }
    else
    {
      const NearestSystem nearest = SelectNearestSystem(_canvas, _printer, _text, _sentences, _message, _view, _galaxy, _picture);
      _jump.distance = nearest.distance;
      _jump.target = nearest.seeds;
    }

    // 6502: TTX111 -- a distance of zero is the system you are
    // already in, and the key does nothing at all -- not even a message.
    if (_jump.distance == 0)
    {
      return JumpOutcome::AlreadyThere;
    }

    // 6502: column 7, then row 23 on a chart and 17 in space.
    _text.column = 7;
    _text.row = (_view.view != 0) ? std::uint8_t{23} : std::uint8_t{17};

    _printer.SetCaseFlags(0);
    _printer.Print(HYPERSPACE_TOKEN);

    /*
     * 6502: goTT147 -- the distance's high byte first, then the fuel against its low byte.
     *
     * Two tests, not one. Anything 256 tenths or further fails on its HIGH byte before the fuel is
     * looked at, so a system 25.6 light years away is out of range with a full tank -- and says the
     * same thing it says when the tank is empty.
     */
    if (!_view.fuel.Reaches(_jump.distance))
    {
      PrintRangeError(_printer);
      return JumpOutcome::OutOfRange;
    }

    _printer.Print(TO_TOKEN);

    SystemSeeds naming = _jump.target;
    PrintSystemName(_printer, naming);

    // 6502: wW and wW2 -- fifteen into both bytes of the countdown, then `ee3`. The one that is
    // printed is the one left in X.
    _jump.countdown = COUNTDOWN_START;
    _jump.counter = COUNTDOWN_START; // 6502: into QQ22 -- the tick within a step, not only the number shown
    PrintCountdown(_extended.Characters(), _text, COUNTDOWN_START);
    return JumpOutcome::CountingDown;
  }

  bool FindSystemByName(TokenPrinter& _printer, CharacterPrinter& _characters, ChartView& _view, const SystemSeeds& _galaxy,
                        std::span<const std::uint8_t> _typed) noexcept
  {
    SystemSeeds seeds = _galaxy;

    for (int system = 0; system < 256; ++system)
    {
      // 6502: MT14 -- justification on, so the name goes into the buffer rather than the
      // screen. This is the whole trick, and it is why the search needed slice 1c-c-b.
      _characters.State().justify = 0x80;
      _characters.State().bufferLength = 0;

      SystemSeeds naming = seeds;
      PrintSystemName(_printer, naming);

      const std::size_t length = _characters.State().bufferLength;

      /*
       * 6502: the buffer's length indexes into the typed name, and the byte there must be a
       * carriage return: the typed name must END where the printed one does. A shorter or longer
       * entry fails on this one comparison rather than on the letters.
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

          // 6502: MT15 -- justification off again, and the buffer thrown away.
          _characters.State().justify = 0;
          _characters.State().bufferLength = 0;
          return true;
        }
      }

      NextSystem(seeds);
    }

    _characters.State().justify = 0;
    _characters.State().bufferLength = 0;
    return false;
  }

} // namespace Elite

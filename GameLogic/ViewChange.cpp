#include "pch.h"

#include "ViewChange.h"

#include "Charts.h"
#include "FlightLoop.h"
#include "LookupTables.h"

namespace Elite
{

  void ZeroPageDown(Canvas& _canvas, std::uint16_t _pageBase, std::uint8_t _first) noexcept
  {
    std::uint8_t y = _first;

    do
    {
      _canvas.Write(static_cast<std::uint16_t>(_pageBase + y), 0u); // 6502: .ZEL1k STA (SC),Y
      y = static_cast<std::uint8_t>(y - 1u);                        // 6502: DEY
    } while (y != 0u); // 6502: BNE ZEL1k
  }

  void CopyPagesDown(Canvas& _canvas, const std::uint8_t* _from, std::uint16_t _to, std::uint8_t _pages, std::uint8_t _first) noexcept
  {
    std::uint8_t y = _first;
    std::uint16_t source = 0;
    std::uint16_t target = _to;
    std::uint8_t pages = _pages;

    for (;;)
    {
      do
      {
        // 6502: LDA (V),Y / STA (SC),Y.
        _canvas.Write(static_cast<std::uint16_t>(target + y), _from[source + y]);
        y = static_cast<std::uint8_t>(y - 1u); // 6502: DEY
      } while (y != 0u); // 6502: BNE mvbllop

      source = static_cast<std::uint16_t>(source + 256u); // 6502: INC V+1
      target = static_cast<std::uint16_t>(target + 256u); // 6502: INC SC+1

      pages = static_cast<std::uint8_t>(pages - 1u); // 6502: DEX
      if (pages == 0u)                               // 6502: BNE mvbllop
      {
        return;
      }
    }
  }

  void DrawScreenRule(Canvas& _canvas, DrawWorkspace& _draw, std::uint8_t _row) noexcept
  {
    _draw.y1 = _row; // 6502: STX Y1
    _draw.x1 = 0u;   // 6502: LDX #0 / STX X1
    _draw.x2 = 255u; // 6502: DEX / STX X2

    DrawHorizontalLine(_canvas, _draw); // 6502: JMP HLOIN, a tail call
  }

  void ToggleVerticalEdge(Canvas& _canvas, std::uint16_t _cell, std::uint8_t _pattern, std::uint8_t _rows) noexcept
  {
    std::uint16_t cell = _cell;

    for (std::uint8_t row = _rows; row != 0u; --row) // 6502: .BOXL2 ... DEX / BNE BOXL2
    {
      for (int line = 7; line >= 0; --line) // 6502: LDY #7 / .BOXL3 ... DEY / BPL BOXL3
      {
        const std::uint16_t at = static_cast<std::uint16_t>(cell + line);

        // 6502: LDA R2 / EOR (SC),Y / STA (SC),Y -- an EOR, so twice puts it back.
        _canvas.Write(at, static_cast<std::uint8_t>(_canvas.Read(at) ^ _pattern));
      }

      cell = static_cast<std::uint16_t>(cell + 0x140u); // 6502: SC += &140, one character row
    }
  }

  void DrawColourBand(Canvas& _canvas, std::uint16_t _cell) noexcept
  {
    std::uint16_t cell = _cell;

    for (std::uint8_t row = 18u; row != 0u; --row) // 6502: LDX #18 / .BLUEL2 ... DEX / BNE BLUEL2
    {
      for (int offset = 23; offset >= 0; --offset) // 6502: LDY #23 / .BLUEL1 ... DEY / BPL BLUEL1
      {
        // 6502: LDA #%11111111 / STA (SC),Y -- a STORE, unlike `BOXS2` above it.
        _canvas.Write(static_cast<std::uint16_t>(cell + offset), 0xFFu);
      }

      cell = static_cast<std::uint16_t>(cell + 0x140u);
    }
  }

  void DrawColourBands(Canvas& _canvas) noexcept
  {
    DrawColourBand(_canvas, 0u);       // 6502: LDX #LO(SCBASE) / LDY #HI(SCBASE) / JSR BLUEBANDS
    DrawColourBand(_canvas, 37u * 8u); // 6502: SCBASE+37*8, and it FALLS INTO BLUEBANDS
  }

  void DrawBorder(Canvas& _canvas, DrawWorkspace& _draw, std::uint8_t _rows) noexcept
  {
    _draw.t2 = _rows; // 6502: STX T2

    // 6502: LDY #LO(SCBASE+3*8) / STY SC / LDY #HI(SCBASE+3*8) / LDA #%00000011 / JSR BOXS2.
    ToggleVerticalEdge(_canvas, 3u * 8u, 0x03u, _rows);

    // 6502: the same again at cell 36 with the opposite two pixels, and the count comes back out
    // of `T2` rather than out of X -- `BOXS2` leaves X at zero.
    ToggleVerticalEdge(_canvas, 36u * 8u, 0xC0u, _draw.t2);

    // 6502: LDA #1 / STA SCBASE+&118 -- one byte, in cell 35 of the top character row.
    _canvas.Write(0x118u, 1u);

    DrawScreenRule(_canvas, _draw, 0u); // 6502: LDX #0, and it falls into BOXS
  }

  void ForgetScannerBlips(Bubble& _bubble) noexcept
  {
    // 6502: LDX #0 / .zonkL LDA FRIN,X / BEQ zonk1 -- it stops at the first empty slot, which is
    // what makes `FRIN`'s terminator a terminator.
    for (std::size_t slot = 0; slot < _bubble.slots.size(); ++slot)
    {
      const std::uint8_t type = _bubble.slots[slot];
      if (type == 0u)
      {
        return;
      }

      // 6502: BMI zonk2 -- the planet and the sun have negative types and no blip to forget.
      if ((type & 0x80u) != 0u)
      {
        continue;
      }

      // 6502: JSR GINF / LDY #31 / LDA (INF),Y / AND #%11101111 / STA (INF),Y.
      ShipBlock& block = _bubble.blocks[slot];
      block[31] = static_cast<std::uint8_t>(block[31] & 0xEFu);
    }
  }

  void HideAllSprites(SightEffects& _effects) noexcept
  {
    _effects.SetRasterMode(0x05u);  // 6502: LDA #%101 / JSR SETL1
    _effects.SetSpritesEnabled(0u); // 6502: LDA #%00000000 / STA VIC+&15
    _effects.SetRasterMode(0x04u);  // 6502: LDA #%100, and it falls into SETL1
  }

  void ShowDashboard(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, GeometryWorkspace& _geometry, ScreenState& _screen,
                     Bubble& _bubble, const FlightState& _flight, const FlightStatus& _status, std::uint8_t _fuel, Compass& _compass,
                     SightEffects& _effects) noexcept
  {
    // 6502: JSR BOX2 -- at its label, so eighteen rows: the space view's height (§6.79).
    DrawBorder(_canvas, _draw, BORDER_ROWS_SPACE_VIEW);

    _screen.colourBank = COLOUR_BANK_DASHBOARD; // 6502: LDA #&91 / STA abraxas
    _screen.bitmapMode = BITMAP_MODE_DASHBOARD; // 6502: LDA #%11010000 / STA caravanserai

    // 6502: LDA DFLAG / BNE nearlyxmas -- the dashboard is already there, so skip the expensive
    // half. The border above and the bands below happen either way.
    if (_screen.dashboardShown == 0u)
    {
      /*
       * 6502: LDX #8 / V = DSTORE% / SC = DLOC% / JSR mvblockK, then LDY #&C0 / LDX #1 /
       * JSR mvbllop with `V` and `SC` still where the first call left them.
       *
       * 2,240 bytes, and NOT the first 2,240: the second entry stores at Y and counts down to 1,
       * so offset 2,048 is skipped and offset 2,240 is written (§6.78).
       */
      CopyPagesDown(_canvas, DASHBOARD_IMAGE.data(), DASHBOARD_BITMAP, 8u, 0u);
      CopyPagesDown(_canvas, DASHBOARD_IMAGE.data() + 8u * 256u, static_cast<std::uint16_t>(DASHBOARD_BITMAP + 8u * 256u), 1u, 0xC0u);

      ForgetScannerBlips(_bubble); // 6502: JSR zonkscanners

      // 6502: JSR DIALS -- all seven dials and the compass, on a dashboard that has just arrived
      // as a picture with every bar empty.
      DrawDials(_canvas, _draw, _math, _geometry, _flight, _status, _fuel, _compass, _bubble);
    }

    DrawColourBands(_canvas); // 6502: .nearlyxmas JSR BLUEBAND
    HideAllSprites(_effects); // 6502: JSR NOSPRITES

    _screen.dashboardShown = 0xFFu; // 6502: LDA #&FF / STA DFLAG
  }

  void SetUpScreenPixels(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, GeometryWorkspace& _geometry, TextState& _text,
                         ScreenState& _screen, Bubble& _bubble, const FlightState& _flight, const FlightStatus& _status, std::uint8_t _fuel,
                         Compass& _compass, SightEffects& _effects, std::uint8_t _view) noexcept
  {
    /*
     * 6502: LDA #&04 / STA SC / LDA #&60 / STA SC+1 / LDX #24 / .BOL3 LDA #&10 / LDY #31 /
     * .BOL4 STA (SC),Y / DEY / BPL BOL4 / SC += 40 / DEX / BNE BOL3.
     *
     * The colour bytes, not the bitmap: `&6004` is the first block of screen RAM, four cells in,
     * which is where the left margin ends. Thirty-two cells a row for twenty-four rows, in steps of
     * forty -- so the four cells either side keep whatever they had.
     */
    std::uint16_t cell = static_cast<std::uint16_t>(Canvas::SCREEN_CELLS + 4u);
    for (std::uint8_t row = 24u; row != 0u; --row)
    {
      for (int offset = 31; offset >= 0; --offset)
      {
        _canvas.Write(static_cast<std::uint16_t>(cell + offset), TEXT_COLOUR_WHITE);
      }
      cell = static_cast<std::uint16_t>(cell + 40u);
    }

    /*
     * 6502: LDX #HI(SCBASE) / .BOL1 JSR ZES1k / INX / CPX #HI(DLOC%) / BNE BOL1 -- the bitmap, as
     * far as the dashboard.
     *
     * THE COMPARE IS AGAINST THE PAGE AND NOT THE ADDRESS. `DLOC%` is &5680, so the loop stops when
     * X reaches &56 and leaves it there -- which is where the partial page below starts AND where
     * the text screen's second loop picks up. A port that ran to `DLOC%` itself would zero one page
     * too many here and start the second loop one page too late.
     */
    std::uint16_t page = 0;
    const std::uint16_t dashboardPage = static_cast<std::uint16_t>(DASHBOARD_BITMAP & 0xFF00u);
    for (; page < dashboardPage; page = static_cast<std::uint16_t>(page + 256u))
    {
      ZeroWholePage(_canvas, page);
    }

    // 6502: LDY #LO(DLOC%)-1 / JSR ZES2k / STA (SC),Y -- the partial page, and then by hand the one
    // byte `ZES2k` walks past because it stops at zero rather than through it.
    ZeroPageDown(_canvas, page, static_cast<std::uint8_t>((DASHBOARD_BITMAP & 0xFFu) - 1u));
    _canvas.Write(page, 0u);

    _text.column = 1u; // 6502: LDA #1 / STA XC
    _text.row = 1u;    // 6502: STA YC

    // 6502: LDA QQ11 / BEQ wantSTEP / CMP #13 / BNE P%+5 / .wantSTEP JMP wantdials -- a tail call,
    // so the space view and view 13 never reach anything below this.
    if (_view == 0u || _view == 13u)
    {
      ShowDashboard(_canvas, _draw, _math, _geometry, _screen, _bubble, _flight, _status, _fuel, _compass, _effects);
      return;
    }

    _screen.colourBank = 0x81u; // 6502: LDA #&81 / STA abraxas -- screen RAM at &6000
    _screen.bitmapMode = 0xC0u; // 6502: LDA #%11000000 / STA caravanserai

    // 6502: .BOL2 JSR ZES1k / INX / CPX #HI(SCBASE)+&20 / BNE BOL2 -- and X is still where the
    // first loop left it, so this clears the dashboard's part of the bitmap as well.
    for (; page < Canvas::SCREEN_CELLS; page = static_cast<std::uint16_t>(page + 256u))
    {
      ZeroWholePage(_canvas, page);
    }

    _compass.colour = 0u;        // 6502: LDX #0 / STX COMC
    _screen.dashboardShown = 0u; // 6502: STX DFLAG
    _text.column = 1u;           // 6502: INX / STX XC
    _text.row = 1u;              // 6502: STX YC

    DrawColourBands(_canvas);    // 6502: JSR BLUEBAND
    ForgetScannerBlips(_bubble); // 6502: JSR zonkscanners
    HideAllSprites(_effects);    // 6502: JSR NOSPRITES

    // 6502: LDY #31 / LDA #&70 / .BOL5 STA &6004,Y / DEY / BPL BOL5 -- the top row's colour band.
    for (int offset = 31; offset >= 0; --offset)
    {
      _canvas.Write(static_cast<std::uint16_t>(Canvas::SCREEN_CELLS + 4u + offset), 0x70u);
    }

    // 6502: LDX QQ11 / CPX #2 / BEQ BOX / CPX #64 / BEQ BOX / CPX #128 / BEQ BOX -- three views
    // stop at one band; everything else gets the second one two rows down.
    if (_view != 2u && _view != 64u && _view != 128u)
    {
      for (int offset = 31; offset >= 0; --offset)
      {
        _canvas.Write(static_cast<std::uint16_t>(Canvas::SCREEN_CELLS + 0x54u + offset), 0x70u);
      }
    }

    DrawScreenRule(_canvas, _draw, 199u); // 6502: .BOX LDX #199 / JSR BOXS

    _canvas.Write(0x1F1Fu, 0xFFu); // 6502: LDA #&FF / STA SCBASE+&1F1F

    // 6502: LDX #25 / EQUB &2C -- and the `&2C` eats `BOX2`'s own `LDX #18`, so the border is the
    // whole screen's height rather than the space view's (§6.79).
    DrawBorder(_canvas, _draw, BORDER_ROWS_TEXT_SCREEN);
  }

  void SetUpScreen(FlightScreen& _screen, std::uint8_t _view) noexcept
  {
    _screen.view = _view; // 6502: .TT66 STA QQ11, and then it falls into TTX66

    // 6502: JSR MT2 -- LDA #32 / STA DTW1 / LDA #0 / STA DTW6. Sentence case for the extended
    // printer, which is the first thing a new screen is put back to.
    _screen.extended.lowerCaseBits = 32u;
    _screen.extended.alwaysLower = 0u;

    _screen.heaps.lsp = 0u; // 6502: LDA #0 / STA LSP -- the ball heap is forgotten

    /*
     * 6502: LDA #%10000000 / STA QQ17 / STA DTW2.
     *
     * BOTH OF THEM, AND ONLY ONE KEEPS IT. `QQ17` is put back to zero five bytes from the end, so
     * what a caller sees is ALL CAPS with `DTW2` still at 128 (§6.29). The intermediate 128 is not
     * dead: the view's name below is printed with it, which is why the port cannot collapse the two
     * stores the way `SetUpTextScreen` does -- that version is only correct because the half slice
     * 2e left out is the half that observes the value in between (§6.81).
     */
    _screen.printer.SetCaseFlags(0x80u);
    _screen.text.caseFlags = 0x80u;
    _screen.extended.sentenceStart = 0x80u;

    ClearSunHeap(_screen.heaps); // 6502: JSR FLFLLS -- and the sun's heap with it

    _screen.status.viewLaser = 0u; // 6502: LDA #0 / STA LAS2 -- stop any laser pulsing
    _screen.message.delay = 0u;    // 6502: STA DLY
    _screen.message.append = 0u;   // 6502: STA de

    _screen.text.column = 1u; // 6502: LDA #1 / STA XC
    _screen.text.row = 1u;    // 6502: STA YC

    SetUpScreenPixels(_screen.canvas, _screen.draw, _screen.math, _screen.geometry, _screen.text, _screen.screen, _screen.bubble,
                      _screen.flight, _screen.status, _screen.commander.At(Field::Fuel), _screen.compass, _screen.sight,
                      _screen.view); // 6502: JSR TTX66K

    // 6502: LDX QQ22+1 / BEQ OLDBOX / JSR ee3 -- the hyperspace countdown outlives a screen change
    // and is reprinted, because the screen it was on has just been wiped.
    if (_screen.status.hyperspaceCountdown != 0u)
    {
      PrintCountdown(_screen.sink, _screen.text, _screen.status.hyperspaceCountdown);
    }

    _screen.text.row = 1u; // 6502: .OLDBOX LDA #1 / JSR DOYC

    // 6502: LDA QQ11 / BNE tt66 -- the view's name belongs to the space view alone.
    if (_screen.view == 0u)
    {
      _screen.text.column = 11u; // 6502: LDA #11 / JSR DOXC

      // 6502: LDA VIEW / ORA #&60 / JSR TT27 -- views 0 to 3 become tokens 96 to 99.
      _screen.printer.Print(static_cast<std::uint8_t>(_screen.spaceView | 0x60u));
      PrintSpace(_screen.printer); // 6502: JSR TT162
      _screen.printer.Print(175u); // 6502: LDA #175 / JSR TT27 -- "VIEW"
    }

    // 6502: .tt66 LDX #1 / STX XC / STX YC / DEX / STX QQ17.
    _screen.text.column = 1u;
    _screen.text.row = 1u;
    _screen.printer.SetCaseFlags(0u);
    _screen.text.caseFlags = 0u;
  }

  void ChangeView(FlightScreen& _screen, std::uint8_t _to) noexcept
  {
    _screen.effects.SetPalette(0u); // 6502: LDA #0 / JSR DOVDU19

    // 6502: LDY QQ11 / BNE LQ -- a chart or a text screen takes the short path.
    if (_screen.view != 0u)
    {
      _screen.spaceView = _to;  // 6502: .LQ STX VIEW
      SetUpScreen(_screen, 0u); // 6502: JSR TT66, with A zero, so it becomes the space view

      DrawLaserSights(_screen.canvas, _screen.math, _screen.commander, _screen.trumbleSprites, _screen.spaceView,
                      _screen.sight); // 6502: JSR SIGHT

      // 6502: JMP NWSTARS -- a whole new field, because there was no space view to keep.
      SeedStardustAndClearShips(_screen.canvas, _screen.draw, _screen.dust, _screen.rng, _screen.heaps, _screen.bubble, _screen.work,
                                _screen.flight, _screen.view, false);
      return;
    }

    // 6502: CPX VIEW / BEQ LO2 -- already looking that way, so `LO2`'s bare RTS. The palette above
    // has happened anyway, and that is the whole of what this path does.
    if (_to == _screen.spaceView)
    {
      return;
    }

    _screen.spaceView = _to;  // 6502: STX VIEW
    SetUpScreen(_screen, 0u); // 6502: JSR TT66

    // 6502: JSR FLIP -- the dust is MIRRORED rather than replaced, which is why the stars look
    // familiar for a moment after a view change.
    FlipStardust(_screen.canvas, _screen.draw, _screen.dust);

    // 6502: JSR WPSHPS, and then it falls into SIGHT.
    ClearAllShips(_screen.canvas, _screen.draw, _screen.heaps, _screen.bubble, _screen.work, _screen.flight, _screen.view);

    DrawLaserSights(_screen.canvas, _screen.math, _screen.commander, _screen.trumbleSprites, _screen.spaceView, _screen.sight);
  }

  void Warp(FlightScreen& _screen) noexcept
  {
    /*
     * 6502: LDX JUNK / LDA FRIN+2,X / ORA SSPR / ORA MJ / BNE WA1.
     *
     * The junk count doubles as an index: every slot up to `JUNK` holds junk, so `FRIN+2,X` is the
     * slot two beyond it -- and a non-zero type there means something worth staying for. `SSPR` is
     * `MANY+SST`, the station count (§6.58), and `MJ` is witchspace.
     */
    const std::size_t slot = static_cast<std::size_t>(_screen.bubble.junk) + 2u;
    const std::uint8_t occupied = (slot < _screen.bubble.slots.size()) ? _screen.bubble.slots[slot] : 0u;
    const std::uint8_t station = _screen.bubble.counts[SHIP_TYPE_STATION];

    if ((occupied | station | _screen.status.midJump) != 0u)
    {
      (void)_screen.effects.PlaySound(SOUND_BOOP, false); // 6502: .WA1 LDY #sfxboop / JMP NOISE
      return;
    }

    /*
     * 6502: LDY K%+8 / BMI WA3 / TAY / JSR MAS2 / CMP #2 / BCC WA1.
     *
     * `LDY K%+8` is read for its FLAGS alone -- `TAY` throws the Y it just loaded away and puts the
     * accumulator's zero there instead, which is slot 0. So the `LDY` is a sign test on the
     * planet's z and the `TAY` is `MAS2`'s argument, two instructions apart and unrelated.
     *
     * A negative z is a body BEHIND you, and you cannot warp into something behind you, so its
     * distance is not tested at all.
     */
    if ((_screen.bubble.blocks[0][8] & 0x80u) == 0u)
    {
      if (LargestAxis(_screen.bubble, 0u) < 2u)
      {
        (void)_screen.effects.PlaySound(SOUND_BOOP, false);
        return;
      }
    }

    // 6502: .WA3 LDY K%+NI%+8 / BMI WA2 / LDY #NI% / JSR m / CMP #2 / BCC WA1 -- the same for the
    // sun, through `m` rather than `MAS2` because there is no accumulator worth keeping this time.
    if ((_screen.bubble.blocks[1][8] & 0x80u) == 0u)
    {
      if (LargestAxis(_screen.bubble, 1u) < 2u)
      {
        (void)_screen.effects.PlaySound(SOUND_BOOP, false);
        return;
      }
    }

    // 6502: .WA2 LDA #&81 / STA S / STA R / STA P -- &81 is -1 in sign-magnitude with the low bit
    // set, so `ADD` subtracts the same fixed amount from each body's z.
    _screen.math.s = 0x81u;
    _screen.math.r = 0x81u;
    _screen.math.p = 0x81u;

    // 6502: LDA K%+8 / JSR ADD / STA K%+8, and the same for the sun.
    _screen.bubble.blocks[0][8] = AddSigned(_screen.math, _screen.bubble.blocks[0][8]).high;
    _screen.bubble.blocks[1][8] = AddSigned(_screen.math, _screen.bubble.blocks[1][8]).high;

    /*
     * 6502: LDA #1 / STA QQ11 / STA MCNT / LSR A / STA EV / LDX VIEW / JMP LOOK1.
     *
     * `QQ11 = 1` IS A LIE TOLD TO `LOOK1`. The view is not changing -- X is `VIEW` itself -- so the
     * space-view path would take `CPX VIEW / BEQ LO2` and do nothing at all. Setting `QQ11` to
     * something non-zero first sends `LOOK1` down `LQ` instead, which clears the screen and seeds a
     * WHOLE NEW stardust field, and `TT66` puts `QQ11` back to zero on the way. One store, to make
     * a routine take the other branch.
     */
    _screen.view = 1u;
    _screen.flight.mainLoopCounter = 1u;
    _screen.explosions = 0u; // 6502: LSR A -- one shifted right is zero

    ChangeView(_screen, _screen.spaceView);
  }

} // namespace Elite

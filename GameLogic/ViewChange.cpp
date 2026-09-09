#include "pch.h"

#include "ViewChange.h"

#include "Charts.h"
#include "Dashboard2x.h"
#include "FlightLoop.h"
#include "Lines2x.h"
#include "LookupTables.h"

namespace Elite
{

  void ZeroPageDown(Canvas& _canvas, std::uint16_t _pageBase, std::uint8_t _first, Picture* _picture) noexcept
  {
    std::uint8_t y = _first;

    do
    {
      _canvas.Write(static_cast<std::uint16_t>(_pageBase + y), 0u); // The store through `SC`
      if (DrawingTwins(_picture))
      {
        WriteBitmapByte2x(*_picture, static_cast<std::uint16_t>(_pageBase + y), 0u, false);
      }
      y = static_cast<std::uint8_t>(y - 1u);
    } while (y != 0u); // Round again while the index is not zero
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
        // A byte in through `V`, out through `SC`.
        _canvas.Write(static_cast<std::uint16_t>(target + y), _from[source + y]);
        y = static_cast<std::uint8_t>(y - 1u);
      } while (y != 0u); // BNE mvbllop

      source = static_cast<std::uint16_t>(source + 256u); // The source page steps up
      target = static_cast<std::uint16_t>(target + 256u); // And the target's

      pages = static_cast<std::uint8_t>(pages - 1u);
      if (pages == 0u)                               // BNE mvbllop
      {
        return;
      }
    }
  }

  void DrawScreenRule(Canvas& _canvas, std::uint8_t _row, Picture* _picture) noexcept
  {
    // The row, then 0 and 255 as its ends, and a tail call into HLOIN.
    DrawHorizontalLine(_canvas, 0u, 255u, _row);
    if (DrawingTwins(_picture))
    {
      DrawCanvasRow2x(*_picture, 0u, 255u, _row);
    }
  }

  void ToggleVerticalEdge(Canvas& _canvas, std::uint16_t _cell, std::uint8_t _pattern, std::uint8_t _rows, Picture* _picture) noexcept
  {
    std::uint16_t cell = _cell;

    for (std::uint8_t row = _rows; row != 0u; --row) // BOXL2, counting rows down
    {
      for (int line = 7; line >= 0; --line) // BOXL3, eight pixel rows down through zero
      {
        const std::uint16_t at = static_cast<std::uint16_t>(cell + line);

        // The pattern EORed into the byte -- so doing it twice puts it back.
        _canvas.Write(at, static_cast<std::uint8_t>(_canvas.Read(at) ^ _pattern));
        if (DrawingTwins(_picture))
        {
          WriteBitmapByte2x(*_picture, at, _pattern, true);
        }
      }

      cell = static_cast<std::uint16_t>(cell + 0x140u); // SC += &140, one character row
    }
  }

  void DrawFullBorder(Canvas& _canvas, Picture* _picture) noexcept
  {
    DrawScreenRule(_canvas, BOTTOM_RULE_ROW, _picture); // BOXS on row 199

    // The corner byte the rule stops one short of, filled by hand. The canvas is laid out
    // from SCBASE contiguously, so the address IS the offset.
    _canvas.Write(BOTTOM_RIGHT_CORNER, 0xFFu);
    if (DrawingTwins(_picture))
    {
      WriteBitmapByte2x(*_picture, BOTTOM_RIGHT_CORNER, 0xFFu, false);
    }

    // 25 rows, and the stray byte after it swallows `BOX2`'s own count, so 25 is what
    // falls through (§6.79).
    DrawBorder(_canvas, BORDER_ROWS_TEXT_SCREEN, _picture);
  }

  void DrawColourBand(Canvas& _canvas, std::uint16_t _cell, Picture* _picture) noexcept
  {
    std::uint16_t cell = _cell;

    for (std::uint8_t row = 18u; row != 0u; --row) // BLUEL2, eighteen rows
    {
      for (int offset = 23; offset >= 0; --offset) // BLUEL1, twenty-four cells down through zero
      {
        // Every bit set, and a STORE rather than the EOR `BOXS2` above it uses.
        _canvas.Write(static_cast<std::uint16_t>(cell + offset), 0xFFu);
        if (DrawingTwins(_picture))
        {
          WriteBitmapByte2x(*_picture, static_cast<std::uint16_t>(cell + offset), 0xFFu, false);
        }
      }

      cell = static_cast<std::uint16_t>(cell + 0x140u);
    }
  }

  void DrawColourBands(Canvas& _canvas, Picture* _picture) noexcept
  {
    DrawColourBand(_canvas, 0u, _picture);       // BLUEBANDS at the top of the screen
    DrawColourBand(_canvas, 37u * 8u, _picture); // SCBASE+37*8, and it FALLS INTO BLUEBANDS
  }

  void DrawBorder(Canvas& _canvas, std::uint8_t _rows, Picture* _picture) noexcept
  {
    // BOXS2 at cell 3, with the left two pixels.
    ToggleVerticalEdge(_canvas, 3u * 8u, 0x03u, _rows, _picture);

    // The original parks the count in the kernel's byte because `BOXS2` clobbers X.
    // The port passes it, so there is nothing to park and nothing to read back.
    // The same again at cell 36 with the opposite two pixels, and the count comes back out
    // of `T2` rather than out of X -- `BOXS2` leaves X at zero.
    ToggleVerticalEdge(_canvas, 36u * 8u, 0xC0u, _rows, _picture);

    // One byte, in cell 35 of the top character row.
    _canvas.Write(0x118u, 1u);
    if (DrawingTwins(_picture))
    {
      WriteBitmapByte2x(*_picture, 0x118u, 1u, false);
    }

    DrawScreenRule(_canvas, 0u, _picture); // Row 0, and it falls into BOXS
  }

  void ForgetScannerBlips(Bubble& _bubble) noexcept
  {
    // It stops at the first empty slot, which is what makes `FRIN`'s terminator a
    // terminator.
    for (std::size_t slot = 0; slot < _bubble.slots.size(); ++slot)
    {
      const std::uint8_t type = _bubble.slots[slot];
      if (type == 0u)
      {
        return;
      }

      // BMI zonk2 -- the planet and the sun have negative types and no blip to forget.
      if ((type & 0x80u) != 0u)
      {
        continue;
      }

      // The state byte read through `INF`, masked, and written back.
      Ship& block = _bubble.blocks[slot];
      block.state = Without(block.state, ShipStateBit::OnScanner);
    }
  }

  void HideAllSprites(VideoState& _video, MemoryMap& _map) noexcept
  {
    SetMemoryMap(_map, MEMORY_MAP_IO);  // SETL1 with the I/O map
    ApplySpritesEnabled(_video, 0u);    // Every sprite disabled
    SetMemoryMap(_map, MEMORY_MAP_RAM); // The RAM map, and it falls into SETL1
  }

  void ShowDashboard(Canvas& _canvas, DrawWorkspace& _draw, ScreenState& _screen, Bubble& _bubble, const FlightState& _flight,
                     const FlightStatus& _status, LightYearsTenths _fuel, Compass& _compass, VideoState& _video, MemoryMap& _map,
                     Picture* _picture) noexcept
  {
    // BOX2 at its label, so eighteen rows: the space view's height (§6.79).
    DrawBorder(_canvas, BORDER_ROWS_SPACE_VIEW, _picture);

    _screen.colourBank = COLOUR_BANK_DASHBOARD; // &91 into abraxas
    _screen.bitmapMode = BITMAP_MODE_DASHBOARD; // The dashboard's mode into caravanserai

    // A set `DFLAG` skips to nearlyxmas -- the dashboard is already there, so skip the
    // expensive half. The border above and the bands below happen either way.
    if (_screen.dashboardShown == 0u)
    {
      /*
       * Eight whole pages through `mvblockK`, then one more entered at `mvbllop` with `V`
       * and `SC` still where the first call left them.
       *
       * 2,240 bytes, and NOT the first 2,240: the second entry stores at Y and counts down to 1,
       * so offset 2,048 is skipped and offset 2,240 is written (§6.78).
       */
      CopyPagesDown(_canvas, DASHBOARD_IMAGE.data(), DASHBOARD_BITMAP, 8u, 0u);
      CopyPagesDown(_canvas, DASHBOARD_IMAGE.data() + 8u * 256u, static_cast<std::uint16_t>(DASHBOARD_BITMAP + 8u * 256u), 1u, 0xC0u);

      if (DrawingTwins(_picture))
      {
        // The same picture on the index plane, as sixteen-colour art at 640x112 (Dashboard2x.h).
        // The twins below draw over it at twice the detail from here.
        CopyDashboardPicture2x(*_picture);
      }

      ForgetScannerBlips(_bubble); // JSR zonkscanners

      // All seven dials and the compass, on a dashboard that has just arrived
      // as a picture with every bar empty.
      DrawDials(_canvas, _draw, _flight, _status, _fuel, _compass, _bubble, _picture);
    }

    DrawColourBands(_canvas, _picture); // BLUEBAND
    HideAllSprites(_video, _map);

    _screen.dashboardShown = 0xFFu; // &FF into DFLAG
  }

  void SetUpScreenPixels(Canvas& _canvas, DrawWorkspace& _draw, TextState& _text, ScreenState& _screen, Bubble& _bubble,
                         const FlightState& _flight, const FlightStatus& _status, LightYearsTenths _fuel, Compass& _compass, VideoState& _video,
                         MemoryMap& _map, std::uint8_t _view, Picture* _picture) noexcept
  {
    /*
     * BOL3 over BOL4 -- one colour byte written into a rectangle of cells.
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
      if (DrawingTwins(_picture))
      {
        // The four wide cells each of those thirty-two becomes. The offset is a SCREEN RAM address,
        // so the canvas cell is what it is past `SCREEN_CELLS`.
        SetCellRun2x(*_picture, static_cast<int>(cell - Canvas::SCREEN_CELLS), 32, TEXT_COLOUR_WHITE);
      }
      cell = static_cast<std::uint16_t>(cell + 40u);
    }

    /*
     * A page at a time through `ZES1k`: the bitmap, as far as the dashboard.
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
      ZeroWholePage(_canvas, page, _picture);
    }

    // ZES2k on the partial page, and then by hand the one byte it walks past, because it
    // stops at zero rather than through it.
    ZeroPageDown(_canvas, page, static_cast<std::uint8_t>((DASHBOARD_BITMAP & 0xFFu) - 1u), _picture);
    _canvas.Write(page, 0u);
    if (DrawingTwins(_picture))
    {
      WriteBitmapByte2x(*_picture, page, 0u, false);
    }

    _text.column = 1u; // 1 into XC
    _text.row = 1u;    // And into YC

    // The space view and view 13 jump to wantdials, a tail call, so neither
    // reaches anything below this.
    if (_view == 0u || _view == 13u)
    {
      ShowDashboard(_canvas, _draw, _screen, _bubble, _flight, _status, _fuel, _compass, _video, _map, _picture);
      return;
    }

    _screen.colourBank = 0x81u; // &81 into abraxas -- screen RAM at &6000
    _screen.bitmapMode = 0xC0u; // The text mode into caravanserai

    // The same page loop again, and X is still where the first left it, so this
    // clears the dashboard's part of the bitmap as well.
    for (; page < Canvas::SCREEN_CELLS; page = static_cast<std::uint16_t>(page + 256u))
    {
      ZeroWholePage(_canvas, page, _picture);
    }

    _compass.pattern = PixelPattern::Blank; // Zero into COMC
    _screen.dashboardShown = 0u;            // And into DFLAG
    _text.column = 1u;                      // Stepped to one, into XC
    _text.row = 1u;                         // And into YC

    DrawColourBands(_canvas, _picture);
    ForgetScannerBlips(_bubble);  // JSR zonkscanners
    HideAllSprites(_video, _map);

    // The top row's colour band, thirty-two cells of &70.
    for (int offset = 31; offset >= 0; --offset)
    {
      _canvas.Write(static_cast<std::uint16_t>(Canvas::SCREEN_CELLS + 4u + offset), 0x70u);
    }
    if (DrawingTwins(_picture))
    {
      SetCellRun2x(*_picture, 4, 32, CellPalette::Of(0x70u));
    }

    // Three views -- 2, 64 and 128 -- jump straight to BOX and stop at one band; everything
    // else gets the second one two rows down.
    if (_view != 2u && _view != 64u && _view != 128u)
    {
      for (int offset = 31; offset >= 0; --offset)
      {
        _canvas.Write(static_cast<std::uint16_t>(Canvas::SCREEN_CELLS + 0x54u + offset), 0x70u);
      }
      if (DrawingTwins(_picture))
      {
        SetCellRun2x(*_picture, 0x54, 32, CellPalette::Of(0x70u));
      }
    }

    DrawScreenRule(_canvas, 199u, _picture); // BOXS on row 199

    _canvas.Write(0x1F1Fu, 0xFFu); // The corner byte again
    if (DrawingTwins(_picture))
    {
      WriteBitmapByte2x(*_picture, 0x1F1Fu, 0xFFu, false);
    }

    // 25 rows, and the stray byte after it eats `BOX2`'s own count, so the border is the
    // whole screen's height rather than the space view's (§6.79).
    DrawBorder(_canvas, BORDER_ROWS_TEXT_SCREEN, _picture);
  }

  void SetUpScreen(Universe& _universe, Ports& _ports, std::uint8_t _view) noexcept
  {
    SetUpScreen(_universe, _ports, _view, LayoutForView(_view));
  }

  void SetUpScreen(Universe& _universe, Ports& _ports, std::uint8_t _view, TextLayout _layout) noexcept
  {
    _universe.view = _view; // Into QQ11, and then it falls into TTX66

    // Where the wide surface puts this screen's text. Not the game's, and written here so that it
    // cannot be a screen behind the view it belongs to (Resolution.md section 6.2).
    _universe.screenLayout = _layout;

    // Sentence case for the extended printer, which is the first thing a new screen
    // is put back to.
    _universe.sentences.lowerCaseBits = 32u;
    _universe.sentences.alwaysLower = 0u;

    _universe.heaps.ballHeapTop = 0u; // Into LSP -- the ball heap is forgotten

    /*
     * 128 into `QQ17` and into `DTW2`.
     *
     * BOTH OF THEM, AND ONLY ONE KEEPS IT. `QQ17` is put back to zero five bytes from the end, so
     * what a caller sees is ALL CAPS with `DTW2` still at 128 (§6.29). The intermediate 128 is not
     * dead: the view's name below is printed with it, which is why the port cannot collapse the two
     * stores the way `SetUpTextScreen` does -- that version is only correct because the half slice
     * 2e left out is the half that observes the value in between (§6.81).
     */
    _universe.text.caseFlags = 0x80u;
    _universe.sentences.sentenceStart = 0x80u;

    ClearSunHeap(_universe.heaps); // FLFLLS -- and the sun's heap with it

    _universe.status.viewLaser = 0u; // Zero into LAS2 -- stop any laser pulsing
    _universe.message.delay = 0u;    // And into DLY
    _universe.message.append = 0u;   // STA de

    _universe.text.column = 1u; // 1 into XC
    _universe.text.row = 1u;    // And into YC

    SetUpScreenPixels(_universe.canvas, _universe.draw, _universe.text, _universe.screen, _universe.bubble, _universe.flight,
                      _universe.status, _universe.commander.fuel, _universe.compass, _universe.video, _universe.memoryMap,
                      _universe.view, &_universe.picture); // Resolution.md §4, rule T3:
                                                           // the wipe has to reach the index plane too, or a
                                                           // screen change leaves the last one's lines behind.

    // A countdown still running goes to `ee3` rather than OLDBOX -- it outlives a screen
    // change and is reprinted, because the screen it was on has just been wiped.
    if (_universe.status.hyperspaceCountdown != 0u)
    {
      PrintCountdown(_ports.sink, _universe.text, _universe.status.hyperspaceCountdown);
    }

    _universe.text.row = 1u; // DOYC with 1

    // Any other view skips to tt66 -- the view's name belongs to the space view alone.
    if (_universe.view == 0u)
    {
      _universe.text.column = 11u;

      // The view ORed with &60 and printed -- views 0 to 3 become tokens 96 to 99.
      _ports.printer.Print(static_cast<std::uint8_t>(_universe.spaceView | 0x60u));
      PrintSpace(_ports.printer);
      _ports.printer.Print(175u); // Token 175 -- "VIEW"
    }

    // One into both cursor bytes, then stepped back to zero for `QQ17`.
    _universe.text.column = 1u;
    _universe.text.row = 1u;
    _universe.text.caseFlags = 0u;
  }

  void ChangeView(Universe& _universe, Ports& _ports, std::uint8_t _to) noexcept
  {
    // DOVDU19 with zero -- a bare return on this build, and `LOOK1` makes it the first
    // thing it does, before it has even looked at the view.

    // A non-zero `QQ11` goes to LQ -- a chart or a text screen takes the short path.
    if (_universe.view != 0u)
    {
      _universe.spaceView = _to;          // Into VIEW
      SetUpScreen(_universe, _ports, 0u); // TT66 with A zero, so it becomes the space view

      DrawLaserSights(_universe.canvas, _universe.commander, _universe.trumbles, _universe.spaceView, _universe.video,
                      _universe.memoryMap);

      // A whole new field, because there was no space view to keep.
      SeedStardustAndClearShips(_universe.canvas, _universe.dust, _universe.rng, _universe.heaps, _universe.bubble, _universe.work,
                                _universe.flight, _universe.view, false, &_universe.picture);
      return;
    }

    // Already looking that way, so LO2's bare return. The palette above has happened
    // anyway, and that is the whole of what this path does.
    if (_to == _universe.spaceView)
    {
      return;
    }

    _universe.spaceView = _to;          // Into VIEW
    SetUpScreen(_universe, _ports, 0u);

    // The dust is MIRRORED rather than replaced, which is why the stars look
    // familiar for a moment after a view change.
    FlipStardust(_universe.canvas, _universe.dust, &_universe.picture);

    // WPSHPS, and then it falls into SIGHT.
    ClearAllShips(_universe.canvas, _universe.heaps, _universe.bubble, _universe.work, _universe.flight, _universe.view,
                  &_universe.picture);

    DrawLaserSights(_universe.canvas, _universe.commander, _universe.trumbles, _universe.spaceView, _universe.video, _universe.memoryMap);
  }

  void Warp(Universe& _universe, Ports& _ports) noexcept
  {
    /*
     * The slot two past the junk count, ORed with the station count and with witchspace.
     *
     * The junk count doubles as an index: every slot up to `JUNK` holds junk, so `FRIN+2,X` is the
     * slot two beyond it -- and a non-zero type there means something worth staying for. `SSPR` is
     * `MANY+SST`, the station count (§6.58), and `MJ` is witchspace.
     */
    const std::size_t slot = static_cast<std::size_t>(_universe.bubble.junk) + 2u;
    const std::uint8_t occupied = (slot < _universe.bubble.slots.size()) ? _universe.bubble.slots[slot] : 0u;
    const std::uint8_t station = _universe.bubble.Count(ShipType::Station);

    if ((occupied | station | _universe.status.midJump) != 0u)
    {
      (void)PlaySoundEffect(_universe.sound, SoundEffect::Boop, false); // The boop, as a tail call into NOISE
      return;
    }

    /*
     * The planet's z sign tested, then its largest axis against 2.
     *
     * The load into Y is read for its FLAGS alone -- the move that follows throws that Y away and
     * puts the accumulator's zero there instead, which is slot 0. So the load is a sign test on the
     * planet's z and the move is `MAS2`'s argument, two instructions apart and unrelated.
     *
     * A negative z is a body BEHIND you, and you cannot warp into something behind you, so its
     * distance is not tested at all.
     */
    if ((_universe.bubble.blocks[0].z.sgn & 0x80u) == 0u)
    {
      if (LargestAxis(_universe.bubble, 0u) < 2u)
      {
        (void)PlaySoundEffect(_universe.sound, SoundEffect::Boop, false);
        return;
      }
    }

    // The same for the sun, through `m` rather than `MAS2` because there is no
    // accumulator worth keeping this time.
    if ((_universe.bubble.blocks[1].z.sgn & 0x80u) == 0u)
    {
      if (LargestAxis(_universe.bubble, 1u) < 2u)
      {
        (void)PlaySoundEffect(_universe.sound, SoundEffect::Boop, false);
        return;
      }
    }

    // &81 into all three scratch bytes. It is -1 in sign-magnitude with the low bit
    // set, so `ADD` subtracts the same fixed amount from each body's z: (A P) is the sign byte
    // over &81, and (S R) is &81 twice.
    constexpr SignMag16 WARP_STEP{0x81u, 0x81u};

    // The planet's z sign through `ADD` and back, and the same for the sun.
    _universe.bubble.blocks[0].z.sgn = AddSigned(SignMag16{0x81u, _universe.bubble.blocks[0].z.sgn}, WARP_STEP).high;
    _universe.bubble.blocks[1].z.sgn = AddSigned(SignMag16{0x81u, _universe.bubble.blocks[1].z.sgn}, WARP_STEP).high;

    /*
     * One into `QQ11` and `MCNT`, halved to zero for `EV`, then LOOK1 on the current view.
     *
     * `QQ11 = 1` IS A LIE TOLD TO `LOOK1`. The view is not changing -- X is `VIEW` itself -- so the
     * space-view path would find the view already correct and do nothing at all. Setting `QQ11` to
     * something non-zero first sends `LOOK1` down `LQ` instead, which clears the screen and seeds a
     * WHOLE NEW stardust field, and `TT66` puts `QQ11` back to zero on the way. One store, to make
     * a routine take the other branch.
     */
    _universe.view = 1u;
    _universe.flight.mainLoopCounter = 1u;
    _universe.explosions = 0u; // One shifted right is zero

    ChangeView(_universe, _ports, _universe.spaceView);
  }

} // namespace Elite

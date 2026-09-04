#include "pch.h"

#include "ViewChange.h"

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
  } while (y != 0u);                                              // 6502: BNE ZEL1k
}

void CopyPagesDown(Canvas& _canvas, const std::uint8_t* _from, std::uint16_t _to,
                   std::uint8_t _pages, std::uint8_t _first) noexcept
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
    } while (y != 0u);                       // 6502: BNE mvbllop

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

void ToggleVerticalEdge(Canvas& _canvas, std::uint16_t _cell, std::uint8_t _pattern,
                        std::uint8_t _rows) noexcept
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
  DrawColourBand(_canvas, 0u);         // 6502: LDX #LO(SCBASE) / LDY #HI(SCBASE) / JSR BLUEBANDS
  DrawColourBand(_canvas, 37u * 8u);   // 6502: SCBASE+37*8, and it FALLS INTO BLUEBANDS
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
  _effects.SetRasterMode(0x05u);   // 6502: LDA #%101 / JSR SETL1
  _effects.SetSpritesEnabled(0u);  // 6502: LDA #%00000000 / STA VIC+&15
  _effects.SetRasterMode(0x04u);   // 6502: LDA #%100, and it falls into SETL1
}

void ShowDashboard(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math,
                   GeometryWorkspace& _geometry, ScreenState& _screen, Bubble& _bubble,
                   const FlightState& _flight, const FlightStatus& _status, std::uint8_t _fuel,
                   Compass& _compass, SightEffects& _effects) noexcept
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
    CopyPagesDown(_canvas, DASHBOARD_IMAGE.data() + 8u * 256u,
                  static_cast<std::uint16_t>(DASHBOARD_BITMAP + 8u * 256u), 1u, 0xC0u);

    ForgetScannerBlips(_bubble); // 6502: JSR zonkscanners

    // 6502: JSR DIALS -- all seven dials and the compass, on a dashboard that has just arrived
    // as a picture with every bar empty.
    DrawDials(_canvas, _draw, _math, _geometry, _flight, _status, _fuel, _compass, _bubble);
  }

  DrawColourBands(_canvas);   // 6502: .nearlyxmas JSR BLUEBAND
  HideAllSprites(_effects);   // 6502: JSR NOSPRITES

  _screen.dashboardShown = 0xFFu; // 6502: LDA #&FF / STA DFLAG
}

} // namespace Elite

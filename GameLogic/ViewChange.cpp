#include "pch.h"

#include "ViewChange.h"

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

} // namespace Elite

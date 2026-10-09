#include "pch.h"

#include "Cga.h"
#include "CgaFont.h"
#include "Memory.h"
#include "Timing.h"

#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint32_t VIDEO = Machine::Cga::VIDEO_MEMORY_LINEAR;
constexpr Machine::Cycles LINE = Machine::CGA_CYCLES_PER_LINE;
constexpr Machine::Cycles FRAME = Machine::CGA_CYCLES_PER_FRAME;

// A CGA on its own clock, with the memory it renders from.
class Rig
{
public:
  Rig()
    : m_cga(m_clock),
      m_image(std::make_unique<Machine::FrameImage>())
  {
  }

  void SetClock(Machine::Cycles _cycles) noexcept
  {
    m_clock = _cycles;
  }

  void SetCrtc(Machine::CrtcRegister _index, std::uint8_t _value)
  {
    m_cga.Out8(Machine::Cga::CRTC_INDEX_PORT, static_cast<std::uint8_t>(_index));
    m_cga.Out8(Machine::Cga::CRTC_DATA_PORT, _value);
  }

  void SetMode(std::uint8_t _modeControl, std::uint8_t _colorSelect)
  {
    m_cga.Out8(Machine::Cga::MODE_CONTROL_PORT, _modeControl);
    m_cga.Out8(Machine::Cga::COLOR_SELECT_PORT, _colorSelect);
  }

  // What the BIOS programs for mode 4: sync at character row 0x70 of 2 scan lines.
  void SetGraphicsTiming()
  {
    SetCrtc(Machine::CrtcRegister::VerticalSyncPosition, 0x70);
    SetCrtc(Machine::CrtcRegister::MaximumScanLine, 1);
  }

  // What the BIOS programs for modes 0 and 1: sync at row 0x1C of 8 scan lines, cursor on lines 6-7.
  void SetTextTiming()
  {
    SetCrtc(Machine::CrtcRegister::VerticalSyncPosition, 0x1C);
    SetCrtc(Machine::CrtcRegister::MaximumScanLine, 7);
    SetCrtc(Machine::CrtcRegister::CursorStart, 0x06);
    SetCrtc(Machine::CrtcRegister::CursorEnd, 0x07);
  }

  [[nodiscard]] std::uint32_t StatusAt(Machine::Cycles _cycles)
  {
    m_clock = _cycles;
    return m_cga.In8(Machine::Cga::STATUS_PORT);
  }

  [[nodiscard]] Machine::Cga& Device() noexcept
  {
    return m_cga;
  }

  [[nodiscard]] Machine::Memory& Ram() noexcept
  {
    return m_memory;
  }

  const Machine::FrameImage& Render()
  {
    m_cga.Render(m_memory, *m_image);
    return *m_image;
  }

private:
  Machine::Cycles m_clock = 0;
  Machine::Memory m_memory;
  Machine::Cga m_cga;
  std::unique_ptr<Machine::FrameImage> m_image;
};

[[nodiscard]] std::uint32_t Dot(const Machine::FrameImage& _image, std::uint32_t _x, std::uint32_t _y)
{
  return _image.At(_x, _y);
}

// The 4 colours of the first 4 pixels of a 320x200 line, each two dots wide.
void AssertFirstPixels(const Machine::FrameImage& _image, std::uint32_t _line, std::uint32_t _p0, std::uint32_t _p1, std::uint32_t _p2,
                       std::uint32_t _p3)
{
  const std::uint32_t expected[] = {_p0, _p1, _p2, _p3};
  for (std::uint32_t pixel = 0; pixel < 4; ++pixel)
  {
    Assert::AreEqual(expected[pixel], Dot(_image, pixel * 2, _line));
    Assert::AreEqual(expected[pixel], Dot(_image, pixel * 2 + 1, _line));
  }
}

constexpr std::uint32_t STATUS_FLAGS = Machine::Cga::STATUS_DISPLAY_INACTIVE | Machine::Cga::STATUS_VERTICAL_SYNC;
constexpr std::uint32_t DISPLAYING = 0;
constexpr std::uint32_t BLANKING = Machine::Cga::STATUS_DISPLAY_INACTIVE;
constexpr std::uint32_t SYNC = Machine::Cga::STATUS_DISPLAY_INACTIVE | Machine::Cga::STATUS_VERTICAL_SYNC;

} // namespace

TEST_CLASS(CgaTests)
{
public:
  // A visible line is 213 cycles of display and 91 of horizontal blanking, which sets bit 0.
  TEST_METHOD(StatusBitZeroMarksHorizontalBlanking)
  {
    Rig rig;
    rig.SetGraphicsTiming();
    Assert::AreEqual(DISPLAYING, rig.StatusAt(0) & STATUS_FLAGS);
    Assert::AreEqual(DISPLAYING, rig.StatusAt(212) & STATUS_FLAGS, L"last displayed cycle of line 0");
    Assert::AreEqual(BLANKING, rig.StatusAt(213) & STATUS_FLAGS, L"first blanking cycle of line 0");
    Assert::AreEqual(BLANKING, rig.StatusAt(303) & STATUS_FLAGS);
    Assert::AreEqual(DISPLAYING, rig.StatusAt(LINE) & STATUS_FLAGS, L"line 1 starts displaying");
    Assert::AreEqual(DISPLAYING, rig.StatusAt(199 * LINE + 212) & STATUS_FLAGS, L"last visible line");
    Assert::AreEqual(BLANKING, rig.StatusAt(199 * LINE + 213) & STATUS_FLAGS);
  }

  // Lines 200-261 are blanked throughout. Sync is the 16 lines from R7 x (R9 + 1), which is line
  // 224 with the BIOS's values for both mode 4 and mode 1.
  TEST_METHOD(StatusBitThreeMarksVerticalSync)
  {
    Rig rig;
    rig.SetGraphicsTiming();
    Assert::AreEqual(BLANKING, rig.StatusAt(200 * LINE) & STATUS_FLAGS, L"line 200: blanked, no sync");
    Assert::AreEqual(BLANKING, rig.StatusAt(224 * LINE - 1) & STATUS_FLAGS, L"end of line 223");
    Assert::AreEqual(SYNC, rig.StatusAt(224 * LINE) & STATUS_FLAGS, L"line 224: sync begins");
    Assert::AreEqual(SYNC, rig.StatusAt(240 * LINE - 1) & STATUS_FLAGS, L"end of line 239");
    Assert::AreEqual(BLANKING, rig.StatusAt(240 * LINE) & STATUS_FLAGS, L"line 240: sync over");
    Assert::AreEqual(BLANKING, rig.StatusAt(FRAME - 1) & STATUS_FLAGS, L"end of line 261");
    Assert::AreEqual(DISPLAYING, rig.StatusAt(FRAME) & STATUS_FLAGS, L"the next frame's first line");
    Assert::AreEqual(SYNC, rig.StatusAt(5 * FRAME + 230 * LINE + 7) & STATUS_FLAGS, L"phase is the clock modulo the frame");

    rig.SetTextTiming();
    Assert::AreEqual(BLANKING, rig.StatusAt(224 * LINE - 1) & STATUS_FLAGS);
    Assert::AreEqual(SYNC, rig.StatusAt(224 * LINE) & STATUS_FLAGS, L"mode 1's values give line 224 too");
    Assert::AreEqual(BLANKING, rig.StatusAt(240 * LINE) & STATUS_FLAGS);

    // Sync follows the register: row 0x60 of 2 lines is line 192, inside the visible area.
    rig.SetCrtc(Machine::CrtcRegister::VerticalSyncPosition, 0x60);
    rig.SetCrtc(Machine::CrtcRegister::MaximumScanLine, 1);
    Assert::AreEqual(std::uint32_t{Machine::Cga::STATUS_VERTICAL_SYNC}, rig.StatusAt(192 * LINE) & STATUS_FLAGS);
    Assert::AreEqual(DISPLAYING, rig.StatusAt(191 * LINE) & STATUS_FLAGS);
  }

  TEST_METHOD(StatusReadsUndrivenBitsAsOnesAndNoLightPen)
  {
    Rig rig;
    rig.SetGraphicsTiming();
    Assert::AreEqual(0xF4u, rig.StatusAt(0));
    Assert::AreEqual(0xFDu, rig.StatusAt(224 * LINE));
  }

  // Mode 4 is 2 bits a pixel, leftmost in the top bits. Even lines come from B8000h and odd lines
  // from BA000h, 80 bytes a line, and each pixel is two of the 640 dots.
  TEST_METHOD(Mode4DecodesInterleavedBanks)
  {
    Rig rig;
    rig.SetMode(0x0A, 0x30);                // graphics, video on; bright palette 1, background black
    rig.Ram().Write8(VIDEO + 0x0000, 0x1B); // line 0: 0 1 2 3
    rig.Ram().Write8(VIDEO + 0x2000, 0xE4); // line 1: 3 2 1 0
    rig.Ram().Write8(VIDEO + 0x0050, 0xC0); // line 2: 3 0 0 0
    rig.Ram().Write8(VIDEO + 0x2050, 0x30); // line 3: 0 3 0 0
    rig.Ram().Write8(VIDEO + 0x1F3F, 0x03); // line 198, last pixel
    rig.Ram().Write8(VIDEO + 0x3F3F, 0xC0); // line 199, pixel 316

    const Machine::FrameImage& image = rig.Render();
    AssertFirstPixels(image, 0, 0, 11, 13, 15);
    AssertFirstPixels(image, 1, 15, 13, 11, 0);
    AssertFirstPixels(image, 2, 15, 0, 0, 0);
    AssertFirstPixels(image, 3, 0, 15, 0, 0);
    Assert::AreEqual(15u, Dot(image, 639, 198));
    Assert::AreEqual(0u, Dot(image, 637, 198));
    Assert::AreEqual(15u, Dot(image, 632, 199));
    Assert::AreEqual(15u, Dot(image, 633, 199));
    Assert::AreEqual(0u, Dot(image, 634, 199));
  }

  // Colour select: bits 0-3 are pixel 0 and the border, bit 4 brightens 1-3, bit 5 picks the
  // palette; mode control's black-and-white bit picks the third palette whatever bit 5 says.
  TEST_METHOD(Mode4PaletteIntensityAndBackground)
  {
    Rig rig;
    rig.Ram().Write8(VIDEO, 0x1B); // 0 1 2 3

    rig.SetMode(0x0A, 0x00);
    AssertFirstPixels(rig.Render(), 0, 0, 2, 4, 6); // green, red, brown
    rig.SetMode(0x0A, 0x10);
    AssertFirstPixels(rig.Render(), 0, 0, 10, 12, 14); // SetGraphicsMode's 0x10
    rig.SetMode(0x0A, 0x20);
    AssertFirstPixels(rig.Render(), 0, 0, 3, 5, 7); // cyan, magenta, light gray
    rig.SetMode(0x0A, 0x14);
    AssertFirstPixels(rig.Render(), 0, 4, 10, 12, 14); // UpdateFuelLeak's red background
    Assert::AreEqual(4u, std::uint32_t{rig.Render().borderColor});
    rig.SetMode(0x0A, 0x19);
    AssertFirstPixels(rig.Render(), 0, 9, 10, 12, 14); // the masking device's light blue
    rig.SetMode(0x0E, 0x00);
    AssertFirstPixels(rig.Render(), 0, 0, 3, 4, 7); // black and white: cyan, red, light gray
    rig.SetMode(0x0E, 0x30);
    AssertFirstPixels(rig.Render(), 0, 0, 11, 12, 15); // bit 5 ignored, bit 4 still brightens
  }

  TEST_METHOD(Mode6DecodesOneBitPixelsInTheForegroundColor)
  {
    Rig rig;
    rig.SetMode(0x1E, 0x0E); // 640x200, video on; foreground yellow
    rig.Ram().Write8(VIDEO + 0x0000, 0x81);
    rig.Ram().Write8(VIDEO + 0x2000, 0x40);
    const Machine::FrameImage& image = rig.Render();
    Assert::AreEqual(14u, Dot(image, 0, 0));
    Assert::AreEqual(0u, Dot(image, 1, 0));
    Assert::AreEqual(14u, Dot(image, 7, 0));
    Assert::AreEqual(14u, Dot(image, 1, 1));
    Assert::AreEqual(0u, std::uint32_t{image.borderColor}, L"the border is black in 640x200");
  }

  TEST_METHOD(VideoDisabledIsBlack)
  {
    Rig rig;
    rig.Ram().Write8(VIDEO, 0xFF);
    rig.SetMode(0x02, 0x24);
    const Machine::FrameImage& image = rig.Render();
    Assert::AreEqual(0u, Dot(image, 0, 0));
    Assert::AreEqual(0u, std::uint32_t{image.borderColor});
  }

  // A 40-column cell is 8 glyph dots, each two of the 640, coloured by the attribute's nibbles.
  TEST_METHOD(TextCellUsesTheAttributeAndTheFont)
  {
    Rig rig;
    rig.SetTextTiming();
    rig.SetCrtc(Machine::CrtcRegister::CursorAddressHigh, 0x0E); // off the page, as SetTextMode does
    rig.SetMode(0x08, 0x04);                                     // 40x25, video on, blink off; red border
    rig.Ram().Write8(VIDEO + 0, 'A');
    rig.Ram().Write8(VIDEO + 1, 0x1E);     // yellow on blue
    rig.Ram().Write8(VIDEO + 2 * 41, 'E'); // row 1, column 1
    rig.Ram().Write8(VIDEO + 2 * 41 + 1, 0x4F);

    const Machine::FrameImage& image = rig.Render();
    for (std::uint32_t line = 0; line < 8; ++line)
    {
      const std::uint8_t glyph = Machine::CgaFont::Row('A', line);
      for (std::uint32_t dot = 0; dot < 8; ++dot)
      {
        const std::uint32_t expected = (glyph & (0x80 >> dot)) != 0 ? 14u : 1u;
        Assert::AreEqual(expected, Dot(image, dot * 2, line));
        Assert::AreEqual(expected, Dot(image, dot * 2 + 1, line));
      }
      const std::uint8_t e = Machine::CgaFont::Row('E', line);
      Assert::AreEqual((e & 0x80) != 0 ? 15u : 4u, Dot(image, 16, 8 + line));
    }
    Assert::AreEqual(4u, std::uint32_t{image.borderColor}, L"the border is colour select's low nibble");

    // 80 columns: one dot a glyph dot, so cell 1 starts at dot 8.
    rig.Ram().Write8(VIDEO + 2, 0xDB); // full block
    rig.Ram().Write8(VIDEO + 3, 0x02);
    rig.SetMode(0x09, 0x00);
    const Machine::FrameImage& wide = rig.Render();
    Assert::AreEqual(2u, Dot(wide, 8, 0));
    Assert::AreEqual(2u, Dot(wide, 15, 7));
  }

  // With blink enabled, attribute bit 7 blinks the character (16 frames shown, 16 hidden) and the
  // background has 3 bits. SetTextMode turns blink off, and bit 7 then brightens the background.
  TEST_METHOD(BlinkEnableChangesTheMeaningOfAttributeBit7)
  {
    Rig rig;
    rig.SetTextTiming();
    rig.SetCrtc(Machine::CrtcRegister::CursorAddressHigh, 0x0E);
    rig.Ram().Write8(VIDEO + 0, 0xDB);
    rig.Ram().Write8(VIDEO + 1, 0x9E); // bit 7, background 1, foreground yellow

    rig.SetMode(0x28, 0x00); // blink enabled
    rig.SetClock(0);
    Assert::AreEqual(14u, Dot(rig.Render(), 0, 0), L"frame 0: shown");
    rig.SetClock(15 * FRAME + FRAME - 1);
    Assert::AreEqual(14u, Dot(rig.Render(), 0, 0), L"frame 15: shown");
    rig.SetClock(16 * FRAME);
    Assert::AreEqual(1u, Dot(rig.Render(), 0, 0), L"frame 16: hidden on background 1");
    rig.SetClock(32 * FRAME);
    Assert::AreEqual(14u, Dot(rig.Render(), 0, 0), L"frame 32: shown again");

    rig.Ram().Write8(VIDEO + 1, 0x1E); // the same without bit 7 never blinks
    rig.SetClock(16 * FRAME);
    Assert::AreEqual(14u, Dot(rig.Render(), 0, 0));

    rig.Ram().Write8(VIDEO + 0, ' ');
    rig.Ram().Write8(VIDEO + 1, 0x9E);
    rig.SetMode(0x08, 0x00); // blink disabled: background 9, light blue, at every frame
    rig.SetClock(0);
    Assert::AreEqual(9u, Dot(rig.Render(), 0, 0));
    rig.SetClock(16 * FRAME);
    Assert::AreEqual(9u, Dot(rig.Render(), 0, 0));
    rig.SetMode(0x28, 0x00);
    Assert::AreEqual(1u, Dot(rig.Render(), 0, 0), L"blink enabled: background 1");
  }

  // The cursor is the cell at R14:R15 on lines R10-R11, in the foreground colour, shown for 8
  // frames and hidden for 8.
  TEST_METHOD(CursorBlinksOnItsLines)
  {
    Rig rig;
    rig.SetTextTiming(); // lines 6-7
    rig.SetCrtc(Machine::CrtcRegister::CursorAddressHigh, 0x00);
    rig.SetCrtc(Machine::CrtcRegister::CursorAddressLow, 41); // row 1, column 1
    rig.SetMode(0x08, 0x00);
    rig.Ram().Write8(VIDEO + 2 * 41, ' ');
    rig.Ram().Write8(VIDEO + 2 * 41 + 1, 0x07);

    const Machine::FrameImage& image = rig.Render();
    Assert::AreEqual(0u, Dot(image, 16, 8 + 5), L"line 5 is not the cursor's");
    Assert::AreEqual(7u, Dot(image, 16, 8 + 6));
    Assert::AreEqual(7u, Dot(image, 31, 8 + 7));
    Assert::AreEqual(0u, Dot(image, 32, 8 + 7), L"the next cell");
    rig.SetClock(8 * FRAME);
    Assert::AreEqual(0u, Dot(rig.Render(), 16, 8 + 6), L"frame 8: hidden");
    rig.SetClock(16 * FRAME);
    Assert::AreEqual(7u, Dot(rig.Render(), 16, 8 + 6), L"frame 16: shown");

    // Start beyond end splits it, as the 6845 does: lines 0-1 and 6-7.
    rig.SetCrtc(Machine::CrtcRegister::CursorStart, 0x06);
    rig.SetCrtc(Machine::CrtcRegister::CursorEnd, 0x01);
    const Machine::FrameImage& split = rig.Render();
    Assert::AreEqual(7u, Dot(split, 16, 8 + 0));
    Assert::AreEqual(7u, Dot(split, 16, 8 + 1));
    Assert::AreEqual(0u, Dot(split, 16, 8 + 2));
    Assert::AreEqual(7u, Dot(split, 16, 8 + 7));
  }

  TEST_METHOD(CursorOffForms)
  {
    Rig rig;
    rig.SetTextTiming();
    rig.SetMode(0x08, 0x00);
    for (std::uint32_t cell = 0; cell < 1000; ++cell)
    {
      rig.Ram().Write8(VIDEO + cell * 2, ' ');
      rig.Ram().Write8(VIDEO + cell * 2 + 1, 0x07);
    }
    auto anyCursorDot = [&rig]()
    {
      const Machine::FrameImage& image = rig.Render();
      for (std::uint32_t y = 0; y < Machine::FrameImage::HEIGHT_PIXELS; ++y)
      {
        for (std::uint32_t x = 0; x < Machine::FrameImage::WIDTH_PIXELS; ++x)
        {
          if (image.At(x, y) != 0)
          {
            return true;
          }
        }
      }
      return false;
    };
    rig.SetCrtc(Machine::CrtcRegister::CursorAddressHigh, 0x00);
    rig.SetCrtc(Machine::CrtcRegister::CursorAddressLow, 0x00);
    Assert::IsTrue(anyCursorDot(), L"the BIOS's cursor at cell 0 shows");

    rig.SetCrtc(Machine::CrtcRegister::CursorStart, 0x26); // bits 5-6 = 01: not displayed
    Assert::IsFalse(anyCursorDot(), L"R10 bits 5-6 = 01");

    rig.SetCrtc(Machine::CrtcRegister::CursorStart, 0x06);
    rig.SetCrtc(Machine::CrtcRegister::CursorAddressHigh, 0x0E); // SetTextMode, 0x7D31
    Assert::IsFalse(anyCursorDot(), L"cursor address 0x0E00 is off the page");

    rig.SetCrtc(Machine::CrtcRegister::CursorAddressHigh, 0x00);
    rig.SetCrtc(Machine::CrtcRegister::CursorStart, 0x08);
    rig.SetCrtc(Machine::CrtcRegister::CursorEnd, 0x0F);
    Assert::IsFalse(anyCursorDot(), L"lines 8-15 miss an 8-line cell");
  }

  // The four even ports are the index register and the four odd ones the data register. Only
  // 14-17 read back, and each register keeps only the bits the 6845 has.
  TEST_METHOD(CrtcPortsAliasAndMask)
  {
    Rig rig;
    Machine::Cga& cga = rig.Device();
    cga.Out8(0x3D0, 14);
    cga.Out8(0x3D7, 0xFF);
    Assert::AreEqual(0x3Fu, std::uint32_t{cga.Crtc(Machine::CrtcRegister::CursorAddressHigh)});
    cga.Out8(0x3D6, 15);
    cga.Out8(0x3D3, 0x5A);
    cga.Out8(0x3D2, 14);
    Assert::AreEqual(0x3Fu, std::uint32_t{cga.In8(0x3D1)});
    cga.Out8(0x3D4, 15);
    Assert::AreEqual(0x5Au, std::uint32_t{cga.In8(0x3D5)});
    cga.Out8(0x3D4, 7);
    cga.Out8(0x3D5, 0x70);
    Assert::AreEqual(0x70u, std::uint32_t{cga.Crtc(Machine::CrtcRegister::VerticalSyncPosition)});
    Assert::AreEqual(0u, std::uint32_t{cga.In8(0x3D5)}, L"R7 is write-only");
    Assert::AreEqual(0xFFu, std::uint32_t{cga.In8(0x3D4)}, L"the index register is write-only");
    cga.Out8(Machine::Cga::MODE_CONTROL_PORT, 0x08);
    Assert::AreEqual(0x08u, std::uint32_t{cga.ModeControl()});
    Assert::AreEqual(0xFFu, std::uint32_t{cga.In8(Machine::Cga::MODE_CONTROL_PORT)}, L"0x3D8 is write-only");
  }
};

} // namespace MachineTests

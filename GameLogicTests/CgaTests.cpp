#include "pch.h"

#include "Cga.h"
#include "CgaFont.h"
#include "Timing.h"

#include <array>
#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr Elite::Cycles LINE = Elite::CGA_CYCLES_PER_LINE;
constexpr Elite::Cycles FRAME = Elite::CGA_CYCLES_PER_FRAME;

// A CGA on its own clock, with the video memory it renders from.
class CgaRig
{
public:
  CgaRig()
    : m_cga(m_clock),
      m_image(std::make_unique<Elite::FrameImage>())
  {
  }

  void SetClock(Elite::Cycles _cycles) noexcept
  {
    m_clock = _cycles;
  }

  void SetCrtc(Elite::CrtcRegister _index, std::uint8_t _value)
  {
    m_cga.WriteCrtc(static_cast<std::uint8_t>(_index), _value);
  }

  void SetMode(std::uint8_t _modeControl, std::uint8_t _colorSelect)
  {
    m_cga.SetModeControl(_modeControl);
    m_cga.SetColorSelect(_colorSelect);
  }

  // What the BIOS programs for mode 4: sync at character row 70h of 2 scan lines.
  void SetGraphicsTiming()
  {
    SetCrtc(Elite::CrtcRegister::VerticalSyncPosition, 0x70);
    SetCrtc(Elite::CrtcRegister::MaximumScanLine, 1);
  }

  // What the BIOS programs for modes 0 and 1: sync at row 1Ch of 8 scan lines, cursor on lines 6-7.
  void SetTextTiming()
  {
    SetCrtc(Elite::CrtcRegister::VerticalSyncPosition, 0x1C);
    SetCrtc(Elite::CrtcRegister::MaximumScanLine, 7);
    SetCrtc(Elite::CrtcRegister::CursorStart, 0x06);
    SetCrtc(Elite::CrtcRegister::CursorEnd, 0x07);
  }

  // The status as the card drives it at _cycles, without paced time's rule for the retrace.
  [[nodiscard]] std::uint32_t StatusAt(Elite::Cycles _cycles)
  {
    m_clock = _cycles;
    return m_cga.Status();
  }

  [[nodiscard]] Elite::Cga& Device() noexcept
  {
    return m_cga;
  }

  void Poke(std::uint32_t _offset, std::uint8_t _value) noexcept
  {
    m_video[_offset] = _value;
  }

  const Elite::FrameImage& Render()
  {
    m_cga.Render(m_video, *m_image);
    return *m_image;
  }

private:
  Elite::Cycles m_clock = 0;
  std::array<std::uint8_t, Elite::Cga::VIDEO_MEMORY_BYTES> m_video{};
  Elite::Cga m_cga;
  std::unique_ptr<Elite::FrameImage> m_image;
};

[[nodiscard]] std::uint32_t Dot(const Elite::FrameImage& _image, std::uint32_t _x, std::uint32_t _y)
{
  return _image.At(_x, _y);
}

// The 4 colours of the first 4 pixels of a 320x200 line, each two dots wide.
void AssertFirstPixels(const Elite::FrameImage& _image, std::uint32_t _line, std::uint32_t _p0, std::uint32_t _p1, std::uint32_t _p2,
                       std::uint32_t _p3)
{
  const std::uint32_t expected[] = {_p0, _p1, _p2, _p3};
  for (std::uint32_t pixel = 0; pixel < 4; ++pixel)
  {
    Assert::AreEqual(expected[pixel], Dot(_image, pixel * 2, _line));
    Assert::AreEqual(expected[pixel], Dot(_image, pixel * 2 + 1, _line));
  }
}

constexpr std::uint32_t STATUS_FLAGS = Elite::Cga::STATUS_DISPLAY_INACTIVE | Elite::Cga::STATUS_VERTICAL_SYNC;
constexpr std::uint32_t DISPLAYING = 0;
constexpr std::uint32_t BLANKING = Elite::Cga::STATUS_DISPLAY_INACTIVE;
constexpr std::uint32_t SYNC = Elite::Cga::STATUS_DISPLAY_INACTIVE | Elite::Cga::STATUS_VERTICAL_SYNC;

} // namespace

// The CGA (Cga, D33): its status, its registers and its picture, the emulated PC's card without its ports.
TEST_CLASS(CgaTests)
{
public:
  // A visible line is 213 cycles of display and 91 of horizontal blanking, which sets bit 0.
  TEST_METHOD(StatusBitZeroMarksHorizontalBlanking)
  {
    CgaRig rig;
    rig.SetGraphicsTiming();
    Assert::AreEqual(DISPLAYING, rig.StatusAt(0) & STATUS_FLAGS);
    Assert::AreEqual(DISPLAYING, rig.StatusAt(212) & STATUS_FLAGS, L"last displayed cycle of line 0");
    Assert::AreEqual(BLANKING, rig.StatusAt(213) & STATUS_FLAGS, L"first blanking cycle of line 0");
    Assert::AreEqual(BLANKING, rig.StatusAt(303) & STATUS_FLAGS);
    Assert::AreEqual(DISPLAYING, rig.StatusAt(LINE) & STATUS_FLAGS, L"line 1 starts displaying");
    Assert::AreEqual(DISPLAYING, rig.StatusAt(199 * LINE + 212) & STATUS_FLAGS, L"last visible line");
    Assert::AreEqual(BLANKING, rig.StatusAt(199 * LINE + 213) & STATUS_FLAGS);
  }

  // Lines 200-261 are blanked throughout. Sync is the 16 lines from R7 x (R9 + 1), which is line 224 with the BIOS's values
  // for both mode 4 and mode 1.
  TEST_METHOD(StatusBitThreeMarksVerticalSync)
  {
    CgaRig rig;
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

    // Sync follows the register: row 60h of 2 lines is line 192, inside the visible area.
    rig.SetCrtc(Elite::CrtcRegister::VerticalSyncPosition, 0x60);
    rig.SetCrtc(Elite::CrtcRegister::MaximumScanLine, 1);
    Assert::AreEqual(std::uint32_t{Elite::Cga::STATUS_VERTICAL_SYNC}, rig.StatusAt(192 * LINE) & STATUS_FLAGS);
    Assert::AreEqual(DISPLAYING, rig.StatusAt(191 * LINE) & STATUS_FLAGS);
  }

  // Paced time's rule (ADR-008 item 2): a read of the status reports each vertical retrace to the first read that sees it,
  // and to no later one, so the game's next wait for a retrace waits for the next.
  TEST_METHOD(EachRetraceIsReportedToTheFirstReadThatSeesIt)
  {
    CgaRig rig;
    rig.SetGraphicsTiming();
    rig.SetClock(224 * LINE + 10);
    Assert::AreEqual(SYNC, std::uint32_t{rig.Device().ReadStatus()} & STATUS_FLAGS, L"the first read sees the retrace");
    rig.SetClock(230 * LINE);
    Assert::AreEqual(BLANKING, std::uint32_t{rig.Device().ReadStatus()} & STATUS_FLAGS, L"a later read in it does not");
    Assert::AreEqual(SYNC, std::uint32_t{rig.Device().Status()} & STATUS_FLAGS, L"though the card still drives it");
    rig.SetClock(FRAME + 239 * LINE);
    Assert::AreEqual(SYNC, std::uint32_t{rig.Device().ReadStatus()} & STATUS_FLAGS, L"the next frame's retrace is reported");
  }

  // Mode 4 is 2 bits a pixel, leftmost in the top bits. Even lines come from B800:0000 and odd lines from B800:2000, 80 bytes
  // a line, and each pixel is two of the 640 dots.
  TEST_METHOD(Mode4DecodesInterleavedBanks)
  {
    CgaRig rig;
    rig.SetMode(0x0A, 0x30); // graphics, video on; bright palette 1, background black
    rig.Poke(0x0000, 0x1B);  // line 0: 0 1 2 3
    rig.Poke(0x2000, 0xE4);  // line 1: 3 2 1 0
    rig.Poke(0x0050, 0xC0);  // line 2: 3 0 0 0
    rig.Poke(0x2050, 0x30);  // line 3: 0 3 0 0
    rig.Poke(0x1F3F, 0x03);  // line 198, last pixel
    rig.Poke(0x3F3F, 0xC0);  // line 199, pixel 316

    const Elite::FrameImage& image = rig.Render();
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

  // Colour select: bits 0-3 are pixel 0 and the border, bit 4 brightens 1-3, bit 5 picks the palette; mode control's
  // black-and-white bit picks the third palette whatever bit 5 says.
  TEST_METHOD(Mode4PaletteIntensityAndBackground)
  {
    CgaRig rig;
    rig.Poke(0, 0x1B); // 0 1 2 3

    rig.SetMode(0x0A, 0x00);
    AssertFirstPixels(rig.Render(), 0, 0, 2, 4, 6); // green, red, brown
    rig.SetMode(0x0A, 0x10);
    AssertFirstPixels(rig.Render(), 0, 0, 10, 12, 14); // SetGraphicsMode's 10h
    rig.SetMode(0x0A, 0x20);
    AssertFirstPixels(rig.Render(), 0, 0, 3, 5, 7); // cyan, magenta, light gray
    rig.SetMode(0x0A, 0x14);
    AssertFirstPixels(rig.Render(), 0, 4, 10, 12, 14); // UpdateFuelLeak's red background
    Assert::AreEqual(4u, std::uint32_t{rig.Render().borderColor});
    rig.SetMode(0x0A, 0x19);
    AssertFirstPixels(rig.Render(), 0, 9, 10, 12, 14); // the masking device's light blue
  }

  // A 40-column cell is 8 glyph dots, each two of the 640, coloured by the attribute's nibbles.
  TEST_METHOD(TextCellUsesTheAttributeAndTheFont)
  {
    CgaRig rig;
    rig.SetTextTiming();
    rig.SetCrtc(Elite::CrtcRegister::CursorAddressHigh, 0x0E); // off the page, as SetTextMode does
    rig.SetMode(0x08, 0x04);                                   // 40x25, video on, blink off; red border
    rig.Poke(0, 'A');
    rig.Poke(1, 0x1E);     // yellow on blue
    rig.Poke(2 * 41, 'E'); // row 1, column 1
    rig.Poke(2 * 41 + 1, 0x4F);

    const Elite::FrameImage& image = rig.Render();
    for (std::uint32_t line = 0; line < 8; ++line)
    {
      const std::uint8_t glyph = Elite::CgaFont::Row('A', line);
      for (std::uint32_t dot = 0; dot < 8; ++dot)
      {
        const std::uint32_t expected = (glyph & (0x80 >> dot)) != 0 ? 14u : 1u;
        Assert::AreEqual(expected, Dot(image, dot * 2, line));
        Assert::AreEqual(expected, Dot(image, dot * 2 + 1, line));
      }
      const std::uint8_t e = Elite::CgaFont::Row('E', line);
      Assert::AreEqual((e & 0x80) != 0 ? 15u : 4u, Dot(image, 16, 8 + line));
    }
    Assert::AreEqual(4u, std::uint32_t{image.borderColor}, L"the border is colour select's low nibble");

    // 80 columns: one dot a glyph dot, so cell 1 starts at dot 8.
    rig.Poke(2, 0xDB); // full block
    rig.Poke(3, 0x02);
    rig.SetMode(0x09, 0x00);
    const Elite::FrameImage& wide = rig.Render();
    Assert::AreEqual(2u, Dot(wide, 8, 0));
    Assert::AreEqual(2u, Dot(wide, 15, 7));
  }

  // With blink enabled, attribute bit 7 blinks the character (16 frames shown, 16 hidden) and the background has 3 bits.
  // SetTextMode turns blink off, and bit 7 then brightens the background.
  TEST_METHOD(BlinkEnableChangesTheMeaningOfAttributeBit7)
  {
    CgaRig rig;
    rig.SetTextTiming();
    rig.SetCrtc(Elite::CrtcRegister::CursorAddressHigh, 0x0E);
    rig.Poke(0, 0xDB);
    rig.Poke(1, 0x9E); // bit 7, background 1, foreground yellow

    rig.SetMode(0x28, 0x00); // blink enabled
    rig.SetClock(0);
    Assert::AreEqual(14u, Dot(rig.Render(), 0, 0), L"frame 0: shown");
    rig.SetClock(15 * FRAME + FRAME - 1);
    Assert::AreEqual(14u, Dot(rig.Render(), 0, 0), L"frame 15: shown");
    rig.SetClock(16 * FRAME);
    Assert::AreEqual(1u, Dot(rig.Render(), 0, 0), L"frame 16: hidden on background 1");
    rig.SetClock(32 * FRAME);
    Assert::AreEqual(14u, Dot(rig.Render(), 0, 0), L"frame 32: shown again");

    rig.Poke(1, 0x1E); // the same without bit 7 never blinks
    rig.SetClock(16 * FRAME);
    Assert::AreEqual(14u, Dot(rig.Render(), 0, 0));

    rig.Poke(0, ' ');
    rig.Poke(1, 0x9E);
    rig.SetMode(0x08, 0x00); // blink disabled: background 9, light blue, at every frame
    rig.SetClock(0);
    Assert::AreEqual(9u, Dot(rig.Render(), 0, 0));
    rig.SetClock(16 * FRAME);
    Assert::AreEqual(9u, Dot(rig.Render(), 0, 0));
    rig.SetMode(0x28, 0x00);
    Assert::AreEqual(1u, Dot(rig.Render(), 0, 0), L"blink enabled: background 1");
  }

  // The cursor is the cell at R14:R15 on lines R10-R11, in the foreground colour, shown for 8 frames and hidden for 8.
  TEST_METHOD(CursorBlinksOnItsLines)
  {
    CgaRig rig;
    rig.SetTextTiming(); // lines 6-7
    rig.SetCrtc(Elite::CrtcRegister::CursorAddressHigh, 0x00);
    rig.SetCrtc(Elite::CrtcRegister::CursorAddressLow, 41); // row 1, column 1
    rig.SetMode(0x08, 0x00);
    rig.Poke(2 * 41, ' ');
    rig.Poke(2 * 41 + 1, 0x07);

    const Elite::FrameImage& image = rig.Render();
    Assert::AreEqual(0u, Dot(image, 16, 8 + 5), L"line 5 is not the cursor's");
    Assert::AreEqual(7u, Dot(image, 16, 8 + 6));
    Assert::AreEqual(7u, Dot(image, 31, 8 + 7));
    Assert::AreEqual(0u, Dot(image, 32, 8 + 7), L"the next cell");
    rig.SetClock(8 * FRAME);
    Assert::AreEqual(0u, Dot(rig.Render(), 16, 8 + 6), L"frame 8: hidden");
    rig.SetClock(16 * FRAME);
    Assert::AreEqual(7u, Dot(rig.Render(), 16, 8 + 6), L"frame 16: shown");

    // Start beyond end splits it, as the 6845 does: lines 0-1 and 6-7.
    rig.SetCrtc(Elite::CrtcRegister::CursorStart, 0x06);
    rig.SetCrtc(Elite::CrtcRegister::CursorEnd, 0x01);
    const Elite::FrameImage& split = rig.Render();
    Assert::AreEqual(7u, Dot(split, 16, 8 + 0));
    Assert::AreEqual(7u, Dot(split, 16, 8 + 1));
    Assert::AreEqual(0u, Dot(split, 16, 8 + 2));
    Assert::AreEqual(7u, Dot(split, 16, 8 + 7));
  }

  TEST_METHOD(CursorOffForms)
  {
    CgaRig rig;
    rig.SetTextTiming();
    rig.SetMode(0x08, 0x00);
    for (std::uint32_t cell = 0; cell < 1000; ++cell)
    {
      rig.Poke(cell * 2, ' ');
      rig.Poke(cell * 2 + 1, 0x07);
    }
    auto anyCursorDot = [&rig]()
    {
      const Elite::FrameImage& image = rig.Render();
      for (std::uint32_t y = 0; y < Elite::FrameImage::HEIGHT_PIXELS; ++y)
      {
        for (std::uint32_t x = 0; x < Elite::FrameImage::WIDTH_PIXELS; ++x)
        {
          if (image.At(x, y) != 0)
          {
            return true;
          }
        }
      }
      return false;
    };
    rig.SetCrtc(Elite::CrtcRegister::CursorAddressHigh, 0x00);
    rig.SetCrtc(Elite::CrtcRegister::CursorAddressLow, 0x00);
    Assert::IsTrue(anyCursorDot(), L"the BIOS's cursor at cell 0 shows");

    rig.SetCrtc(Elite::CrtcRegister::CursorStart, 0x26); // bits 5-6 = 01: not displayed
    Assert::IsFalse(anyCursorDot(), L"R10 bits 5-6 = 01");

    rig.SetCrtc(Elite::CrtcRegister::CursorStart, 0x06);
    rig.SetCrtc(Elite::CrtcRegister::CursorAddressHigh, 0x0E); // SetTextMode, 0x7D11
    Assert::IsFalse(anyCursorDot(), L"cursor address 0E00h is off the page");

    rig.SetCrtc(Elite::CrtcRegister::CursorAddressHigh, 0x00);
    rig.SetCrtc(Elite::CrtcRegister::CursorStart, 0x08);
    rig.SetCrtc(Elite::CrtcRegister::CursorEnd, 0x0F);
    Assert::IsFalse(anyCursorDot(), L"lines 8-15 miss an 8-line cell");
  }

  // A register keeps only the bits the 6845 has, its index taken modulo 32; the light-pen registers take no writes; mode
  // control and colour select keep their low six bits. These are the 20 bytes the game-state digest hashes of the card.
  TEST_METHOD(RegistersKeepTheBitsTheCardHas)
  {
    CgaRig rig;
    Elite::Cga& cga = rig.Device();
    cga.WriteCrtc(14, 0xFF);
    Assert::AreEqual(0x3Fu, std::uint32_t{cga.Crtc(Elite::CrtcRegister::CursorAddressHigh)});
    cga.WriteCrtc(15, 0x5A);
    Assert::AreEqual(0x5Au, std::uint32_t{cga.Crtc(Elite::CrtcRegister::CursorAddressLow)});
    cga.WriteCrtc(32 + 7, 0x70);
    Assert::AreEqual(0x70u, std::uint32_t{cga.Crtc(Elite::CrtcRegister::VerticalSyncPosition)}, L"index 39 is register 7");
    cga.WriteCrtc(16, 0x12);
    cga.WriteCrtc(17, 0x34);
    Assert::AreEqual(0u, std::uint32_t{cga.Crtc(Elite::CrtcRegister::LightPenHigh)}, L"the light pen's registers are read-only");
    Assert::AreEqual(0u, std::uint32_t{cga.Crtc(Elite::CrtcRegister::LightPenLow)});
    cga.SetModeControl(0xFF);
    Assert::AreEqual(0x3Fu, std::uint32_t{cga.ModeControl()});
    cga.SetColorSelect(0xFF);
    Assert::AreEqual(0x3Fu, std::uint32_t{cga.ColorSelect()});
  }
};

} // namespace GameLogicTests

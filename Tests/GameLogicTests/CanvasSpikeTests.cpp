#include "pch.h"

#include "OracleImage.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * A measurement, not a port. Read this before slice 1d writes a line of Canvas.cpp.
 *
 * ADR-002 section 4 fixes the canvas as "320x200 logical pixels, one byte per pixel holding a
 * C64 colour index". That was asserted rather than derived, and the drawing code does not
 * support it. The C64 screen is a MULTICOLOUR bitmap: 160 double-width pixels across 200 rows,
 * two bits each, with the colours for %01 and %10 coming from a per-8x8-cell byte in screen RAM
 * and %11 from colour RAM. Every drawing routine EORs whole BYTES into that bitmap.
 *
 * The question this file exists to settle is whether an index-per-pixel canvas can reproduce
 * those writes. The tests ask the shipped game rather than reasoning about it: each one snapshots
 * the bitmap, calls a routine, and reports what actually changed -- it does not model the
 * routine's address arithmetic, because a spike that assumes the answer measures nothing. Each
 * logs what it saw, so the finding can be read off a test run and written into an ADR-002
 * amendment, and each asserts what must hold, so this does not decay into a log nobody notices
 * going stale.
 *
 * DELETE THIS FILE when slice 1d closes. Its findings belong in ADR-002 and in Canvas.h's
 * commentary; a spike that outlives its slice becomes furniture nobody dares move.
 */
namespace GameLogicTests
{

  namespace
  {
    /// Logs why the oracle is unavailable and says whether the caller should stop.
    bool OracleMissing()
    {
      const OracleImage& oracle = OracleImage::Instance();
      if (oracle.Available())
      {
        return false;
      }
      Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
      return true;
    }

    /// 6502: SCBASE -- the bitmap base, and 0x2000 bytes long. It is an assembler constant rather
    /// than a label, so it is not in the label map; ylookup's first entry is SCBASE plus the space
    /// view's left margin, and deriving it from there measures the margin at the same time.
    constexpr std::uint16_t SPACE_VIEW_MARGIN = 0x20;
    constexpr std::uint16_t BITMAP_SIZE = 0x2000;
    constexpr std::uint16_t SCREEN_RAM_OFFSET = 0x2000;

    /// A multicolour byte holds four two-bit pixels. A mask is ALIGNED when every bit it sets falls
    /// inside one pixel's pair, and STRADDLES when it sets one bit of one pixel and one of the next.
    /// A straddling mask cannot be expressed as "plot colour C at pixel P", which is the whole
    /// question this file asks.
    [[nodiscard]] bool IsMulticolourAligned(std::uint8_t _mask) noexcept
    {
      return ((_mask >> 1) & 0x55) == (_mask & 0x55);
    }

    /// The two-bit colour code at pixel _index (0 is the leftmost) of a multicolour byte.
    [[nodiscard]] std::uint8_t PixelCode(std::uint8_t _byte, int _index) noexcept
    {
      return static_cast<std::uint8_t>((_byte >> (6 - 2 * _index)) & 0x03);
    }

    [[nodiscard]] std::string Binary(std::uint8_t _byte)
    {
      std::string text = "%";
      for (int bit = 7; bit >= 0; --bit)
      {
        text += ((_byte >> bit) & 1) ? '1' : '0';
      }
      return text;
    }

    [[nodiscard]] std::string Hex(std::uint32_t _value, int _digits)
    {
      static constexpr char DIGITS[] = "0123456789ABCDEF";
      std::string text = "$";
      for (int shift = (_digits - 1) * 4; shift >= 0; shift -= 4)
      {
        text += DIGITS[(_value >> shift) & 0x0F];
      }
      return text;
    }

    /// One byte the routine under test changed, found by comparison rather than by predicting where
    /// the routine would write.
    struct Change
    {
      std::uint16_t address = 0;
      std::uint8_t before = 0;
      std::uint8_t after = 0;

      [[nodiscard]] std::uint8_t Mask() const noexcept
      {
        return static_cast<std::uint8_t>(before ^ after);
      }
    };

    /// The state of the bitmap, so that a call's effect is whatever differs afterwards.
    class BitmapSnapshot
    {
    public:
      BitmapSnapshot(const Cpu6502& _cpu, std::uint16_t _base)
        : m_base(_base)
      {
        for (std::uint16_t offset = 0; offset < BITMAP_SIZE; ++offset)
        {
          m_bytes[offset] = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
        }
      }

      [[nodiscard]] std::vector<Change> ChangesIn(const Cpu6502& _cpu) const
      {
        std::vector<Change> changes;
        for (std::uint16_t offset = 0; offset < BITMAP_SIZE; ++offset)
        {
          const std::uint16_t address = static_cast<std::uint16_t>(m_base + offset);
          if (_cpu.memory[address] != m_bytes[offset])
          {
            changes.push_back({address, m_bytes[offset], _cpu.memory[address]});
          }
        }
        return changes;
      }

    private:
      std::uint16_t m_base = 0;
      std::array<std::uint8_t, BITMAP_SIZE> m_bytes{};
    };

    /// The address ylookup gives for a screen row, which is where the space view's x = 0 sits.
    [[nodiscard]] std::uint16_t RowAddress(const Cpu6502& _cpu, const OracleImage& _oracle, std::uint8_t _y)
    {
      const std::uint16_t low = _oracle.Label("ylookupl");
      const std::uint16_t high = _oracle.Label("ylookuph");
      return static_cast<std::uint16_t>(_cpu.memory[low + _y] | (_cpu.memory[high + _y] << 8));
    }

    [[nodiscard]] std::uint16_t ScreenBase(const Cpu6502& _cpu, const OracleImage& _oracle)
    {
      return static_cast<std::uint16_t>(RowAddress(_cpu, _oracle, 0) - SPACE_VIEW_MARGIN);
    }

    /*
     * 6502: PIXEL -- plot at (X, A) with the size taken from ZZ.
     *
     * ZZ defaults to 255, which is past the routine's 144 threshold and so draws the smallest mark
     * it has: one EOR of a mask into one byte. That is the primitive every other size is built from
     * and the one worth measuring.
     */
    std::vector<Change> CallPixel(Cpu6502& _cpu, const OracleImage& _oracle, std::uint8_t _x, std::uint8_t _y, std::uint8_t _distance = 255)
    {
      const BitmapSnapshot before(_cpu, ScreenBase(_cpu, _oracle));

      _cpu.memory[_oracle.Label("ZZ")] = _distance;
      _cpu.a = _y;
      _cpu.x = _x;
      _cpu.y = 0;
      _cpu.sp = 0xFD;

      const auto run = _cpu.CallSubroutine(_oracle.Label("PIXEL"), 5'000);
      Assert::IsTrue(run.completed, L"PIXEL should return");

      return before.ChangesIn(_cpu);
    }
  } // namespace

  TEST_CLASS(CanvasRepresentationSpike)
  {
  public:
    /*
     * ADR-005 section 1 says the space view's horizontal placement and the dashboard row split
     * are "read off the reference screenshots in slice 0b". Slice 0b-b was cancelled and the
     * plan's section 6.5 accounts for only two of its dependents; this is the third, and it never
     * needed a screenshot. Both numbers are in the game's own address table.
     */
    TEST_METHOD(SpaceViewPlacementIsInTheAddressTableNotInAScreenshot)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Cpu6502 cpu = oracle.Fresh();

      const std::uint16_t row0 = RowAddress(cpu, oracle, 0);
      const std::uint16_t scbase = ScreenBase(cpu, oracle);

      // Eight consecutive y values share a character row; the ninth starts the next one, 320
      // bytes on (40 cells of 8 bytes).
      Assert::AreEqual<std::uint32_t>(row0, RowAddress(cpu, oracle, 7), L"y 0..7 are one character row");
      Assert::AreEqual<std::uint32_t>(row0 + 320u, RowAddress(cpu, oracle, 8), L"a character row is 320 bytes");

      // The dashboard begins at character row 18, so the space view owns rows 0..143 -- which is
      // the 144 in the masters' "256 x 144 space view" comment on Y = 72.
      Assert::AreEqual<std::uint32_t>(row0 + 17u * 320u, RowAddress(cpu, oracle, 143), L"y 143 is the last space view row");
      Assert::AreEqual<std::uint32_t>(row0 + 18u * 320u, RowAddress(cpu, oracle, 144), L"y 144 starts the dashboard");

      // The margin is the answer ADR-005 wanted. 0x20 bytes is 4 character cells, so the space
      // view's x = 0 begins at cell 4 of 40 and x = 255 ends at cell 35: two x-units per
      // multicolour pixel means 256 x-units span exactly 32 cells.
      std::string report = "SPACE VIEW PLACEMENT (measured from ylookup, no screenshot)\n";
      report += "  SCBASE               " + Hex(scbase, 4) + "\n";
      report += "  left margin          " + Hex(SPACE_VIEW_MARGIN, 2) + " bytes = 4 character cells\n";
      report += "  x range 0..255       cells 4..35 of 40, i.e. 128 multicolour pixels of 160\n";
      report += "  space view rows      y 0..143 (18 character rows); dashboard from y 144\n";
      Logger::WriteMessage(report.c_str());

      Assert::AreEqual<std::uint32_t>(SPACE_VIEW_MARGIN, static_cast<std::uint32_t>(row0 - scbase), L"the left margin is 4 cells");
    }

    /*
     * The erase-by-redraw the plan's section 4.6 relies on, confirmed rather than assumed: two
     * identical calls leave the bitmap exactly as it was. Nothing in the canvas design may break
     * this, because LL9 and SUN use it to decide what to erase.
     */
    TEST_METHOD(PixelDrawsByExclusiveOrSoDrawingTwiceErases)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      Cpu6502 cpu = oracle.Fresh();

      const BitmapSnapshot original(cpu, ScreenBase(cpu, oracle));

      const std::vector<Change> first = CallPixel(cpu, oracle, 100, 60);
      Assert::IsFalse(first.empty(), L"the first call should change the bitmap");

      const std::vector<Change> second = CallPixel(cpu, oracle, 100, 60);
      Assert::IsFalse(second.empty(), L"the second call should touch the same bytes");

      Assert::IsTrue(original.ChangesIn(cpu).empty(), L"drawing the same point twice should leave the bitmap untouched");
    }

    /*
     * THE FINDING THIS FILE EXISTS FOR.
     *
     * The C64 build of PIXEL indexes TWOS2, not the multicolour-aligned CTWOS2 that CPIX2 uses.
     * TWOS2 slides its two set bits along by one bit per x, so at some x offsets the mask sets the
     * LOW bit of one multicolour pixel and the HIGH bit of the next.
     *
     * A canvas holding one resolved colour index per pixel cannot express that write. There is no
     * "colour" to store: the operation is an exclusive-or on a byte whose bits belong to two
     * adjacent pixels. Either the canvas holds the bitmap as bytes (or as two-bit patterns) with a
     * separate per-cell colour plane, or PIXEL is unportable.
     */
    TEST_METHOD(PixelMasksStraddleMulticolourPixelBoundaries)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();

      std::string report = "PIXEL MASK PER x MOD 8 (ZZ = 255, the single-mark case)\n";
      int straddling = 0;

      for (std::uint8_t offset = 0; offset < 8; ++offset)
      {
        Cpu6502 cpu = oracle.Fresh();
        const std::vector<Change> changes = CallPixel(cpu, oracle, static_cast<std::uint8_t>(64 + offset), 60);

        Assert::AreEqual<std::size_t>(1, changes.size(), L"the smallest mark should touch exactly one byte");

        const std::uint8_t mask = changes.front().Mask();
        const bool aligned = IsMulticolourAligned(mask);
        straddling += aligned ? 0 : 1;

        report += "  x mod 8 = " + std::to_string(offset) + "   " + Hex(changes.front().address, 4) + "   mask " + Binary(mask) + "   " +
                  (aligned ? "inside one multicolour pixel" : "STRADDLES two multicolour pixels") + "\n";
      }

      report += "  " + std::to_string(straddling) + " of 8 offsets straddle a pixel boundary.\n";
      report += "  => a canvas of one resolved colour index per pixel cannot represent this write.\n";
      Logger::WriteMessage(report.c_str());

      Assert::IsTrue(straddling > 0, L"PIXEL's masks are expected to straddle multicolour pixel boundaries. If this ever fails, the C64 "
                                     L"build has stopped indexing TWOS2 and the canvas decision should be revisited");
    }

    /*
     * The same point from the other side, and the one to show anybody who wants the simpler
     * canvas: two marks that share no bit still combine INSIDE a pixel and produce a colour
     * neither of them drew.
     *
     * At x = 66 the mask covers bits 6 and 5; at x = 68 it covers bits 4 and 3. Bits 5 and 4 are
     * both multicolour pixel 1, so that pixel ends up %11 -- green -- although one call asked for
     * a mark to its left and the other for a mark to its right.
     */
    TEST_METHOD(OverlappingMarksCombineAsPatternBitsNotAsColourIndices)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      Cpu6502 cpu = oracle.Fresh();

      const std::vector<Change> left = CallPixel(cpu, oracle, 66, 60);
      const std::vector<Change> right = CallPixel(cpu, oracle, 68, 60);
      Assert::AreEqual<std::size_t>(1, left.size(), L"the left mark should touch one byte");
      Assert::AreEqual<std::size_t>(1, right.size(), L"the right mark should touch one byte");
      Assert::AreEqual<std::uint32_t>(left.front().address, right.front().address, L"x 66 and 68 should share a byte");

      const std::uint8_t combined = right.front().after;

      std::string report = "TWO MARKS IN ONE BYTE at " + Hex(left.front().address, 4) + "\n";
      report += "  x = 66 alone        " + Binary(left.front().Mask()) + "\n";
      report += "  x = 68 alone        " + Binary(right.front().Mask()) + "\n";
      report += "  both                " + Binary(combined) + "\n";
      for (int pixel = 0; pixel < 4; ++pixel)
      {
        const std::uint8_t code = PixelCode(combined, pixel);
        report += "    multicolour pixel " + std::to_string(pixel) + " = %" + std::to_string(code >> 1) + std::to_string(code & 1) + "\n";
      }
      report += "  => a colour appears between the two marks that neither call plotted.\n";
      Logger::WriteMessage(report.c_str());

      Assert::AreEqual<std::uint32_t>(3u, static_cast<std::uint32_t>(PixelCode(combined, 1)),
                                      L"the pixel between the two marks should carry a colour neither call drew");
    }

    /*
     * 6502: HLOIN -- the horizontal line, where byte granularity is most obvious: the middle of a
     * run is written as whole bytes and only the two ends carry a mask. Reported rather than
     * predicted; this file measures, it does not re-derive TWFR and TWFL.
     */
    TEST_METHOD(HorizontalLinesAreWrittenAsWholeBytes)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      Cpu6502 cpu = oracle.Fresh();

      constexpr std::uint8_t X_FROM = 10;
      constexpr std::uint8_t X_TO = 30;
      constexpr std::uint8_t ROW = 60;

      const BitmapSnapshot before(cpu, ScreenBase(cpu, oracle));

      cpu.memory[oracle.Label("X1")] = X_FROM;
      cpu.memory[oracle.Label("X2")] = X_TO;
      cpu.memory[oracle.Label("Y1")] = ROW;
      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;

      const auto run = cpu.CallSubroutine(oracle.Label("HLOIN"), 50'000);
      Assert::IsTrue(run.completed, L"HLOIN should return");

      const std::vector<Change> changes = before.ChangesIn(cpu);
      Assert::IsFalse(changes.empty(), L"HLOIN should have drawn something");

      std::string report = "HLOIN x " + std::to_string(X_FROM) + ".." + std::to_string(X_TO) + " on row " + std::to_string(ROW) +
                           " touched " + std::to_string(changes.size()) + " bytes\n";
      int fullBytes = 0;
      for (const Change& change : changes)
      {
        if (change.Mask() == 0xFF)
        {
          ++fullBytes;
        }
        report += "  " + Hex(change.address, 4) + "   " + Binary(change.Mask()) +
                  (change.Mask() == 0xFF ? "   whole byte" : "   masked end") + "\n";
      }
      report += "  => the interior is whole bytes; only the ends are masked. Byte granularity, not pixel.\n";
      Logger::WriteMessage(report.c_str());

      Assert::IsTrue(fullBytes > 0, L"the interior of a horizontal line should be written as whole bytes");
    }

    /*
     * The colour plane the ADR-002 canvas has nowhere to put.
     *
     * Screen RAM sits at SCBASE + 0x2000 and holds one byte per 8x8 cell: the high nibble is the
     * colour for %01 and the low nibble for %10. The game writes it (RED2, GREEN2, YELLOW2 and
     * BLACK2 for the missile indicators, MAG2 for the text view) and it also EORs it -- BULBCOL is
     * documented in the masters as "EOR'd into screen RAM" to toggle the E.C.M. and station bulbs.
     * Cell colour is mutable game state on the same footing as the bitmap, not a fixed palette.
     *
     * The three-cell offset in celllook looks like it disagrees with the bitmap's four-cell left
     * margin, and it does not: CHPR writes the colour AFTER advancing the cursor, so the colour
     * lands at celllook + (XC + 1) = cell 4 + XC, which is exactly the cell the glyph went into.
     * TextAndItsColourLandInTheSameCell measures that rather than trusting this paragraph.
     */
    TEST_METHOD(CellColourIsASeparateMutablePlane)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Cpu6502 cpu = oracle.Fresh();

      const std::uint16_t scbase = ScreenBase(cpu, oracle);
      const std::uint16_t cellLow = oracle.Label("celllookl");
      const std::uint16_t cellHigh = oracle.Label("celllookh");

      const auto cellAddress = [&](int _row)
      { return static_cast<std::uint16_t>(cpu.memory[cellLow + _row] | (cpu.memory[cellHigh + _row] << 8)); };

      std::string report = "COLOUR CELL PLANE\n";
      for (const int row : {0, 1, 18, 24})
      {
        report += "  celllook[" + std::to_string(row) + "] = " + Hex(cellAddress(row), 4) + "  = SCBASE + " +
                  Hex(static_cast<std::uint16_t>(cellAddress(row) - scbase), 4) + "\n";
      }
      report += "  bitmap left margin   " + Hex(SPACE_VIEW_MARGIN, 2) + " bytes = 4 cells\n";
      report += "  celllook offset      3 cells, because CHPR colours the cell after INC XC\n";
      Logger::WriteMessage(report.c_str());

      Assert::AreEqual<std::uint32_t>(SCREEN_RAM_OFFSET + 3u, static_cast<std::uint32_t>(cellAddress(0) - scbase),
                                      L"screen RAM starts at SCBASE + 0x2000 and celllook is offset three cells into it");
      Assert::AreEqual<std::uint32_t>(40u, static_cast<std::uint32_t>(cellAddress(1) - cellAddress(0)),
                                      L"one colour byte per cell, 40 cells to a row");
    }

    /*
     * 6502: CHPR -- one character into the bitmap, then its colour into the cell.
     *
     * This is the test that settles where text sits, and it is worth having because the two
     * address paths look like they disagree. The glyph goes to bitmap cell 4 + XC (CHPR builds
     * SCBASE + YC * 320 + 32 + XC * 8, the same 32-byte margin ylookup uses). The colour goes to
     * celllook[YC] + XC, and celllook starts three cells into screen RAM -- but only after the
     * routine has already incremented XC. Both therefore land on cell 4 + XC.
     *
     * The port must not "tidy" that by making celllook start at four cells: the increment is
     * ordinary cursor advance and other callers depend on it.
     */
    TEST_METHOD(TextAndItsColourLandInTheSameCell)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();

      for (std::uint8_t column = 0; column < 3; ++column)
      {
        Cpu6502 cpu = oracle.Fresh();
        const std::uint16_t scbase = ScreenBase(cpu, oracle);
        const std::array<std::uint8_t, 65536> before = cpu.memory;

        cpu.memory[oracle.Label("XC")] = column;
        cpu.memory[oracle.Label("YC")] = 3;
        cpu.memory[oracle.Label("COL2")] = 0x40; // 6502: MAG2 -- purple on black, the text view's cell colour.
        cpu.a = 'A';
        cpu.x = 0;
        cpu.y = 0;
        cpu.sp = 0xFD;

        const auto run = cpu.CallSubroutine(oracle.Label("CHPR"), 200'000);
        Assert::IsTrue(run.completed, L"CHPR should return");

        int glyphCell = -1;
        int colourCell = -1;
        for (std::uint32_t address = scbase; address < scbase + SCREEN_RAM_OFFSET + 0x400u; ++address)
        {
          if (cpu.memory[address] == before[address])
          {
            continue;
          }
          const std::uint32_t offset = address - scbase;
          if (offset < BITMAP_SIZE)
          {
            if (glyphCell < 0)
            {
              glyphCell = static_cast<int>((offset % 320) / 8);
            }
          }
          else if (colourCell < 0)
          {
            colourCell = static_cast<int>((offset - SCREEN_RAM_OFFSET) % 40);
          }
        }

        Logger::WriteMessage(("CHPR 'A' at XC = " + std::to_string(column) + "   glyph cell " + std::to_string(glyphCell) +
                              "   colour cell " + std::to_string(colourCell) +
                              "   XC after = " + std::to_string(cpu.memory[oracle.Label("XC")]) + "\n")
                               .c_str());

        Assert::AreEqual(4 + static_cast<int>(column), glyphCell, L"the glyph belongs in cell 4 + XC");
        Assert::AreEqual(glyphCell, colourCell, L"the colour belongs in the same cell as the glyph");
        Assert::AreEqual<std::uint32_t>(column + 1u, cpu.memory[oracle.Label("XC")], L"CHPR advances the cursor");
      }
    }

    /// A guard on the reasoning above rather than on the game. If IsMulticolourAligned is wrong,
    /// every conclusion in this file is wrong, and it is four lines nobody would think to test.
    TEST_METHOD(TheAlignmentPredicateItselfIsCorrect)
    {
      Assert::IsTrue(IsMulticolourAligned(0b11000000), L"pixel 0 set");
      Assert::IsTrue(IsMulticolourAligned(0b00110000), L"pixel 1 set");
      Assert::IsTrue(IsMulticolourAligned(0b00000011), L"pixel 3 set");
      Assert::IsTrue(IsMulticolourAligned(0b11111111), L"every pixel set");
      Assert::IsTrue(IsMulticolourAligned(0b00000000), L"nothing set");
      Assert::IsFalse(IsMulticolourAligned(0b01100000), L"one bit of pixel 0 and one of pixel 1");
      Assert::IsFalse(IsMulticolourAligned(0b00011000), L"one bit of pixel 1 and one of pixel 2");
      Assert::IsFalse(IsMulticolourAligned(0b00000110), L"one bit of pixel 2 and one of pixel 3");

      Assert::AreEqual<std::uint32_t>(0u, PixelCode(0b00110000, 0), L"pixel 0 of %00110000");
      Assert::AreEqual<std::uint32_t>(3u, PixelCode(0b00110000, 1), L"pixel 1 of %00110000");
      Assert::AreEqual<std::uint32_t>(1u, PixelCode(0b01000000, 0), L"pixel 0 of %01000000");
    }
  };

} // namespace GameLogicTests

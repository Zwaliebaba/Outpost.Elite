#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "LookupTables.h"
#include "Scanner.h"
#include "ShipSlot.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The scanner and the compass (slice 3d-a).
 *
 * Both are compared on the SCREEN and not on their arguments, because both are drawing routines
 * and the interesting part of each is where the marks land. `SCAN` in particular does its own
 * address arithmetic -- it walks on from where `CPIX4` left the cursor rather than plotting (x,
 * y) pairs -- so a port that agreed about every coordinate could still put the stick in the
 * wrong character block.
 *
 * THE SWEEPS ACCUMULATE NOTHING. Each case starts from a clear screen on both sides, which costs
 * two memsets and buys the ability to measure which ROWS a case marked -- and that is what says
 * the sweep reached all three shapes of stick rather than one of them sixteen thousand times.
 */
namespace GameLogicTests
{

  namespace
  {
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

    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    /// Every zero-page byte the two routines read or write, plus the bitmap base.
    struct Labels
    {
      std::uint16_t inwk = 0, type = 0, qq11 = 0, col = 0, x1 = 0, y1 = 0, x2 = 0, sc = 0;
      std::uint16_t comx = 0, comy = 0, comc = 0, k3 = 0, kPercent = 0, frin = 0, many = 0;
      std::uint16_t p = 0, q = 0, t = 0, screen = 0;

      explicit Labels(const OracleImage& _oracle)
      {
        inwk = _oracle.Label("INWK");
        type = _oracle.Label("TYPE");
        qq11 = _oracle.Label("QQ11");
        col = _oracle.Label("COL");
        x1 = _oracle.Label("X1");
        y1 = _oracle.Label("Y1");
        x2 = _oracle.Label("X2");
        sc = _oracle.Label("SC");
        comx = _oracle.Label("COMX");
        comy = _oracle.Label("COMY");
        comc = _oracle.Label("COMC");
        k3 = _oracle.Label("K3");
        kPercent = _oracle.Label("K%");
        frin = _oracle.Label("FRIN");
        many = _oracle.Label("MANY");
        p = _oracle.Label("P");
        q = _oracle.Label("Q");
        t = _oracle.Label("T");

        // 6502: SCBASE, an assembler constant rather than a label -- ylookup's first entry is it
        // plus the space view's four-cell left margin.
        const Cpu6502 cpu = _oracle.Fresh();
        screen = static_cast<std::uint16_t>((cpu.memory[_oracle.Label("ylookupl")] | (cpu.memory[_oracle.Label("ylookuph")] << 8)) - 0x20);
      }
    };

    /// What one case drew, measured from the bitmap rather than worked out from the inputs -- so a
    /// coverage counter cannot agree with a re-implementation of the routine it is counting.
    struct Marks
    {
      std::uint32_t bytes = 0;
      int top = 1000;  ///< lowest screen row touched
      int bottom = -1; ///< highest
    };

    /// Compares the port's bitmap against the oracle's and reports what was marked. One walk does
    /// both, because the walk is the expensive part of a sixteen-thousand-case sweep.
    Marks CompareAndMeasure(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas, const std::wstring& _context)
    {
      const std::span<const std::uint8_t> ours = _canvas.Screen();
      Marks marks;

      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
        if (expected != ours[offset])
        {
          Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset) + L" -- game has " + std::to_wstring(expected) +
                        L", port has " + std::to_wstring(ours[offset]))
                         .c_str());
        }

        if (ours[offset] != 0u && offset >= 0x20u && offset < Elite::Canvas::BITMAP_SIZE)
        {
          ++marks.bytes;
          const int row = static_cast<int>((offset - 0x20u) / Elite::Canvas::ROW_BYTES) * 8 + static_cast<int>(offset & 7u);
          marks.top = (row < marks.top) ? row : marks.top;
          marks.bottom = (row > marks.bottom) ? row : marks.bottom;
        }
      }

      return marks;
    }

    void ClearScreen(Cpu6502& _cpu, std::uint16_t _base) noexcept
    {
      std::memset(&_cpu.memory[_base], 0, Elite::Canvas::SCREEN_SIZE);
    }

    /// One ship's nine coordinate bytes plus its state and type, written to both sides.
    void PlaceShip(Cpu6502& _cpu, const Labels& _at, Elite::ShipBlock& _ship, const std::array<std::uint8_t, 6>& _position,
                   std::uint8_t _state, std::uint8_t _type) noexcept
    {
      const std::uint8_t OFFSETS[6] = {1u, 2u, 4u, 5u, 7u, 8u};
      _ship = Elite::ShipBlock{};
      for (int which = 0; which < 6; ++which)
      {
        _ship[OFFSETS[which]] = _position[static_cast<std::size_t>(which)];
        _cpu.memory[static_cast<std::uint16_t>(_at.inwk + OFFSETS[which])] = _position[static_cast<std::size_t>(which)];
      }
      _ship[31] = _state;
      _cpu.memory[static_cast<std::uint16_t>(_at.inwk + 31)] = _state;
      _cpu.memory[_at.type] = _type;
    }
  } // namespace

  TEST_CLASS(TheScanner)
  {
  public:
    /*
     * 6502: SCAN -- exhaustive in DEPTH and HEIGHT, which is what the routine is about.
     *
     * Every z high byte the range check lets through, both signs of it, every y high byte and both
     * signs of that: 16,384 cases, and between them they produce every baseline row the scanner
     * has and every stick height that can reach it, in both directions and through every character
     * block boundary the walk can cross.
     *
     * §6.50's question -- exhaustive in WHICH dimension -- answered by asking what `SCAN` branches
     * on. It branches on the sign of each coordinate, on two clamps, and on which way the stick
     * goes; x decides only where the blip sits across the screen, so x gets its own sweep below
     * rather than multiplying this one by 128.
     */
    TEST_METHOD(TheBlipAndItsStickMatchSCAN)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t scan = oracle.Label("SCAN");

      Cpu6502 cpu = oracle.Fresh();
      cpu.memory[at.qq11] = 0;

      Elite::Canvas canvas;
      Elite::DrawWorkspace draw;
      Elite::ShipBlock ship;

      std::uint32_t compared = 0;
      std::uint32_t up = 0;
      std::uint32_t down = 0;
      std::uint32_t dotOnly = 0;
      std::uint32_t clampedLow = 0;
      std::uint32_t clampedHigh = 0;
      std::uint32_t tallest = 0;

      for (std::uint32_t depth = 0; depth < 64; ++depth)
      {
        for (const std::uint8_t depthSign : {std::uint8_t{0}, std::uint8_t{0x80}})
        {
          for (std::uint32_t height = 0; height < 64; ++height)
          {
            for (const std::uint8_t heightSign : {std::uint8_t{0}, std::uint8_t{0x80}})
            {
              ClearScreen(cpu, at.screen);
              canvas.Clear();

              const std::array<std::uint8_t, 6> position = {
                0x15u, 0x00u, static_cast<std::uint8_t>(height), heightSign, static_cast<std::uint8_t>(depth), depthSign,
              };
              PlaceShip(cpu, at, ship, position, 0x10u, 11u);

              const Elite::Testing::RunResult run = cpu.CallSubroutine(scan, 20'000);
              Assert::IsTrue(run.completed, L"SCAN returned");

              Elite::DrawScannerBlip(canvas, draw, ship, 11u, 0u);

              const std::wstring where = Widen("SCAN z=" + std::to_string(depth) + (depthSign ? "-" : "+") +
                                               " y=" + std::to_string(height) + (heightSign ? "-" : "+"));

              const Marks marks = CompareAndMeasure(cpu, at.screen, canvas, where);

              /*
               * The three bytes it leaves behind, which are not incidental: `X1` stops being a
               * coordinate and becomes the stick's pixel pattern, `Y1` comes back one less than
               * the row because `CPIX4` decrements it, and `COL` is the type's scanner colour.
               */
              Assert::AreEqual(cpu.memory[at.x1], draw.x1, (where + L": X1").c_str());
              Assert::AreEqual(cpu.memory[at.y1], draw.y1, (where + L": Y1").c_str());
              Assert::AreEqual(cpu.memory[at.col], draw.col, (where + L": COL").c_str());

              /*
               * Which shape of stick this case drew, measured from the marks. The dot occupies the
               * blip's row and the one above it, so anything below is a downward stick and
               * anything two rows above is an upward one.
               */
              const int blip = static_cast<int>(draw.y1) + 1;
              if (marks.bottom > blip)
              {
                ++down;
              }
              else if (marks.top < blip - 1)
              {
                ++up;
              }
              else
              {
                ++dotOnly;
              }

              const std::uint32_t span = static_cast<std::uint32_t>(marks.bottom - marks.top);
              tallest = (span > tallest) ? span : tallest;

              clampedLow += (blip == 146) ? 1u : 0u;
              clampedHigh += (blip == 198) ? 1u : 0u;
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(64u * 2u * 64u * 2u, compared, L"the whole sweep ran");
      Assert::IsTrue(up > 100u, L"sticks were drawn upwards");
      Assert::IsTrue(down > 100u, L"and downwards");
      Assert::IsTrue(dotOnly > 0u, L"and some ships sat exactly on the plane of flight");
      Assert::IsTrue(clampedLow > 0u, L"the top clamp bit");
      Assert::IsTrue(clampedHigh > 0u, L"and the bottom one");
      Assert::IsTrue(tallest > 8u, L"and the longest stick crossed a character block");

      Logger::WriteMessage(("SCAN: " + std::to_string(compared) + " blips -- " + std::to_string(up) + " up, " + std::to_string(down) +
                            " down, " + std::to_string(dotOnly) + " dot only; tallest " + std::to_string(tallest) + " rows")
                             .c_str());
    }

    /*
     * 6502: SCAN across the scanner -- exhaustive in x, both signs.
     *
     * This is the sweep that covers the wrap: the dot's second pixel is `CTWOS2+2,X`, and for
     * x AND 7 of 6 or 7 that mask has bit 7 set, which moves the cursor into the NEXT character
     * cell -- and the stick then has to be drawn in that cell rather than in the dot's own.
     *
     * It also covers the quirk at zero. `CLC` sits above the branch, so the negating path's
     * `ADC #1` leaves a carry when x_hi is zero and the `ADC #123` below adds it: a ship at
     * x_hi = 0 with the sign bit set lands one pixel to the right of one without it.
     */
    TEST_METHOD(TheBlipAcrossTheScannerMatchesSCAN)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t scan = oracle.Label("SCAN");

      Cpu6502 cpu = oracle.Fresh();
      cpu.memory[at.qq11] = 0;

      Elite::Canvas canvas;
      Elite::DrawWorkspace draw;
      Elite::ShipBlock ship;

      struct Depth
      {
        std::uint8_t height;
        std::uint8_t heightSign;
        std::uint8_t depth;
        std::uint8_t depthSign;
      };
      const Depth DEPTHS[] = {
        {0u, 0u, 0u, 0u}, {40u, 0u, 20u, 0u}, {40u, 0x80u, 20u, 0u}, {63u, 0u, 63u, 0x80u}, {63u, 0x80u, 63u, 0u},
      };

      std::uint32_t compared = 0;
      std::uint32_t wrapped = 0;
      std::uint32_t marked = 0;
      std::set<std::uint8_t> patterns;

      for (std::uint32_t across = 0; across < 64; ++across)
      {
        for (const std::uint8_t sign : {std::uint8_t{0}, std::uint8_t{0x80}})
        {
          for (const Depth& item : DEPTHS)
          {
            ClearScreen(cpu, at.screen);
            canvas.Clear();

            const std::array<std::uint8_t, 6> position = {
              static_cast<std::uint8_t>(across), sign, item.height, item.heightSign, item.depth, item.depthSign};
            PlaceShip(cpu, at, ship, position, 0x10u, 11u);

            const Elite::Testing::RunResult run = cpu.CallSubroutine(scan, 20'000);
            Assert::IsTrue(run.completed, L"SCAN returned");

            Elite::DrawScannerBlip(canvas, draw, ship, 11u, 0u);

            const std::wstring where =
              Widen("SCAN x=" + std::to_string(across) + (sign ? "-" : "+") + " depth " + std::to_string(item.depth));

            const Marks marks = CompareAndMeasure(cpu, at.screen, canvas, where);
            marked += marks.bytes;

            Assert::AreEqual(cpu.memory[at.x1], draw.x1, (where + L": X1").c_str());

            /*
             * 6502: LDA CTWOS2+2,X / BPL CP1. `X1` comes back holding the stick's pixel pattern,
             * which is `CTWOS2+2,X AND COL` -- so the pattern IS the alignment, and the one that
             * says the dot's second pixel crossed into the next cell is the negative mask 0xC0.
             * Counting them here needs nothing worked out from x: the byte the routine left says
             * which case it took, and it is compared against the game's on the line above.
             */
            patterns.insert(draw.x1);
            wrapped += (draw.x1 == static_cast<std::uint8_t>(0xC0u & Elite::SCANNER_COLOUR_TABLE[11])) ? 1u : 0u;
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(64u * 2u * 5u, compared, L"the whole sweep ran");
      Assert::AreEqual<std::size_t>(4u, patterns.size(), L"all four of a colour's pixel patterns were reached");
      Assert::IsTrue(wrapped > 0u, L"and some dots wrapped into the next character cell");
      Assert::IsTrue(marked > 0u, L"and blips were actually drawn");
    }

    /*
     * 6502: SCAN's four guards and its colour table -- every ship type, and the three ways out.
     *
     * The colour is the point of the sweep over types: `scacol` is thirty-four entries of which
     * twenty-one are the same yellow, two are zero, and one is the Thargoid's striped `WHITE`, so
     * a port that indexed it one out would agree with the game for most of the fleet.
     */
    TEST_METHOD(TheGuardsAndTheColoursMatchSCAN)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t scan = oracle.Label("SCAN");

      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      Elite::DrawWorkspace draw;
      Elite::ShipBlock ship;

      struct Case
      {
        const char* what;
        std::uint8_t view;  ///< QQ11
        std::uint8_t state; ///< INWK+31
        std::uint8_t type;  ///< TYPE
        std::array<std::uint8_t, 6> position;
        bool draws;
      };

      const std::array<std::uint8_t, 6> ON = {0x15u, 0u, 0x20u, 0u, 0x18u, 0u};
      const std::vector<Case> CASES = {
        {"an ordinary Cobra on the space view", 0u, 0x10u, 11u, ON, true},
        {"the same ship with a chart on screen", 1u, 0x10u, 11u, ON, false},
        {"and with the short-range chart", 128u, 0x10u, 11u, ON, false},
        {"a ship whose scanner bit is clear", 0u, 0x00u, 11u, ON, false},
        {"the planet", 0u, 0x10u, 128u, ON, false},
        {"the sun", 0u, 0x10u, 129u, ON, false},
        {"too far across", 0u, 0x10u, 11u, {0x40u, 0u, 0x20u, 0u, 0x18u, 0u}, false},
        {"too far up", 0u, 0x10u, 11u, {0x15u, 0u, 0x80u, 0u, 0x18u, 0u}, false},
        {"too far away", 0u, 0x10u, 11u, {0x15u, 0u, 0x20u, 0u, 0xC0u, 0u}, false},
        {"just inside the range check", 0u, 0x10u, 11u, {0x3Fu, 0u, 0x3Fu, 0u, 0x3Fu, 0u}, true},
      };

      std::uint32_t compared = 0;
      std::uint32_t drew = 0;

      for (const Case& item : CASES)
      {
        ClearScreen(cpu, at.screen);
        canvas.Clear();
        cpu.memory[at.qq11] = item.view;
        PlaceShip(cpu, at, ship, item.position, item.state, item.type);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(scan, 20'000);
        Assert::IsTrue(run.completed, L"SCAN returned");

        Elite::DrawScannerBlip(canvas, draw, ship, item.type, item.view);

        const std::wstring where = Widen(std::string("SCAN: ") + item.what);
        const Marks marks = CompareAndMeasure(cpu, at.screen, canvas, where);

        Assert::AreEqual(item.draws, marks.bytes != 0u, (where + L": whether it drew").c_str());
        drew += (marks.bytes != 0u) ? 1u : 0u;
        ++compared;
      }

      // And every type the colour table can be indexed by, on a ship that does draw.
      std::uint32_t colours = 0;
      cpu.memory[at.qq11] = 0;
      for (std::uint32_t type = 1; type <= Elite::SHIP_TYPE_COUNT; ++type)
      {
        ClearScreen(cpu, at.screen);
        canvas.Clear();
        PlaceShip(cpu, at, ship, ON, 0x10u, static_cast<std::uint8_t>(type));

        const Elite::Testing::RunResult run = cpu.CallSubroutine(scan, 20'000);
        Assert::IsTrue(run.completed, L"SCAN returned");

        Elite::DrawScannerBlip(canvas, draw, ship, static_cast<std::uint8_t>(type), 0u);

        const std::wstring where = Widen("SCAN type " + std::to_string(type));
        (void)CompareAndMeasure(cpu, at.screen, canvas, where);
        Assert::AreEqual(cpu.memory[at.col], draw.col, (where + L": COL from scacol").c_str());
        ++colours;
      }

      Assert::AreEqual<std::uint32_t>(10u, compared, L"every guard");
      Assert::AreEqual<std::uint32_t>(2u, drew, L"two of the cases draw and the rest do not");
      Assert::AreEqual<std::uint32_t>(Elite::SHIP_TYPE_COUNT, colours, L"every ship type");
    }
  };

  TEST_CLASS(TheCompass)
  {
  public:
    /*
     * 6502: TAS2 -- normalising three coordinates into a direction.
     *
     * The dimension that matters is HOW MANY SHIFTS the loop makes, and that is decided by the
     * largest high byte together with the ORed low bytes -- so the sweep is over high bytes that
     * bracket every shift count from none to sixteen, in all three axes independently, with the
     * low bytes and signs cycling underneath.
     */
    TEST_METHOD(NormalisingMatchesTAS2)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t tas2 = oracle.Label("TAS2");

      const std::uint8_t HIGHS[] = {0, 1, 2, 3, 7, 15, 31, 63, 64, 127, 128, 255};
      const std::uint8_t LOWS[] = {0, 1, 0x40, 0x80, 0xFF};
      const std::uint8_t SIGNS[] = {0, 0x80};

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;
      std::uint32_t cycle = 0;

      for (const std::uint8_t x : HIGHS)
      {
        for (const std::uint8_t y : HIGHS)
        {
          for (const std::uint8_t z : HIGHS)
          {
            const std::uint8_t highs[3] = {x, y, z};
            for (int variant = 0; variant < 3; ++variant)
            {
              Elite::K3Block axes{};
              for (int axis = 0; axis < 3; ++axis)
              {
                const std::size_t base = static_cast<std::size_t>(axis) * 3u;
                axes[base] = LOWS[(cycle + static_cast<std::uint32_t>(axis)) % 5u];
                axes[base + 1u] = highs[axis];
                axes[base + 2u] = SIGNS[(cycle + static_cast<std::uint32_t>(axis)) % 2u];
                ++cycle;
              }
              axes[9] = 0x5Au; // scribbled, because TAS2 builds it rather than reading it

              for (std::size_t byte = 0; byte < 10u; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(at.k3 + byte)] = axes[byte];
              }

              const Elite::Testing::RunResult run = cpu.CallSubroutine(tas2, 20'000);
              Assert::IsTrue(run.completed, L"TAS2 returned");

              Elite::DrawWorkspace draw;
              Elite::MathWorkspace math;
              Elite::NormaliseAxes(axes, draw, math);

              const std::wstring where = Widen("TAS2 highs " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) +
                                               " variant " + std::to_string(variant));
              for (std::size_t byte = 0; byte < 10u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.k3 + byte)], axes[byte],
                                 (where + L": K3+" + std::to_wstring(byte)).c_str());
              }
              Assert::AreEqual(cpu.memory[at.x1], draw.x1, (where + L": XX15").c_str());
              Assert::AreEqual(cpu.memory[at.y1], draw.y1, (where + L": XX15+1").c_str());
              Assert::AreEqual(cpu.memory[at.x2], draw.x2, (where + L": XX15+2").c_str());
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(12u * 12u * 12u * 3u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: SPS2 -- exhaustive, because there are only 256 inputs and every one of them is a
     * different position on the compass.
     *
     * The carry is asserted alongside the two registers, and it is the whole reason `SPS2` returns
     * three things: `SP2` adds it in one place and subtracts it in another, with nothing between
     * the call and the arithmetic to set it (§6.60).
     */
    TEST_METHOD(TheCompassOffsetMatchesSPS2)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t sps2 = oracle.Label("SPS2");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t carried = 0;

      for (std::uint32_t value = 0; value < 256; ++value)
      {
        for (const bool carryIn : {false, true})
        {
          cpu.a = static_cast<std::uint8_t>(value);
          cpu.c = carryIn;
          cpu.memory[at.q] = 0x5Au; // SPS2 sets Q itself

          const Elite::Testing::RunResult run = cpu.CallSubroutine(sps2, 20'000);
          Assert::IsTrue(run.completed, L"SPS2 returned");

          Elite::MathWorkspace math;
          math.q = 0x5Au;
          const Elite::CompassOffset offset = Elite::ScaleToCompass(math, static_cast<std::uint8_t>(value));

          const std::wstring where = Widen("SPS2(" + std::to_string(value) + ", carry " + std::to_string(carryIn ? 1 : 0) + ")");
          Assert::AreEqual(cpu.x, offset.offset, (where + L": X").c_str());
          Assert::AreEqual(cpu.y, offset.sign, (where + L": Y").c_str());
          Assert::AreEqual(cpu.c, offset.carry, (where + L": the exit carry").c_str());
          Assert::AreEqual(cpu.memory[at.p], math.p, (where + L": P").c_str());
          Assert::AreEqual(cpu.memory[at.q], math.q, (where + L": Q").c_str());
          carried += offset.carry ? 1u : 0u;
        }
      }

      /*
       * And the flag's actual value, stated rather than merely compared: `DVID4` sets the carry
       * only on its saturating exit, the eight division steps always leave it clear, and a
       * remainder is always smaller than the divisor -- so with Q fixed at 20 it can never
       * saturate. `SP2`'s `SBC T` therefore always borrows, which is why 156 comes out as 155.
       */
      Assert::AreEqual<std::uint32_t>(0u, carried, L"the divide never saturates at a radius of 20");
    }

    /*
     * 6502: SPS3 and SPS1 -- the planet's direction, and the low byte SPS3 throws away.
     *
     * `SPS1` falls through into `TAS2` rather than calling it, so the comparison is of the whole
     * chain: nine bytes of `K%` in, `K3` and `XX15` out.
     */
    TEST_METHOD(ThePlanetsDirectionMatchesSPS1)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t sps1 = oracle.Label("SPS1");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;

      for (std::uint32_t seed = 0; seed < 400; ++seed)
      {
        Elite::Bubble bubble;
        Elite::K3Block axes{};

        // A deterministic spread of nine-byte positions, including the ones with the sign bit set
        // and the ones whose sign byte carries seven more bits of magnitude.
        std::uint32_t state = 0x1F35C7B1u ^ (seed * 0x9E3779B9u);
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          state = state * 1103515245u + 12345u;
          const std::uint8_t value = static_cast<std::uint8_t>(state >> 17);
          bubble.blocks[0][byte] = value;
          cpu.memory[static_cast<std::uint16_t>(at.kPercent + byte)] = value;
        }

        for (std::size_t byte = 0; byte < 10u; ++byte)
        {
          axes[byte] = static_cast<std::uint8_t>(0xA5u + byte);
          cpu.memory[static_cast<std::uint16_t>(at.k3 + byte)] = axes[byte];
        }

        const Elite::Testing::RunResult run = cpu.CallSubroutine(sps1, 20'000);
        Assert::IsTrue(run.completed, L"SPS1 returned");

        Elite::DrawWorkspace draw;
        Elite::MathWorkspace math;
        Elite::LoadPlanetAxes(bubble, axes, draw, math);

        const std::wstring where = Widen("SPS1 seed " + std::to_string(seed));
        for (std::size_t byte = 0; byte < 10u; ++byte)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.k3 + byte)], axes[byte],
                           (where + L": K3+" + std::to_wstring(byte)).c_str());
        }
        Assert::AreEqual(cpu.memory[at.x1], draw.x1, (where + L": XX15").c_str());
        Assert::AreEqual(cpu.memory[at.y1], draw.y1, (where + L": XX15+1").c_str());
        Assert::AreEqual(cpu.memory[at.x2], draw.x2, (where + L": XX15+2").c_str());
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(400u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: SPS4 -- the station's direction, which is slot ONE of `K%` and nine bytes copied
     * whole. Getting the slot wrong is the failure this is for: slot 0 is the planet, and a
     * compass that pointed at the planet while a station was present would look plausible.
     */
    TEST_METHOD(TheStationsDirectionMatchesSPS4)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t sps4 = oracle.Label("SPS4");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;

      for (std::uint32_t seed = 0; seed < 400; ++seed)
      {
        Elite::Bubble bubble;
        Elite::K3Block axes{};

        std::uint32_t state = 0x77A31D05u ^ (seed * 0x85EBCA6Bu);
        for (std::size_t slot = 0; slot < 2u; ++slot)
        {
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            state = state * 1103515245u + 12345u;
            const std::uint8_t value = static_cast<std::uint8_t>(state >> 17);
            bubble.blocks[slot][byte] = value;
            cpu.memory[static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
          }
        }

        for (std::size_t byte = 0; byte < 10u; ++byte)
        {
          axes[byte] = static_cast<std::uint8_t>(0x3Cu + byte);
          cpu.memory[static_cast<std::uint16_t>(at.k3 + byte)] = axes[byte];
        }

        const Elite::Testing::RunResult run = cpu.CallSubroutine(sps4, 20'000);
        Assert::IsTrue(run.completed, L"SPS4 returned");

        Elite::DrawWorkspace draw;
        Elite::MathWorkspace math;
        Elite::LoadStationAxes(bubble, axes, draw, math);

        const std::wstring where = Widen("SPS4 seed " + std::to_string(seed));
        for (std::size_t byte = 0; byte < 10u; ++byte)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.k3 + byte)], axes[byte],
                           (where + L": K3+" + std::to_wstring(byte)).c_str());
        }
        Assert::AreEqual(cpu.memory[at.x1], draw.x1, (where + L": XX15").c_str());
        Assert::AreEqual(cpu.memory[at.y1], draw.y1, (where + L": XX15+1").c_str());
        Assert::AreEqual(cpu.memory[at.x2], draw.x2, (where + L": XX15+2").c_str());
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(400u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: DOT -- exhaustive across the compass in x, over the rows it can reach, and in both
     * shapes. The colour is what chooses the shape, so the third colour in the sweep is one the
     * game never sets: `DOT` compares against `YELLOW` and everything else takes the dash.
     */
    TEST_METHOD(TheCompassDotMatchesDOT)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t dot = oracle.Label("DOT");

      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      Elite::DrawWorkspace draw;

      std::uint32_t compared = 0;
      std::uint32_t blocks = 0;
      std::uint32_t dashes = 0;

      for (std::uint32_t x = 0; x < 256; ++x)
      {
        for (const std::uint8_t y : {143u, 144u, 150u, 156u, 160u, 167u, 168u, 190u})
        {
          for (const std::uint8_t colour : {Elite::COMPASS_AHEAD, Elite::COMPASS_BEHIND, std::uint8_t{0x55}})
          {
            ClearScreen(cpu, at.screen);
            canvas.Clear();

            const Elite::Compass compass{static_cast<std::uint8_t>(x), y, colour};
            cpu.memory[at.comx] = compass.x;
            cpu.memory[at.comy] = compass.y;
            cpu.memory[at.comc] = compass.colour;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(dot, 20'000);
            Assert::IsTrue(run.completed, L"DOT returned");

            Elite::DrawCompassDot(canvas, draw, compass);

            const std::wstring where =
              Widen("DOT(" + std::to_string(x) + ", " + std::to_string(y) + ", colour " + std::to_string(colour) + ")");
            const Marks marks = CompareAndMeasure(cpu, at.screen, canvas, where);

            Assert::AreEqual(cpu.memory[at.x1], draw.x1, (where + L": X1").c_str());
            Assert::AreEqual(cpu.memory[at.y1], draw.y1, (where + L": Y1").c_str());
            Assert::AreEqual(cpu.memory[at.col], draw.col, (where + L": COL").c_str());

            if (colour == Elite::COMPASS_AHEAD)
            {
              Assert::IsTrue(marks.bottom - marks.top >= 1, (where + L": a block is two rows").c_str());
              ++blocks;
            }
            else
            {
              Assert::AreEqual(marks.top, marks.bottom, (where + L": a dash is one row").c_str());
              ++dashes;
            }
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(256u * 8u * 3u, compared, L"the whole sweep ran");
      Assert::IsTrue(blocks > 0u && dashes > 0u, L"both shapes were drawn");
    }

    /*
     * 6502: SP2 -- exhaustive in the two coordinates that move the dot, which is 65,536 cases.
     *
     * The third coordinate only chooses the colour, so it takes two values rather than 256, and
     * the sweep is over the pair that decides where the dot goes. Compared on `COMX`, `COMY`,
     * `COMC` AND the bitmap, because `SP2` ends `JMP DOT`: the drawing is part of the routine.
     */
    TEST_METHOD(TheCompassMatchesSP2)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t sp2 = oracle.Label("SP2");

      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      Elite::DrawWorkspace draw;

      std::uint32_t compared = 0;
      std::uint32_t ahead = 0;
      std::uint32_t behind = 0;

      for (std::uint32_t across = 0; across < 256; ++across)
      {
        for (std::uint32_t down = 0; down < 256; ++down)
        {
          const std::uint8_t depth = static_cast<std::uint8_t>(((across + down) & 1u) ? 0x40u : 0xC0u);

          ClearScreen(cpu, at.screen);
          canvas.Clear();

          cpu.memory[at.x1] = static_cast<std::uint8_t>(across);
          cpu.memory[at.y1] = static_cast<std::uint8_t>(down);
          cpu.memory[at.x2] = depth;

          const Elite::Testing::RunResult run = cpu.CallSubroutine(sp2, 20'000);
          Assert::IsTrue(run.completed, L"SP2 returned");

          Elite::MathWorkspace math;
          Elite::Compass compass;
          draw.x1 = static_cast<std::uint8_t>(across);
          draw.y1 = static_cast<std::uint8_t>(down);
          draw.x2 = depth;
          Elite::DrawCompass(canvas, draw, math, compass);

          const std::wstring where = Widen("SP2(" + std::to_string(across) + ", " + std::to_string(down) + ")");

          Assert::AreEqual(cpu.memory[at.comx], compass.x, (where + L": COMX").c_str());
          Assert::AreEqual(cpu.memory[at.comy], compass.y, (where + L": COMY").c_str());
          Assert::AreEqual(cpu.memory[at.comc], compass.colour, (where + L": COMC").c_str());
          Assert::AreEqual(cpu.memory[at.t], math.t, (where + L": T").c_str());
          (void)CompareAndMeasure(cpu, at.screen, canvas, where);

          ahead += (compass.colour == Elite::COMPASS_AHEAD) ? 1u : 0u;
          behind += (compass.colour == Elite::COMPASS_BEHIND) ? 1u : 0u;
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(256u * 256u, compared, L"the whole sweep ran");
      Assert::IsTrue(ahead > 0u && behind > 0u, L"both directions were reached");
    }

    /*
     * 6502: COMPAS -- the whole thing, both targets, with the dot erased before it is redrawn.
     *
     * The erase is what makes this more than `SP2` twice: `COMPAS` opens by drawing the OLD dot
     * again, so the compass state has to survive between calls and the second frame has to take
     * the first frame's mark off. The sweep therefore runs each case TWICE with the same state
     * carried across, and a port that forgot the erase would agree on the first frame and diverge
     * on the second.
     */
    TEST_METHOD(TheWholeCompassMatchesCOMPAS)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t compas = oracle.Label("COMPAS");

      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      Elite::DrawWorkspace draw;

      std::uint32_t compared = 0;
      std::uint32_t stationCases = 0;
      std::uint32_t marked = 0;

      for (std::uint32_t seed = 0; seed < 120; ++seed)
      {
        for (const std::uint8_t stations : {std::uint8_t{0}, std::uint8_t{1}})
        {
          ClearScreen(cpu, at.screen);
          canvas.Clear();

          Elite::Bubble bubble;
          Elite::Compass compass{0xC3u, 0x9Cu, Elite::COMPASS_AHEAD};
          cpu.memory[at.comx] = compass.x;
          cpu.memory[at.comy] = compass.y;
          cpu.memory[at.comc] = compass.colour;

          // 6502: SSPR is MANY+SST, so setting the count IS setting the flag (§6.58).
          bubble.counts[Elite::SHIP_TYPE_STATION] = stations;
          cpu.memory[static_cast<std::uint16_t>(at.many + Elite::SHIP_TYPE_STATION)] = stations;

          std::uint32_t state = 0x2C41A9F7u ^ (seed * 0xC2B2AE35u);
          for (std::size_t slot = 0; slot < 2u; ++slot)
          {
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              state = state * 1103515245u + 12345u;
              const std::uint8_t value = static_cast<std::uint8_t>(state >> 17);
              bubble.blocks[slot][byte] = value;
              cpu.memory[static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
            }
          }

          for (int frame = 0; frame < 2; ++frame)
          {
            const Elite::Testing::RunResult run = cpu.CallSubroutine(compas, 40'000);
            Assert::IsTrue(run.completed, L"COMPAS returned");

            Elite::MathWorkspace math;
            Elite::UpdateCompass(canvas, draw, math, compass, bubble);

            const std::wstring where =
              Widen("COMPAS seed " + std::to_string(seed) + (stations ? " station" : " planet") + " frame " + std::to_string(frame));

            Assert::AreEqual(cpu.memory[at.comx], compass.x, (where + L": COMX").c_str());
            Assert::AreEqual(cpu.memory[at.comy], compass.y, (where + L": COMY").c_str());
            Assert::AreEqual(cpu.memory[at.comc], compass.colour, (where + L": COMC").c_str());

            const Marks marks = CompareAndMeasure(cpu, at.screen, canvas, where);
            marked += marks.bytes;
            ++compared;
          }

          stationCases += stations;
        }
      }

      Assert::AreEqual<std::uint32_t>(120u * 2u * 2u, compared, L"the whole sweep ran");
      Assert::AreEqual<std::uint32_t>(120u, stationCases, L"half the cases had a station");
      Assert::IsTrue(marked > 0u, L"and the compass was actually drawn");
    }
  };

} // namespace GameLogicTests

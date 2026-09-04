#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "LineHeap.h"
#include "ShipDraw.h"
#include "ShipSlot.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The perspective projection (slice 3b).
 *
 * Everything the space view draws is positioned by two divisions, and those divisions are one
 * routine with three names -- `DVID3B` does the arithmetic, `DVID3B2` points it at a ship's z,
 * and `PLS6` decides whether the answer will fit on a screen. Each is swept against the shipped
 * code separately, because each has a caller in a different slice.
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

/// The bytes that decide how far each scaling loop shifts: side of 64, side of 128, and the ends.
const std::vector<std::uint8_t> EDGES = { 0, 1, 2, 63, 64, 127, 128, 255 };

/// The low bytes, which reach the answer only through the scaling, so three values are enough.
const std::vector<std::uint8_t> LOW = { 0, 1, 0x80, 0xFF };

/// Somewhere in the arena between `K%` and `LS%` for a ship's line heap to live, with room above
/// it for the longest heap any blueprint asks for (157 bytes, the Anaconda and the Constrictor).
constexpr std::uint16_t HEAP_AT = 0xFE00;

/// 6502: SCBASE, derived the way `CanvasTests.cpp` derives it -- from ylookup's first entry less
/// the space view's four-cell left margin, because it is an assembler constant and not a label.
std::uint16_t ScreenBase(const OracleImage& _oracle)
{
  const Cpu6502 cpu = _oracle.Fresh();
  const std::uint16_t low = _oracle.Label("ylookupl");
  const std::uint16_t high = _oracle.Label("ylookuph");
  return static_cast<std::uint16_t>((cpu.memory[low] | (cpu.memory[high] << 8)) - 0x20u);
}

void CompareScreens(const Cpu6502& _cpu, std::uint16_t _screenBase, const Elite::Canvas& _canvas,
                    const std::wstring& _context)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();
  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_screenBase + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_context + L": the screen differs at offset " + std::to_wstring(offset)
                    + L" -- game has " + std::to_wstring(expected) + L", port has "
                    + std::to_wstring(ours[offset]))
                     .c_str());
    }
  }
}

/// Put the same heap bytes in both, and clear what is around them so a stray write shows up.
void SeedHeap(Cpu6502& _cpu, Elite::LineHeap& _heap, const std::vector<std::uint8_t>& _bytes)
{
  for (std::uint16_t offset = 0; offset < 256u; ++offset)
  {
    const std::uint16_t address = static_cast<std::uint16_t>(HEAP_AT + offset);
    const std::uint8_t value = (offset < _bytes.size()) ? _bytes[offset] : std::uint8_t{ 0 };
    _cpu.memory[address] = value;
    _heap.Write(address, value);
  }
}

void CompareHeaps(const Cpu6502& _cpu, const Elite::LineHeap& _heap, const std::wstring& _context)
{
  for (std::uint16_t offset = 0; offset < 256u; ++offset)
  {
    const std::uint16_t address = static_cast<std::uint16_t>(HEAP_AT + offset);
    Assert::AreEqual(_cpu.memory[address], _heap.Read(address),
                     (_context + L": heap byte " + std::to_wstring(offset)).c_str());
  }
}
} // namespace

TEST_CLASS(TheProjectionDivide)
{
public:
  /*
   * 6502: DVID3B -- K(3 2 1 0) = P(2 1 0) / (S R Q).
   *
   * The whole denominator is swept, not just its sign, because the routine's answer depends on
   * how far LEFT each side can be shifted before it overflows -- so 63 against 64 and 127
   * against 128 are the interesting values rather than the extremes.
   *
   * An all-zero denominator is skipped, and that is a statement about the routine rather than
   * about the test: with nothing to shift up into bit 7 the original's `BPL DVL6` never falls
   * through, and it spins. `DVID3B2` is what stops that happening, with an `ORA #1`.
   */
  TEST_METHOD(TheDivideMatchesDVID3B)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t pp = oracle.Label("P");
    const std::uint16_t qq = oracle.Label("Q");
    const std::uint16_t rr = oracle.Label("R");
    const std::uint16_t ss = oracle.Label("S");
    const std::uint16_t tt = oracle.Label("T");
    const std::uint16_t kk = oracle.Label("K");
    const std::uint16_t dvid3b = oracle.Label("DVID3B");

    std::uint32_t compared = 0;
    std::uint32_t scaledUp = 0;
    std::uint32_t scaledDown = 0;
    std::uint32_t unscaled = 0;
    std::uint32_t unclassified = 0;

    for (const std::uint8_t p : LOW)
    {
      for (const std::uint8_t p1 : LOW)
      {
        for (const std::uint8_t p2 : EDGES)
        {
          for (const std::uint8_t q : EDGES)
          {
            for (const std::uint8_t r : EDGES)
            {
              for (const std::uint8_t s : EDGES)
              {
                if ((q | r | (s & 0x7Fu)) == 0u)
                {
                  continue;
                }

                Cpu6502 cpu = oracle.Fresh();
                Elite::MathWorkspace math;

                cpu.memory[pp] = p;
                cpu.memory[static_cast<std::uint16_t>(pp + 1)] = p1;
                cpu.memory[static_cast<std::uint16_t>(pp + 2)] = p2;
                cpu.memory[qq] = q;
                cpu.memory[rr] = r;
                cpu.memory[ss] = s;
                math.p = p;
                math.p1 = p1;
                math.p2 = p2;
                math.q = q;
                math.r = r;
                math.s = s;

                const Elite::Testing::RunResult run = cpu.CallSubroutine(dvid3b);
                Assert::IsTrue(run.completed, L"DVID3B returned");

                Elite::DivideSignedToK(math);

                const std::wstring where =
                  Widen("DVID3B(P=" + std::to_string(p) + "/" + std::to_string(p1) + "/"
                        + std::to_string(p2) + ", Q=" + std::to_string(q) + ", R="
                        + std::to_string(r) + ", S=" + std::to_string(s) + ")");
                for (int byte = 0; byte < 4; ++byte)
                {
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(kk + byte)], math.k[byte],
                                   (where + L": K+" + std::to_wstring(byte)).c_str());
                }

                // The scratch bytes too. Nothing downstream reads them today, but they are what
                // a later routine reaching this through a different entry point would see, and a
                // divergence here is a divergence in the loop counts above it.
                Assert::AreEqual(cpu.memory[tt], math.t, (where + L": T").c_str());
                Assert::AreEqual(cpu.memory[qq], math.q, (where + L": Q").c_str());
                Assert::AreEqual(cpu.memory[rr], math.r, (where + L": R").c_str());
                Assert::AreEqual(cpu.memory[pp], math.p, (where + L": P").c_str());
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(pp + 1)], math.p1,
                                 (where + L": P+1").c_str());
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(pp + 2)], math.p2,
                                 (where + L": P+2").c_str());

                /*
                 * Which of the three tails ran, decided from the answer rather than from the
                 * port's internals -- and it is decidable, as long as the quotient in R is not
                 * zero. `DV13` stores R unchanged. `DVL8` shifts it LEFT at least once, so
                 * either something reaches the top three bytes or the low byte grew. `DVL10`
                 * shifts it RIGHT at least once, so the low byte shrank and nothing can reach
                 * the top three. A zero quotient looks like all three and is counted apart.
                 */
                if (math.r == 0u)
                {
                  ++unclassified;
                }
                else if ((math.k[1] | math.k[2] | (math.k[3] & 0x7Fu)) != 0u || math.k[0] > math.r)
                {
                  ++scaledUp;
                }
                else if (math.k[0] == math.r)
                {
                  ++unscaled;
                }
                else
                {
                  ++scaledDown;
                }
                ++compared;
              }
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(4u * 4u * 8u * 8u * 8u * 8u - 4u * 4u * 8u * 2u, compared,
                                    L"the whole sweep ran, less the denominators that cannot terminate");
    Assert::IsTrue(scaledUp > 0u, L"the shift-left tail ran");
    Assert::IsTrue(scaledDown > 0u, L"the shift-right tail ran");
    Assert::IsTrue(unscaled > 0u, L"the unscaled tail ran");
  }

  /// 6502: DVID3B2 -- the same divide with the denominator taken from a ship's z, including the
  /// `ORA #1` that is the only reason a ship at z = 0 does not hang the game.
  TEST_METHOD(DividingByShipZMatchesDVID3B2)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t pp = oracle.Label("P");
    const std::uint16_t kk = oracle.Label("K");
    const std::uint16_t dvid3b2 = oracle.Label("DVID3B2");

    std::uint32_t compared = 0;
    std::uint32_t atZeroDistance = 0;

    for (const std::uint8_t a : EDGES)
    {
      for (const std::uint8_t p : LOW)
      {
        for (const std::uint8_t p1 : LOW)
        {
          for (const std::uint8_t zLow : EDGES)
          {
            for (const std::uint8_t zHigh : EDGES)
            {
              for (const std::uint8_t zSign : EDGES)
              {
                // No denominator is skipped here, and that is the point of the sweep: the
                // `ORA #1` makes every one of them terminate, z = 0 included.
                Cpu6502 cpu = oracle.Fresh();
                Elite::ShipBlock ship;
                Elite::MathWorkspace math;

                const std::uint8_t z[3] = { zLow, zHigh, zSign };
                for (int byte = 0; byte < 3; ++byte)
                {
                  cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_Z_OFFSET + byte)] = z[byte];
                  ship[Elite::SHIP_Z_OFFSET + byte] = z[byte];
                }
                cpu.memory[pp] = p;
                cpu.memory[static_cast<std::uint16_t>(pp + 1)] = p1;
                math.p = p;
                math.p1 = p1;

                cpu.a = a;
                const Elite::Testing::RunResult run = cpu.CallSubroutine(dvid3b2);
                Assert::IsTrue(run.completed, L"DVID3B2 returned");

                Elite::DivideByShipZ(ship, math, a);

                const std::wstring where =
                  Widen("DVID3B2(a=" + std::to_string(a) + ", P=" + std::to_string(p) + "/"
                        + std::to_string(p1) + ", z=" + std::to_string(zLow) + "/"
                        + std::to_string(zHigh) + "/" + std::to_string(zSign) + ")");
                for (int byte = 0; byte < 4; ++byte)
                {
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(kk + byte)], math.k[byte],
                                   (where + L": K+" + std::to_wstring(byte)).c_str());
                }

                if (zLow == 0u && zHigh == 0u && (zSign & 0x7Fu) == 0u)
                {
                  ++atZeroDistance;
                }
                ++compared;
              }
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(8u * 4u * 4u * 8u * 8u * 8u, compared, L"the whole sweep ran");
    Assert::IsTrue(atZeroDistance > 0u, L"a ship at z = 0 was compared");
  }
};

TEST_CLASS(TheScreenProjection)
{
public:
  /*
   * 6502: PLS6 -- (X K) = (A P+1 P) / z, with an overflow at 1024.
   *
   * Four exits, and the test insists all four were taken: the two high bytes non-zero (`PL21`),
   * the high byte at four or more, a positive result, and a negative one that gets negated into
   * two's complement. The last is the only path that writes K back, and the only one where A and
   * X come out as anything but zero.
   */
  TEST_METHOD(TheScreenOffsetMatchesPLS6)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t pp = oracle.Label("P");
    const std::uint16_t kk = oracle.Label("K");
    const std::uint16_t pls6 = oracle.Label("PLS6");

    std::uint32_t compared = 0;
    std::uint32_t wayOff = 0;
    std::uint32_t overflowed = 0;
    std::uint32_t positive = 0;
    std::uint32_t negated = 0;

    for (const std::uint8_t a : EDGES)
    {
      for (const std::uint8_t p : LOW)
      {
        for (const std::uint8_t p1 : LOW)
        {
          for (const std::uint8_t zLow : EDGES)
          {
            for (const std::uint8_t zHigh : EDGES)
            {
              for (const std::uint8_t zSign : EDGES)
              {
                Cpu6502 cpu = oracle.Fresh();
                Elite::ShipBlock ship;
                Elite::MathWorkspace math;

                const std::uint8_t z[3] = { zLow, zHigh, zSign };
                for (int byte = 0; byte < 3; ++byte)
                {
                  cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_Z_OFFSET + byte)] = z[byte];
                  ship[Elite::SHIP_Z_OFFSET + byte] = z[byte];
                }
                cpu.memory[pp] = p;
                cpu.memory[static_cast<std::uint16_t>(pp + 1)] = p1;
                math.p = p;
                math.p1 = p1;

                cpu.a = a;
                const Elite::Testing::RunResult run = cpu.CallSubroutine(pls6);
                Assert::IsTrue(run.completed, L"PLS6 returned");

                const Elite::ScreenOffset offset = Elite::DivideToScreenOffset(ship, math, a);

                const std::wstring where =
                  Widen("PLS6(a=" + std::to_string(a) + ", P=" + std::to_string(p) + "/"
                        + std::to_string(p1) + ", z=" + std::to_string(zLow) + "/"
                        + std::to_string(zHigh) + "/" + std::to_string(zSign) + ")");

                Assert::AreEqual(cpu.c, offset.overflow, (where + L": C").c_str());
                Assert::AreEqual(cpu.a, offset.a, (where + L": A").c_str());
                Assert::AreEqual(cpu.memory[kk], offset.low, (where + L": K").c_str());

                // X is only part of the contract when the point fits. On `PL21` the original
                // never loads it, so what it holds is the caller's -- there is nothing to
                // compare, and comparing it anyway would be asserting the test harness's own
                // register state.
                if (!offset.overflow)
                {
                  Assert::AreEqual(cpu.x, offset.high, (where + L": X").c_str());
                }

                if (offset.overflow)
                {
                  if (offset.a != 0u)
                  {
                    ++wayOff;
                  }
                  else
                  {
                    ++overflowed;
                  }
                }
                else if (offset.a == 0u && offset.high < 4u)
                {
                  ++positive;
                }
                else
                {
                  ++negated;
                }
                ++compared;
              }
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(8u * 4u * 4u * 8u * 8u * 8u, compared, L"the whole sweep ran");
    Assert::IsTrue(wayOff > 0u, L"PL21 was reached");
    Assert::IsTrue(overflowed > 0u, L"the high byte reached four");
    Assert::IsTrue(positive > 0u, L"a positive result was returned");
    Assert::IsTrue(negated > 0u, L"a negative result was negated");
  }

  /*
   * 6502: PROJ -- x / z and -y / z, offset to the centre of the space view.
   *
   * The interesting case is not either coordinate on its own but the one where x projects and y
   * does not: K3 has been written and K4 has not, and the routine returns leaving them that way.
   * The test pre-fills both with a sentinel so a port that computed both and stored at the end
   * would be caught, and counts the case to prove the sweep produced it.
   */
  TEST_METHOD(ProjectingAShipMatchesPROJ)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t k3 = oracle.Label("K3");
    const std::uint16_t k4 = oracle.Label("K4");
    const std::uint16_t proj = oracle.Label("PROJ");

    const std::vector<std::uint8_t> LOWS = { 0, 0xFF };
    const std::vector<std::uint8_t> HIGHS = { 0, 3, 4, 0xFF };
    const std::vector<std::uint8_t> SIGNS = { 0, 0x80 };
    const std::vector<std::uint8_t> Z_HIGHS = { 0, 1, 64 };

    std::uint32_t compared = 0;
    std::uint32_t projected = 0;
    std::uint32_t lostOnX = 0;
    std::uint32_t lostOnY = 0;

    for (const std::uint8_t xLow : LOWS)
    {
      for (const std::uint8_t xHigh : HIGHS)
      {
        for (const std::uint8_t xSign : SIGNS)
        {
          for (const std::uint8_t yHigh : HIGHS)
          {
            for (const std::uint8_t ySign : SIGNS)
            {
              for (const std::uint8_t zLow : LOWS)
              {
                for (const std::uint8_t zHigh : Z_HIGHS)
                {
                  for (const std::uint8_t zSign : SIGNS)
                  {
                    Cpu6502 cpu = oracle.Fresh();
                    Elite::ShipBlock ship;
                    Elite::MathWorkspace math;
                    Elite::Projection screen;

                    const std::uint8_t block[9] = { xLow,  xHigh, xSign, xLow,  yHigh,
                                                    ySign, zLow,  zHigh, zSign };
                    for (int byte = 0; byte < 9; ++byte)
                    {
                      cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = block[byte];
                      ship[static_cast<std::size_t>(byte)] = block[byte];
                    }

                    // The sentinel that makes a half-written answer visible.
                    cpu.memory[k3] = 0xAA;
                    cpu.memory[static_cast<std::uint16_t>(k3 + 1)] = 0xBB;
                    cpu.memory[k4] = 0xCC;
                    cpu.memory[static_cast<std::uint16_t>(k4 + 1)] = 0xDD;
                    screen.x = 0xAA;
                    screen.x1 = 0xBB;
                    screen.y = 0xCC;
                    screen.y1 = 0xDD;

                    const Elite::Testing::RunResult run = cpu.CallSubroutine(proj);
                    Assert::IsTrue(run.completed, L"PROJ returned");

                    const Elite::ProjectResult result = Elite::Project(ship, math, screen);

                    const std::wstring where =
                      Widen("PROJ(x=" + std::to_string(xLow) + "/" + std::to_string(xHigh) + "/"
                            + std::to_string(xSign) + ", y=" + std::to_string(xLow) + "/"
                            + std::to_string(yHigh) + "/" + std::to_string(ySign)
                            + ", z=" + std::to_string(zLow) + "/" + std::to_string(zHigh) + "/"
                            + std::to_string(zSign) + ")");

                    Assert::AreEqual(cpu.c, result.offScreen, (where + L": C").c_str());
                    Assert::AreEqual(cpu.a, result.a, (where + L": A").c_str());
                    Assert::AreEqual(cpu.memory[k3], screen.x, (where + L": K3").c_str());
                    Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(k3 + 1)], screen.x1,
                                     (where + L": K3+1").c_str());
                    Assert::AreEqual(cpu.memory[k4], screen.y, (where + L": K4").c_str());
                    Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(k4 + 1)], screen.y1,
                                     (where + L": K4+1").c_str());

                    if (!result.offScreen)
                    {
                      ++projected;
                    }
                    else if (screen.x == 0xAA && screen.x1 == 0xBB)
                    {
                      ++lostOnX;
                    }
                    else
                    {
                      ++lostOnY;
                    }
                    ++compared;
                  }
                }
              }
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(2u * 4u * 2u * 4u * 2u * 2u * 3u * 2u, compared,
                                    L"the whole sweep ran");
    Assert::IsTrue(projected > 0u, L"a ship projected onto the screen");
    Assert::IsTrue(lostOnX > 0u, L"a ship was lost on the x divide");
    Assert::IsTrue(lostOnY > 0u, L"a ship was lost on the y divide, with K3 already written");
  }
};


/*
 * The ship line heap, and the three routines that read and write it (slice 3b).
 *
 * These are the first tests in the suite where the answer is the SCREEN rather than a byte, so
 * every one of them compares the whole 10,240-byte canvas against the game's own memory. A line
 * that is right for most of its length and steps a row late at one cell boundary fails.
 */
TEST_CLASS(TheShipLineHeap)
{
public:
  /*
   * 6502: LL155 and its LL27 loop.
   *
   * The lengths are the interesting input, not the coordinates: 0 and 3 draw nothing, 4 is the
   * first that draws, and a length that is not a multiple of four leaves a partial entry at the
   * end which the original reads anyway -- the loop tests `Y < XX20` after advancing, so it will
   * happily draw a line out of three bytes and whatever follows them.
   */
  TEST_METHOD(DrawingTheHeapMatchesLL155)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t ll155 = oracle.Label("LL155");
    const std::uint16_t screenBase = ScreenBase(oracle);

    // Five lines' worth of coordinates, so a length can take any prefix of them.
    const std::vector<std::uint8_t> lines = {
      0,                    // byte 0 is the length, overwritten per case
      10,  20,  200, 20,    // a long horizontal
      0,   0,   255, 143,   // corner to corner
      64,  100, 64,  8,     // vertical
      1,   1,   2,   3,     // two pixels apart
      250, 140, 255, 143,   // hard against the bottom right
    };

    std::uint32_t cases = 0;
    for (const std::uint8_t length : { 0, 1, 3, 4, 5, 8, 9, 12, 17, 20, 21 })
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      Elite::DrawWorkspace draw;
      Elite::LineHeap heap;

      std::vector<std::uint8_t> seeded = lines;
      seeded[0] = length;
      SeedHeap(cpu, heap, seeded);

      cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_LOW_OFFSET)] = HEAP_AT & 0xFFu;
      cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_HIGH_OFFSET)] = HEAP_AT >> 8;

      const Elite::Testing::RunResult run = cpu.CallSubroutine(ll155, 500'000);
      Assert::IsTrue(run.completed, L"LL155 returned");

      Elite::DrawShipLines(canvas, draw, heap, HEAP_AT);

      CompareScreens(cpu, screenBase, canvas, L"LL155 length " + std::to_wstring(length));
      CompareHeaps(cpu, heap, L"LL155 length " + std::to_wstring(length));
      ++cases;
    }

    Assert::AreEqual<std::uint32_t>(11u, cases, L"every length ran");
  }

  /// 6502: LL81 -- the length goes in from U, and then the same drawing happens. Entered at the
  /// label rather than at `LL81+2`, so this is the path `LL9` takes.
  TEST_METHOD(StoringTheLengthMatchesLL81)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t uu = oracle.Label("U");
    const std::uint16_t ll81 = oracle.Label("LL81");
    const std::uint16_t screenBase = ScreenBase(oracle);

    const std::vector<std::uint8_t> lines = { 99, 5, 5, 60, 40, 100, 100, 104, 96, 8, 143, 250, 0 };

    std::uint32_t cases = 0;
    for (const std::uint8_t count : { 0, 3, 4, 8, 12, 13 })
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      Elite::DrawWorkspace draw;
      Elite::LineHeap heap;

      SeedHeap(cpu, heap, lines);
      cpu.memory[uu] = count;
      cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_LOW_OFFSET)] = HEAP_AT & 0xFFu;
      cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_HIGH_OFFSET)] = HEAP_AT >> 8;

      const Elite::Testing::RunResult run = cpu.CallSubroutine(ll81, 500'000);
      Assert::IsTrue(run.completed, L"LL81 returned");

      Elite::StoreLineCountAndDraw(canvas, draw, heap, HEAP_AT, count);

      CompareScreens(cpu, screenBase, canvas, L"LL81 count " + std::to_wstring(count));
      CompareHeaps(cpu, heap, L"LL81 count " + std::to_wstring(count));
      ++cases;
    }

    Assert::AreEqual<std::uint32_t>(6u, cases, L"every count ran");
  }

  /*
   * 6502: EE51 -- both halves, which is the point: with bit 3 clear it must do NOTHING, and a
   * port that redrew anyway would erase a ship that was never on the screen and leave its lines
   * behind for good.
   */
  TEST_METHOD(ErasingAShipMatchesEE51)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t ee51 = oracle.Label("EE51");
    const std::uint16_t screenBase = ScreenBase(oracle);

    const std::vector<std::uint8_t> lines = { 12, 30, 30, 90, 30, 90, 30, 90, 90, 30, 90, 30, 30 };

    std::uint32_t cases = 0;
    std::uint32_t erased = 0;
    for (const std::uint8_t state : { 0x00, 0x08, 0x07, 0x0F, 0xF7, 0xFF })
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      Elite::DrawWorkspace draw;
      Elite::LineHeap heap;
      Elite::ShipBlock ship;

      SeedHeap(cpu, heap, lines);
      cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_LOW_OFFSET)] = HEAP_AT & 0xFFu;
      cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_HIGH_OFFSET)] = HEAP_AT >> 8;
      cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_STATE_OFFSET)] = state;
      ship[Elite::SHIP_HEAP_LOW_OFFSET] = HEAP_AT & 0xFFu;
      ship[Elite::SHIP_HEAP_HIGH_OFFSET] = HEAP_AT >> 8;
      ship[Elite::SHIP_STATE_OFFSET] = state;

      const Elite::Testing::RunResult run = cpu.CallSubroutine(ee51, 500'000);
      Assert::IsTrue(run.completed, L"EE51 returned");

      Elite::EraseShip(canvas, draw, ship, heap);

      const std::wstring where = L"EE51 state " + std::to_wstring(state);
      CompareScreens(cpu, screenBase, canvas, where);
      Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_STATE_OFFSET)],
                       ship[Elite::SHIP_STATE_OFFSET], (where + L": INWK+31").c_str());
      erased += ((state & 0x08u) != 0u) ? 1u : 0u;
      ++cases;
    }

    Assert::AreEqual<std::uint32_t>(6u, cases, L"every state ran");
    Assert::AreEqual<std::uint32_t>(3u, erased, L"three of the six had something to erase");
  }

  /*
   * 6502: SHPPT -- and run as a SEQUENCE rather than as six separate calls, because one of the
   * things being tested is what it does with state left over from the call before it (§6.33).
   *
   * `PROJ` can return with the carry set having already written `K3` and not `K4`, and SHPPT does
   * not look at the carry: it ORs the accumulator with `K3+1`. So a ship whose x coordinate
   * overflowed sees the PREVIOUS ship's high byte, and whether it is drawn depends on what was
   * projected before it. A test that reset the machine between positions would agree with the
   * game on every case and still miss that entirely.
   */
  TEST_METHOD(DrawingADistantShipMatchesSHPPT)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t shppt = oracle.Label("SHPPT");
    const std::uint16_t k3 = oracle.Label("K3");
    const std::uint16_t k4 = oracle.Label("K4");
    const std::uint16_t screenBase = ScreenBase(oracle);

    // x_lo, x_hi, x_sign, y_lo, y_hi, y_sign, z_lo, z_hi, z_sign -- and then the state byte.
    struct Position
    {
      std::array<std::uint8_t, 9> block;
      std::uint8_t state;
      const wchar_t* what;
    };

    /*
     * The coordinates are chosen with the divide's SCALE in mind, which is the thing about this
     * chain that is easiest to get wrong from the outside: `DVID3B` returns 256 times the ratio,
     * not the ratio, so an offset of one pixel is x/z = 1/256 and a ship at z = 1 is off the
     * screen whatever its x is. The first draft of this test used z = 1 throughout and every
     * single position came out rejected -- agreeing with the game, and testing one branch.
     */
    const std::vector<Position> POSITIONS = {
      { { 0, 0, 0, 0, 0, 0, 0, 4, 0 }, 0x00, L"dead centre, a quarter of the way out" },
      { { 0, 0, 0, 0, 0, 0, 0, 4, 0 }, 0x08, L"the same place, and now already drawn" },
      { { 0, 1, 0, 0, 0, 0, 0, 4, 0 }, 0x08, L"64 pixels right" },
      { { 0, 1, 0x80, 0, 1, 0, 0, 4, 0 }, 0x08, L"left and up" },
      { { 0, 1, 0, 0, 1, 0x80, 0, 4, 0 }, 0x08, L"right and down" },
      { { 0xF4, 0x01, 0, 0, 0, 0, 0, 4, 0 }, 0x08, L"x + 3 carries out of the byte" },
      { { 0, 0, 0, 0x18, 0x01, 0x80, 0, 4, 0 }, 0x08, L"y on the dashboard's first row" },
      { { 0x88, 0x13, 0, 0, 0, 0, 0, 4, 0 }, 0x08, L"x overflows the high-byte test" },
      { { 0, 1, 0, 0x88, 0x13, 0, 0, 4, 0 }, 0x08, L"y overflows AFTER K3 has been stored" },
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 0x08, L"z is zero, so the ORA #1 decides it" },
      { { 0, 1, 0, 0, 1, 0, 0, 4, 0x80 }, 0x08, L"behind the player" },
      { { 0, 0x10, 0, 0, 0x08, 0, 0, 0x40, 0 }, 0x08, L"a long way off, and back on screen" },
      { { 0, 0, 0, 0, 0, 0, 0, 4, 0 }, 0x08, L"centre once more, after the stale ones" },
    };

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    Elite::DrawWorkspace draw;
    Elite::LineHeap heap;
    Elite::MathWorkspace math;
    Elite::Projection screen;
    Elite::ShipBlock ship;

    SeedHeap(cpu, heap, {});
    cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_LOW_OFFSET)] = HEAP_AT & 0xFFu;
    cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_HEAP_HIGH_OFFSET)] = HEAP_AT >> 8;
    ship[Elite::SHIP_HEAP_LOW_OFFSET] = HEAP_AT & 0xFFu;
    ship[Elite::SHIP_HEAP_HIGH_OFFSET] = HEAP_AT >> 8;

    // K3 and K4 start where the machine starts them, and the port has to agree from there --
    // the stale-coordinate path reads them before anything has written them.
    screen.x = cpu.memory[k3];
    screen.x1 = cpu.memory[static_cast<std::uint16_t>(k3 + 1)];
    screen.y = cpu.memory[k4];
    screen.y1 = cpu.memory[static_cast<std::uint16_t>(k4 + 1)];

    std::uint32_t drawn = 0;
    std::uint32_t halfWritten = 0;
    for (const Position& position : POSITIONS)
    {
      const Elite::Projection before = screen;

      for (std::uint8_t byte = 0; byte < 9u; ++byte)
      {
        cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = position.block[byte];
        ship[byte] = position.block[byte];
      }
      cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_STATE_OFFSET)] = position.state;
      ship[Elite::SHIP_STATE_OFFSET] = position.state;

      const Elite::Testing::RunResult run = cpu.CallSubroutine(shppt, 500'000);
      Assert::IsTrue(run.completed, L"SHPPT returned");

      Elite::DrawShipAsPoint(canvas, draw, ship, heap, math, screen);

      const std::wstring where = std::wstring(L"SHPPT: ") + position.what;
      CompareScreens(cpu, screenBase, canvas, where);
      CompareHeaps(cpu, heap, where);
      Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + Elite::SHIP_STATE_OFFSET)],
                       ship[Elite::SHIP_STATE_OFFSET], (where + L": INWK+31").c_str());
      Assert::AreEqual(cpu.memory[k3], screen.x, (where + L": K3").c_str());
      Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(k3 + 1)], screen.x1,
                       (where + L": K3+1").c_str());
      Assert::AreEqual(cpu.memory[k4], screen.y, (where + L": K4").c_str());
      Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(k4 + 1)], screen.y1,
                       (where + L": K4+1").c_str());

      const bool nowDrawn = (ship[Elite::SHIP_STATE_OFFSET] & Elite::SHIP_STATE_DRAWN) != 0u;
      drawn += nowDrawn ? 1u : 0u;

      // The case the sequence exists for: `PROJ` stored K3, then gave up on K4, so the ship is
      // refused while carrying half a new position. Whatever comes next reads the other half.
      if (!nowDrawn && (screen.x != before.x || screen.x1 != before.x1) && screen.y == before.y
          && screen.y1 == before.y1)
      {
        ++halfWritten;
      }
    }

    Assert::IsTrue(drawn > 0u, L"some of the sequence was drawn");
    Assert::IsTrue(drawn < POSITIONS.size(), L"and some of it was refused");
    Assert::IsTrue(halfWritten > 0u, L"and at least one was refused with K3 written and K4 not");
  }
};


TEST_CLASS(TheGeometryDotProducts)
{
public:
  /*
   * 6502: LL51 -- XX12(5 0) = XX15(5 0) . each of XX16's three vectors.
   *
   * Twenty-four input bytes, so this is swept rather than exhausted: a fixed generator walks the
   * space, and a block of hand-picked cases pins the boundaries -- all zero, all 255, every
   * combination of the three signs, and magnitudes either side of 128.
   *
   * The thing most likely to be got wrong is `S`. It is set from the FIRST product's sign and
   * then `LL38` flips it whenever a subtraction crosses zero, so what comes out is the sign of
   * the whole sum. A port that computed each sign independently would agree wherever the terms
   * happen to point the same way, which is most of the space; the test counts the flips.
   */
  TEST_METHOD(TheDotProductsMatchLL51)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t xx15 = oracle.Label("XX15");
    const std::uint16_t xx16 = oracle.Label("XX16");
    const std::uint16_t xx12 = oracle.Label("XX12");
    const std::uint16_t ll51 = oracle.Label("LL51");

    // The hand-picked block first, then the generated sweep. The generator is an LCG so the
    // sequence is the same on both runners and on every run.
    std::vector<std::array<std::uint8_t, 24>> cases;

    const std::vector<std::uint8_t> CORNERS = { 0, 1, 127, 128, 129, 255 };
    for (const std::uint8_t value : CORNERS)
    {
      for (const std::uint8_t sign : { 0x00, 0x80 })
      {
        std::array<std::uint8_t, 24> block{};
        for (std::size_t byte = 0; byte < 24u; ++byte)
        {
          block[byte] = ((byte & 1u) != 0u) ? sign : value;
        }
        cases.push_back(block);
      }
    }

    std::uint32_t seed = 0x1D872B41u;
    for (std::uint32_t sample = 0; sample < 400u; ++sample)
    {
      std::array<std::uint8_t, 24> block{};
      for (std::size_t byte = 0; byte < 24u; ++byte)
      {
        seed = seed * 1664525u + 1013904223u;
        block[byte] = static_cast<std::uint8_t>(seed >> 24);
      }
      cases.push_back(block);
    }

    std::uint32_t compared = 0;
    std::uint32_t flipped = 0;
    for (const std::array<std::uint8_t, 24>& block : cases)
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::DrawWorkspace draw;
      Elite::GeometryWorkspace geometry;
      Elite::MathWorkspace math;

      // XX15 is the first six; XX16 is the eighteen after it.
      const std::uint8_t* const vector = block.data();
      draw.x1 = vector[0];
      draw.y1 = vector[1];
      draw.x2 = vector[2];
      draw.y2 = vector[3];
      draw.xx15Plus4 = vector[4];
      draw.xx15Plus5 = vector[5];
      for (std::size_t byte = 0; byte < 6u; ++byte)
      {
        cpu.memory[static_cast<std::uint16_t>(xx15 + byte)] = vector[byte];
      }
      for (std::size_t byte = 0; byte < 18u; ++byte)
      {
        geometry.xx16[byte] = block[6u + byte];
        cpu.memory[static_cast<std::uint16_t>(xx16 + byte)] = block[6u + byte];
      }

      const Elite::Testing::RunResult run = cpu.CallSubroutine(ll51, 200'000);
      Assert::IsTrue(run.completed, L"LL51 returned");

      Elite::DotProducts(draw, geometry, math);

      const std::wstring where = Widen("LL51 case " + std::to_string(compared));
      for (std::size_t byte = 0; byte < 6u; ++byte)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(xx12 + byte)], geometry.xx12[byte],
                         (where + L": XX12+" + std::to_wstring(byte)).c_str());
      }

      // Did the accumulated sign end up different from the first product's? That is `LL40`
      // having run, and it is the only way this routine's answer can change sign.
      for (std::size_t product = 0; product < 3u; ++product)
      {
        const std::size_t base = product * 6u;
        const std::uint8_t firstSign =
          static_cast<std::uint8_t>(vector[1] ^ block[6u + base + 1u]) & 0x80u;
        if ((geometry.xx12[product * 2u + 1u] & 0x80u) != firstSign)
        {
          ++flipped;
        }
      }
      ++compared;
    }

    Assert::AreEqual<std::uint32_t>(412u, compared, L"the whole sweep ran");
    Assert::IsTrue(flipped > 0u, L"a sum crossed zero and took the sign with it");
  }
};


TEST_CLASS(TheSlopeArithmetic)
{
public:
  /*
   * 6502: LL129, LL120 and LL123 -- the step the line clipper walks a point along a line with.
   *
   * All three are swept together over the same space, because they are the same code: `LL120`
   * and `LL123` differ only in which of the multiply and the divide `T` selects and in whether
   * `R` comes from `x1_lo`, and both run through `LL129` first.
   *
   * X and Y are compared as well as the workspace, because that is where the answer is -- these
   * return in registers and the caller uses both.
   */
  TEST_METHOD(TheSlopeStepsMatchLL120AndLL123)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t xx15 = oracle.Label("XX15");
    const std::uint16_t xx12 = oracle.Label("XX12");
    const std::uint16_t qq = oracle.Label("Q");
    const std::uint16_t rr = oracle.Label("R");
    const std::uint16_t ss = oracle.Label("S");
    const std::uint16_t tt = oracle.Label("T");
    const std::uint16_t ll129 = oracle.Label("LL129");
    const std::uint16_t ll120 = oracle.Label("LL120");
    const std::uint16_t ll123 = oracle.Label("LL123");

    std::uint32_t compared = 0;
    std::uint32_t negated = 0;
    std::uint32_t multiplied = 0;
    std::uint32_t divided = 0;

    for (const std::uint8_t gradient : EDGES)
    {
      for (const std::uint8_t direction : { 0x00, 0x80 })
      {
        for (const std::uint8_t steep : { 0x00, 0xFF })
        {
          for (const std::uint8_t r : EDGES)
          {
            for (const std::uint8_t s : EDGES)
            {
              for (const std::uint8_t x1 : LOW)
              {
                // Each of the three routines gets the same starting state.
                for (int which = 0; which < 3; ++which)
                {
                  Cpu6502 cpu = oracle.Fresh();
                  Elite::DrawWorkspace draw;
                  Elite::GeometryWorkspace geometry;
                  Elite::MathWorkspace math;

                  cpu.memory[static_cast<std::uint16_t>(xx12 + 2)] = gradient;
                  cpu.memory[static_cast<std::uint16_t>(xx12 + 3)] = direction;
                  cpu.memory[tt] = steep;
                  cpu.memory[rr] = r;
                  cpu.memory[ss] = s;
                  cpu.memory[xx15] = x1;
                  geometry.xx12[2] = gradient;
                  geometry.xx12[3] = direction;
                  math.t = steep;
                  math.r = r;
                  math.s = s;
                  draw.x1 = x1;

                  const std::uint16_t routine =
                    (which == 0) ? ll129 : ((which == 1) ? ll120 : ll123);
                  const Elite::Testing::RunResult run = cpu.CallSubroutine(routine, 200'000);
                  Assert::IsTrue(run.completed, L"the slope routine returned");

                  const std::wstring where =
                    Widen(std::string(which == 0 ? "LL129" : (which == 1 ? "LL120" : "LL123"))
                          + "(XX12+2=" + std::to_string(gradient) + ", XX12+3="
                          + std::to_string(direction) + ", T=" + std::to_string(steep) + ", R="
                          + std::to_string(r) + ", S=" + std::to_string(s) + ", x1="
                          + std::to_string(x1) + ")");

                  if (which == 0)
                  {
                    const std::uint8_t sign = Elite::PrepareSlope(math, geometry);
                    Assert::AreEqual(cpu.a, sign, (where + L": A").c_str());
                  }
                  else
                  {
                    const Elite::SlopeStep step = (which == 1)
                                                    ? Elite::StepAlongX(math, geometry, draw)
                                                    : Elite::StepAlongY(math, geometry);
                    Assert::AreEqual(cpu.x, step.low, (where + L": X").c_str());
                    Assert::AreEqual(cpu.y, step.high, (where + L": Y").c_str());

                    if (step.low != 0u || step.high != 0u)
                    {
                      const bool wentNegative = (step.high & 0x80u) != 0u;
                      negated += wentNegative ? 1u : 0u;
                    }
                    const bool isMultiply = (which == 1) ? (steep == 0u) : (steep != 0u);
                    multiplied += isMultiply ? 1u : 0u;
                    divided += isMultiply ? 0u : 1u;
                  }

                  Assert::AreEqual(cpu.memory[qq], math.q, (where + L": Q").c_str());
                  Assert::AreEqual(cpu.memory[rr], math.r, (where + L": R").c_str());
                  Assert::AreEqual(cpu.memory[ss], math.s, (where + L": S").c_str());
                  ++compared;
                }
              }
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(8u * 2u * 2u * 8u * 8u * 4u * 3u, compared,
                                    L"the whole sweep ran");
    Assert::IsTrue(multiplied > 0u, L"the multiply loop ran");
    Assert::IsTrue(divided > 0u, L"the divide loop ran");
    Assert::IsTrue(negated > 0u, L"a step came out negative");
  }

  /*
   * 6502: LL118 -- the four clamps, one per screen edge.
   *
   * The sweep is chosen so that every clamp fires and so that pairs of them fire together: a
   * point can be off the left edge AND above the screen, and the second clamp then works on the
   * coordinate the first one moved. The counters below insist each of the four ran and that at
   * least one point needed two of them.
   */
  TEST_METHOD(MovingAPointOnScreenMatchesLL118)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t xx15 = oracle.Label("XX15");
    const std::uint16_t xx12 = oracle.Label("XX12");
    const std::uint16_t qq = oracle.Label("Q");
    const std::uint16_t rr = oracle.Label("R");
    const std::uint16_t ss = oracle.Label("S");
    const std::uint16_t tt = oracle.Label("T");
    const std::uint16_t ll118 = oracle.Label("LL118");

    const std::vector<std::uint8_t> HIGHS = { 0, 1, 0x80, 0xFF };
    const std::vector<std::uint8_t> LOWS = { 0, 143, 144, 255 };
    const std::vector<std::uint8_t> GRADIENTS = { 1, 64, 128, 255 };

    std::uint32_t compared = 0;
    std::uint32_t offLeft = 0;
    std::uint32_t offRight = 0;
    std::uint32_t above = 0;
    std::uint32_t below = 0;
    std::uint32_t twoClamps = 0;

    for (const std::uint8_t x1Low : LOWS)
    {
      for (const std::uint8_t x1High : HIGHS)
      {
        for (const std::uint8_t y1Low : LOWS)
        {
          for (const std::uint8_t y1High : HIGHS)
          {
            for (const std::uint8_t gradient : GRADIENTS)
            {
              for (const std::uint8_t direction : { 0x00, 0x80 })
              {
                for (const std::uint8_t steep : { 0x00, 0xFF })
                {
                  Cpu6502 cpu = oracle.Fresh();
                  Elite::DrawWorkspace draw;
                  Elite::GeometryWorkspace geometry;
                  Elite::MathWorkspace math;

                  const std::uint8_t point[4] = { x1Low, x1High, y1Low, y1High };
                  for (std::size_t byte = 0; byte < 4u; ++byte)
                  {
                    cpu.memory[static_cast<std::uint16_t>(xx15 + byte)] = point[byte];
                  }
                  draw.x1 = x1Low;
                  draw.y1 = x1High;
                  draw.x2 = y1Low;
                  draw.y2 = y1High;

                  cpu.memory[static_cast<std::uint16_t>(xx12 + 2)] = gradient;
                  cpu.memory[static_cast<std::uint16_t>(xx12 + 3)] = direction;
                  cpu.memory[tt] = steep;
                  geometry.xx12[2] = gradient;
                  geometry.xx12[3] = direction;
                  math.t = steep;

                  const Elite::Testing::RunResult run = cpu.CallSubroutine(ll118, 200'000);
                  Assert::IsTrue(run.completed, L"LL118 returned");

                  Elite::MovePointOnScreen(draw, geometry, math);

                  const std::wstring where =
                    Widen("LL118(x1=" + std::to_string(x1Low) + "/" + std::to_string(x1High)
                          + ", y1=" + std::to_string(y1Low) + "/" + std::to_string(y1High)
                          + ", grad=" + std::to_string(gradient) + ", dir="
                          + std::to_string(direction) + ", T=" + std::to_string(steep) + ")");

                  const std::uint8_t ours[4] = { draw.x1, draw.y1, draw.x2, draw.y2 };
                  for (std::size_t byte = 0; byte < 4u; ++byte)
                  {
                    Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(xx15 + byte)], ours[byte],
                                     (where + L": XX15+" + std::to_wstring(byte)).c_str());
                  }
                  Assert::AreEqual(cpu.memory[qq], math.q, (where + L": Q").c_str());
                  Assert::AreEqual(cpu.memory[rr], math.r, (where + L": R").c_str());
                  Assert::AreEqual(cpu.memory[ss], math.s, (where + L": S").c_str());

                  // Which clamps the inputs asked for, decided from the inputs and not from the
                  // port, so a port that skipped one still counts as having been asked.
                  const bool left = (x1High & 0x80u) != 0u;
                  const bool right = !left && x1High != 0u;
                  const bool over = (y1High & 0x80u) != 0u;
                  const bool under = !over && (y1High != 0u || y1Low >= 144u);
                  offLeft += left ? 1u : 0u;
                  offRight += right ? 1u : 0u;
                  above += over ? 1u : 0u;
                  below += under ? 1u : 0u;
                  twoClamps += ((left || right) && (over || under)) ? 1u : 0u;
                  ++compared;
                }
              }
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(4u * 4u * 4u * 4u * 4u * 2u * 2u, compared,
                                    L"the whole sweep ran");
    Assert::IsTrue(offLeft > 0u, L"a point was off the left edge");
    Assert::IsTrue(offRight > 0u, L"a point was off the right edge");
    Assert::IsTrue(above > 0u, L"a point was above the screen");
    Assert::IsTrue(below > 0u, L"a point was below the screen");
    Assert::IsTrue(twoClamps > 0u, L"a point needed clamping on both axes");
  }
};


TEST_CLASS(TheLineClipper)
{
public:
  /*
   * 6502: LL145 and LL147.
   *
   * Both entry points over the same 4,096 lines, and `SWAP` is seeded non-zero for the `LL147`
   * runs so that the one difference between them -- that `LL147` does not clear it -- is what
   * the comparison actually tests rather than something the sweep happens not to reach.
   *
   * The coordinates are chosen to sit on the boundaries rather than to be plausible: 143 and 144
   * either side of the bottom edge, 255 and 256 either side of the right, and negatives so that
   * the sign tests at `LL83` fire. Every one of the six outcomes is counted.
   */
  TEST_METHOD(ClippingALineMatchesLL145AndLL147)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t xx15 = oracle.Label("XX15");
    const std::uint16_t xx12 = oracle.Label("XX12");
    const std::uint16_t xx13 = oracle.Label("XX13");
    const std::uint16_t swap = oracle.Label("SWAP");
    const std::uint16_t dontclip = oracle.Label("dontclip");
    const std::uint16_t qq = oracle.Label("Q");
    const std::uint16_t rr = oracle.Label("R");
    const std::uint16_t ss = oracle.Label("S");
    const std::uint16_t tt = oracle.Label("T");
    const std::uint16_t ll145 = oracle.Label("LL145");
    const std::uint16_t ll147 = oracle.Label("LL147");

    // Sixteen-bit coordinates either side of every edge the clipper knows about.
    const std::vector<std::uint16_t> COORDINATES = { 0x0000, 0x0001, 0x008F, 0x0090,
                                                     0x00FF, 0x0100, 0xFF80, 0xFFFF };

    std::uint32_t compared = 0;
    std::uint32_t fitted = 0;
    std::uint32_t rejected = 0;
    std::uint32_t swapped = 0;
    std::uint32_t untouched = 0;
    std::uint32_t unclipped = 0;

    for (const std::uint16_t x1 : COORDINATES)
    {
      for (const std::uint16_t y1 : COORDINATES)
      {
        for (const std::uint16_t x2 : COORDINATES)
        {
          for (const std::uint16_t y2 : COORDINATES)
          {
            for (int entry = 0; entry < 2; ++entry)
            {
              /*
               * Every sixteenth line is run with clipping switched off, which is what the
               * short-range chart does through `dontclip` -- and every eighth with the flag set
               * to 1 instead, which is NOT something the game does.
               *
               * `TT23` only ever writes 199 and `RES2` only ever writes 0, so a port that tested
               * the whole byte instead of its bit 7 would agree with the game everywhere. The
               * original is `BIT dontclip / BMI`, so the contract is the bit; 1 is in the sweep
               * to hold the port to the contract rather than to the two values it will see.
               */
              const std::uint8_t off = ((compared % 16u) == 0u)   ? std::uint8_t{ 199 }
                                       : ((compared % 8u) == 0u) ? std::uint8_t{ 1 }
                                                                 : std::uint8_t{ 0 };
              const std::uint8_t seededSwap = (entry == 0) ? std::uint8_t{ 0 } : std::uint8_t{ 3 };

              Cpu6502 cpu = oracle.Fresh();
              Elite::DrawWorkspace draw;
              Elite::GeometryWorkspace geometry;
              Elite::MathWorkspace math;
              Elite::ClipState clip;

              const std::uint8_t block[6] = {
                static_cast<std::uint8_t>(x1), static_cast<std::uint8_t>(x1 >> 8),
                static_cast<std::uint8_t>(y1), static_cast<std::uint8_t>(y1 >> 8),
                static_cast<std::uint8_t>(x2), static_cast<std::uint8_t>(x2 >> 8),
              };
              for (std::size_t byte = 0; byte < 6u; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(xx15 + byte)] = block[byte];
              }
              draw.x1 = block[0];
              draw.y1 = block[1];
              draw.x2 = block[2];
              draw.y2 = block[3];
              draw.xx15Plus4 = block[4];
              draw.xx15Plus5 = block[5];

              cpu.memory[xx12] = static_cast<std::uint8_t>(y2);
              cpu.memory[static_cast<std::uint16_t>(xx12 + 1)] = static_cast<std::uint8_t>(y2 >> 8);
              geometry.xx12[0] = static_cast<std::uint8_t>(y2);
              geometry.xx12[1] = static_cast<std::uint8_t>(y2 >> 8);

              cpu.memory[dontclip] = off;
              cpu.memory[swap] = seededSwap;
              clip.dontclip = off;
              clip.swap = seededSwap;

              // LL147 is entered with XX15+5 in the accumulator, which is where its only caller
              // leaves it.
              cpu.a = block[5];
              const Elite::Testing::RunResult run =
                cpu.CallSubroutine((entry == 0) ? ll145 : ll147, 400'000);
              Assert::IsTrue(run.completed, L"the clipper returned");

              const bool missed = (entry == 0)
                                    ? Elite::ClipLine(draw, geometry, math, clip)
                                    : Elite::ClipLineKeepingSwap(draw, geometry, math, clip, block[5]);

              const std::wstring where =
                Widen(std::string(entry == 0 ? "LL145" : "LL147") + "(x1=" + std::to_string(x1)
                      + ", y1=" + std::to_string(y1) + ", x2=" + std::to_string(x2) + ", y2="
                      + std::to_string(y2) + ", dontclip=" + std::to_string(off) + ")");

              Assert::AreEqual(cpu.c, missed, (where + L": C").c_str());

              const std::uint8_t ours[6] = { draw.x1,  draw.y1,        draw.x2,
                                             draw.y2,  draw.xx15Plus4, draw.xx15Plus5 };
              for (std::size_t byte = 0; byte < 6u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(xx15 + byte)], ours[byte],
                                 (where + L": XX15+" + std::to_wstring(byte)).c_str());
              }
              for (std::size_t byte = 0; byte < 6u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(xx12 + byte)],
                                 geometry.xx12[byte],
                                 (where + L": XX12+" + std::to_wstring(byte)).c_str());
              }
              Assert::AreEqual(cpu.memory[xx13], clip.xx13, (where + L": XX13").c_str());
              Assert::AreEqual(cpu.memory[swap], clip.swap, (where + L": SWAP").c_str());
              Assert::AreEqual(cpu.memory[qq], math.q, (where + L": Q").c_str());
              Assert::AreEqual(cpu.memory[rr], math.r, (where + L": R").c_str());
              Assert::AreEqual(cpu.memory[ss], math.s, (where + L": S").c_str());
              Assert::AreEqual(cpu.memory[tt], math.t, (where + L": T").c_str());

              if ((off & 0x80u) != 0u)
              {
                ++unclipped;
              }
              else if (missed)
              {
                ++rejected;
              }
              else
              {
                ++fitted;
                swapped += (clip.swap != seededSwap) ? 1u : 0u;
                untouched += (clip.xx13 == 0u && clip.swap == seededSwap) ? 1u : 0u;
              }
              ++compared;
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(8u * 8u * 8u * 8u * 2u, compared, L"the whole sweep ran");
    Assert::IsTrue(unclipped > 0u, L"dontclip switched the clipper off");
    Assert::IsTrue(rejected > 0u, L"a line was refused");
    Assert::IsTrue(fitted > 0u, L"a line was clipped to fit");
    Assert::IsTrue(swapped > 0u, L"a line came back with its ends exchanged");
    Assert::IsTrue(untouched > 0u, L"a line was already on screen and needed nothing");
  }
};


TEST_CLASS(TheWideDivide)
{
public:
  /*
   * 6502: LL61 -- (U R) = 256 * A / Q for A >= Q, and its LL84 error exit.
   *
   * Swept over every A and every Q, all 65,536 pairs, because the routine's shape is entirely
   * decided by how many halvings A needs and the answer changes at every power of two. `U` is
   * seeded to a value the caller might plausibly have left there, because the routine ROTATES
   * into it rather than clearing it first -- what U held on entry is part of the answer.
   */
  TEST_METHOD(TheWideDivideMatchesLL61)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t qq = oracle.Label("Q");
    const std::uint16_t rr = oracle.Label("R");
    const std::uint16_t ss = oracle.Label("S");
    const std::uint16_t uu = oracle.Label("U");
    const std::uint16_t ll61 = oracle.Label("LL61");

    std::uint32_t compared = 0;
    std::uint32_t failed = 0;
    std::uint32_t divided = 0;

    for (std::uint32_t q = 0; q < 256u; ++q)
    {
      for (std::uint32_t a = 0; a < 256u; ++a)
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::MathWorkspace math;

        // Not zero: `ROL U` brings the old bits back up, so a cleared U would hide a port that
        // dropped the rotate and assigned instead.
        const std::uint8_t seededU = static_cast<std::uint8_t>((a * 7u + q * 13u) & 0x3Fu);

        cpu.memory[qq] = static_cast<std::uint8_t>(q);
        cpu.memory[uu] = seededU;
        math.q = static_cast<std::uint8_t>(q);
        math.u = seededU;

        cpu.a = static_cast<std::uint8_t>(a);
        const Elite::Testing::RunResult run = cpu.CallSubroutine(ll61, 100'000);
        Assert::IsTrue(run.completed, L"LL61 returned");

        Elite::DivideToUR(math, static_cast<std::uint8_t>(a));

        const std::wstring where =
          Widen("LL61(a=" + std::to_string(a) + ", Q=" + std::to_string(q) + ", U="
                + std::to_string(seededU) + ")");
        Assert::AreEqual(cpu.memory[rr], math.r, (where + L": R").c_str());
        Assert::AreEqual(cpu.memory[uu], math.u, (where + L": U").c_str());
        Assert::AreEqual(cpu.memory[ss], math.s, (where + L": S").c_str());

        if (math.r == 50u && math.u == 50u)
        {
          ++failed;
        }
        else
        {
          ++divided;
        }
        ++compared;
      }
    }

    Assert::AreEqual<std::uint32_t>(65536u, compared, L"every pair was compared");
    Assert::IsTrue(failed > 0u, L"the LL84 exit was taken");
    Assert::IsTrue(divided > 0u, L"and an answer was produced");
  }
};

} // namespace GameLogicTests

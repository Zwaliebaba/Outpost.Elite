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

} // namespace GameLogicTests

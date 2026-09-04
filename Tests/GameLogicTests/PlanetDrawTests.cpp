#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Canvas.h"
#include "PlanetDraw.h"
#include "ShipDraw.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The planet and sun line heaps (slice 3c).
 *
 * Everything in Elite is drawn by EOR, so erasing means redrawing, and redrawing means having
 * kept what was drawn. These are the two structures that keep it -- 200 half-widths for the sun
 * and a run of coordinate pairs for the planet -- and the routines that fill, walk and clear
 * them. Compared on the whole canvas as well as on the heaps, because a routine that remembers
 * the right line and draws it in the wrong place is the failure that matters.
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

struct HeapLabels
{
  std::uint16_t lso = 0, lsx2 = 0, lsy2 = 0, lsp = 0, sunx = 0, yx2m1 = 0;
  std::uint16_t yy = 0, t = 0, k = 0, k3 = 0, k4 = 0, p = 0;
  std::uint16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0, swap = 0, type = 0, dontclip = 0;
  std::uint16_t screen = 0;

  explicit HeapLabels(const OracleImage& _oracle)
  {
    lso = _oracle.Label("LSO");   lsx2 = _oracle.Label("LSX2"); lsy2 = _oracle.Label("LSY2");
    lsp = _oracle.Label("LSP");   sunx = _oracle.Label("SUNX"); yx2m1 = _oracle.Label("Yx2M1");
    yy = _oracle.Label("YY");     t = _oracle.Label("T");       k = _oracle.Label("K");
    k3 = _oracle.Label("K3");     k4 = _oracle.Label("K4");     p = _oracle.Label("P");
    x1 = _oracle.Label("X1");     y1 = _oracle.Label("Y1");
    x2 = _oracle.Label("X2");     y2 = _oracle.Label("Y2");
    swap = _oracle.Label("SWAP"); type = _oracle.Label("TYPE");
    dontclip = _oracle.Label("dontclip");

    const Cpu6502 cpu = _oracle.Fresh();
    screen = static_cast<std::uint16_t>((cpu.memory[_oracle.Label("ylookupl")]
                                         | (cpu.memory[_oracle.Label("ylookuph")] << 8))
                                        - 0x20u);
  }
};

void SeedSunHeap(Cpu6502& _cpu, Elite::PlanetSunState& _state, const HeapLabels& _at,
                 std::uint32_t _seed)
{
  std::uint32_t bits = _seed;
  for (std::size_t row = 0; row < Elite::SUN_HEAP_SIZE; ++row)
  {
    bits = bits * 1664525u + 1013904223u;
    // Two rows in five are blank, which is what a disc's top and bottom look like.
    const std::uint8_t width =
      ((bits >> 13) % 5u < 2u) ? 0u : static_cast<std::uint8_t>((bits >> 24) & 0x7Fu);
    _state.sun[row] = width;
    _cpu.memory[static_cast<std::uint16_t>(_at.lso + row)] = width;
  }
}

void SeedBallHeap(Cpu6502& _cpu, Elite::PlanetSunState& _state, const HeapLabels& _at,
                  std::uint32_t _seed, std::uint8_t _lsp)
{
  std::uint32_t bits = _seed;
  for (std::size_t at = 0; at < Elite::BALL_HEAP_SIZE; ++at)
  {
    bits = bits * 1664525u + 1013904223u;
    const std::uint8_t x = static_cast<std::uint8_t>(bits >> 24);
    bits = bits * 1664525u + 1013904223u;
    // One entry in seven is the 255 that breaks a run, so both walks are exercised.
    const std::uint8_t y =
      ((bits >> 11) % 7u == 0u) ? 0xFFu : static_cast<std::uint8_t>((bits >> 24) % 144u);

    _state.SetBallX(static_cast<std::uint8_t>(at), x);
    _state.SetBallY(static_cast<std::uint8_t>(at), y);
    _cpu.memory[static_cast<std::uint16_t>(_at.lsx2 + at)] = x;
    _cpu.memory[static_cast<std::uint16_t>(_at.lsy2 + at)] = y;
  }

  _state.lsp = _lsp;
  _cpu.memory[_at.lsp] = _lsp;
}

void CompareHeaps(const Cpu6502& _cpu, const Elite::PlanetSunState& _state, const HeapLabels& _at,
                  const std::wstring& _where)
{
  for (std::size_t row = 0; row < Elite::SUN_HEAP_SIZE; ++row)
  {
    Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.lso + row)], _state.sun[row],
                     (_where + L": LSO+" + std::to_wstring(row)).c_str());
  }
  for (std::size_t at = 0; at < Elite::BALL_HEAP_SIZE; ++at)
  {
    Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.lsx2 + at)],
                     _state.BallX(static_cast<std::uint8_t>(at)),
                     (_where + L": LSX2+" + std::to_wstring(at)).c_str());
    Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.lsy2 + at)],
                     _state.BallY(static_cast<std::uint8_t>(at)),
                     (_where + L": LSY2+" + std::to_wstring(at)).c_str());
  }
  Assert::AreEqual(_cpu.memory[_at.lsp], _state.lsp, (_where + L": LSP").c_str());
}

std::uint32_t CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas,
                             const std::wstring& _where)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();
  std::uint32_t marked = 0;
  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_where + L": the screen differs at offset " + std::to_wstring(offset)
                    + L" -- game has " + std::to_wstring(expected) + L", port has "
                    + std::to_wstring(ours[offset]))
                     .c_str());
    }
    marked += (expected != 0u) ? 1u : 0u;
  }
  return marked;
}
} // namespace


TEST_CLASS(TheSunLineHeap)
{
public:
  /*
   * 6502: EDGES -- the sun's clipper, which is a different routine from `LL145` because a
   * horizontal line has no slope to preserve.
   *
   * Swept over every centre the sixteen-bit workspace can hold at a useful granularity and every
   * half-width, on both ends, the carry AND the heap byte -- because the off-screen exit does not
   * just return, it clears the row.
   */
  TEST_METHOD(TheRowClipperMatchesEDGES)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const HeapLabels at(oracle);
    const std::uint16_t edges = oracle.Label("EDGES");

    const std::vector<std::uint16_t> CENTRES = { 0,   1,   40,  127, 128, 129, 200, 255,
                                                 256, 300, 383, 384, 500, 0xFF00u, 0xFFFFu };
    const std::vector<std::uint8_t> WIDTHS = { 0, 1, 2, 63, 64, 100, 127, 128, 200, 255 };

    std::uint32_t compared = 0;
    std::uint32_t cleared = 0;
    std::uint32_t clamped = 0;

    for (const std::uint16_t centre : CENTRES)
    {
      for (const std::uint8_t width : WIDTHS)
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::PlanetSunState state;
        Elite::MathWorkspace math;
        Elite::DrawWorkspace draw;

        const std::uint8_t row = 91;
        SeedSunHeap(cpu, state, at, 0x71C3492Bu + width);
        state.sun[row] = 77;
        cpu.memory[static_cast<std::uint16_t>(at.lso + row)] = 77;

        math.yy = static_cast<std::uint8_t>(centre);
        math.yyNext = static_cast<std::uint8_t>(centre >> 8);
        cpu.memory[at.yy] = math.yy;
        cpu.memory[static_cast<std::uint16_t>(at.yy + 1)] = math.yyNext;

        cpu.a = width;
        cpu.y = row;
        const Elite::Testing::RunResult run = cpu.CallSubroutine(edges, 20'000);
        Assert::IsTrue(run.completed, L"EDGES returned");

        const bool off = Elite::ClipSunRow(state, math, draw, width, row);

        const std::wstring where = Widen("EDGES centre=" + std::to_string(centre) + " width="
                                         + std::to_string(width));
        Assert::AreEqual(cpu.c, off, (where + L": the carry").c_str());
        Assert::AreEqual(cpu.memory[at.x1], draw.x1, (where + L": X1").c_str());
        Assert::AreEqual(cpu.memory[at.x2], draw.x2, (where + L": X2").c_str());
        Assert::AreEqual(cpu.memory[at.t], math.t, (where + L": T").c_str());
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.lso + row)], state.sun[row],
                         (where + L": the heap row").c_str());

        cleared += (state.sun[row] == 0u) ? 1u : 0u;
        clamped += (!off && (draw.x1 == 0u || draw.x2 == 255u)) ? 1u : 0u;
        ++compared;
      }
    }

    Assert::AreEqual<std::uint32_t>(15u * 10u, compared, L"every centre against every width");
    Assert::IsTrue(cleared > 0u, L"some rows were cleared");
    Assert::IsTrue(clamped > 0u, L"some rows were clamped to a screen edge");
  }

  /// 6502: FLFLLS and WP1 -- forget the sun, and forget the ball.
  TEST_METHOD(TheHeapsAreClearedTheSameWay)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const HeapLabels at(oracle);

    for (int which = 0; which < 2; ++which)
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::PlanetSunState state;

      SeedSunHeap(cpu, state, at, 0x2C6A91B7u);
      SeedBallHeap(cpu, state, at, 0x8D14E703u, 90);

      const wchar_t* name = L"";
      if (which == 0)
      {
        name = L"FLFLLS";
        cpu.CallSubroutine(oracle.Label("FLFLLS"), 20'000);
        Elite::ClearSunHeap(state);
      }
      else
      {
        name = L"WP1";
        cpu.CallSubroutine(oracle.Label("WP1"), 20'000);
        Elite::ClearBallHeap(state);
      }

      CompareHeaps(cpu, state, at, name);
    }
  }

  /*
   * 6502: HLOIN2 and WPLS -- rub the sun out.
   *
   * Whole-canvas comparisons, because the point of the heap is what ends up on the screen. The
   * sweep runs `WPLS` twice in a row on the same state: the second call must find the flag byte
   * `WPLS` itself wrote and do nothing, which is the guard the routine opens with.
   */
  TEST_METHOD(TheSunIsErasedTheSameWay)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const HeapLabels at(oracle);
    const std::uint16_t wpls = oracle.Label("WPLS");
    const std::uint16_t hloin2 = oracle.Label("HLOIN2");

    const std::vector<std::uint16_t> CENTRES = { 0, 60, 128, 200, 255, 260, 400, 0xFFF0u };

    std::uint32_t marked = 0;
    std::uint32_t compared = 0;

    for (const std::uint16_t centre : CENTRES)
    {
      // 6502: `LDA LSX / BMI` -- bit 7, not "non-zero". 1 and 127 are what tell the two apart,
      // and a sweep of 0 and 255 alone cannot (§6.48).
      for (const std::uint8_t marker : { 0x00u, 0x01u, 0x7Fu, 0x80u, 0xFFu })
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        Elite::PlanetSunState state;
        Elite::MathWorkspace math;
        Elite::DrawWorkspace draw;

        SeedSunHeap(cpu, state, at, 0x51F3A2C9u + centre);
        SeedBallHeap(cpu, state, at, 0x8D14E703u, 40);
        // Entry 0 is the flag, and the negative case is the one that returns immediately.
        state.sun[0] = marker;
        cpu.memory[at.lso] = marker;

        state.sunX = static_cast<std::uint8_t>(centre);
        state.sunXNext = static_cast<std::uint8_t>(centre >> 8);
        cpu.memory[at.sunx] = state.sunX;
        cpu.memory[static_cast<std::uint16_t>(at.sunx + 1)] = state.sunXNext;

        // Two calls, because the second must find what the first left.
        for (int pass = 0; pass < 2; ++pass)
        {
          const Elite::Testing::RunResult run = cpu.CallSubroutine(wpls, 4'000'000);
          Assert::IsTrue(run.completed, L"WPLS returned");
          Elite::EraseSun(canvas, state, math, draw);

          const std::wstring where = Widen("WPLS centre=" + std::to_string(centre)
                                           + " flag=" + std::to_string(marker) + " pass="
                                           + std::to_string(pass));
          marked += CompareScreens(cpu, at.screen, canvas, where);
          CompareHeaps(cpu, state, at, where);
          ++compared;
        }
      }
    }

    // And one direct call to HLOIN2, because WPLS only ever reaches it for a non-blank row and
    // §6.20 says run a routine where it can be run.
    for (const std::uint8_t width : { std::uint8_t{ 0 }, std::uint8_t{ 30 }, std::uint8_t{ 255 } })
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      Elite::PlanetSunState state;
      Elite::MathWorkspace math;
      Elite::DrawWorkspace draw;

      const std::uint8_t row = 64;
      SeedSunHeap(cpu, state, at, 0xA07B5E26u);
      SeedBallHeap(cpu, state, at, 0x3FCD0841u, 40);
      math.yy = 128;
      math.yyNext = 0;
      cpu.memory[at.yy] = 128;
      cpu.memory[static_cast<std::uint16_t>(at.yy + 1)] = 0;

      cpu.a = width;
      cpu.y = row;
      Assert::IsTrue(cpu.CallSubroutine(hloin2, 40'000).completed, L"HLOIN2 returned");
      Elite::EraseSunRow(canvas, state, math, draw, width, row);

      const std::wstring where = Widen("HLOIN2 width=" + std::to_string(width));
      marked += CompareScreens(cpu, at.screen, canvas, where);
      CompareHeaps(cpu, state, at, where);
      Assert::AreEqual(cpu.memory[at.y1], draw.y1, (where + L": Y1").c_str());
      ++compared;
    }

    Assert::AreEqual<std::uint32_t>(8u * 5u * 2u + 3u, compared,
                                    L"every centre, twice, every flag");
    Assert::IsTrue(marked > 0u, L"the sun was actually drawn");
    Logger::WriteMessage(("WPLS: " + std::to_string(compared) + " calls, " + std::to_string(marked)
                          + " marked bytes")
                           .c_str());
  }
};


TEST_CLASS(ThePlanetLineHeap)
{
public:
  /*
   * 6502: WPLS2 -- rub the planet out, and the two things worth getting wrong are both about
   * what the walk carries from one segment to the next.
   *
   * 255 in the y heap is a BREAK: the pair after it is a new run's start, not another end. And
   * `LOIN` sets `SWAP` when it drew a line right-to-left, in which case X2/Y2 no longer hold this
   * segment's end and the hand-off to the next segment is skipped entirely.
   */
  TEST_METHOD(ThePlanetIsErasedTheSameWay)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const HeapLabels at(oracle);
    const std::uint16_t wpls2 = oracle.Label("WPLS2");

    std::uint32_t marked = 0;
    std::uint32_t compared = 0;

    for (const std::uint8_t lsp : { 0, 1, 2, 9, 40, 128, 255 })
    {
      for (const std::uint8_t flag : { 0x00u, 0x01u, 0xFFu })
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        Elite::PlanetSunState state;
        Elite::DrawWorkspace draw;

        SeedBallHeap(cpu, state, at, 0x3FCD0841u + lsp, lsp);
        state.SetBallX(0, flag);
        cpu.memory[at.lsx2] = flag;

        // LOIN starts from whatever X1/Y1 hold, and the first segment of a run has no
        // predecessor -- so what the caller left is part of the answer.
        draw.x1 = 100;
        draw.y1 = 50;
        cpu.memory[at.x1] = 100;
        cpu.memory[at.y1] = 50;

        const Elite::Testing::RunResult run = cpu.CallSubroutine(wpls2, 8'000'000);
        Assert::IsTrue(run.completed, L"WPLS2 returned");
        Logger::WriteMessage(("WPLS2 lsp=" + std::to_string(lsp) + " flag=" + std::to_string(flag)
                              + ": " + std::to_string(run.instructions) + " instructions").c_str());
        Elite::EraseBall(canvas, state, draw);

        const std::wstring where = Widen("WPLS2 lsp=" + std::to_string(lsp) + " flag="
                                         + std::to_string(flag));
        Assert::AreEqual(cpu.memory[at.x1], draw.x1, (where + L": X1").c_str());
        Assert::AreEqual(cpu.memory[at.y1], draw.y1, (where + L": Y1").c_str());
        Assert::AreEqual(cpu.memory[at.x2], draw.x2, (where + L": X2").c_str());
        Assert::AreEqual(cpu.memory[at.y2], draw.y2, (where + L": Y2").c_str());
        Assert::AreEqual(cpu.memory[at.swap], draw.swap, (where + L": SWAP").c_str());
        marked += CompareScreens(cpu, at.screen, canvas, where);
        CompareHeaps(cpu, state, at, where);
        ++compared;
      }
    }

    Assert::AreEqual<std::uint32_t>(7u * 3u, compared, L"every length against every flag");
    Assert::IsTrue(marked > 0u, L"the planet was actually drawn");
    Logger::WriteMessage(("WPLS2: " + std::to_string(compared) + " calls, " + std::to_string(marked)
                          + " marked bytes")
                           .c_str());
  }

  /// 6502: PL2 -- and the whole of its test is the bottom bit of TYPE.
  TEST_METHOD(TheEraserPicksTheSameOne)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const HeapLabels at(oracle);
    const std::uint16_t pl2 = oracle.Label("PL2");

    for (const std::uint8_t type : { 128, 129 })
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      Elite::PlanetSunState state;
      Elite::MathWorkspace math;
      Elite::DrawWorkspace draw;

      SeedSunHeap(cpu, state, at, 0x2C6A91B7u);
      SeedBallHeap(cpu, state, at, 0x8D14E703u, 24);
      state.sun[0] = 0;
      cpu.memory[at.lso] = 0;
      state.SetBallX(0, 0);
      cpu.memory[at.lsx2] = 0;

      state.sunX = 128;
      cpu.memory[at.sunx] = 128;
      cpu.memory[at.type] = type;
      draw.x1 = 100;
      draw.y1 = 50;
      cpu.memory[at.x1] = 100;
      cpu.memory[at.y1] = 50;

      Assert::IsTrue(cpu.CallSubroutine(pl2, 8'000'000).completed, L"PL2 returned");
      Elite::ErasePlanetOrSun(canvas, state, math, draw, type);

      const std::wstring where = Widen("PL2 type=" + std::to_string(type));
      CompareScreens(cpu, at.screen, canvas, where);
      CompareHeaps(cpu, state, at, where);
    }
  }

  /*
   * 6502: CHKON -- is any of this circle on screen?
   *
   * Three answers, not one: the carry, and the top and bottom of the circle in P+1 and P+2, which
   * are written on some paths and not others. And its `BMI PL44` leaves through `PLS6`'s `CLC`
   * rather than through the `PL44` in `EDGES`, which this build does not assemble (§6.45).
   */
  TEST_METHOD(TheCircleVisibilityTestMatchesCHKON)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const HeapLabels at(oracle);
    const std::uint16_t chkon = oracle.Label("CHKON");

    /*
     * The high bytes matter as much as the low ones. `CHKON` has four exits that test bit 7 of a
     * sixteen-bit sum, and a sweep whose coordinates all fit in nine bits reaches none of them --
     * which is what the mutation testing said (§6.48), not what reading the sweep suggested.
     */
    const std::vector<std::uint16_t> CENTRES = { 0,   1,     60,     96,     128,    143,
                                                 144, 199,   255,    256,    300,    0x7F00u,
                                                 0x8000u,    0xC000u, 0xFF00u, 0xFFFFu };
    const std::vector<std::uint8_t> RADII = { 0, 1, 8, 60, 96, 128, 255 };

    std::uint32_t compared = 0;
    std::uint32_t visible = 0;

    for (const std::uint16_t x : CENTRES)
    {
      for (const std::uint16_t y : CENTRES)
      {
        for (const std::uint8_t radius : RADII)
        {
          for (const std::uint8_t bottom : { 143, 199 })
          {
            Cpu6502 cpu = oracle.Fresh();
            Elite::PlanetSunState state;
            Elite::MathWorkspace math;
            Elite::Projection centre;

            centre.x = static_cast<std::uint8_t>(x);
            centre.x1 = static_cast<std::uint8_t>(x >> 8);
            centre.y = static_cast<std::uint8_t>(y);
            centre.y1 = static_cast<std::uint8_t>(y >> 8);
            math.k[0] = radius;
            state.yx2M1 = bottom;

            cpu.memory[at.k3] = centre.x;
            cpu.memory[static_cast<std::uint16_t>(at.k3 + 1)] = centre.x1;
            cpu.memory[at.k4] = centre.y;
            cpu.memory[static_cast<std::uint16_t>(at.k4 + 1)] = centre.y1;
            cpu.memory[at.k] = radius;
            cpu.memory[at.yx2m1] = bottom;

            Assert::IsTrue(cpu.CallSubroutine(chkon, 20'000).completed, L"CHKON returned");
            const bool off = Elite::CircleOffScreen(state, math, centre);

            const std::wstring where = Widen("CHKON x=" + std::to_string(x) + " y="
                                             + std::to_string(y) + " r=" + std::to_string(radius)
                                             + " bottom=" + std::to_string(bottom));
            Assert::AreEqual(cpu.c, off, (where + L": the carry").c_str());
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.p + 1)], math.p1,
                             (where + L": P+1").c_str());
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.p + 2)], math.p2,
                             (where + L": P+2").c_str());

            visible += off ? 0u : 1u;
            ++compared;
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(16u * 16u * 7u * 2u, compared, L"every centre and radius");
    Assert::IsTrue(visible > 0u, L"some circles were on screen");
    Assert::IsTrue(visible < compared, L"and some were not");
  }
};

} // namespace GameLogicTests

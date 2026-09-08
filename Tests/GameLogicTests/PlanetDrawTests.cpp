#include "pch.h"

#include "ShipBytes.h"

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
      std::uint16_t lso = 0, lsx2 = 0, lsy2 = 0, ballHeapTop = 0, sunx = 0, yx2m1 = 0;
      std::uint16_t yy = 0, zeroPageT = 0, zeroPageK = 0, zeroPageK3 = 0, k4 = 0, zeroPageP = 0;
      std::uint16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0, swap = 0, type = 0, clippingOff = 0;
      std::uint16_t zeroPageK5 = 0, zeroPageK6 = 0, circleStep = 0, flag = 0, zeroPageCnt = 0, xx13 = 0, dotProducts = 0;
      std::uint16_t inwk = 0, zeroPageK2 = 0, scaledOrientation = 0, tgt = 0, coneWidth = 0, planetDetail = 0, sun = 0;
      std::uint16_t qq11 = 0;
      std::uint16_t screen = 0;

      explicit HeapLabels(const OracleImage& _oracle)
      {
        lso = _oracle.Label("LSO");
        lsx2 = _oracle.Label("LSX2");
        lsy2 = _oracle.Label("LSY2");
        ballHeapTop = _oracle.Label("LSP");
        sunx = _oracle.Label("SUNX");
        yx2m1 = _oracle.Label("Yx2M1");
        yy = _oracle.Label("YY");
        zeroPageT = _oracle.Label("T");
        zeroPageK = _oracle.Label("K");
        zeroPageK3 = _oracle.Label("K3");
        k4 = _oracle.Label("K4");
        zeroPageP = _oracle.Label("P");
        x1 = _oracle.Label("X1");
        y1 = _oracle.Label("Y1");
        x2 = _oracle.Label("X2");
        y2 = _oracle.Label("Y2");
        swap = _oracle.Label("SWAP");
        type = _oracle.Label("TYPE");
        clippingOff = _oracle.Label("dontclip");
        zeroPageK5 = _oracle.Label("K5");
        zeroPageK6 = _oracle.Label("K6");
        circleStep = _oracle.Label("STP");
        flag = _oracle.Label("FLAG");
        zeroPageCnt = _oracle.Label("CNT");
        xx13 = _oracle.Label("XX13");
        dotProducts = _oracle.Label("XX12");
        inwk = _oracle.Label("INWK");
        zeroPageK2 = _oracle.Label("K2");
        scaledOrientation = _oracle.Label("XX16");
        tgt = _oracle.Label("TGT");
        coneWidth = _oracle.Label("CNT2");
        planetDetail = _oracle.Label("PLTOG");
        sun = _oracle.Label("SUN");
        qq11 = _oracle.Label("QQ11");

        const Cpu6502 cpu = _oracle.Fresh();
        screen = static_cast<std::uint16_t>((cpu.memory[_oracle.Label("ylookupl")] | (cpu.memory[_oracle.Label("ylookuph")] << 8)) - 0x20u);
      }
    };

    void SeedSunHeap(Cpu6502& _cpu, Elite::PlanetSunState& _state, const HeapLabels& _at, std::uint32_t _seed)
    {
      std::uint32_t bits = _seed;
      for (std::size_t row = 0; row < Elite::SUN_HEAP_SIZE; ++row)
      {
        bits = bits * 1664525u + 1013904223u;
        // Two rows in five are blank, which is what a disc's top and bottom look like.
        const std::uint8_t width = ((bits >> 13) % 5u < 2u) ? 0u : static_cast<std::uint8_t>((bits >> 24) & 0x7Fu);
        _state.sun[row] = width;
        _cpu.memory[static_cast<std::uint16_t>(_at.lso + row)] = width;
      }
    }

    void SeedBallHeap(Cpu6502& _cpu, Elite::PlanetSunState& _state, const HeapLabels& _at, std::uint32_t _seed, std::uint8_t _ballHeapTop)
    {
      std::uint32_t bits = _seed;
      for (std::size_t at = 0; at < Elite::BALL_HEAP_SIZE; ++at)
      {
        bits = bits * 1664525u + 1013904223u;
        const std::uint8_t x = static_cast<std::uint8_t>(bits >> 24);
        bits = bits * 1664525u + 1013904223u;
        // One entry in seven is the 255 that breaks a run, so both walks are exercised.
        const std::uint8_t y = ((bits >> 11) % 7u == 0u) ? 0xFFu : static_cast<std::uint8_t>((bits >> 24) % 144u);

        _state.SetBallX(static_cast<std::uint8_t>(at), x);
        _state.SetBallY(static_cast<std::uint8_t>(at), y);
        _cpu.memory[static_cast<std::uint16_t>(_at.lsx2 + at)] = x;
        _cpu.memory[static_cast<std::uint16_t>(_at.lsy2 + at)] = y;
      }

      _state.ballHeapTop = _ballHeapTop;
      _cpu.memory[_at.ballHeapTop] = _ballHeapTop;
    }

    void CompareHeaps(const Cpu6502& _cpu, const Elite::PlanetSunState& _state, const HeapLabels& _at, const std::wstring& _where)
    {
      for (std::size_t row = 0; row < Elite::SUN_HEAP_SIZE; ++row)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.lso + row)], _state.sun[row],
                         (_where + L": LSO+" + std::to_wstring(row)).c_str());
      }
      for (std::size_t at = 0; at < Elite::BALL_HEAP_SIZE; ++at)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.lsx2 + at)], _state.BallX(static_cast<std::uint8_t>(at)),
                         (_where + L": LSX2+" + std::to_wstring(at)).c_str());
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.lsy2 + at)], _state.BallY(static_cast<std::uint8_t>(at)),
                         (_where + L": LSY2+" + std::to_wstring(at)).c_str());
      }
      Assert::AreEqual(_cpu.memory[_at.ballHeapTop], _state.ballHeapTop, (_where + L": LSP").c_str());
    }

    std::uint32_t CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas, const std::wstring& _where)
    {
      const std::span<const std::uint8_t> ours = _canvas.Screen();
      std::uint32_t marked = 0;
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
        if (expected != ours[offset])
        {
          Assert::Fail((_where + L": the screen differs at offset " + std::to_wstring(offset) + L" -- game has " +
                        std::to_wstring(expected) + L", port has " + std::to_wstring(ours[offset]))
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

      const std::vector<std::uint16_t> CENTRES = {0, 1, 40, 127, 128, 129, 200, 255, 256, 300, 383, 384, 500, 0xFF00u, 0xFFFFu};
      const std::vector<std::uint8_t> WIDTHS = {0, 1, 2, 63, 64, 100, 127, 128, 200, 255};

      std::uint32_t compared = 0;
      std::uint32_t cleared = 0;
      std::uint32_t clamped = 0;

      for (const std::uint16_t centre : CENTRES)
      {
        for (const std::uint8_t width : WIDTHS)
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::PlanetSunState state;

          const std::uint8_t row = 91;
          SeedSunHeap(cpu, state, at, 0x71C3492Bu + width);
          state.sun[row] = 77;
          cpu.memory[static_cast<std::uint16_t>(at.lso + row)] = 77;

          // `YY(1 0)` is the centre `EDGES` clips against and a parameter since M2-c-3.
          const Elite::SignMag16 at16{static_cast<std::uint8_t>(centre), static_cast<std::uint8_t>(centre >> 8)};
          cpu.memory[at.yy] = at16.lo;
          cpu.memory[static_cast<std::uint16_t>(at.yy + 1)] = at16.hi;

          cpu.a = width;
          cpu.y = row;
          const Elite::Testing::RunResult run = cpu.CallSubroutine(edges, 20'000);
          Assert::IsTrue(run.completed, L"EDGES returned");

          const Elite::SunRow clipped = Elite::ClipSunRow(state, at16, width, row);

          const std::wstring where = Widen("EDGES centre=" + std::to_string(centre) + " width=" + std::to_string(width));
          Assert::AreEqual(cpu.c, clipped.offScreen, (where + L": the carry").c_str());
          Assert::AreEqual(cpu.memory[at.x1], clipped.x1, (where + L": X1").c_str());
          Assert::AreEqual(cpu.memory[at.x2], clipped.x2, (where + L": X2").c_str());
          // `T` is `EDGES`'s own since M2-c-2: it parks the half-width for the left end's subtraction.
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.lso + row)], state.sun[row], (where + L": the heap row").c_str());

          cleared += (state.sun[row] == 0u) ? 1u : 0u;
          clamped += (!clipped.offScreen && (clipped.x1 == 0u || clipped.x2 == 255u)) ? 1u : 0u;
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

      const std::vector<std::uint16_t> CENTRES = {0, 60, 128, 200, 255, 260, 400, 0xFFF0u};

      std::uint32_t marked = 0;
      std::uint32_t compared = 0;

      for (const std::uint16_t centre : CENTRES)
      {
        // 6502: `LDA LSX / BMI` -- bit 7, not "non-zero". 1 and 127 are what tell the two apart,
        // and a sweep of 0 and 255 alone cannot (§6.48).
        for (const std::uint8_t marker : {0x00u, 0x01u, 0x7Fu, 0x80u, 0xFFu})
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::Canvas canvas;
          Elite::PlanetSunState state;
          Elite::MathWorkspace math;

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
            Elite::EraseSun(canvas, state);

            const std::wstring where =
              Widen("WPLS centre=" + std::to_string(centre) + " flag=" + std::to_string(marker) + " pass=" + std::to_string(pass));
            marked += CompareScreens(cpu, at.screen, canvas, where);
            CompareHeaps(cpu, state, at, where);
            ++compared;
          }
        }
      }

      // And one direct call to HLOIN2, because WPLS only ever reaches it for a non-blank row and
      // §6.20 says run a routine where it can be run.
      for (const std::uint8_t width : {std::uint8_t{0}, std::uint8_t{30}, std::uint8_t{255}})
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        Elite::PlanetSunState state;
        Elite::MathWorkspace math;

        const std::uint8_t row = 64;
        SeedSunHeap(cpu, state, at, 0xA07B5E26u);
        SeedBallHeap(cpu, state, at, 0x3FCD0841u, 40);
        const Elite::SignMag16 at16{128, 0}; // 6502: YY(1 0)
        cpu.memory[at.yy] = at16.lo;
        cpu.memory[static_cast<std::uint16_t>(at.yy + 1)] = at16.hi;

        cpu.a = width;
        cpu.y = row;
        Assert::IsTrue(cpu.CallSubroutine(hloin2, 40'000).completed, L"HLOIN2 returned");
        Elite::EraseSunRow(canvas, state, at16, width, row);

        const std::wstring where = Widen("HLOIN2 width=" + std::to_string(width));
        marked += CompareScreens(cpu, at.screen, canvas, where);
        CompareHeaps(cpu, state, at, where);
        // `Y1` is the row `HLOIN` is given and this routine's own since M2-c-2; the pixels above
        // are what it drew with it.
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(8u * 5u * 2u + 3u, compared, L"every centre, twice, every flag");
      Assert::IsTrue(marked > 0u, L"the sun was actually drawn");
      Logger::WriteMessage(("WPLS: " + std::to_string(compared) + " calls, " + std::to_string(marked) + " marked bytes").c_str());
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

      for (const std::uint8_t ballHeapTop : {0, 1, 2, 9, 40, 128, 255})
      {
        for (const std::uint8_t flag : {0x00u, 0x01u, 0xFFu})
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::Canvas canvas;
          Elite::PlanetSunState state;

          SeedBallHeap(cpu, state, at, 0x3FCD0841u + ballHeapTop, ballHeapTop);
          state.SetBallX(0, flag);
          cpu.memory[at.lsx2] = flag;

          // LOIN starts from whatever X1/Y1 hold, and the first segment of a run has no
          // predecessor. `WS2` makes entry 0 a break, so the walk the game reaches starts from a
          // break and the port starts from a zeroed `Line` (M2-c-2); the oracle is given the same.
          cpu.memory[at.x1] = 0;
          cpu.memory[at.y1] = 0;

          const Elite::Testing::RunResult run = cpu.CallSubroutine(wpls2, 8'000'000);
          Assert::IsTrue(run.completed, L"WPLS2 returned");
          Logger::WriteMessage(("WPLS2 lsp=" + std::to_string(ballHeapTop) + " flag=" + std::to_string(flag) + ": " +
                                std::to_string(run.instructions) + " instructions")
                                 .c_str());
          Elite::EraseBall(canvas, state);

          const std::wstring where = Widen("WPLS2 lsp=" + std::to_string(ballHeapTop) + " flag=" + std::to_string(flag));
          /*
           * `X1`, `Y1`, `X2`, `Y2` and `SWAP` are the walk's own since M2-c-2: it hands each segment
           * to `LOIN` and takes the ends back from it (`DrawnLine`), which the canvas sweep pins
           * against the game byte for byte. What is compared here is the pixels and the heap.
           */
          marked += CompareScreens(cpu, at.screen, canvas, where);
          CompareHeaps(cpu, state, at, where);
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(7u * 3u, compared, L"every length against every flag");
      Assert::IsTrue(marked > 0u, L"the planet was actually drawn");
      Logger::WriteMessage(("WPLS2: " + std::to_string(compared) + " calls, " + std::to_string(marked) + " marked bytes").c_str());
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

      for (const std::uint8_t type : {128, 129})
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        Elite::PlanetSunState state;
        Elite::MathWorkspace math;

        SeedSunHeap(cpu, state, at, 0x2C6A91B7u);
        SeedBallHeap(cpu, state, at, 0x8D14E703u, 24);
        state.sun[0] = 0;
        cpu.memory[at.lso] = 0;
        state.SetBallX(0, 0);
        cpu.memory[at.lsx2] = 0;

        state.sunX = 128;
        cpu.memory[at.sunx] = 128;
        cpu.memory[at.type] = type;

        // `WPLS2` walks the ball heap from whatever `X1`/`Y1` hold, and entry 0 is a break, so the
        // first segment of the walk the game reaches has no predecessor. The port starts from a
        // zeroed `Line` since M2-c-2; the oracle is given the same rather than a stale pair.
        cpu.memory[at.x1] = 0;
        cpu.memory[at.y1] = 0;

        Assert::IsTrue(cpu.CallSubroutine(pl2, 8'000'000).completed, L"PL2 returned");
        Elite::ErasePlanetOrSun(canvas, state, Elite::TypeOf(type));

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
      const std::vector<std::uint16_t> CENTRES = {0,   1,   60,  96,      128,     143,     144,     199,
                                                  255, 256, 300, 0x7F00u, 0x8000u, 0xC000u, 0xFF00u, 0xFFFFu};
      const std::vector<std::uint8_t> RADII = {0, 1, 8, 60, 96, 128, 255};

      std::uint32_t compared = 0;
      std::uint32_t visible = 0;

      for (const std::uint16_t x : CENTRES)
      {
        for (const std::uint16_t y : CENTRES)
        {
          for (const std::uint8_t radius : RADII)
          {
            for (const std::uint8_t bottom : {143, 199})
            {
              Cpu6502 cpu = oracle.Fresh();
              Elite::PlanetSunState state;
              Elite::MathWorkspace math;
              Elite::Projection centre;

              centre.x = static_cast<std::uint8_t>(x);
              centre.x1 = static_cast<std::uint8_t>(x >> 8);
              centre.y = static_cast<std::uint8_t>(y);
              centre.y1 = static_cast<std::uint8_t>(y >> 8);
  
              state.lowestVisibleRow = bottom;

              cpu.memory[at.zeroPageK3] = centre.x;
              cpu.memory[static_cast<std::uint16_t>(at.zeroPageK3 + 1)] = centre.x1;
              cpu.memory[at.k4] = centre.y;
              cpu.memory[static_cast<std::uint16_t>(at.k4 + 1)] = centre.y1;
              cpu.memory[at.zeroPageK] = radius;
              cpu.memory[at.yx2m1] = bottom;

              Assert::IsTrue(cpu.CallSubroutine(chkon, 20'000).completed, L"CHKON returned");
              const Elite::CircleExtent extent = Elite::CircleOffScreen(state, radius, centre);
              const bool off = extent.offScreen;

              const std::wstring where = Widen("CHKON x=" + std::to_string(x) + " y=" + std::to_string(y) + " r=" + std::to_string(radius) +
                                               " bottom=" + std::to_string(bottom));
              Assert::AreEqual(cpu.c, off, (where + L": the carry").c_str());
              // `(P+2 P+1)` is a returned value since M2-c-3, and it is compared only where the
              // original writes it. `PL21`'s three early exits leave both bytes as the caller had
              // them and the fourth leaves `P+2`; `SUN`, the only reader, looks at the pair after
              // an on-screen answer alone, so those paths are the contract.
              if (!off)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.zeroPageP + 1)], extent.bottom, (where + L": P+1").c_str());
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.zeroPageP + 2)], extent.bottomHigh, (where + L": P+2").c_str());
              }

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

  TEST_CLASS(TheBallDrawing)
  {
  public:
    /*
     * 6502: CIRCLE, CIRCLE2 and BLINE -- the planet as a sixty-four-sided polygon.
     *
     * Compared on the whole canvas AND on the heap it builds, because the heap is what `WPLS2`
     * will walk next frame: a circle drawn correctly onto a wrong heap looks right once and leaves
     * the screen dirty for ever after. Every centre and radius that puts a circle wholly on the
     * screen, wholly off it, and across each edge, so the run-breaking path runs.
     */
    TEST_METHOD(TheCircleMatchesCIRCLE)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const HeapLabels at(oracle);
      const std::uint16_t circle = oracle.Label("CIRCLE");

      const std::vector<std::uint16_t> CENTRES = {0, 40, 128, 200, 255, 256, 0xFF00u};
      const std::vector<std::uint8_t> RADII = {0, 1, 7, 8, 30, 59, 60, 96, 255};

      std::uint32_t compared = 0;
      std::uint32_t marked = 0;
      std::uint32_t drew = 0;
      std::uint32_t refused = 0;

      for (const std::uint16_t x : CENTRES)
      {
        for (const std::uint16_t y : CENTRES)
        {
          for (const std::uint8_t radius : RADII)
          {
            Cpu6502 cpu = oracle.Fresh();
            Elite::Canvas canvas;
            Elite::PlanetSunState state;
            Elite::GeometryWorkspace geometry;
            Elite::MathWorkspace math;
            Elite::ClipState clip;
            Elite::Projection centre;

            SeedBallHeap(cpu, state, at, 0x8D14E703u, 1);
            SeedSunHeap(cpu, state, at, 0x2C6A91B7u);

            centre.x = static_cast<std::uint8_t>(x);
            centre.x1 = static_cast<std::uint8_t>(x >> 8);
            centre.y = static_cast<std::uint8_t>(y);
            centre.y1 = static_cast<std::uint8_t>(y >> 8);

            state.lowestVisibleRow = 143;

            // `CIRCLE2` opens by zeroing `CNT`, which a sweep that leaves it at zero anyway cannot
            // measure (§6.48). The byte is `CIRCLE2`'s own local since M2-c-3, so the oracle is
            // still seeded non-zero -- if the port failed to start its walk at zero the segments
            // would come out at the wrong angles and the screen and heap would say so.
            cpu.memory[at.zeroPageCnt] = 200;

            cpu.memory[at.zeroPageK3] = centre.x;
            cpu.memory[static_cast<std::uint16_t>(at.zeroPageK3 + 1)] = centre.x1;
            cpu.memory[at.k4] = centre.y;
            cpu.memory[static_cast<std::uint16_t>(at.k4 + 1)] = centre.y1;
            cpu.memory[at.zeroPageK] = radius;
            cpu.memory[at.yx2m1] = 143;
            cpu.memory[at.clippingOff] = 0;
            clip.clippingOff = 0;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(circle, 8'000'000);
            Assert::IsTrue(run.completed, L"CIRCLE returned");

            const bool off = Elite::DrawCircle(canvas, state, geometry, math, clip, centre, radius);

            const std::wstring where = Widen("CIRCLE x=" + std::to_string(x) + " y=" + std::to_string(y) + " r=" + std::to_string(radius));
            Assert::AreEqual(cpu.c, off, (where + L": the carry").c_str());
            marked += CompareScreens(cpu, at.screen, canvas, where);
            CompareHeaps(cpu, state, at, where);

            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.zeroPageK5 + byte)], state.segmentStart[byte],
                               (where + L": K5+" + std::to_wstring(byte)).c_str());
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.zeroPageK6 + byte)], state.segmentEnd[byte],
                               (where + L": K6+" + std::to_wstring(byte)).c_str());
            }
            Assert::AreEqual(cpu.memory[at.circleStep], state.circleStep, (where + L": STP").c_str());
            Assert::AreEqual(cpu.memory[at.flag], state.flag, (where + L": FLAG").c_str());

            refused += off ? 1u : 0u;
            drew += off ? 0u : 1u;
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(7u * 7u * 9u, compared, L"every centre and radius");
      Assert::IsTrue(marked > 0u, L"the circles were actually drawn");
      Assert::IsTrue(drew > 0u, L"some circles were on screen");
      Assert::IsTrue(refused > 0u, L"and some were refused");
      Logger::WriteMessage(("CIRCLE: " + std::to_string(compared) + " circles, " + std::to_string(drew) + " drawn, " +
                            std::to_string(refused) + " refused, " + std::to_string(marked) + " marked bytes")
                             .c_str());
    }

    /*
     * 6502: BLINE on its own, because `CIRCLE2` never reaches it with a set carry and `PLS22`
     * does -- and `TXA / ADC K4` is its first instruction (§6.20).
     *
     * The `FLAG` and heap states are swept as well: the first segment of a circle takes a
     * different path from the rest, and a segment following a break takes a third.
     */
    TEST_METHOD(TheSegmentMatchesBLINE)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const HeapLabels at(oracle);
      const std::uint16_t bline = oracle.Label("BLINE");

      std::uint32_t compared = 0;
      std::uint32_t marked = 0;

      for (const std::uint8_t flag : {0x00u, 0x01u, 0xFFu})
      {
        for (const bool carryIn : {false, true})
        {
          for (const std::uint8_t ballHeapTop : {1, 2, 40})
          {
            for (const std::uint8_t xIn : {0u, 5u, 128u, 250u})
            {
              /*
               * Three segments: one wholly on screen, one that has to be clipped at an end, and one
               * that `LL145` refuses entirely. The third is the `BCS BL5` path, and without it the
               * sweep cannot tell a break from no break (§6.48).
               */
              for (const int shape : {0, 1, 2})
              {
                Cpu6502 cpu = oracle.Fresh();
                Elite::Canvas canvas;
                Elite::PlanetSunState state;
                Elite::GeometryWorkspace geometry;
                Elite::MathWorkspace math;
                Elite::ClipState clip;
                Elite::Projection centre;

                SeedBallHeap(cpu, state, at, 0x51F3A2C9u + ballHeapTop, ballHeapTop);

                const std::uint8_t ENDS[3][8] = {
                  {30, 0, 90, 0, 200, 0, 60, 0},  // both ends on screen
                  {30, 0, 90, 0, 200, 2, 60, 0},  // one end a long way to the right
                  {200, 8, 90, 0, 250, 9, 60, 0}, // both ends off the same side
                };
                const std::uint8_t* const ends = ENDS[shape];
                const std::uint8_t zeroPageK5[4] = {ends[0], ends[1], ends[2], ends[3]};
                const std::uint8_t zeroPageK6[4] = {ends[4], ends[5], ends[6], ends[7]};
                for (std::size_t byte = 0; byte < 4u; ++byte)
                {
                  state.segmentStart[byte] = zeroPageK5[byte];
                  state.segmentEnd[byte] = zeroPageK6[byte];
                  cpu.memory[static_cast<std::uint16_t>(at.zeroPageK5 + byte)] = zeroPageK5[byte];
                  cpu.memory[static_cast<std::uint16_t>(at.zeroPageK6 + byte)] = zeroPageK6[byte];
                }

                centre.y = 72;
                centre.y1 = 0;
                // `T` and `CNT` are parameters since M2-c-3: the offset's high byte and the
                // segment counter `BLINE` advances and hands back.
                constexpr std::uint8_t OFFSET_HIGH = 0;
                constexpr std::uint8_t CNT_IN = 12;
                state.circleStep = 4;
                state.flag = flag;

                cpu.memory[at.k4] = 72;
                cpu.memory[static_cast<std::uint16_t>(at.k4 + 1)] = 0;
                cpu.memory[at.zeroPageT] = OFFSET_HIGH;
                cpu.memory[at.zeroPageCnt] = CNT_IN;
                cpu.memory[at.circleStep] = 4;
                cpu.memory[at.flag] = flag;
                cpu.memory[at.clippingOff] = 0;
                clip.clippingOff = 0;

                cpu.x = static_cast<std::uint8_t>(xIn);
                cpu.c = carryIn;
                const Elite::Testing::RunResult run = cpu.CallSubroutine(bline, 400'000);
                Assert::IsTrue(run.completed, L"BLINE returned");

                const std::uint8_t got = Elite::DrawBallLine(canvas, state, geometry, math, clip, centre,
                                                             Elite::SignMag16{static_cast<std::uint8_t>(xIn), OFFSET_HIGH}, CNT_IN,
                                                             carryIn);

                const std::wstring where =
                  Widen("BLINE flag=" + std::to_string(flag) + " carry=" + std::to_string(carryIn ? 1 : 0) + " lsp=" + std::to_string(ballHeapTop) +
                        " x=" + std::to_string(xIn) + " shape=" + std::to_string(shape));
                Assert::AreEqual(cpu.a, got, (where + L": the returned CNT").c_str());
                marked += CompareScreens(cpu, at.screen, canvas, where);
                CompareHeaps(cpu, state, at, where);
                for (std::size_t byte = 0; byte < 4u; ++byte)
                {
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.zeroPageK5 + byte)], state.segmentStart[byte],
                                   (where + L": K5+" + std::to_wstring(byte)).c_str());
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.zeroPageK6 + byte)], state.segmentEnd[byte],
                                   (where + L": K6+" + std::to_wstring(byte)).c_str());
                }
                Assert::AreEqual(cpu.memory[at.flag], state.flag, (where + L": FLAG").c_str());
                // `CNT` is compared as the returned value above, which is where `BLINE` leaves it
                // for its two callers; the byte itself is theirs since M2-c-3.
                ++compared;
              }
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(3u * 2u * 3u * 4u * 3u, compared, L"every state and entry");
      Assert::IsTrue(marked > 0u, L"the segments were actually drawn");
    }
  };

  TEST_CLASS(ThePlanet)
  {
  public:
    /*
     * 6502: PLANET, PL9 and the PLS family -- the whole planet, run as the main loop runs it.
     *
     * Elite's planets have two looks and one bit of the system's tech level picks between them:
     * type 128 gets MERIDIANS, two great circles seen at whatever angle the planet is turned to,
     * and type 130 gets a CRATER offset along the nose vector. Both are swept, at orientations that
     * put each meridian edge-on and face-on and that take the crater over the horizon.
     *
     * Compared on the whole canvas, both heaps, and every byte the chain writes: `K`, `K2`, `K3`,
     * `K4`, `K5`, `K6`, `XX16`, `CNT`, `CNT2`, `TGT`, `STP` and `FLAG`. The heaps matter as much as
     * the picture for the reason they always do -- the next frame erases through them.
     */
    TEST_METHOD(ThePlanetMatchesPLANET)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const HeapLabels at(oracle);
      const std::uint16_t planet = oracle.Label("PLANET");

      struct Placement
      {
        std::uint8_t z[3];
        std::uint8_t nose, roof, side; // the high bytes of INWK+14, +20, +26
        const wchar_t* what;
      };

      /*
       * The orientation vectors, and the first version of this sweep had only one of them: all
       * positive, all small. `PLS1` masks a sign bit off, saturates at 254 and returns `K+3` --
       * three behaviours none of which a positive small axis can show (§6.54). These are the same
       * six bytes at four sizes and signs.
       */
      struct Orientation
      {
        std::uint8_t bytes[6]; // INWK+10, +12, +16, +18, +22, +24 -- the high halves
        const wchar_t* what;
      };

      const std::vector<Orientation> ORIENTATIONS = {
        {{0x10, 0x08, 0x18, 0x28, 0x04, 0x38}, L"small and positive"},
        {{0x90, 0x88, 0x18, 0x28, 0x84, 0x38}, L"some axes negative"},
        {{0x7F, 0x7F, 0x7F, 0x02, 0x7F, 0x01}, L"large enough to saturate"},
        {{0xFF, 0x01, 0x80, 0xFF, 0x00, 0x7F}, L"the extremes"},
      };

      /*
       * The distances are the point of this table, and the first version of it got them wrong.
       *
       * A ship's z is TWENTY-FOUR bits -- `INWK+6`, `+7` and `+8` -- and the radius is
       * `96 * 256 * 256 / z`, so `K+1` is zero only above z = 24576. Every placement in the first
       * sweep was nearer than that, `PL9`'s `LDA K+1 / BEQ PL25` skipped the markings on all 54,
       * and the whole-canvas comparison passed while half the unit never ran (§6.52). A planet in
       * Elite is normally hundreds of thousands of units away.
       */
      const std::vector<Placement> PLACEMENTS = {
        {{0, 0x70, 0}, 0x60, 0x00, 0x00, L"filling the view"},
        {{0, 0, 1}, 0x60, 0x00, 0x00, L"close, level"},
        {{0, 0, 2}, 0x60, 0x00, 0x00, L"mid, level"},
        {{0, 0, 8}, 0x60, 0x00, 0x00, L"far, level"},
        {{0, 0, 24}, 0x60, 0x00, 0x00, L"a speck, too small for meridians"},
        // The radius is 96*256*256/z, so these three straddle `PL9`'s `CMP #6` exactly: 6, 5, 4.
        {{0x00, 0x00, 0x10}, 0x60, 0x00, 0x00, L"a radius of six, the smallest with meridians"},
        {{0x00, 0x50, 0x12}, 0x60, 0x00, 0x00, L"a radius of five, the largest without"},
        {{0x00, 0x00, 0x14}, 0x60, 0x00, 0x00, L"a radius of four"},
        {{0, 0, 2}, 0x60, 0x90, 0x00, L"its crater on the far side"},
        {{0, 0, 48}, 0x60, 0x00, 0x00, L"exactly at the distance limit"},
        {{0, 2, 0}, 0x60, 0x00, 0x00, L"too close for its own markings"},
        {{0, 0, 60}, 0x60, 0x00, 0x00, L"beyond the distance byte"},
        {{0, 0, 0}, 0x60, 0x00, 0x00, L"no distance at all"},
        {{0, 0, 2}, 0x00, 0x60, 0x00, L"turned a quarter"},
        {{0, 0, 2}, 0xE0, 0x00, 0x00, L"turned away"},
        {{0, 0, 2}, 0x40, 0x40, 0x40, L"turned every way"},
        {{0, 0, 1}, 0x7F, 0x10, 0x20, L"close and tilted"},
      };

      std::uint32_t compared = 0;
      std::uint32_t marked = 0;
      std::uint32_t suns = 0;
      std::uint32_t meridians = 0;
      std::uint32_t craters = 0;
      std::uint32_t plain = 0;

      for (const Placement& where : PLACEMENTS)
      {
        for (const Orientation& turned : ORIENTATIONS)
        {
          for (const std::uint8_t type : {128, 129, 130})
          {
            for (const std::uint8_t detail : {0x00u, 0xFFu})
            {
              Cpu6502 cpu = oracle.Fresh();
              Elite::Canvas canvas;
              Elite::PlanetSunState state;
              Elite::GeometryWorkspace geometry;
              Elite::MathWorkspace math;
              Elite::ClipState clip;
              Elite::Projection centre;
              Elite::Ship ship{};
              Elite::Rng rng;

              // The sun draws for real now, and its ragged edge comes from `DORND` -- so the
              // generator is part of the comparison, seeded identically on both sides.
              const std::array<std::uint8_t, 4> seed = {0x49, 0x2B, 0x71, 0xC3};
              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(oracle.Label("RAND") + byte)] = seed[byte];
              }
              rng.SetState(seed);

              SeedBallHeap(cpu, state, at, 0x8D14E703u, 1);
              SeedSunHeap(cpu, state, at, 0x2C6A91B7u);

              // A planet's block: a position, and three orientation vectors as (lo, hi) pairs with
              // the magnitude in the HIGH byte -- which is §6.39's lesson, and the reason this sweep
              // sets the high bytes rather than the low ones.
              ship.x.lo = 0;
              PokeShip(ship, Elite::SHIP_X_OFFSET + 1, 1);
              ship.y.lo = 0;
              PokeShip(ship, Elite::SHIP_Y_OFFSET + 1, 1);
              for (std::size_t byte = 0; byte < 3u; ++byte)
              {
                PokeShip(ship, Elite::SHIP_Z_OFFSET + byte, where.z[byte]);
              }
              ship.nose.x.hi = turned.bytes[0];
              ship.nose.y.hi = turned.bytes[1];
              ship.nose.z.hi = where.nose;
              ship.roof.x.hi = turned.bytes[2];
              ship.roof.y.hi = turned.bytes[3];
              ship.roof.z.hi = where.roof;
              ship.side.x.hi = turned.bytes[4];
              ship.side.y.hi = turned.bytes[5];
              ship.side.z.hi = where.side;
              ship.nose.x.lo = 0x20;
              ship.nose.y.lo = 0x30;
              ship.nose.z.lo = 0x40;
              ship.roof.x.lo = 0x50;
              ship.roof.y.lo = 0x60;
              ship.roof.z.lo = 0x70;
              ship.side.x.lo = 0x80;
              ship.side.y.lo = 0x90;
              ship.side.z.lo = 0xA0;

              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)] = ship.ToBytes()[byte];
              }

              cpu.memory[at.type] = type;
              cpu.memory[at.planetDetail] = detail;
              cpu.memory[at.yx2m1] = 143;
              cpu.memory[at.clippingOff] = 0;
              state.planetDetail = detail;
              state.lowestVisibleRow = 143;
              clip.clippingOff = 0;

              // §6.36: `TGT` is 31 only if a meridian was walked and 64 only if a crater was, so
              // seeding it to neither is what turns "the screens agree" into "and something ran".
              // Since M2-c-3 the byte is `PLS22`'s parameter, so the counters below read the
              // ORACLE's copy: it is the sweep's coverage they measure, not the port's answer.
              cpu.memory[at.tgt] = 200;

              const Elite::Testing::RunResult run = cpu.CallSubroutine(planet, 20'000'000);
              Assert::IsTrue(run.completed, L"PLANET returned");

              Elite::DrawPlanetOrSun(canvas, state, geometry, math, clip, rng, ship, centre, Elite::TypeOf(type));

              const std::wstring label =
                Widen("PLANET type=" + std::to_string(type) + " pltog=" + std::to_string(detail) + " ") + where.what + L" / " + turned.what;

              marked += CompareScreens(cpu, at.screen, canvas, label);
              CompareHeaps(cpu, state, at, label);

              const std::pair<std::uint16_t, std::uint8_t> BYTES[] = {
                {at.zeroPageK3, centre.x},   {static_cast<std::uint16_t>(at.zeroPageK3 + 1), centre.x1},
                {at.k4, centre.y},   {static_cast<std::uint16_t>(at.k4 + 1), centre.y1},
                {at.circleStep, state.circleStep}, {at.flag, state.flag},
              };
              // `CNT`, `CNT2` and `TGT` were compared here until M2-c-3 and are the ellipse walk's
              // own now -- where it is, what angle it is at, and where it stops. `BLINE` returns
              // `CNT` to its two callers and the sweep above compares that; the other two are
              // arguments `PLS2`, `PL26` and `PLS4` choose, and what they produced is the screen
              // and the heap.
              static const wchar_t* NAMES[] = {L"K3", L"K3+1", L"K4", L"K4+1", L"STP", L"FLAG"};
              for (std::size_t byte = 0; byte < std::size(BYTES); ++byte)
              {
                Assert::AreEqual(cpu.memory[BYTES[byte].first], BYTES[byte].second, (label + L": " + NAMES[byte]).c_str());
              }
              // `K` is not compared here: `PLS22` used it as scratch for two of its products and the
              // port keeps those as locals since M2-b, so after a planet with markings the two `K`s
              // differ by design. The radius the routine reads and clamps is the first byte, which
              // the sun's and the crater's paths above pin through the screen.
              // `K2`'s bottom byte alone since M2-c-3: the other three are the ellipse's axes and
              // travel as an `EllipseAxes` value, while this one outlives the frame on purpose --
              // `MV40` reads it for the carry of its first addition without ever writing it (§8).
              Assert::AreEqual(cpu.memory[at.zeroPageK2], math.k2Low, (label + L": K2").c_str());
              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.zeroPageK5 + byte)], state.segmentStart[byte],
                                 (label + L": K5+" + std::to_wstring(byte)).c_str());
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.zeroPageK6 + byte)], state.segmentEnd[byte],
                                 (label + L": K6+" + std::to_wstring(byte)).c_str());
              }
              for (std::size_t byte = 0; byte < 6u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.scaledOrientation + byte)], geometry.scaledOrientation[byte],
                                 (label + L": XX16+" + std::to_wstring(byte)).c_str());
              }

              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("RAND") + byte)], rng.State()[byte],
                                 (label + L": RAND+" + std::to_wstring(byte)).c_str());
              }
              suns += (type == 129u) ? 1u : 0u;
              meridians += (cpu.memory[at.tgt] == 31u) ? 1u : 0u;
              craters += (cpu.memory[at.tgt] == 64u) ? 1u : 0u;
              plain += (cpu.memory[at.tgt] == 200u) ? 1u : 0u;
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(17u * 4u * 3u * 2u, compared, L"every placement, orientation, type and toggle");
      Assert::IsTrue(marked > 0u, L"the planets were actually drawn");
      Logger::WriteMessage(("PLANET: " + std::to_string(compared) + " planets, " + std::to_string(suns) + " suns, " +
                            std::to_string(meridians) + " meridians, " + std::to_string(craters) + " craters, " + std::to_string(plain) +
                            " plain, " + std::to_string(marked) + " marked bytes")
                             .c_str());
      Assert::IsTrue(suns > 0u, L"and the sun seam was reached");
      Assert::IsTrue(meridians > 0u, L"some planets were drawn with meridians");
      Assert::IsTrue(craters > 0u, L"and some with a crater");
      Assert::IsTrue(plain > 0u, L"and some with neither");
    }
  };

  TEST_CLASS(TheSun)
  {
  public:
    /*
     * 6502: SUN, run as the main loop runs it -- several frames in a row with the sun moving.
     *
     * One call exercises only `PLF11`, the path for a row that had nothing on it. The routine's
     * whole design is the other path: for each row it holds LAST frame's half-width and THIS
     * frame's, clips both -- against last frame's centre and this frame's respectively -- and draws
     * only the two slivers that differ. A sun drifting across the screen therefore costs two short
     * lines per row instead of two long ones, and that is only visible over consecutive frames
     * (§6.33).
     *
     * `SUNX` is the old centre and `K3` the new one, and the routine ends by copying one into the
     * other -- so the comparison has to include it or the next frame's arithmetic is untested.
     */
    TEST_METHOD(TheSunMatchesSUN)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const HeapLabels at(oracle);
      const std::uint16_t sun = oracle.Label("SUN");
      const std::uint16_t rand = oracle.Label("RAND");

      struct Drift
      {
        std::uint16_t x, y;
        std::int8_t dx, dy;
        std::uint8_t radius, grow;
        const wchar_t* what;
      };

      const std::vector<Drift> DRIFTS = {
        {128, 72, 0, 0, 40, 0, L"still"},
        {128, 72, 9, 0, 40, 0, L"drifting across"},
        {128, 72, 0, 7, 40, 0, L"drifting down"},
        {128, 72, 5, 5, 20, 6, L"coming closer"},
        {128, 72, -6, -3, 100, 0, L"large and drifting back"},
        {30, 20, 4, 4, 96, 0, L"off the top left corner"},
        {250, 130, -4, 2, 96, 0, L"off the bottom right"},
        {128, 72, 0, 0, 8, 0, L"a smooth speck"},
        {128, 72, 0, 0, 200, 0, L"filling the screen"},

        /*
         * `SUN`'s first thirty instructions are all about WHERE the centre is relative to the
         * screen, and a table of suns comfortably on it reaches none of them: the negation for a
         * centre below the bottom row, the clamp for one further than a byte above the top, the
         * exactly-on-the-bottom-row case, and the saturation when a half-width plus its ragged
         * bits passes 255. Six more drifts, one per branch (§6.56).
         */
        {128, 200, 0, 0, 96, 0, L"centred below the screen"},
        {128, 0xFF00u, 0, 0, 96, 0, L"centred far above it"},
        {128, 143, 0, 0, 60, 0, L"centred exactly on the bottom row"},
        {128, 72, 0, 0, 255, 0, L"wide enough to saturate"},
        {0xFF80u, 72, 6, 0, 96, 0, L"off the left edge entirely"},
        {128, 30, 3, 3, 30, 0, L"its bottom edge on row zero"},

        /*
         * And five more, each worked out from a branch rather than guessed at. `CHKON` has to
         * accept every one of them, which is what makes them awkward: a centre off the screen is
         * only reachable when the radius is large enough for the edge to come back on.
         */
        {128, 0, 0, 0, 0, 0, L"a radius of nothing on row zero"},
        {128, 0xFF8Fu, 0, 0, 120, 0, L"centred a whole byte above the screen"},
        {20, 72, 100, 0, 30, 0, L"jumping clear of where it was"},
        {0x0110u, 72, 0, 0, 60, 0, L"centred off the right edge, its own edge on screen"},
        {128, 110, 5, -8, 25, 0, L"climbing, so its old rows are above the new ones"},
      };

      std::uint32_t compared = 0;
      std::uint32_t marked = 0;
      std::uint32_t redrawn = 0;

      for (const Drift& drift : DRIFTS)
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        Elite::PlanetSunState state;
        Elite::MathWorkspace math;
        Elite::Projection centre;
        Elite::Rng rng;

        SeedSunHeap(cpu, state, at, 0x2C6A91B7u);
        SeedBallHeap(cpu, state, at, 0x8D14E703u, 1);
        state.sun[0] = 0xFF;
        cpu.memory[at.lso] = 0xFF;

        const std::array<std::uint8_t, 4> seed = {0x71, 0xC3, 0x49, 0x2B};
        for (std::size_t byte = 0; byte < 4u; ++byte)
        {
          cpu.memory[static_cast<std::uint16_t>(rand + byte)] = seed[byte];
        }
        rng.SetState(seed);

        state.sunX = static_cast<std::uint8_t>(drift.x);
        state.sunXNext = static_cast<std::uint8_t>(drift.x >> 8);
        cpu.memory[at.sunx] = state.sunX;
        cpu.memory[static_cast<std::uint16_t>(at.sunx + 1)] = state.sunXNext;

        state.lowestVisibleRow = 143;
        cpu.memory[at.yx2m1] = 143;

        std::uint16_t x = drift.x;
        std::uint16_t y = drift.y;
        std::uint8_t radius = drift.radius;

        // Four frames, because the second is the first one this routine can do its real work on.
        for (int frame = 0; frame < 4; ++frame)
        {
          centre.x = static_cast<std::uint8_t>(x);
          centre.x1 = static_cast<std::uint8_t>(x >> 8);
          centre.y = static_cast<std::uint8_t>(y);
          centre.y1 = static_cast<std::uint8_t>(y >> 8);


          cpu.memory[at.zeroPageK3] = centre.x;
          cpu.memory[static_cast<std::uint16_t>(at.zeroPageK3 + 1)] = centre.x1;
          cpu.memory[at.k4] = centre.y;
          cpu.memory[static_cast<std::uint16_t>(at.k4 + 1)] = centre.y1;
          cpu.memory[at.zeroPageK] = radius;

          const Elite::Testing::RunResult run = cpu.CallSubroutine(sun, 20'000'000);
          Assert::IsTrue(run.completed, L"SUN returned");

          Elite::DrawSun(canvas, state, math, rng, centre, radius);

          const std::wstring label = std::wstring(drift.what) + Widen(" frame=" + std::to_string(frame));
          marked += CompareScreens(cpu, at.screen, canvas, label);
          CompareHeaps(cpu, state, at, label);

          Assert::AreEqual(cpu.memory[at.sunx], state.sunX, (label + L": SUNX").c_str());
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.sunx + 1)], state.sunXNext, (label + L": SUNX+1").c_str());
          // `CNT` and `TGT` are `SUN`'s own since M2-c-3 -- the raggedness mask it ANDs each
          // row's random byte with, and the row it stops at. Both are derived from `K` and
          // `CHKON`'s answer inside this one routine, and what they produced is the sun on the
          // screen and the widths on the heap, compared above over ten frames of drift.
          for (std::size_t byte = 0; byte < 4u; ++byte)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(rand + byte)], rng.State()[byte],
                             (label + L": RAND+" + std::to_wstring(byte)).c_str());
          }

          // `K2`'s bottom byte alone since M2-c-3 -- the radius squared's low half, which `SUN`
          // reads back inside the row loop and `MV40` reads a frame later (§8).
          Assert::AreEqual(cpu.memory[at.zeroPageK2], math.k2Low, (label + L": K2").c_str());

          // A frame that found something already on the heap is one that took the difference path.
          if (frame > 0)
          {
            ++redrawn;
          }

          x = static_cast<std::uint16_t>(x + drift.dx);
          y = static_cast<std::uint16_t>(y + drift.dy);
          radius = static_cast<std::uint8_t>(radius + drift.grow);
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(20u * 4u, compared, L"every drift, four frames each");
      Assert::IsTrue(marked > 0u, L"the sun was actually drawn");
      Assert::IsTrue(redrawn > 0u, L"and redrawn over itself");
      Logger::WriteMessage(("SUN: " + std::to_string(compared) + " frames, " + std::to_string(marked) + " marked bytes").c_str());
    }
  };

  TEST_CLASS(TheBubbleReset)
  {
  public:
    /// 6502: ZINF -- clear a block and square it to the axes.
    TEST_METHOD(TheBlockIsClearedTheSameWay)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const HeapLabels at(oracle);

      Cpu6502 cpu = oracle.Fresh();
      Elite::Ship work{};

      // Fill both sides with something, so "cleared" means cleared rather than "already zero".
      std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        const std::uint8_t value = static_cast<std::uint8_t>(0x5Au + byte * 7u);
        shipBytes[byte] = value;
        cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)] = value;
      }
      work = Elite::Ship::FromBytes(shipBytes);

      Assert::IsTrue(cpu.CallSubroutine(oracle.Label("ZINF"), 20'000).completed, L"ZINF returned");
      Elite::ClearShip(work);

      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)], work.ToBytes()[byte],
                         (L"ZINF: INWK+" + std::to_wstring(byte)).c_str());
      }
    }

    /*
     * 6502: NWSTARS, nWq and WPSHPS -- one fall-through chain the ledger lists as four rows.
     *
     * The stardust half is compared on the whole canvas, all six arrays and the generator's four
     * state bytes, because each speck's first random byte runs on the carry the PREVIOUS speck's
     * plot left. The ship half is compared on every slot's state byte and on the SCANNER ITSELF,
     * because `WPSHPS` masks the byte in the SLOT and leaves the copy in `INWK` alone.
     *
     * `SCAN` was a seam here until slice 3d-a built it, and the seam's recording is replaced by the
     * canvas comparison that was already running -- so the blips are now compared where they land
     * rather than counted where they were asked for (§6.61).
     */
    TEST_METHOD(TheFieldAndTheShipsAreResetTheSameWay)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const HeapLabels at(oracle);
      const std::uint16_t nwstars = oracle.Label("NWSTARS");
      const std::uint16_t rand = oracle.Label("RAND");
      const std::uint16_t frin = oracle.Label("FRIN");
      const std::uint16_t kPercent = oracle.Label("K%");
      const std::uint16_t nostm = oracle.Label("NOSTM");

      const std::vector<std::vector<std::uint8_t>> FLEETS = {
        {}, {1, 0}, {3, 9, 2, 0}, {128, 5, 129, 7, 0}, {2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
      };

      std::uint32_t compared = 0;
      std::uint32_t marked = 0;
      std::uint32_t scannedTotal = 0;
      std::uint32_t replaced = 0;

      for (const std::vector<std::uint8_t>& fleet : FLEETS)
      {
        for (const std::uint8_t viewType : {0u, 1u, 4u})
        {
          for (const std::uint8_t count : {3, 12})
          {
            for (const bool carryIn : {false, true})
            {
              Cpu6502 cpu = oracle.Fresh();
              Elite::Canvas canvas;
              Elite::Stardust dust;
              Elite::Rng rng;
              Elite::PlanetSunState state;
              Elite::Bubble bubble;
              Elite::Ship work{};
              Elite::FlightState flight;

              SeedSunHeap(cpu, state, at, 0x2C6A91B7u);
              SeedBallHeap(cpu, state, at, 0x8D14E703u, 40);

              dust.count = count;
              cpu.memory[nostm] = count;
              for (std::size_t slot = 0; slot < Elite::STARDUST_SLOTS; ++slot)
              {
                const std::uint8_t value = static_cast<std::uint8_t>(0x31u + slot * 11u);
                dust.x[slot] = dust.y[slot] = dust.z[slot] = value;
                cpu.memory[static_cast<std::uint16_t>(oracle.Label("SX") + slot)] = value;
                cpu.memory[static_cast<std::uint16_t>(oracle.Label("SY") + slot)] = value;
                cpu.memory[static_cast<std::uint16_t>(oracle.Label("SZ") + slot)] = value;
              }

              const std::array<std::uint8_t, 4> seed = {0x2B, 0x49, 0xC3, 0x71};
              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(rand + byte)] = seed[byte];
              }
              rng.SetState(seed);

              // The fleet, in both FRIN and the K% blocks it points at.
              for (std::size_t slot = 0; slot < fleet.size() && slot < Elite::MAX_SHIPS; ++slot)
              {
                bubble.slots[slot] = fleet[slot];
                cpu.memory[static_cast<std::uint16_t>(frin + slot)] = fleet[slot];
                std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = bubble.blocks[slot].ToBytes();
                for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
                {
                  const std::uint8_t value = static_cast<std::uint8_t>(0x11u + slot * 13u + byte * 3u);
                  shipBytes[byte] = value;
                  cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
                }
                bubble.blocks[slot] = Elite::Ship::FromBytes(shipBytes);
              }

              std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                const std::uint8_t value = static_cast<std::uint8_t>(0xC7u - byte * 5u);
                shipBytes[byte] = value;
                cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)] = value;
              }
              work = Elite::Ship::FromBytes(shipBytes);

              cpu.memory[at.qq11] = viewType;
              cpu.c = carryIn;

              const Elite::Testing::RunResult run = cpu.CallSubroutine(nwstars, 200'000);
              Assert::IsTrue(run.completed, L"NWSTARS returned");

              Elite::SeedStardustAndClearShips(canvas, dust, rng, state, bubble, work, flight, viewType, carryIn);

              const std::wstring where = Widen("NWSTARS ships=" + std::to_string(fleet.size()) + " view=" + std::to_string(viewType) +
                                               " count=" + std::to_string(count) + " carry=" + std::to_string(carryIn ? 1 : 0));

              marked += CompareScreens(cpu, at.screen, canvas, where);
              CompareHeaps(cpu, state, at, where);

              for (std::size_t slot = 0; slot < Elite::STARDUST_SLOTS; ++slot)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("SX") + slot)], dust.x[slot],
                                 (where + L": SX").c_str());
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("SY") + slot)], dust.y[slot],
                                 (where + L": SY").c_str());
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("SZ") + slot)], dust.z[slot],
                                 (where + L": SZ").c_str());
              }
              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(rand + byte)], rng.State()[byte],
                                 (where + L": RAND+" + std::to_wstring(byte)).c_str());
              }
              for (std::size_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
              {
                Assert::AreEqual(
                  cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + Elite::SHIP_STATE_OFFSET)],
                  bubble.blocks[slot].state, (where + L": the slot's state byte").c_str());
              }

              // `WPSHPS` copies THIRTY-TWO bytes into `INWK`, not the whole block, so the four
              // above them keep whatever was there. Comparing the copy is what measures that.
              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)], work.ToBytes()[byte],
                                 (where + L": INWK+" + std::to_wstring(byte)).c_str());
              }

              // 6502: STA TYPE / STX XSAV -- the two globals `WPSHPS` writes for `SCAN`, and
              // they are left holding the LAST ship the loop reached.
              Assert::AreEqual(cpu.memory[oracle.Label("TYPE")], Elite::Byte(flight.type), (where + L": TYPE after the walk").c_str());
              Assert::AreEqual(cpu.memory[oracle.Label("XSAV")], flight.slot, (where + L": XSAV after the walk").c_str());

              /*
               * §6.36 again: the arrays are seeded to fixed values on both sides, so "they agree"
               * is also what a routine that did nothing would produce. The space view must
               * replace every speck and a menu view must replace none.
               */
              std::uint32_t changed = 0;
              for (std::size_t slot = 1; slot <= count; ++slot)
              {
                const std::uint8_t was = static_cast<std::uint8_t>(0x31u + slot * 11u);
                changed += (dust.x[slot] != was || dust.y[slot] != was || dust.z[slot] != was) ? 1u : 0u;
              }
              if (viewType == 0u)
              {
                Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(count), changed, (where + L": every speck replaced").c_str());
                replaced += changed;
              }
              else
              {
                Assert::AreEqual<std::uint32_t>(0u, changed, (where + L": a menu keeps its field").c_str());
              }

              /*
               * And that the blips actually landed, which the seam count used to stand in for.
               * The scanner lives below row 146, so anything set down there came from `SCAN`.
               */
              const std::span<const std::uint8_t> plotted = canvas.Screen();
              for (std::uint16_t offset = Elite::Canvas::RowOffset(146); offset < Elite::Canvas::BITMAP_SIZE; ++offset)
              {
                scannedTotal += (plotted[offset] != 0u) ? 1u : 0u;
              }

              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(5u * 3u * 2u * 2u, compared, L"every fleet and view");
      Assert::IsTrue(marked > 0u, L"the new field was actually drawn");
      Assert::IsTrue(scannedTotal > 0u, L"and some blips were actually drawn on the scanner");
      Assert::IsTrue(replaced > 0u, L"and the field was actually replaced");
      Logger::WriteMessage(("NWSTARS: " + std::to_string(compared) + " resets, " + std::to_string(scannedTotal) + " scanned, " +
                            std::to_string(marked) + " marked bytes")
                             .c_str());
    }
  };

} // namespace GameLogicTests

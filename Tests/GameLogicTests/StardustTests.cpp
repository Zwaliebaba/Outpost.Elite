#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "ShipMove.h"
#include "Stardust.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The stardust (slice 3c).
 *
 * Elite's whole sense of motion is twelve specks in a box, moved and redrawn every frame, and
 * each of the three views moves them differently. These compare the arithmetic wrappers one at a
 * time and then the movers whole -- on the entire canvas, the entire particle field and the
 * generator's state, because a mover that gets one speck's replacement wrong is a mover that
 * diverges for ever after.
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

    /// Where the six parallel arrays live, and the flight bytes the movers read.
    struct DustLabels
    {
      std::uint16_t sx = 0, sxl = 0, sy = 0, syl = 0, sz = 0, szl = 0, nostm = 0;
      std::uint16_t xx = 0, yy = 0, newzp = 0, rand = 0;
      std::uint16_t alpha = 0, alp1 = 0, alp2 = 0, beta = 0, bet1 = 0, bet2 = 0;
      std::uint16_t delta = 0, delt4 = 0, rat = 0, rat2 = 0;
      std::uint16_t p = 0, q = 0, r = 0, s = 0, t = 0, x1 = 0, y1 = 0, zz = 0;
      std::uint16_t view = 0, screen = 0;

      explicit DustLabels(const OracleImage& _oracle)
      {
        sx = _oracle.Label("SX");
        sxl = _oracle.Label("SXL");
        sy = _oracle.Label("SY");
        syl = _oracle.Label("SYL");
        sz = _oracle.Label("SZ");
        szl = _oracle.Label("SZL");
        nostm = _oracle.Label("NOSTM");
        xx = _oracle.Label("XX");
        yy = _oracle.Label("YY");
        newzp = _oracle.Label("newzp");
        rand = _oracle.Label("RAND");
        alpha = _oracle.Label("ALPHA");
        alp1 = _oracle.Label("ALP1");
        alp2 = _oracle.Label("ALP2");
        beta = _oracle.Label("BETA");
        bet1 = _oracle.Label("BET1");
        bet2 = _oracle.Label("BET2");
        delta = _oracle.Label("DELTA");
        delt4 = _oracle.Label("DELT4");
        rat = _oracle.Label("RAT");
        rat2 = _oracle.Label("RAT2");
        p = _oracle.Label("P");
        q = _oracle.Label("Q");
        r = _oracle.Label("R");
        s = _oracle.Label("S");
        t = _oracle.Label("T");
        x1 = _oracle.Label("X1");
        y1 = _oracle.Label("Y1");
        zz = _oracle.Label("ZZ");
        view = _oracle.Label("VIEW");

        const Cpu6502 cpu = _oracle.Fresh();
        const std::uint16_t low = _oracle.Label("ylookupl");
        const std::uint16_t high = _oracle.Label("ylookuph");
        screen = static_cast<std::uint16_t>((cpu.memory[low] | (cpu.memory[high] << 8)) - 0x20u);
      }
    };

    /// One field of specks, put into both sides identically. The values are chosen so that some
    /// specks survive the frame and some are killed at each of the three edges.
    void SeedField(Cpu6502& _cpu, Elite::Stardust& _dust, const DustLabels& _at, std::uint8_t _count, std::uint32_t _seed)
    {
      _dust.count = _count;
      _cpu.memory[_at.nostm] = _count;

      std::uint32_t state = _seed;
      for (std::size_t slot = 0; slot < Elite::STARDUST_SLOTS; ++slot)
      {
        const auto next = [&state]()
        {
          state = state * 1664525u + 1013904223u;
          return static_cast<std::uint8_t>(state >> 24);
        };

        const std::uint8_t values[6] = {next(), next(), next(), next(), static_cast<std::uint8_t>(next() | 0x20u), next()};
        _dust.x[slot] = values[0];
        _cpu.memory[static_cast<std::uint16_t>(_at.sx + slot)] = values[0];
        _dust.xLow[slot] = values[1];
        _cpu.memory[static_cast<std::uint16_t>(_at.sxl + slot)] = values[1];
        _dust.y[slot] = values[2];
        _cpu.memory[static_cast<std::uint16_t>(_at.sy + slot)] = values[2];
        _dust.yLow[slot] = values[3];
        _cpu.memory[static_cast<std::uint16_t>(_at.syl + slot)] = values[3];
        _dust.z[slot] = values[4];
        _cpu.memory[static_cast<std::uint16_t>(_at.sz + slot)] = values[4];
        _dust.zLow[slot] = values[5];
        _cpu.memory[static_cast<std::uint16_t>(_at.szl + slot)] = values[5];
      }
    }

    void CompareField(const Cpu6502& _cpu, const Elite::Stardust& _dust, const DustLabels& _at, const std::wstring& _where)
    {
      for (std::size_t slot = 0; slot < Elite::STARDUST_SLOTS; ++slot)
      {
        const std::wstring which = _where + L" slot " + std::to_wstring(slot);
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.sx + slot)], _dust.x[slot], (which + L": SX").c_str());
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.sxl + slot)], _dust.xLow[slot], (which + L": SXL").c_str());
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.sy + slot)], _dust.y[slot], (which + L": SY").c_str());
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.syl + slot)], _dust.yLow[slot], (which + L": SYL").c_str());
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.sz + slot)], _dust.z[slot], (which + L": SZ").c_str());
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.szl + slot)], _dust.zLow[slot], (which + L": SZL").c_str());
      }
    }

    /// One set of flight bytes, put into both sides. `ALP2+1` and `BET2+1` are seeded as the exact
    /// complements they always are in flight, so that `ST2`'s normalisation is not what is being
    /// measured.
    struct Flight
    {
      std::uint8_t delta, delt4, delt4Next, alpha, alp1, alp2, beta, bet1, bet2;
      const wchar_t* what;
    };

    const std::vector<Flight>& Flights()
    {
      static const std::vector<Flight> FLIGHTS = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, L"stationary and level"},
        {20, 80, 0, 0, 0, 0, 0, 0, 0, L"cruising straight"},
        {31, 124, 0, 12, 12, 0, 0, 0, 0, L"rolling right"},
        {31, 124, 0, 0xF4, 12, 0x80, 0, 0, 0, L"rolling left"},
        {8, 32, 0, 0, 0, 0, 10, 10, 0, L"pitching up"},
        {8, 32, 0, 0, 0, 0, 0x8A, 10, 0x80, L"pitching down"},
        {31, 124, 0, 12, 12, 0, 10, 10, 0, L"rolling and pitching at speed"},
      };
      return FLIGHTS;
    }

    void SeedFlight(Cpu6502& _cpu, Elite::FlightState& _state, const DustLabels& _at, const Flight& _flight)
    {
      const std::uint8_t alp2Next = static_cast<std::uint8_t>(_flight.alp2 ^ 0x80u);
      const std::uint8_t bet2Next = static_cast<std::uint8_t>(_flight.bet2 ^ 0x80u);

      _cpu.memory[_at.delta] = _flight.delta;
      _cpu.memory[_at.delt4] = _flight.delt4;
      _cpu.memory[static_cast<std::uint16_t>(_at.delt4 + 1)] = _flight.delt4Next;
      _cpu.memory[_at.alpha] = _flight.alpha;
      _cpu.memory[_at.alp1] = _flight.alp1;
      _cpu.memory[_at.alp2] = _flight.alp2;
      _cpu.memory[static_cast<std::uint16_t>(_at.alp2 + 1)] = alp2Next;
      _cpu.memory[_at.beta] = _flight.beta;
      _cpu.memory[_at.bet1] = _flight.bet1;
      _cpu.memory[_at.bet2] = _flight.bet2;
      _cpu.memory[static_cast<std::uint16_t>(_at.bet2 + 1)] = bet2Next;

      _state.delta = _flight.delta;
      _state.delt4 = _flight.delt4;
      _state.delt4Next = _flight.delt4Next;
      _state.alpha = _flight.alpha;
      _state.alp1 = _flight.alp1;
      _state.alp2 = _flight.alp2;
      _state.alp2Next = alp2Next;
      _state.beta = _flight.beta;
      _state.bet1 = _flight.bet1;
      _state.bet2 = _flight.bet2;
      _state.bet2Next = bet2Next;
    }

    /// The bytes the side views WRITE. `ST2` turns the roll and the pitch over on the way in and back
    /// on the way out, and leaves `RAT`, `RAT2` and both complements set, so a whole call is only
    /// almost a no-op on the flight state -- and `newzp` survives it too.
    void CompareFlight(const Cpu6502& _cpu, const Elite::FlightState& _state, const DustLabels& _at, const std::wstring& _where)
    {
      const std::pair<std::uint16_t, std::uint8_t> BYTES[] = {
        {_at.alpha, _state.alpha},
        {_at.alp2, _state.alp2},
        {static_cast<std::uint16_t>(_at.alp2 + 1), _state.alp2Next},
        {_at.bet2, _state.bet2},
        {static_cast<std::uint16_t>(_at.bet2 + 1), _state.bet2Next},
        {_at.rat, _state.rat},
        {_at.rat2, _state.rat2},
      };
      static const wchar_t* NAMES[] = {L"ALPHA", L"ALP2", L"ALP2+1", L"BET2", L"BET2+1", L"RAT", L"RAT2"};

      for (std::size_t byte = 0; byte < std::size(BYTES); ++byte)
      {
        Assert::AreEqual(_cpu.memory[BYTES[byte].first], BYTES[byte].second, (_where + L": " + NAMES[byte]).c_str());
      }
    }

    void SeedRandom(Cpu6502& _cpu, Elite::Rng& _rng, const DustLabels& _at, const std::array<std::uint8_t, 4>& _seed)
    {
      for (std::size_t byte = 0; byte < 4u; ++byte)
      {
        _cpu.memory[static_cast<std::uint16_t>(_at.rand + byte)] = _seed[byte];
      }
      _rng.SetState(_seed);
    }

    void CompareRandom(const Cpu6502& _cpu, const Elite::Rng& _rng, const DustLabels& _at, const std::wstring& _where)
    {
      for (std::size_t byte = 0; byte < 4u; ++byte)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.rand + byte)], _rng.State()[byte],
                         (_where + L": RAND+" + std::to_wstring(byte)).c_str());
      }
    }

    void CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas, const std::wstring& _where)
    {
      const std::span<const std::uint8_t> ours = _canvas.Screen();
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
        if (expected != ours[offset])
        {
          Assert::Fail((_where + L": the screen differs at offset " + std::to_wstring(offset) + L" -- game has " +
                        std::to_wstring(expected) + L", port has " + std::to_wstring(ours[offset]))
                         .c_str());
        }
      }
    }
  } // namespace

  TEST_CLASS(TheStardustArithmetic)
  {
  public:
    /*
     * 6502: DV41, DV42, MLU1, MLS1, MLS2, MUT1, MUT2 and MULTS-2.
     *
     * Each is one or two instructions and a fall-through, so what is being checked is the SETUP --
     * which byte goes into which zero-page slot before the shared body runs. Get one of those
     * wrong and the stardust still moves, plausibly, in the wrong direction.
     */
    TEST_METHOD(TheWrappersMatchTheirEntryPoints)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const DustLabels at(oracle);

      const std::vector<std::uint8_t> EDGES = {0, 1, 2, 63, 64, 127, 128, 200, 255};

      std::uint32_t compared = 0;
      for (const std::uint8_t a : EDGES)
      {
        for (const std::uint8_t operand : EDGES)
        {
          for (int which = 0; which < 8; ++which)
          {
            Cpu6502 cpu = oracle.Fresh();
            Elite::Stardust dust;
            Elite::MathWorkspace math;
            Elite::DrawWorkspace draw;
            Elite::FlightState flight;

            // Everything each wrapper might read, on both sides.
            SeedField(cpu, dust, at, 12, 0x51F3A2C9u + operand);
            cpu.memory[at.delta] = operand;
            flight.delta = operand;
            cpu.memory[at.alp1] = operand;
            flight.alp1 = operand;
            cpu.memory[at.q] = operand;
            math.q = operand;
            cpu.memory[at.p] = a;
            math.p = a;
            cpu.memory[at.r] = operand;
            math.r = operand;
            cpu.memory[at.s] = a;
            math.s = a;
            cpu.memory[at.xx] = operand;
            math.xx = operand;
            cpu.memory[static_cast<std::uint16_t>(at.xx + 1)] = a;
            math.xxNext = a;

            const std::uint8_t slot = 5;
            cpu.y = slot;
            cpu.x = operand;
            cpu.a = a;

            std::uint8_t got = 0;
            const wchar_t* name = L"";
            switch (which)
            {
            case 0:
              name = L"DV41";
              cpu.CallSubroutine(oracle.Label("DV41"), 20'000);
              got = Elite::DivideSpeedBy(math, flight, a);
              break;
            case 1:
              name = L"DV42";
              cpu.CallSubroutine(oracle.Label("DV42"), 20'000);
              got = Elite::DivideSpeedByDistance(math, flight, dust, slot);
              break;
            case 2:
              name = L"MLU1";
              cpu.CallSubroutine(oracle.Label("MLU1"), 20'000);
              got = Elite::MultiplyByHeight(math, draw, dust, slot).high;
              break;
            case 3:
              name = L"MLS1";
              cpu.CallSubroutine(oracle.Label("MLS1"), 20'000);
              got = Elite::MultiplyByRoll(math, flight, a);
              break;
            case 4:
              name = L"MLS2";
              cpu.CallSubroutine(oracle.Label("MLS2"), 20'000);
              got = Elite::MultiplyPositionByRoll(math, flight, a);
              break;
            case 5:
              name = L"MUT1";
              cpu.CallSubroutine(oracle.Label("MUT1"), 20'000);
              got = Elite::MultiplyPosition(math, a);
              break;
            case 6:
              name = L"MUT2";
              cpu.CallSubroutine(oracle.Label("MUT2"), 20'000);
              got = Elite::MultiplyPositionSigned(math, a);
              break;
            default:
              name = L"MULTS-2";
              cpu.CallSubroutine(static_cast<std::uint16_t>(oracle.Label("MULTS") - 2), 20'000);
              got = Elite::MultiplyScaledBy(math, operand, a);
              break;
            }

            const std::wstring where = std::wstring(name) + L"(a=" + std::to_wstring(a) + L", x=" + std::to_wstring(operand) + L")";
            Assert::AreEqual(cpu.a, got, (where + L": A").c_str());
            Assert::AreEqual(cpu.memory[at.p], math.p, (where + L": P").c_str());
            Assert::AreEqual(cpu.memory[at.q], math.q, (where + L": Q").c_str());
            Assert::AreEqual(cpu.memory[at.r], math.r, (where + L": R").c_str());
            Assert::AreEqual(cpu.memory[at.s], math.s, (where + L": S").c_str());
            Assert::AreEqual(cpu.memory[at.y1], draw.y1, (where + L": Y1").c_str());
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(9u * 9u * 8u, compared, L"every wrapper over every pair");
    }
  };

  TEST_CLASS(TheStardustMovers)
  {
  public:
    /*
     * 6502: STARS1 -- the front view, run as a whole frame.
     *
     * Compared on the entire canvas, all six arrays and the generator's state. The generator
     * matters as much as the arithmetic: a speck that leaves the box is REPLACED from `DORND`, so a
     * port that took one random byte too few would agree about this frame and disagree about every
     * frame after it.
     *
     * The sweep runs several frames in a row on the same state for the same reason `MVEIT` and
     * `SHPPT` are run rather than called (§6.33): the output is the next frame's input.
     */
    TEST_METHOD(TheFrontViewMatchesSTARS1)
    {
      RunAndCompare("STARS1", 0, false);
    }

    /*
     * 6502: STARS6 -- the rear view.
     *
     * Not the front view with a sign flipped, which is what the shape of the two routines invites
     * you to assume: the coordinates are done in the other order, the roll's two sign bytes are
     * swapped, the pitch multiplies by the negated x where the front view squares its own scale
     * factor, and the kill tests are on y and distance rather than on x, y and distance. Five
     * differences, and only the first two are what "backwards" would predict.
     */
    TEST_METHOD(TheRearViewMatchesSTARS6)
    {
      RunAndCompare("STARS6", 1, false);
    }

    /*
     * 6502: STARS2 -- the two side views, which are one routine run with the angles turned over.
     *
     * This one has side effects the other two do not: `ST2` rewrites `ALPHA`, `ALP2`, `ALP2+1`,
     * `BET2` and `BET2+1` on the way in and again on the way out, and `RAT` and `RAT2` are left
     * behind. A whole call is very nearly a no-op on the flight state and not quite one, so all
     * seven bytes are compared as well.
     */
    TEST_METHOD(TheLeftViewMatchesSTARS2)
    {
      RunAndCompare("STARS2", 2, false);
    }

    TEST_METHOD(TheRightViewMatchesSTARS2)
    {
      RunAndCompare("STARS2", 3, false);
    }

    /*
     * 6502: STARS -- `LDX VIEW / BEQ STARS1 / DEX / BNE ST11 / JMP STARS6 / .ST11 JMP STARS2`.
     *
     * Six instructions, and one of them is the reason this test exists: the `DEX` means `STARS2`
     * is entered with the view ALREADY DECREMENTED, so its `CPX #2` is comparing against the LEFT
     * view rather than against 2 as a view number. Read the two routines separately and the port
     * comes out mirrored -- the dust in the left view sliding the way the right view's should.
     */
    TEST_METHOD(TheDispatcherMatchesSTARS)
    {
      for (std::uint8_t view = 0; view < 4u; ++view)
      {
        RunAndCompare("STARS", view, true);
      }
    }

  private:
    /*
     * One view, every flight, both particle counts, four consecutive frames each.
     *
     * `_through` picks the entry point: the mover itself, or `STARS` with `VIEW` set, which is what
     * makes the dispatcher's `DEX` observable.
     */
    void RunAndCompare(const char* _label, std::uint8_t _view, bool _through)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const DustLabels at(oracle);
      const std::string entry(_label);
      const std::wstring name = Widen(entry);
      const std::uint16_t routine = oracle.Label(entry.c_str());

      const int frames = _through ? 2 : 4;
      std::uint32_t compared = 0;
      std::uint32_t replaced = 0;
      std::uint32_t marked = 0;

      /*
       * Five fields rather than one. The kill tests are comparisons against a single byte -- the
       * side view's is `room == newzp`, which one field of twelve specks over a few frames simply
       * never lands on -- and a comparison never reached is a comparison the sweep does not
       * measure, whatever its screens agree about.
       */
      for (const std::uint32_t field : {0x2C6A91B7u, 0x8D14E703u, 0x51F3A2C9u, 0xA07B5E26u, 0x3FCD0841u})
      {
        for (const std::uint8_t count : {3, 12})
        {
          for (const Flight& flight : Flights())
          {
            Cpu6502 cpu = oracle.Fresh();
            Elite::Canvas canvas;
            Elite::DrawWorkspace draw;
            Elite::MathWorkspace math;
            Elite::FlightState state;
            Elite::Stardust dust;
            Elite::Rng rng;

            SeedField(cpu, dust, at, count, field);
            SeedFlight(cpu, state, at, flight);

            const std::array<std::uint8_t, 4> seed = {0x49, 0x2B, 0x71, 0xC3};
            SeedRandom(cpu, rng, at, seed);

            for (int frame = 0; frame < frames; ++frame)
            {
              // 6502: `STARS` reads VIEW; `STARS2` is entered with the view already decremented,
              // and `STARS1` and `STARS6` do not care what is in X at all.
              cpu.memory[at.view] = _view;
              cpu.x = static_cast<std::uint8_t>(_through ? _view : _view - 1u);

              const Elite::Testing::RunResult run = cpu.CallSubroutine(routine, 2'000'000);
              Assert::IsTrue(run.completed, (name + L" returned").c_str());

              if (_through)
              {
                Elite::MoveStardust(canvas, draw, math, state, dust, rng, _view);
              }
              else if (_view == 0u)
              {
                Elite::MoveStardustAhead(canvas, draw, math, state, dust, rng);
              }
              else if (_view == 1u)
              {
                Elite::MoveStardustAstern(canvas, draw, math, state, dust, rng);
              }
              else
              {
                Elite::MoveStardustSideways(canvas, draw, math, state, dust, rng, _view);
              }

              const std::wstring where =
                name +
                Widen(" view=" + std::to_string(_view) + " count=" + std::to_string(count) + " frame=" + std::to_string(frame) + " ") +
                flight.what;

              CompareScreens(cpu, at.screen, canvas, where);
              CompareField(cpu, dust, at, where);
              CompareRandom(cpu, rng, at, where);

              if (_view >= 2u)
              {
                CompareFlight(cpu, state, at, where);
                Assert::AreEqual(cpu.memory[at.newzp], dust.newzp, (where + L": newzp").c_str());
              }

              if (rng.State() != seed)
              {
                ++replaced;
              }
              for (const std::uint8_t byte : canvas.Screen())
              {
                marked += (byte != 0u) ? 1u : 0u;
              }
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(5 * 2 * 7 * frames), compared, L"every field, every flight, every frame");
      Assert::IsTrue(replaced > 0u, L"a speck left the box and was replaced");

      // §6.36: a screen comparison over two blank screens passes and measures nothing.
      Assert::IsTrue(marked > 0u, L"the dust was actually drawn");
      Logger::WriteMessage((entry + ": " + std::to_string(compared) + " frames, " + std::to_string(marked) + " marked bytes").c_str());
    }
  };

} // namespace GameLogicTests

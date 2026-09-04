#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "Dashboard.h"
#include "LookupTables.h"
#include "Scanner.h"
#include "ShipDraw.h"
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
 * The dashboard (slice 3d-b).
 *
 * Compared on the BITMAP and on the screen-RAM cells, because the dials are one and the bulbs are
 * the other. The dials also STORE rather than EOR, so a case that drew nothing and a case that
 * drew the same thing twice are distinguishable here in a way they are not in the space view --
 * which is why the sweeps below start from a screen full of a marker byte rather than from zero.
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

struct Labels
{
  std::uint16_t sc = 0, col = 0, k = 0, q = 0, r = 0, p = 0, s = 0, t1 = 0, xx12 = 0;
  std::uint16_t mcnt = 0, flh = 0, ecma = 0, alp1 = 0, alp2 = 0, beta = 0, bet1 = 0, delta = 0;
  std::uint16_t fsh = 0, ash = 0, energy = 0, cabtmp = 0, gntmp = 0, altit = 0, qq14 = 0;
  std::uint16_t qq11 = 0, many = 0, kPercent = 0, comx = 0, comy = 0, comc = 0, screen = 0;

  explicit Labels(const OracleImage& _oracle)
  {
    sc = _oracle.Label("SC");
    col = _oracle.Label("COL");
    k = _oracle.Label("K");
    q = _oracle.Label("Q");
    r = _oracle.Label("R");
    p = _oracle.Label("P");
    s = _oracle.Label("S");
    t1 = _oracle.Label("T1");
    xx12 = _oracle.Label("XX12");
    mcnt = _oracle.Label("MCNT");
    flh = _oracle.Label("FLH");
    ecma = _oracle.Label("ECMA");
    alp1 = _oracle.Label("ALP1");
    alp2 = _oracle.Label("ALP2");
    beta = _oracle.Label("BETA");
    bet1 = _oracle.Label("BET1");
    delta = _oracle.Label("DELTA");
    fsh = _oracle.Label("FSH");
    ash = _oracle.Label("ASH");
    energy = _oracle.Label("ENERGY");
    cabtmp = _oracle.Label("CABTMP");
    gntmp = _oracle.Label("GNTMP");
    altit = _oracle.Label("ALTIT");
    qq14 = _oracle.Label("QQ14");
    qq11 = _oracle.Label("QQ11");
    many = _oracle.Label("MANY");
    kPercent = _oracle.Label("K%");
    comx = _oracle.Label("COMX");
    comy = _oracle.Label("COMY");
    comc = _oracle.Label("COMC");

    const Cpu6502 image = _oracle.Fresh();
    screen = static_cast<std::uint16_t>(
      (image.memory[_oracle.Label("ylookupl")] | (image.memory[_oracle.Label("ylookuph")] << 8))
      - 0x20);
  }
};

/// Compares the whole of the port's screen -- bitmap and both blocks of screen RAM -- against the
/// oracle's, and returns how many bytes are not the marker the case started from.
std::uint32_t CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas,
                             std::uint8_t _marker, const std::wstring& _context)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();
  std::uint32_t drawn = 0;

  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset)
                    + L" -- game has " + std::to_wstring(expected) + L", port has "
                    + std::to_wstring(ours[offset]))
                     .c_str());
    }
    drawn += (ours[offset] != _marker) ? 1u : 0u;
  }

  return drawn;
}

/*
 * Both screens filled with the same marker, which is what makes a STORING routine testable: a
 * dial that drew nothing would leave the marker, and a dial that drew a blank byte would not, and
 * starting from zero cannot tell those apart (§6.39's shape, in a slice where the plot is not an
 * EOR).
 */
void FillScreens(Cpu6502& _cpu, Elite::Canvas& _canvas, std::uint16_t _base, std::uint8_t _marker)
{
  std::memset(&_cpu.memory[_base], _marker, Elite::Canvas::SCREEN_SIZE);
  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    _canvas.Write(offset, _marker);
  }
}
} // namespace


TEST_CLASS(TheDashboardDials)
{
public:
  /*
   * 6502: PZW -- exhaustive, because there are only two inputs and 65,536 pairs of them, and the
   * whole routine is one `AND` and a branch spelled as a data byte.
   */
  TEST_METHOD(TheDangerColourMatchesPZW)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Labels at(oracle);
    const std::uint16_t pzw = oracle.Label("PZW");

    Cpu6502 cpu = oracle.Fresh();
    std::uint32_t red = 0;
    std::uint32_t yellow = 0;

    for (std::uint32_t counter = 0; counter < 256; ++counter)
    {
      for (std::uint32_t flash = 0; flash < 256; ++flash)
      {
        cpu.memory[at.mcnt] = static_cast<std::uint8_t>(counter);
        cpu.memory[at.flh] = static_cast<std::uint8_t>(flash);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(pzw, 200);
        Assert::IsTrue(run.completed, L"PZW returned");

        const Elite::DangerColours colours = Elite::DangerColour(
          static_cast<std::uint8_t>(counter), static_cast<std::uint8_t>(flash));

        const std::wstring where =
          Widen("PZW(MCNT=" + std::to_string(counter) + ", FLH=" + std::to_string(flash) + ")");
        Assert::AreEqual(cpu.a, colours.a, (where + L": A").c_str());
        Assert::AreEqual(cpu.x, colours.x, (where + L": X").c_str());

        red += (colours.a == Elite::DIAL_DANGER) ? 1u : 0u;
        yellow += (colours.a == Elite::DIAL_NORMAL) ? 1u : 0u;
      }
    }

    Assert::AreEqual<std::uint32_t>(65536u, red + yellow, L"the whole sweep ran");
    Assert::IsTrue(red > 0u && yellow > 0u, L"and both colours came out");
  }

  /*
   * 6502: DIL and its four entry points, over every reading and both colour orders.
   *
   * Exhaustive in the VALUE, which is what the routine is about, and swept over the four shift
   * counts because the entry point is part of the call (§6.63). The threshold takes the values
   * `DIALS` actually uses plus the two ends, and `K+1` takes zero as well, because a zero there
   * falls through to `K` and that is the branch a port is most likely to miss.
   */
  TEST_METHOD(OneBarMatchesDIL)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Labels at(oracle);

    struct Entry
    {
      const char* what;
      const char* label;
      int offset;
      int shifts;
    };
    const Entry ENTRIES[] = {
      { "DILX", "DILX", 0, 4 },
      { "DILX+2", "DILX", 2, 2 },
      { "DIL-1", "DIL", -1, 1 },
      { "DIL", "DIL", 0, 0 },
    };

    const std::uint8_t THRESHOLDS[] = { 0, 3, 11, 14, 240, 255 };
    const std::uint8_t COLOURS[][2] = {
      { Elite::DIAL_NORMAL, Elite::DIAL_DANGER },
      { Elite::DIAL_DANGER, Elite::DIAL_NORMAL },
      { Elite::DIAL_NORMAL, 0 },
      { Elite::DIAL_NORMAL, Elite::DIAL_NORMAL },
    };

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    Elite::DrawWorkspace draw;
    Elite::MathWorkspace math;

    std::uint32_t compared = 0;
    std::uint32_t drawn = 0;
    std::set<std::uint8_t> colours;

    for (const Entry& entry : ENTRIES)
    {
      const std::uint16_t address =
        static_cast<std::uint16_t>(static_cast<int>(oracle.Label(entry.label)) + entry.offset);

      for (const std::uint8_t threshold : THRESHOLDS)
      {
        for (const auto& pair : COLOURS)
        {
          for (std::uint32_t value = 0; value < 256; ++value)
          {
            const std::uint16_t start =
              static_cast<std::uint16_t>(Elite::DASHBOARD_BITMAP + 8u * 6u);

            FillScreens(cpu, canvas, at.screen, 0x3Cu);

            cpu.memory[at.sc] = static_cast<std::uint8_t>((at.screen + start) & 0xFFu);
            cpu.memory[static_cast<std::uint16_t>(at.sc + 1)] =
              static_cast<std::uint8_t>((at.screen + start) >> 8);
            cpu.memory[at.t1] = threshold;
            cpu.memory[at.k] = pair[0];
            cpu.memory[static_cast<std::uint16_t>(at.k + 1)] = pair[1];
            cpu.a = static_cast<std::uint8_t>(value);

            const Elite::Testing::RunResult run = cpu.CallSubroutine(address, 20'000);
            Assert::IsTrue(run.completed, L"DIL returned");

            draw.sc = start;
            math.t1 = threshold;
            math.k[0] = pair[0];
            math.k[1] = pair[1];
            Elite::DrawBar(canvas, draw, math, static_cast<std::uint8_t>(value), entry.shifts);

            const std::wstring where =
              Widen(std::string(entry.what) + "(" + std::to_string(value) + ", T1="
                    + std::to_string(threshold) + ", K=" + std::to_string(pair[0]) + "/"
                    + std::to_string(pair[1]) + ")");

            drawn += CompareScreens(cpu, at.screen, canvas, 0x3Cu, where);
            Assert::AreEqual(cpu.memory[at.col], draw.col, (where + L": COL").c_str());
            Assert::AreEqual(cpu.memory[at.q], math.q, (where + L": Q").c_str());

            // 6502: SC comes out one character row further down, which is how four calls in a row
            // draw four dials.
            const std::uint16_t exit = static_cast<std::uint16_t>(
              (cpu.memory[at.sc] | (cpu.memory[static_cast<std::uint16_t>(at.sc + 1)] << 8))
              - at.screen);
            Assert::AreEqual<std::uint32_t>(exit, draw.sc, (where + L": SC on the way out").c_str());

            colours.insert(draw.col);
            ++compared;
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(4u * 6u * 4u * 256u, compared, L"the whole sweep ran");
    Assert::AreEqual<std::size_t>(2u, colours.size(), L"both colours were chosen");
    Assert::IsTrue(drawn > 0u, L"and bars were actually drawn");
  }

  /// 6502: DIL2 -- exhaustive in its one argument, which is a position rather than a length.
  TEST_METHOD(OneIndicatorMatchesDIL2)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Labels at(oracle);
    const std::uint16_t dil2 = oracle.Label("DIL2");

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    Elite::DrawWorkspace draw;
    Elite::MathWorkspace math;

    std::uint32_t drawn = 0;

    for (std::uint32_t value = 0; value < 256; ++value)
    {
      const std::uint16_t start =
        static_cast<std::uint16_t>(Elite::DASHBOARD_BITMAP + 8u * 30u);

      FillScreens(cpu, canvas, at.screen, 0xA7u);

      cpu.memory[at.sc] = static_cast<std::uint8_t>((at.screen + start) & 0xFFu);
      cpu.memory[static_cast<std::uint16_t>(at.sc + 1)] =
        static_cast<std::uint8_t>((at.screen + start) >> 8);
      cpu.a = static_cast<std::uint8_t>(value);

      const Elite::Testing::RunResult run = cpu.CallSubroutine(dil2, 20'000);
      Assert::IsTrue(run.completed, L"DIL2 returned");

      draw.sc = start;
      Elite::DrawIndicator(canvas, draw, math, static_cast<std::uint8_t>(value));

      const std::wstring where = Widen("DIL2(" + std::to_string(value) + ")");
      drawn += CompareScreens(cpu, at.screen, canvas, 0xA7u, where);
      Assert::AreEqual(cpu.memory[at.q], math.q, (where + L": Q").c_str());

      const std::uint16_t exit = static_cast<std::uint16_t>(
        (cpu.memory[at.sc] | (cpu.memory[static_cast<std::uint16_t>(at.sc + 1)] << 8)) - at.screen);
      Assert::AreEqual<std::uint32_t>(exit, draw.sc, (where + L": SC on the way out").c_str());
    }

    Assert::IsTrue(drawn > 0u, L"the indicator was actually drawn");
  }
};


TEST_CLASS(TheDashboardIndicators)
{
public:
  /// 6502: MSBAR -- four missiles, every colour byte, written into screen RAM rather than drawn.
  TEST_METHOD(TheMissileIndicatorsMatchMSBAR)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Labels at(oracle);
    const std::uint16_t msbar = oracle.Label("MSBAR");

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    std::uint32_t compared = 0;

    for (std::uint32_t missile = 1; missile <= 4; ++missile)
    {
      for (std::uint32_t colour = 0; colour < 256; ++colour)
      {
        FillScreens(cpu, canvas, at.screen, 0x11u);

        cpu.x = static_cast<std::uint8_t>(missile);
        cpu.y = static_cast<std::uint8_t>(colour);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(msbar, 200);
        Assert::IsTrue(run.completed, L"MSBAR returned");

        Elite::SetMissileIndicator(canvas, static_cast<std::uint8_t>(missile),
                                   static_cast<std::uint8_t>(colour));

        const std::wstring where =
          Widen("MSBAR(" + std::to_string(missile) + ", " + std::to_string(colour) + ")");
        (void)CompareScreens(cpu, at.screen, canvas, 0x11u, where);

        // 6502: LDY #0 -- it leaves Y at zero, and its callers in the original rely on that.
        Assert::AreEqual<std::uint32_t>(0u, cpu.y, (where + L": Y on the way out").c_str());
        ++compared;
      }
    }

    Assert::AreEqual<std::uint32_t>(4u * 256u, compared, L"the whole sweep ran");
  }

  /*
   * 6502: ECBLB and SPBLB -- the two bulbs, and the point is that they TOGGLE.
   *
   * Called twice they put the screen back, which is how the flight loop turns them off. So the
   * sweep calls each an odd and an even number of times and compares both.
   */
  TEST_METHOD(TheBulbsMatchECBLBandSPBLB)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Labels at(oracle);

    struct Bulb
    {
      const char* what;
      const char* label;
      void (*toggle)(Elite::Canvas&);
    };
    const Bulb BULBS[] = {
      { "ECBLB", "ECBLB", &Elite::ToggleEcmIndicator },
      { "SPBLB", "SPBLB", &Elite::ToggleStationIndicator },
    };

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    std::uint32_t marked = 0;

    for (const Bulb& bulb : BULBS)
    {
      for (int times = 1; times <= 3; ++times)
      {
        FillScreens(cpu, canvas, at.screen, 0x00u);

        for (int call = 0; call < times; ++call)
        {
          const Elite::Testing::RunResult run = cpu.CallSubroutine(oracle.Label(bulb.label), 200);
          Assert::IsTrue(run.completed, L"the bulb returned");
          bulb.toggle(canvas);
        }

        const std::wstring where =
          Widen(std::string(bulb.what) + " x" + std::to_string(times));
        const std::uint32_t drawn = CompareScreens(cpu, at.screen, canvas, 0x00u, where);

        // An odd number of toggles leaves the bulb lit and an even number leaves nothing.
        Assert::AreEqual<std::uint32_t>((times % 2 == 0) ? 0u : 2u, drawn,
                                        (where + L": how many cells are lit").c_str());
        marked += drawn;
      }
    }

    Assert::IsTrue(marked > 0u, L"the bulbs were actually lit");
  }

  /*
   * 6502: ECBLB2 -- and it has no `RTS`, so lighting the bulb is part of starting the E.C.M.
   * rather than something the caller does next.
   */
  TEST_METHOD(StartingTheEcmMatchesECBLB2)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Labels at(oracle);

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;

    // 6502: JSR NOISE -- the sound is hardware, so it is trapped on one side and recorded on the
    // other. The trap is what makes the fall-through into `ECBLB` observable at all.
    cpu.AddTrap(oracle.Label("NOISE"));

    FillScreens(cpu, canvas, at.screen, 0x00u);
    cpu.memory[at.ecma] = 0x7Bu;

    const Elite::Testing::RunResult run = cpu.CallSubroutine(oracle.Label("ECBLB2"), 2'000);
    Assert::IsTrue(run.completed, L"ECBLB2 returned");

    struct Recorder final : Elite::DashboardEffects
    {
      std::vector<std::uint8_t> sounds;
      void PlaySound(std::uint8_t _effect) override { sounds.push_back(_effect); }
    } effects;

    Elite::FlightStatus status;
    status.ecmCountdown = 0x7Bu;
    Elite::StartEcm(canvas, status, effects);

    (void)CompareScreens(cpu, at.screen, canvas, 0x00u, L"ECBLB2");
    Assert::AreEqual(cpu.memory[at.ecma], status.ecmCountdown, L"ECMA");
    Assert::AreEqual<std::size_t>(1u, effects.sounds.size(), L"one sound was asked for");
    Assert::AreEqual<std::uint32_t>(Elite::SOUND_ECM, effects.sounds[0], L"and it is sfxecm");
    Assert::AreEqual<std::size_t>(1u, cpu.trapHits.size(), L"the game asked for one too");
    Assert::AreEqual<std::uint32_t>(Elite::SOUND_ECM, cpu.trapHits[0].y, L"with the same number");
  }
};


TEST_CLASS(TheWholeDashboard)
{
public:
  /*
   * 6502: DIALS parts 1 to 4 -- four files, one fall-through, and it ends `JMP COMPAS`.
   *
   * Compared on the whole screen and on every byte the chain leaves behind. The main loop counter
   * is swept over all four of its phases because part 3 -- the energy bars -- runs on one pass in
   * four and returns through `TT26`'s borrowed `RTS` on the other three, and over the bit that
   * makes the danger colour flash.
   *
   * The pitch is what this test is really for. `LDA BETA / LDX BET1 / BEQ P%+4 / SBC #1` has no
   * `SEC`, so the subtraction runs on the carry `DIL2` left behind, and the sweep covers a zero
   * `BET1` (which skips it) and every non-zero one (which does not).
   */
  TEST_METHOD(TheWholeDashboardMatchesDIALS)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Labels at(oracle);
    const std::uint16_t dials = oracle.Label("DIALS");

    struct Case
    {
      const char* what;
      std::uint8_t delta, alp1, alp2, beta, bet1;
      std::uint8_t energy, fsh, ash, fuel, cabtmp, gntmp, altit;
      std::uint8_t flash;
    };

    const std::vector<Case> CASES = {
      { "everything at zero", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
      { "everything full", 255, 127, 0x80, 127, 127, 255, 255, 255, 70, 255, 255, 255, 0 },
      { "cruising", 20, 8, 0, 6, 6, 200, 200, 190, 50, 30, 20, 90, 0 },
      { "rolling left", 20, 31, 0x80, 0, 0, 200, 200, 190, 50, 30, 20, 90, 0 },
      { "rolling right", 20, 31, 0, 0, 0, 200, 200, 190, 50, 30, 20, 90, 0 },
      { "pitching, BET1 zero so no SBC", 20, 0, 0, 0x88, 0, 200, 200, 190, 50, 30, 20, 90, 0 },
      { "pitching, BET1 one so the SBC runs", 20, 0, 0, 0x88, 1, 200, 200, 190, 50, 30, 20, 90, 0 },
      { "pitching the other way", 20, 0, 0, 8, 8, 200, 200, 190, 50, 30, 20, 90, 0 },
      { "pitch at the extreme", 20, 0, 0, 127, 127, 200, 200, 190, 50, 30, 20, 90, 0 },
      { "in the danger zone, not flashing", 30, 4, 0, 4, 4, 40, 20, 20, 5, 200, 200, 30, 0 },
      { "in the danger zone, flashing", 30, 4, 0, 4, 4, 40, 20, 20, 5, 200, 200, 30, 0xFF },
      { "energy on a bank boundary", 10, 0, 0, 0, 0, 64, 100, 100, 20, 10, 10, 10, 0 },
      { "energy just under one", 10, 0, 0, 0, 0, 63, 100, 100, 20, 10, 10, 10, 0 },
      { "altitude at its threshold", 10, 0, 0, 0, 0, 100, 100, 100, 20, 10, 10, 240, 0 },
    };

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    Elite::DrawWorkspace draw;
    Elite::MathWorkspace math;
    Elite::GeometryWorkspace geometry;

    std::uint32_t compared = 0;
    std::uint32_t drawn = 0;
    std::uint32_t energyPasses = 0;

    for (const Case& item : CASES)
    {
      for (const std::uint8_t counter : { 0u, 1u, 2u, 3u, 8u, 9u })
      {
        for (const std::uint8_t stations : { 0u, 1u })
        {
          FillScreens(cpu, canvas, at.screen, 0x00u);

          const std::uint8_t READINGS[][2] = {
            { 0u, item.delta },  { 1u, item.alp1 },   { 2u, item.alp2 },   { 3u, item.beta },
            { 4u, item.bet1 },   { 5u, item.energy }, { 6u, item.fsh },    { 7u, item.ash },
            { 8u, item.fuel },   { 9u, item.cabtmp }, { 10u, item.gntmp }, { 11u, item.altit },
            { 12u, item.flash },
          };
          const std::uint16_t WHERE[] = { at.delta, at.alp1,   at.alp2,  at.beta,  at.bet1,
                                          at.energy, at.fsh,   at.ash,   at.qq14,  at.cabtmp,
                                          at.gntmp,  at.altit, at.flh };
          for (const auto& reading : READINGS)
          {
            cpu.memory[WHERE[reading[0]]] = reading[1];
          }
          cpu.memory[at.mcnt] = counter;
          cpu.memory[at.qq11] = 0;

          // The compass, which `DIALS` ends by calling. `SSPR` is `MANY+SST` (§6.58).
          Elite::Bubble bubble;
          bubble.counts[Elite::SHIP_TYPE_STATION] = stations;
          cpu.memory[static_cast<std::uint16_t>(at.many + Elite::SHIP_TYPE_STATION)] = stations;

          std::uint32_t state = 0x5C31A70Fu ^ (counter * 0x9E3779B9u) ^ (stations * 0x85EBCA6Bu);
          for (std::size_t slot = 0; slot < 2u; ++slot)
          {
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              state = state * 1103515245u + 12345u;
              const std::uint8_t value = static_cast<std::uint8_t>(state >> 17);
              bubble.blocks[slot][byte] = value;
              cpu.memory[static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE
                                                    + byte)] = value;
            }
          }

          Elite::Compass compass{ 0xC3u, 0x9Cu, Elite::COMPASS_AHEAD };
          cpu.memory[at.comx] = compass.x;
          cpu.memory[at.comy] = compass.y;
          cpu.memory[at.comc] = compass.colour;

          // `XX12` is scratch that part 3 clears before it reads, so both sides start it dirty.
          for (std::size_t byte = 0; byte < 4u; ++byte)
          {
            cpu.memory[static_cast<std::uint16_t>(at.xx12 + byte)] = static_cast<std::uint8_t>(0x9Du + byte);
            geometry.xx12[byte] = static_cast<std::uint8_t>(0x9Du + byte);
          }

          const Elite::Testing::RunResult run = cpu.CallSubroutine(dials, 200'000);
          Assert::IsTrue(run.completed, L"DIALS returned");

          Elite::FlightState flight;
          flight.delta = item.delta;
          flight.alp1 = item.alp1;
          flight.alp2 = item.alp2;
          flight.beta = item.beta;
          flight.bet1 = item.bet1;
          flight.mainLoopCounter = counter;

          Elite::FlightStatus status;
          status.energy = item.energy;
          status.forwardShield = item.fsh;
          status.aftShield = item.ash;
          status.cabinTemperature = item.cabtmp;
          status.laserTemperature = item.gntmp;
          status.altitude = item.altit;
          status.damageFlash = item.flash;

          Elite::DrawDials(canvas, draw, math, geometry, flight, status, item.fuel, compass, bubble);

          const std::wstring where =
            Widen(std::string("DIALS: ") + item.what + ", MCNT " + std::to_string(counter)
                  + (stations ? ", station" : ", planet"));

          drawn += CompareScreens(cpu, at.screen, canvas, 0x00u, where);

          Assert::AreEqual(cpu.memory[at.comx], compass.x, (where + L": COMX").c_str());
          Assert::AreEqual(cpu.memory[at.comy], compass.y, (where + L": COMY").c_str());
          Assert::AreEqual(cpu.memory[at.comc], compass.colour, (where + L": COMC").c_str());
          Assert::AreEqual(cpu.memory[at.k], math.k[0], (where + L": K").c_str());
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.k + 1)], math.k[1],
                           (where + L": K+1").c_str());
          Assert::AreEqual(cpu.memory[at.t1], math.t1, (where + L": T1").c_str());
          Assert::AreEqual(cpu.memory[at.col], draw.col, (where + L": COL").c_str());

          for (std::size_t byte = 0; byte < 4u; ++byte)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.xx12 + byte)],
                             geometry.xx12[byte], (where + L": XX12+" + std::to_wstring(byte)).c_str());
          }

          energyPasses += ((counter & 3u) == 0u) ? 1u : 0u;
          ++compared;
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(CASES.size()) * 6u * 2u, compared,
                                    L"the whole sweep ran");
    Assert::IsTrue(energyPasses > 0u, L"the energy bars were redrawn on some passes");
    Assert::IsTrue(compared > energyPasses, L"and skipped on others");
    Assert::IsTrue(drawn > 0u, L"and the dashboard was actually drawn");
    Logger::WriteMessage(("DIALS: " + std::to_string(compared) + " dashboards, "
                          + std::to_string(energyPasses) + " with the energy bars")
                           .c_str());
  }
};

} // namespace GameLogicTests

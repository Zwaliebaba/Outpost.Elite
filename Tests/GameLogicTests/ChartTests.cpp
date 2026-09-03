#include "pch.h"

#include "OracleImage.h"

#include "Canvas.h"
#include "Charts.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Canvas;
using Elite::ChartView;
using Elite::Crosshairs;
using Elite::DrawWorkspace;
using Elite::SystemSeeds;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The galactic charts against the game that draws them (slice 2b).
 *
 * These compare the WHOLE SCREEN after every call, the way the pixel primitives are compared,
 * because the charts are almost entirely coordinate arithmetic and a wrong carry moves a dot by
 * one pixel rather than putting it somewhere obviously silly. A spot check would pass.
 *
 * The fuel circle and the short-range chart's system discs are drawn by CIRCLE2 and SUN, which
 * keep a line heap that belongs to slice 3c. Both are trapped in the oracle and left to a seam
 * in the port, and the tests compare the ARGUMENTS rather than skipping them -- so the two
 * halves are pinned against each other before the drawing exists.
 */
namespace GameLogicTests
{

namespace
{
constexpr std::uint16_t SPACE_VIEW_MARGIN = 0x20;

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

/// The zero-page and workspace bytes the chart routines read.
struct Scratch
{
  std::uint16_t qq9 = 0;
  std::uint16_t qq10 = 0;
  std::uint16_t qq0 = 0;
  std::uint16_t qq1 = 0;
  std::uint16_t qq11 = 0;
  std::uint16_t qq14 = 0;
  std::uint16_t qq15 = 0;
  std::uint16_t qq19 = 0;
  std::uint16_t qq21 = 0;
  std::uint16_t k = 0;
  std::uint16_t k3 = 0;
  std::uint16_t k4 = 0;
  std::uint16_t stp = 0;
  std::uint16_t screen = 0;

  explicit Scratch(const OracleImage& _oracle)
    : qq9(_oracle.Label("QQ9"))
    , qq10(_oracle.Label("QQ10"))
    , qq0(_oracle.Label("QQ0"))
    , qq1(_oracle.Label("QQ1"))
    , qq11(_oracle.Label("QQ11"))
    , qq14(_oracle.Label("QQ14"))
    , qq15(_oracle.Label("QQ15"))
    , qq19(_oracle.Label("QQ19"))
    , qq21(_oracle.Label("QQ21"))
    , k(_oracle.Label("K"))
    , k3(_oracle.Label("K3"))
    , k4(_oracle.Label("K4"))
    , stp(_oracle.Label("STP"))
  {
    const Cpu6502 cpu = _oracle.Fresh();
    const std::uint16_t low = _oracle.Label("ylookupl");
    const std::uint16_t high = _oracle.Label("ylookuph");
    screen = static_cast<std::uint16_t>((cpu.memory[low] | (cpu.memory[high] << 8)) - SPACE_VIEW_MARGIN);
  }
};

void CompareScreens(const Cpu6502& _cpu, std::uint16_t _screenBase, const Canvas& _canvas, const std::wstring& _context)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();

  for (std::uint16_t offset = 0; offset < Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_screenBase + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset) + L" -- game has "
                    + std::to_wstring(expected) + L", port has " + std::to_wstring(ours[offset]))
                     .c_str());
    }
  }
}

void LoadSeeds(Cpu6502& _cpu, std::uint16_t _address, const SystemSeeds& _seeds)
{
  for (std::size_t index = 0; index < _seeds.bytes.size(); ++index)
  {
    _cpu.memory[static_cast<std::uint16_t>(_address + index)] = _seeds.bytes[index];
  }
}

/// Records what the port asked the deferred routines to draw.
struct RecordedShapes : public Elite::ChartShapes
{
  void DrawRangeCircle(const Elite::RangeCircle& _circle) override { circles.push_back(_circle); }

  void DrawSystemDisc(std::uint8_t _x, std::uint8_t _y, std::uint8_t _radius) override
  {
    discs.push_back({ _x, _y, _radius });
  }

  struct Disc
  {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t radius = 0;
    [[nodiscard]] bool operator==(const Disc&) const = default;
  };

  std::vector<Elite::RangeCircle> circles;
  std::vector<Disc> discs;
};

std::wstring Where(const wchar_t* _what, const ChartView& _view)
{
  return std::wstring(_what) + L" (QQ9=" + std::to_wstring(_view.cursorX) + L" QQ10=" + std::to_wstring(_view.cursorY)
         + L" QQ0=" + std::to_wstring(_view.homeX) + L" QQ1=" + std::to_wstring(_view.homeY) + L" QQ11="
         + std::to_wstring(_view.view) + L" QQ14=" + std::to_wstring(_view.fuel) + L")";
}

void SeedChart(Cpu6502& _cpu, const Scratch& _zp, const ChartView& _view)
{
  _cpu.memory[_zp.qq9] = _view.cursorX;
  _cpu.memory[_zp.qq10] = _view.cursorY;
  _cpu.memory[_zp.qq0] = _view.homeX;
  _cpu.memory[_zp.qq1] = _view.homeY;
  _cpu.memory[_zp.qq11] = _view.view;
  _cpu.memory[_zp.qq14] = _view.fuel;
}

/// What TT66 leaves behind. The charts are compared with it trapped, because clearing the screen
/// and drawing the border is screen work rather than chart work, so this is set by hand instead.
void SeedAfterScreenReset(Cpu6502& _cpu, const OracleImage& _oracle)
{
  _cpu.memory[_oracle.Label("QQ17")] = 0x80;
  _cpu.memory[_oracle.Label("XC")] = 1;
  _cpu.memory[_oracle.Label("YC")] = 1;
  _cpu.memory[_oracle.Label("DTW1")] = 0;
  _cpu.memory[_oracle.Label("DTW2")] = 0xFF;
  _cpu.memory[_oracle.Label("DTW3")] = 0;
  _cpu.memory[_oracle.Label("DTW4")] = 0;
  _cpu.memory[_oracle.Label("DTW5")] = 0;
  _cpu.memory[_oracle.Label("DTW6")] = 0;
  _cpu.memory[_oracle.Label("DTW8")] = 0xFF;
  _cpu.memory[_oracle.Label("COL2")] = 0x40;
}

/*
 * 6502: tal -- CLC / LDX GCNT / INX / JMP pr2.
 *
 * The long-range chart's title ends with the galaxy's number, and that arrives as recursive
 * token 1, which is a VALUE token: it reads GCNT, which is commander state and belongs to slice
 * 2d. So the charts leave it to the seam the token printer already has, and this is what the
 * game would have supplied.
 */
struct GalaxyNumber : public Elite::ValueTokens
{
  void Print(std::uint8_t _token, Elite::TextSink& _sink) override
  {
    if (_token == 1)
    {
      Elite::PrintByteValue(_sink, static_cast<std::uint8_t>(number + 1), false);
      return;
    }
    ++unexpected;
  }

  std::uint8_t number = 0; ///< 6502: GCNT, counting from zero
  std::uint32_t unexpected = 0;
};

/// The port's side of the same: the two text systems wired to the canvas the way the game wires
/// them -- token printer into DASC, DASC into CHPR.
struct PortScreen
{
  explicit PortScreen(std::uint8_t _galaxy = 0)
    : screen(canvas, text)
    , characters(screen)
    , printer(characters, &galaxy)
  {
    galaxy.number = _galaxy;
    text.column = 1;
    text.row = 1;
    text.caseFlags = 0x80;
    text.cellColour = 0x40;
    characters.state.sentenceStart = 0xFF;
    printer.SetCaseFlags(0x80);
  }

  Canvas canvas;
  DrawWorkspace work;
  Elite::TextState text;
  GalaxyNumber galaxy;
  Elite::TextPrinter screen;
  Elite::CharacterPrinter characters;
  Elite::TokenPrinter printer;
};
} // namespace

TEST_CLASS(ChartsAgainstTheShippedGame)
{
public:
  /*
   * 6502: TT123 -- every value against every step, all 65,536 of them.
   *
   * Exhaustive because the routine's whole content is one comparison between the step's sign and
   * the carry the addition left, and the two ways of getting it wrong -- clamping, or wrapping --
   * both look right for the 60,000 pairs in the middle.
   */
  TEST_METHOD(CrosshairStepMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t routine = oracle.Label("TT123");
    const std::uint16_t step = static_cast<std::uint16_t>(oracle.Label("QQ19") + 3);
    const std::uint16_t result = static_cast<std::uint16_t>(oracle.Label("QQ19") + 4);

    std::uint32_t compared = 0;
    std::uint32_t refused = 0;

    for (std::uint32_t value = 0; value < 256; ++value)
    {
      for (std::uint32_t delta = 0; delta < 256; ++delta)
      {
        Cpu6502 cpu = oracle.Fresh();
        cpu.memory[step] = static_cast<std::uint8_t>(delta);
        cpu.a = static_cast<std::uint8_t>(value);
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        Assert::IsTrue(cpu.CallSubroutine(routine, 1'000).completed, L"TT123 should return");

        const std::uint8_t ours =
          Elite::StepCoordinate(static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(delta));
        Assert::AreEqual<std::uint32_t>(cpu.memory[result], ours,
                                        (L"TT123(" + std::to_wstring(value) + L", " + std::to_wstring(delta) + L")")
                                          .c_str());
        if (ours == static_cast<std::uint8_t>(value) && delta != 0)
        {
          ++refused;
        }
        ++compared;
      }
    }

    Logger::WriteMessage(("TT123: " + std::to_string(compared) + " moves compared, " + std::to_string(refused)
                          + " refused at the edge of the galaxy")
                           .c_str());
    Assert::IsTrue(refused > 1000, L"the refusing branch must actually be exercised");
  }

  /*
   * 6502: TT15 -- the crosshair itself, over the sizes the game uses and the edges it saturates
   * at. Compared as a whole screen, so a stroke one pixel long or one row low fails.
   */
  TEST_METHOD(CrosshairsMatchTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("TT15");

    std::uint32_t compared = 0;

    for (const std::uint32_t view : { 0u, 0x80u })
    {
      for (const std::uint32_t size : { 0u, 4u, 7u, 8u, 16u })
      {
        for (const std::uint32_t x : { 0u, 1u, 3u, 7u, 8u, 104u, 200u, 248u, 252u, 255u })
        {
          for (const std::uint32_t y : { 0u, 1u, 7u, 8u, 63u, 90u, 120u, 127u, 128u, 150u, 199u, 255u })
          {
            Cpu6502 cpu = oracle.Fresh();
            cpu.memory[zp.qq19] = static_cast<std::uint8_t>(x);
            cpu.memory[static_cast<std::uint16_t>(zp.qq19 + 1)] = static_cast<std::uint8_t>(y);
            cpu.memory[static_cast<std::uint16_t>(zp.qq19 + 2)] = static_cast<std::uint8_t>(size);
            cpu.memory[zp.qq11] = static_cast<std::uint8_t>(view);
            cpu.a = cpu.x = cpu.y = 0;
            cpu.sp = 0xFD;
            Assert::IsTrue(cpu.CallSubroutine(routine, 200'000).completed, L"TT15 should return");

            Canvas canvas;
            DrawWorkspace work;
            const Crosshairs at{ static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y),
                                 static_cast<std::uint8_t>(size) };
            Elite::DrawCrosshairs(canvas, work, at, static_cast<std::uint8_t>(view));

            CompareScreens(cpu, zp.screen, canvas,
                           L"TT15 (x=" + std::to_wstring(x) + L" y=" + std::to_wstring(y) + L" size="
                             + std::to_wstring(size) + L" view=" + std::to_wstring(view) + L")");
            ++compared;
          }
        }
      }
    }

    Logger::WriteMessage(("TT15: " + std::to_string(compared) + " crosshairs compared by whole screen").c_str());
  }

  /*
   * 6502: TT103 and TT105 -- the crosshair at the selected system, on both charts.
   *
   * The short-range half is the interesting one: it refuses to draw when the selection is off
   * screen, and the window it accepts is not centred -- x runs from -26 to +37 and y from -36 to
   * +37, out of four constants that look interchangeable.
   *
   * The two offsets are swept INDEPENDENTLY, which is the point. A grid that moved them together
   * -- as this test first did -- lets whichever test rejects first mask the other, and both
   * constants can then be changed without failing anything.
   */
  TEST_METHOD(TargetCrosshairsMatchTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("TT103");

    std::uint32_t compared = 0;
    std::uint32_t drawn = 0;

    /*
     * Whether anything was drawn is read off the raw bitmap rather than off Canvas::Hash, which
     * walks the RESOLVED picture -- and with every cell colour left at zero a drawn line
     * resolves to exactly the same picture as a blank screen.
     */
    const auto anythingDrawn = [](const Canvas& _canvas) noexcept {
      for (const std::uint8_t byte : _canvas.Screen())
      {
        if (byte != 0)
        {
          return true;
        }
      }
      return false;
    };

    // Every edge of both windows, and a few values well inside and well outside them.
    const std::array<std::uint32_t, 16> offsets = { 0,   1,   36,  37,  38,  39,  100, 128,
                                                    200, 218, 219, 220, 221, 229, 230, 231 };

    for (const std::uint32_t view : { 0u, 0x80u })
    {
      for (const std::uint32_t home : { 0u, 128u })
      {
        for (const std::uint32_t offsetX : offsets)
        {
          for (const std::uint32_t offsetY : offsets)
          {
            ChartView chart;
            chart.view = static_cast<std::uint8_t>(view);
            chart.homeX = static_cast<std::uint8_t>(home);
            chart.homeY = static_cast<std::uint8_t>(home);
            chart.cursorX = static_cast<std::uint8_t>(home + offsetX);
            chart.cursorY = static_cast<std::uint8_t>(home + offsetY);

            Cpu6502 cpu = oracle.Fresh();
            SeedChart(cpu, zp, chart);
            cpu.a = cpu.x = cpu.y = 0;
            cpu.sp = 0xFD;
            Assert::IsTrue(cpu.CallSubroutine(routine, 200'000).completed, L"TT103 should return");

            Canvas canvas;
            DrawWorkspace work;
            Elite::DrawTargetCrosshairs(canvas, work, chart);

            CompareScreens(cpu, zp.screen, canvas, Where(L"TT103", chart));
            ++compared;
            if (anythingDrawn(canvas))
            {
              ++drawn;
          }
          }
        }
      }
    }

    Logger::WriteMessage(("TT103: " + std::to_string(compared) + " compared, of which " + std::to_string(drawn)
                          + " drew something")
                           .c_str());

    // A comparison where neither side ever drew would pass while proving nothing, and one where
    // both always drew would never reach the range tests.
    Assert::IsTrue(drawn > compared / 4, L"a good share of the grid should be on screen");
    Assert::IsTrue(drawn < compared, L"and some of it should be off screen");
  }

  /*
   * 6502: TT16 -- erase, move, redraw.
   *
   * The step arrives negated on the vertical axis, so the oracle is given the register values and
   * the port the step the routine actually applies; getting that backwards would move the
   * crosshairs the wrong way and still draw a chart that looked fine.
   */
  TEST_METHOD(MovingTheCrosshairsMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("TT16");

    std::uint32_t compared = 0;

    for (const std::uint32_t view : { 0u, 0x80u })
    {
      for (const std::uint32_t startX : { 0u, 1u, 100u, 254u, 255u })
      {
        for (const std::uint32_t startY : { 0u, 2u, 100u, 253u, 255u })
        {
          for (const std::uint32_t dx : { 0u, 1u, 8u, 248u, 255u })
          {
            for (const std::uint32_t dy : { 0u, 1u, 8u, 248u, 255u })
            {
              ChartView chart;
              chart.view = static_cast<std::uint8_t>(view);
              chart.cursorX = static_cast<std::uint8_t>(startX);
              chart.cursorY = static_cast<std::uint8_t>(startY);
              chart.homeX = 100;
              chart.homeY = 100;

              Cpu6502 cpu = oracle.Fresh();
              cpu.AddTrap(oracle.Label("WSCAN"));
              SeedChart(cpu, zp, chart);
              cpu.a = 0;
              cpu.x = static_cast<std::uint8_t>(dx);
              cpu.y = static_cast<std::uint8_t>(dy);
              cpu.sp = 0xFD;
              Assert::IsTrue(cpu.CallSubroutine(routine, 500'000).completed, L"TT16 should return");

              Canvas canvas;
              DrawWorkspace work;
              ChartView ours = chart;

              // 6502: DEY / TYA / EOR #255 -- the vertical step the routine applies is -Y.
              Elite::MoveCrosshairs(canvas, work, ours, static_cast<std::uint8_t>(dx),
                                    static_cast<std::uint8_t>(0u - dy));

              const std::wstring where = Where(L"TT16", chart) + L" step (" + std::to_wstring(dx) + L", "
                                         + std::to_wstring(dy) + L")";
              Assert::AreEqual<std::uint32_t>(cpu.memory[zp.qq9], ours.cursorX, (where + L": QQ9").c_str());
              Assert::AreEqual<std::uint32_t>(cpu.memory[zp.qq10], ours.cursorY, (where + L": QQ10").c_str());
              CompareScreens(cpu, zp.screen, canvas, where);
              ++compared;
            }
          }
        }
      }
    }

    Logger::WriteMessage(("TT16: " + std::to_string(compared) + " crosshair moves compared").c_str());
  }

  /*
   * 6502: TT14 -- the fuel circle's crosshair, and the arguments it leaves for CIRCLE2.
   *
   * CIRCLE2 is trapped, so what is compared is the four bytes it would have been given: where the
   * circle goes, how big it is, and the step round it. That is the whole of TT14's own
   * arithmetic, and it is what slice 3c will draw from.
   */
  TEST_METHOD(FuelRangeMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("TT14");

    std::uint32_t compared = 0;

    for (const std::uint32_t view : { 0u, 0x80u })
    {
      for (const std::uint32_t fuel : { 0u, 1u, 3u, 4u, 7u, 70u, 128u, 255u })
      {
        for (const std::uint32_t home : { 0u, 1u, 7u, 60u, 104u, 200u, 255u })
        {
          ChartView chart;
          chart.view = static_cast<std::uint8_t>(view);
          chart.fuel = static_cast<std::uint8_t>(fuel);
          chart.homeX = static_cast<std::uint8_t>(home);
          chart.homeY = static_cast<std::uint8_t>(255u - home);

          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(oracle.Label("CIRCLE2"));
          SeedChart(cpu, zp, chart);
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          Assert::IsTrue(cpu.CallSubroutine(routine, 500'000).completed, L"TT14 should return");

          Canvas canvas;
          DrawWorkspace work;
          RecordedShapes shapes;
          Elite::DrawFuelRange(canvas, work, chart, &shapes);

          const std::wstring where = Where(L"TT14", chart);
          Assert::AreEqual<std::size_t>(1u, shapes.circles.size(), (where + L": one circle").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.k3], shapes.circles[0].x, (where + L": K3").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.k4], shapes.circles[0].y, (where + L": K4").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.k], shapes.circles[0].radius, (where + L": K").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.stp], shapes.circles[0].step, (where + L": STP").c_str());
          CompareScreens(cpu, zp.screen, canvas, where);
          ++compared;
        }
      }
    }

    Logger::WriteMessage(("TT14: " + std::to_string(compared) + " fuel circles compared").c_str());
  }

  /*
   * 6502: TT22 -- the whole long-range chart, compared as a screen.
   *
   * TT66 is trapped: resetting the view, clearing the screen and drawing the border is screen
   * work rather than chart work, so both sides start blank and the state TT66 would have left is
   * set by hand. CIRCLE2 is trapped for the reason the seam exists.
   *
   * Everything else runs on both sides: the title, both rules, the fuel crosshair, all 256 dots
   * and the selection crosshair. Getting the dots' brightness byte or the halving of y wrong
   * moves hundreds of pixels, and a whole-screen compare says which one differs first.
   */
  TEST_METHOD(LongRangeChartMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("TT22");

    SystemSeeds galaxy = Elite::GALAXY_ONE_SEEDS;
    std::uint32_t compared = 0;

    for (int galaxyNumber = 1; galaxyNumber <= 8; ++galaxyNumber)
    {
      ChartView chart;
      chart.view = 0x40; // 6502: LDA #64 / JSR TT66
      chart.fuel = 70;
      chart.homeX = 20;
      chart.homeY = 173;
      chart.cursorX = 20;
      chart.cursorY = 173;

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("TT66"));
      cpu.AddTrap(oracle.Label("CIRCLE2"));
      LoadSeeds(cpu, zp.qq21, galaxy);
      SeedChart(cpu, zp, chart);
      SeedAfterScreenReset(cpu, oracle);

      // 6502: GCNT -- the title ends with the galaxy's number, so both sides must be in the
      // same galaxy or the chart differs by one character and nothing else.
      cpu.memory[oracle.Label("GCNT")] = static_cast<std::uint8_t>(galaxyNumber - 1);

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      const auto run = cpu.CallSubroutine(routine, 5'000'000);
      Assert::IsTrue(run.completed && !run.illegalOpcode, L"TT22 should return");

      PortScreen port(static_cast<std::uint8_t>(galaxyNumber - 1));
      RecordedShapes shapes;
      Elite::DrawLongRangeChart(port.canvas, port.work, port.printer, port.text, chart, galaxy, &shapes);

      const std::wstring where = L"TT22 galaxy " + std::to_wstring(galaxyNumber);
      Assert::AreEqual<std::size_t>(1u, shapes.circles.size(), (where + L": one fuel circle").c_str());
      Assert::AreEqual<std::uint32_t>(cpu.memory[zp.k3], shapes.circles[0].x, (where + L": K3").c_str());
      Assert::AreEqual<std::uint32_t>(cpu.memory[zp.k4], shapes.circles[0].y, (where + L": K4").c_str());
      Assert::AreEqual<std::uint32_t>(cpu.memory[zp.k], shapes.circles[0].radius, (where + L": K").c_str());
      CompareScreens(cpu, zp.screen, port.canvas, where);
      Assert::AreEqual<std::uint32_t>(0u, port.galaxy.unexpected,
                                      (where + L": only the galaxy number should be a value token").c_str());

      ++compared;
      Elite::NextGalaxy(galaxy);
    }

    Logger::WriteMessage(("TT22: " + std::to_string(compared) + " long-range charts compared by whole screen").c_str());
  }

  /*
   * 6502: TT23 -- the short-range chart, screen and discs together.
   *
   * SUN is trapped and its arguments recorded, because the disc it draws is slice 3c's. What this
   * compares is everything else: which systems are near enough to appear, where they land, which
   * character row each name goes on when its own row is taken, and the systems that get neither
   * name nor disc because their row came out above three.
   */
  TEST_METHOD(ShortRangeChartMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("TT23");
    const std::uint16_t sun = oracle.Label("SUN");

    SystemSeeds galaxy = Elite::GALAXY_ONE_SEEDS;
    std::uint32_t compared = 0;
    std::uint32_t discsSeen = 0;

    // Lave sits at (20, 173) in galaxy 1; the others crowd the chart differently.
    const std::array<std::pair<std::uint8_t, std::uint8_t>, 4> homes = {
      { { 20, 173 }, { 96, 40 }, { 128, 128 }, { 0, 0 } }
    };

    for (int galaxyNumber = 1; galaxyNumber <= 8; ++galaxyNumber)
    {
      for (const auto& home : homes)
      {
        ChartView chart;
        chart.view = 0x80; // 6502: LDA #128 / JSR TT66
        chart.fuel = 70;
        chart.homeX = home.first;
        chart.homeY = home.second;
        chart.cursorX = home.first;
        chart.cursorY = home.second;

        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(oracle.Label("TT66"));
        cpu.AddTrap(oracle.Label("CIRCLE2"));
        cpu.AddTrap(sun);
        cpu.AddTrap(oracle.Label("FLFLLS"));

        // SUN takes its centre in K3 and K4 and its radius in K, so the values have to be
        // snapshotted as each call happens -- by the time the chart is finished they hold
        // whatever the last system left.
        cpu.watch = { zp.k3, zp.k4, zp.k, 0 };
        LoadSeeds(cpu, zp.qq21, galaxy);
        SeedChart(cpu, zp, chart);
        SeedAfterScreenReset(cpu, oracle);
        cpu.memory[oracle.Label("GCNT")] = static_cast<std::uint8_t>(galaxyNumber - 1);

        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        const auto run = cpu.CallSubroutine(routine, 20'000'000);
        Assert::IsTrue(run.completed && !run.illegalOpcode, L"TT23 should return");

        /*
         * 6502: ee1 -- K3 and K4 are stored immediately before the SUN call and K holds the
         * radius, so the trap's registers are not enough; the memory is read at each hit.
         *
         * A trap records in order, so this is the SEQUENCE of discs, not a set. Two systems that
         * swapped places would still fail.
         */
        std::vector<RecordedShapes::Disc> expected;
        for (const auto& hit : cpu.trapHits)
        {
          if (hit.address == sun)
          {
            expected.push_back({ hit.watched[0], hit.watched[1], hit.watched[2] });
          }
        }

        PortScreen port(static_cast<std::uint8_t>(galaxyNumber - 1));
        RecordedShapes shapes;
        Elite::DrawShortRangeChart(port.canvas, port.work, port.printer, port.text, chart, galaxy, &shapes);

        const std::wstring where = L"TT23 galaxy " + std::to_wstring(galaxyNumber) + L" at ("
                                   + std::to_wstring(home.first) + L", " + std::to_wstring(home.second) + L")";

        Assert::AreEqual<std::size_t>(expected.size(), shapes.discs.size(), (where + L": disc count").c_str());
        for (std::size_t index = 0; index < expected.size() && index < shapes.discs.size(); ++index)
        {
          Assert::IsTrue(expected[index] == shapes.discs[index],
                         (where + L": disc " + std::to_wstring(index) + L" -- game ("
                          + std::to_wstring(expected[index].x) + L", " + std::to_wstring(expected[index].y) + L", r"
                          + std::to_wstring(expected[index].radius) + L") port ("
                          + std::to_wstring(shapes.discs[index].x) + L", " + std::to_wstring(shapes.discs[index].y)
                          + L", r" + std::to_wstring(shapes.discs[index].radius) + L")")
                           .c_str());
        }

        CompareScreens(cpu, zp.screen, port.canvas, where);
        discsSeen += static_cast<std::uint32_t>(shapes.discs.size());
        ++compared;
      }
      Elite::NextGalaxy(galaxy);
    }

    Logger::WriteMessage(("TT23: " + std::to_string(compared) + " short-range charts compared by whole screen, "
                          + std::to_string(discsSeen) + " system discs")
                           .c_str());
    Assert::IsTrue(discsSeen > 50, L"the sample should put a good number of systems on screen");
  }

  /*
   * 6502: HME2's HME3 loop -- find a system by name, for every system in two galaxies.
   *
   * It is run in BOTH case states, and the difference between them is a bug in the shipped game
   * rather than a detail of the test. The search forces bit 5 on in what was typed and leaves the
   * printed name alone, so it only matches while QQ17 has its "a letter has been seen" bit set.
   * The game's own prompt ends at CLYNS, which leaves QQ17 at %10000000 -- so the FIRST system of
   * every galaxy is printed in sentence case, does not match, and cannot be found by typing its
   * name. Every system after it can, because printing the first one set the bit.
   *
   * The port reproduces that rather than fixing it (ADR-003), and this is what stops a later
   * change quietly "improving" it.
   */
  TEST_METHOD(FindingASystemByNameMatchesTheShippedSearch)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t hme3 = 0x31D5;
    const std::uint16_t hme5 = 0x3208;
    const std::uint16_t typedName = 0x000E; // 6502: INWK+5, where MT26 leaves the typed line

    SystemSeeds galaxy = Elite::GALAXY_ONE_SEEDS;
    std::uint32_t compared = 0;
    std::uint32_t found = 0;
    std::uint32_t firstSystemMisses = 0;

    for (int galaxyNumber = 1; galaxyNumber <= 2; ++galaxyNumber)
    {
      SystemSeeds seeds = galaxy;

      for (int system = 0; system < 256; ++system)
      {
        // The name as a player would type it, taken from the port's own buffer so that the two
        // sides are being asked the same question.
        std::vector<std::uint8_t> typed;
        {
          Canvas scratchCanvas;
          Elite::TextState scratchText;
          Elite::TextPrinter scratchScreen(scratchCanvas, scratchText);
          Elite::CharacterPrinter scratchCharacters(scratchScreen);
          scratchCharacters.state.justify = 0x80;
          Elite::TokenPrinter scratchPrinter(scratchCharacters);
          SystemSeeds naming = seeds;
          Elite::PrintSystemName(scratchPrinter, naming);
          for (std::size_t index = 0; index < scratchCharacters.state.bufferLength; ++index)
          {
            typed.push_back(scratchCharacters.buffer[index]);
          }
        }
        typed.push_back(13);

        for (const std::uint32_t caseFlags : { 0x80u, 0xC0u })
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(oracle.Label("CHPR"), Cpu6502::TrapExit::ClearCarry);
          cpu.AddTrap(oracle.Label("TT103"));
          cpu.AddTrap(oracle.Label("TT111"));
          cpu.AddTrap(oracle.Label("NOISE"));
          cpu.AddTrap(hme5);

          /*
           * HME5 is trapped, so its own `LDA QQ15+3 / STA QQ9` never runs and QQ9 stays as it
           * was. What the search settled on is the SEEDS it stopped at, which is what HME5 would
           * have copied, so those are watched at the trap instead.
           */
          cpu.watch = { static_cast<std::uint16_t>(zp.qq15 + 3), static_cast<std::uint16_t>(zp.qq15 + 1), 0, 0 };

          LoadSeeds(cpu, zp.qq21, galaxy);
          for (std::size_t index = 0; index < typed.size(); ++index)
          {
            cpu.memory[static_cast<std::uint16_t>(typedName + index)] = typed[index];
          }
          cpu.memory[oracle.Label("QQ17")] = static_cast<std::uint8_t>(caseFlags);
          cpu.memory[oracle.Label("DTW1")] = 0;
          cpu.memory[oracle.Label("DTW2")] = 0;
          cpu.memory[oracle.Label("DTW3")] = 0;
          cpu.memory[oracle.Label("DTW4")] = 0;
          cpu.memory[oracle.Label("DTW5")] = 0;
          cpu.memory[oracle.Label("DTW6")] = 0;
          cpu.memory[oracle.Label("DTW8")] = 0xFF;

          // 6502: JSR TT81 -- the search always starts from the galaxy's own seeds.
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          Assert::IsTrue(cpu.CallSubroutine(oracle.Label("TT81"), 10'000).completed, L"TT81 should return");

          cpu.memory[oracle.Label("XX20")] = 0;
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          const auto run = cpu.CallSubroutine(hme3, 40'000'000);
          Assert::IsTrue(run.completed && !run.illegalOpcode, L"the shipped search should return");

          bool gameFound = false;
          std::uint8_t gameX = 0;
          std::uint8_t gameY = 0;
          for (const auto& hit : cpu.trapHits)
          {
            if (hit.address == hme5)
            {
              gameFound = true;
              gameX = hit.watched[0];
              gameY = hit.watched[1];
            }
          }

          Canvas canvas;
          Elite::TextState text;
          Elite::TextPrinter screen(canvas, text);
          Elite::CharacterPrinter characters(screen);
          Elite::TokenPrinter printer(characters);
          printer.SetCaseFlags(static_cast<std::uint8_t>(caseFlags));

          ChartView chart;
          const bool ourFound = Elite::FindSystemByName(printer, characters, chart, galaxy, typed);

          const std::wstring where = L"galaxy " + std::to_wstring(galaxyNumber) + L" system "
                                     + std::to_wstring(system) + L" QQ17=" + std::to_wstring(caseFlags);
          Assert::AreEqual(gameFound, ourFound, (where + L": found").c_str());

          if (gameFound)
          {
            Assert::AreEqual<std::uint32_t>(gameX, chart.cursorX, (where + L": QQ9").c_str());
            Assert::AreEqual<std::uint32_t>(gameY, chart.cursorY, (where + L": QQ10").c_str());
            ++found;
          }
          else if (system == 0 && caseFlags == 0x80u)
          {
            ++firstSystemMisses;
          }

          ++compared;
        }

        Elite::NextSystem(seeds);
      }

      Elite::NextGalaxy(galaxy);
    }

    Logger::WriteMessage(("HME2: " + std::to_string(compared) + " searches compared, " + std::to_string(found)
                          + " found. The first system of a galaxy was missed " + std::to_string(firstSystemMisses)
                          + " times with QQ17=128, which is the state the game's own prompt leaves.")
                           .c_str());

    // Both galaxies must show the shipped defect, or the port has quietly improved on the game.
    Assert::AreEqual<std::uint32_t>(2u, firstSystemMisses,
                                    L"the first system of each galaxy is unfindable in the state the prompt leaves");
    Assert::IsTrue(found > 900u, L"and everything else should be findable");
  }
};

} // namespace GameLogicTests

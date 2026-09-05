#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Canvas.h"
#include "Controls.h"
#include "DockedKeys.h"
#include "FlightLoop.h"
#include "KeyMap.h"
#include "LookupTables.h"
#include "ExtendedTokens.h"
#include "MarketScreen.h"
#include "Presentation.h"
#include "StartUp.h"
#include "TextPrint.h"
#include "Tokens.h"

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The half of the shell that is a decision rather than an API call (slice 2e).
 *
 * ADR-005 asks the executable for three things that are arithmetic -- a palette, an integer
 * scale with black bars, and a fixed-timestep accumulator -- and one that is a mapping, from a
 * Windows key to a Commodore 64 one. None of the four needs a GPU or a window, so none of them
 * is verified by compiling and hoping. They are here, and they run on both legs.
 *
 * What is deliberately NOT here is Direct3D, the message pump and the game thread. Those are the
 * part of slice 2e that only a Windows machine can check, and the split exists so that a person
 * debugging a blank window knows the palette and the viewport are not the reason.
 */
namespace GameLogicTests
{

  namespace
  {
    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

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

    /// 6502: SCBASE. An assembler constant rather than a label, so it is derived the way CanvasTests
    /// derives it -- from ylookup's first entry, which is SCBASE plus the space view's left margin.
    std::uint16_t ScreenBase(const OracleImage& _oracle)
    {
      const Cpu6502 cpu = _oracle.Fresh();
      const std::uint16_t low = _oracle.Label("ylookupl");
      const std::uint16_t high = _oracle.Label("ylookuph");
      return static_cast<std::uint16_t>((cpu.memory[low] | (cpu.memory[high] << 8)) - Elite::Canvas::SPACE_VIEW_MARGIN);
    }

    /// Every byte of text state the two screen seams touch, read out of wherever it lives.
    struct TextStateBytes
    {
      std::uint8_t column = 0;        ///< 6502: XC
      std::uint8_t row = 0;           ///< 6502: YC
      std::uint8_t caseFlags = 0;     ///< 6502: QQ17
      std::uint8_t lowerCaseBits = 0; ///< 6502: DTW1
      std::uint8_t sentenceStart = 0; ///< 6502: DTW2
      std::uint8_t alwaysLower = 0;   ///< 6502: DTW6

      [[nodiscard]] bool operator==(const TextStateBytes&) const = default;
    };

    TextStateBytes FromOracle(const Cpu6502& _cpu, const OracleImage& _oracle)
    {
      return {_cpu.memory[_oracle.Label("XC")],   _cpu.memory[_oracle.Label("YC")],   _cpu.memory[_oracle.Label("QQ17")],
              _cpu.memory[_oracle.Label("DTW1")], _cpu.memory[_oracle.Label("DTW2")], _cpu.memory[_oracle.Label("DTW6")]};
    }

    TextStateBytes FromPort(const Elite::TokenPrinter& _printer, const Elite::TextState& _text, const Elite::ExtendedTextState& _extended)
    {
      return {_text.column, _text.row, _printer.CaseFlags(), _extended.lowerCaseBits, _extended.sentenceStart, _extended.alwaysLower};
    }

    void Write(Cpu6502& _cpu, const OracleImage& _oracle, const TextStateBytes& _state)
    {
      _cpu.memory[_oracle.Label("XC")] = _state.column;
      _cpu.memory[_oracle.Label("YC")] = _state.row;
      _cpu.memory[_oracle.Label("QQ17")] = _state.caseFlags;
      _cpu.memory[_oracle.Label("DTW1")] = _state.lowerCaseBits;
      _cpu.memory[_oracle.Label("DTW2")] = _state.sentenceStart;
      _cpu.memory[_oracle.Label("DTW6")] = _state.alwaysLower;
    }

    /// Somewhere for the token printer's characters to go. Nothing here prints anything -- the
    /// comparison is the STATE the two routines leave -- and a sink that stored them would only make
    /// the failure message longer.
    struct Discard final : public Elite::TextSink
    {
      void Put(std::uint8_t) noexcept override {}
    };

    void CompareTextState(const TextStateBytes& _game, const TextStateBytes& _port, const wchar_t* _what)
    {
      Assert::AreEqual(_game.column, _port.column, (std::wstring(_what) + L": XC").c_str());
      Assert::AreEqual(_game.row, _port.row, (std::wstring(_what) + L": YC").c_str());
      Assert::AreEqual(_game.caseFlags, _port.caseFlags, (std::wstring(_what) + L": QQ17").c_str());
      Assert::AreEqual(_game.lowerCaseBits, _port.lowerCaseBits, (std::wstring(_what) + L": DTW1").c_str());
      Assert::AreEqual(_game.sentenceStart, _port.sentenceStart, (std::wstring(_what) + L": DTW2").c_str());
      Assert::AreEqual(_game.alwaysLower, _port.alwaysLower, (std::wstring(_what) + L": DTW6").c_str());
    }
  } // namespace

  /*
   * The two seams the shell answers with a screen clear, against the shipped routines (slice 2e).
   *
   * `TradeScreenEffects` and `ChartEffects` are seams because the REST of what they do is the
   * dashboard, the sprites and the border box, which are phase 3's. The text state they leave
   * behind is not phase 3's, and until the shell needed it nothing had checked it -- the per-screen
   * oracle tests set that state up themselves on BOTH sides, so a seam that got it wrong would
   * agree with the game on every screen and still print every one of them in the wrong case.
   *
   * Section 6.20 of the plan is the rule this follows: where a routine can be run, run it. Both of
   * these can, running them cost about what reading them would have -- and it caught a misreading
   * that reading them had produced. See section 6.29.
   */
  TEST_CLASS(TheScreenSeamsMatchTheShippedRoutines)
  {
  public:
    /*
     * 6502: TT66, which falls into TTX66 -- what a screen change leaves the text system in.
     *
     * Two routines are trapped and both are phase 3's: `TTX66K`, which redraws the dashboard, the
     * border box and the colour bands, and `FLFLLS`, which empties the ball line heap. Neither
     * touches a byte compared here, and letting them run would mean emulating the VIC-II.
     *
     * QQ11 is set to a trade view rather than to zero, and that is faithful rather than convenient:
     * a docked screen IS entered with QQ11 non-zero, so `LDA QQ11 / BNE tt66` takes the branch that
     * skips printing the space view's name -- which is the path the shell actually takes.
     */
    TEST_METHOD(TheTradeScreenSeamLeavesTheTextSystemWhereTT66Does)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("TTX66K"));
      cpu.AddTrap(oracle.Label("FLFLLS"));

      // 6502: LDX QQ22+1 / BEQ OLDBOX -- no jump counting down, so the countdown is not reprinted.
      cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ22") + 1u)] = 0;

      // The state a screen change is entered from, chosen so that NOTHING here is already the
      // answer: the cursor somewhere else, and every flag the opposite of what TT66 leaves.
      Write(cpu, oracle, {17, 9, 0xFF, 0, 0, 0xFF});

      cpu.a = Elite::BUY_CARGO_VIEW; // 6502: TT66 opens with STA QQ11
      const Elite::Testing::RunResult run = cpu.CallSubroutine(oracle.Label("TT66"));
      Assert::IsTrue(run.completed, L"TT66 returned");
      Assert::AreEqual<std::uint8_t>(Elite::BUY_CARGO_VIEW, cpu.memory[oracle.Label("QQ11")],
                                     L"and the view it was given is the view it set");

      Discard sink;
      Elite::CharacterPrinter characters{sink};
      Elite::TokenPrinter printer{characters};
      Elite::TextState text{17, 9, 0xFF, 0};
      printer.SetCursor(&text);
      printer.SetCaseFlags(0xFF);
      characters.state.lowerCaseBits = 0;
      characters.state.sentenceStart = 0;
      characters.state.alwaysLower = 0xFF;

      Elite::SetUpTextScreen(printer, text, characters.state);

      CompareTextState(FromOracle(cpu, oracle), FromPort(printer, text, characters.state), L"TT66");

      /*
       * And the assertion the whole test is about, stated so a reader does not have to reconstruct
       * it from the comparison: ALL CAPS, not sentence case. The routine stores 128 into QQ17 near
       * its top and zero into it five bytes from its end.
       */
      Assert::AreEqual<std::uint8_t>(0, printer.CaseFlags(), L"a screen change ends in ALL CAPS");
      Assert::AreEqual<std::uint8_t>(0x80, characters.state.sentenceStart, L"but DTW2 keeps the 128");
    }

    /*
     * 6502: CLYNS, which falls into CLYNS2 -- and this one needs no traps at all.
     *
     * The screen is compared as well as the state, and it is filled with a recognisable byte first
     * on both sides. Without that, "cleared the right three rows" and "cleared nothing, and the
     * screen was already zero" are the same measurement.
     */
    TEST_METHOD(TheMessageRowSeamClearsWhatCLYNSClears)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t screen = ScreenBase(oracle);

      Cpu6502 cpu = oracle.Fresh();
      Write(cpu, oracle, {17, 9, 0, 0, 0, 0xFF});

      Discard sink;
      Elite::CharacterPrinter characters{sink};
      Elite::TokenPrinter printer{characters};
      Elite::TextState text{17, 9, 0, 0};
      printer.SetCursor(&text);
      printer.SetCaseFlags(0);
      characters.state.lowerCaseBits = 0;
      characters.state.sentenceStart = 0;
      characters.state.alwaysLower = 0xFF;

      Elite::Canvas canvas;
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        canvas.Write(offset, 0xA5);
        cpu.memory[static_cast<std::uint16_t>(screen + offset)] = 0xA5;
      }

      const Elite::Testing::RunResult run = cpu.CallSubroutine(oracle.Label("CLYNS"));
      Assert::IsTrue(run.completed, L"CLYNS returned");

      // 6502: CLYNS clears the message counters too, which the port's copy did not until 3d-c
      // gave them a home (§6.67).
      Elite::MessageState message;
      message.delay = 0x5Au;
      message.append = 0x5Au;
      Elite::ClearMessageRows(canvas, printer, text, characters.state, message);
      Assert::AreEqual<std::uint8_t>(0, message.delay, L"CLYNS clears DLY");
      Assert::AreEqual<std::uint8_t>(0, message.append, L"and de");

      CompareTextState(FromOracle(cpu, oracle), FromPort(printer, text, characters.state), L"CLYNS");

      std::uint32_t cleared = 0;
      const std::span<const std::uint8_t> ours = canvas.Screen();
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        const std::uint8_t expected = cpu.memory[static_cast<std::uint16_t>(screen + offset)];
        Assert::AreEqual(expected, ours[offset], (L"CLYNS: the screen differs at offset " + std::to_wstring(offset)).c_str());
        cleared += (expected == 0) ? 1u : 0u;
      }

      // Three character rows of thirty-two cells: 768 bytes, and nothing else.
      Assert::AreEqual<std::uint32_t>(3u * 256u, cleared, L"three rows of 256 bytes, and no more");
      Assert::AreEqual<std::uint8_t>(Elite::MESSAGE_ROW, text.row, L"and the cursor is on the first of them");
    }
  };

  TEST_CLASS(TheShellsArithmetic)
  {
  public:
    /*
     * ADR-005 section 1: the largest integer factor that fits, centred, black bars around it.
     *
     * The cases are chosen for the boundaries rather than for plausible window sizes: exactly the
     * canvas, one pixel short of the next factor, exactly it, a client too small for 1x, and a
     * shape that is generous in one axis and mean in the other -- which is the case a port that
     * took the wrong minimum gets wrong, and the only one a maximised window on a wide monitor
     * ever produces.
     */
    TEST_METHOD(TheViewportIsTheLargestIntegerScaleThatFits)
    {
      struct Case
      {
        const char* what;
        int clientWidth;
        int clientHeight;
        Outpost::Viewport expected;
      };

      const std::vector<Case> CASES = {
        {"exactly the canvas", 320, 200, {0, 0, 320, 200, 1}},
        {"one pixel short of 2x in width", 639, 400, {159, 100, 320, 200, 1}},
        {"one pixel short of 2x in height", 640, 399, {160, 99, 320, 200, 1}},
        {"exactly 2x", 640, 400, {0, 0, 640, 400, 2}},
        {"a wide monitor, and the height is what binds", 1920, 800, {320, 0, 1280, 800, 4}},
        {"a tall window, and the width binds", 700, 2000, {30, 800, 640, 400, 2}},
        {"1080p, where the width is a multiple and the height is not", 1920, 1080, {160, 40, 1600, 1000, 5}},
        {"smaller than the canvas still gets 1x", 200, 100, {-60, -50, 320, 200, 1}},
        {"and clips evenly when the overflow is odd", 319, 199, {-1, -1, 320, 200, 1}},
        {"minimised", 0, 0, {0, 0, 0, 0, 0}},
        {"a negative client area", -8, 100, {0, 0, 0, 0, 0}},
      };

      for (const Case& item : CASES)
      {
        const Outpost::Viewport got = Outpost::FitCanvas(item.clientWidth, item.clientHeight);
        const std::wstring where = Widen(std::string("viewport: ") + item.what);

        Assert::AreEqual(item.expected.scale, got.scale, (where + L": the scale").c_str());
        Assert::AreEqual(item.expected.width, got.width, (where + L": the width").c_str());
        Assert::AreEqual(item.expected.height, got.height, (where + L": the height").c_str());
        Assert::AreEqual(item.expected.x, got.x, (where + L": the x").c_str());
        Assert::AreEqual(item.expected.y, got.y, (where + L": the y").c_str());
        Assert::AreEqual(item.expected.Empty(), got.Empty(), (where + L": whether it is empty").c_str());
      }

      /*
       * And the property the cases are examples of: whatever the client area, the image is a whole
       * number of canvases and it is centred to within a pixel. A scale computed with the wrong
       * rounding passes several of the cases above and fails this.
       */
      for (int width = 1; width <= 2400; width += 7)
      {
        for (int height = 1; height <= 1600; height += 11)
        {
          const Outpost::Viewport view = Outpost::FitCanvas(width, height);
          Assert::AreEqual(0, view.width % Elite::Canvas::WIDTH, L"the width is a whole number of canvases");
          Assert::AreEqual(0, view.height % Elite::Canvas::HEIGHT, L"and so is the height");
          Assert::AreEqual(view.scale, view.width / Elite::Canvas::WIDTH, L"and the scale agrees");
          Assert::IsTrue(view.x * 2 + view.width == width || view.x * 2 + view.width == width - 1, L"centred to within the odd pixel");
          Assert::IsTrue(view.y * 2 + view.height == height || view.y * 2 + view.height == height - 1, L"in both axes");
        }
      }
    }

    /// The palette packs the way a shader reads it, and every entry is opaque.
    TEST_METHOD(ThePaletteIsSixteenOpaqueColours)
    {
      const std::array<std::uint32_t, 16> packed = Outpost::PaletteAsRgba();

      std::set<std::uint32_t> distinct;
      for (std::size_t index = 0; index < packed.size(); ++index)
      {
        const Outpost::Colour& colour = Outpost::C64_PALETTE[index];
        Assert::AreEqual<std::uint32_t>(colour.red, packed[index] & 0xFFu, L"red in the low byte");
        Assert::AreEqual<std::uint32_t>(colour.green, (packed[index] >> 8) & 0xFFu, L"then green");
        Assert::AreEqual<std::uint32_t>(colour.blue, (packed[index] >> 16) & 0xFFu, L"then blue");
        Assert::AreEqual<std::uint32_t>(0xFFu, packed[index] >> 24, L"and opaque");
        distinct.insert(packed[index]);
      }

      // Sixteen DIFFERENT colours: a table with a duplicated row would make two of the game's
      // colours indistinguishable and nothing else would notice.
      Assert::AreEqual<std::size_t>(16, distinct.size(), L"sixteen distinct colours");
      Assert::AreEqual<std::uint32_t>(0xFF000000u, packed[0], L"index 0 is black");
      Assert::AreEqual<std::uint32_t>(0xFFFFFFFFu, packed[1], L"index 1 is white");
    }

    /*
     * The title ship's pace, which is a MEASUREMENT the shell reads rather than a rate it picks.
     *
     * `TITLE` has no `WSCAN` in it (§6.17), so the ship turns at whatever rate a 6510 gets through
     * `MVEIT` and `LL9` -- and that is not one rate, because `LL9` draws a distant ship as a dot
     * and a near one as a wireframe of long lines. The numbers come from
     * `CycleTests::TheTitleScreensLoopCostsWhatItCosts`, which runs the shipped routines and adds
     * the cycles up; this checks the curve through them behaves, which is the half a cycle count
     * cannot check for itself.
     */
    TEST_METHOD(TheTitleShipIsPacedByWhatATurnCosts)
    {
      // The two ends, straight off the measurement: a dot at the start and a full wireframe at the
      // distance the ship settles at.
      Assert::AreEqual(15'600.0 / Outpost::NTSC_CLOCK_HZ, Outpost::TitleTurnSeconds(96), 1e-9, L"a dot, 96 away");
      Assert::AreEqual(121'276.0 / Outpost::NTSC_CLOCK_HZ, Outpost::TitleTurnSeconds(1), 1e-9, L"a wireframe, settled");

      // 8.4 turns a second settled and 65 while it is still a dot -- which is the whole point, and
      // the reason a single rate would make the ship's arrival take eleven seconds instead of four.
      Assert::AreEqual(8.43, 1.0 / Outpost::TitleTurnSeconds(1), 0.05, L"settled: 8.4 turns a second");
      Assert::AreEqual(65.6, 1.0 / Outpost::TitleTurnSeconds(96), 0.5, L"approaching: 65 turns a second");

      /*
       * THE CURVE IS NOT MONOTONIC AND THE TABLE IS NOT SMOOTHED, which is worth an assertion of
       * its own because the obvious test is the wrong one. A turn costs what `LL9` does that turn,
       * and that depends on the ship's ORIENTATION as much as its distance: the measurement has
       * 59,400 cycles for 18 lines at a distance of 48 and 58,000 for 14 at a distance of 16, so
       * the middle of the approach wobbles by a couple of percent in the wrong direction. Asserting
       * that a turn never gets cheaper closer in fails at 47, on the data rather than on the code.
       *
       * So what is asserted is the trend across the three regimes -- dot, wireframe, and a
       * wireframe filling the screen -- and that every distance in between is bounded by the two
       * ends. That is what the pacing depends on; two percent of wobble in the middle is not.
       */
      const double dot = Outpost::TitleTurnSeconds(96);
      const double wireframe = Outpost::TitleTurnSeconds(32);
      const double settled = Outpost::TitleTurnSeconds(1);

      Assert::IsTrue(dot < wireframe, L"a dot is cheaper than a wireframe");
      Assert::IsTrue(wireframe < settled, L"and a wireframe than one across the middle of the screen");

      for (int distance = 96; distance >= 1; --distance)
      {
        const double seconds = Outpost::TitleTurnSeconds(static_cast<std::uint8_t>(distance));
        Assert::IsTrue(seconds >= dot - 1e-12, (L"never cheaper than a dot, at " + std::to_wstring(distance)).c_str());
        Assert::IsTrue(seconds <= settled + 1e-12, (L"never dearer than the settled ship, at " + std::to_wstring(distance)).c_str());
      }

      // Outside the table it holds rather than extrapolating: `TITLE` starts the ship at 96 and
      // stops it at 1, so neither of these can happen -- and neither should run off the end.
      Assert::AreEqual(Outpost::TitleTurnSeconds(96), Outpost::TitleTurnSeconds(255), 1e-12, L"beyond the table's far end");
      Assert::AreEqual(Outpost::TitleTurnSeconds(1), Outpost::TitleTurnSeconds(0), 1e-12, L"and beyond its near one");
    }

    /*
     * ADR-005 section 3: a fixed timestep, and steps are never SILENTLY skipped or doubled.
     *
     * The clamp is the part worth testing hardest. A breakpoint or a closed laptop lid leaves the
     * accumulator holding minutes; without a clamp the next call runs thousands of steps with no
     * presentation between them, so the game appears to hang and then teleports. With one it drops
     * the backlog -- and says it did, which is the difference between a design and a bug.
     */
    TEST_METHOD(TheStepPlannerNeverSkipsOrDoublesSilently)
    {
      constexpr double RATE = 15.0;
      constexpr double PERIOD = 1.0 / RATE;

      // Nothing has passed: no steps, and the accumulator is untouched.
      {
        const Outpost::StepPlan plan = Outpost::PlanSteps(0.0, 0.0, RATE);
        Assert::AreEqual(0, plan.steps, L"no time, no steps");
        Assert::AreEqual(0.0, plan.leftoverSeconds, 1e-12, L"and nothing accumulated");
        Assert::IsFalse(plan.stalled, L"and no stall");
      }

      // Just under a period, then just over: the step lands on the second call, not the first.
      {
        const Outpost::StepPlan first = Outpost::PlanSteps(PERIOD * 0.6, 0.0, RATE);
        Assert::AreEqual(0, first.steps, L"six tenths of a period is not a step");

        const Outpost::StepPlan second = Outpost::PlanSteps(PERIOD * 0.6, first.leftoverSeconds, RATE);
        Assert::AreEqual(1, second.steps, L"and the remainder carries into the next call");
        Assert::AreEqual(PERIOD * 0.2, second.leftoverSeconds, 1e-9, L"leaving a fifth over");
      }

      // Exactly three periods is three steps and nothing left.
      {
        const Outpost::StepPlan plan = Outpost::PlanSteps(PERIOD * 3.0, 0.0, RATE);
        Assert::AreEqual(3, plan.steps, L"three periods, three steps");
        Assert::AreEqual(0.0, plan.leftoverSeconds, 1e-9, L"and nothing over");
        Assert::IsFalse(plan.stalled, L"three is inside the clamp");
      }

      // A long gap is clamped, reported, and does not carry a backlog into the next call.
      {
        const Outpost::StepPlan plan = Outpost::PlanSteps(30.0, 0.0, RATE);
        Assert::AreEqual(Outpost::MAX_STEPS_PER_CALL, plan.steps, L"clamped to the maximum");
        Assert::IsTrue(plan.stalled, L"and it says so");
        Assert::AreEqual(0.0, plan.leftoverSeconds, 1e-12, L"the backlog is dropped, not carried");

        const Outpost::StepPlan next = Outpost::PlanSteps(0.0, plan.leftoverSeconds, RATE);
        Assert::AreEqual(0, next.steps, L"so the next call starts clean");
      }

      // A clock that went backwards adds nothing rather than unwinding the accumulator.
      {
        const Outpost::StepPlan plan = Outpost::PlanSteps(-5.0, PERIOD * 0.5, RATE);
        Assert::AreEqual(0, plan.steps, L"no steps");
        Assert::AreEqual(PERIOD * 0.5, plan.leftoverSeconds, 1e-12, L"and the accumulator is intact");
      }

      // A rate of zero or less cannot produce a period, so it produces no steps rather than a
      // division by zero.
      for (const double rate : {0.0, -1.0})
      {
        const Outpost::StepPlan plan = Outpost::PlanSteps(10.0, 0.0, rate);
        Assert::AreEqual(0, plan.steps, L"a rate of zero or less runs nothing");
      }

      /*
       * And the property: over a long run at a steady frame time, the number of steps taken tracks
       * the elapsed time. An accumulator that lost its remainder would drift, and drift is exactly
       * what no single-call assertion above can see.
       */
      double accumulated = 0.0;
      int steps = 0;
      constexpr double FRAME = 1.0 / 60.0;
      constexpr int FRAMES = 6000; // a hundred seconds at sixty frames a second
      for (int frame = 0; frame < FRAMES; ++frame)
      {
        const Outpost::StepPlan plan = Outpost::PlanSteps(FRAME, accumulated, RATE);
        accumulated = plan.leftoverSeconds;
        steps += plan.steps;
        Assert::IsFalse(plan.stalled, L"a steady sixty frames a second never stalls at fifteen steps");
      }

      const int expected = static_cast<int>(FRAMES * FRAME * RATE);
      Assert::IsTrue(
        steps == expected || steps == expected - 1,
        (L"a hundred seconds should be about " + std::to_wstring(expected) + L" steps, not " + std::to_wstring(steps)).c_str());
    }
  };

  TEST_CLASS(TheKeyMap)
  {
  public:
    /*
     * Every binding, through the game's own translation table, to the character the screens
     * compare against.
     *
     * This is the assertion the whole shape of `KeyMap` exists for. The port could have mapped a
     * virtual key straight to a character -- and it would have agreed with the game on the keys
     * somebody thought to try, and quietly disagreed on the rest. Mapping to the C64's MATRIX
     * POSITION and letting `TRANTABLE` do the translation means there is only one table, and it is
     * the game's.
     *
     * The flight keys are in here too, and their characters are the ODD-LOOKING half of the list:
     * the up arrow types "X" and the comma types "/", because those are the C64 keys the positions
     * belong to. That is the map working, not the map wrong -- the same key steers in the space
     * view and types in the line editor, exactly as it does on the original.
     */
    TEST_METHOD(EveryBoundKeyTranslatesToWhatTheScreensCompareAgainst)
    {
      struct Expected
      {
        int virtualKey;
        std::uint8_t character;
        const char* what;
      };

      const std::vector<Expected> CASES = {
        // The number row, which is what `gnum` and the line editor read as digits.
        {0x31, '1', "1"},
        {0x32, '2', "2"},
        {0x33, '3', "3"},
        {0x34, '4', "4"},
        {0x35, '5', "5"},
        {0x36, '6', "6"},
        {0x37, '7', "7"},
        {0x38, '8', "8"},
        {0x39, '9', "9"},

        // The letters, every one of which is the letter the C64 key carries.
        {0x41, 'A', "A -- fire"},
        {0x43, 'C', "C -- docking computer"},
        {0x44, 'D', "D"},
        {0x45, 'E', "E -- E.C.M."},
        {0x46, 'F', "F"},
        {0x48, 'H', "H"},
        {0x4A, 'J', "J -- in-system jump"},
        {0x4D, 'M', "M -- fire missile"},
        {0x4E, 'N', "N"},
        {0x4F, 'O', "O"},
        {0x50, 'P', "P -- cancel docking"},
        {0x54, 'T', "T -- target missile"},
        {0x55, 'U', "U -- unarm missile"},
        {0x59, 'Y', "Y"},

        // The steering keys, whose characters are the C64 keys they stand in for.
        {0x25, ',', "Left -- the C64's \"<\""},
        {0x27, '.', "Right -- the C64's \">\""},
        {0x26, 'X', "Up -- the C64's \"X\""},
        {0x28, 'S', "Down -- the C64's \"S\""},
        {0xBE, ' ', "period -- the C64's Space"},
        {0x20, ' ', "Space -- the C64's Space itself, which the title screen asks for"},
        {0xBC, '/', "comma -- the C64's \"?\""},
        {0x09, 2, "Tab -- the C64's Commodore key"},

        {0xC0, '@', "@"},
        {0x0D, 13, "RETURN"},
        {0x08, 127, "DELETE"},
        {0x1B, 27, "ESCAPE"},
      };

      for (const Expected& item : CASES)
      {
        const std::wstring where = Widen(std::string("key ") + item.what);
        const std::uint8_t c64 = Outpost::C64KeyFor(item.virtualKey);
        Assert::AreNotEqual<std::uint8_t>(Outpost::NO_KEY, c64, (where + L" should be bound").c_str());
        Assert::AreEqual(item.character, Outpost::CharacterFor(c64), (where + L": the character TRANTABLE gives it").c_str());
      }
    }

    /*
     * And the other half of the same key press: the DISPATCH sees the position, not the character.
     *
     * `TT102` compares against 37 for "8", never against `'8'` -- so a shell that handed it the
     * translated character would find that no docked screen key worked, and one that handed the
     * screens the position would find that nothing typed appeared. Both directions are asserted
     * from the same binding.
     *
     * The function keys are the new layout and the digits are the aliases they kept, so each of the
     * six screens is asserted twice from two different Windows keys.
     */
    TEST_METHOD(TheDispatchSeesThePositionAndTheScreensSeeTheCharacter)
    {
      struct Expected
      {
        int virtualKey;
        Elite::KeyAction action;
        const char* what;
      };

      const std::vector<Expected> CASES = {
        {0x70, Elite::KeyAction::LongRangeChart, "F1 -- Galactic Chart"},
        {0x71, Elite::KeyAction::ShortRangeChart, "F2 -- local chart"},
        {0x72, Elite::KeyAction::DataOnSystem, "F3 -- data on system"},
        {0x73, Elite::KeyAction::MarketPrice, "F4 -- market prices"},
        {0x74, Elite::KeyAction::StatusMode, "F5 -- status"},
        {0x75, Elite::KeyAction::Inventory, "F6 -- inventory"},
        {0x76, Elite::KeyAction::Launch, "F7 -- launch and the forward view"},

        {0x34, Elite::KeyAction::LongRangeChart, "4 -- F1's digit"},
        {0x35, Elite::KeyAction::ShortRangeChart, "5 -- F2's digit"},
        {0x36, Elite::KeyAction::DataOnSystem, "6 -- F3's digit"},
        {0x37, Elite::KeyAction::MarketPrice, "7 -- F4's digit"},
        {0x38, Elite::KeyAction::StatusMode, "8 -- F5's digit"},
        {0x39, Elite::KeyAction::Inventory, "9 -- F6's digit"},

        {0x31, Elite::KeyAction::BuyCargo, "1 -- buy"},
        {0x32, Elite::KeyAction::SellCargo, "2 -- sell"},
        {0x33, Elite::KeyAction::EquipShip, "3 -- equip"},
        {0xC0, Elite::KeyAction::DiskAccess, "@ -- disk menu"},
      };

      for (const Expected& item : CASES)
      {
        const std::wstring where = Widen(std::string("dispatch: ") + item.what);
        const std::uint8_t c64 = Outpost::C64KeyFor(item.virtualKey);

        // Docked, not on a chart, no jump counting down, no hyperspace key held.
        const Elite::KeyOutcome outcome = Elite::ActionForKey(c64, 0xFF, 0, 0, false);
        Assert::AreEqual(static_cast<int>(item.action), static_cast<int>(outcome.action), (where + L": where the dispatch goes").c_str());

        // And the same position, translated, is NOT what the dispatch wanted -- which is the point.
        const std::uint8_t character = Outpost::CharacterFor(c64);
        Assert::AreNotEqual(c64, character, (where + L": the position and the character differ").c_str());
      }

      // The three view changes, which are the flight half of the same table.
      for (const auto& item : {std::pair{0x77, Elite::VIEW_REAR}, std::pair{0x78, Elite::VIEW_LEFT}, std::pair{0x79, Elite::VIEW_RIGHT}})
      {
        const Elite::KeyOutcome outcome = Elite::ActionForKey(Outpost::C64KeyFor(item.first), 0x00, 0, 0, false);
        Assert::AreEqual(static_cast<int>(Elite::KeyAction::ChangeView), static_cast<int>(outcome.action),
                         L"a function key changes the view in flight");
        Assert::AreEqual<std::uint8_t>(item.second, outcome.view, L"to the right one");
      }
    }

    /*
     * Every key the flight loop and `DOKEY` watch has a Windows key that reaches it.
     *
     * This is the test the gap needed. `KeyMap` shipped with the docked half of the map and a
     * comment saying the flight controls would be read from `KYTB` "in phase 3"; phase 3 landed,
     * `Controls.h` and `FlightLoop.h` named all sixteen positions, and not one of them was ever
     * bound -- so the ship could not be steered, the lasers could not be fired and the map still
     * passed its own tests, because nothing asserted the other direction.
     */
    TEST_METHOD(EveryFlightControlHasAKey)
    {
      struct Expected
      {
        std::uint8_t c64Key;
        const char* what;
      };

      const std::vector<Expected> CONTROLS = {
        {Elite::KEY_SLOW_DOWN, "KY1 -- slow down"},
        {Elite::KEY_SPEED_UP, "KY2 -- speed up"},
        {Elite::KEY_ROLL_LEFT, "KY3 -- roll left"},
        {Elite::KEY_ROLL_RIGHT, "KY4 -- roll right"},
        {Elite::KEY_PITCH_UP, "KY5 -- climb"},
        {Elite::KEY_PITCH_DOWN, "KY6 -- dive"},
        {Elite::KEY_FIRE, "KY7 -- fire lasers"},
        {Elite::KEY_ENERGY_BOMB, "KY12 -- energy bomb"},
        {Elite::KEY_ESCAPE_POD, "KY13 -- escape capsule"},
        {Elite::KEY_ARM_MISSILE, "KY14 -- target missile"},
        {Elite::KEY_UNARM_MISSILE, "KY15 -- unarm missile"},
        {Elite::KEY_FIRE_MISSILE, "KY16 -- fire missile"},
        {Elite::KEY_ECM, "KY17 -- E.C.M."},
        {Elite::KEY_WARP, "KY18 -- in-system jump"},
        {Elite::KEY_DOCKING_COMPUTER, "KY19 -- docking computer on"},
        {Elite::KEY_CANCEL_DOCKING, "KY20 -- docking computer off"},
      };

      for (const Expected& item : CONTROLS)
      {
        bool bound = false;
        for (int index = 0; index < Outpost::BindingCount(); ++index)
        {
          bound = bound || (Outpost::Bindings()[index].c64Key == item.c64Key);
        }
        Assert::IsTrue(bound, (Widen(std::string("no Windows key reaches ") + item.what)).c_str());
      }
    }

    /*
     * No Windows key is bound twice, nothing is bound to "no key", and the C64 keys that ARE bound
     * twice are exactly the six the new layout moved to the function keys -- and Space.
     *
     * The map used to be one-to-one in both directions and this is where that stopped. Moving the
     * six information screens to F1 to F6 could not take the digits with them -- `gnum` reads a
     * quantity as the CHARACTER the position translates to, so "4" has to keep position 53 -- and a
     * second Windows key for the same position is the cheapest way to have both. The set is named
     * rather than merely permitted, so that an eighth alias is a failure and not a shrug.
     *
     * SPACE IS THE SEVENTH AND IT IS A DIFFERENT KIND OF THING. The other six are one C64 key
     * reachable from two PC keys because the layout moved. Space is the C64's own speed-up key,
     * which the layout moved to "." and then left unbound -- and it is the one key the GAME NAMES
     * in its own text, in "PRESS SPACE OR FIRE, COMMANDER.". `TITLE` takes any key, so an unbound
     * Space meant the title screen ignored the only press a player is told to make.
     */
    TEST_METHOD(OnlyTheMovedScreensAndSpaceAreBoundTwice)
    {
      const std::set<std::uint8_t> ALLOWED_ALIASES = {
        Elite::KEY_LONG_RANGE, Elite::KEY_SHORT_RANGE, Elite::KEY_DATA_ON_SYSTEM, Elite::KEY_MARKET_PRICE,
        Elite::KEY_STATUS,     Elite::KEY_INVENTORY,   Elite::KEY_SPEED_UP,
      };

      std::set<int> virtualKeys;
      std::set<std::uint8_t> c64Keys;
      std::set<std::uint8_t> aliased;

      for (int index = 0; index < Outpost::BindingCount(); ++index)
      {
        const Outpost::KeyBinding& binding = Outpost::Bindings()[index];
        const std::wstring where = Widen(std::string("binding: ") + binding.what);

        Assert::AreNotEqual<std::uint8_t>(Outpost::NO_KEY, binding.c64Key, (where + L" must not map to nothing").c_str());
        Assert::IsTrue(binding.c64Key < Elite::KEY_TRANSLATION.size(), (where + L" must be a position the hardware can report").c_str());
        Assert::IsTrue(virtualKeys.insert(binding.virtualKey).second, (where + L": that Windows key is already bound").c_str());

        if (!c64Keys.insert(binding.c64Key).second)
        {
          Assert::IsTrue(ALLOWED_ALIASES.count(binding.c64Key) == 1,
                         (where + L": that C64 key is already bound, and is not one of the six that may be").c_str());
          Assert::IsTrue(aliased.insert(binding.c64Key).second, (where + L": three Windows keys for one C64 key").c_str());
        }
      }

      Assert::AreEqual(ALLOWED_ALIASES.size(), aliased.size(), L"every allowed alias is actually used");

      /*
       * And the four arrows carry a SECOND C64 key each, which the walk above cannot see.
       *
       * `TT17` reads one cursor key per axis and takes the direction from SHIFT, so a PC arrow is
       * an axis key and, for two of them, a shift as well (§6.115). They are a separate table
       * because they are a separate question -- `C64KeyFor` answers "what does this key type",
       * and this answers "what does it aim" -- and the two never meet in the game: the crosshairs
       * are read from the logger and the dispatch is read from the queue.
       */
      struct Aim
      {
        int virtualKey;
        std::uint8_t axis;
        std::uint8_t shift;
        const wchar_t* what;
      };

      const Aim AIMS[] = {
        {0x27, Elite::KEY_CURSOR_X, Outpost::NO_KEY, L"Right"},
        {0x25, Elite::KEY_CURSOR_X, Elite::KEY_SHIFT_LEFT, L"Left"},
        {0x26, Elite::KEY_CURSOR_Y, Outpost::NO_KEY, L"Up"},
        {0x28, Elite::KEY_CURSOR_Y, Elite::KEY_SHIFT_LEFT, L"Down"},
      };

      for (const Aim& aim : AIMS)
      {
        const Outpost::CursorKeys keys = Outpost::CursorKeysFor(aim.virtualKey);
        Assert::AreEqual<std::uint32_t>(aim.axis, keys.axis, (std::wstring(aim.what) + L": the axis key").c_str());
        Assert::AreEqual<std::uint32_t>(aim.shift, keys.shift, (std::wstring(aim.what) + L": the shift").c_str());
      }

      /*
       * UP IS THE UNSHIFTED ONE AND DOWN IS THE SHIFTED ONE, which is the opposite of the x pair
       * and is not a slip: `TT17` ends the y axis with `EOR #%11111110`, so the unshifted key steps
       * `QQ10` by -1, and `QQ10` grows downwards on both charts. Asserting it here is what stops
       * somebody tidying the table into symmetry.
       */
      Assert::AreEqual<std::uint32_t>(Outpost::NO_KEY, Outpost::CursorKeysFor(0x26).shift, L"up presses no shift");
      Assert::AreNotEqual<std::uint32_t>(Outpost::NO_KEY, Outpost::CursorKeysFor(0x28).shift, L"and down does");

      // A key that aims nothing says so, which is what `Window::PressKey` tests before it stores.
      Assert::AreEqual<std::uint32_t>(Outpost::NO_KEY, Outpost::CursorKeysFor(0x41).axis, L"A does not aim");

      Assert::AreEqual<std::uint8_t>(Outpost::NO_KEY, Outpost::C64KeyFor(0x5A), L"Z is not bound");
      Assert::AreEqual<std::uint8_t>(0, Outpost::CharacterFor(Outpost::NO_KEY), L"and nothing pressed translates to nothing printable");
    }
  };

} // namespace GameLogicTests

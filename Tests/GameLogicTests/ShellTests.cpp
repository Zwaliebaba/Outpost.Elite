#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Canvas.h"
#include "DockedKeys.h"
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
  std::uint8_t column = 0;       ///< 6502: XC
  std::uint8_t row = 0;          ///< 6502: YC
  std::uint8_t caseFlags = 0;    ///< 6502: QQ17
  std::uint8_t lowerCaseBits = 0;///< 6502: DTW1
  std::uint8_t sentenceStart = 0;///< 6502: DTW2
  std::uint8_t alwaysLower = 0;  ///< 6502: DTW6

  [[nodiscard]] bool operator==(const TextStateBytes&) const = default;
};

TextStateBytes FromOracle(const Cpu6502& _cpu, const OracleImage& _oracle)
{
  return { _cpu.memory[_oracle.Label("XC")],   _cpu.memory[_oracle.Label("YC")],
           _cpu.memory[_oracle.Label("QQ17")], _cpu.memory[_oracle.Label("DTW1")],
           _cpu.memory[_oracle.Label("DTW2")], _cpu.memory[_oracle.Label("DTW6")] };
}

TextStateBytes FromPort(const Elite::TokenPrinter& _printer, const Elite::TextState& _text,
                        const Elite::ExtendedTextState& _extended)
{
  return { _text.column,           _text.row,
           _printer.CaseFlags(),   _extended.lowerCaseBits,
           _extended.sentenceStart, _extended.alwaysLower };
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
    Write(cpu, oracle, { 17, 9, 0xFF, 0, 0, 0xFF });

    cpu.a = Elite::BUY_CARGO_VIEW; // 6502: TT66 opens with STA QQ11
    const Elite::Testing::RunResult run = cpu.CallSubroutine(oracle.Label("TT66"));
    Assert::IsTrue(run.completed, L"TT66 returned");
    Assert::AreEqual<std::uint8_t>(Elite::BUY_CARGO_VIEW, cpu.memory[oracle.Label("QQ11")],
                                   L"and the view it was given is the view it set");

    Discard sink;
    Elite::CharacterPrinter characters{ sink };
    Elite::TokenPrinter printer{ characters };
    Elite::TextState text{ 17, 9, 0xFF, 0 };
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
    Write(cpu, oracle, { 17, 9, 0, 0, 0, 0xFF });

    Discard sink;
    Elite::CharacterPrinter characters{ sink };
    Elite::TokenPrinter printer{ characters };
    Elite::TextState text{ 17, 9, 0, 0 };
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

    Elite::ClearMessageRows(canvas, printer, text, characters.state);

    CompareTextState(FromOracle(cpu, oracle), FromPort(printer, text, characters.state), L"CLYNS");

    std::uint32_t cleared = 0;
    const std::span<const std::uint8_t> ours = canvas.Screen();
    for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
    {
      const std::uint8_t expected = cpu.memory[static_cast<std::uint16_t>(screen + offset)];
      Assert::AreEqual(expected, ours[offset],
                       (L"CLYNS: the screen differs at offset " + std::to_wstring(offset)).c_str());
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
      { "exactly the canvas", 320, 200, { 0, 0, 320, 200, 1 } },
      { "one pixel short of 2x in width", 639, 400, { 159, 100, 320, 200, 1 } },
      { "one pixel short of 2x in height", 640, 399, { 160, 99, 320, 200, 1 } },
      { "exactly 2x", 640, 400, { 0, 0, 640, 400, 2 } },
      { "a wide monitor, and the height is what binds", 1920, 800, { 320, 0, 1280, 800, 4 } },
      { "a tall window, and the width binds", 700, 2000, { 30, 800, 640, 400, 2 } },
      { "1080p, where the width is a multiple and the height is not", 1920, 1080, { 160, 40, 1600, 1000, 5 } },
      { "smaller than the canvas still gets 1x", 200, 100, { -60, -50, 320, 200, 1 } },
      { "and clips evenly when the overflow is odd", 319, 199, { -1, -1, 320, 200, 1 } },
      { "minimised", 0, 0, { 0, 0, 0, 0, 0 } },
      { "a negative client area", -8, 100, { 0, 0, 0, 0, 0 } },
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
        Assert::IsTrue(view.x * 2 + view.width == width || view.x * 2 + view.width == width - 1,
                       L"centred to within the odd pixel");
        Assert::IsTrue(view.y * 2 + view.height == height || view.y * 2 + view.height == height - 1,
                       L"in both axes");
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
    for (const double rate : { 0.0, -1.0 })
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
    Assert::IsTrue(steps == expected || steps == expected - 1,
                   (L"a hundred seconds should be about " + std::to_wstring(expected) + L" steps, not "
                    + std::to_wstring(steps))
                     .c_str());
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
      { 0x31, '1', "1" },  { 0x32, '2', "2" },  { 0x33, '3', "3" },  { 0x34, '4', "4" },
      { 0x35, '5', "5" },  { 0x36, '6', "6" },  { 0x37, '7', "7" },  { 0x38, '8', "8" },
      { 0x39, '9', "9" },  { 0x44, 'D', "D" },  { 0x46, 'F', "F" },  { 0x48, 'H', "H" },
      { 0x4F, 'O', "O" },  { 0x59, 'Y', "Y" },  { 0x4E, 'N', "N" },  { 0xC0, '@', "@" },
      { 0x0D, 13, "RETURN" }, { 0x08, 127, "DELETE" }, { 0x1B, 27, "ESCAPE" },
    };

    for (const Expected& item : CASES)
    {
      const std::wstring where = Widen(std::string("key ") + item.what);
      const std::uint8_t c64 = Outpost::C64KeyFor(item.virtualKey);
      Assert::AreNotEqual<std::uint8_t>(Outpost::NO_KEY, c64, (where + L" should be bound").c_str());
      Assert::AreEqual(item.character, Outpost::CharacterFor(c64),
                       (where + L": the character TRANTABLE gives it").c_str());
    }
  }

  /*
   * And the other half of the same key press: the DISPATCH sees the position, not the character.
   *
   * `TT102` compares against 37 for "8", never against `'8'` -- so a shell that handed it the
   * translated character would find that no docked screen key worked, and one that handed the
   * screens the position would find that nothing typed appeared. Both directions are asserted
   * from the same binding.
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
      { 0x38, Elite::KeyAction::StatusMode, "8 -- status" },
      { 0x37, Elite::KeyAction::MarketPrice, "7 -- market" },
      { 0x31, Elite::KeyAction::BuyCargo, "1 -- buy" },
      { 0x32, Elite::KeyAction::SellCargo, "2 -- sell" },
      { 0x33, Elite::KeyAction::EquipShip, "3 -- equip" },
      { 0x34, Elite::KeyAction::LongRangeChart, "4 -- long-range chart" },
      { 0x35, Elite::KeyAction::ShortRangeChart, "5 -- short-range chart" },
      { 0x36, Elite::KeyAction::DataOnSystem, "6 -- data on system" },
      { 0x39, Elite::KeyAction::Inventory, "9 -- inventory" },
      { 0x70, Elite::KeyAction::Launch, "F1 -- launch" },
      { 0xC0, Elite::KeyAction::DiskAccess, "@ -- disk menu" },
    };

    for (const Expected& item : CASES)
    {
      const std::wstring where = Widen(std::string("dispatch: ") + item.what);
      const std::uint8_t c64 = Outpost::C64KeyFor(item.virtualKey);

      // Docked, not on a chart, no jump counting down, no hyperspace key held.
      const Elite::KeyOutcome outcome = Elite::ActionForKey(c64, 0xFF, 0, 0, false);
      Assert::AreEqual(static_cast<int>(item.action), static_cast<int>(outcome.action),
                       (where + L": where the dispatch goes").c_str());

      // And the same position, translated, is NOT what the dispatch wanted -- which is the point.
      const std::uint8_t character = Outpost::CharacterFor(c64);
      Assert::AreNotEqual(c64, character, (where + L": the position and the character differ").c_str());
    }

    // The three views, which are the flight half of the same table.
    for (const auto& item : { std::pair{ 0x72, Elite::VIEW_REAR }, std::pair{ 0x74, Elite::VIEW_LEFT },
                              std::pair{ 0x76, Elite::VIEW_RIGHT } })
    {
      const Elite::KeyOutcome outcome =
        Elite::ActionForKey(Outpost::C64KeyFor(item.first), 0x00, 0, 0, false);
      Assert::AreEqual(static_cast<int>(Elite::KeyAction::ChangeView), static_cast<int>(outcome.action),
                       L"a function key changes the view in flight");
      Assert::AreEqual<std::uint8_t>(item.second, outcome.view, L"to the right one");
    }
  }

  /// No key is bound twice, in either direction, and nothing is bound to "no key".
  TEST_METHOD(TheMapIsOneToOne)
  {
    std::set<int> virtualKeys;
    std::set<std::uint8_t> c64Keys;

    for (int index = 0; index < Outpost::BindingCount(); ++index)
    {
      const Outpost::KeyBinding& binding = Outpost::Bindings()[index];
      const std::wstring where = Widen(std::string("binding: ") + binding.what);

      Assert::AreNotEqual<std::uint8_t>(Outpost::NO_KEY, binding.c64Key,
                                        (where + L" must not map to nothing").c_str());
      Assert::IsTrue(binding.c64Key < Elite::KEY_TRANSLATION.size(),
                     (where + L" must be a position the hardware can report").c_str());
      Assert::IsTrue(virtualKeys.insert(binding.virtualKey).second,
                     (where + L": that Windows key is already bound").c_str());
      Assert::IsTrue(c64Keys.insert(binding.c64Key).second,
                     (where + L": that C64 key is already bound").c_str());
    }

    Assert::AreEqual<std::uint8_t>(Outpost::NO_KEY, Outpost::C64KeyFor(0x5A), L"Z is not bound");
    Assert::AreEqual<std::uint8_t>(0, Outpost::CharacterFor(Outpost::NO_KEY),
                                   L"and nothing pressed translates to nothing printable");
  }
};

} // namespace GameLogicTests

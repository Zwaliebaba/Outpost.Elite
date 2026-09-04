#include "pch.h"

#include "OracleImage.h"

#include "Canvas.h"
#include "Commander.h"
#include "ExtendedTokens.h"
#include "Market.h"
#include "Rng.h"
#include "MarketScreen.h"
#include "StateTokens.h"
#include "Equipment.h"
#include "StatusScreen.h"
#include "TextPrint.h"
#include "Tokens.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Canvas;
using Elite::CharacterPrinter;
using Elite::DigitResult;
using Elite::KeySource;
using Elite::NumberEntry;
using Elite::TextState;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The docked trading screens, against the game that draws them (slice 2c).
 *
 * These are the routines slice 2c left behind as "loops around gnum", and the reason they can be
 * built now is the reason the charts could be built before their circles existed: the thing they
 * were waiting for is a SEAM, not a dependency. What they wait on is a key press, and a key press
 * arrives through Elite::KeySource here and through a scripted stepping loop in the oracle, so
 * both halves run the same real branches.
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

/*
 * The port's side of a scripted keyboard.
 *
 * It RECORDS running off the end rather than asserting there, and the caller checks afterwards.
 * That is not squeamishness: everything in GameLogic is `noexcept`, so an assertion thrown from
 * inside this override unwinds through a noexcept frame and calls std::terminate -- which turns a
 * caught mutation into a dead process with no named failing test. Found exactly that way, by a
 * mutation that asked for a thirteenth key.
 */
class ScriptedKeys : public KeySource
{
public:
  explicit ScriptedKeys(std::vector<std::uint8_t> _keys) noexcept : m_keys(std::move(_keys)) {}

  std::uint8_t NextKey() override
  {
    if (m_taken >= m_keys.size())
    {
      /*
       * A LETTER rather than a RETURN, because a letter is the only key that gets out of a
       * trading screen: gnum answers it with `JMP BAY2`, which abandons the screen entirely.
       *
       * That matters because the buy screen's retry loop is unbounded, as the original's is. A
       * mutation that made a quantity of zero fail the room check turned the loop into a hang,
       * and a hanging test reports nothing at all. This turns any overrun into a clean, named
       * failure -- m_overrun is asserted by the caller either way.
       */
      m_overrun = true;
      return 'C';
    }
    return m_keys[m_taken++];
  }

  [[nodiscard]] std::size_t Taken() const noexcept { return m_taken; }
  [[nodiscard]] bool Overran() const noexcept { return m_overrun; }

private:
  std::vector<std::uint8_t> m_keys;
  std::size_t m_taken = 0;
  bool m_overrun = false;
};

/*
 * The oracle's side of the same keyboard.
 *
 * TT217 blocks until a key is pressed, so it cannot simply be trapped and returned from -- the
 * caller needs the key in A. This steps the shipped routine and, whenever it arrives at TT217,
 * puts the next scripted key in A and performs the RTS by hand. That is exactly what a keyboard
 * is from the routine's point of view, and it keeps every other instruction real.
 */
struct KeyboardRun
{
  bool completed = false;
  bool leftEarly = false;   ///< stopped at `_leaveAt` rather than by returning
  std::size_t keysTaken = 0;
  std::uint32_t instructions = 0;
};

/*
 * `_leaveAt` is an address that ends the run as surely as a return does.
 *
 * gnum's letter exit is `JMP BAY2`, which does not return at all -- it transfers control to the
 * inventory screen, which reads the keyboard again. Without somewhere to stop, a test of gnum
 * would find itself running the next screen and asking for keys nobody scripted.
 */
KeyboardRun RunWithKeys(Cpu6502& _cpu, std::uint16_t _routine, std::uint16_t _keyRead,
                        const std::vector<std::uint8_t>& _keys, std::uint16_t _leaveAt = 0,
                        std::uint32_t _budget = 400'000, std::uint16_t _alsoLeaveAt = 0)
{
  constexpr std::uint16_t STOP = 0xFFF9;
  KeyboardRun run{};

  const std::uint8_t entrySp = _cpu.sp;
  const std::uint16_t ret = static_cast<std::uint16_t>(STOP - 1);
  _cpu.memory[static_cast<std::uint16_t>(0x0100 + _cpu.sp)] = static_cast<std::uint8_t>(ret >> 8);
  --_cpu.sp;
  _cpu.memory[static_cast<std::uint16_t>(0x0100 + _cpu.sp)] = static_cast<std::uint8_t>(ret & 0xFFu);
  --_cpu.sp;
  _cpu.pc = _routine;

  while (run.instructions < _budget)
  {
    /*
     * TWO addresses, because a screen can be left two ways and they are different labels. The
     * equipment shop's quiet exits go to BAY; gnum's letter exit is `JMP BAY2`, which is the
     * INVENTORY screen -- so a run that stopped only at BAY would go on to draw the whole
     * inventory and ask for keys nobody scripted.
     */
    if ((_leaveAt != 0 && _cpu.pc == _leaveAt) || (_alsoLeaveAt != 0 && _cpu.pc == _alsoLeaveAt))
    {
      run.completed = true;
      run.leftEarly = true;
      break;
    }

    if (_cpu.pc == STOP || _cpu.sp == entrySp)
    {
      run.completed = true;
      break;
    }

    if (_cpu.pc == _keyRead)
    {
      Assert::IsTrue(run.keysTaken < _keys.size(), L"the shipped routine asked for more keys than the script holds");
      _cpu.a = _keys[run.keysTaken++];

      // The RTS TT217 would have executed. sp is public, so this is the pop written out.
      const std::uint8_t lo = _cpu.memory[static_cast<std::uint16_t>(0x0100 + ((_cpu.sp + 1u) & 0xFFu))];
      const std::uint8_t hi = _cpu.memory[static_cast<std::uint16_t>(0x0100 + ((_cpu.sp + 2u) & 0xFFu))];
      _cpu.sp = static_cast<std::uint8_t>(_cpu.sp + 2u);
      _cpu.pc = static_cast<std::uint16_t>((lo | (hi << 8)) + 1);
      continue;
    }

    Assert::IsTrue(_cpu.Step(), L"the routine should not reach an unimplemented opcode");
    ++run.instructions;
  }

  return run;
}
/*
 * Every character with the cursor it was printed at, which is how the market screen is already
 * compared. A line in the right words at the wrong column fails.
 */
struct RecordingSink : public Elite::TextSink
{
  void Put(std::uint8_t _character) override
  {
    const std::uint32_t column = (cursor != nullptr) ? cursor->column : 0u;
    const std::uint32_t row = (cursor != nullptr) ? cursor->row : 0u;
    stamped.push_back(static_cast<std::uint32_t>(_character) | (column << 8) | (row << 16));
  }

  Elite::TextState* cursor = nullptr;
  std::vector<std::uint32_t> stamped;
};

std::wstring FirstDifference(const std::vector<std::uint32_t>& _game, const std::vector<std::uint32_t>& _port)
{
  std::size_t at = 0;
  while (at < _game.size() && at < _port.size() && _game[at] == _port[at])
  {
    ++at;
  }

  const auto window = [](const std::vector<std::uint32_t>& _seq, std::size_t _from) {
    std::wstring text = L"[";
    for (std::size_t index = _from; index < _seq.size() && index < _from + 12; ++index)
    {
      const std::uint8_t character = static_cast<std::uint8_t>(_seq[index]);
      text += (character >= 32 && character < 127) ? static_cast<wchar_t>(character) : L'.';
      text += L'@';
      text += std::to_wstring((_seq[index] >> 8) & 0xFFu);
      text += L',';
      text += std::to_wstring((_seq[index] >> 16) & 0xFFu);
      text += L' ';
    }
    return text + L"]";
  };

  return L"first difference at " + std::to_wstring(at) + L" of " + std::to_wstring(_game.size()) + L"/"
         + std::to_wstring(_port.size()) + L"; game " + window(_game, at) + L", port " + window(_port, at);
}

/// The seams a trading screen reaches, recorded rather than performed -- so the two sides can be
/// compared on WHEN they were reached as well as on what was printed.
class RecordingEffects : public Elite::TradeScreenEffects
{
public:
  void SetUpTradeScreen(std::uint8_t _view) override { log.push_back(static_cast<std::uint32_t>(0x100u + _view)); }
  void ClearBottomRows() override { log.push_back(0x200u); }
  void BeepAndPause() override { log.push_back(0x300u); }
  void ClearToView(std::uint8_t _view) override { log.push_back(static_cast<std::uint32_t>(0x400u + _view)); }
  void ResetMissileIndicators() override { log.push_back(0x500u); }

  std::vector<std::uint32_t> log;
};
} // namespace

TEST_CLASS(TypingAWholeNumberMatchesTheShippedGame)
{
public:
  /*
   * 6502: gnum, loop and all, against the port's ReadNumber.
   *
   * The step inside it is already compared over 393,216 keystrokes; what this adds is everything
   * the loop owns and the step cannot see -- the twelve-key limit, which keys are echoed to the
   * screen and which are not, and the text colour on each of the exits.
   *
   * The scripts are chosen for the loop's own branches rather than for the arithmetic: a number
   * ended by RETURN, one refused for being too large, "Y" and "N", a letter that abandons the
   * screen, and twelve digits in a row that end the number without the player ending it.
   */
  TEST_METHOD(ReadNumberMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t gnum = oracle.Label("gnum");
    const std::uint16_t tt217 = oracle.Label("TT217");
    const std::uint16_t r = oracle.Label("R");
    const std::uint16_t col2 = oracle.Label("COL2");
    const std::uint16_t dasc = oracle.Label("DASC");

    struct Script
    {
      const char* what;
      std::vector<std::uint8_t> keys;
      std::uint8_t available;
    };

    // 13 is RETURN on the C64, which is below '0' and so ends the number.
    const std::vector<Script> SCRIPTS = {
      { "one digit then RETURN", { '4', 13 }, 60 },
      { "two digits then RETURN", { '1', '2', 13 }, 60 },
      { "exactly the amount available", { '2', '5', 13 }, 25 },
      { "one more than available", { '2', '6' }, 25 },
      { "past 26 with room to spare", { '9', '9', '9' }, 200 },
      { "Y takes the lot", { 'Y' }, 37 },
      { "N takes none", { 'N' }, 37 },
      { "zero then Y", { '0', 'Y' }, 37 },
      { "a letter abandons the screen", { 'C' }, 60 },
      { "a letter after a digit", { '3', 'C' }, 60 },
      { "twelve zeros run the counter out", { '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0' }, 200 },
      { "nothing available", { '1', 13 }, 0 },
      { "RETURN straight away", { 13 }, 60 },
    };

    std::uint32_t compared = 0;

    for (const Script& script : SCRIPTS)
    {
      const std::wstring where = Widen(std::string("gnum: ") + script.what);

      // ---- the shipped routine, with the keyboard scripted ------------------------------
      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(dasc, Cpu6502::TrapExit::ClearCarry);
      cpu.memory[oracle.Label("QQ25")] = script.available;
      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;

      const KeyboardRun run = RunWithKeys(cpu, gnum, tt217, script.keys, oracle.Label("BAY2"));
      Assert::IsTrue(run.completed, (where + L": the shipped routine should return").c_str());

      // Which keys reached the screen, in order, from the trap on DASC.
      std::vector<std::uint8_t> gameEchoed;
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        gameEchoed.push_back(hit.a);
      }

      // ---- the port ----------------------------------------------------------------------
      Canvas canvas;
      TextState text;
      Elite::TextPrinter screen(canvas, text);
      CharacterPrinter characters(screen);
      ScriptedKeys keys(script.keys);

      std::vector<std::uint8_t> ourEchoed;
      class Recording : public Elite::TextSink
      {
      public:
        explicit Recording(std::vector<std::uint8_t>& _into) noexcept : m_into(_into) {}
        void Put(std::uint8_t _character) override { m_into.push_back(_character); }

      private:
        std::vector<std::uint8_t>& m_into;
      };
      Recording recording(ourEchoed);
      CharacterPrinter recordingCharacters(recording);

      const NumberEntry entry = Elite::ReadNumber(keys, recordingCharacters, text, script.available);

      // ---- compare -----------------------------------------------------------------------
      Assert::IsFalse(keys.Overran(), (where + L": the port asked for more keys than the script holds").c_str());
      Assert::AreEqual(run.keysTaken, keys.Taken(), (where + L": how many keys were read").c_str());
      Assert::AreEqual(cpu.memory[r], entry.value, (where + L": the number in R").c_str());
      Assert::AreEqual(cpu.memory[col2], text.cellColour, (where + L": the text colour on exit").c_str());

      /*
       * What a CALLER can tell apart, which is less than this enum carries.
       *
       * gnum leaves through OUT or through `JMP BAY2` and nothing else, so a caller sees three
       * things: the number in R, whether the carry came back set, and whether control came back
       * at all. TakeAll, TakeNone and Complete are one outcome from outside -- OUT with a clear
       * carry -- which is why the buy and sell screens never distinguish them.
       */
      Assert::AreEqual(run.leftEarly, entry.outcome == DigitResult::LeaveScreen,
                       (where + L": whether the screen was abandoned").c_str());
      if (!run.leftEarly)
      {
        Assert::AreEqual(cpu.c, entry.outcome == DigitResult::TooBig,
                         (where + L": the carry the caller branches on").c_str());
      }

      /*
       * And one invariant rather than a comparison: gnum cannot return in the middle of a
       * number. `Accepted` is TT226, which loops back for another key -- so a ReadNumber that
       * hands it to a caller has run its counter out without noticing, and the caller would treat
       * an unfinished number as a finished one. There is nothing in the shipped routine to
       * compare that against, because the shipped routine cannot do it.
       */
      Assert::IsTrue(entry.outcome != DigitResult::Accepted,
                     (where + L": gnum never returns mid-number").c_str());

      Assert::AreEqual(gameEchoed.size(), ourEchoed.size(), (where + L": how many characters were echoed").c_str());
      for (std::size_t index = 0; index < gameEchoed.size(); ++index)
      {
        Assert::AreEqual(gameEchoed[index], ourEchoed[index],
                         (where + L": echoed character " + std::to_wstring(index)).c_str());
      }

      ++compared;
    }

    Logger::WriteMessage(("gnum: " + std::to_string(compared) + " whole numbers typed, key for key\n").c_str());
  }
};

TEST_CLASS(TheBuyScreenMatchesTheShippedGame)
{
public:
  /*
   * 6502: TT219, whole screen, with the keyboard scripted.
   *
   * The scripts are chosen for the screen's own branches rather than for the arithmetic: buying
   * nothing at every item, buying something, and each of the three complaints -- a quantity too
   * large, a hold too full, and a purse too light -- which are the paths that loop back and ask
   * for the SAME item again.
   *
   * TRADEMODE, CLYNS and dn2 are trapped on the oracle and recorded on the port, and the two logs
   * are compared. dn2 has to be trapped: it ends in DELAY, which waits on RASTCT for fifty
   * vertical syncs, and nothing in an interpreter with no VIC-II ever changes it.
   */
  TEST_METHOD(BuyingCargoMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t chpr = oracle.Label("CHPR");
    const std::uint16_t tt217 = oracle.Label("TT217");
    const std::uint16_t trademode = oracle.Label("TRADEMODE");
    const std::uint16_t clyns = oracle.Label("CLYNS");
    const std::uint16_t dn2 = oracle.Label("dn2");

    struct Scenario
    {
      const char* what;
      std::vector<std::uint8_t> keys;
      std::uint32_t cash;      ///< in tenths
      std::uint8_t stock;      ///< how much of everything the market holds
      std::uint8_t alreadyHeld;///< how much of everything is already in the hold
    };

    // 13 is RETURN, which ends a number. A long tail of them walks the rest of the screen.
    const std::vector<std::uint8_t> RETURNS(24, 13);
    const auto withReturns = [&RETURNS](std::vector<std::uint8_t> _first) {
      _first.insert(_first.end(), RETURNS.begin(), RETURNS.end());
      return _first;
    };

    const std::vector<Scenario> SCENARIOS = {
      { "buy nothing anywhere", RETURNS, 1000u, 30, 0 },
      { "buy one of the first item", withReturns({ '1', 13 }), 1000u, 30, 0 },
      { "buy more than the market holds", withReturns({ '9', '9', 13 }), 100000u, 30, 0 },
      { "buy more than the hold takes", withReturns({ '2', '0', 13 }), 100000u, 30, 15 },
      { "buy more than the cash covers", withReturns({ '5', 13 }), 0u, 30, 0 },
      { "a letter leaves at once", { 'C' }, 1000u, 30, 0 },
      { "nothing in stock anywhere", RETURNS, 1000u, 0, 0 },
    };

    std::uint32_t compared = 0;

    for (const Scenario& scenario : SCENARIOS)
    {
      constexpr std::uint8_t ECONOMY = 3;
      constexpr std::uint8_t RANDOMISER = 37;

      const std::wstring where = Widen(std::string("TT219: ") + scenario.what);

      // ---- the shipped routine ------------------------------------------------------------
      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
      cpu.AddTrap(trademode);
      cpu.AddTrap(clyns);
      cpu.AddTrap(dn2);
      cpu.watch = { oracle.Label("XC"), oracle.Label("YC"), 0, 0 };

      Elite::CommanderBlock commander = Elite::DefaultCommander();
      commander.SetCash(scenario.cash);
      for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + item] = scenario.alreadyHeld;
      }

      Elite::MarketState market;
      market.randomiser = RANDOMISER;
      for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        market.availability[item] = scenario.stock;
      }

      for (std::size_t index = 0; index < 4; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("CASH") + index)] =
          static_cast<std::uint8_t>(scenario.cash >> (24 - 8 * index));
      }
      cpu.memory[oracle.Label("CRGO")] = commander.At(Elite::Field::CargoCapacity);
      cpu.memory[oracle.Label("QQ28")] = ECONOMY;
      cpu.memory[oracle.Label("QQ26")] = RANDOMISER;
      cpu.memory[oracle.Label("MJ")] = 0;
      cpu.memory[oracle.Label("TRIBBLE")] = 0;
      cpu.memory[static_cast<std::uint16_t>(oracle.Label("TRIBBLE") + 1)] = 0;
      for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("AVL") + item)] = scenario.stock;
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ20") + item)] = scenario.alreadyHeld;
      }

      // The state TRADEMODE would have left, since it is trapped on both sides.
      cpu.memory[oracle.Label("QQ17")] = 0;
      cpu.memory[oracle.Label("XC")] = 1;
      cpu.memory[oracle.Label("YC")] = 1;
      cpu.memory[oracle.Label("DTW1")] = 0;
      cpu.memory[oracle.Label("DTW2")] = 0xFF;
      cpu.memory[oracle.Label("DTW3")] = 0;
      cpu.memory[oracle.Label("DTW4")] = 0;
      cpu.memory[oracle.Label("DTW5")] = 0;
      cpu.memory[oracle.Label("DTW6")] = 0;
      cpu.memory[oracle.Label("DTW8")] = 0xFF;

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;

      const KeyboardRun run =
        RunWithKeys(cpu, oracle.Label("TT219"), tt217, scenario.keys, oracle.Label("BAY2"), 4'000'000);
      Assert::IsTrue(run.completed, (where + L": the shipped screen should finish").c_str());

      std::vector<std::uint32_t> expected;
      std::vector<std::uint32_t> gameEffects;
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        if (hit.address == chpr)
        {
          expected.push_back(static_cast<std::uint32_t>(hit.a) | (static_cast<std::uint32_t>(hit.watched[0]) << 8)
                             | (static_cast<std::uint32_t>(hit.watched[1]) << 16));
        }
        else if (hit.address == trademode)
        {
          gameEffects.push_back(0x100u + hit.a);
        }
        else if (hit.address == clyns)
        {
          gameEffects.push_back(0x200u);
        }
        else if (hit.address == dn2)
        {
          gameEffects.push_back(0x300u);
        }
      }

      // ---- the port ------------------------------------------------------------------------
      RecordingSink sink;
      Elite::TextState text;
      text.column = 1;
      text.row = 1;
      text.caseFlags = 0;
      sink.cursor = &text;
      Elite::CharacterPrinter characters(sink);
      characters.state.sentenceStart = 0xFF;
      Elite::TokenPrinter printer(characters);
      printer.SetCaseFlags(0);

      /*
       * The value tokens have to be attached, because `dn` prints recursive token 119 and that
       * token contains control code 0 -- the cash. Without them the screen prints "CASH:" and
       * then nothing, which is how the first version of this test failed.
       *
       * They are the CALLER's to wire up rather than the screen's, for the same reason the game
       * has one TT27 and one QQ17: a screen that owned its own would be a second token printer.
       */
      static constexpr std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> NAME = { 'J', 'A', 'M', 'E',
                                                                                     'S', 'O', 'N', 13 };
      Elite::SystemSeeds current = commander.GalaxySeeds();
      Elite::SystemSeeds selected = commander.GalaxySeeds();
      Elite::StateTokens values(printer, text, commander,
                                std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(NAME), current,
                                selected, false);
      printer.SetValueTokens(&values);
      printer.SetCursor(&text);

      ScriptedKeys keys(scenario.keys);
      RecordingEffects effects;
      Elite::Rng rng;
      Elite::ExtendedTokenPrinter extended(characters, printer, rng);
      Elite::TradeScreen screen{ printer, characters, extended, text, keys, effects, rng };

      Elite::BuyScreen(screen, commander, market, ECONOMY, false);

      // ---- compare -------------------------------------------------------------------------
      Assert::IsFalse(keys.Overran(), (where + L": the port asked for more keys than the script holds").c_str());
      Assert::AreEqual(run.keysTaken, keys.Taken(), (where + L": how many keys were read").c_str());

      if (sink.stamped != expected)
      {
        Assert::Fail((where + L" differs, " + FirstDifference(expected, sink.stamped)).c_str());
      }

      Assert::AreEqual(gameEffects.size(), effects.log.size(), (where + L": how many seams were reached").c_str());
      for (std::size_t index = 0; index < gameEffects.size(); ++index)
      {
        Assert::AreEqual(gameEffects[index], effects.log[index],
                         (where + L": seam " + std::to_wstring(index)).c_str());
      }

      // What the purchase actually did, which the screen alone does not show.
      for (std::size_t index = 0; index < 4; ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("CASH") + index)],
                         commander.bytes[static_cast<std::size_t>(Elite::Field::Cash) + index],
                         (where + L": cash byte " + std::to_wstring(index)).c_str());
      }
      for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ20") + item)],
                         commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + item],
                         (where + L": the hold, item " + std::to_wstring(item)).c_str());
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("AVL") + item)],
                         market.availability[item],
                         (where + L": what is left on the market, item " + std::to_wstring(item)).c_str());
      }

      ++compared;
    }

    Logger::WriteMessage(("TT219: " + std::to_string(compared)
                          + " buy screens compared character for character, with the cursor stamped\n")
                           .c_str());
  }
};

TEST_CLASS(TheCargoListingMatchesTheShippedGame)
{
public:
  /*
   * 6502: TT210 as the Sell Cargo screen, and TT213 as the Inventory screen.
   *
   * One routine, two screens, told apart by QQ11 -- so both are driven through the same
   * comparison with the view as a parameter. The scripts exercise selling nothing, selling some,
   * selling the lot with "Y", refusing with "N", a quantity larger than the hold (which reprints
   * the whole line rather than just the question), and a letter that abandons the screen.
   *
   * The inventory cases add what only that screen reaches: the large cargo bay line, and the
   * Trumble tail with none, one and several -- which prints almost nothing but moves the random
   * state, so the state is compared afterwards.
   */
  TEST_METHOD(ListingAndSellingCargoMatchTheShippedRoutines)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t chpr = oracle.Label("CHPR");
    const std::uint16_t tt217 = oracle.Label("TT217");
    const std::uint16_t trademode = oracle.Label("TRADEMODE");
    const std::uint16_t clyns = oracle.Label("CLYNS");
    const std::uint16_t dn2 = oracle.Label("dn2");
    const std::uint16_t rand = oracle.Label("RAND");

    struct Scenario
    {
      const char* what;
      std::uint8_t view;         ///< 4 sells, 8 lists
      std::vector<std::uint8_t> keys;
      std::uint8_t held;         ///< how much of everything is in the hold
      std::uint8_t capacity;     ///< CRGO as stored, so 22 is a standard hold and 37 a large one
      std::uint16_t trumbles;
    };

    const std::vector<std::uint8_t> RETURNS(24, 13);
    const auto withReturns = [&RETURNS](std::vector<std::uint8_t> _first) {
      _first.insert(_first.end(), RETURNS.begin(), RETURNS.end());
      return _first;
    };

    const std::vector<Scenario> SCENARIOS = {
      { "an empty hold, listed", Elite::INVENTORY_VIEW, {}, 0, 22, 0 },
      { "a full hold, listed", Elite::INVENTORY_VIEW, {}, 5, 22, 0 },
      { "a large cargo bay", Elite::INVENTORY_VIEW, {}, 3, 37, 0 },
      /*
       * The two capacities either side of the threshold. Neither is a capacity the game hands
       * out -- a standard hold stores 22 and a large one 37 -- but `CMP #26` reads CRGO as a
       * byte, and testing only the two real values leaves the constant free to move by one in
       * either direction. Found by mutation: 25 passed as readily as 26.
       */
      { "one below the large-bay threshold", Elite::INVENTORY_VIEW, {}, 3, 25, 0 },
      { "exactly the large-bay threshold", Elite::INVENTORY_VIEW, {}, 3, 26, 0 },
      { "one Trumble", Elite::INVENTORY_VIEW, {}, 2, 22, 1 },
      { "several Trumbles", Elite::INVENTORY_VIEW, {}, 2, 22, 700 },
      { "sell nothing", Elite::SELL_CARGO_VIEW, RETURNS, 5, 22, 0 },
      { "sell one of the first", Elite::SELL_CARGO_VIEW, withReturns({ '1', 13 }), 5, 22, 0 },
      { "sell the lot with Y", Elite::SELL_CARGO_VIEW, withReturns({ 'Y' }), 5, 22, 0 },
      { "refuse with N", Elite::SELL_CARGO_VIEW, withReturns({ 'N' }), 5, 22, 0 },
      { "more than the hold holds", Elite::SELL_CARGO_VIEW, withReturns({ '9', 13 }), 5, 22, 0 },
      { "a letter leaves at once", Elite::SELL_CARGO_VIEW, { 'C' }, 5, 22, 0 },
    };

    // A fixed random state, so the Trumble tail's DORND has somewhere to start from.
    const std::array<std::uint8_t, 4> SEED = { 0x21, 0x43, 0x65, 0x87 };

    std::uint32_t compared = 0;

    for (const Scenario& scenario : SCENARIOS)
    {
      constexpr std::uint8_t ECONOMY = 3;
      constexpr std::uint8_t RANDOMISER = 37;
      const bool inventory = scenario.view == Elite::INVENTORY_VIEW;

      const std::wstring where =
        Widen(std::string(inventory ? "TT213: " : "TT210: ") + scenario.what);

      // ---- the shipped routine ------------------------------------------------------------
      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
      cpu.AddTrap(trademode);
      cpu.AddTrap(clyns);
      cpu.AddTrap(dn2);
      cpu.watch = { oracle.Label("XC"), oracle.Label("YC"), 0, 0 };

      Elite::CommanderBlock commander = Elite::DefaultCommander();
      commander.SetCash(1000);
      commander.At(Elite::Field::CargoCapacity) = scenario.capacity;
      commander.At(Elite::Field::Tribbles) = static_cast<std::uint8_t>(scenario.trumbles & 0xFFu);
      commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles) + 1u] =
        static_cast<std::uint8_t>(scenario.trumbles >> 8);
      for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + item] = scenario.held;
      }

      Elite::MarketState market;
      market.randomiser = RANDOMISER;
      for (auto& stock : market.availability)
      {
        stock = 30;
      }

      for (std::size_t index = 0; index < 4; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("CASH") + index)] =
          commander.bytes[static_cast<std::size_t>(Elite::Field::Cash) + index];
        cpu.memory[static_cast<std::uint16_t>(rand + index)] = SEED[index];
      }
      cpu.memory[oracle.Label("CRGO")] = scenario.capacity;
      cpu.memory[oracle.Label("QQ14")] = commander.At(Elite::Field::Fuel);
      cpu.memory[oracle.Label("GCNT")] = commander.At(Elite::Field::GalaxyNumber);
      cpu.memory[oracle.Label("QQ28")] = ECONOMY;
      cpu.memory[oracle.Label("QQ26")] = RANDOMISER;
      cpu.memory[oracle.Label("QQ11")] = scenario.view;
      cpu.memory[oracle.Label("MJ")] = 0;
      cpu.memory[oracle.Label("TRIBBLE")] = commander.At(Elite::Field::Tribbles);
      cpu.memory[static_cast<std::uint16_t>(oracle.Label("TRIBBLE") + 1)] =
        commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles) + 1u];
      for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("AVL") + item)] = market.availability[item];
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ20") + item)] = scenario.held;
      }
      for (std::size_t index = 0; index < Elite::COMMANDER_NAME_SIZE; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("NAME") + index)] =
          Elite::DefaultCommanderName()[index];
      }
      const Elite::SystemSeeds seeds = commander.GalaxySeeds();
      for (std::size_t index = 0; index < 6; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ15") + index)] = seeds.bytes[index];
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ2") + index)] = seeds.bytes[index];
      }

      cpu.memory[oracle.Label("QQ17")] = 0;
      cpu.memory[oracle.Label("XC")] = 1;
      cpu.memory[oracle.Label("YC")] = 1;
      cpu.memory[oracle.Label("DTW1")] = 0;
      cpu.memory[oracle.Label("DTW2")] = 0xFF;
      cpu.memory[oracle.Label("DTW3")] = 0;
      cpu.memory[oracle.Label("DTW4")] = 0;
      cpu.memory[oracle.Label("DTW5")] = 0;
      cpu.memory[oracle.Label("DTW6")] = 0;
      cpu.memory[oracle.Label("DTW8")] = 0xFF;

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;

      const std::uint16_t entry = inventory ? oracle.Label("TT213") : oracle.Label("TT210");
      const KeyboardRun run =
        RunWithKeys(cpu, entry, tt217, scenario.keys, oracle.Label("BAY2"), 4'000'000);
      Assert::IsTrue(run.completed, (where + L": the shipped screen should finish").c_str());

      std::vector<std::uint32_t> expected;
      std::vector<std::uint32_t> gameEffects;
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        if (hit.address == chpr)
        {
          expected.push_back(static_cast<std::uint32_t>(hit.a) | (static_cast<std::uint32_t>(hit.watched[0]) << 8)
                             | (static_cast<std::uint32_t>(hit.watched[1]) << 16));
        }
        else if (hit.address == trademode)
        {
          gameEffects.push_back(0x100u + hit.a);
        }
        else if (hit.address == clyns)
        {
          gameEffects.push_back(0x200u);
        }
        else if (hit.address == dn2)
        {
          gameEffects.push_back(0x300u);
        }
      }

      // ---- the port ------------------------------------------------------------------------
      RecordingSink sink;
      Elite::TextState text;
      text.column = 1;
      text.row = 1;
      text.caseFlags = 0;
      sink.cursor = &text;
      Elite::CharacterPrinter characters(sink);
      characters.state.sentenceStart = 0xFF;
      Elite::TokenPrinter printer(characters);
      printer.SetCaseFlags(0);

      const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
      Elite::SystemSeeds current = seeds;
      Elite::SystemSeeds selected = seeds;
      Elite::StateTokens values(printer, text, commander,
                                std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name), current,
                                selected, false);
      printer.SetValueTokens(&values);
      printer.SetCursor(&text);

      ScriptedKeys keys(scenario.keys);
      RecordingEffects effects;
      Elite::Rng rng;
      rng.SetState(SEED);
      Elite::ExtendedTokenPrinter extended(characters, printer, rng);
      Elite::TradeScreen screen{ printer, characters, extended, text, keys, effects, rng };

      if (inventory)
      {
        Elite::InventoryScreen(screen, commander, market, ECONOMY);
      }
      else
      {
        Elite::ListCargo(screen, commander, market, ECONOMY, scenario.view);
      }

      // ---- compare -------------------------------------------------------------------------
      Assert::IsFalse(keys.Overran(), (where + L": the port asked for more keys than the script holds").c_str());
      Assert::AreEqual(run.keysTaken, keys.Taken(), (where + L": how many keys were read").c_str());

      if (sink.stamped != expected)
      {
        Assert::Fail((where + L" differs, " + FirstDifference(expected, sink.stamped)).c_str());
      }

      Assert::AreEqual(gameEffects.size(), effects.log.size(), (where + L": how many seams were reached").c_str());
      for (std::size_t index = 0; index < gameEffects.size(); ++index)
      {
        Assert::AreEqual(gameEffects[index], effects.log[index], (where + L": seam " + std::to_wstring(index)).c_str());
      }

      for (std::size_t index = 0; index < 4; ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("CASH") + index)],
                         commander.bytes[static_cast<std::size_t>(Elite::Field::Cash) + index],
                         (where + L": cash byte " + std::to_wstring(index)).c_str());

        // The Trumble tail calls DORND, so the random state is part of what this screen does.
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(rand + index)], rng.State()[index],
                         (where + L": random state byte " + std::to_wstring(index)).c_str());
      }
      for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ20") + item)],
                         commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + item],
                         (where + L": the hold, item " + std::to_wstring(item)).c_str());
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("AVL") + item)],
                         market.availability[item],
                         (where + L": the market, item " + std::to_wstring(item)).c_str());
      }

      ++compared;
    }

    Logger::WriteMessage(("TT210/TT213: " + std::to_string(compared)
                          + " cargo listings compared character for character, with the cursor stamped\n")
                           .c_str());
  }
};

TEST_CLASS(TheStatusScreenMatchesTheShippedGame)
{
public:
  /*
   * 6502: STATUS, whole screen, over the states it branches on.
   *
   * It reads no keys, so it needed no seam -- only the commander block. The cases cover the four
   * condition lines (docked, and green/yellow/red in space), the three legal statuses, the rating
   * either side of the 256-kill switch from shift-counting to comparisons, every combination of
   * the seven equipment flags at their edges, and all four laser types.
   */
  TEST_METHOD(TheStatusReportMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t chpr = oracle.Label("CHPR");
    const std::uint16_t trademode = oracle.Label("TRADEMODE");

    struct Situation
    {
      const char* what;
      std::uint8_t docked;
      std::uint8_t junk;
      std::uint8_t firstShip;
      std::uint8_t energy;
      std::uint8_t legal;
      std::uint16_t kills;
      std::uint8_t escapePod, fuelScoops, ecm, bomb, energyUnit, docking, galactic;
      std::array<std::uint8_t, 4> lasers;
    };

    const std::vector<Situation> SITUATIONS = {
      { "docked, clean, harmless, nothing fitted", 1, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "in space with nothing about", 0, 0, 0, 255, 0, 3, 0, 0, 0, 0, 0, 0, 0, { 15, 0, 0, 0 } },
      { "in space, ships about, energy high", 0, 0, 5, 200, 0, 3, 0, 0, 0, 0, 0, 0, 0, { 15, 0, 0, 0 } },
      { "in space, ships about, energy low", 0, 0, 5, 100, 0, 3, 0, 0, 0, 0, 0, 0, 0, { 15, 0, 0, 0 } },
      { "in space, energy exactly 128", 0, 2, 9, 128, 0, 3, 0, 0, 0, 0, 0, 0, 0, { 15, 0, 0, 0 } },
      { "an offender", 1, 0, 0, 255, 1, 5, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "one short of a fugitive", 1, 0, 0, 255, 49, 5, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "exactly a fugitive", 1, 0, 0, 255, 50, 5, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "one kill", 1, 0, 0, 255, 0, 1, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "four kills", 1, 0, 0, 255, 0, 4, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "255 kills", 1, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "256 kills, the switch to comparisons", 1, 0, 0, 255, 0, 256, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "512 kills", 1, 0, 0, 255, 0, 512, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "2560 kills", 1, 0, 0, 255, 0, 2560, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "6400 kills, Elite", 1, 0, 0, 255, 0, 6400, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } },
      { "everything fitted", 1, 0, 0, 255, 0, 100, 1, 1, 1, 1, 1, 1, 1, { 15, 143, 151, 50 } },
      { "only the galactic drive", 1, 0, 0, 255, 0, 100, 0, 0, 0, 0, 0, 0, 1, { 0, 0, 0, 0 } },
      { "only the energy bomb", 1, 0, 0, 255, 0, 100, 0, 0, 0, 1, 0, 0, 0, { 0, 0, 0, 0 } },
      { "a laser on every mount", 1, 0, 0, 255, 0, 100, 0, 0, 0, 0, 0, 0, 0, { 143, 151, 50, 15 } },
      { "a laser power nothing names", 1, 0, 0, 255, 0, 100, 0, 0, 0, 0, 0, 0, 0, { 99, 0, 0, 0 } },
    };

    std::uint32_t compared = 0;

    for (const Situation& s : SITUATIONS)
    {
      const std::wstring where = Widen(std::string("STATUS: ") + s.what);

      Elite::CommanderBlock commander = Elite::DefaultCommander();
      commander.At(Elite::Field::LegalStatus) = s.legal;
      commander.bytes[static_cast<std::size_t>(Elite::Field::Kills)] = static_cast<std::uint8_t>(s.kills & 0xFFu);
      commander.bytes[static_cast<std::size_t>(Elite::Field::Kills) + 1u] =
        static_cast<std::uint8_t>(s.kills >> 8);
      commander.At(Elite::Field::EscapePod) = s.escapePod;
      commander.At(Elite::Field::FuelScoops) = s.fuelScoops;
      commander.At(Elite::Field::Ecm) = s.ecm;
      commander.At(Elite::Field::EnergyBomb) = s.bomb;
      commander.At(Elite::Field::EnergyUnit) = s.energyUnit;
      commander.At(Elite::Field::DockingComputer) = s.docking;
      commander.At(Elite::Field::GalacticDrive) = s.galactic;
      for (std::size_t mount = 0; mount < 4; ++mount)
      {
        commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + mount] = s.lasers[mount];
      }

      constexpr std::uint8_t CROSSHAIR_X = 30;
      constexpr std::uint8_t CROSSHAIR_Y = 160;

      // ---- the shipped routine -----------------------------------------------------------
      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
      cpu.AddTrap(trademode);
      cpu.AddTrap(oracle.Label("NLIN"));
      cpu.watch = { oracle.Label("XC"), oracle.Label("YC"), 0, 0 };

      cpu.memory[oracle.Label("QQ12")] = s.docked;
      cpu.memory[oracle.Label("JUNK")] = s.junk;
      cpu.memory[static_cast<std::uint16_t>(oracle.Label("FRIN") + 2 + s.junk)] = s.firstShip;
      cpu.memory[oracle.Label("ENERGY")] = s.energy;
      cpu.memory[oracle.Label("FIST")] = s.legal;
      cpu.memory[oracle.Label("TALLY")] = static_cast<std::uint8_t>(s.kills & 0xFFu);
      cpu.memory[static_cast<std::uint16_t>(oracle.Label("TALLY") + 1)] = static_cast<std::uint8_t>(s.kills >> 8);
      cpu.memory[oracle.Label("ESCP")] = s.escapePod;
      cpu.memory[oracle.Label("BST")] = s.fuelScoops;
      cpu.memory[oracle.Label("ECM")] = s.ecm;
      cpu.memory[oracle.Label("BOMB")] = s.bomb;
      cpu.memory[oracle.Label("ENGY")] = s.energyUnit;
      cpu.memory[oracle.Label("DKCMP")] = s.docking;
      cpu.memory[oracle.Label("GHYP")] = s.galactic;
      for (std::size_t mount = 0; mount < 4; ++mount)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("LASER") + mount)] = s.lasers[mount];
      }
      cpu.memory[oracle.Label("QQ9")] = CROSSHAIR_X;
      cpu.memory[oracle.Label("QQ10")] = CROSSHAIR_Y;
      cpu.memory[oracle.Label("QQ0")] = commander.At(Elite::Field::SystemX);
      cpu.memory[oracle.Label("QQ1")] = commander.At(Elite::Field::SystemY);

      // The status screen's top four lines print the fuel and the cash through control codes 5
      // and 0, which is not obvious from its source -- they arrive inside recursive token 126.
      cpu.memory[oracle.Label("QQ14")] = commander.At(Elite::Field::Fuel);
      cpu.memory[oracle.Label("GCNT")] = commander.At(Elite::Field::GalaxyNumber);
      for (std::size_t index = 0; index < 4; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("CASH") + index)] =
          commander.bytes[static_cast<std::size_t>(Elite::Field::Cash) + index];
      }
      const Elite::SystemSeeds galaxy = commander.GalaxySeeds();
      for (std::size_t index = 0; index < 6; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ21") + index)] = galaxy.bytes[index];

        // QQ2 is the CURRENT system's seeds, which control code 2 prints. Leaving it at whatever
        // a fresh image holds made the game print an empty name where the port printed a real
        // one -- a hole in the setup rather than a difference in the port.
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ2") + index)] = galaxy.bytes[index];
      }
      for (std::size_t index = 0; index < Elite::COMMANDER_NAME_SIZE; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("NAME") + index)] =
          Elite::DefaultCommanderName()[index];
      }
      cpu.memory[oracle.Label("QQ17")] = 0;
      cpu.memory[oracle.Label("XC")] = 1;
      cpu.memory[oracle.Label("YC")] = 1;
      cpu.memory[oracle.Label("DTW1")] = 0;
      cpu.memory[oracle.Label("DTW2")] = 0xFF;
      cpu.memory[oracle.Label("DTW3")] = 0;
      cpu.memory[oracle.Label("DTW4")] = 0;
      cpu.memory[oracle.Label("DTW5")] = 0;
      cpu.memory[oracle.Label("DTW6")] = 0;
      cpu.memory[oracle.Label("DTW8")] = 0xFF;

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      const auto run = cpu.CallSubroutine(oracle.Label("STATUS"), 4'000'000);
      Assert::IsTrue(run.completed && !run.illegalOpcode, (where + L": STATUS should return").c_str());

      std::vector<std::uint32_t> expected;
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        if (hit.address == chpr)
        {
          expected.push_back(static_cast<std::uint32_t>(hit.a) | (static_cast<std::uint32_t>(hit.watched[0]) << 8)
                             | (static_cast<std::uint32_t>(hit.watched[1]) << 16));
        }
      }

      // ---- the port ------------------------------------------------------------------------
      RecordingSink sink;
      Elite::TextState text;
      text.column = 1;
      text.row = 1;
      text.caseFlags = 0;
      sink.cursor = &text;
      Elite::CharacterPrinter characters(sink);
      characters.state.sentenceStart = 0xFF;
      Elite::TokenPrinter printer(characters);
      printer.SetCaseFlags(0);

      const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
      Elite::SystemSeeds current = galaxy;
      Elite::SystemSeeds selected = galaxy;
      Elite::StateTokens values(printer, text, commander,
                                std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name), current,
                                selected, false);
      printer.SetValueTokens(&values);
      printer.SetCursor(&text);

      ScriptedKeys keys({});
      RecordingEffects effects;
      Elite::Rng rng;
      Elite::ExtendedTokenPrinter extended(characters, printer, rng);
      Elite::TradeScreen screen{ printer, characters, extended, text, keys, effects, rng };

      const Elite::ShipCondition condition{ s.docked, s.junk, s.firstShip, s.energy };
      Elite::StatusScreen(screen, commander, condition, CROSSHAIR_X, CROSSHAIR_Y, selected);

      if (sink.stamped != expected)
      {
        Assert::Fail((where + L" differs, " + FirstDifference(expected, sink.stamped)).c_str());
      }

      /*
       * QQ17 at the end, which is state the characters alone do not show.
       *
       * The docked branch prints its newline through TT67K -- LDA #12 falling straight into CHPR
       * -- rather than through TT67, which goes via TT27. For character 12 the two print the same
       * byte, but TT27's path CLEARS the "first letter seen" bit on a non-letter and TT67K does
       * not touch QQ17 at all, so the next letter would capitalise differently. Comparing the
       * flags is what tells them apart; a mutation swapping one for the other passed without it.
       */
      Assert::AreEqual(cpu.memory[oracle.Label("QQ17")], printer.CaseFlags(),
                       (where + L": the case flags at the end").c_str());

      // TT111 leaves the system it found in QQ15, and the screen's title line reads it.
      for (std::size_t index = 0; index < 6; ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ15") + index)],
                         selected.bytes[index],
                         (where + L": the selected system's seed byte " + std::to_wstring(index)).c_str());
      }

      ++compared;
    }

    Logger::WriteMessage(("STATUS: " + std::to_string(compared)
                          + " status screens compared character for character, with the cursor stamped\n")
                           .c_str());
  }
};

TEST_CLASS(TheEquipmentShopMatchesTheShippedGame)
{
public:
  /*
   * 6502: EQSHP, with prx, qv, eq and refund.
   *
   * Every branch of the thirteen-comparison chain, each item bought once into an empty ship and
   * once into one that already has it, plus the two lasers that need a view chosen, the refund
   * for replacing one laser with another, and the three ways out.
   *
   * TT66 and msblob are trapped alongside the usual three, so the view menu's screen clear and
   * the dashboard's missile indicators are compared as seam calls rather than skipped.
   */
  TEST_METHOD(BuyingEquipmentMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t chpr = oracle.Label("CHPR");
    const std::uint16_t tt217 = oracle.Label("TT217");
    const std::uint16_t trademode = oracle.Label("TRADEMODE");
    const std::uint16_t clyns = oracle.Label("CLYNS");
    const std::uint16_t dn2 = oracle.Label("dn2");
    const std::uint16_t tt66 = oracle.Label("TT66");
    const std::uint16_t msblob = oracle.Label("msblob");

    struct Scenario
    {
      const char* what;
      std::uint8_t tech;
      std::vector<std::uint8_t> keys;
      std::uint32_t cash;                 ///< in tenths
      std::uint8_t fuel;
      std::uint8_t capacity;
      std::uint8_t missiles;
      std::array<std::uint8_t, 7> fitted; ///< ECM, scoops, pod, bomb, unit, docking, galactic
      std::array<std::uint8_t, 4> lasers;
    };

    static constexpr std::array<std::uint8_t, 7> NOTHING = { 0, 0, 0, 0, 0, 0, 0 };
    static constexpr std::array<std::uint8_t, 4> NO_LASERS = { 0, 0, 0, 0 };
    const std::uint32_t RICH = 999999u;

    // 13 is RETURN. A purchase redraws the screen and asks again, so every script ends with one.
    const std::vector<Scenario> SCENARIOS = {
      { "nothing entered", 5, { 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "a letter", 5, { 'C' }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "a number past what is sold", 5, { '9', 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "fill the tank", 5, { '1', 13, 13 }, RICH, 20, 22, 0, NOTHING, NO_LASERS },
      { "fill an already full tank", 5, { '1', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a missile", 5, { '2', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a fifth missile", 5, { '2', 13 }, RICH, 70, 22, 4, NOTHING, NO_LASERS },
      { "buy a large cargo bay", 5, { '3', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a second large cargo bay", 5, { '3', 13 }, RICH, 70, 37, 0, NOTHING, NO_LASERS },
      { "buy an ECM", 5, { '4', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a second ECM", 5, { '4', 13 }, RICH, 70, 22, 0, { 1, 0, 0, 0, 0, 0, 0 }, NO_LASERS },
      { "buy a pulse laser, front", 9, { '5', 13, '0', 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a beam laser over a pulse", 9, { '6', 13, '1', 13 }, RICH, 70, 22, 0, NOTHING, { 0, 15, 0, 0 } },
      { "an invalid view then a valid one", 9, { '6', 13, '9', '2', 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy fuel scoops", 9, { '7', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a second set of scoops", 9, { '7', 13 }, RICH, 70, 22, 0, { 0, 1, 0, 0, 0, 0, 0 }, NO_LASERS },
      { "buy an escape pod", 9, { '8', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a second escape pod", 9, { '8', 13 }, RICH, 70, 22, 0, { 0, 0, 1, 0, 0, 0, 0 }, NO_LASERS },
      { "buy an energy bomb", 9, { '9', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a second energy bomb", 9, { '9', 13 }, RICH, 70, 22, 0, { 0, 0, 0, 1, 0, 0, 0 }, NO_LASERS },
      { "buy an energy unit", 14, { '1', '0', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a second energy unit", 14, { '1', '0', 13 }, RICH, 70, 22, 0, { 0, 0, 0, 0, 1, 0, 0 }, NO_LASERS },
      { "buy a docking computer", 14, { '1', '1', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a second docking computer", 14, { '1', '1', 13 }, RICH, 70, 22, 0, { 0, 0, 0, 0, 0, 1, 0 }, NO_LASERS },
      { "buy a galactic hyperdrive", 14, { '1', '2', 13, 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a second galactic drive", 14, { '1', '2', 13 }, RICH, 70, 22, 0, { 0, 0, 0, 0, 0, 0, 1 }, NO_LASERS },
      { "buy a military laser, left", 14, { '1', '3', 13, '2', 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "buy a mining laser over a beam", 14, { '1', '4', 13, '3', 13 }, RICH, 70, 22, 0, NOTHING, { 0, 0, 0, 143 } },
      /*
       * The other two laser powers the refund knows, and one it does not.
       *
       * `refund` matches the OLD laser's power against pulse, beam and military and falls through
       * to the mining laser's price for anything else -- so replacing a military laser and
       * replacing a power nothing names are different amounts of money, and a mutation that
       * conflated them passed until these three lines existed.
       */
      { "replace a military laser", 14, { '5', 13, '0', 13 }, RICH, 70, 22, 0, NOTHING, { 151, 0, 0, 0 } },
      { "replace a mining laser", 14, { '6', 13, '0', 13 }, RICH, 70, 22, 0, NOTHING, { 50, 0, 0, 0 } },
      { "replace a power nothing names", 14, { '5', 13, '0', 13 }, RICH, 70, 22, 0, NOTHING, { 99, 0, 0, 0 } },
      { "cannot afford it", 14, { '1', '2', 13 }, 100u, 70, 22, 0, NOTHING, NO_LASERS },
      { "a station that sells almost nothing", 0, { 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "the tech level at the cap's edge", 8, { 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      { "one below the menu's screen clear", 7, { '5', 13, '0', 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
      /*
       * A laser bought at exactly tech level 8, which is where `CMP #8 / BCC` decides whether the
       * view menu clears the screen first. Tech 7 and 9 straddle it and neither pins it -- a
       * mutation moving the test to `> 8` passed until this line existed.
       */
      { "a laser at the menu's threshold", 8, { '5', 13, '0', 13 }, RICH, 70, 22, 0, NOTHING, NO_LASERS },
    };

    std::uint32_t compared = 0;

    for (const Scenario& s : SCENARIOS)
    {
      const std::wstring where = Widen(std::string("EQSHP: ") + s.what);

      Elite::CommanderBlock commander = Elite::DefaultCommander();
      commander.SetCash(s.cash);
      commander.At(Elite::Field::Fuel) = s.fuel;
      commander.At(Elite::Field::CargoCapacity) = s.capacity;
      commander.At(Elite::Field::Missiles) = s.missiles;
      commander.At(Elite::Field::Ecm) = s.fitted[0];
      commander.At(Elite::Field::FuelScoops) = s.fitted[1];
      commander.At(Elite::Field::EscapePod) = s.fitted[2];
      commander.At(Elite::Field::EnergyBomb) = s.fitted[3];
      commander.At(Elite::Field::EnergyUnit) = s.fitted[4];
      commander.At(Elite::Field::DockingComputer) = s.fitted[5];
      commander.At(Elite::Field::GalacticDrive) = s.fitted[6];
      for (std::size_t mount = 0; mount < 4; ++mount)
      {
        commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + mount] = s.lasers[mount];
      }

      // ---- the shipped routine ------------------------------------------------------------
      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
      cpu.AddTrap(trademode);
      cpu.AddTrap(clyns);
      cpu.AddTrap(dn2);
      cpu.AddTrap(tt66);
      cpu.AddTrap(msblob);
      cpu.watch = { oracle.Label("XC"), oracle.Label("YC"), 0, 0 };

      cpu.memory[oracle.Label("tek")] = s.tech;
      cpu.memory[oracle.Label("QQ14")] = s.fuel;
      cpu.memory[oracle.Label("CRGO")] = s.capacity;
      cpu.memory[oracle.Label("NOMSL")] = s.missiles;
      cpu.memory[oracle.Label("ECM")] = s.fitted[0];
      cpu.memory[oracle.Label("BST")] = s.fitted[1];
      cpu.memory[oracle.Label("ESCP")] = s.fitted[2];
      cpu.memory[oracle.Label("BOMB")] = s.fitted[3];
      cpu.memory[oracle.Label("ENGY")] = s.fitted[4];
      cpu.memory[oracle.Label("DKCMP")] = s.fitted[5];
      cpu.memory[oracle.Label("GHYP")] = s.fitted[6];
      for (std::size_t mount = 0; mount < 4; ++mount)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("LASER") + mount)] = s.lasers[mount];
      }
      for (std::size_t index = 0; index < 4; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("CASH") + index)] =
          commander.bytes[static_cast<std::size_t>(Elite::Field::Cash) + index];
      }
      cpu.memory[oracle.Label("GCNT")] = commander.At(Elite::Field::GalaxyNumber);
      cpu.memory[oracle.Label("QQ17")] = 0;
      cpu.memory[oracle.Label("XC")] = 1;
      cpu.memory[oracle.Label("YC")] = 1;
      cpu.memory[oracle.Label("DTW1")] = 0;
      cpu.memory[oracle.Label("DTW2")] = 0xFF;
      cpu.memory[oracle.Label("DTW3")] = 0;
      cpu.memory[oracle.Label("DTW4")] = 0;
      cpu.memory[oracle.Label("DTW5")] = 0;
      cpu.memory[oracle.Label("DTW6")] = 0;
      cpu.memory[oracle.Label("DTW8")] = 0xFF;

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;

      const KeyboardRun run =
        RunWithKeys(cpu, oracle.Label("EQSHP"), tt217, s.keys, oracle.Label("BAY"), 8'000'000,
                    oracle.Label("BAY2"));
      Assert::IsTrue(run.completed, (where + L": the shipped screen should finish").c_str());

      std::vector<std::uint32_t> expected;
      std::vector<std::uint32_t> gameEffects;
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        if (hit.address == chpr)
        {
          expected.push_back(static_cast<std::uint32_t>(hit.a) | (static_cast<std::uint32_t>(hit.watched[0]) << 8)
                             | (static_cast<std::uint32_t>(hit.watched[1]) << 16));
        }
        else if (hit.address == trademode)
        {
          gameEffects.push_back(0x100u + hit.a);
        }
        else if (hit.address == clyns)
        {
          gameEffects.push_back(0x200u);
        }
        else if (hit.address == dn2)
        {
          gameEffects.push_back(0x300u);
        }
        else if (hit.address == tt66)
        {
          gameEffects.push_back(0x400u + hit.a);
        }
        else if (hit.address == msblob)
        {
          gameEffects.push_back(0x500u);
        }
      }

      // ---- the port ------------------------------------------------------------------------
      RecordingSink sink;
      Elite::TextState text;
      text.column = 1;
      text.row = 1;
      text.caseFlags = 0;
      sink.cursor = &text;
      Elite::CharacterPrinter characters(sink);
      characters.state.sentenceStart = 0xFF;
      Elite::TokenPrinter printer(characters);
      printer.SetCaseFlags(0);

      const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
      Elite::SystemSeeds current = commander.GalaxySeeds();
      Elite::SystemSeeds selected = commander.GalaxySeeds();
      Elite::StateTokens values(printer, text, commander,
                                std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name), current,
                                selected, false);
      printer.SetValueTokens(&values);
      printer.SetCursor(&text);

      ScriptedKeys keys(s.keys);
      RecordingEffects effects;
      Elite::Rng rng;
      Elite::ExtendedTokenPrinter extended(characters, printer, rng);
      Elite::TradeScreen screen{ printer, characters, extended, text, keys, effects, rng };

      Elite::EquipShipScreen(screen, commander, s.tech);

      // ---- compare -------------------------------------------------------------------------
      Assert::IsFalse(keys.Overran(), (where + L": the port asked for more keys than the script holds").c_str());
      Assert::AreEqual(run.keysTaken, keys.Taken(), (where + L": how many keys were read").c_str());

      if (sink.stamped != expected)
      {
        Assert::Fail((where + L" differs, " + FirstDifference(expected, sink.stamped)).c_str());
      }

      Assert::AreEqual(gameEffects.size(), effects.log.size(), (where + L": how many seams were reached").c_str());
      for (std::size_t index = 0; index < gameEffects.size(); ++index)
      {
        Assert::AreEqual(gameEffects[index], effects.log[index], (where + L": seam " + std::to_wstring(index)).c_str());
      }

      // Everything a purchase can change.
      struct Check
      {
        const char* name;
        std::uint16_t address;
        Elite::Field field;
      };
      const Check CHECKS[] = {
        { "fuel", oracle.Label("QQ14"), Elite::Field::Fuel },
        { "cargo capacity", oracle.Label("CRGO"), Elite::Field::CargoCapacity },
        { "missiles", oracle.Label("NOMSL"), Elite::Field::Missiles },
        { "ECM", oracle.Label("ECM"), Elite::Field::Ecm },
        { "fuel scoops", oracle.Label("BST"), Elite::Field::FuelScoops },
        { "escape pod", oracle.Label("ESCP"), Elite::Field::EscapePod },
        { "energy bomb", oracle.Label("BOMB"), Elite::Field::EnergyBomb },
        { "energy unit", oracle.Label("ENGY"), Elite::Field::EnergyUnit },
        { "docking computer", oracle.Label("DKCMP"), Elite::Field::DockingComputer },
        { "galactic drive", oracle.Label("GHYP"), Elite::Field::GalacticDrive },
      };
      for (const Check& check : CHECKS)
      {
        Assert::AreEqual(cpu.memory[check.address], commander.At(check.field),
                         (where + L": " + Widen(check.name)).c_str());
      }
      for (std::size_t mount = 0; mount < 4; ++mount)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("LASER") + mount)],
                         commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + mount],
                         (where + L": laser mount " + std::to_wstring(mount)).c_str());
      }
      for (std::size_t index = 0; index < 4; ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("CASH") + index)],
                         commander.bytes[static_cast<std::size_t>(Elite::Field::Cash) + index],
                         (where + L": cash byte " + std::to_wstring(index)).c_str());
      }

      ++compared;
    }

    Logger::WriteMessage(("EQSHP: " + std::to_string(compared)
                          + " equipment screens compared character for character, with the cursor stamped\n")
                           .c_str());
  }
};

} // namespace GameLogicTests

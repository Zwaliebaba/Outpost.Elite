#include "pch.h"

#include "OracleImage.h"

#include "Canvas.h"
#include "Commander.h"
#include "ExtendedTokens.h"
#include "Market.h"
#include "MarketScreen.h"
#include "StateTokens.h"
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
                        std::uint32_t _budget = 400'000)
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
    if (_leaveAt != 0 && _cpu.pc == _leaveAt)
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

      ScriptedKeys keys(scenario.keys);
      RecordingEffects effects;
      Elite::TradeScreen screen{ printer, characters, text, keys, effects };

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

} // namespace GameLogicTests

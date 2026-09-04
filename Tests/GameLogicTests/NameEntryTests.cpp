#include "pch.h"

#include "OracleImage.h"

#include "Canvas.h"
#include "Commander.h"
#include "ExtendedTokens.h"
#include "NameEntry.h"
#include "Rng.h"
#include "TextPrint.h"
#include "Tokens.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::CharacterPrinter;
using Elite::KeySource;
using Elite::LineLimits;
using Elite::LineResult;
using Elite::TextState;
using Elite::TokenPrinter;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The line editor and the commander's name, against the game (slice 2d).
 *
 * 2d's row deferred these to 2e because they read the keyboard. They do, through TT217 -- the
 * KeySource seam slice 2c built and proved against four screens -- so the deferral was the same
 * mistake plan section 6.12 records, for the third time. What genuinely waits for 2e is the file:
 * SVE and DFAULT end at the C64 Kernal's save and load calls.
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

    /// The port's side of a scripted keyboard. Records an overrun rather than throwing, because
    /// GameLogic is noexcept and an assertion here would unwind through it into std::terminate.
    class ScriptedKeys : public KeySource
    {
    public:
      explicit ScriptedKeys(std::vector<std::uint8_t> _keys) noexcept
        : m_keys(std::move(_keys))
      {
      }

      std::uint8_t NextKey() override
      {
        if (m_taken >= m_keys.size())
        {
          m_overrun = true;
          return 13; // RETURN, which ends the line rather than running away.
        }
        return m_keys[m_taken++];
      }

      [[nodiscard]] std::size_t Taken() const noexcept
      {
        return m_taken;
      }
      [[nodiscard]] bool Overran() const noexcept
      {
        return m_overrun;
      }

    private:
      std::vector<std::uint8_t> m_keys;
      std::size_t m_taken = 0;
      bool m_overrun = false;
    };

    /// The two C64 things the line editor reaches for, recorded rather than performed.
    class RecordingEffects : public Elite::LineEntryEffects
    {
    public:
      void WaitFrames(std::uint8_t _frames) override
      {
        log.push_back(static_cast<std::uint32_t>(0x100u + _frames));
      }
      void FlushKeyboard() override
      {
        log.push_back(0x200u);
      }

      std::vector<std::uint32_t> log;
    };

    /// Every character with the cursor it was printed at, as the docked screens are compared.
    struct RecordingSink : public Elite::TextSink
    {
      void Put(std::uint8_t _character) override
      {
        const std::uint32_t column = (cursor != nullptr) ? cursor->column : 0u;
        const std::uint32_t row = (cursor != nullptr) ? cursor->row : 0u;
        stamped.push_back(static_cast<std::uint32_t>(_character) | (column << 8) | (row << 16));
      }

      TextState* cursor = nullptr;
      std::vector<std::uint32_t> stamped;
    };

    std::string Describe(const std::vector<std::uint32_t>& _stamped)
    {
      std::string text;
      for (const std::uint32_t entry : _stamped)
      {
        const std::uint8_t character = static_cast<std::uint8_t>(entry);
        text += (character >= 32 && character < 127) ? static_cast<char>(character) : '.';
      }
      return text;
    }

    /*
     * Step the shipped routine, answering TT217 from a script.
     *
     * TT217 blocks until a key is pressed and the caller needs the key in A, so it cannot be trapped
     * and returned from; the RTS is performed by hand with the next scripted key in the accumulator.
     * Every other instruction is the real one.
     */
    struct KeyboardRun
    {
      bool completed = false;
      std::size_t keysTaken = 0;
    };

    KeyboardRun RunWithKeys(Cpu6502& _cpu, std::uint16_t _routine, std::uint16_t _keyRead, const std::vector<std::uint8_t>& _keys,
                            std::uint32_t _budget = 200'000)
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

      for (std::uint32_t step = 0; step < _budget; ++step)
      {
        if (_cpu.pc == STOP || _cpu.sp == entrySp)
        {
          run.completed = true;
          break;
        }

        if (_cpu.pc == _keyRead)
        {
          Assert::IsTrue(run.keysTaken < _keys.size(), L"the shipped routine asked for more keys than the script holds");
          _cpu.a = _keys[run.keysTaken++];

          const std::uint8_t lo = _cpu.memory[static_cast<std::uint16_t>(0x0100 + ((_cpu.sp + 1u) & 0xFFu))];
          const std::uint8_t hi = _cpu.memory[static_cast<std::uint16_t>(0x0100 + ((_cpu.sp + 2u) & 0xFFu))];
          _cpu.sp = static_cast<std::uint8_t>(_cpu.sp + 2u);
          _cpu.pc = static_cast<std::uint16_t>((lo | (hi << 8)) + 1);
          continue;
        }

        Assert::IsTrue(_cpu.Step(), L"the routine should not reach an unimplemented opcode");
      }

      return run;
    }
  } // namespace

  TEST_CLASS(TypingALineMatchesTheShippedGame)
  {
  public:
    /*
     * 6502: MT26, over the keys it branches on.
     *
     * The scripts cover an ordinary name, an empty line, a line filled exactly to the limit and one
     * key past it, every kind of rejected character (below the range, at the excluded top of it, a
     * control code), DELETE on a character and DELETE on an empty line, and ESCAPE -- which is the
     * only exit that sets the carry and the only one that writes no terminator.
     */
    TEST_METHOD(ReadingALineMatchesTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t chpr = oracle.Label("CHPR");
      const std::uint16_t tt217 = oracle.Label("TT217");
      const std::uint16_t mt26 = oracle.Label("MT26");
      const std::uint16_t rline = oracle.Label("RLINE");
      const std::uint16_t inwk = oracle.Label("INWK");
      const std::uint16_t col2 = oracle.Label("COL2");
      const std::uint16_t delay = oracle.Label("DELAY");
      const std::uint16_t flkb = oracle.Label("FLKB");

      struct Script
      {
        const char* what;
        std::vector<std::uint8_t> keys;
        std::uint8_t maxLength;
      };

      const std::vector<Script> SCRIPTS = {
        {"an ordinary name", {'J', 'A', 'M', 'E', 'S', 13}, 7},
        {"nothing but RETURN", {13}, 7},
        {"exactly the limit", {'A', 'B', 'C', 'D', 'E', 'F', 'G', 13}, 7},
        {"one key past the limit", {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 13}, 7},
        {"a space, which is below the range", {'A', ' ', 'B', 13}, 7},
        {"an open brace, the excluded top", {'A', '{', 'B', 13}, 7},
        {"a lower-case z, the last accepted", {'z', 13}, 7},
        /*
         * The two characters ON the boundary, which the scripts either side of them do not pin.
         * '!' is RLINE+3 and the test is `CMP RLINE+3 / BCC`, so '!' itself is accepted; ' ' is one
         * below and refused. A mutation moving the comparison to `>` passed until this line existed.
         */
        {"an exclamation mark, the first accepted", {'!', 'A', 13}, 7},
        {"a control code", {'A', 1, 'B', 13}, 7},
        {"delete a character", {'A', 'B', 127, 'C', 13}, 7},
        {"delete on an empty line", {127, 'A', 13}, 7},
        {"delete everything typed", {'A', 'B', 127, 127, 127, 'C', 13}, 7},
        {"escape", {'A', 'B', 27}, 7},
        {"escape immediately", {27}, 7},
        {"the full nine-character limit", {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 13}, 9},
      };

      std::uint32_t compared = 0;

      for (const Script& script : SCRIPTS)
      {
        const std::wstring where = Widen(std::string("MT26: ") + script.what);

        // ---- the shipped routine ------------------------------------------------------------
        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
        cpu.AddTrap(delay);
        cpu.AddTrap(flkb);
        cpu.watch = {oracle.Label("XC"), oracle.Label("YC"), 0, 0};

        cpu.memory[static_cast<std::uint16_t>(rline + 2)] = script.maxLength;
        cpu.memory[oracle.Label("XC")] = 1;
        cpu.memory[oracle.Label("YC")] = 1;
        for (std::size_t index = 0; index < 16; ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(inwk + 5 + index)] = 0xAA;
        }

        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;

        const KeyboardRun run = RunWithKeys(cpu, mt26, tt217, script.keys);
        Assert::IsTrue(run.completed, (where + L": MT26 should return").c_str());

        std::vector<std::uint32_t> expected;
        std::vector<std::uint32_t> gameEffects;
        for (const Cpu6502::TrapHit& hit : cpu.trapHits)
        {
          if (hit.address == chpr)
          {
            expected.push_back(static_cast<std::uint32_t>(hit.a) | (static_cast<std::uint32_t>(hit.watched[0]) << 8) |
                               (static_cast<std::uint32_t>(hit.watched[1]) << 16));
          }
          else if (hit.address == delay)
          {
            gameEffects.push_back(0x100u + hit.y);
          }
          else if (hit.address == flkb)
          {
            gameEffects.push_back(0x200u);
          }
        }

        // ---- the port ------------------------------------------------------------------------
        RecordingSink sink;
        TextState text;
        text.column = 1;
        text.row = 1;
        sink.cursor = &text;

        ScriptedKeys keys(script.keys);
        RecordingEffects effects;
        std::array<std::uint8_t, 16> buffer;
        buffer.fill(0xAA);

        LineLimits limits;
        limits.maxLength = script.maxLength;

        const LineResult result = Elite::ReadLine(keys, sink, text, effects, buffer, limits);

        // ---- compare -------------------------------------------------------------------------
        Assert::IsFalse(keys.Overran(), (where + L": the port asked for more keys than the script holds").c_str());
        Assert::AreEqual(run.keysTaken, keys.Taken(), (where + L": how many keys were read").c_str());

        Assert::AreEqual(expected.size(), sink.stamped.size(),
                         (where + L": how many characters were printed -- game \"" + Widen(Describe(expected)) + L"\", port \"" +
                          Widen(Describe(sink.stamped)) + L"\"")
                           .c_str());
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
          Assert::AreEqual(expected[index], sink.stamped[index],
                           (where + L": character " + std::to_wstring(index) + L" -- game \"" + Widen(Describe(expected)) + L"\", port \"" +
                            Widen(Describe(sink.stamped)) + L"\"")
                             .c_str());
        }

        Assert::AreEqual(gameEffects.size(), effects.log.size(), (where + L": how many seams were reached").c_str());
        for (std::size_t index = 0; index < gameEffects.size(); ++index)
        {
          Assert::AreEqual(gameEffects[index], effects.log[index], (where + L": seam " + std::to_wstring(index)).c_str());
        }

        // 6502: Y on return, and the carry -- which ESCAPE sets and nothing else does.
        Assert::AreEqual(cpu.y, result.length, (where + L": the length in Y").c_str());
        Assert::AreEqual(cpu.c, result.escaped, (where + L": the carry ESCAPE sets").c_str());

        // 6502: COL2 -- purple while typing, white on both exits.
        Assert::AreEqual(cpu.memory[col2], text.cellColour, (where + L": the text colour on exit").c_str());

        // The buffer itself, including the carriage return RETURN writes into it and the bytes
        // beyond the line that must be left alone.
        for (std::size_t index = 0; index < buffer.size(); ++index)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + 5 + index)], buffer[index],
                           (where + L": buffer byte " + std::to_wstring(index)).c_str());
        }

        ++compared;
      }

      Logger::WriteMessage(("MT26: " + std::to_string(compared) + " lines typed, key for key\n").c_str());
    }

    /*
     * 6502: TRNME (which falls into TR1), TR1 on its own, and GTNME.
     *
     * GTNME is the one with state to check afterwards: it lowers the line limit to seven for the
     * question and puts it back to nine, and when nothing is typed it copies the existing name back
     * over the buffer so the player keeps it.
     */
    TEST_METHOD(TheNameRoutinesMatchTheShippedRoutines)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t chpr = oracle.Label("CHPR");
      const std::uint16_t tt217 = oracle.Label("TT217");
      const std::uint16_t rline = oracle.Label("RLINE");
      const std::uint16_t inwk = oracle.Label("INWK");
      const std::uint16_t na = oracle.Label("NA%");

      static constexpr std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> EXISTING = {'J', 'A', 'M', 'E', 'S', 'O', 'N', 13};

      // ---- TRNME, which falls into TR1 -------------------------------------------------------
      {
        Cpu6502 cpu = oracle.Fresh();
        static constexpr std::array<std::uint8_t, 8> TYPED = {'B', 'R', 'A', 'B', 'E', 'N', 13, 0x55};
        for (std::size_t index = 0; index < TYPED.size(); ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(inwk + 5 + index)] = TYPED[index];
          cpu.memory[static_cast<std::uint16_t>(na + index)] = EXISTING[index];
        }
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        const auto run = cpu.CallSubroutine(oracle.Label("TRNME"), 10'000);
        Assert::IsTrue(run.completed, L"TRNME should return");

        std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = EXISTING;
        std::array<std::uint8_t, 8> buffer = TYPED;
        Elite::StoreCommanderName(buffer, name);

        for (std::size_t index = 0; index < name.size(); ++index)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(na + index)], name[index],
                           (L"TRNME: stored name byte " + std::to_wstring(index)).c_str());
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + 5 + index)], buffer[index],
                           (L"TRNME: buffer byte " + std::to_wstring(index) + L" after the fall-through").c_str());
        }
      }

      // ---- TR1 on its own --------------------------------------------------------------------
      {
        Cpu6502 cpu = oracle.Fresh();
        for (std::size_t index = 0; index < Elite::COMMANDER_NAME_SIZE; ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(na + index)] = EXISTING[index];
          cpu.memory[static_cast<std::uint16_t>(inwk + 5 + index)] = 0x33;
        }
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        const auto run = cpu.CallSubroutine(oracle.Label("TR1"), 10'000);
        Assert::IsTrue(run.completed, L"TR1 should return");

        std::array<std::uint8_t, 8> buffer;
        buffer.fill(0x33);
        Elite::LoadCommanderName(EXISTING, buffer);

        for (std::size_t index = 0; index < buffer.size(); ++index)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + 5 + index)], buffer[index],
                           (L"TR1: buffer byte " + std::to_wstring(index)).c_str());
        }
      }

      // ---- GTNME, with and without a name typed ----------------------------------------------
      struct Case
      {
        const char* what;
        std::vector<std::uint8_t> keys;
      };
      const std::vector<Case> CASES = {
        {"a name typed", {'B', 'E', 'L', 'L', 13}},
        {"nothing typed, so the old name is kept", {13}},
        {"escape", {'X', 27}},
        {"a name at the seven-character limit", {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 13}},
      };

      std::uint32_t compared = 0;

      for (const Case& item : CASES)
      {
        const std::wstring where = Widen(std::string("GTNME: ") + item.what);

        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
        cpu.AddTrap(oracle.Label("DELAY"));
        cpu.AddTrap(oracle.Label("FLKB"));
        cpu.watch = {oracle.Label("XC"), oracle.Label("YC"), 0, 0};

        cpu.memory[static_cast<std::uint16_t>(rline + 2)] = 9;
        cpu.memory[oracle.Label("XC")] = 1;
        cpu.memory[oracle.Label("YC")] = 1;
        cpu.memory[oracle.Label("DTW1")] = 0;
        cpu.memory[oracle.Label("DTW2")] = 0xFF;
        cpu.memory[oracle.Label("DTW3")] = 0;
        cpu.memory[oracle.Label("DTW4")] = 0;
        cpu.memory[oracle.Label("DTW5")] = 0;
        cpu.memory[oracle.Label("DTW6")] = 0;
        cpu.memory[oracle.Label("DTW8")] = 0xFF;
        cpu.memory[oracle.Label("QQ17")] = 0;
        for (std::size_t index = 0; index < Elite::COMMANDER_NAME_SIZE; ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(na + index)] = EXISTING[index];
          cpu.memory[static_cast<std::uint16_t>(inwk + 5 + index)] = 0x77;
        }

        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;

        const KeyboardRun run = RunWithKeys(cpu, oracle.Label("GTNME"), tt217, item.keys);
        Assert::IsTrue(run.completed, (where + L": GTNME should return").c_str());

        std::vector<std::uint32_t> expected;
        for (const Cpu6502::TrapHit& hit : cpu.trapHits)
        {
          if (hit.address == chpr)
          {
            expected.push_back(static_cast<std::uint32_t>(hit.a) | (static_cast<std::uint32_t>(hit.watched[0]) << 8) |
                               (static_cast<std::uint32_t>(hit.watched[1]) << 16));
          }
        }

        RecordingSink sink;
        TextState text;
        text.column = 1;
        text.row = 1;
        sink.cursor = &text;
        CharacterPrinter characters(sink);
        characters.state.sentenceStart = 0xFF;
        TokenPrinter printer(characters);
        printer.SetCaseFlags(0);
        printer.SetCursor(&text);
        Elite::Rng rng;
        Elite::ExtendedTokenPrinter extended(characters, printer, rng);

        ScriptedKeys keys(item.keys);
        RecordingEffects effects;
        std::array<std::uint8_t, 16> buffer;
        buffer.fill(0x77);
        LineLimits limits;

        const LineResult result = Elite::AskCommanderName(keys, sink, text, extended, effects, buffer, EXISTING, limits);

        Assert::IsFalse(keys.Overran(), (where + L": the port asked for more keys than the script holds").c_str());
        Assert::AreEqual(run.keysTaken, keys.Taken(), (where + L": how many keys were read").c_str());

        Assert::AreEqual(
          expected.size(), sink.stamped.size(),
          (where + L": how many characters -- game \"" + Widen(Describe(expected)) + L"\", port \"" + Widen(Describe(sink.stamped)) + L"\"")
            .c_str());
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
          Assert::AreEqual(expected[index], sink.stamped[index],
                           (where + L": character " + std::to_wstring(index) + L" -- game \"" + Widen(Describe(expected)) + L"\", port \"" +
                            Widen(Describe(sink.stamped)) + L"\"")
                             .c_str());
        }

        // 6502: LDA #9 / STA RLINE+2 -- restored whether a name was typed or not.
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(rline + 2)], limits.maxLength,
                         (where + L": the line limit is put back").c_str());
        Assert::AreEqual(cpu.y, result.length, (where + L": the length in Y").c_str());

        for (std::size_t index = 0; index < Elite::COMMANDER_NAME_SIZE; ++index)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + 5 + index)], buffer[index],
                           (where + L": buffer byte " + std::to_wstring(index)).c_str());
        }

        ++compared;
      }

      Logger::WriteMessage(("GTNME: " + std::to_string(compared) + " name prompts compared, plus TRNME and TR1\n").c_str());
    }
  };

} // namespace GameLogicTests

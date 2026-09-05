#include "pch.h"

#include "OracleImage.h"

#include "Commander.h"
#include "ExtendedTokens.h"
#include "Rng.h"
#include "StartUp.h"
#include "StateTokens.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::CurrentSystem;
using Elite::SystemSeeds;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Starting a game, and going back to the docking bay (slice 2e).
 *
 * What is portable about TT170, BR1 and BAY is the SEQUENCE and the state, because everything
 * they reach -- the rotating ship, the resets, the theme, the dashboard -- is phase 3's or the
 * executable's. So this compares the order the seams are reached in, the arguments they are
 * reached with, and every byte of game state the sequence leaves behind. That is not a weak
 * comparison: the order carries the double reset, the music bracketing one branch and not the
 * other, and DFAULT running twice, none of which a reading of the source hands you.
 *
 * The disk menu runs FOR REAL on both sides, keyboard and all, because it is slice 2d's and it is
 * built -- so the "Y" branch is compared through a routine rather than over one.
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

    /// One seam reached, with whatever it was reached with. Compared as a sequence.
    struct Seam
    {
      std::string what;
      std::uint8_t a = 0;
      std::uint8_t x = 0;
      std::uint8_t y = 0;

      [[nodiscard]] bool operator==(const Seam&) const = default;
    };

    std::wstring Describe(const std::vector<Seam>& _seams)
    {
      std::wstring text = L"[";
      for (std::size_t index = 0; index < _seams.size(); ++index)
      {
        if (index != 0)
        {
          text += L", ";
        }
        text += Widen(_seams[index].what);
        if (_seams[index].what == "TITLE")
        {
          text += L"(" + std::to_wstring(_seams[index].a) + L"," + std::to_wstring(_seams[index].x) + L"," +
                  std::to_wstring(_seams[index].y) + L")";
        }
      }
      return text + L"]";
    }

    /// The port's side: every seam recorded, and the title screen answering from a script.
    class RecordingStart : public Elite::StartUpEffects
    {
    public:
      explicit RecordingStart(std::vector<std::uint8_t> _answers) noexcept
        : m_answers(std::move(_answers))
      {
      }

      void ResetUniverse() override
      {
        seams.push_back({"RESET", 0, 0, 0});
      }
      void ResetShip() override
      {
        seams.push_back({"RES2", 0, 0, 0});
      }
      void ClearKeyLogger() override
      {
        seams.push_back({"ZEKTRAN", 0, 0, 0});
      }
      void StartTheme() override
      {
        seams.push_back({"startat", 0, 0, 0});
      }
      void StopTheme() override
      {
        seams.push_back({"stopat", 0, 0, 0});
      }
      void ResetMissileIndicators() override
      {
        seams.push_back({"msblob", 0, 0, 0});
      }

      // Reached by DOENTRY rather than by the start sequence, so neither script here should see one.
      /// 6502: JSR RDKEY inside `TLL2`. Nothing here rotates a ship, so the first scan dismisses it.
      [[nodiscard]] Elite::TitleKey ScanTitleKeys(Elite::KeyLogger& _keys) override
      {
        (void)_keys;
        return {true, 0u};
      }

      void WaitFrames(std::uint8_t _frames) override
      {
        seams.push_back({"DELAY", _frames, 0, 0});
      }

      std::uint8_t ShowTitleScreen(std::uint8_t _token, std::uint8_t _shipType, std::uint8_t _distance) override
      {
        seams.push_back({"TITLE", _token, _shipType, _distance});
        if (m_taken >= m_answers.size())
        {
          overran = true;
          return 0;
        }
        return m_answers[m_taken++];
      }

      std::vector<Seam> seams;
      bool overran = false;

    private:
      std::vector<std::uint8_t> m_answers;
      std::size_t m_taken = 0;
    };

    class ScriptedKeys : public Elite::KeySource
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
          overran = true;
          return ((m_extra++ % 2u) == 0u) ? static_cast<std::uint8_t>(13) : static_cast<std::uint8_t>('N');
        }
        return m_keys[m_taken++];
      }
      [[nodiscard]] std::size_t Taken() const noexcept
      {
        return m_taken;
      }
      bool overran = false;

    private:
      std::vector<std::uint8_t> m_keys;
      std::size_t m_taken = 0;
      std::size_t m_extra = 0;
    };

    class SilentEffects : public Elite::LineEntryEffects
    {
    public:
      void WaitFrames(std::uint8_t) override {}
      void FlushKeyboard() override {}
    };

    class IgnoredControls : public Elite::ControlCodes
    {
    public:
      void Run(std::uint8_t) override {}
    };

    struct CountingSink : public Elite::TextSink
    {
      void Put(std::uint8_t) override
      {
        ++characters;
      }
      std::uint32_t characters = 0;
    };

    /// One run of the title sequence: how it is entered, what the first screen answers, what is typed
    /// at the disk menu, and the seams the shipped routine should reach in that order.
    struct Script
    {
      const char* what;
      bool coldStart;           ///< enter at TT170 rather than at BR1
      std::uint8_t firstAnswer; ///< what the first title screen returns
      std::vector<std::uint8_t> menuKeys;
      std::vector<Seam> expected;
    };

    /// The fixture's device, standing in for the two Kernal calls exactly as SaveGameTests does.
    class DeviceStore : public Elite::CommanderStore
    {
    public:
      bool Write(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>,
                 std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE>) override
      {
        ++writes;
        return true;
      }
      bool Read(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>, std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE>) override
      {
        ++reads;
        return false; // 6502: the Kernal setting the carry, which reaches tapeerror
      }
      int writes = 0;
      int reads = 0;
    };
  } // namespace

  TEST_CLASS(StartingAGameMatchesTheShippedGame)
  {
  public:
    /*
     * 6502: ping and jmp, over every coordinate pair.
     *
     * Two two-byte copies, which is exactly the kind of routine a port gets subtly wrong -- ping
     * counts DOWN from 1, so it moves the y first, and a port that wrote them in the other order
     * would agree on every input and disagree on nothing, which is why this compares the memory
     * rather than the arithmetic.
     */
    TEST_METHOD(TheCrosshairCopiesMatchTheShippedRoutines)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t qq0 = oracle.Label("QQ0");
      const std::uint16_t qq9 = oracle.Label("QQ9");
      std::uint32_t compared = 0;

      for (std::uint16_t x = 0; x < 256; x += 17)
      {
        for (std::uint16_t y = 0; y < 256; y += 13)
        {
          // 6502: ping.
          {
            Cpu6502 cpu = oracle.Fresh();
            cpu.memory[qq0] = static_cast<std::uint8_t>(x);
            cpu.memory[static_cast<std::uint16_t>(qq0 + 1)] = static_cast<std::uint8_t>(y);
            cpu.memory[qq9] = 0xAA;
            cpu.memory[static_cast<std::uint16_t>(qq9 + 1)] = 0xBB;
            cpu.a = cpu.x = cpu.y = 0;
            cpu.sp = 0xFD;
            Assert::IsTrue(cpu.CallSubroutine(oracle.Label("ping"), 10'000).completed, L"ping should return");

            Elite::CommanderBlock commander;
            commander.At(Elite::Field::SystemX) = static_cast<std::uint8_t>(x);
            commander.At(Elite::Field::SystemY) = static_cast<std::uint8_t>(y);
            std::uint8_t crosshairX = 0xAA;
            std::uint8_t crosshairY = 0xBB;
            Elite::CrosshairsToCurrentSystem(commander, crosshairX, crosshairY);

            Assert::AreEqual(cpu.memory[qq9], crosshairX, L"ping: the crosshair x");
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(qq9 + 1)], crosshairY, L"ping: the y");
          }

          // 6502: jmp.
          {
            Cpu6502 cpu = oracle.Fresh();
            cpu.memory[qq9] = static_cast<std::uint8_t>(x);
            cpu.memory[static_cast<std::uint16_t>(qq9 + 1)] = static_cast<std::uint8_t>(y);
            cpu.memory[qq0] = 0xAA;
            cpu.memory[static_cast<std::uint16_t>(qq0 + 1)] = 0xBB;
            cpu.a = cpu.x = cpu.y = 0;
            cpu.sp = 0xFD;
            Assert::IsTrue(cpu.CallSubroutine(oracle.Label("jmp"), 10'000).completed, L"jmp should return");

            Elite::CommanderBlock commander;
            commander.At(Elite::Field::SystemX) = 0xAA;
            commander.At(Elite::Field::SystemY) = 0xBB;
            Elite::CurrentSystemToCrosshairs(commander, static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y));

            Assert::AreEqual(cpu.memory[qq0], commander.At(Elite::Field::SystemX), L"jmp: the current x");
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(qq0 + 1)], commander.At(Elite::Field::SystemY), L"jmp: the y");
          }

          ++compared;
        }
      }

      Logger::WriteMessage(("ping/jmp: " + std::to_string(compared) + " coordinate pairs each way\n").c_str());
    }

    /*
     * 6502: BAY and FRCE.
     *
     * Four instructions and five, and the interesting one is FRCE's `BEQ P%+5`, which skips the
     * `JMP MLOOP` -- so it is a ZERO docked flag that reaches TT100, not a set one. Read the branch
     * the other way and the game enters the wrong half of its own main loop.
     */
    TEST_METHOD(TheDockingBayAndForcedKeysMatchTheShippedRoutines)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t qq12 = oracle.Label("QQ12");
      const std::uint16_t mloop = oracle.Label("MLOOP");
      const std::uint16_t tt100 = oracle.Label("TT100");
      const std::uint16_t tt102 = oracle.Label("TT102");

      struct Case
      {
        const char* what;
        std::uint16_t entry;
        std::uint8_t docked;
        std::uint8_t key;
      };

      const std::vector<Case> CASES = {
        {"BAY from space", oracle.Label("BAY"), 0x00, 0},
        {"BAY while already docked", oracle.Label("BAY"), 0xFF, 0},
        {"FRCE docked, status key", oracle.Label("FRCE"), 0xFF, Elite::KEY_STATUS},
        {"FRCE in space, status key", oracle.Label("FRCE"), 0x00, Elite::KEY_STATUS},
        {"FRCE with a one in QQ12", oracle.Label("FRCE"), 0x01, Elite::KEY_STATUS},
        {"FRCE in space, a view key", oracle.Label("FRCE"), 0x00, Elite::KEY_LEFT_VIEW},
        {"FRCE docked, a key that does nothing", oracle.Label("FRCE"), 0xFF, 0x7F},
      };

      for (const Case& item : CASES)
      {
        const std::wstring where = Widen(std::string("FRCE: ") + item.what);

        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(tt102);
        cpu.memory[qq12] = item.docked;
        cpu.a = item.key;
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.pc = item.entry;

        // The two main-loop entries are jumped to, not called, so the run stops when it arrives.
        bool docked = false;
        bool inSpace = false;
        std::uint8_t dispatched = 0;
        for (int step = 0; step < 10'000; ++step)
        {
          if (cpu.pc == mloop)
          {
            docked = true;
            break;
          }
          if (cpu.pc == tt100)
          {
            inSpace = true;
            break;
          }
          if (cpu.pc == tt102)
          {
            dispatched = cpu.a;
          }
          Assert::IsTrue(cpu.Step(), (where + L": no unimplemented opcode").c_str());
        }
        Assert::IsTrue(docked || inSpace, (where + L": one of the two loops should be reached").c_str());

        // ---- the port ------------------------------------------------------------------------
        std::uint8_t portDocked = item.docked;
        const Elite::ForcedKey result = (item.entry == oracle.Label("BAY")) ? Elite::EnterDockingBay(portDocked, 0, 0, false)
                                                                            : Elite::ForceKey(item.key, portDocked, 0, 0, false);

        Assert::AreEqual(cpu.memory[qq12], portDocked, (where + L": the docked flag").c_str());
        Assert::AreEqual(static_cast<int>(docked ? Elite::MainLoop::Docked : Elite::MainLoop::InSpace), static_cast<int>(result.loop),
                         (where + L": which loop was entered").c_str());

        // The key TT102 was actually handed, which BAY chooses for itself.
        Assert::AreEqual(dispatched, (item.entry == oracle.Label("BAY")) ? Elite::KEY_STATUS : item.key,
                         (where + L": the key the dispatch was given").c_str());
      }

      Logger::WriteMessage(("BAY/FRCE: " + std::to_string(CASES.size()) + " entries compared\n").c_str());
    }
    /*
     * 6502: BR1 and TT170, seam for seam and byte for byte.
     *
     * The shipped routine is run whole. Only what it reaches OUTSIDE this slice is stood in for --
     * the two resets, the theme, the dashboard, the key logger and the rotating ship -- and the
     * disk menu on the "Y" branch runs for real, keyboard and all, because slice 2d built it.
     */
    TEST_METHOD(TheTitleSequenceMatchesTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();

      /*
       * The fall-throughs, asserted structurally rather than trusted.
       *
       * TT170 has no RTS: `LDX #&FF / TXS / JSR RESET` is six bytes and DEATH2 begins at the
       * seventh, and DEATH2's own six bytes end at BR1. So a cold start runs both resets and then
       * the title sequence, and none of that is written down at any call site.
       */
      Assert::AreEqual<std::uint16_t>(static_cast<std::uint16_t>(oracle.Label("TT170") + 6), oracle.Label("DEATH2"),
                                      L"TT170 should fall into DEATH2");
      Assert::AreEqual<std::uint16_t>(static_cast<std::uint16_t>(oracle.Label("DEATH2") + 6), oracle.Label("BR1"),
                                      L"DEATH2 should fall into BR1");
      Assert::AreNotEqual<std::uint8_t>(0x60, oracle.Fresh().memory[static_cast<std::uint16_t>(oracle.Label("RES2") - 1)],
                                        L"RESET should fall into RES2 rather than returning");

      const Seam ZEK{"ZEKTRAN", 0, 0, 0};
      const Seam START{"startat", 0, 0, 0};
      const Seam STOP{"stopat", 0, 0, 0};
      const Seam BLOB{"msblob", 0, 0, 0};
      const Seam RESET{"RESET", 0, 0, 0};
      const Seam RES2{"RES2", 0, 0, 0};
      const Seam FIRST{"TITLE", Elite::TITLE_LOAD_TOKEN, Elite::SHIP_COBRA_MK3, Elite::TITLE_COBRA_DISTANCE};
      const Seam SECOND{"TITLE", Elite::TITLE_START_TOKEN, Elite::SHIP_ADDER, Elite::TITLE_ADDER_DISTANCE};

      const std::vector<Script> SCRIPTS = {
        {"N at the prompt", false, 'N', {}, {ZEK, START, FIRST, BLOB, SECOND, STOP}},
        {"a key that is not Y", false, ' ', {}, {ZEK, START, FIRST, BLOB, SECOND, STOP}},
        {"no key at all", false, 0, {}, {ZEK, START, FIRST, BLOB, SECOND, STOP}},
        {"Y, then leave the menu", false, Elite::KEY_YES_INTERNAL, {'5'}, {ZEK, START, FIRST, STOP, START, BLOB, SECOND, STOP}},
        {"Y, toggle the media, then leave",
         false,
         Elite::KEY_YES_INTERNAL,
         {'3', '5'},
         {ZEK, START, FIRST, STOP, START, BLOB, SECOND, STOP}},
        {"a cold start", true, 'N', {}, {RESET, RES2, ZEK, START, FIRST, BLOB, SECOND, STOP}},
        {"a cold start into the menu",
         true,
         Elite::KEY_YES_INTERNAL,
         {'5'},
         {RESET, RES2, ZEK, START, FIRST, STOP, START, BLOB, SECOND, STOP}},
      };

      std::uint32_t compared = 0;

      for (const Script& script : SCRIPTS)
      {
        const std::wstring where = Widen(std::string("BR1: ") + script.what);

        // The commander the sequence starts from: a valid save image whose coordinates fall
        // BETWEEN systems, so the snap through ping/TT111/jmp actually moves them.
        Elite::CommanderBlock saved = Elite::DefaultCommander();
        saved.At(Elite::Field::SystemX) = 0x63;
        saved.At(Elite::Field::SystemY) = 0x4D;
        static constexpr std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> NAME = {'B', 'E', 'L', 'L', 13, 0, 0, 0};
        std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> image{};
        Elite::SaveCommander(saved, NAME, image);

        // ---- the shipped routine -----------------------------------------------------------
        Cpu6502 cpu = oracle.Fresh();
        const std::uint16_t title = oracle.Label("TITLE");
        const std::uint16_t keyRead = oracle.Label("t");
        const std::uint16_t ysav = oracle.Label("YSAV");
        const std::uint16_t chpr = oracle.Label("CHPR");
        const std::uint16_t doxc = oracle.Label("DOXC");
        constexpr std::uint16_t KERNAL_SAVE = 0xFFD8;
        constexpr std::uint16_t KERNAL_LOAD = 0xFFD5;

        std::vector<std::pair<std::uint16_t, std::string>> named = {
          {oracle.Label("ZEKTRAN"), "ZEKTRAN"}, {oracle.Label("startat"), "startat"}, {oracle.Label("stopat"), "stopat"},
          {oracle.Label("msblob"), "msblob"},   {oracle.Label("RESET"), "RESET"},     {oracle.Label("RES2"), "RES2"},
        };
        for (const auto& entry : named)
        {
          cpu.AddTrap(entry.first);
        }
        cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
        for (const char* seam :
             {"DOXC", "DOYC", "MT9", "NLIN4", "FILEPR", "OTHERFILEPR", "KERNALSETUP", "SETL1", "SWAPPZERO", "DELAY", "FLKB"})
        {
          cpu.AddTrap(oracle.Label(seam));
        }

        for (std::size_t index = 0; index < image.size(); ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(oracle.Label("NA%") + index)] = image[index];
        }
        cpu.memory[oracle.Label("DISK")] = 0;
        cpu.memory[oracle.Label("XC")] = 1;
        cpu.memory[oracle.Label("YC")] = 1;
        cpu.memory[oracle.Label("QQ17")] = 0;
        for (const char* byte : {"DTW1", "DTW2", "DTW3", "DTW4", "DTW5", "DTW6"})
        {
          cpu.memory[oracle.Label(byte)] = 0;
        }
        cpu.memory[oracle.Label("DTW8")] = 0xFF;

        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        constexpr std::uint16_t STOP_AT = 0xFFF9;
        const std::uint16_t ret = static_cast<std::uint16_t>(STOP_AT - 1);
        cpu.memory[static_cast<std::uint16_t>(0x0100 + cpu.sp)] = static_cast<std::uint8_t>(ret >> 8);
        --cpu.sp;
        cpu.memory[static_cast<std::uint16_t>(0x0100 + cpu.sp)] = static_cast<std::uint8_t>(ret & 0xFFu);
        --cpu.sp;
        cpu.pc = script.coldStart ? oracle.Label("TT170") : oracle.Label("BR1");

        /*
         * BR1 does not return -- it runs off its end into BAY, and TT170 resets the stack pointer
         * on the way in, so neither an RTS nor the stack depth says when the routine is done.
         * Arriving at BAY does, and that the run gets there at all is the fall-through proved.
         */
        const std::uint16_t bay = oracle.Label("BAY");

        const auto ReturnFromCall = [&]()
        {
          const std::uint8_t lo = cpu.memory[static_cast<std::uint16_t>(0x0100 + ((cpu.sp + 1u) & 0xFFu))];
          const std::uint8_t hi = cpu.memory[static_cast<std::uint16_t>(0x0100 + ((cpu.sp + 2u) & 0xFFu))];
          cpu.sp = static_cast<std::uint8_t>(cpu.sp + 2u);
          cpu.pc = static_cast<std::uint16_t>((lo | (hi << 8)) + 1);
        };

        std::vector<Seam> seams;
        std::size_t titles = 0;
        std::size_t keysTaken = 0;
        std::uint8_t firstColumn = 0;
        bool sawColumn = false;
        bool completed = false;

        for (std::uint32_t step = 0; step < 3'000'000; ++step)
        {
          if (cpu.pc == bay)
          {
            completed = true;
            break;
          }

          if (cpu.pc == title)
          {
            // 6502: JSR TITLE -- the arguments in A, X and Y, the key it ended on in A and X.
            seams.push_back({"TITLE", cpu.a, cpu.x, cpu.y});
            const std::uint8_t answer = (titles == 0) ? script.firstAnswer : 0;
            ++titles;
            cpu.a = answer;
            cpu.x = answer;
            ReturnFromCall();
            continue;
          }

          if (cpu.pc == keyRead)
          {
            Assert::IsTrue(keysTaken < script.menuKeys.size(),
                           (where + L": the shipped routine asked for more keys than the script holds").c_str());
            cpu.a = script.menuKeys[keysTaken++];
            cpu.x = cpu.a;
            cpu.y = cpu.memory[ysav];
            ReturnFromCall();
            continue;
          }

          if (cpu.pc == KERNAL_SAVE || cpu.pc == KERNAL_LOAD)
          {
            cpu.c = true; // the device refuses, which no script here reaches
            ReturnFromCall();
            continue;
          }

          if (cpu.pc == doxc && !sawColumn)
          {
            firstColumn = cpu.a;
            sawColumn = true;
          }

          for (const auto& entry : named)
          {
            if (cpu.pc == entry.first)
            {
              seams.push_back({entry.second, 0, 0, 0});
            }
          }

          Assert::IsTrue(cpu.Step(), (where + L": BR1 should not reach an unimplemented opcode").c_str());
        }
        Assert::IsTrue(completed, (where + L": BR1 should run off its end into BAY").c_str());

        // ---- the port ------------------------------------------------------------------------
        CountingSink sink;
        Elite::TextState text;
        text.column = 1;
        text.row = 1;
        Elite::CharacterPrinter characters(sink);
        Elite::TokenPrinter recursive(characters);
        recursive.SetCursor(&text);
        Elite::Rng rng;
        IgnoredControls controls;
        Elite::ExtendedTokenPrinter extended(characters, recursive, rng, &controls);
        Elite::NumberWorkspace numbers;

        ScriptedKeys keys(script.menuKeys);
        SilentEffects lineEffects;
        DeviceStore store;
        Elite::SaveScreen save{recursive, characters, extended, sink, text, keys, lineEffects, store, numbers};

        Elite::CommanderBlock commander;
        std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name{};
        std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> portImage = image;
        std::array<std::uint8_t, 16> buffer{};
        std::uint8_t useDisk = 0;

        CurrentSystem current;
        SystemSeeds selected{};
        std::uint8_t crosshairX = 0;
        std::uint8_t crosshairY = 0;
        std::uint8_t explosionCount = 0xEE;
        std::uint8_t dockedFlag = 0;

        RecordingStart effects({script.firstAnswer, 0});
        Elite::GameStart game{effects, save,    text,     commander,  name,       portImage,      buffer,
                              useDisk, current, selected, crosshairX, crosshairY, explosionCount, dockedFlag};

        const Elite::ForcedKey forced = script.coldStart ? Elite::ResetAndStartGame(game) : Elite::StartGame(game);

        // ---- compare -------------------------------------------------------------------------
        Assert::IsFalse(effects.overran, (where + L": the port asked for more title screens").c_str());
        Assert::IsFalse(keys.overran, (where + L": the port asked for more keys than the script holds").c_str());
        Assert::AreEqual(keysTaken, keys.Taken(), (where + L": how many keys the menu read").c_str());

        Assert::AreEqual(
          script.expected.size(), effects.seams.size(),
          (where + L": the port's seams are " + Describe(effects.seams) + L", expected " + Describe(script.expected)).c_str());
        Assert::AreEqual(seams.size(), effects.seams.size(),
                         (where + L": the game reached " + Describe(seams) + L", the port " + Describe(effects.seams)).c_str());
        for (std::size_t index = 0; index < seams.size(); ++index)
        {
          Assert::IsTrue(
            seams[index] == effects.seams[index] && seams[index] == script.expected[index],
            (where + L": seam " + std::to_wstring(index) + L" -- game " + Describe(seams) + L", port " + Describe(effects.seams)).c_str());
        }

        // 6502: LDA #3 / JSR DOXC, which is the port's text.column and not a seam.
        Assert::IsTrue(sawColumn, (where + L": the game should set the prompt column").c_str());
        Assert::AreEqual<std::uint8_t>(Elite::TITLE_PROMPT_COLUMN, firstColumn, (where + L": the column the game set").c_str());
        Assert::AreEqual<std::uint8_t>(Elite::TITLE_PROMPT_COLUMN, text.column, (where + L": the column the port set").c_str());

        // The state the sequence leaves behind, which is what the game then plays.
        /*
         * 6502: QQ0 and QQ1 are TP+1 and TP+2 -- two bytes of the COMMANDER, which is why the
         * comparison of the whole block below covers them and why the port has no separate copy.
         */
        Assert::AreEqual(cpu.memory[oracle.Label("QQ0")], commander.At(Elite::Field::SystemX),
                         (where + L": QQ0, which is the commander's").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("QQ1")], commander.At(Elite::Field::SystemY),
                         (where + L": QQ1, which is the commander's").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("QQ9")], crosshairX, (where + L": QQ9").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("QQ10")], crosshairY, (where + L": QQ10").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("EV")], explosionCount, (where + L": EV").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("QQ28")], current.economy, (where + L": QQ28").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("tek")], current.techLevel, (where + L": tek").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("gov")], current.government, (where + L": gov").c_str());
        for (std::size_t index = 0; index < 6; ++index)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ2") + index)], current.seeds.bytes[index],
                           (where + L": QQ2 byte " + std::to_wstring(index)).c_str());
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ15") + index)], selected.bytes[index],
                           (where + L": QQ15 byte " + std::to_wstring(index)).c_str());
        }

        // DFAULT ran, so the live commander is what the image held.
        for (std::size_t index = 0; index + 1 < Elite::COMMANDER_BLOCK_SIZE; ++index)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("TP") + index)], commander.bytes[index],
                           (where + L": the live commander's byte " + std::to_wstring(index)).c_str());
        }

        /*
         * 6502: BAY's own two stores, which the fall-through reaches. The docked flag is &FF and
         * the key it forces is "8", so a game that has just started is, to the dispatch, a player
         * pressing the status key on the pad.
         */
        Assert::AreEqual<std::uint8_t>(0xFF, dockedFlag, (where + L": the docked flag BAY sets").c_str());
        Assert::AreEqual(static_cast<int>(Elite::KeyAction::StatusMode), static_cast<int>(forced.outcome.action),
                         (where + L": what BAY's key reaches").c_str());
        Assert::AreEqual(static_cast<int>(Elite::MainLoop::Docked), static_cast<int>(forced.loop),
                         (where + L": which loop the game enters").c_str());

        ++compared;
      }

      Logger::WriteMessage(("BR1: " + std::to_string(compared) + " title sequences compared seam for seam\n").c_str());
    }
  };

} // namespace GameLogicTests

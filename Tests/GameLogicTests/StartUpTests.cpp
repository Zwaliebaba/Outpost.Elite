#include "pch.h"

#include "NullSeams.h"

#include "OracleImage.h"

#include "Commander.h"
#include "Controls.h"
#include "LookupTables.h"
#include "ExtendedTokens.h"
#include "Rng.h"
#include "StartUp.h"
#include "StateTokens.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Galaxy.h"

#include <algorithm>
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
    /// 6502: SID -- the chip's registers, which the interpreter treats as memory.
    constexpr std::uint16_t SID_BASE = 0xD400;

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

    /*
     * `RecordingMusic` WAS HERE AND IS NOT ANY MORE (M3-b-2b).
     *
     * It recorded `RES2`'s `JSR stopbd` into the same list as the theme's `startat` and `stopat`,
     * and the oracle side trapped all three so that neither machine ran them. Both run them now --
     * `Music.cpp`'s routines over `Universe::music` -- so what the two sides are compared on is the
     * PLAYER'S STATE and the SID WRITES it makes, which is a byte comparison rather than a tally
     * (§6.73's corollary: a seam is what a suite counts, and the count goes with it).
     */

    /*
     * The port's side: the one seam the sequence could still reach, recorded.
     *
     * `TITLE` WAS ANSWERED HERE FROM A SCRIPT until M6-0-h-2 and is not any more: `BR1` calls
     * `Elite::ShowTitleShip` itself, both machines run the title screen for real, and what ends it
     * is a key held on the oracle's CIA and on `ScriptedKeys` below. The title frames pace through
     * `HoldTitleFrame`, which is counted.
     */
    class RecordingStart : public Elite::Presenter
    {
    public:
      void Present() override {}
      void HoldFlightFrame(std::uint8_t) override {}
      void HoldTitleFrame(std::uint8_t) override
      {
        ++titleFrames;
      }

      void WaitFrames(std::uint8_t _frames) override
      {
        seams.push_back({"DELAY", _frames, 0, 0});
      }

      std::vector<Seam> seams;
      std::uint32_t titleFrames = 0;
    };

    class ScriptedKeys : public Elite::Keyboard
    {
    public:
      explicit ScriptedKeys(std::vector<std::uint8_t> _keys) noexcept
        : m_keys(std::move(_keys))
      {
      }
      /// 6502: FLKB. `SilentEffects` answered it until M3-b-3d and the disk menu only types.
      void Flush() override {}

      /*
       * 6502: the matrix walk, which `TITLE` runs once a frame until a key is down (M6-0-h-2).
       *
       * One key per title screen, held from that screen's first scan: `titleKeys[0]` ends the
       * "LOAD NEW COMMANDER" screen and `titleKeys[1]` the "PRESS FIRE OR SPACE" one, and the
       * last entry stays held for any scan after that. A walk starts at the top of the logger, so
       * the scan count advances when the top index is asked for.
       */
      std::vector<std::uint8_t> titleKeys;
      [[nodiscard]] bool Held(std::size_t _key) override
      {
        if (_key + 1u == std::tuple_size_v<Elite::KeyLogger>)
        {
          ++m_scans;
        }
        if (titleKeys.empty() || m_scans == 0u)
        {
          return false;
        }
        const std::size_t scan = std::min(m_scans - 1u, titleKeys.size() - 1u);
        return _key == titleKeys[scan];
      }

      [[nodiscard]] std::uint8_t NextKey() override
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
      std::size_t m_scans = 0;
    };

    /*
     * 6502: the internal key numbers the scripts hold at the title screens -- matrix positions,
     * which is what `RDKEY` returns and `BR1` compares against `YINT`. `Y` is `KEY_YES_INTERNAL`
     * (column 3, row 1, so &40 - 25 = 39); `N` is column 4, row 7; Space is `KEY_SPEED_UP` and
     * "A" is `KEY_FIRE`. `TRANTABLE` pins all four below.
     */
    constexpr std::uint8_t KEY_N_INTERNAL = 25;

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
      bool coldStart;        ///< enter at TT170 rather than at BR1
      std::uint8_t firstKey; ///< the key held at the first title screen, which is what it returns
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

            Elite::Universe pinged;
            pinged.commander.systemX = static_cast<std::uint8_t>(x);
            pinged.commander.systemY = static_cast<std::uint8_t>(y);
            pinged.crosshairX = 0xAA;
            pinged.crosshairY = 0xBB;
            Elite::CrosshairsToCurrentSystem(pinged);

            Assert::AreEqual(cpu.memory[qq9], pinged.crosshairX, L"ping: the crosshair x");
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(qq9 + 1)], pinged.crosshairY, L"ping: the y");
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

            Elite::Commander commander;
            commander.systemX = 0xAA;
            commander.systemY = 0xBB;
            Elite::CurrentSystemToCrosshairs(commander, static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y));

            Assert::AreEqual(cpu.memory[qq0], commander.systemX, L"jmp: the current x");
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(qq0 + 1)], commander.systemY, L"jmp: the y");
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
        // 6502: QQ12 -- `BAY` writes the universe's byte since M5-a-2, so the fixture keeps the
        // scenario there rather than in a local `EnterDockingBay` was handed a reference to.
        Elite::Universe entering;
        entering.dockedFlag = item.docked;
        std::uint8_t portDocked = item.docked;
        const Elite::ForcedKey result = (item.entry == oracle.Label("BAY")) ? Elite::EnterDockingBay(entering, 0, 0, false)
                                                                            : Elite::ForceKey(item.key, portDocked, 0, 0, false);
        if (item.entry == oracle.Label("BAY"))
        {
          portDocked = entering.dockedFlag;
        }

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

      const Seam FIRST{"TITLE", Elite::TITLE_LOAD_TOKEN, Elite::Byte(Elite::ShipType::CobraMk3), Elite::TITLE_COBRA_DISTANCE};
      const Seam SECOND{"TITLE", Elite::TITLE_START_TOKEN, Elite::Byte(Elite::ShipType::Adder), Elite::TITLE_ADDER_DISTANCE};

      // The keys are matrix positions since M6-0-h-2, because the title screen really scans;
      // "no key at all" was a script only a stub could answer and is the fire button now, which
      // is the one key that ends `TITLE` without turning the joystick off.
      Assert::AreEqual<std::uint8_t>('N', Elite::KEY_TRANSLATION[KEY_N_INTERNAL], L"TRANTABLE: N");
      Assert::AreEqual<std::uint8_t>('Y', Elite::KEY_TRANSLATION[Elite::KEY_YES_INTERNAL], L"TRANTABLE: Y");
      Assert::AreEqual<std::uint8_t>(' ', Elite::KEY_TRANSLATION[Elite::KEY_SPEED_UP], L"TRANTABLE: Space");
      Assert::AreEqual<std::uint8_t>('A', Elite::KEY_TRANSLATION[Elite::KEY_FIRE], L"TRANTABLE: A");

      const std::vector<Script> SCRIPTS = {
        {"N at the prompt", false, KEY_N_INTERNAL, {}, {FIRST, SECOND}},
        {"a key that is not Y", false, Elite::KEY_SPEED_UP, {}, {FIRST, SECOND}},
        {"the fire button", false, Elite::KEY_FIRE, {}, {FIRST, SECOND}},
        {"Y, then leave the menu", false, Elite::KEY_YES_INTERNAL, {'5'}, {FIRST, SECOND}},
        {"Y, toggle the media, then leave", false, Elite::KEY_YES_INTERNAL, {'3', '5'}, {FIRST, SECOND}},
        {"a cold start", true, KEY_N_INTERNAL, {}, {FIRST, SECOND}},
        {"a cold start into the menu", true, Elite::KEY_YES_INTERNAL, {'5'}, {FIRST, SECOND}},
      };

      std::uint32_t compared = 0;

      for (const Script& script : SCRIPTS)
      {
        const std::wstring where = Widen(std::string("BR1: ") + script.what);

        // The commander the sequence starts from: a valid save image whose coordinates fall
        // BETWEEN systems, so the snap through ping/TT111/jmp actually moves them.
        Elite::Commander saved = Elite::DefaultCommander();
        saved.systemX = 0x63;
        saved.systemY = 0x4D;
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

        /*
         * NO NAMED TRAP WHERE THERE WERE FOUR (M3-b-2b, M6-0-h-1). `startat`, `stopat` and `stopbd`
         * are trapped nowhere: both machines run the music player, and the SID writes below are
         * what the comparison is made of. `ZEKTRAN` was the last of them, trapped and counted as a
         * seam until M6-0-h-1; it runs on both machines now, over a logger seeded FULL on both, and
         * the sixty-five bytes it leaves are compared.
         */
        cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
        for (const char* seam :
             {"DOXC", "DOYC", "MT9", "NLIN4", "FILEPR", "OTHERFILEPR", "KERNALSETUP", "SWAPPZERO", "DELAY", "FLKB", "DOVDU19"})
        {
          cpu.AddTrap(oracle.Label(seam));
        }

        // 6502: CIA1 port A, as the previous scan's `STA &DC00` leaves it (M6-0-a-4): the joystick
        // reads idle, so `TITLE`'s `RDKEY` walks the matrix and finds the key held there.
        cpu.Io(Cpu6502::CIA1_PORT_A) = 0x7Fu;

        // 6502: SID -- every store the music makes, in order, which is what the port's log holds.
        cpu.LogStores(SID_BASE, static_cast<std::uint16_t>(SID_BASE + 0x18));

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
        const std::uint16_t klo = oracle.Label("KLO");
        for (std::size_t slot = 0; slot < std::tuple_size_v<Elite::KeyLogger>; ++slot)
        {
          cpu.memory[static_cast<std::uint16_t>(klo + slot)] = 0xFFu; // 6502: KEYLOOK -- full, for ZEKTRAN to clear
        }

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
            /*
             * 6502: JSR TITLE -- the arguments in A, X and Y, recorded; and the routine RUNS since
             * M6-0-h-2, ending on the key the script holds on the CIA from this screen's first
             * scan: the script's key at the first screen, Space at the second (M6-0-a-4).
             */
            seams.push_back({"TITLE", cpu.a, cpu.x, cpu.y});
            const std::uint8_t key = (titles == 0) ? script.firstKey : Elite::KEY_SPEED_UP;
            ++titles;
            cpu.keysDown.fill(0u);
            const std::size_t at = 0x40u - key;
            cpu.HoldKey(static_cast<std::uint8_t>(at >> 3), static_cast<std::uint8_t>(at & 0x07u));
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

          Assert::IsTrue(cpu.Step(), (where + L": BR1 should not reach an unimplemented opcode").c_str());
        }
        Assert::IsTrue(completed, (where + L": BR1 should run off its end into BAY").c_str());

        // ---- the port ------------------------------------------------------------------------
        CountingSink sink;

        /*
         * The universe, and the names below are ALIASES INTO IT rather than separate objects.
         *
         * `BR1` takes `(Universe&, Ports&)` since M3-a-3 -- `GameStart` went with `SaveScreen`,
         * because it held one -- so every byte the sequence reads or writes is this object's, and
         * the assertions below still read as they did.
         */
        Elite::Universe universe;
        universe.keys.fill(0xFFu); // 6502: KEYLOOK -- full, as on the oracle
        Elite::TextState& text = universe.text;
        text.column = 1;
        text.row = 1;
        Elite::CharacterPrinter characters(sink, universe.sentences);
        Elite::TokenPrinter recursive(characters, text);
        Elite::Rng& rng = universe.rng;
        Elite::ExtendedTokenPrinter extended(characters, recursive, rng);

        ScriptedKeys keys(script.menuKeys);
        DeviceStore store;

        Elite::Commander& commander = universe.commander;
        universe.commanderFile = image;

        CurrentSystem& current = universe.current;
        SystemSeeds& selected = universe.selectedSeeds;
        std::uint8_t& crosshairX = universe.crosshairX;
        std::uint8_t& crosshairY = universe.crosshairY;
        std::uint8_t& explosionCount = universe.explosions;
        explosionCount = 0xEE;
        std::uint8_t& dockedFlag = universe.dockedFlag;

        RecordingStart effects;
        keys.titleKeys = {script.firstKey, Elite::KEY_SPEED_UP};
        Elite::SidWriteLog sid; ///< 6502: SID -- what `startat`, `stopat` and `stopbd` write
        NullSeams nulls;
        Elite::Ports ports{recursive, characters, sink, sid,
                           extended,  effects, keys, store};

        /*
         * 6502: msblob -- the one thing the sequence draws, and a count cannot say so any more
         * (M3-b-1e). Nothing else here touches the canvas: the text goes into `CountingSink` and
         * `TITLE` is still a seam. So a canvas that gained ink is a `msblob` that ran.
         */
        std::uint32_t inkBefore = 0;
        for (const std::uint8_t byte : universe.canvas.Screen())
        {
          inkBefore += (byte != 0u) ? 1u : 0u;
        }

        const Elite::ForcedKey forced =
          script.coldStart ? Elite::ResetAndStartGame(universe, ports, false) : Elite::StartGame(universe, ports, false);

        std::uint32_t inkAfter = 0;
        for (const std::uint8_t byte : universe.canvas.Screen())
        {
          inkAfter += (byte != 0u) ? 1u : 0u;
        }
        Assert::IsTrue(inkAfter > inkBefore, (where + L": msblob drew the missile indicators").c_str());

        // ---- compare -------------------------------------------------------------------------
        Assert::IsFalse(keys.overran, (where + L": the port asked for more keys than the script holds").c_str());
        Assert::AreEqual(keysTaken, keys.Taken(), (where + L": how many keys the menu read").c_str());

        // 6502: the two `JSR TITLE`s, with their arguments, in order -- the shipped routine's own
        // calls, watched at the label. The port has no seam there to record any more (M6-0-h-2);
        // what it did at the title is compared below, as state.
        Assert::AreEqual(script.expected.size(), seams.size(),
                         (where + L": the game reached " + Describe(seams) + L", expected " + Describe(script.expected)).c_str());
        for (std::size_t index = 0; index < seams.size(); ++index)
        {
          Assert::IsTrue(seams[index] == script.expected[index],
                         (where + L": seam " + std::to_wstring(index) + L" -- game " + Describe(seams)).c_str());
        }
        Assert::IsTrue(effects.seams.empty(), (where + L": the port reached a seam the sequence has none of: " + Describe(effects.seams)).c_str());
        Assert::AreEqual<std::uint32_t>(2u, effects.titleFrames, (where + L": one frame per title screen, each ended by a held key").c_str());

        /*
         * 6502: what the two title screens leave behind, on both machines -- the second screen's
         * Adder in `TYPE` and `INWK`, the bubble `RESET` and `NWSHP` left, `JSTK` (&FF if the fire
         * button ended a screen, zero if a key did), and the title loop's own counters.
         */
        Assert::AreEqual(cpu.memory[oracle.Label("TYPE")], Elite::Byte(universe.flight.type), (where + L": TYPE").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("JSTK")], universe.options.joystick, (where + L": JSTK").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("DELTA")], universe.flight.speed, (where + L": DELTA").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("MCNT")], universe.flight.mainLoopCounter, (where + L": MCNT").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("CNT2")], universe.flight.steerCone, (where + L": CNT2").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("QQ11")], universe.view, (where + L": QQ11").c_str());
        for (std::size_t slot = 0; slot < universe.bubble.slots.size(); ++slot)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("FRIN") + slot)], universe.bubble.slots[slot],
                           (where + L": FRIN+" + std::to_wstring(slot)).c_str());
        }
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("INWK") + byte)], universe.work.ToBytes()[byte],
                           (where + L": INWK+" + std::to_wstring(byte)).c_str());
        }

        // 6502: KEYLOOK after ZEKTRAN -- sixty-five bytes, cleared on both machines rather than
        // counted as a seam on both (M6-0-h-1).
        for (std::size_t slot = 0; slot < std::tuple_size_v<Elite::KeyLogger>; ++slot)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(klo + slot)], universe.keys[slot],
                           (where + L": KLO+" + std::to_wstring(slot)).c_str());
        }

        /*
         * 6502: SID and MUPLA -- what the music did, on both machines, since neither is trapped.
         *
         * The order matters and the picture does not (`SoundEffects.h`): `BDENTRY` runs the chip's
         * registers down and then sets four of them, which a real SID hears as a gate falling and
         * rising. So the writes are compared one at a time rather than as a final state.
         */
        Assert::AreEqual<std::size_t>(0, sid.dropped, (where + L": the port's SID log overflowed").c_str());
        Assert::AreEqual<std::size_t>(cpu.stores.size(), sid.count, (where + L": how many SID writes").c_str());
        for (std::size_t index = 0; index < sid.count; ++index)
        {
          const std::wstring at = where + L" SID write " + std::to_wstring(index);
          Assert::AreEqual<int>(cpu.stores[index].address - SID_BASE, sid.writes[index].reg, (at + L": register").c_str());
          Assert::AreEqual<int>(cpu.stores[index].value, sid.writes[index].value, (at + L": value").c_str());
        }

        Assert::AreEqual<std::uint8_t>(cpu.memory[oracle.Label("MUPLA")], universe.music.playing,
                                       (where + L": MUPLA -- whether a tune is playing").c_str());
        Assert::AreEqual<std::uint8_t>(cpu.memory[oracle.Label("MULIE")], universe.status.titleReset,
                                       (where + L": MULIE -- the title screen's bracket").c_str());

        // 6502: LDA #3 / JSR DOXC, which is the port's text.column and not a seam.
        Assert::IsTrue(sawColumn, (where + L": the game should set the prompt column").c_str());
        Assert::AreEqual<std::uint8_t>(Elite::TITLE_PROMPT_COLUMN, firstColumn, (where + L": the column the game set").c_str());
        // The port's column is not compared here any more: the title screens print after `BR1`'s
        // `LDA #3 / JSR DOXC` and move it, on both machines, and the oracle's `DOXC` is trapped.

        // The state the sequence leaves behind, which is what the game then plays.
        /*
         * 6502: QQ0 and QQ1 are TP+1 and TP+2 -- two bytes of the COMMANDER, which is why the
         * comparison of the whole block below covers them and why the port has no separate copy.
         */
        Assert::AreEqual(cpu.memory[oracle.Label("QQ0")], commander.systemX,
                         (where + L": QQ0, which is the commander's").c_str());
        Assert::AreEqual(cpu.memory[oracle.Label("QQ1")], commander.systemY,
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
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("TP") + index)], commander.ToBytes()[index],
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

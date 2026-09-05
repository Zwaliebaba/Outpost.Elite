#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "Missions.h"
#include "ShipBlueprint.h"

#include <array>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

namespace GameLogicTests
{

  namespace
  {
    /*
     * 6502: RDKEY, scripted -- the same device `TheTitleMatchesTITLE` uses and for the same reason.
     *
     * `PAUSE`'s two loops are key-driven, so a trap that answered one way for ever would either
     * never leave the first loop or never leave the second. The port's side counts calls and the
     * oracle's is a stub written over `RDKEY` that counts the same way, so both sides run the same
     * number of frames and the comparison is of what those frames drew.
     *
     * `quiet` is how many scans answer "no key" before one answers `key`. `PAUSE` needs at least
     * one of each in that order; `PAUSE2` needs the same.
     */
    struct ScriptedStart final : Elite::StartUpEffects
    {
      std::uint32_t quiet = 0;
      std::uint8_t key = 0;
      std::uint32_t scans = 0;
      std::vector<std::uint8_t> delays;

      void ResetUniverse() override {}
      void ResetShip() override {}
      void ClearKeyLogger() override {}
      void StartTheme() override {}
      void StopTheme() override {}
      void ResetMissileIndicators() override {}

      [[nodiscard]] Elite::TitleKey ScanTitleKeys(Elite::KeyLogger&) override
      {
        ++scans;
        if (scans <= quiet)
        {
          return {};
        }
        return {true, key};
      }

      void WaitFrames(std::uint8_t _frames) override
      {
        delays.push_back(_frames);
      }

      std::uint8_t ShowTitleScreen(std::uint8_t, std::uint8_t, std::uint8_t) override
      {
        return 0;
      }
    };

    /*
     * The counted `RDKEY` written over the real one, in eleven bytes.
     *
     *    0  CE lo hi   DEC counter
     *    3  D0 05      BNE quiet
     *    5  A9 kk      LDA #key
     *    7  AA         TAX
     *    8  38         SEC
     *    9  60         RTS
     *   10  A9 00 quiet: LDA #0
     *   12  AA         TAX
     *   13  18         CLC
     *   14  60         RTS
     *   15  nn         counter
     *
     * `RDKEY` returns `thiskey` in BOTH A and X (`LDA thiskey / TAX / RTS`), and `PAUSE` branches
     * on the flags the `LDA` set -- so the stub has to load A last of the two and cannot simply
     * `LDX`.
     */
    void ScriptRdkey(Cpu6502& _cpu, std::uint16_t _rdkey, std::uint32_t _quiet, std::uint8_t _key)
    {
      const std::uint16_t counter = static_cast<std::uint16_t>(_rdkey + 15u);
      const std::uint8_t stub[16] = {0xCEu,
                                     static_cast<std::uint8_t>(counter & 0xFFu),
                                     static_cast<std::uint8_t>(counter >> 8),
                                     0xD0u,
                                     0x05u,
                                     0xA9u,
                                     _key,
                                     0xAAu,
                                     0x38u,
                                     0x60u,
                                     0xA9u,
                                     0x00u,
                                     0xAAu,
                                     0x18u,
                                     0x60u,
                                     static_cast<std::uint8_t>(_quiet + 1u)};
      _cpu.Load(_rdkey, stub, sizeof(stub));
    }

    /// Where the mission routines live, looked up once.
    struct MissionWhere
    {
      std::uint16_t pause, pas1, pause2, bris, mt27, mt28, rdkey, detok, delay, gcnt, xc, yc, ll9, setl1, dovdu19, nosprites;
      std::uint16_t alpha, alp2Next, bet2, bet2Next, typeByte, xsav, inf, kPercent;

      explicit MissionWhere(const OracleImage& _oracle)
      {
        pause = _oracle.Label("PAUSE");
        pas1 = _oracle.Label("PAS1");
        pause2 = _oracle.Label("PAUSE2");
        bris = _oracle.Label("BRIS");
        mt27 = _oracle.Label("MT27");
        mt28 = _oracle.Label("MT28");
        rdkey = _oracle.Label("RDKEY");
        detok = _oracle.Label("DETOK");
        delay = _oracle.Label("DELAY");
        gcnt = _oracle.Label("GCNT");
        xc = _oracle.Label("XC");
        yc = _oracle.Label("YC");
        ll9 = _oracle.Label("LL9");
        setl1 = _oracle.Label("SETL1");
        dovdu19 = _oracle.Label("DOVDU19");
        nosprites = _oracle.Label("NOSPRITES");

        /*
         * What `MVEIT` and `LL9` read that `Mirror` does not send.
         *
         * `Mirror` is the SCREEN routines' list and this is the flight model's: the player's
         * rotation rates, the type of the ship being moved, the slot it came from, and `INF`,
         * which is the pointer `LL9` part 1 writes two bytes of the block through.
         */
        alpha = _oracle.Label("ALPHA");
        alp2Next = static_cast<std::uint16_t>(_oracle.Label("ALP2") + 1u);
        bet2 = _oracle.Label("BET2");
        bet2Next = static_cast<std::uint16_t>(bet2 + 1u);
        typeByte = _oracle.Label("TYPE");
        xsav = _oracle.Label("XSAV");
        inf = _oracle.Label("INF");
        kPercent = _oracle.Label("K%");
      }
    };

    /// The first byte of the arena that is heap rather than ship block.
    constexpr std::uint16_t HEAP_START = Elite::SHIP_BLOCK_BASE + Elite::MAX_SHIPS * Elite::SHIP_BLOCK_SIZE;
  } // namespace

  /*
   * Slice 4d-b: the four control codes a mission briefing is made of.
   *
   * A BRIEFING IS NOT A SCREEN, and that is what this suite is shaped around. Elite has no
   * briefing screen: token 10 is a paragraph like any other, and what turns it into a briefing is
   * that four of the control codes inside it stop and wait. `{25}` prints "INCOMING MESSAGE" and
   * pauses; `{22}` spins the Constrictor until a key is pressed; `{24}` waits without a ship;
   * `{27}` and `{28}` name a captain and a planet from the galaxy number. So the routines here are
   * the ones the TEXT calls, and slice 4d-c's missions are the state around them.
   */
  TEST_CLASS(TheMissionText)
  {
  public:
    /*
     * 6502: PAS1 -- one frame of the briefing ship, on the whole canvas.
     *
     * Four stores, `LL9`, `MVEIT` and a keyboard scan, and the four stores are the reason the ship
     * turns on the spot: `MVEIT` moves it and the next pass puts x, z and y straight back.
     *
     * `INWK+7` GETS 2 ON THIS BUILD and the upstream comment says 1. The Master and Apple versions
     * load 1; the C64 loads 2, so the ship sits at (z_hi z_lo) = 512 rather than 256 -- twice as
     * far away and half the size on screen. `tools/c64_source.py` is what stops a port inheriting
     * the comment, and the sweep below is what would fail if it had.
     */
    TEST_METHOD(TheBriefingShipMatchesPAS1)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const MissionWhere to(oracle);

      std::uint32_t compared = 0;

      for (const std::uint8_t type : {Elite::SHIP_COBRA_MK3, Elite::SHIP_ADDER})
      {
        for (const std::uint8_t roll : {std::uint8_t{0}, std::uint8_t{0x7Fu}, std::uint8_t{0x80u}})
        {
          for (const std::uint8_t pitch : {std::uint8_t{0}, std::uint8_t{0x7Fu}})
          {
            LoopWorld world;
            Seed(world.world, type * 31u + roll + pitch);
            world.world.LendSunHeap(world.heap);
            world.world.trumbles.count = 0u;

            SetUpBriefingShip(world, type, roll, pitch);

            Cpu6502 cpu = oracle.Fresh();
            Trap(cpu, to);
            ScriptRdkey(cpu, to.rdkey, 0u, 0x27u);
            FillScreens(cpu, world.world.canvas, at.screen, 0x1Du);
            Mirror(world.world, cpu, at);
            MirrorMission(world, cpu, at, to, 0u);

            Assert::IsTrue(cpu.CallSubroutine(to.pas1, 2'000'000).completed, L"PAS1 returned");

            ScriptedStart start;
            start.key = 0x27u;
            Elite::FlightScreen screen = world.world.Screen();
            Elite::FlightLoop loop = LoopOver(world, screen);
            Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
            const Elite::TitleKey answer = Elite::ShowBriefingShip(mission);

            const std::wstring where =
              WidenText("PAS1 (ship " + std::to_string(type) + ", roll " + std::to_string(roll) + ", pitch " + std::to_string(pitch) + ")");

            Assert::AreEqual<std::uint32_t>(0x27u, answer.key, (where + L": thiskey").c_str());
            CompareBlock(cpu, world, at, where);
            CompareState(cpu, world.world, at, where);
            CompareScreens(cpu, at.screen, world.world.canvas, 0x1Du, where);
            CompareHeap(cpu, world, where);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(2u * 3u * 2u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: PAUSE -- the two loops, the screen change, and the fall into MT23.
     *
     * THE FIRST LOOP IS THE ONE WORTH TESTING. `JSR PAS1 / BNE PAUSE` runs while a key is still
     * HELD, so a briefing waits for the player to let go of the key that turned the previous page
     * before it starts watching for the next one. Without it a single keystroke would run the
     * whole briefing past in one frame -- and a port that dropped it would pass any test that did
     * not START with a key down, which is why `held` sweeps both.
     *
     * The comparison is the whole canvas over every frame the loop ran, plus `YC`, which is all
     * that MT23's fall-through leaves behind on this side of the seam.
     */
    TEST_METHOD(ThePauseMatchesPAUSE)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const MissionWhere to(oracle);

      std::uint32_t compared = 0;

      for (const std::uint32_t quiet : {std::uint32_t{1}, std::uint32_t{2}, std::uint32_t{5}})
      {
        for (const std::uint8_t type : {Elite::SHIP_COBRA_MK3, Elite::SHIP_ADDER})
        {
          LoopWorld world;
          Seed(world.world, quiet * 17u + type);
          world.world.LendSunHeap(world.heap);
          world.world.trumbles.count = 0u;
          world.world.text.row = 0x17u; // so MT23's row 10 is a change rather than a coincidence
          world.world.text.column = 0x1Du;

          SetUpBriefingShip(world, type, 0x7Fu, 0x7Fu);

          Cpu6502 cpu = oracle.Fresh();
          Trap(cpu, to);
          ScriptRdkey(cpu, to.rdkey, quiet, 0x27u);
          FillScreens(cpu, world.world.canvas, at.screen, 0x1Du);
          Mirror(world.world, cpu, at);
          MirrorMission(world, cpu, at, to, 0u);

          Assert::IsTrue(cpu.CallSubroutine(to.pause, 20'000'000).completed, L"PAUSE returned");

          ScriptedStart start;
          start.quiet = quiet;
          start.key = 0x27u;
          Elite::FlightScreen screen = world.world.Screen();
          Elite::FlightLoop loop = LoopOver(world, screen);
          std::uint8_t galaxy = 0;
          Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
          Elite::MissionCodes codes{mission, world.world.text, galaxy};
          world.world.codes.to = &codes;

          /*
           * Through the PRINTER rather than by calling `PauseForKey`, because the fall-through into
           * MT23 is split across two objects: the row is the seam's and the two case flags are the
           * text system's. `DETOK2` with A = 22 is what the oracle runs, and this is its opposite
           * number.
           */
          world.world.extendedPrinter.PrintByte(22u);

          const std::wstring where = WidenText("PAUSE (" + std::to_string(quiet) + " quiet scans, ship " + std::to_string(type) + ")");

          /*
           * The first loop runs once -- the scripted key is "down" on scan 1 only when `quiet` is
           * zero, and it never is here -- so the count is `quiet` refusals plus one press, and the
           * second loop takes the press. A port that dropped the release wait would scan one time
           * fewer.
           */
          Assert::AreEqual<std::uint32_t>(quiet + 1u, start.scans, (where + L": RDKEY calls").c_str());

          Assert::AreEqual<std::uint32_t>(cpu.memory[to.yc], world.world.text.row, (where + L": YC after MT23").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[to.xc], world.world.text.column, (where + L": XC, which MT23 leaves alone").c_str());

          CompareBlock(cpu, world, at, where);
          CompareState(cpu, world.world, at, where);
          CompareScreens(cpu, at.screen, world.world.canvas, 0x1Du, where);
          CompareHeap(cpu, world, where);
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(6u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: PAUSE2 -- the same wait with nothing drawn, and a branch that goes further back than
     * it looks.
     *
     * `JSR RDKEY / BNE PAUSE2 / JSR RDKEY / BEQ PAUSE2`, and the second branch targets the TOP of
     * the routine rather than the scan above it. So an empty keyboard on the second scan sends the
     * routine back to re-check for a release it has already had, which costs one extra scan per
     * failed attempt -- and counting the scans is the only way to see it, because nothing is drawn.
     *
     * Swept over how long the keyboard stays empty, so the loop goes round once, twice and five
     * times, and the counts are compared against the oracle's own by counting its stub's calls.
     */
    TEST_METHOD(TheKeyWaitMatchesPAUSE2)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const MissionWhere to(oracle);

      std::uint32_t compared = 0;

      for (const std::uint32_t quiet : {std::uint32_t{0}, std::uint32_t{1}, std::uint32_t{2}, std::uint32_t{5}})
      {
        Cpu6502 cpu = oracle.Fresh();
        ScriptRdkey(cpu, to.rdkey, quiet, 0x27u);
        const std::uint16_t counter = static_cast<std::uint16_t>(to.rdkey + 15u);

        Assert::IsTrue(cpu.CallSubroutine(to.pause2, 200'000).completed, L"PAUSE2 returned");

        // The stub counts DOWN from `quiet + 1`, so what is left says how many scans it answered.
        const std::uint32_t theirScans = static_cast<std::uint32_t>((quiet + 1u - cpu.memory[counter]) & 0xFFu);

        ScriptedStart start;
        start.quiet = quiet;
        start.key = 0x27u;

        LoopWorld world;
        Elite::FlightScreen screen = world.world.Screen();
        Elite::FlightLoop loop = LoopOver(world, screen);
        Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
        Elite::WaitForKeyPress(mission);

        const std::wstring where = WidenText("PAUSE2 (" + std::to_string(quiet) + " quiet scans)");
        Assert::AreEqual<std::uint32_t>(theirScans, start.scans, (where + L": RDKEY calls").c_str());
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(4u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: BRIS -- "INCOMING MESSAGE", and a hundred frames of it.
     *
     * The token is compared character for character through a trap on `CHPR`, and the delay is
     * compared as a number because `DELAY` is `WSCAN` in a loop and neither side has a raster.
     * Token 216 opens with `{9}`, which clears to a new view, so the port's control-code seam has
     * to be answered for the two sides to print the same thing -- which is the point: this is the
     * first briefing routine and it needs the ones under it.
     */
    TEST_METHOD(TheIncomingMessageMatchesBRIS)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const MissionWhere to(oracle);

      LoopWorld world;
      Seed(world.world, 0x4Du);
      world.world.LendSunHeap(world.heap);
      world.world.trumbles.count = 0u;

      Cpu6502 cpu = oracle.Fresh();
      Trap(cpu, to);
      cpu.AddTrap(to.delay);
      FillScreens(cpu, world.world.canvas, at.screen, 0x1Du);
      Mirror(world.world, cpu, at);
      MirrorMission(world, cpu, at, to, 0u);

      Assert::IsTrue(cpu.CallSubroutine(to.bris, 4'000'000).completed, L"BRIS returned");

      std::uint32_t theirDelays = 0;
      std::uint8_t theirFrames = 0;
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        if (hit.address == to.delay)
        {
          ++theirDelays;
          theirFrames = hit.y;
        }
      }

      ScriptedStart start;
      std::uint8_t galaxy = 0;
      Elite::FlightScreen screen = world.world.Screen();
      Elite::FlightLoop loop = LoopOver(world, screen);
      Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
      Elite::MissionCodes codes{mission, world.world.text, galaxy};
      world.world.codes.to = &codes;

      Elite::ShowIncomingMessage(mission);

      Assert::AreEqual<std::uint32_t>(1u, theirDelays, L"BRIS delays exactly once");
      Assert::AreEqual<std::size_t>(1u, start.delays.size(), L"and so does the port");
      Assert::AreEqual(theirFrames, start.delays[0], L"for the same number of frames");
      Assert::AreEqual<std::uint32_t>(Elite::INCOMING_MESSAGE_FRAMES, start.delays[0], L"which is a hundred");

      /*
       * The WHOLE CANVAS, because `CHPR` is not trapped: token 216 opens with `{9}`, which clears
       * the screen and moves the cursor, and comparing a character stream would leave out the two
       * things the code actually does. Both sides run the real character printer into the bitmap.
       */
      CompareState(cpu, world.world, at, L"BRIS");
      CompareScreens(cpu, at.screen, world.world.canvas, 0x1Du, L"BRIS");
      Assert::AreEqual<std::uint32_t>(cpu.memory[to.xc], world.world.text.column, L"BRIS: XC");
      Assert::AreEqual<std::uint32_t>(cpu.memory[to.yc], world.world.text.row, L"BRIS: YC");
    }

    /*
     * 6502: MT27 and MT28 -- the captain and the planet, one token per galaxy.
     *
     * Eight galaxies each, sixteen cases, compared character for character. The two ranges OVERLAP
     * -- 217 to 224 and 220 to 227 -- so galaxy 3's captain and galaxy 0's planet are the same
     * token, which is worth having a test say rather than a reader having to work out.
     *
     * MT27's whole body is `LDA #217 / BNE P%+4`, and the branch lands on MT28's `CLC` -- skipping
     * only the `LDA #220`. Two entry points, one addition, and a port that duplicated the addition
     * would be right and a port that duplicated the WRONG constant would be caught here.
     */
    TEST_METHOD(TheMissionNamesMatchMT27AndMT28)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const MissionWhere to(oracle);

      /*
       * THE TWO RANGES OVERLAP AND THEY RUN OFF THE END OF THE NAMES.
       *
       * MT27 is 217 + GCNT and MT28 is 220 + GCNT, so galaxy 3's captain is galaxy 0's planet. And
       * above galaxy 2 both walk out of the names entirely: 217, 218 and 219 are CURRUTHERS,
       * FOSDYKE SMYTHE and FORTESQUE, 220, 221 and 222 are the three the Constrictor can be hiding
       * in -- and 223 onwards are the Thargoid-plans briefing, its thank-you, and three words the
       * system descriptions use.
       *
       * That is the game's rather than a defect: the Constrictor mission is only offered in the
       * first few galaxies, so a high `GCNT` is a state neither routine can be reached in. The
       * token NUMBER is compared for all eight anyway, because the addition is what these two
       * routines ARE; the printed TEXT is compared where the token is still prose.
       *
       * The two ranges run out at different points, which is why this is two numbers. MT27 has
       * three captains -- CURRUTHERS, FOSDYKE SMYTHE, FORTESQUE -- and MT28 has two clauses, a
       * planet name and "IS BELIEVED TO HAVE JUMPED TO GALAXY". 222 is the briefing itself, and
       * printing it here would need a screen and a keyboard, which is slice 4d-c's business.
       */
      constexpr std::uint8_t CAPTAIN_GALAXIES = 3;
      constexpr std::uint8_t PLANET_GALAXIES = 2;

      std::uint32_t compared = 0;
      std::uint32_t printed = 0;

      for (std::uint8_t galaxy = 0; galaxy < 8u; ++galaxy)
      {
        for (const bool captain : {true, false})
        {
          const std::wstring where = WidenText(std::string(captain ? "MT27" : "MT28") + " (galaxy " + std::to_string(galaxy) + ")");

          // ---- the token number, for every galaxy --------------------------------------------------
          {
            Cpu6502 cpu = oracle.Fresh();
            cpu.AddTrap(to.detok);
            cpu.memory[to.gcnt] = galaxy;

            Assert::IsTrue(cpu.CallSubroutine(captain ? to.mt27 : to.mt28, 20'000).completed, (where + L": returned").c_str());

            std::uint32_t calls = 0;
            std::uint8_t theirToken = 0;
            for (const Cpu6502::TrapHit& hit : cpu.trapHits)
            {
              if (hit.address == to.detok)
              {
                ++calls;
                theirToken = hit.a;
              }
            }

            Assert::AreEqual<std::uint32_t>(1u, calls, (where + L": one DETOK").c_str());

            const std::uint8_t base = captain ? Elite::MISSION_CAPTAIN_TOKEN : Elite::MISSION_PLANET_TOKEN;
            Assert::AreEqual<std::uint32_t>(theirToken, static_cast<std::uint32_t>(base + galaxy), (where + L": token number").c_str());
            ++compared;
          }

          if (galaxy >= (captain ? CAPTAIN_GALAXIES : PLANET_GALAXIES))
          {
            continue;
          }

          // ---- and the text, for the three where the token is a name -------------------------------
          LoopWorld world;
          Seed(world.world, galaxy * 7u + (captain ? 1u : 0u));
          world.world.LendSunHeap(world.heap);
          world.world.trumbles.count = 0u;
          world.world.text.column = 1u; // 6502: XC -- `Seed` leaves it at 31, which is off the edge
          world.world.text.row = 10u;   // 6502: YC -- where MT23 would have put it

          /*
           * 6502: GCNT, and it is `TP+15` -- INSIDE the commander block, one field along from
           * slice 2e's finding about `QQ0` and `QQ1`.
           *
           * So the galaxy is set here rather than by poking the label: `Mirror` sends the whole
           * block, and a fixture that wrote `GCNT` directly would have it overwritten a line later.
           * This one did, and every galaxy above zero was silently compared against galaxy zero's
           * token until the two character streams were dumped side by side.
           */
          world.world.commander.At(Elite::Field::GalaxyNumber) = galaxy;

          Cpu6502 cpu = oracle.Fresh();
          Trap(cpu, to);
          FillScreens(cpu, world.world.canvas, at.screen, 0x1Du);
          Mirror(world.world, cpu, at);
          MirrorMission(world, cpu, at, to, 0u);

          Assert::IsTrue(cpu.CallSubroutine(captain ? to.mt27 : to.mt28, 4'000'000).completed, (where + L": printed").c_str());

          ScriptedStart start;
          Elite::FlightScreen screen = world.world.Screen();
          Elite::FlightLoop loop = LoopOver(world, screen);
          Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
          Elite::MissionCodes codes{mission, world.world.text, world.world.commander.At(Elite::Field::GalaxyNumber)};
          world.world.codes.to = &codes;

          /*
           * Through the control-code dispatch rather than by calling `PrintMissionToken` -- these
           * ARE control codes 27 and 28, and running them the way a token does is what checks that
           * the dispatch reaches them with the right base.
           */
          world.world.extendedPrinter.PrintByte(captain ? std::uint8_t{27u} : std::uint8_t{28u});

          CompareState(cpu, world.world, at, where);
          CompareScreens(cpu, at.screen, world.world.canvas, 0x1Du, where);
          Assert::AreEqual<std::uint32_t>(cpu.memory[to.xc], world.world.text.column, (where + L": XC").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[to.yc], world.world.text.row, (where + L": YC").c_str());
          ++printed;
        }
      }

      Assert::AreEqual<std::uint32_t>(16u, compared, L"every galaxy's token number was compared");
      Assert::AreEqual<std::uint32_t>(CAPTAIN_GALAXIES + PLANET_GALAXIES, printed, L"and the five that are prose were printed");
    }

  private:
    /// The seams that are not this slice's, trapped so both sides do the same nothing.
    static void Trap(Cpu6502& _cpu, const MissionWhere& _to)
    {
      _cpu.AddTrap(_to.setl1);
      _cpu.AddTrap(_to.dovdu19);

      /*
       * `NOSPRITES` is trapped for §6.108's reason and not because it is a seam: `XX21` is at
       * &D000 and so are the VIC-II registers, so its `STA VIC+&15` lands on a blueprint pointer
       * in a flat image and `NWSHP` then refuses the ship it names.
       */
      _cpu.AddTrap(_to.nosprites);
      _cpu.AddTrap(OracleImage::Instance().Label("TACTICS"));
      _cpu.AddTrap(OracleImage::Instance().Label("DOEXP"));
      _cpu.AddTrap(OracleImage::Instance().Label("PLANET"));
    }

    /// A Constrictor-shaped ship in slot 0, turning, which is what `BRIEF` leaves for `PAS1`.
    static void SetUpBriefingShip(LoopWorld& _world, std::uint8_t _type, std::uint8_t _roll, std::uint8_t _pitch)
    {
      World& world = _world.world;

      world.flight.type = _type;
      world.flight.blueprint = Elite::BlueprintAddress(_type);
      world.bubble.slots[0] = _type;
      world.bubble.slots[1] = 0u;
      world.bubble.slots[2] = 0u;
      world.view = 1u;      // 6502: QQ11 -- the briefing runs on the space view `BRIEF` set up
      world.spaceView = 0u; // 6502: VIEW -- forwards

      /*
       * 6502: what `ZINF` and `NWSHP` leave, plus the roll and pitch `BRL1` writes.
       *
       * Byte 31 is the "drawn" flag with the exploding bit clear, and bytes 32 and 34 are zero so
       * that `MVEIT` cannot reach `TACTICS` -- the same reason `TITLE`'s ship cannot (§6.122).
       */
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        world.work[byte] = 0u;
      }
      /*
       * 6502: what `ZINF` leaves -- 96 in the roof's y and the side's x, and 96 WITH THE SIGN BIT
       * in the nose's z, so the ship faces the player. `TITLE` writes a bare 96 over that third one
       * and gets a ship facing away; a briefing does not, because `BRIEF` never touches it.
       */
      world.work[14] = 0xE0u;
      world.work[18] = 96u;
      world.work[22] = 96u;

      world.flight.slot = 0u; // 6502: XSAV -- which slot `MVEIT` thinks it is moving
      world.work[29] = _roll;
      world.work[30] = _pitch;
      world.work[31] = 0u;
      world.work[32] = 0u;

      /*
       * 6502: INWK+33 and INWK+34 -- the ship's OWN line heap, which `NWSHP` allocates.
       *
       * Zero is not a value the game can have here, and leaving it zero is not a harmless
       * simplification: `LL9` writes its lines through `(XX19),Y`, so a pointer of zero puts them
       * in ZERO PAGE -- and `INWK` is at &0009, so the ship overwrites its own position while it is
       * being drawn. That is what the first draft of this fixture did, and the sixteen bytes it
       * scribbled were the whole of the disagreement.
       */
      const std::uint16_t heap = static_cast<std::uint16_t>(Elite::SHIP_HEAP_TOP - 0x100u);
      world.work[Elite::SHIP_HEAP_LOW_OFFSET] = static_cast<std::uint8_t>(heap & 0xFFu);
      world.work[Elite::SHIP_HEAP_HIGH_OFFSET] = static_cast<std::uint8_t>(heap >> 8);
      world.bubble.heapBottom = heap;

      world.bubble.blocks[0] = world.work;
    }

    /// `FlightLoop` over a `LoopWorld`, which is twelve references nobody should type twice.
    [[nodiscard]] static Elite::FlightLoop LoopOver(LoopWorld& _world, Elite::FlightScreen& _screen)
    {
      return Elite::FlightLoop{_screen,     _world.keys,       _world.control, _world.options, _world.burst,   _world.heap,
                               _world.clip, _world.projection, _world.axes,    _world.effects, _world.effects, _world.effects};
    }

    /// What `Mirror` does not send: the line heap, the flight model's rotation rates, and `INF`.
    static void MirrorMission(const LoopWorld& _world, Cpu6502& _cpu, const Where& _at, const MissionWhere& _to, std::uint8_t _slot)
    {
      for (std::uint16_t address = HEAP_START; address < Elite::LineHeap::TOP; ++address)
      {
        _cpu.memory[address] = _world.heap.Read(address);
      }
      _cpu.memory[_at.lsp] = _world.world.heaps.lsp;

      _cpu.memory[_to.alpha] = _world.world.flight.alpha;
      _cpu.memory[_to.alp2Next] = _world.world.flight.alp2Next;
      _cpu.memory[_to.bet2] = _world.world.flight.bet2;
      _cpu.memory[_to.bet2Next] = _world.world.flight.bet2Next;
      _cpu.memory[_to.typeByte] = _world.world.flight.type;
      _cpu.memory[_to.xsav] = _slot;

      // 6502: INF -- the pointer `LL9` writes the block through, which is `K% + slot * NI%`.
      const std::uint16_t block = static_cast<std::uint16_t>(_to.kPercent + _slot * Elite::SHIP_BLOCK_SIZE);
      _cpu.memory[_to.inf] = static_cast<std::uint8_t>(block & 0xFFu);
      _cpu.memory[static_cast<std::uint16_t>(_to.inf + 1u)] = static_cast<std::uint8_t>(block >> 8);
    }

    /// 6502: INWK, byte for byte, and it is asserted BEFORE the pixels on purpose: a divergence in
    /// the block and one in the bitmap look the same through a canvas compare and are not.
    static void CompareBlock(const Cpu6502& _cpu, const LoopWorld& _world, const Where& _at, const std::wstring& _where)
    {
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.inwk + byte)], _world.world.work[byte],
                         (_where + L": INWK+" + std::to_wstring(byte)).c_str());
      }
    }

    /// 6502: the ship line heap, which `LL9` writes through `XX19`.
    static void CompareHeap(const Cpu6502& _cpu, const LoopWorld& _world, const std::wstring& _where)
    {
      for (std::uint16_t address = HEAP_START; address < Elite::LineHeap::TOP; ++address)
      {
        Assert::AreEqual(_cpu.memory[address], _world.heap.Read(address), (_where + L": heap " + std::to_wstring(address)).c_str());
      }
    }
  };

} // namespace GameLogicTests

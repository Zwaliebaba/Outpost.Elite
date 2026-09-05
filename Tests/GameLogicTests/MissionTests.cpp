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

      /// How many scans answer "pressed" BEFORE the quiet run -- the state `PAUSE`'s first loop
      /// exists for, and the one a script that starts empty cannot reach.
      std::uint32_t held = 0;

      /*
       * When set, the keyboard alternates empty, pressed, empty, pressed for ever.
       *
       * A counted script answers one wait. A token that contains two -- and the Thargoid-plans
       * briefing contains two `{24}`s -- exhausts it and then answers "empty" for ever, which is a
       * hang rather than a failure. This cannot be exhausted.
       */
      bool alternate = false;

      [[nodiscard]] Elite::TitleKey ScanTitleKeys(Elite::KeyLogger&) override
      {
        ++scans;
        if (alternate)
        {
          return ((scans & 1u) == 0u) ? Elite::TitleKey{true, key} : Elite::TitleKey{};
        }
        const std::uint32_t index = scans - 1u;
        const bool pressed = (index < held) || (index >= held + quiet && ((index - held - quiet) % 2u) == 0u);
        return pressed ? Elite::TitleKey{true, key} : Elite::TitleKey{};
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
     * The scripted `RDKEY`, written over the real one, and it is TABLE-DRIVEN.
     *
     * A stub that counts down and answers once has two faults, and this suite found both. It
     * cannot express "a key is already held", which is the state `PAUSE`'s first loop exists for --
     * and its counter WRAPS, so a routine that scans two hundred and fifty-seven times leaves the
     * same byte behind as one that scanned once. The second fault made a test that compared scan
     * counts compare two numbers that happened to agree.
     *
     * So: a separate call counter that only ever goes up, and a table of answers.
     *
     *    0  EE c c    INC calls
     *    3  AE c c    LDX calls
     *    6  BD t t    LDA table,X
     *    9  AA        TAX
     *   10 60         RTS
     *   15 FF         calls, so the first call reads table[0]
     *   16..          table: `_held` keys, then `_quiet` zeroes, then key, empty, key, empty ...
     *
     * THE TAIL ALTERNATES and that is not decoration. `PAUSE2`'s `BEQ` goes back to the TOP of the
     * routine, so a keyboard that stays pressed for ever after the quiet run reads as "still held"
     * and the routine waits for a release that never comes. Alternating gives every wait a release
     * and a press, however many times a token asks for one.
     *
     * `RDKEY` answers in A and X both (`LDA thiskey / TAX / RTS`) and the caller branches on the
     * flags, which `TAX` sets from the same byte -- so the order of the two here is not free.
     */
    constexpr std::size_t RDKEY_TABLE = 16;
    constexpr std::size_t RDKEY_TABLE_SIZE = 48;

    void ScriptRdkey(Cpu6502& _cpu, std::uint16_t _rdkey, std::uint32_t _held, std::uint32_t _quiet, std::uint8_t _key)
    {
      const std::uint16_t calls = static_cast<std::uint16_t>(_rdkey + 15u);
      const std::uint16_t table = static_cast<std::uint16_t>(_rdkey + RDKEY_TABLE);
      const std::uint8_t stub[16] = {0xEEu,
                                     static_cast<std::uint8_t>(calls & 0xFFu),
                                     static_cast<std::uint8_t>(calls >> 8),
                                     0xAEu,
                                     static_cast<std::uint8_t>(calls & 0xFFu),
                                     static_cast<std::uint8_t>(calls >> 8),
                                     0xBDu,
                                     static_cast<std::uint8_t>(table & 0xFFu),
                                     static_cast<std::uint8_t>(table >> 8),
                                     0xAAu,
                                     0x60u,
                                     0u,
                                     0u,
                                     0u,
                                     0u,
                                     0xFFu};
      _cpu.Load(_rdkey, stub, sizeof(stub));

      std::uint8_t answers[RDKEY_TABLE_SIZE]{};
      for (std::size_t index = 0; index < RDKEY_TABLE_SIZE; ++index)
      {
        const bool pressed = (index < _held) || (index >= _held + _quiet && ((index - _held - _quiet) % 2u) == 0u);
        answers[index] = pressed ? _key : std::uint8_t{0};
      }
      _cpu.Load(table, answers, sizeof(answers));
    }

    /// How many times the scripted `RDKEY` was called. The counter starts at &FF and is INCremented
    /// before each answer, so it holds one less than the number of calls.
    [[nodiscard]] std::uint32_t RdkeyCalls(const Cpu6502& _cpu, std::uint16_t _rdkey)
    {
      return static_cast<std::uint32_t>(_cpu.memory[static_cast<std::uint16_t>(_rdkey + 15u)]) + 1u;
    }

    /// Where the mission routines live, looked up once.
    struct MissionWhere
    {
      std::uint16_t pause, pas1, pause2, bris, mt27, mt28, rdkey, detok, delay, gcnt, xc, yc, ll9, setl1, dovdu19, nosprites;
      std::uint16_t alpha, alp2Next, bet2, bet2Next, typeByte, xsav, inf, kPercent;
      std::uint16_t brief, brief2, brief3, brp, debrief, debrief2, tbrief, bay, yesno, tp, mcnt, slsp;

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

        brief = _oracle.Label("BRIEF");
        brief2 = _oracle.Label("BRIEF2");
        brief3 = _oracle.Label("BRIEF3");
        brp = _oracle.Label("BRP");
        debrief = _oracle.Label("DEBRIEF");
        debrief2 = _oracle.Label("DEBRIEF2");
        tbrief = _oracle.Label("TBRIEF");
        bay = _oracle.Label("BAY");
        yesno = _oracle.Label("YESNO");
        tp = _oracle.Label("TP");
        mcnt = _oracle.Label("MCNT");
        slsp = _oracle.Label("SLSP");
      }
    };

    /// The first byte of the arena that is heap rather than ship block.
    constexpr std::uint16_t HEAP_START = Elite::SHIP_BLOCK_BASE + Elite::MAX_SHIPS * Elite::SHIP_BLOCK_SIZE;
  } // namespace

  namespace
  {
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
    void MirrorMission(const LoopWorld& _world, Cpu6502& _cpu, const Where& _at, const MissionWhere& _to, std::uint8_t _slot)
    {
      for (std::uint16_t address = HEAP_START; address < Elite::LineHeap::TOP; ++address)
      {
        _cpu.memory[address] = _world.heap.Read(address);
      }
      _cpu.memory[_at.lsp] = _world.world.heaps.lsp;

      // 6502: SLSP -- the bottom of the ship line heap, which `NWSHP` allocates downwards from.
      _cpu.memory[_to.slsp] = static_cast<std::uint8_t>(_world.world.bubble.heapBottom & 0xFFu);
      _cpu.memory[static_cast<std::uint16_t>(_to.slsp + 1u)] = static_cast<std::uint8_t>(_world.world.bubble.heapBottom >> 8);

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
    void CompareBlock(const Cpu6502& _cpu, const LoopWorld& _world, const Where& _at, const std::wstring& _where)
    {
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.inwk + byte)], _world.world.work[byte],
                         (_where + L": INWK+" + std::to_wstring(byte)).c_str());
      }
    }

    /// 6502: the ship line heap, which `LL9` writes through `XX19`.
    void CompareHeap(const Cpu6502& _cpu, const LoopWorld& _world, const std::wstring& _where)
    {
      for (std::uint16_t address = HEAP_START; address < Elite::LineHeap::TOP; ++address)
      {
        Assert::AreEqual(_cpu.memory[address], _world.heap.Read(address), (_where + L": heap " + std::to_wstring(address)).c_str());
      }
    }
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
            ScriptRdkey(cpu, to.rdkey, 0u, 0u, 0x27u);
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

      for (const std::uint32_t held : {std::uint32_t{0}, std::uint32_t{1}, std::uint32_t{3}})
      {
        for (const std::uint32_t quiet : {std::uint32_t{1}, std::uint32_t{2}, std::uint32_t{5}})
        {
          const std::uint8_t type = ((held + quiet) & 1u) != 0u ? Elite::SHIP_COBRA_MK3 : Elite::SHIP_ADDER;

          LoopWorld world;
          Seed(world.world, quiet * 17u + held * 5u + type);
          world.world.LendSunHeap(world.heap);
          world.world.trumbles.count = 0u;
          world.world.text.row = 0x17u; // so MT23's row 10 is a change rather than a coincidence
          world.world.text.column = 0x1Du;

          SetUpBriefingShip(world, type, 0x7Fu, 0x7Fu);

          Cpu6502 cpu = oracle.Fresh();
          Trap(cpu, to);
          ScriptRdkey(cpu, to.rdkey, held, quiet, 0x27u);
          FillScreens(cpu, world.world.canvas, at.screen, 0x1Du);
          Mirror(world.world, cpu, at);
          MirrorMission(world, cpu, at, to, 0u);

          Assert::IsTrue(cpu.CallSubroutine(to.pause, 20'000'000).completed, L"PAUSE returned");
          const std::uint32_t theirScans = RdkeyCalls(cpu, to.rdkey);

          ScriptedStart start;
          start.held = held;
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

          const std::wstring where =
            WidenText("PAUSE (" + std::to_string(held) + " held, " + std::to_string(quiet) + " quiet, ship " + std::to_string(type) + ")");

          /*
           * THE SCAN COUNT IS WHAT SEES THE FIRST LOOP. `JSR PAS1 / BNE PAUSE` runs while a key is
           * still down, so a briefing that opens with one held scans `held` extra times before it
           * starts waiting for the next press -- and with `held` at zero the loop is invisible,
           * which is why a sweep that only ever started with an empty keyboard could not tell a
           * port that had dropped it from one that had not.
           */
          Assert::AreEqual<std::uint32_t>(held + quiet + 1u, theirScans, (where + L": the oracle's RDKEY calls").c_str());
          Assert::AreEqual<std::uint32_t>(theirScans, start.scans, (where + L": RDKEY calls").c_str());

          Assert::AreEqual<std::uint32_t>(cpu.memory[to.yc], world.world.text.row, (where + L": YC after MT23").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[to.xc], world.world.text.column, (where + L": XC, which MT23 leaves alone").c_str());

          CompareBlock(cpu, world, at, where);
          CompareState(cpu, world.world, at, where);
          CompareScreens(cpu, at.screen, world.world.canvas, 0x1Du, where);
          CompareHeap(cpu, world, where);
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(9u, compared, L"the whole sweep ran");
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

      for (const std::uint32_t held : {std::uint32_t{0}, std::uint32_t{1}, std::uint32_t{2}})
      {
        for (const std::uint32_t quiet : {std::uint32_t{1}, std::uint32_t{2}, std::uint32_t{5}})
        {
          Cpu6502 cpu = oracle.Fresh();
          ScriptRdkey(cpu, to.rdkey, held, quiet, 0x27u);

          Assert::IsTrue(cpu.CallSubroutine(to.pause2, 200'000).completed, L"PAUSE2 returned");
          const std::uint32_t theirScans = RdkeyCalls(cpu, to.rdkey);

          ScriptedStart start;
          start.held = held;
          start.quiet = quiet;
          start.key = 0x27u;

          LoopWorld world;
          Elite::FlightScreen screen = world.world.Screen();
          Elite::FlightLoop loop = LoopOver(world, screen);
          Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
          Elite::WaitForKeyPress(mission);

          const std::wstring where = WidenText("PAUSE2 (" + std::to_string(held) + " held, " + std::to_string(quiet) + " quiet)");
          Assert::AreEqual<std::uint32_t>(theirScans, start.scans, (where + L": RDKEY calls").c_str());
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(9u, compared, L"the whole sweep ran");
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
     * 6502: MT9 -- LDA #1 / JSR DOXC / JMP TT66, and ONE LOAD DOES BOTH.
     *
     * `DOXC` is `STA XC / RTS` and `STA` does not touch the accumulator, so the 1 that became the
     * text column is still in A when `TT66` reads it as the view. The port had the view and not the
     * column until slice 4d-b, and the reason no test saw it is that the only token which reaches
     * this code -- 216, "INCOMING MESSAGE" -- follows the `{9}` with an `{8}`, which moves the
     * cursor to column 6 and hides the difference. So this runs the code ITSELF, from a cursor
     * nowhere near either answer.
     *
     * Swept over four starting views, because `TT66` draws a different screen for each and the
     * comparison is the whole canvas.
     */
    TEST_METHOD(TheNewViewMatchesMT9)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const MissionWhere to(oracle);

      std::uint32_t compared = 0;

      for (const std::uint8_t view : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{4}, std::uint8_t{64}})
      {
        LoopWorld world;
        Seed(world.world, view * 13u + 7u);
        world.world.LendSunHeap(world.heap);
        world.world.trumbles.count = 0u;
        world.world.view = view;
        world.world.text.column = 0x1Du; // 6502: XC -- neither 1 nor 6, so both answers are visible
        world.world.text.row = 0x11u;

        Cpu6502 cpu = oracle.Fresh();
        Trap(cpu, to);
        FillScreens(cpu, world.world.canvas, at.screen, 0x1Du);
        Mirror(world.world, cpu, at);
        MirrorMission(world, cpu, at, to, 0u);

        // 6502: DETOK2 with A = 9 -- the dispatch, rather than `MT9` on its own, because that is
        // how a token reaches it and the port's half of the split lives in the printer.
        cpu.a = 9u;
        cpu.x = 0u;
        cpu.y = 0u;
        Assert::IsTrue(cpu.CallSubroutine(oracle.Label("DETOK2"), 4'000'000).completed, L"DETOK2 returned");

        ScriptedStart start;
        std::uint8_t galaxy = 0;
        Elite::FlightScreen screen = world.world.Screen();
        Elite::FlightLoop loop = LoopOver(world, screen);
        Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
        Elite::MissionCodes codes{mission, world.world.text, galaxy};
        world.world.codes.to = &codes;

        world.world.extendedPrinter.PrintByte(9u);

        const std::wstring where = WidenText("MT9 (from view " + std::to_string(view) + ")");
        Assert::AreEqual<std::uint32_t>(cpu.memory[to.xc], world.world.text.column, (where + L": XC").c_str());
        Assert::AreEqual<std::uint32_t>(cpu.memory[to.yc], world.world.text.row, (where + L": YC").c_str());
        CompareState(cpu, world.world, at, where);
        CompareScreens(cpu, at.screen, world.world.canvas, 0x1Du, where);
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(4u, compared, L"the whole sweep ran");
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
  };

  /*
   * Slice 4d-c: the seven missions, which are eleven instructions of state each and one that is not.
   *
   * SIX OF THE SEVEN ARE A FLAG AND A TOKEN. `BRIEF2`, `BRIEF3`, `DEBRIEF`, `DEBRIEF2` and the two
   * halves of `TBRIEF` change bits in `TP`, sometimes move money, and tail-call `BRP` -- so what
   * this suite compares is the commander block, byte for byte, and the token number each asked for.
   * `BRIEF` is the seventh and it draws.
   */
  TEST_CLASS(TheMissions)
  {
  public:
    /*
     * 6502: BRIEF2, BRIEF3, DEBRIEF and DEBRIEF2 -- the four that are state and a token.
     *
     * The WHOLE commander block is compared and not just `TP`, because three of the four move
     * something else as well: `DEBRIEF` pays 5,000 credits through `MCASH`, `DEBRIEF2` sets `ENGY`
     * and adds 256 kills to the HIGH byte of the tally. A comparison of `TP` alone would agree with
     * a port that paid nothing.
     *
     * Swept over sixteen starting values of `TP` so the bit arithmetic is exercised from states the
     * game can be in and states it cannot. `BRIEF3`'s `AND #%11110000` is the reason that matters:
     * it clears mission 1's bits as well as setting mission 2's, and a port that only ORed would
     * agree from every state whose low nibble was already zero.
     */
    TEST_METHOD(TheMissionStatesMatchTheOriginals)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const MissionWhere to(oracle);

      struct Case
      {
        const wchar_t* name;
        std::uint16_t entry;
        std::uint8_t token;
        Elite::ForcedKey (*run)(Elite::MissionScreen&, Elite::MissionBay&);
      };

      const Case CASES[] = {
        {L"BRIEF2", to.brief2, Elite::MISSION_2_CONTACT, &Elite::BriefMission2},
        {L"BRIEF3", to.brief3, Elite::MISSION_2_BRIEFING, &Elite::CollectPlans},
        {L"DEBRIEF", to.debrief, Elite::MISSION_1_DEBRIEFING, &Elite::DebriefMission1},
        {L"DEBRIEF2", to.debrief2, Elite::MISSION_2_DEBRIEFING, &Elite::DebriefMission2},
      };

      std::uint32_t compared = 0;

      for (const Case& item : CASES)
      {
        for (std::uint32_t progress = 0; progress < 16u; ++progress)
        {
          LoopWorld world;
          Seed(world.world, progress * 5u + item.entry);
          world.world.commander.At(Elite::Field::MissionProgress) = static_cast<std::uint8_t>(progress * 17u);

          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(to.detok);
          cpu.AddTrap(to.bay);
          for (std::size_t byte = 0; byte < Elite::COMMANDER_BLOCK_SIZE; ++byte)
          {
            cpu.memory[static_cast<std::uint16_t>(to.tp + byte)] = world.world.commander.bytes[byte];
          }

          Assert::IsTrue(cpu.CallSubroutine(item.entry, 200'000).completed, L"the mission returned");

          std::uint32_t detoks = 0;
          std::uint8_t theirToken = 0;
          std::uint32_t bays = 0;
          for (const Cpu6502::TrapHit& hit : cpu.trapHits)
          {
            if (hit.address == to.detok)
            {
              ++detoks;
              theirToken = hit.a;
            }
            else if (hit.address == to.bay)
            {
              ++bays;
            }
          }

          /*
           * THE PORT PRINTS THE TOKEN AND THE ORACLE DOES NOT, and that is deliberate.
           *
           * `DETOK` is trapped on the oracle's side so that the number it was asked for can be read
           * off the trap; the port has no trap and prints for real, into a canvas this test does
           * not look at. Printing changes nothing in the commander block, which is what IS
           * compared -- and letting it run is what makes the control codes inside those tokens
           * (two of the four contain a `{24}`) part of what the sweep exercises rather than
           * something to be routed around.
           */
          ScriptedStart start;
          start.alternate = true;
          start.key = 0x27u;

          std::uint8_t dockedFlag = 0;
          Elite::FlightScreen screen = world.world.Screen();
          Elite::FlightLoop loop = LoopOver(world, screen);
          Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
          Elite::MissionBay bay{world.world.commander, dockedFlag, 0u, 0u, false};
          Elite::MissionCodes codes{mission, world.world.text, world.world.commander.At(Elite::Field::GalaxyNumber)};
          world.world.codes.to = &codes;

          const Elite::ForcedKey key = item.run(mission, bay);

          const std::wstring where = std::wstring(item.name) + L" (TP " + std::to_wstring(progress * 17u) + L")";

          Assert::AreEqual<std::uint32_t>(1u, detoks, (where + L": one DETOK").c_str());
          Assert::AreEqual<std::uint32_t>(1u, bays, (where + L": one BAY").c_str());
          Assert::AreEqual<std::uint32_t>(theirToken, item.token, (where + L": the token").c_str());
          Assert::AreEqual<std::uint32_t>(0xFFu, dockedFlag, (where + L": QQ12 -- BAY sets it to &FF").c_str());
          static_cast<void>(key);

          for (std::size_t byte = 0; byte < Elite::COMMANDER_BLOCK_SIZE; ++byte)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(to.tp + byte)], world.world.commander.bytes[byte],
                             (where + L": TP+" + std::to_wstring(byte)).c_str());
          }
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(4u * 16u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: TBRIEF -- the only mission that asks a question, and the only one that can be refused.
     *
     * BIT 4 IS SET BEFORE THE QUESTION, so declining still marks it as offered and it is never
     * offered again. Both answers are swept, from sixteen starting values of `TP` and two cash
     * positions -- one that can afford 5,000 credits and one that cannot.
     *
     * THE CARRY FROM `LCASH` IS NOT TESTED. `INC TRIBBLE` follows it unconditionally and `LCASH`
     * puts the money back when it cannot afford the spend, so a commander who is short gets the
     * Trumble for nothing. The poor half of this sweep is what says so, and it is ported rather
     * than fixed (ADR-001 §6).
     */
    TEST_METHOD(TheTrumbleOfferMatchesTBRIEF)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const MissionWhere to(oracle);

      std::uint32_t compared = 0;
      std::uint32_t bought = 0;
      std::uint32_t freeTrumbles = 0;

      for (const bool accept : {true, false})
      {
        for (const bool rich : {true, false})
        {
          for (std::uint32_t progress = 0; progress < 16u; ++progress)
          {
            LoopWorld world;
            Seed(world.world, progress * 11u + (accept ? 2u : 0u) + (rich ? 1u : 0u));
            world.world.commander.At(Elite::Field::MissionProgress) = static_cast<std::uint8_t>(progress * 17u);

            /*
             * 6502: CASH -- four bytes, big-endian, in tenths of a credit. 100,000 tenths is
             * 10,000 credits and 100 tenths is ten, so one half of the sweep can afford the
             * Trumble and the other cannot.
             */
            const std::uint32_t cash = rich ? 100000u : 100u;
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              world.world.commander.bytes[static_cast<std::size_t>(Elite::Field::Cash) + byte] =
                static_cast<std::uint8_t>(cash >> (8u * (3u - byte)));
            }
            world.world.commander.At(Elite::Field::Tribbles) = 0u;

            Cpu6502 cpu = oracle.Fresh();
            cpu.AddTrap(to.detok);
            cpu.AddTrap(to.bay);
            cpu.AddTrap(to.yesno, accept ? Cpu6502::TrapExit::SetCarry : Cpu6502::TrapExit::ClearCarry);
            for (std::size_t byte = 0; byte < Elite::COMMANDER_BLOCK_SIZE; ++byte)
            {
              cpu.memory[static_cast<std::uint16_t>(to.tp + byte)] = world.world.commander.bytes[byte];
            }

            Assert::IsTrue(cpu.CallSubroutine(to.tbrief, 200'000).completed, L"TBRIEF returned");

            std::uint32_t detoks = 0;
            std::uint8_t theirToken = 0;
            std::uint32_t bays = 0;
            for (const Cpu6502::TrapHit& hit : cpu.trapHits)
            {
              if (hit.address == to.detok)
              {
                ++detoks;
                theirToken = hit.a;
              }
              else if (hit.address == to.bay)
              {
                ++bays;
              }
            }

            ScriptedStart start;
            start.alternate = true;
            start.key = 0x27u;

            ScriptedKeys keys{accept};
            std::uint8_t dockedFlag = 0;
            Elite::FlightScreen screen = world.world.Screen();
            Elite::FlightLoop loop = LoopOver(world, screen);
            Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
            Elite::MissionBay bay{world.world.commander, dockedFlag, 0u, 0u, false};
            Elite::MissionCodes codes{mission, world.world.text, world.world.commander.At(Elite::Field::GalaxyNumber)};
            world.world.codes.to = &codes;

            static_cast<void>(Elite::OfferTrumble(mission, bay, keys));

            const std::wstring where = WidenText(std::string("TBRIEF (") + (accept ? "yes" : "no") + ", " + (rich ? "rich" : "poor") +
                                                 ", TP " + std::to_string(progress * 17u) + ")");

            Assert::AreEqual<std::uint32_t>(1u, detoks, (where + L": one DETOK").c_str());
            Assert::AreEqual<std::uint32_t>(Elite::TRUMBLE_OFFER, theirToken, (where + L": the offer token").c_str());
            Assert::AreEqual<std::uint32_t>(1u, bays, (where + L": one BAY").c_str());
            Assert::AreEqual<std::uint32_t>(0xFFu, dockedFlag, (where + L": QQ12").c_str());

            for (std::size_t byte = 0; byte < Elite::COMMANDER_BLOCK_SIZE; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(to.tp + byte)], world.world.commander.bytes[byte],
                               (where + L": TP+" + std::to_wstring(byte)).c_str());
            }

            if (accept)
            {
              Assert::AreEqual<std::uint32_t>(1u, world.world.commander.At(Elite::Field::Tribbles), (where + L": one Trumble").c_str());
              bought += rich ? 1u : 0u;
              freeTrumbles += rich ? 0u : 1u;
            }
            else
            {
              Assert::AreEqual<std::uint32_t>(0u, world.world.commander.At(Elite::Field::Tribbles), (where + L": no Trumble").c_str());
            }
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(2u * 2u * 16u, compared, L"the whole sweep ran");
      Assert::AreEqual<std::uint32_t>(16u, bought, L"sixteen Trumbles were paid for");
      Assert::AreEqual<std::uint32_t>(16u, freeTrumbles, L"and sixteen were not, which is the bug");
    }

    /*
     * 6502: BRIEF -- the seventh mission, and the only one that draws.
     *
     * Run to `BRP` on both sides, which is where the original itself splits: `BR2` ends
     * `LDA #10 / BNE BRPS` and everything past the branch is `BRP`'s and is shared with four other
     * missions. The oracle stops there because `BRP` is trapped; the port stops there because
     * `RunConstrictorBriefing` returns the token rather than printing it.
     *
     * WHAT IS COMPARED IS THE WHOLE SCREEN over roughly two hundred frames of `LL9` and `MVEIT`,
     * plus `INWK`, the ship line heap, the bubble, `MCNT` and the commander block. Two hundred
     * frames is not a choice: the first loop is sixty-four and the second runs until `z_lo` has
     * been incremented past 255 twice a frame, so the length is the arithmetic's.
     *
     * THE COUNTER IS LIVE IN ONE LOOP AND DEAD IN THE OTHER. `DEC MCNT` is in both and only the
     * first branches on it, so `MCNT` comes out of the routine at whatever the second loop left --
     * which is why it is compared rather than assumed.
     */
    TEST_METHOD(TheConstrictorBriefingMatchesBRIEF)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const MissionWhere to(oracle);

      std::uint32_t compared = 0;

      for (std::uint32_t progress = 0; progress < 4u; ++progress)
      {
        LoopWorld world;
        Seed(world.world, progress * 23u + 5u);
        world.world.LendSunHeap(world.heap);
        world.world.trumbles.count = 0u;
        world.world.commander.At(Elite::Field::MissionProgress) = static_cast<std::uint8_t>(progress * 85u);

        /*
         * An EMPTY bubble, because `NWSHP` is what puts the Constrictor in it and a briefing runs
         * on the docked game's ship list. `Seed` fills three slots with a fleet, which would send
         * the Constrictor to slot 3 and leave three other ships for `LL9` to trip over.
         */
        for (std::size_t slot = 0; slot < world.world.bubble.slots.size(); ++slot)
        {
          world.world.bubble.slots[slot] = 0u;
        }
        for (std::size_t type = 0; type < world.world.bubble.counts.size(); ++type)
        {
          world.world.bubble.counts[type] = 0u;
        }
        world.world.bubble.junk = 0u;
        world.world.bubble.heapBottom = Elite::SHIP_HEAP_TOP;

        Cpu6502 cpu = oracle.Fresh();
        Trap(cpu, to);
        cpu.AddTrap(to.delay); // 6502: BRIS's `LDY #100 / JMP DELAY`, which is `WSCAN` in a loop
        cpu.AddTrap(to.brp);   // where both sides stop

        FillScreens(cpu, world.world.canvas, at.screen, 0x1Du);
        Mirror(world.world, cpu, at);
        MirrorMission(world, cpu, at, to, 0u);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(to.brief, 200'000'000);
        Assert::IsTrue(run.completed, L"BRIEF reached BRP");

        std::uint8_t theirToken = 0;
        std::uint32_t brps = 0;
        for (const Cpu6502::TrapHit& hit : cpu.trapHits)
        {
          if (hit.address == to.brp)
          {
            ++brps;
            theirToken = hit.a;
          }
        }

        ScriptedStart start;
        start.alternate = true;
        start.key = 0x27u;

        std::uint8_t dockedFlag = 0;
        Elite::FlightScreen screen = world.world.Screen();
        Elite::FlightLoop loop = LoopOver(world, screen);
        Elite::MissionScreen mission{loop, start, world.world.extendedPrinter, world.keys, 0u};
        Elite::MissionBay bay{world.world.commander, dockedFlag, 0u, 0u, false};
        Elite::MissionCodes codes{mission, world.world.text, world.world.commander.At(Elite::Field::GalaxyNumber)};
        world.world.codes.to = &codes;

        const std::uint8_t ourToken = Elite::RunConstrictorBriefing(mission, bay);

        const std::wstring where = WidenText("BRIEF (TP " + std::to_string(progress * 85u) + ")");

        Assert::AreEqual<std::uint32_t>(1u, brps, (where + L": one BRP").c_str());
        Assert::AreEqual(theirToken, ourToken, (where + L": the token BR2 leaves in A").c_str());
        Assert::AreEqual<std::uint32_t>(Elite::MISSION_1_BRIEFING, ourToken, (where + L": which is ten").c_str());

        Assert::AreEqual<std::size_t>(1u, start.delays.size(), (where + L": BRIS delayed once").c_str());
        Assert::AreEqual<std::uint32_t>(Elite::INCOMING_MESSAGE_FRAMES, start.delays[0], (where + L": for a hundred frames").c_str());

        for (std::size_t byte = 0; byte < Elite::COMMANDER_BLOCK_SIZE; ++byte)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(to.tp + byte)], world.world.commander.bytes[byte],
                           (where + L": TP+" + std::to_wstring(byte)).c_str());
        }

        Assert::AreEqual(cpu.memory[to.mcnt], world.world.flight.mainLoopCounter, (where + L": MCNT").c_str());
        Assert::AreEqual(cpu.memory[to.typeByte], world.world.flight.type, (where + L": TYPE").c_str());

        CompareBlock(cpu, world, at, where);
        CompareState(cpu, world.world, at, where);
        CompareScreens(cpu, at.screen, world.world.canvas, 0x1Du, where);
        CompareHeap(cpu, world, where);
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(4u, compared, L"the whole sweep ran");
      Logger::WriteMessage("BRIEF: 4 Constrictor briefings compared on the whole screen\n");
    }

  private:
    /// 6502: TT217 -- the one key `YESNO` reads, scripted. "Y" is 89 and anything else is a no.
    struct ScriptedKeys final : Elite::KeySource
    {
      bool yes = false;
      explicit ScriptedKeys(bool _yes) noexcept
        : yes(_yes)
      {
      }
      std::uint8_t NextKey() override
      {
        return yes ? std::uint8_t{'Y'} : std::uint8_t{'N'};
      }
    };
  };

} // namespace GameLogicTests

#include "pch.h"

#include "Cpu6502.h"
#include "FlightUniverse.h"
#include "OracleImage.h"

#include "Commander.h"
#include "LineHeap.h"
#include "ShipSlot.h"
#include "GameLoop.h"
#include "StartUp.h"

#include <array>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

namespace GameLogicTests
{

  /*
   * Slice 4c-a: main game loop parts 1 to 4, which is everything that arrives in the bubble on its
   * own -- traders, asteroids, canisters, police, bounty hunters, Thargoids and packs of pirates.
   *
   * WHERE THE COMPARISON STARTS AND STOPS is the whole design of this fixture. The spawning is not
   * a subroutine: it begins three bytes past `ytq`, in the middle of part 2 and after the flight
   * loop `TT100` opens with, and it never returns -- every path ends at `MLOOP`, which is the label
   * on part 5. So the oracle is run from `ytq+3` with `MLOOP` as its stop address, and what is
   * compared is the state both sides are left holding.
   *
   * EVERY DECISION IS A `DORND`, so this is exact rather than statistical. Seeding `RAND` on both
   * sides makes the sequence of rolls identical, and a port that took one branch differently ends
   * up with a different generator state even when the bubble happens to look the same -- which is
   * why `RAND` is compared as carefully as the ships are (§6.121).
   */
  /// Flat memory the game never touches, where the patched top of the loop counts its arrivals --
  /// the same address and the same trick `FlightLoopTests` uses for `DEATH`, `DOENTRY` and `ESCAPE`.
  constexpr std::uint16_t TT100_PROBE = 0xFFF1;

  TEST_CLASS(TheMainGameLoop)
  {
    struct Labels
    {
      std::uint16_t entry = 0, mloop = 0, tt100 = 0, gthg = 0, there = 0;
      std::uint16_t frin = 0, kPercent = 0, many = 0, junk = 0, slsp = 0;
      std::uint16_t inwk = 0, rand = 0, xx0 = 0;
      std::uint16_t mj = 0, ev = 0, tp = 0, gov = 0, fist = 0, qq20 = 0;
      std::uint16_t gcnt = 0, qq0 = 0, qq1 = 0;

      explicit Labels(const OracleImage& _oracle)
      {
        // 6502: `.ytq JMP MLOOP` is three bytes, and the `LDA MJ` after it is where the spawning
        // begins. There is no label on it because nothing jumps to it -- part 2 falls in.
        entry = static_cast<std::uint16_t>(_oracle.Label("ytq") + 3u);
        mloop = _oracle.Label("MLOOP");
        // 6502: `.TT100` is the byte after part 1's `JSR NWSHP`, so a trader's pass arrives here
        // rather than at `MLOOP`. The fixture patches it (M6-a-1) and reads which door was used.
        tt100 = _oracle.Label("TT100");
        gthg = _oracle.Label("GTHG");
        there = _oracle.Label("THERE");
        frin = _oracle.Label("FRIN");
        kPercent = _oracle.Label("K%");
        many = _oracle.Label("MANY");
        junk = _oracle.Label("JUNK");
        slsp = _oracle.Label("SLSP");
        inwk = _oracle.Label("INWK");
        rand = _oracle.Label("RAND");
        xx0 = _oracle.Label("XX0");
        mj = _oracle.Label("MJ");
        ev = _oracle.Label("EV");
        tp = _oracle.Label("TP");
        gov = _oracle.Label("gov");
        fist = _oracle.Label("FIST");
        qq20 = _oracle.Label("QQ20");
        gcnt = _oracle.Label("GCNT");
        qq0 = _oracle.Label("QQ0");
        qq1 = _oracle.Label("QQ1");
      }
    };

    /// One bubble into both sides, with each ship's line heap carved off the top the way `NWSHP`
    /// carves it -- otherwise the first spawn's heap check compares against nothing.
    static void SeedBubble(Cpu6502& _cpu, Elite::Bubble& _bubble, Elite::LineHeap& _heap, const Labels& _at,
                           const std::vector<std::uint8_t>& _fleet)
    {
      std::uint16_t heapAt = Elite::SHIP_HEAP_TOP;

      for (std::size_t slot = 0; slot < _fleet.size() && slot < Elite::MAX_SHIPS; ++slot)
      {
        const std::uint8_t type = _fleet[slot];
        _bubble.slots[slot] = type;
        _cpu.memory[static_cast<std::uint16_t>(_at.frin + slot)] = type;

        std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = _bubble.blocks[slot].ToBytes();
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          const std::uint8_t value = static_cast<std::uint8_t>(0x21u + slot * 17u + byte * 5u);
          shipBytes[byte] = value;
          _cpu.memory[static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
        }
        _bubble.blocks[slot] = Elite::Ship::FromBytes(shipBytes);

        const Elite::Blueprint* blueprint = Elite::BlueprintOf(Elite::TypeOf(type));
        const std::uint8_t size = (blueprint == 0u) ? std::uint8_t{0} : blueprint->heapBytes;
        heapAt = static_cast<std::uint16_t>(heapAt - size);

        _bubble.blocks[slot].heap = Elite::HeapOffset::FromAddress(heapAt);
        _cpu.memory[static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + Elite::SHIP_HEAP_LOW_OFFSET)] =
          static_cast<std::uint8_t>(heapAt);
        _cpu.memory[static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + Elite::SHIP_HEAP_HIGH_OFFSET)] =
          static_cast<std::uint8_t>(heapAt >> 8);

        for (std::uint16_t byte = 0; byte < size; ++byte)
        {
          const std::uint8_t value = static_cast<std::uint8_t>(0x40u + slot * 23u + byte);
          _heap.Write(Elite::HeapOffset::FromAddress(static_cast<std::uint16_t>(heapAt + byte)), value);
          _cpu.memory[static_cast<std::uint16_t>(heapAt + byte)] = value;
        }

        if (type != 0u && type < _bubble.counts.size())
        {
          ++_bubble.counts[type];
          ++_cpu.memory[static_cast<std::uint16_t>(_at.many + type)];
        }
      }

      _bubble.heapBottom = Elite::HeapOffset::FromAddress(heapAt);
      _cpu.memory[_at.slsp] = static_cast<std::uint8_t>(heapAt);
      _cpu.memory[static_cast<std::uint16_t>(_at.slsp + 1)] = static_cast<std::uint8_t>(heapAt >> 8);
    }

    static void CompareBubble(const Cpu6502& _cpu, const Elite::Bubble& _bubble, const Elite::LineHeap& _heap, const Labels& _at,
                              const std::wstring& _where)
    {
      for (std::size_t slot = 0; slot < _bubble.slots.size(); ++slot)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.frin + slot)], _bubble.slots[slot],
                         (_where + L": FRIN+" + std::to_wstring(slot)).c_str());
      }
      for (std::size_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
      {
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)],
                           _bubble.blocks[slot].ToBytes()[byte],
                           (_where + L": K%+" + std::to_wstring(slot) + L"." + std::to_wstring(byte)).c_str());
        }
      }
      for (std::size_t type = 0; type < _bubble.counts.size(); ++type)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.many + type)], _bubble.counts[type],
                         (_where + L": MANY+" + std::to_wstring(type)).c_str());
      }
      Assert::AreEqual(_cpu.memory[_at.junk], _bubble.junk, (_where + L": JUNK").c_str());
      Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(_cpu.memory[_at.slsp] | (_cpu.memory[_at.slsp + 1] << 8)),
                                      _bubble.heapBottom.Address(), (_where + L": SLSP").c_str());

      constexpr std::uint16_t LINES_START = static_cast<std::uint16_t>(Elite::SHIP_BLOCK_BASE + Elite::MAX_SHIPS * Elite::SHIP_BLOCK_SIZE);
      for (std::uint16_t address = LINES_START; address < Elite::LineHeap::TOP; ++address)
      {
        Assert::AreEqual(_cpu.memory[address], _heap.Read(Elite::HeapOffset::FromAddress(address)),
                         (_where + L": the heap at " + std::to_wstring(address)).c_str());
      }
    }

  public:
    /*
     * 6502: TT100's head, from three bytes past the label -- after `JSR M%` -- to `ytq`.
     *
     * `ytq` is the `JMP MLOOP` the head takes 255 passes in 256, so stopping there catches BOTH
     * answers: the run reaches it on a skip, and on a spawn the fall-through goes past it into
     * part 2 and the interpreter runs on to the `MLOOP` the spawner ends at. Which one happened is
     * `run.stoppedAt`, and the port's `LoopHead` has to agree with it.
     *
     * `DLY` is swept across 0, 1, 2 and 255 because the countdown's whole shape is that it stops at
     * zero rather than wrapping, and only a 1 reaches `me2`. `MCNT` across 1 and 2 because one of
     * them is the pass in 256 that spawns.
     */
    TEST_METHOD(TheLoopHeadMatchesTT100)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Where where(oracle);
      const std::uint16_t head = static_cast<std::uint16_t>(oracle.Label("TT100") + 3u);
      const std::uint16_t ytq = oracle.Label("ytq");
      const std::uint16_t mloop = oracle.Label("MLOOP");

      std::uint32_t compared = 0;
      std::set<std::string> outcomes;

      for (const std::uint8_t delay : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{255}})
      {
        for (const std::uint8_t counter : {std::uint8_t{1}, std::uint8_t{2}})
        {
          for (const std::uint8_t view : {std::uint8_t{0}, std::uint8_t{1}})
          {
            Cpu6502 cpu = oracle.Fresh();
            // `CLYNS` is not trapped since M3-b-3b -- both machines run it. `WSCAN` and `DELAY`
            // still are and always will be: both wait for a raster line a flat-memory interpreter
            // never reaches, and both are the platform's (ADR-005 section 3; M6-0-e).
            for (const char* seam : {"WSCAN", "DELAY"})
            {
              std::uint16_t address = 0;
              if (oracle.TryLabel(seam, address))
              {
                cpu.AddTrap(address);
              }
            }

            LoopUniverse universe;
            Seed(universe.universe, 4u);
            universe.universe.message.delay = delay;
            universe.universe.message.token = 200u; // 6502: MCH -- the message `me2` re-sends to erase it
            universe.universe.flight.mainLoopCounter = counter;
            universe.universe.view = view;

            Mirror(universe.universe, cpu, where);

            // Stopped at `ytq` -- the skip's own `JMP` -- so a spawn is the run that goes PAST it.
            const Elite::Testing::RunResult run = cpu.CallSubroutine(head, 4'000'000, ytq);

            Elite::Ports ports = universe.Ports();

            // 6502: JSR CLYNS -- a seam counted here until M3-b-3b, and `ClearMessageRows` on both
            // sides now, so what it did is in `CompareState` below with the rest of the universe.
            const Elite::LoopHead decided = Elite::RunLoopHead(universe.universe, ports);

            const std::wstring context =
              WidenText("TT100 dly " + std::to_string(delay) + " mcnt " + std::to_string(counter) + " view " + std::to_string(view));
            Assert::IsTrue(run.completed, (context + L": the head reached MLOOP").c_str());

            if (decided == Elite::LoopHead::Spawn)
            {
              // 6502: the fall-through into part 2's `LDA MJ`, which is slice 4c-a. The port has to
              // run it too or the comparison is against a machine that did more work.
              //
              // AND IT HAS TO END AT `MLOOP`. Part 1's tail falls back into `.TT100` and another
              // `JSR M%` (M6-a-1), which this fixture's oracle would run and its port could not --
              // so the assertion says that this seed rolls no trader rather than leaving it to luck.
              Assert::IsTrue(Elite::RunSpawning(universe.universe, false) == Elite::SpawnOutcome::Ended,
                             (context + L": the spawning pass ended at MLOOP").c_str());
            }

            CompareState(cpu, universe.universe, where, context);
            static_cast<void>(ytq);

            outcomes.insert(std::to_string(decided == Elite::LoopHead::Spawn ? 1 : 0) + "/" +
                            std::to_string(universe.universe.message.delay) + "/" + std::to_string(universe.universe.text.row));
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(4u * 2u * 2u, compared, L"the whole sweep ran");
      Assert::IsTrue(outcomes.size() >= 5u, L"and the skip, the spawn, the expiry and the clear were all reached");
    }

    /*
     * 6502: MLOOP -- part 5, run from its second instruction to `TT17`.
     *
     * NOT FROM THE FIRST, because the first two are `LDX #&FF / TXS`: the stack reset that six
     * `JMP`s into this label make necessary and a port whose calls are calls does not need. Running
     * them under `CallSubroutine` would throw away the return address the interpreter pushed.
     *
     * The sweep is over what the routine branches on and nothing else -- the two countdowns, the
     * view, the author-names option, the Trumble population and the cabin temperature -- and the
     * whole universe is compared, because `DIALS` draws. What it exists to catch is the shape §6.138
     * found: three player-visible behaviours that were transcribed into the executable by hand, two
     * of which were never actually there.
     */
    TEST_METHOD(TheLoopTailMatchesMLOOP)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Where where(oracle);
      const std::uint16_t mloop = static_cast<std::uint16_t>(oracle.Label("MLOOP") + 3u);
      const std::uint16_t tt17 = oracle.Label("TT17");
      const std::uint16_t patg = oracle.Label("PATG");
      const std::uint16_t tribble = oracle.Label("TRIBBLE");

      struct Case
      {
        std::uint8_t laser, count, view, authors, tribbleLow, tribbleHigh, cabin;
      };

      const Case CASES[] = {
        {0u, 0u, 0u, 0u, 0u, 0u, 30u},      // nothing to do at all
        {1u, 1u, 0u, 0u, 0u, 0u, 30u},      // both countdowns land exactly on zero
        {40u, 2u, 0u, 0u, 0u, 0u, 30u},     // an even countdown steps through zero
        {40u, 5u, 0u, 0u, 0u, 0u, 30u},     // an odd one stops on it
        {40u, 5u, 1u, 0u, 0u, 0u, 30u},     // a docked screen: no dials, and the delay is asked for
        {40u, 5u, 1u, 0xFFu, 0u, 0u, 30u},  // the option on, so no delay
        {40u, 5u, 128u, 0u, 0u, 0u, 30u},   // the short-range chart
        {0u, 0u, 0u, 0u, 200u, 1u, 30u},    // Trumbles that can breed and squeak
        {0u, 0u, 0u, 0u, 255u, 1u, 30u},    // and a low byte about to wrap
        {0u, 0u, 0u, 0u, 200u, 127u, 30u},  // the high byte at its clamp
        {0u, 0u, 0u, 0u, 200u, 20u, 240u},  // a hot cabin: they burn instead of squeaking
        {0u, 0u, 1u, 0u, 100u, 8u, 224u},   // exactly on the temperature the routine reads twice
        {0u, 0u, 0u, 0u, 200u, 127u, 240u}, // a hot cabin and the most Trumbles the byte can hold,
                                            // which is what makes the BURNING sound reachable at all
      };

      std::uint32_t compared = 0;
      std::set<std::string> outcomes;

      for (const Case& one : CASES)
      {
        for (const std::array<std::uint8_t, 4> seed :
             {std::array<std::uint8_t, 4>{0x21u, 0x84u, 0x5Fu, 0xC0u}, std::array<std::uint8_t, 4>{0xDDu, 0x0Eu, 0xB7u, 0x39u}})
        {
          for (const std::uint8_t carryIn : {std::uint8_t{0}, std::uint8_t{1}})
          {
            Cpu6502 cpu = oracle.Fresh();
            for (const char* seam : {"MESS", "WSCAN", "DELAY"})
            {
              std::uint16_t address = 0;
              if (oracle.TryLabel(seam, address))
              {
                cpu.AddTrap(address);
              }
            }

            LoopUniverse universe;
            Seed(universe.universe, 3u);
            universe.universe.status.laserTemperature = one.laser;
            universe.universe.status.laserCount = one.count;
            universe.universe.status.cabinTemperature = one.cabin;
            universe.universe.view = one.view;
            universe.universe.commander.tribbles.lo = one.tribbleLow;
            universe.universe.commander.tribbles.hi = one.tribbleHigh;

            Mirror(universe.universe, cpu, where);
            cpu.memory[patg] = one.authors;
            cpu.memory[tribble] = one.tribbleLow;
            cpu.memory[static_cast<std::uint16_t>(tribble + 1u)] = one.tribbleHigh;
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              cpu.memory[static_cast<std::uint16_t>(where.rand + byte)] = seed[byte];
            }
            cpu.c = carryIn != 0u;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(mloop, 4'000'000, tt17);
            Assert::IsTrue(run.completed, L"MLOOP reached TT17");

            Elite::Ports ports = universe.Ports();
            universe.universe.rng.SetState(seed);

            const std::uint8_t frames =
              Elite::RunLoopTail(universe.universe, ports, universe.universe.commander, one.authors, carryIn != 0u);

            const std::wstring context =
              WidenText("MLOOP laser " + std::to_string(one.laser) + " lasct " + std::to_string(one.count) + " view " +
                        std::to_string(one.view) + " trib " + std::to_string(one.tribbleHigh) + " cab " + std::to_string(one.cabin) +
                        " seed " + std::to_string(seed[0]) + " carry " + std::to_string(carryIn));

            CompareState(cpu, universe.universe, where, context);
            Assert::AreEqual(cpu.memory[tribble], universe.universe.commander.tribbles.lo, (context + L": TRIBBLE").c_str());
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(tribble + 1u)],
                             universe.universe.commander.tribbles.hi,
                             (context + L": TRIBBLE+1").c_str());
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(where.rand + byte)], universe.universe.rng.State()[byte],
                               (context + L": RAND+" + std::to_wstring(byte)).c_str());
            }

            /*
             * The buffer rather than a list of calls (M3-b-2a): `SOFLG` says which voices are busy
             * and `SOSUS` what the Trumble squeak's sustain was, which is the byte that used to be
             * recorded. `CompareState` above has already checked both against the shipped game;
             * this is the coverage tally that says the sweep reached different answers.
             */
            std::string voices;
            for (std::size_t voice = 0; voice < Elite::SID_VOICE_COUNT; ++voice)
            {
              voices += "." + std::to_string(universe.universe.sound.flag[voice]) + ":" +
                        std::to_string(universe.universe.sound.sustain[voice]);
            }
            outcomes.insert(std::to_string(frames) + "/" + std::to_string(universe.universe.status.laserCount) + "/" + voices);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(13u * 2u * 2u, compared, L"the whole sweep ran");
      std::string reached;
      for (const std::string& one : outcomes)
      {
        reached += one + " ";
      }
      Logger::WriteMessage(("MLOOP: " + std::to_string(compared) + " cases, outcomes (frames/LASCT/sounds/sustain) " + reached).c_str());
      Assert::IsTrue(outcomes.size() >= 6u, L"and the countdowns, the delay, the squeak AND the burn were reached");
    }
    /*
     * 6502: THERE -- galaxy 2 at (144, 33), and the answer comes back in the CARRY.
     *
     * Swept over every galaxy and a grid of coordinates that includes the exact one, because the
     * routine's shape -- `BEQ THEX+1` landing on the `RTS` past the `CLC` -- means the true answer
     * is a flag the compare happened to leave rather than a value the routine computed.
     */
    TEST_METHOD(TheConstrictorSystemMatchesTHERE)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);

      std::uint32_t compared = 0;
      std::uint32_t agreed = 0;

      for (std::uint8_t galaxy = 0; galaxy < 8u; ++galaxy)
      {
        for (const std::uint8_t x : {std::uint8_t{0}, std::uint8_t{143}, std::uint8_t{144}, std::uint8_t{145}, std::uint8_t{255}})
        {
          for (const std::uint8_t y : {std::uint8_t{0}, std::uint8_t{32}, std::uint8_t{33}, std::uint8_t{34}, std::uint8_t{255}})
          {
            Cpu6502 cpu = oracle.Fresh();
            cpu.memory[at.gcnt] = galaxy;
            cpu.memory[at.qq0] = x;
            cpu.memory[at.qq1] = y;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(at.there);
            Assert::IsTrue(run.completed, L"THERE returned");

            Elite::Commander commander{};
            commander.galaxyNumber = galaxy;
            commander.systemX = x;
            commander.systemY = y;

            const bool ours = Elite::AtConstrictorSystem(commander);
            const std::wstring where = L"THERE(" + std::to_wstring(galaxy) + L"," + std::to_wstring(x) + L"," + std::to_wstring(y) + L")";
            Assert::AreEqual(cpu.c, ours, where.c_str());

            if (ours)
            {
              ++agreed;
            }
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(8u * 5u * 5u, compared, L"the whole sweep ran");
      Assert::AreEqual<std::uint32_t>(1u, agreed, L"and exactly one system in it is the Constrictor's");
    }

    /*
     * 6502: GTHG -- the Thargoid and its Thargon, which is the only pair the game spawns together.
     *
     * Swept over bubbles with room for both, room for one, and room for neither, because the
     * routine's `JMP NWSHP` means it reports the THARGON's answer: a bubble that takes the
     * mothership and refuses the escort comes back saying it failed, and one that is full comes
     * back the same way. Only the bubble can tell those apart, so the bubble is what is compared.
     */
    TEST_METHOD(TheThargoidPairMatchesGTHG)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);

      const std::vector<std::uint8_t> FLEETS[] = {
        {},
        {128u, 129u},
        {128u, 129u, 11u, 11u, 11u},
        {128u, 129u, 11u, 11u, 11u, 11u, 11u, 11u, 11u},      // one slot left: the escort is refused
        {128u, 129u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u}, // no slot at all
        {128u, 129u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u}, // Anacondas: the heap runs out first
      };

      std::uint32_t compared = 0;
      std::set<std::string> outcomes;

      for (const std::vector<std::uint8_t>& fleet : FLEETS)
      {
        for (const std::array<std::uint8_t, 4> seed :
             {std::array<std::uint8_t, 4>{0x11u, 0x22u, 0x33u, 0x44u}, std::array<std::uint8_t, 4>{0xC7u, 0x05u, 0x9Au, 0x2Eu}})
        {
          for (const std::uint8_t carryIn : {std::uint8_t{0}, std::uint8_t{1}})
          {
            Cpu6502 cpu = oracle.Fresh();

            Elite::Bubble bubble;
            Elite::LineHeap heap;
            SeedBubble(cpu, bubble, heap, at, fleet);

            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              cpu.memory[static_cast<std::uint16_t>(at.rand + byte)] = seed[byte];
            }
            Elite::Rng rng;
            rng.SetState(seed);

            Elite::Ship work{};
            std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              const std::uint8_t value = static_cast<std::uint8_t>(0x55u + byte * 3u);
              shipBytes[byte] = value;
              cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)] = value;
            }
            work = Elite::Ship::FromBytes(shipBytes);

            const Elite::Blueprint* blueprint =
              Elite::BlueprintOf(Elite::ShipType::CobraMk3); // a real XX0, so the routine can hand it back
            cpu.memory[at.xx0] = static_cast<std::uint8_t>(blueprint->address & 0xFFu);
            cpu.memory[static_cast<std::uint16_t>(at.xx0 + 1u)] = static_cast<std::uint8_t>(blueprint->address >> 8);

            cpu.c = carryIn != 0u;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(at.gthg, 200'000);
            Assert::IsTrue(run.completed, L"GTHG returned");

            const Elite::NewShip made = Elite::SpawnThargoidPair(bubble, work, rng, blueprint, carryIn != 0u);

            const std::wstring where = WidenText("GTHG: " + std::to_string(fleet.size()) + " ships, seed " + std::to_string(seed[0]) +
                                                 ", carry " + std::to_string(carryIn));

            CompareBubble(cpu, bubble, heap, at, where);
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)], work.ToBytes()[byte],
                               (where + L": INWK+" + std::to_wstring(byte)).c_str());
            }
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.rand + byte)], rng.State()[byte],
                               (where + L": RAND+" + std::to_wstring(byte)).c_str());
            }

            // The carry `GTHG` exits with, which is the THARGON's and not the Thargoid's.
            Assert::AreEqual(cpu.c, made.created, (where + L": the exit carry").c_str());

            outcomes.insert(std::to_string(bubble.Count(Elite::ShipType::Thargoid)) + "/" +
                            std::to_string(bubble.Count(Elite::ShipType::Thargon)) + "/" + std::to_string(made.created ? 1 : 0));
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(6u * 2u * 2u, compared, L"the whole sweep ran");
      /*
       * Three answers, and the middle one is the point: a bubble with one slot free takes the
       * mothership, refuses the Thargon, and `GTHG` reports FAILURE because the answer it hands
       * back is the second `NWSHP`'s. Observed rather than asserted from the source.
       */
      Assert::IsTrue(outcomes.size() >= 3u, L"and both, one and neither of the pair were reached");
    }

    /*
     * Main game loop parts 1 to 4, from `ytq+3` to `MLOOP`, against the shipped routine.
     *
     * The state swept is what the four parts branch on and nothing else: the generator, because
     * every decision is a `DORND`; `MJ`, which stops the whole thing; the junk count and the
     * station, which are part 2's and part 3's gates; the government, which is part 4's; the
     * contraband and the legal status, which set the threshold a policeman is rolled against; the
     * mission byte, which is the Thargoid's and the Constrictor's; and `EV`, the rate limiter that
     * decides whether part 4 runs at all.
     *
     * WHAT IS COMPARED is the whole bubble -- the slot list, all ten blocks, the type counts, the
     * junk count, `SLSP` and the line heap -- plus `INWK`, `EV`, `XX0` and the generator's four
     * bytes, which is slice 4a-b's standard (§6.121). A spawn that landed in the right slot with
     * the wrong block, or the right block from the wrong number of rolls, fails on one of those.
     */
    TEST_METHOD(TheSpawningMatchesTheMainGameLoop)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);

      struct Situation
      {
        const char* what;
        std::vector<std::uint8_t> fleet;
        std::uint8_t junk, government, mission, legal, contraband, encounters, witchspace;
        std::uint8_t galaxy, systemX, systemY;
      };

      const Situation SITUATIONS[] = {
        {"empty and lawless", {}, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {"empty, corporate state", {}, 0u, 7u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {"empty, feudal", {}, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {"a station in range", {128u, 129u, 2u}, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {"junk everywhere", {128u, 129u, 5u, 5u, 5u}, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {"one canister", {128u, 129u, 5u}, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {"a Viper already here", {128u, 129u, 16u}, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {"carrying slaves", {128u, 129u}, 0u, 0u, 0u, 0u, 40u, 0u, 0u, 0u, 0u, 0u},
        {"an offender carrying slaves", {128u, 129u}, 0u, 0u, 0u, 30u, 40u, 0u, 0u, 0u, 0u, 0u},
        {"a fugitive", {128u, 129u}, 0u, 2u, 0u, 200u, 0u, 0u, 0u, 0u, 0u, 0u},
        {"in witchspace", {128u, 129u}, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u},
        {"mission 1 hunting", {128u, 129u}, 0u, 0u, 0x08u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {"mission 1 at stage 1", {128u, 129u}, 0u, 0u, 0x01u, 0u, 0u, 0u, 0u, 1u, 144u, 33u},
        {"at the Constrictor, no mission", {128u, 129u}, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 144u, 33u},
        {"at the Constrictor, already found", {128u, 129u, 31u}, 0u, 0u, 0x01u, 0u, 0u, 0u, 0u, 1u, 144u, 33u},
        {"the encounter counter spent", {128u, 129u}, 0u, 0u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 0u},
        {"a busy bubble", {128u, 129u, 11u, 17u, 5u, 7u}, 2u, 1u, 0u, 10u, 5u, 0u, 0u, 0u, 0u, 0u},
      };

      /*
       * Six generator states, because one seed exercises one path through a routine that is all
       * rolls -- the lesson of section 6.124.
       *
       * THE LAST TWO ARE CHOSEN AND NOT ARBITRARY (M6-a-1). Reaching part 1 at all takes three
       * rolls in a row to agree: the first under 35, then a `DORND` for the position, then a
       * `DORND` whose OVERFLOW flag is set -- and of the four seeds above, three stop at the 35
       * and the fourth rolls an asteroid, so `MTT4` was reached by nothing in the suite and the
       * coverage review said so. These two reach it and then part from each other at `BMI nodo`:
       * `0x61` spawns an Anaconda minding its own business, with `NEWB` bit 4 set and an AI byte
       * of 246, and `0x53` a Cobra Mk III with neither, whose type is 11 or 13 depending on the
       * carry the fixture came in with.
       */
      const std::array<std::uint8_t, 4> SEEDS[] = {
        {0x00u, 0x00u, 0x00u, 0x00u},
        {0x9Cu, 0x17u, 0x4Bu, 0xE3u},
        {0xFFu, 0xFEu, 0x01u, 0x80u},
        {0x2Au, 0x7Fu, 0xC3u, 0x11u},
        {0x61u, 0x51u, 0x64u, 0xC6u},
        {0x53u, 0x61u, 0x56u, 0xBCu},
      };

      std::uint32_t compared = 0;
      std::uint32_t restarted = 0;
      std::set<std::string> outcomes;

      for (const Situation& one : SITUATIONS)
      {
        for (const std::array<std::uint8_t, 4>& seed : SEEDS)
        {
          for (const std::uint8_t carryIn : {std::uint8_t{0}, std::uint8_t{1}})
          {
            Cpu6502 cpu = oracle.Fresh();

            // 6502: every byte `MLOOP`'s spawner reads is the universe's, and it takes the
            // universe since M5-a-3 -- so the fixture keeps them there and names the pieces it
            // seeds, rather than building nine separate objects the routine was handed one by one.
            Elite::Universe spawning;
            Elite::Bubble& bubble = spawning.bubble;
            Elite::LineHeap heap;
            SeedBubble(cpu, bubble, heap, at, one.fleet);

            bubble.junk = one.junk;
            cpu.memory[at.junk] = one.junk;

            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              cpu.memory[static_cast<std::uint16_t>(at.rand + byte)] = seed[byte];
            }
            Elite::Rng& rng = spawning.rng;
            rng.SetState(seed);

            cpu.memory[at.mj] = one.witchspace;
            cpu.memory[at.ev] = one.encounters;
            cpu.memory[at.gov] = one.government;
            cpu.memory[at.tp] = one.mission;
            cpu.memory[at.fist] = one.legal;
            cpu.memory[at.gcnt] = one.galaxy;
            cpu.memory[at.qq0] = one.systemX;
            cpu.memory[at.qq1] = one.systemY;

            // 6502: QQ20+3, +6 and +10 -- slaves, narcotics and firearms, which are the three
            // slots `BAD` reads and the only cargo that matters here.
            cpu.memory[static_cast<std::uint16_t>(at.qq20 + 3u)] = one.contraband;
            cpu.memory[static_cast<std::uint16_t>(at.qq20 + 6u)] = static_cast<std::uint8_t>(one.contraband / 2u);
            cpu.memory[static_cast<std::uint16_t>(at.qq20 + 10u)] = static_cast<std::uint8_t>(one.contraband / 4u);

            Elite::Ship& work = spawning.work;
            std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              const std::uint8_t value = static_cast<std::uint8_t>(0x13u + byte * 7u);
              shipBytes[byte] = value;
              cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)] = value;
            }
            work = Elite::Ship::FromBytes(shipBytes);

            spawning.flight.blueprint =
              Elite::BlueprintOf(Elite::ShipType::CobraMk3); // a real XX0, so the routine can hand it back
            // 6502: XX0 -- the spawner WRITES it, so this is a reference and not a snapshot: the
            // comparison below reads what the routine left, as it did when the pointer was passed
            // as `const Blueprint*&`.
            const Elite::Blueprint*& blueprint = spawning.flight.blueprint;
            cpu.memory[at.xx0] = static_cast<std::uint8_t>(blueprint->address & 0xFFu);
            cpu.memory[static_cast<std::uint16_t>(at.xx0 + 1u)] = static_cast<std::uint8_t>(blueprint->address >> 8);

            cpu.c = carryIn != 0u;

            /*
             * 6502: `.MTT4 ... JSR NWSHP` and then `.TT100` -- the trader's pass goes back to the
             * top of the loop, not on to part 3 (M6-a-1).
             *
             * `TT100` is `JSR M%`, a whole flight frame, and this fixture has no frame to run: the
             * top of the loop is patched with a counter and a jump to the stop address, so the run
             * ends where the game's pass would have left parts 1 to 4 and the probe says by which
             * door. The port's `SpawnOutcome` is compared against it below.
             */
            cpu.memory[TT100_PROBE] = 0u;
            const std::uint8_t backToTheTop[] = {0xEEu,
                                                 static_cast<std::uint8_t>(TT100_PROBE & 0xFFu),
                                                 static_cast<std::uint8_t>(TT100_PROBE >> 8),
                                                 0x4Cu,
                                                 static_cast<std::uint8_t>(at.mloop & 0xFFu),
                                                 static_cast<std::uint8_t>(at.mloop >> 8)};
            cpu.Load(at.tt100, backToTheTop, sizeof(backToTheTop));

            const Elite::Testing::RunResult run = cpu.CallSubroutine(at.entry, 400'000, at.mloop);
            Assert::IsTrue(run.completed, L"the spawner reached MLOOP");

            Elite::Commander& commander = spawning.commander;
            commander.galaxyNumber = one.galaxy;
            commander.systemX = one.systemX;
            commander.systemY = one.systemY;
            commander.missionProgress = one.mission;
            commander.legalStatus = one.legal;
            commander.cargoHold[3] = one.contraband;
            commander.cargoHold[6] =
              static_cast<std::uint8_t>(one.contraband / 2u);
            commander.cargoHold[10] =
              static_cast<std::uint8_t>(one.contraband / 4u);

            Elite::CurrentSystem& current = spawning.current;
            current.government = one.government;

            Elite::FlightStatus& status = spawning.status;
            status.midJump = one.witchspace;

            std::uint8_t& encounters = spawning.explosions;
            encounters = one.encounters;

            const Elite::SpawnOutcome outcome = Elite::RunSpawning(spawning, carryIn != 0u);

            const std::wstring where =
              WidenText(std::string("spawn: ") + one.what + " seed " + std::to_string(seed[0]) + " carry " + std::to_string(carryIn));

            Assert::AreEqual<std::uint32_t>(cpu.memory[TT100_PROBE], outcome == Elite::SpawnOutcome::Restarted ? 1u : 0u,
                                            (where + L": whether the pass fell back into TT100").c_str());
            restarted += (outcome == Elite::SpawnOutcome::Restarted) ? 1u : 0u;

            CompareBubble(cpu, bubble, heap, at, where);

            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)], work.ToBytes()[byte],
                               (where + L": INWK+" + std::to_wstring(byte)).c_str());
            }
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.rand + byte)], rng.State()[byte],
                               (where + L": RAND+" + std::to_wstring(byte)).c_str());
            }
            Assert::AreEqual(cpu.memory[at.ev], encounters, (where + L": EV").c_str());
            Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(cpu.memory[at.xx0] | (cpu.memory[at.xx0 + 1] << 8)),
                                            blueprint->address, (where + L": XX0").c_str());

            // What arrived, measured from the bubble rather than from the inputs -- section 6.132's
            // counter, so a sweep that only ever spawns nothing cannot look healthy.
            std::string arrived;
            for (std::size_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
            {
              arrived += std::to_string(bubble.slots[slot]) + ",";
            }
            outcomes.insert(arrived);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(17u * 6u * 2u, compared, L"the whole sweep ran");
      Assert::IsTrue(outcomes.size() >= 10u, L"and it reached at least ten different bubbles");
      Assert::IsTrue(restarted > 0u, L"and `MTT4` was reached, which nothing in the suite did before M6-a-1");
      Logger::WriteMessage(("spawner: " + std::to_string(compared) + " cases, " + std::to_string(outcomes.size()) +
                            " distinct bubbles, " + std::to_string(restarted) + " traders back to TT100")
                             .c_str());
    }
  };

} // namespace GameLogicTests

#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Canvas.h"
#include "PlanetDraw.h"
#include "Stardust.h"
#include "ShipBlueprint.h"
#include "ShipSlot.h"
#include "Spawn.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Taking a ship out of the bubble, and putting a system's own two in (slice 3c).
 *
 * `NWSHP` only had to move a pointer. `KILLSHP` has to RELOCATE: every ship above the dead one
 * moves down a slot and its line heap moves down by the dead ship's size, so the region stays
 * packed. Three counts and every locked missile have to be renumbered as the slots shift, and a
 * test that compares only the slot list would miss all of it.
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

    /// The four things `KILLSHP` and `SOLAR` still reach outside this slice, recorded rather than
    /// run. `SCAN` was a fifth until slice 3d-a built it (§6.61).
    class RecordingEffects : public Elite::SpawnEffects
    {
    public:
      std::vector<std::uint8_t> aborts;
      std::vector<std::uint8_t> messages;
      std::uint32_t stationBlobs = 0;
      std::uint32_t missileBlobs = 0;

      void AbortMissile(std::uint8_t _colour) override
      {
        aborts.push_back(_colour);
      }
      void ShowMessage(std::uint8_t _token) override
      {
        messages.push_back(_token);
      }
      void ToggleStationIndicator() override
      {
        ++stationBlobs;
      }
      void ResetMissileIndicators() override
      {
        ++missileBlobs;
      }
    };

    struct SpawnLabels
    {
      std::uint16_t frin = 0, kPercent = 0, many = 0, junk = 0, slsp = 0, mstg = 0, sspr = 0;
      std::uint16_t tp = 0, tally = 0, fist = 0, tribble = 0, qq20 = 0, tek = 0, qq15 = 0;
      std::uint16_t inwk = 0, rand = 0, lso = 0, xx4 = 0, inf = 0, xx0 = 0;

      explicit SpawnLabels(const OracleImage& _oracle)
      {
        frin = _oracle.Label("FRIN");
        kPercent = _oracle.Label("K%");
        many = _oracle.Label("MANY");
        junk = _oracle.Label("JUNK");
        slsp = _oracle.Label("SLSP");
        mstg = _oracle.Label("MSTG");
        sspr = _oracle.Label("SSPR");
        tp = _oracle.Label("TP");
        tally = _oracle.Label("TALLY");
        fist = _oracle.Label("FIST");
        tribble = _oracle.Label("TRIBBLE");
        qq20 = _oracle.Label("QQ20");
        tek = _oracle.Label("tek");
        qq15 = _oracle.Label("QQ15");
        inwk = _oracle.Label("INWK");
        rand = _oracle.Label("RAND");
        lso = _oracle.Label("LSO");
        xx4 = _oracle.Label("XX4");
        inf = _oracle.Label("INF");
        xx0 = _oracle.Label("XX0");
      }
    };

    /// One bubble, put into both sides identically: the slot list, the blocks, the counts and the
    /// line heap the blocks point into.
    void SeedBubble(Cpu6502& _cpu, Elite::Bubble& _bubble, Elite::LineHeap& _heap, const SpawnLabels& _at,
                    const std::vector<std::uint8_t>& _fleet)
    {
      std::uint16_t heapAt = Elite::SHIP_HEAP_TOP;

      for (std::size_t slot = 0; slot < _fleet.size() && slot < Elite::MAX_SHIPS; ++slot)
      {
        const std::uint8_t type = _fleet[slot];
        _bubble.slots[slot] = type;
        _cpu.memory[static_cast<std::uint16_t>(_at.frin + slot)] = type;

        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          const std::uint8_t value = static_cast<std::uint8_t>(0x21u + slot * 17u + byte * 5u);
          _bubble.blocks[slot][byte] = value;
          _cpu.memory[static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
        }

        // Each ship's heap block, carved off the top the way `NWSHP` carves it.
        const std::uint16_t blueprint = Elite::BlueprintAddress(type);
        const std::uint8_t size = (blueprint == 0u) ? std::uint8_t{0} : Elite::ShipByte(static_cast<std::uint16_t>(blueprint + 5u));
        heapAt = static_cast<std::uint16_t>(heapAt - size);

        _bubble.blocks[slot][Elite::SHIP_HEAP_LOW_OFFSET] = static_cast<std::uint8_t>(heapAt);
        _bubble.blocks[slot][Elite::SHIP_HEAP_HIGH_OFFSET] = static_cast<std::uint8_t>(heapAt >> 8);
        _cpu.memory[static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + Elite::SHIP_HEAP_LOW_OFFSET)] =
          static_cast<std::uint8_t>(heapAt);
        _cpu.memory[static_cast<std::uint16_t>(_at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + Elite::SHIP_HEAP_HIGH_OFFSET)] =
          static_cast<std::uint8_t>(heapAt >> 8);

        for (std::uint16_t byte = 0; byte < size; ++byte)
        {
          const std::uint8_t value = static_cast<std::uint8_t>(0x40u + slot * 23u + byte);
          _heap.Write(static_cast<std::uint16_t>(heapAt + byte), value);
          _cpu.memory[static_cast<std::uint16_t>(heapAt + byte)] = value;
        }
      }

      _bubble.heapBottom = heapAt;
      _cpu.memory[_at.slsp] = static_cast<std::uint8_t>(heapAt);
      _cpu.memory[static_cast<std::uint16_t>(_at.slsp + 1)] = static_cast<std::uint8_t>(heapAt >> 8);
    }

    void CompareBubble(const Cpu6502& _cpu, const Elite::Bubble& _bubble, const Elite::LineHeap& _heap, const SpawnLabels& _at,
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
                           _bubble.blocks[slot][byte], (_where + L": K%+" + std::to_wstring(slot) + L"." + std::to_wstring(byte)).c_str());
        }
      }
      for (std::size_t type = 0; type < _bubble.counts.size(); ++type)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_at.many + type)], _bubble.counts[type],
                         (_where + L": MANY+" + std::to_wstring(type)).c_str());
      }

      Assert::AreEqual(_cpu.memory[_at.junk], _bubble.junk, (_where + L": JUNK").c_str());
      Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(_cpu.memory[_at.slsp] | (_cpu.memory[_at.slsp + 1] << 8)),
                                      _bubble.heapBottom, (_where + L": SLSP").c_str());

      /*
       * The line heap itself, which is the half a slot-list comparison cannot see -- and only the
       * part of the arena above the ship blocks.
       *
       * `LineHeap` spans `K%` to `LS%` because `XX19` is an absolute pointer into that region, but
       * the bottom of it is where `K%`'s ten data blocks live, and the port keeps those in `Bubble`
       * rather than in the arena. Comparing from `LineHeap::BASE` compares the blocks against a
       * copy the port never writes.
       */
      constexpr std::uint16_t LINES_START = static_cast<std::uint16_t>(Elite::SHIP_BLOCK_BASE + Elite::MAX_SHIPS * Elite::SHIP_BLOCK_SIZE);

      for (std::uint16_t address = LINES_START; address < Elite::LineHeap::TOP; ++address)
      {
        Assert::AreEqual(_cpu.memory[address], _heap.Read(address), (_where + L": the heap at " + std::to_wstring(address)).c_str());
      }
    }
  } // namespace

  TEST_CLASS(TheBubbleKill)
  {
  public:
    /*
     * 6502: KILLSHP -- and the whole line heap is compared, because the shuffle is the routine.
     *
     * The fleets are chosen so that a kill happens at the bottom, the middle and the top of the
     * list, with a missile above and below the dead slot, with junk and non-junk types, and with
     * the Constrictor.
     */
    TEST_METHOD(TheKillMatchesKILLSHP)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const SpawnLabels at(oracle);
      const std::uint16_t killshp = oracle.Label("KILLSHP");

      const std::vector<std::vector<std::uint8_t>> FLEETS = {
        {9, 0},
        {9, 10, 0},
        {1, 9, 10, 0},
        {9, 1, 10, 11, 0},
        {3, 9, 5, 1, 10, 0},
        {31, 9, 1, 0},
        {15, 9, 1, 10, 0},
        {9, 10, 11, 12, 13, 14, 16, 0},
        {9, 2, 10, 0}, // the space station, whose death is `KS4` and shuffles nothing
        {2, 9, 0},
      };

      std::uint32_t compared = 0;
      std::uint32_t aborted = 0;
      std::uint32_t stations = 0;
      std::uint32_t missions = 0;
      std::uint32_t moved = 0;

      for (const std::vector<std::uint8_t>& fleet : FLEETS)
      {
        const std::size_t living = fleet.size() - 1u;
        for (std::size_t victim = 0; victim < living; ++victim)
        {
          for (const std::uint8_t locked : {0xFFu, 0u, 1u, 2u})
          {
            Cpu6502 cpu = oracle.Fresh();
            Elite::Bubble bubble;
            Elite::LineHeap heap;
            Elite::PlanetSunState state;
            Elite::ShipBlock work{};
            Elite::CommanderBlock commander;
            RecordingEffects effects;

            // ABORT, MESS and SPBLB are slice 3d's.
            cpu.AddTrap(oracle.Label("ABORT"));
            cpu.AddTrap(oracle.Label("MESS"));
            cpu.AddTrap(oracle.Label("SPBLB"));

            SeedBubble(cpu, bubble, heap, at, fleet);

            // The counts the kill has to decrement, seeded high enough to see them come down.
            for (std::size_t type = 0; type < bubble.counts.size(); ++type)
            {
              const std::uint8_t value = static_cast<std::uint8_t>(3u + (type & 3u));
              bubble.counts[type] = value;
              cpu.memory[static_cast<std::uint16_t>(at.many + type)] = value;
            }
            bubble.junk = 7;
            cpu.memory[at.junk] = 7;

            bubble.missileTarget = static_cast<std::uint8_t>(locked);
            cpu.memory[at.mstg] = static_cast<std::uint8_t>(locked);

            // A locked missile keeps its target in INWK+32 as %1ttttttt.
            for (std::size_t slot = 0; slot < living; ++slot)
            {
              if (fleet[slot] != Elite::SHIP_TYPE_MISSILE)
              {
                continue;
              }
              const std::uint8_t ai = static_cast<std::uint8_t>(0x80u | ((slot == 0u ? living - 1u : 0u) << 1));
              bubble.blocks[slot][32] = ai;
              cpu.memory[static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + 32u)] = ai;
            }

            commander.At(Elite::Field::MissionProgress) = 0x01;
            cpu.memory[at.tp] = 0x01;
            commander.bytes[static_cast<std::size_t>(Elite::Field::Kills) + 1u] = 40;
            cpu.memory[static_cast<std::uint16_t>(at.tally + 1)] = 40;

            /*
             * `KILLSHP` does NOT call `GINF`. It reads the dead ship's block through `INF` and its
             * blueprint through `XX0`, and both are the caller's to set -- `KS1` reaches it from
             * the main loop, which has just done so. A test that only puts X in place gets a
             * routine reading whatever the last call left.
             */
            const std::uint16_t block = Elite::SlotAddress(static_cast<std::uint8_t>(victim));
            cpu.memory[at.inf] = static_cast<std::uint8_t>(block);
            cpu.memory[static_cast<std::uint16_t>(at.inf + 1)] = static_cast<std::uint8_t>(block >> 8);

            const std::uint16_t blueprint = Elite::BlueprintAddress(fleet[victim]);
            cpu.memory[at.xx0] = static_cast<std::uint8_t>(blueprint);
            cpu.memory[static_cast<std::uint16_t>(at.xx0 + 1)] = static_cast<std::uint8_t>(blueprint >> 8);

            cpu.x = static_cast<std::uint8_t>(victim);
            const Elite::Testing::RunResult run = cpu.CallSubroutine(killshp, 400'000);
            Assert::IsTrue(run.completed, L"KILLSHP returned");

            Elite::KillShip(bubble, heap, state, work, commander, effects, static_cast<std::uint8_t>(victim));

            const std::wstring where = Widen("KILLSHP fleet=" + std::to_string(fleet.size()) + " victim=" + std::to_string(victim) +
                                             " locked=" + std::to_string(locked));

            CompareBubble(cpu, bubble, heap, at, where);
            Assert::AreEqual(cpu.memory[at.tp], commander.At(Elite::Field::MissionProgress), (where + L": TP").c_str());
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.tally + 1)],
                             commander.bytes[static_cast<std::size_t>(Elite::Field::Kills) + 1u], (where + L": TALLY+1").c_str());
            Assert::AreEqual<std::size_t>(cpu.trapHits.size(), effects.aborts.size() + effects.messages.size() + effects.stationBlobs,
                                          (where + L": the seams").c_str());

            aborted += static_cast<std::uint32_t>(effects.aborts.size());
            stations += effects.stationBlobs;
            missions += (commander.At(Elite::Field::MissionProgress) != 0x01u) ? 1u : 0u;
            // A kill only relocates when something is living above it, and the station's own
            // path shuffles nothing at all.
            moved += (fleet[victim] != Elite::SHIP_TYPE_STATION && victim + 1u < living) ? 1u : 0u;
            ++compared;
          }
        }
      }

      Assert::IsTrue(compared > 100u, L"a kill at every position in every fleet");
      Assert::IsTrue(aborted > 0u, L"and some of them unlocked the player's missile");
      Assert::IsTrue(stations > 0u, L"the space station's own path was taken");
      Assert::IsTrue(missions > 0u, L"the Constrictor's mission flag was set");
      Assert::IsTrue(moved > 0u, L"and some kills relocated a heap above them");
      Logger::WriteMessage(("KILLSHP: " + std::to_string(compared) + " kills, " + std::to_string(aborted) + " missiles unlocked, " +
                            std::to_string(stations) + " stations, " + std::to_string(missions) + " missions, " + std::to_string(moved) +
                            " relocations")
                             .c_str());
    }
  };

  TEST_CLASS(TheSystemBuild)
  {
  public:
    /*
     * 6502: SOLAR and SOS1 -- everything a system contains, made from its own seed bytes.
     *
     * There is no stored map: the planet's distance and offset come from `QQ15+1`, `QQ15+3` and
     * `QQ15+5`, and its LOOK comes from one bit of the tech level. The Trumbles breed first, and
     * that arithmetic is compared as carefully as the rest because it runs on `DORND`'s exit carry.
     */
    TEST_METHOD(TheSystemMatchesSOLAR)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const SpawnLabels at(oracle);
      const std::uint16_t solar = oracle.Label("SOLAR");

      // 6502: SCBASE -- an assembler constant, so it is derived from ylookup's first entry, which
      // is it plus the space view's four-cell left margin.
      const Cpu6502 image = oracle.Fresh();
      const std::uint16_t screenBase =
        static_cast<std::uint16_t>((image.memory[oracle.Label("ylookupl")] | (image.memory[oracle.Label("ylookuph")] << 8)) - 0x20);

      struct System
      {
        std::array<std::uint8_t, 6> seeds;
        std::uint8_t techLevel, tribbleLow, tribbleHigh, legal;
        std::array<std::uint8_t, 4> rand;
        bool carryIn;
        const wchar_t* what;
      };

      /*
       * The generator's state varies per system as well as the seeds, because `SOLAR`'s `ADC
       * TRIBBLE` runs on `DORND`'s EXIT carry -- and a single generator seed makes that constant,
       * so the breeding arithmetic is measured against one value of it (§6.48's shape again).
       */
      const std::vector<System> SYSTEMS = {
        {{0x5A, 0x00, 0x48, 0x02, 0x53, 0xB7}, 5, 0, 0, 0, {0xC3, 0x71, 0x2B, 0x49}, false, L"Lave, no Trumbles"},
        {{0x5A, 0x01, 0x48, 0x05, 0x53, 0xB7}, 8, 2, 0, 12, {0xC3, 0x71, 0x2B, 0x49}, false, L"a pair breeding"},
        {{0x11, 0x02, 0x33, 0x07, 0x91, 0x44}, 1, 200, 3, 255, {0x01, 0x02, 0x03, 0x04}, true, L"a swarm, and a fugitive"},
        {{0xFF, 0x03, 0xFF, 0x00, 0xFF, 0xFF}, 14, 0xFF, 0x7F, 1, {0xFE, 0xFF, 0xFD, 0xFC}, false, L"every seed byte high"},
        {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0, 1, 0xFF, 0, {0x80, 0x7F, 0x81, 0x00}, true, L"every seed byte low"},
        {{0x37, 0x02, 0x19, 0x04, 0x2A, 0x60}, 3, 7, 1, 9, {0x55, 0xAA, 0x33, 0xCC}, false, L"a generator that carries out"},
        {{0x48, 0x01, 0x22, 0x06, 0x71, 0x13}, 11, 33, 0, 200, {0x0F, 0xF0, 0x77, 0x88}, true, L"and one that does not"},
      };

      std::uint32_t compared = 0;
      std::uint32_t bred = 0;
      std::uint32_t drawn = 0;

      for (const System& system : SYSTEMS)
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Bubble bubble;
        Elite::LineHeap heap;
        Elite::ShipBlock work{};
        Elite::CommanderBlock commander;
        RecordingEffects effects;
        Elite::FlightState flight;
        Elite::Rng rng;
        Elite::Canvas canvas;
        Elite::DrawWorkspace draw;
        Elite::Stardust dust;
        Elite::PlanetSunState state;

        cpu.AddTrap(oracle.Label("msblob"));

        SeedBubble(cpu, bubble, heap, at, {});

        for (std::size_t byte = 0; byte < 6u; ++byte)
        {
          cpu.memory[static_cast<std::uint16_t>(at.qq15 + byte)] = system.seeds[byte];
        }
        cpu.memory[at.tek] = system.techLevel;

        const std::size_t tribble = static_cast<std::size_t>(Elite::Field::Tribbles);
        commander.bytes[tribble] = system.tribbleLow;
        commander.bytes[tribble + 1u] = system.tribbleHigh;
        cpu.memory[at.tribble] = system.tribbleLow;
        cpu.memory[static_cast<std::uint16_t>(at.tribble + 1)] = system.tribbleHigh;

        commander.At(Elite::Field::LegalStatus) = system.legal;
        cpu.memory[at.fist] = system.legal;

        for (std::size_t good = 0; good < 17u; ++good)
        {
          const std::uint8_t value = static_cast<std::uint8_t>(7u + good);
          commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + good] = value;
          cpu.memory[static_cast<std::uint16_t>(at.qq20 + good)] = value;
        }

        // `SOLAR` falls through into `NWSTARS`, so the stardust count and the view type are part
        // of its arguments even though nothing in `SOLAR` itself mentions them.
        dust.count = 12;
        cpu.memory[oracle.Label("NOSTM")] = 12;
        cpu.memory[oracle.Label("QQ11")] = 0;

        for (std::size_t byte = 0; byte < 4u; ++byte)
        {
          cpu.memory[static_cast<std::uint16_t>(at.rand + byte)] = system.rand[byte];
        }
        rng.SetState(system.rand);
        cpu.c = system.carryIn;

        const Elite::Testing::RunResult run = cpu.CallSubroutine(solar, 400'000);
        Assert::IsTrue(run.completed, L"SOLAR returned");

        Elite::BuildSystem(canvas, draw, dust, state, bubble, work, commander, rng, flight, effects, system.techLevel, system.seeds, 0,
                           system.carryIn);

        const std::wstring where = std::wstring(L"SOLAR ") + system.what;

        /*
         * The screen, which `SOLAR` reaches through `nWq` and `SCAN` and which this test did not
         * look at until 3d-a. `nWq` plots twelve specks and `WPSHPS` scans whatever the bubble
         * held, so a port that got either wrong agreed with the game on every byte this test used
         * to compare.
         */
        const std::span<const std::uint8_t> ours = canvas.Screen();
        for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
        {
          const std::uint8_t expected = cpu.memory[static_cast<std::uint16_t>(screenBase + offset)];
          if (expected != ours[offset])
          {
            Assert::Fail((where + L": screen offset " + std::to_wstring(offset) + L" -- game has " + std::to_wstring(expected) +
                          L", port has " + std::to_wstring(ours[offset]))
                           .c_str());
          }
          drawn += (ours[offset] != 0u) ? 1u : 0u;
        }

        CompareBubble(cpu, bubble, heap, at, where);
        Assert::AreEqual(cpu.memory[at.tribble], commander.bytes[tribble], (where + L": TRIBBLE").c_str());
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.tribble + 1)], commander.bytes[tribble + 1u],
                         (where + L": TRIBBLE+1").c_str());
        Assert::AreEqual(cpu.memory[at.fist], commander.At(Elite::Field::LegalStatus), (where + L": FIST").c_str());
        for (std::size_t good = 0; good < 17u; ++good)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.qq20 + good)],
                           commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + good],
                           (where + L": QQ20+" + std::to_wstring(good)).c_str());
        }
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.inwk + byte)], work[byte],
                           (where + L": INWK+" + std::to_wstring(byte)).c_str());
        }
        for (std::size_t byte = 0; byte < 4u; ++byte)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.rand + byte)], rng.State()[byte],
                           (where + L": RAND+" + std::to_wstring(byte)).c_str());
        }

        bred += (commander.bytes[tribble] != system.tribbleLow) ? 1u : 0u;
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(7u, compared, L"every system");
      Assert::IsTrue(bred > 0u, L"and some of them had Trumbles breed");
      Assert::IsTrue(drawn > 0u, L"and the stardust was actually plotted");
      Logger::WriteMessage(("SOLAR: " + std::to_string(compared) + " systems, " + std::to_string(bred) + " with Trumbles, " +
                            std::to_string(drawn) + " marked bytes")
                             .c_str());
    }
  };

} // namespace GameLogicTests

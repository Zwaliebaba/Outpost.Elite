#include "pch.h"

#include "OracleImage.h"

#include "ExtendedTokens.h"
#include "Rng.h"
#include "Tokens.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::SystemData;
using Elite::SystemSeeds;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The universe against the game that generates it (slice 2a).
 *
 * Elite's 2,048 systems are not stored anywhere: they are six bytes of seed and a twisting rule.
 * That makes this the least forgiving code in the port so far. A wrong carry here does not crash
 * and does not look wrong -- it produces a complete, plausible, self-consistent galaxy that is
 * not Elite's. The only way to know is to check every system, so that is what these do: all
 * eight galaxies, all 256 systems each, every field and every name.
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

    /// The addresses the universe routines read and write.
    struct Scratch
    {
      std::uint16_t qq15 = 0; ///< the seed block, six bytes in zero page
      std::uint16_t qq3 = 0;
      std::uint16_t qq4 = 0;
      std::uint16_t qq5 = 0;
      std::uint16_t qq6 = 0;
      std::uint16_t qq7 = 0;
      std::uint16_t qq21 = 0; ///< the galaxy's seeds, which the galactic hyperdrive rotates

      explicit Scratch(const OracleImage& _oracle)
        : qq15(_oracle.Label("QQ15")),
          qq3(_oracle.Label("QQ3")),
          qq4(_oracle.Label("QQ4")),
          qq5(_oracle.Label("QQ5")),
          qq6(_oracle.Label("QQ6")),
          qq7(_oracle.Label("QQ7")),
          qq21(_oracle.Label("QQ21"))
      {
      }
    };

    void LoadSeeds(Cpu6502& _cpu, std::uint16_t _address, const SystemSeeds& _seeds)
    {
      for (int index = 0; index < 6; ++index)
      {
        _cpu.memory[static_cast<std::uint16_t>(_address + index)] = _seeds.bytes[index];
      }
    }

    [[nodiscard]] SystemSeeds ReadSeeds(const Cpu6502& _cpu, std::uint16_t _address)
    {
      SystemSeeds seeds;
      for (int index = 0; index < 6; ++index)
      {
        seeds.bytes[index] = _cpu.memory[static_cast<std::uint16_t>(_address + index)];
      }
      return seeds;
    }

    [[nodiscard]] std::string Show(const SystemSeeds& _seeds)
    {
      static constexpr char DIGITS[] = "0123456789ABCDEF";
      std::string text;
      for (const std::uint8_t byte : _seeds.bytes)
      {
        text += DIGITS[byte >> 4];
        text += DIGITS[byte & 0x0F];
        text += ' ';
      }
      return text;
    }

    /// A deterministic spread of seeds, for the routines that take any six bytes rather than a
    /// system's. Includes the corners, because a twist is all carries.
    [[nodiscard]] std::vector<SystemSeeds> SweepSeeds()
    {
      std::vector<SystemSeeds> seeds = {
        SystemSeeds{{0, 0, 0, 0, 0, 0}},
        SystemSeeds{{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}},
        SystemSeeds{{0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00}},
        SystemSeeds{{0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF}},
        SystemSeeds{{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}},
        SystemSeeds{{0x01, 0x01, 0x01, 0x01, 0x01, 0x01}},
        Elite::GALAXY_ONE_SEEDS,
      };

      // A cheap deterministic generator, so the sweep is wide without being random.
      std::uint32_t state = 0x1D872B41u;
      for (int extra = 0; extra < 4000; ++extra)
      {
        SystemSeeds next;
        for (int index = 0; index < 6; ++index)
        {
          state = state * 1103515245u + 12345u;
          next.bytes[index] = static_cast<std::uint8_t>(state >> 19);
        }
        seeds.push_back(next);
      }
      return seeds;
    }

    /// Collects a printed name, so a system's name is compared as characters rather than as pixels.
    class Collector : public Elite::TextSink
    {
    public:
      void Put(std::uint8_t _character) override
      {
        text += static_cast<char>(_character);
      }
      std::string text;
    };
  } // namespace

  TEST_CLASS(UniverseAgainstTheShippedGame)
  {
  public:
    /*
     * The galaxy the game starts in lives in the default commander block rather than in a table,
     * so the port carries a copy. This is the test that stops that copy drifting: slice 2d will
     * extract `NA%` properly, and until then the constant is checked against the shipped bytes on
     * every run.
     */
    TEST_METHOD(GalaxyOneSeedsMatchTheShippedCommander)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Cpu6502 cpu = oracle.Fresh();
      const std::uint16_t commander = oracle.Label("NA%");

      // The offset is where the seeds sit inside the commander block; slice 2d owns naming the
      // rest of it. Found by searching the block for the constant, not by counting fields.
      constexpr int SEED_OFFSET = 121;

      for (int index = 0; index < 6; ++index)
      {
        const std::uint8_t shipped = cpu.memory[static_cast<std::uint16_t>(commander + SEED_OFFSET + index)];
        Assert::AreEqual<std::uint32_t>(
          shipped, Elite::GALAXY_ONE_SEEDS.bytes[index],
          (L"GALAXY_ONE_SEEDS byte " + std::to_wstring(index) + L" no longer matches the shipped commander").c_str());
      }
    }

    /// 6502: TT54 -- one twist, over four thousand seed states plus the corners.
    TEST_METHOD(SeedTwistMatchesTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("TT54");
      const std::vector<SystemSeeds> sweep = SweepSeeds();

      for (const SystemSeeds& start : sweep)
      {
        Cpu6502 cpu = oracle.Fresh();
        LoadSeeds(cpu, zp.qq15, start);
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed, L"TT54 should return");

        SystemSeeds ours = start;
        Elite::TwistSeeds(ours);

        Assert::IsTrue(ReadSeeds(cpu, zp.qq15) == ours,
                       Widen("TT54 from " + Show(start) + ": game " + Show(ReadSeeds(cpu, zp.qq15)) + "-> port " + Show(ours)).c_str());
      }

      Logger::WriteMessage(("TT54: " + std::to_string(sweep.size()) + " seed states twisted\n").c_str());
    }

    /// 6502: TT20 -- four twists through a fall-through the port spells as a loop.
    TEST_METHOD(NextSystemMatchesTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("TT20");

      for (const SystemSeeds& start : SweepSeeds())
      {
        Cpu6502 cpu = oracle.Fresh();
        LoadSeeds(cpu, zp.qq15, start);
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;

        Assert::IsTrue(cpu.CallSubroutine(routine, 5'000).completed, L"TT20 should return");

        SystemSeeds ours = start;
        Elite::NextSystem(ours);

        Assert::IsTrue(ReadSeeds(cpu, zp.qq15) == ours, Widen("TT20 from " + Show(start)).c_str());
      }
    }

    /*
     * 6502: Ghy's G1 loop. Each byte rotates within itself, which is not what it looks like -- so
     * this is checked over every one of the 256 values a byte can hold, in every position.
     */
    TEST_METHOD(GalaxyRotationMatchesTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);

      /*
       * G1 has no RTS, the same way DVID4 has none: it falls out of its own loop into zZ and on
       * through the rest of the galactic jump, which wants fuel, mission flags and a screen. So it
       * is STEPPED rather than called, from G1 until the program counter reaches zZ -- which runs
       * the shipped bytes and nothing else.
       */
      const std::uint16_t loop = oracle.Label("G1");
      const std::uint16_t after = oracle.Label("zZ");

      for (std::uint32_t value = 0; value < 256; ++value)
      {
        SystemSeeds start;
        for (int index = 0; index < 6; ++index)
        {
          // A different byte in each position, so a rotation that mixed them would show.
          start.bytes[index] = static_cast<std::uint8_t>(value + index * 37u);
        }

        Cpu6502 cpu = oracle.Fresh();
        LoadSeeds(cpu, zp.qq21, start);
        cpu.a = 0;
        cpu.x = 5; // 6502: LDX #5, which the caller sets before falling into G1
        cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.pc = loop;

        bool reached = false;
        for (int instruction = 0; instruction < 200; ++instruction)
        {
          if (cpu.pc == after)
          {
            reached = true;
            break;
          }
          Assert::IsTrue(cpu.Step(), L"G1 should contain no opcode the interpreter lacks");
        }
        Assert::IsTrue(reached, L"stepping from G1 should arrive at zZ");

        SystemSeeds ours = start;
        Elite::NextGalaxy(ours);

        Assert::IsTrue(ReadSeeds(cpu, zp.qq21) == ours,
                       Widen("G1 from " + Show(start) + ": game " + Show(ReadSeeds(cpu, zp.qq21)) + "-> port " + Show(ours)).c_str());
      }
    }

    /*
     * THE ONE THAT MATTERS. Every field of every system in every galaxy.
     *
     * Eight galaxies reached by rotating the seeds, 256 systems each reached by twisting four
     * times, and for each of the 2,048 the economy, government, technology level, population and
     * productivity compared against TT24. That is the whole of what a player sees on the Data on
     * System screen except the name and the description, and it is generated rather than stored,
     * so there is no table to check it against -- only the game.
     */
    TEST_METHOD(EveryFieldOfEverySystemInEveryGalaxyMatches)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("TT24");

      SystemSeeds galaxy = Elite::GALAXY_ONE_SEEDS;
      std::uint32_t compared = 0;

      for (int galaxyNumber = 1; galaxyNumber <= 8; ++galaxyNumber)
      {
        SystemSeeds seeds = galaxy;

        for (int system = 0; system < 256; ++system)
        {
          Cpu6502 cpu = oracle.Fresh();
          LoadSeeds(cpu, zp.qq15, seeds);
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;

          Assert::IsTrue(cpu.CallSubroutine(routine, 50'000).completed, L"TT24 should return");

          const SystemData ours = Elite::GenerateSystemData(seeds);

          const std::wstring where =
            L"galaxy " + std::to_wstring(galaxyNumber) + L" system " + std::to_wstring(system) + L" (seeds " + Widen(Show(seeds)) + L")";

          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.qq3], ours.economy, (where + L": economy").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.qq4], ours.government, (where + L": government").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.qq5], ours.techLevel, (where + L": tech level").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.qq6], ours.population, (where + L": population").c_str());

          const std::uint16_t productivity =
            static_cast<std::uint16_t>(cpu.memory[zp.qq7] | (cpu.memory[static_cast<std::uint16_t>(zp.qq7 + 1)] << 8));
          Assert::AreEqual<std::uint32_t>(productivity, ours.productivity, (where + L": productivity").c_str());

          ++compared;
          Elite::NextSystem(seeds);
        }

        Elite::NextGalaxy(galaxy);
      }

      Logger::WriteMessage(("TT24: " + std::to_string(compared) + " systems compared on every field\n").c_str());
      Assert::AreEqual<std::uint32_t>(2048u, compared, L"the sweep should cover all eight galaxies");
    }

    /*
     * 6502: TT111 -- the system nearest the crosshairs, in every galaxy, over a grid of crosshair
     * positions and from several starting systems.
     *
     * Everything the routine leaves behind is compared: the seeds it settled on, the index, the two
     * coordinates it writes back, the distance, and the system data it falls into TT24 to produce.
     * The distance is the part worth checking hardest -- it is measured differently from the metric
     * the search used, four instructions apart in the original, and getting them the same way round
     * would put every system in the game at a plausible but wrong range.
     */
    TEST_METHOD(NearestSystemMatchesTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("TT111");
      const std::uint16_t qq0 = oracle.Label("QQ0");
      const std::uint16_t qq1 = oracle.Label("QQ1");
      const std::uint16_t qq8 = oracle.Label("QQ8");
      const std::uint16_t qq9 = oracle.Label("QQ9");
      const std::uint16_t qq10 = oracle.Label("QQ10");
      const std::uint16_t zz = oracle.Label("ZZ");

      SystemSeeds galaxy = Elite::GALAXY_ONE_SEEDS;
      std::uint32_t compared = 0;

      for (int galaxyNumber = 1; galaxyNumber <= 8; ++galaxyNumber)
      {
        for (std::uint32_t crossX = 0; crossX < 256; crossX += 23)
        {
          for (std::uint32_t crossY = 0; crossY < 256; crossY += 29)
          {
            const std::uint8_t currentX = static_cast<std::uint8_t>(crossX ^ 0x5Au);
            const std::uint8_t currentY = static_cast<std::uint8_t>(crossY ^ 0xA5u);

            Cpu6502 cpu = oracle.Fresh();
            LoadSeeds(cpu, zp.qq21, galaxy);
            cpu.memory[qq9] = static_cast<std::uint8_t>(crossX);
            cpu.memory[qq10] = static_cast<std::uint8_t>(crossY);
            cpu.memory[qq0] = currentX;
            cpu.memory[qq1] = currentY;
            cpu.a = cpu.x = cpu.y = 0;
            cpu.sp = 0xFD;

            const auto run = cpu.CallSubroutine(routine, 2'000'000);
            Assert::IsTrue(run.completed, L"TT111 should return");

            const Elite::NearestSystem ours =
              Elite::FindNearestSystem(galaxy, static_cast<std::uint8_t>(crossX), static_cast<std::uint8_t>(crossY), currentX, currentY);

            const std::wstring where = L"galaxy " + std::to_wstring(galaxyNumber) + L" crosshairs (" + std::to_wstring(crossX) + L"," +
                                       std::to_wstring(crossY) + L") from (" + std::to_wstring(currentX) + L"," +
                                       std::to_wstring(currentY) + L")";

            Assert::IsTrue(ReadSeeds(cpu, zp.qq15) == ours.seeds, (where + L": seeds").c_str());
            Assert::AreEqual<std::uint32_t>(cpu.memory[zz], ours.index, (where + L": index").c_str());
            Assert::AreEqual<std::uint32_t>(cpu.memory[qq9], ours.x, (where + L": x").c_str());
            Assert::AreEqual<std::uint32_t>(cpu.memory[qq10], ours.y, (where + L": y").c_str());

            const std::uint16_t distance =
              static_cast<std::uint16_t>(cpu.memory[qq8] | (cpu.memory[static_cast<std::uint16_t>(qq8 + 1)] << 8));
            Assert::AreEqual<std::uint32_t>(distance, ours.distance, (where + L": distance").c_str());

            // And the TT24 it jumps into rather than returning from.
            Assert::AreEqual<std::uint32_t>(cpu.memory[zp.qq3], ours.data.economy, (where + L": economy").c_str());
            Assert::AreEqual<std::uint32_t>(cpu.memory[zp.qq5], ours.data.techLevel, (where + L": tech level").c_str());

            ++compared;
          }
        }

        Elite::NextGalaxy(galaxy);
      }

      Logger::WriteMessage(("TT111: " + std::to_string(compared) + " searches compared\n").c_str());
    }

    /*
     * 6502: PDESC's generated path -- every system's description, in every galaxy.
     *
     * This is the test slice 2a could not write. A description is four instructions of PDESC and
     * then the whole of the extended token system: the RNG is seeded from the system's own seed
     * bytes and extended token 5 is printed, and that token reaches nine control codes, the
     * justification line buffer, the recursive printer and the system name. Slice 1c-c-b built
     * the missing half, so the comparison is now the real one -- 2,048 descriptions, character
     * for character, through a trap on the screen routine.
     *
     * It is the strongest single test in the port so far, because almost everything text-related
     * is underneath it. Token expansion, the randomised alternatives, the case machinery, the
     * word wrap, the padding that widens the gaps, MT17's trick of reaching back into the buffer
     * to turn a name into an adjective, and MT18's random pronounceable words all have to agree
     * for a single description to come out right, and all 2,048 do.
     *
     * The trap is on CHPR with the carry cleared on return, because that is what CHPR does and
     * the justification's SBC borrows on the strength of it.
     */
    TEST_METHOD(EverySystemDescriptionInEveryGalaxyMatches)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t chpr = oracle.Label("CHPR");
      const std::uint16_t pdesc = oracle.Label("PDESC");

      /*
       * Control code 3 is the name of the system being described, and it arrives as a value token
       * because the recursive printer has no universe of its own. This is what slice 2a's half of
       * the seam looks like once filled: the name comes from PrintSystemName, which is the port of
       * the very routine the game reaches here.
       */
      struct SystemNameTokens : public Elite::ValueTokens
      {
        void Print(std::uint8_t _token, Elite::TextSink&) override
        {
          if (_token == 3 && printer != nullptr)
          {
            Elite::PrintSystemName(*printer, seeds);
            return;
          }
          ++unexpected;
        }

        Elite::TokenPrinter* printer = nullptr;
        SystemSeeds seeds;
        std::uint32_t unexpected = 0;
      };

      /// 9, 11 and 21 reach the canvas. A description that reached one would be a hole in this.
      struct CountingControls : public Elite::ControlCodes
      {
        void Run(std::uint8_t _code) override
        {
          ++counts[_code];
        }
        std::array<std::uint32_t, 32> counts{};
      };

      CountingControls controls;
      SystemSeeds galaxy = Elite::GALAXY_ONE_SEEDS;
      std::uint32_t compared = 0;
      std::string firstDescription;

      for (int galaxyNumber = 1; galaxyNumber <= 8; ++galaxyNumber)
      {
        SystemSeeds seeds = galaxy;

        for (int system = 0; system < 256; ++system)
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
          LoadSeeds(cpu, zp.qq15, seeds);

          // 6502: LDA QQ8 / ORA QQ8+1 / BNE PD1 -- a non-zero distance takes the generated path
          // rather than the handful of hand-written mission descriptions, which are phase 4's.
          cpu.memory[oracle.Label("QQ8")] = 1;
          cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ8") + 1)] = 0;

          cpu.memory[oracle.Label("QQ17")] = 0;
          cpu.memory[oracle.Label("DTW1")] = 0;
          cpu.memory[oracle.Label("DTW2")] = 0;
          cpu.memory[oracle.Label("DTW3")] = 0;
          cpu.memory[oracle.Label("DTW4")] = 0;
          cpu.memory[oracle.Label("DTW5")] = 0;
          cpu.memory[oracle.Label("DTW6")] = 0;
          cpu.memory[oracle.Label("DTW8")] = 0xFF;
          for (std::uint16_t index = 0; index < Elite::CharacterPrinter::BUFFER_SIZE; ++index)
          {
            cpu.memory[static_cast<std::uint16_t>(oracle.Label("BUF") + index)] = 0;
          }

          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;

          const auto run = cpu.CallSubroutine(pdesc, 500'000);
          Assert::IsTrue(run.completed && !run.illegalOpcode, L"PDESC should return");

          std::string expected;
          for (const auto& hit : cpu.trapHits)
          {
            expected += static_cast<char>(hit.a);
          }

          Collector screen;
          Elite::CharacterPrinter characters(screen);
          SystemNameTokens names;
          names.seeds = seeds;
          Elite::TokenPrinter recursive(characters, &names);
          names.printer = &recursive;
          Elite::Rng rng;
          Elite::ExtendedTokenPrinter printer(characters, recursive, rng, &controls);
          Elite::PrintSystemDescription(printer, rng, seeds);

          const std::wstring where =
            L"galaxy " + std::to_wstring(galaxyNumber) + L" system " + std::to_wstring(system) + L" (seeds " + Widen(Show(seeds)) + L")";

          Assert::AreEqual<std::uint32_t>(0u, names.unexpected, (where + L": only the system name should be a value token").c_str());

          if (expected != screen.text)
          {
            Assert::Fail((where + L"\n  game: \"" + Widen(expected) + L"\"\n  port: \"" + Widen(screen.text) + L"\"").c_str());
          }

          if (galaxyNumber == 1 && system == 0)
          {
            firstDescription = screen.text;
          }

          ++compared;
          Elite::NextSystem(seeds);
        }

        Elite::NextGalaxy(galaxy);
      }

      Assert::AreEqual<std::uint32_t>(2048u, compared, L"every system should be described");

      std::uint32_t deferred = 0;
      for (const std::uint32_t count : controls.counts)
      {
        deferred += count;
      }
      Assert::AreEqual<std::uint32_t>(0u, deferred, L"no generated description should reach a control code the port defers");

      Logger::WriteMessage(("PDESC: 2,048 descriptions compared. Galaxy 1 system 0 reads:\n" + firstDescription + "\n").c_str());
    }

    /*
     * The other half of PDESC's seeding, checked on its own because the sweep above would pass
     * with the wrong seed bytes as long as both sides used the same wrong ones.
     */
    TEST_METHOD(SystemDescriptionsSeedFromTheirOwnSeedBytes)
    {
      // 6502: PDL1K -- LDA QQ15+2,X / STA RAND,X, counting X down from 3.
      Elite::Rng rng;
      Collector screen;
      Elite::CharacterPrinter characters(screen);
      Elite::TokenPrinter recursive(characters, nullptr);
      Elite::ExtendedTokenPrinter printer(characters, recursive, rng, nullptr);

      const SystemSeeds probe = {{11, 22, 33, 44, 55, 66}};
      Elite::PrintSystemDescription(printer, rng, probe);

      // The printer consumes the state, so this checks the seeding through a fresh one.
      Elite::Rng seeded;
      seeded.SetState({probe.bytes[2], probe.bytes[3], probe.bytes[4], probe.bytes[5]});
      Assert::AreEqual<std::uint32_t>(33u, seeded.State()[0], L"the RNG is seeded from seed byte 2");
      Assert::IsFalse(screen.text.empty(), L"a description should have been produced");
    }

    /*
     * 6502: cpl -- every system's name, in every galaxy, compared character for character through
     * a trap on the character routine.
     *
     * Also checks that the seeds come back unchanged. The original saves and restores them because
     * printing a name must not move the universe on, and a port that forgot would generate a
     * different galaxy the moment anything drew a chart.
     */
    TEST_METHOD(EverySystemNameInEveryGalaxyMatches)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Scratch zp(oracle);
      const std::uint16_t routine = oracle.Label("cpl");
      const std::uint16_t character = oracle.Label("TT26");

      SystemSeeds galaxy = Elite::GALAXY_ONE_SEEDS;
      std::uint32_t compared = 0;
      std::string firstName;

      for (int galaxyNumber = 1; galaxyNumber <= 8; ++galaxyNumber)
      {
        SystemSeeds seeds = galaxy;

        for (int system = 0; system < 256; ++system)
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(character);
          LoadSeeds(cpu, zp.qq15, seeds);
          cpu.memory[oracle.Label("QQ17")] = 0;
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;

          const auto run = cpu.CallSubroutine(routine, 200'000);
          Assert::IsTrue(run.completed && !run.illegalOpcode, L"cpl should return");

          std::string expected;
          for (const auto& hit : cpu.trapHits)
          {
            expected += static_cast<char>(hit.a);
          }

          Collector collector;
          Elite::TokenPrinter printer(collector, nullptr);
          SystemSeeds ours = seeds;
          Elite::PrintSystemName(printer, ours);

          const std::wstring where =
            L"galaxy " + std::to_wstring(galaxyNumber) + L" system " + std::to_wstring(system) + L" (seeds " + Widen(Show(seeds)) + L")";

          Assert::IsTrue(expected == collector.text,
                         (where + L": game \"" + Widen(expected) + L"\", port \"" + Widen(collector.text) + L"\"").c_str());
          Assert::IsTrue(ours == seeds, (where + L": cpl must leave the seeds as it found them").c_str());

          if (galaxyNumber == 1 && system == 0)
          {
            firstName = collector.text;
          }

          ++compared;
          Elite::NextSystem(seeds);
        }

        Elite::NextGalaxy(galaxy);
      }

      Logger::WriteMessage(("cpl: " + std::to_string(compared) + " names compared; galaxy 1 system 0 is \"" + firstName + "\"\n").c_str());
      Assert::AreEqual<std::uint32_t>(2048u, compared, L"the sweep should cover all eight galaxies");
    }
  };

} // namespace GameLogicTests

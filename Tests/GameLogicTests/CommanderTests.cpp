#include "pch.h"

#include "OracleImage.h"

#include "Commander.h"
#include "LookupTables.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::CommanderBlock;
using Elite::Field;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The commander against the game that saves it (slice 2d).
 *
 * The two checksums are the whole difficulty. Both thread a carry through seventy-three steps and
 * the second rotates the accumulator through that carry in the middle of each one, so they cannot
 * be checked by reasoning -- only against the routine. They are swept over blocks built to hit
 * every case that matters: the shipped default, all-zeros, all-255, a walking one bit through all
 * 77 bytes, and a spread of pseudo-random blocks.
 *
 * The one place the port deliberately differs is the failure path. DFAULT does not reject a bad
 * block, it hangs -- `BNE doitagain` branches backwards for ever. The last test here establishes
 * that the two nonetheless agree on WHICH blocks are acceptable, by running the shipped routine
 * under an instruction budget and treating "did not finish" as "rejected".
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

/// Where the block lives inside the game's own copy of it. NA% is the name, and the block
/// follows the eight bytes of it.
struct Layout
{
  std::uint16_t na = 0;
  std::uint16_t na2 = 0;
  std::uint16_t block = 0;
  std::uint16_t tp = 0;

  explicit Layout(const OracleImage& _oracle)
    : na(_oracle.Label("NA%"))
    , na2(_oracle.Label("NA2%"))
    , block(static_cast<std::uint16_t>(_oracle.Label("NA%") + Elite::COMMANDER_NAME_SIZE))
    , tp(_oracle.Label("TP"))
  {
  }
};

/// Puts a block where the shipped checksum routines read it.
void PlaceBlock(Cpu6502& _cpu, const Layout& _layout, const CommanderBlock& _block)
{
  for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
  {
    _cpu.memory[static_cast<std::uint16_t>(_layout.block + index)] = _block.bytes[index];
  }
}

/// A spread of blocks: the shipped one, the corners, a walking bit, and pseudo-random fill.
std::vector<CommanderBlock> SampleBlocks()
{
  std::vector<CommanderBlock> blocks;

  blocks.push_back(Elite::DefaultCommander());

  CommanderBlock zeros;
  blocks.push_back(zeros);

  CommanderBlock ones;
  ones.bytes.fill(0xFF);
  blocks.push_back(ones);

  // One bit set at a time, through every byte. This is what finds a carry that threads the wrong
  // way: a single bit in byte N changes the result of every step after N, and only if the chain
  // is right.
  for (std::size_t byte = 0; byte < Elite::COMMANDER_BLOCK_SIZE; ++byte)
  {
    for (const std::uint8_t bit : { 0x01u, 0x80u })
    {
      CommanderBlock walking;
      walking.bytes[byte] = static_cast<std::uint8_t>(bit);
      blocks.push_back(walking);
    }
  }

  // And a spread that fills every byte, so the carry chain is exercised at length rather than
  // from a single perturbation.
  std::uint32_t state = 0x1234567u;
  for (int round = 0; round < 64; ++round)
  {
    CommanderBlock random;
    for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
    {
      state = state * 1103515245u + 12345u;
      random.bytes[index] = static_cast<std::uint8_t>(state >> 16);
    }
    blocks.push_back(random);
  }

  return blocks;
}
} // namespace

TEST_CLASS(CommanderAgainstTheShippedGame)
{
public:
  /*
   * The extracted default commander against the block the game carries, byte for byte.
   *
   * And against slice 2a's copy of the galaxy seeds, which were lifted out of this same block
   * before it had a name -- so the two must still agree or one of them has drifted.
   */
  TEST_METHOD(TheDefaultCommanderMatchesTheShippedBlock)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Layout layout(oracle);
    const Cpu6502 cpu = oracle.Fresh();

    for (std::size_t index = 0; index < Elite::DEFAULT_COMMANDER.size(); ++index)
    {
      Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(layout.na2 + index)],
                                      Elite::DEFAULT_COMMANDER[index],
                                      (L"DEFAULT_COMMANDER byte " + std::to_wstring(index)).c_str());
    }

    const CommanderBlock block = Elite::DefaultCommander();
    Assert::IsTrue(block.GalaxySeeds() == Elite::GALAXY_ONE_SEEDS,
                   L"the default commander's seeds and GALAXY_ONE_SEEDS must still agree");

    // The values a player would recognise, as a check that the offsets name the right bytes
    // rather than merely being self-consistent.
    Assert::AreEqual<std::uint32_t>(1000u, block.Cash(), L"a new commander starts with 100.0 credits");
    Assert::AreEqual<std::uint32_t>(70u, block.At(Field::Fuel), L"and 7.0 light years of fuel");
    Assert::AreEqual<std::uint32_t>(20u, block.At(Field::SystemX), L"at Lave, which is at x = 20");
    Assert::AreEqual<std::uint32_t>(173u, block.At(Field::SystemY), L"and y = 173");
    Assert::AreEqual<std::uint32_t>(0u, block.At(Field::GalaxyNumber), L"in galaxy one, counted from zero");
    // CRGO is stored two greater than the capacity it means, to save an instruction in tnpr, so
    // a standard twenty-tonne hold reads 22 and not 20.
    Assert::AreEqual<std::uint32_t>(22u, block.At(Field::CargoCapacity),
                                    L"with twenty tonnes of space, written as 22");
    Assert::AreEqual<std::uint32_t>(3u, block.At(Field::Missiles), L"and three missiles");

    std::string name;
    for (const std::uint8_t byte : Elite::DefaultCommanderName())
    {
      name += (byte >= 32 && byte < 127) ? static_cast<char>(byte) : ' ';
    }
    Logger::WriteMessage(("NA2%: the default commander is \"" + name + "\"").c_str());
  }

  /// 6502: CHECK -- the carry-threaded checksum, over the sample of blocks.
  TEST_METHOD(ChecksumMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Layout layout(oracle);
    const std::uint16_t routine = oracle.Label("CHECK");

    std::uint32_t compared = 0;
    for (const CommanderBlock& block : SampleBlocks())
    {
      Cpu6502 cpu = oracle.Fresh();
      PlaceBlock(cpu, layout, block);
      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      Assert::IsTrue(cpu.CallSubroutine(routine, 10'000).completed, L"CHECK should return");

      Assert::AreEqual<std::uint32_t>(cpu.a, Elite::Checksum(block),
                                      (L"CHECK differs on block " + std::to_wstring(compared)).c_str());
      ++compared;
    }

    Logger::WriteMessage(("CHECK: " + std::to_string(compared) + " blocks compared").c_str());
  }

  /// 6502: CHECK2 -- the same, with a rotate through the carry inside every step.
  TEST_METHOD(SecondChecksumMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Layout layout(oracle);
    const std::uint16_t routine = oracle.Label("CHECK2");

    std::uint32_t compared = 0;
    for (const CommanderBlock& block : SampleBlocks())
    {
      Cpu6502 cpu = oracle.Fresh();
      PlaceBlock(cpu, layout, block);
      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      Assert::IsTrue(cpu.CallSubroutine(routine, 10'000).completed, L"CHECK2 should return");

      Assert::AreEqual<std::uint32_t>(cpu.a, Elite::Checksum2(block),
                                      (L"CHECK2 differs on block " + std::to_wstring(compared)).c_str());
      ++compared;
    }

    Logger::WriteMessage(("CHECK2: " + std::to_string(compared) + " blocks compared").c_str());
  }

  /*
   * 6502: SVE's SVL1 loop and the two CHECK calls -- what the game writes to disk.
   *
   * The comparison is the whole file: the eight bytes of name and the seventy-seven of block, as
   * the original leaves them in NA%. That covers the order the two checksums are computed in,
   * which is the part a reading of the code gets wrong -- CHECK2 runs first and CHECK then runs
   * over a block that already holds its result.
   */
  TEST_METHOD(SavingACommanderMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Layout layout(oracle);

    std::uint32_t compared = 0;
    for (const CommanderBlock& block : SampleBlocks())
    {
      Cpu6502 cpu = oracle.Fresh();

      // 6502: SVE copies from TP, the live commander, so that is where the block goes.
      for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(layout.tp + index)] = block.bytes[index];
      }

      const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
      for (std::size_t index = 0; index < name.size(); ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(layout.na + index)] = name[index];
      }

      /*
       * SVE itself asks for a drive, prints menus and writes a file. What is being compared is
       * its arithmetic half, so the loop and the two checksums are stepped directly: the copy is
       * eight instructions and reproducing it here is less misleading than trapping six routines
       * and hoping the remainder is the same shape.
       */
      cpu.x = 0x4C;
      for (int step = 0x4C; step >= 0; --step)
      {
        cpu.memory[static_cast<std::uint16_t>(layout.block + step)] =
          cpu.memory[static_cast<std::uint16_t>(layout.tp + step)];
      }

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      Assert::IsTrue(cpu.CallSubroutine(oracle.Label("CHECK2"), 10'000).completed, L"CHECK2 should return");
      cpu.memory[oracle.Label("CHK3")] = cpu.a;

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      Assert::IsTrue(cpu.CallSubroutine(oracle.Label("CHECK"), 10'000).completed, L"CHECK should return");
      cpu.memory[oracle.Label("CHK")] = cpu.a;

      std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> file{};
      Elite::SaveCommander(block, std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name),
                           std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE>(file));

      for (std::size_t index = 0; index < file.size(); ++index)
      {
        Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(layout.na + index)], file[index],
                                        (L"saved file byte " + std::to_wstring(index) + L" on block "
                                         + std::to_wstring(compared))
                                          .c_str());
      }

      /*
       * Saving does NOT change the commander: the two `STA` instructions write to NA%, the copy
       * bound for disk, and TP is left alone. The oracle's live block is checked for that too,
       * because a port that stored them back would still write the right file and would give the
       * NEXT save a different one.
       */
      for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
      {
        Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(layout.tp + index)],
                                        block.bytes[index],
                                        (L"the live commander should be untouched at byte "
                                         + std::to_wstring(index))
                                          .c_str());
      }
      ++compared;
    }

    Logger::WriteMessage(("SVE: " + std::to_string(compared) + " commanders saved and compared byte for byte").c_str());
  }

  /*
   * A save and a load must come back to the same commander, which is this slice's own criterion
   * rather than the oracle's -- and the file that goes in is the one the original would have
   * written, checked above.
   */
  TEST_METHOD(SavingAndLoadingRoundTrips)
  {
    std::uint32_t compared = 0;

    for (const CommanderBlock& block : SampleBlocks())
    {
      const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
      std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> file{};
      Elite::SaveCommander(block, std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name),
                           std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE>(file));

      // Seeded with the block, so that the two bytes the load does NOT overwrite -- the block's
      // own checksum, and nothing else -- start where a round trip would leave them.
      CommanderBlock loaded = block;
      std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> loadedName{};
      const bool accepted =
        Elite::LoadCommander(std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE>(file), loaded,
                             std::span<std::uint8_t, Elite::COMMANDER_NAME_SIZE>(loadedName));

      Assert::IsTrue(accepted, L"a file this code wrote must be one it accepts");
      Assert::IsTrue(loadedName == name, L"the name must come back");

      /*
       * Every byte but three must come back unchanged, and the three are the point of the
       * exercise rather than exceptions to it. The competition flags gain bits 6 and 7 on the way
       * in -- that is what the load is FOR. The second checksum is written by the save. And the
       * first is written by the save and then not loaded at all, because the copy loop stops one
       * byte short of it.
       */
      for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
      {
        if (index == static_cast<std::size_t>(Field::Competition)
            || index == static_cast<std::size_t>(Field::Checksum3Byte)
            || index == static_cast<std::size_t>(Field::ChecksumByte))
        {
          continue;
        }
        Assert::AreEqual<std::uint32_t>(block.bytes[index], loaded.bytes[index],
                                        (L"round trip differs at byte " + std::to_wstring(index)).c_str());
      }

      Assert::IsTrue((loaded.At(Field::Competition) & 0x40u) != 0u,
                     L"loading sets the flag that says the commander came from a file");

      // The block's own checksum byte is never loaded, so it keeps whatever the caller had.
      Assert::AreEqual<std::uint32_t>(block.At(Field::ChecksumByte), loaded.At(Field::ChecksumByte),
                                      L"the checksum byte is not loaded, so the caller's survives");
      ++compared;
    }

    Logger::WriteMessage(("save and load: " + std::to_string(compared) + " round trips").c_str());
  }

  /*
   * 6502: DFAULT's `JSR CHECK / CMP CHK / BNE doitagain`.
   *
   * The branch goes BACKWARDS, so a block whose checksum is wrong spins there for ever -- the
   * game hangs on a black screen rather than reporting anything. That is copy protection, and it
   * is the one behaviour in this slice the port does not reproduce: a hang is not something a
   * caller can handle.
   *
   * What CAN be established is that the two agree on which blocks are acceptable. The shipped
   * routine is run under an instruction budget, and "did not finish" is read as "rejected" --
   * which is exactly what the loop means. So a port that accepted a file the original would hang
   * on, or rejected one it would load, fails here.
   */
  TEST_METHOD(TheGameAndThePortAgreeOnWhichCommandersAreAcceptable)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Layout layout(oracle);

    std::uint32_t accepted = 0;
    std::uint32_t rejected = 0;

    for (const CommanderBlock& block : SampleBlocks())
    {
      // Both a block with the right checksum and the same block with a wrong one, so the test
      // covers each answer rather than whichever the sample happens to give.
      for (const bool corrupt : { false, true })
      {
        const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
        std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> file{};
        Elite::SaveCommander(block, std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name),
                             std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE>(file));

        if (corrupt)
        {
          // One bit, in the byte the checksum is stored in.
          file[Elite::COMMANDER_NAME_SIZE + static_cast<std::size_t>(Field::ChecksumByte)] ^= 0x01u;
        }

        CommanderBlock loaded;
        std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> loadedName{};
        const bool ourAnswer =
          Elite::LoadCommander(std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE>(file), loaded,
                               std::span<std::uint8_t, Elite::COMMANDER_NAME_SIZE>(loadedName));

        Cpu6502 cpu = oracle.Fresh();
        for (std::size_t index = 0; index < file.size(); ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(layout.na + index)] = file[index];
        }
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;

        /*
         * DFAULT returns when both checksums agree and spins for ever when either does not, so
         * "did it return inside the budget" IS the answer. The loop is under a hundred
         * instructions, so anything that leaves it returns long before the budget runs out.
         */
        const auto run = cpu.CallSubroutine(oracle.Label("DFAULT"), 200'000);
        const bool gameGotPast = run.completed;

        Assert::AreEqual(ourAnswer, gameGotPast,
                         (std::wstring(L"the two disagree on a ") + (corrupt ? L"corrupted" : L"sound")
                          + L" commander")
                           .c_str());

        if (ourAnswer)
        {
          ++accepted;
        }
        else
        {
          ++rejected;
        }
      }
    }

    Logger::WriteMessage(("DFAULT: " + std::to_string(accepted) + " commanders accepted, "
                          + std::to_string(rejected) + " rejected -- and the shipped routine hangs on exactly those")
                           .c_str());
    Assert::IsTrue(accepted > 0 && rejected > 0, L"both answers must be exercised");
  }
};

} // namespace GameLogicTests

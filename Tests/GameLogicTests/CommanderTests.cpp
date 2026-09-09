#include "pch.h"


#include "Commander.h"
#include "LookupTables.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Commander;
using Elite::Field;

/*
 * The commander block, saved and loaded (slice 2d).
 *
 * The two checksums are the whole difficulty: both thread a carry through seventy-three steps and
 * the second rotates the accumulator through that carry in the middle of each one. They were
 * swept against the shipped routine over the default block, all-zeros, all-255, a walking bit and
 * a spread of pseudo-random blocks, and that comparison went with the oracle (M6-b-5).
 *
 * What is left is this slice's OWN criterion rather than the original's: a save and a load come
 * back to the same commander, over the same sample of blocks. Four bytes are expected to move and
 * each is the point of the exercise rather than an exception to it -- the competition flags gain
 * bit 6, which is what a load is for, and of the three checksums the save writes, two are read
 * back over the caller's block and the third is not, because the copy loop stops one byte short.
 */
namespace GameLogicTests
{

  namespace
  {
    /// A spread of blocks: the shipped one, the corners, a walking bit, and pseudo-random fill.
    std::vector<Commander> SampleBlocks()
    {
      std::vector<Commander> blocks;

      blocks.push_back(Elite::DefaultCommander());

      Commander zeros;
      blocks.push_back(zeros);

      std::array<std::uint8_t, Elite::COMMANDER_BLOCK_SIZE> allSet{};
      allSet.fill(0xFF);
      const Commander ones = Commander::FromBytes(allSet);
      blocks.push_back(ones);

      // One bit set at a time, through every byte. This is what finds a carry that threads the wrong
      // way: a single bit in byte N changes the result of every step after N, and only if the chain
      // is right.
      for (std::size_t byte = 0; byte < Elite::COMMANDER_BLOCK_SIZE; ++byte)
      {
        for (const std::uint8_t bit : {0x01u, 0x80u})
        {
          std::array<std::uint8_t, Elite::COMMANDER_BLOCK_SIZE> oneBit{};
          oneBit[byte] = static_cast<std::uint8_t>(bit);
          const Commander walking = Commander::FromBytes(oneBit);
          blocks.push_back(walking);
        }
      }

      // And a spread that fills every byte, so the carry chain is exercised at length rather than
      // from a single perturbation.
      std::uint32_t state = 0x1234567u;
      for (int round = 0; round < 64; ++round)
      {
        std::array<std::uint8_t, Elite::COMMANDER_BLOCK_SIZE> noise{};
        for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
        {
          state = state * 1103515245u + 12345u;
          noise[index] = static_cast<std::uint8_t>(state >> 16);
        }
        blocks.push_back(Commander::FromBytes(noise));
      }

      return blocks;
    }
  } // namespace

  TEST_CLASS(TheCommanderBlock)
  {
  public:
    /*
     * A save and a load must come back to the same commander. This was always this slice's own
     * criterion rather than the original's, which is why it outlived the comparison beside it: the
     * three checksums the save writes are checked by the load that reads them back.
     */
    TEST_METHOD(SavingAndLoadingRoundTrips)
    {
      std::uint32_t compared = 0;

      for (const Commander& block : SampleBlocks())
      {
        const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
        std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> file{};
        Elite::SaveCommander(block, std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name),
                             std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE>(file));

        // Seeded with the block, so that the two bytes the load does NOT overwrite -- the block's
        // own checksum, and nothing else -- start where a round trip would leave them.
        Commander loaded = block;
        std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> loadedName{};
        const bool accepted = Elite::LoadCommander(std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE>(file), loaded,
                                                   std::span<std::uint8_t, Elite::COMMANDER_NAME_SIZE>(loadedName));

        Assert::IsTrue(accepted, L"a file this code wrote must be one it accepts");
        Assert::IsTrue(loadedName == name, L"the name must come back");

        /*
         * Every byte but FOUR must come back unchanged, and the four are the point of the exercise
         * rather than exceptions to it. The competition flags gain bit 6 on the way in -- that is
         * what the load is FOR. All three checksums are written by the save; two of them are then
         * loaded back over the caller's block, and the third is not, because DFAULT's copy loop
         * stops one byte short of it.
         */
        for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
        {
          if (index == static_cast<std::size_t>(Field::Competition) || index == static_cast<std::size_t>(Field::Checksum2Byte) ||
              index == static_cast<std::size_t>(Field::Checksum3Byte) || index == static_cast<std::size_t>(Field::ChecksumByte))
          {
            continue;
          }
          Assert::AreEqual<std::uint32_t>(block.ToBytes()[index], loaded.ToBytes()[index],
                                          (L"round trip differs at byte " + std::to_wstring(index)).c_str());
        }

        /*
         * Bit 6 says it came from a file. Bit 7 must not be ADDED, which is the assertion the third
         * checksum earns: the save writes CHK2 = CHK EOR &A9 and the load sets bit 7 when they
         * disagree. Until 2026-09-03 the port did not write CHK2 at all, and this test passed
         * anyway, because it skipped the byte and only looked at bit 6.
         *
         * Not "bit 7 is clear": `ORA #&80` only ever SETS it, so a block that arrives with the bit
         * already on keeps it -- and one of the sample blocks is all 255. What a correct save
         * guarantees is that loading it does not turn the bit on, not that it turns it off.
         */
        Assert::IsTrue((loaded.competition & 0x40u) != 0u, L"loading sets the flag that says the commander came from a file");
        Assert::AreEqual<std::uint32_t>(block.competition & 0x80u, loaded.competition & 0x80u,
                                        L"a correctly saved file is not newly flagged as tampered");

        std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> written{};
        Elite::SaveCommander(block, std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name),
                             std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE>(written));
        Assert::AreEqual<std::uint32_t>(written[8u + static_cast<std::size_t>(Field::Checksum2Byte)], loaded.checksum2,
                                        L"the second checksum is loaded back over the caller's block");

        // The block's own checksum byte is never loaded, so it keeps whatever the caller had.
        Assert::AreEqual<std::uint32_t>(block.checksum, loaded.checksum,
                                        L"the checksum byte is not loaded, so the caller's survives");
        ++compared;
      }

      Logger::WriteMessage(("save and load: " + std::to_string(compared) + " round trips").c_str());
    }

  };

} // namespace GameLogicTests

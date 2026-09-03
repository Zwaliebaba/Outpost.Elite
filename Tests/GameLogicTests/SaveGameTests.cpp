#include "pch.h"

#include "OracleImage.h"

#include "Commander.h"
#include "SaveGame.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::CommanderBlock;
using Elite::CompetitionNumber;
using Elite::Field;
using Elite::KeySource;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Saving and loading a commander, against the game (slice 2d).
 *
 * The C64's `SVE` is the disk access menu rather than a file write, and the only instructions in
 * the whole flow that touch a device are two Kernal calls. Everything before them -- the save
 * count, the three checksums, the competition number -- is arithmetic with an oracle, and this is
 * the comparison of it.
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

/// A store that keeps the bytes in memory, so the flow can be compared without a file system.
class MemoryStore : public Elite::CommanderStore
{
public:
  bool Write(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name,
             std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE> _file) override
  {
    for (std::size_t index = 0; index < _name.size(); ++index)
    {
      name[index] = _name[index];
    }
    for (std::size_t index = 0; index < _file.size(); ++index)
    {
      file[index] = _file[index];
    }
    ++writes;
    return !failWrites;
  }

  bool Read(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>,
            std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE> _outFile) override
  {
    if (failReads)
    {
      return false;
    }
    for (std::size_t index = 0; index < _outFile.size(); ++index)
    {
      _outFile[index] = file[index];
    }
    return true;
  }

  std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name{};
  std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> file{};
  int writes = 0;
  bool failWrites = false;
  bool failReads = false;
};

class ScriptedKeys : public KeySource
{
public:
  explicit ScriptedKeys(std::vector<std::uint8_t> _keys) noexcept : m_keys(std::move(_keys)) {}
  std::uint8_t NextKey() override
  {
    if (m_taken >= m_keys.size())
    {
      m_overrun = true;
      return 'N';
    }
    return m_keys[m_taken++];
  }
  [[nodiscard]] std::size_t Taken() const noexcept { return m_taken; }
  [[nodiscard]] bool Overran() const noexcept { return m_overrun; }

private:
  std::vector<std::uint8_t> m_keys;
  std::size_t m_taken = 0;
  bool m_overrun = false;
};

/// A spread of commanders chosen for the bytes the competition number folds in.
std::vector<CommanderBlock> Commanders()
{
  std::vector<CommanderBlock> blocks;

  blocks.push_back(Elite::DefaultCommander());

  CommanderBlock zero;
  blocks.push_back(zero);

  CommanderBlock full;
  full.bytes.fill(0xFF);
  blocks.push_back(full);

  // Walk the four bytes the number actually reads -- the competition flags, the third cash byte
  // and the high byte of the kill tally -- since everything else reaches it only through CHK.
  for (const std::uint8_t cok : { 0x00, 0x40, 0xC0, 0x5A })
  {
    for (const std::uint8_t cash2 : { 0x00, 0x01, 0x7F, 0xFF })
    {
      for (const std::uint8_t kills : { 0x00, 0x01, 0xA9, 0xFF })
      {
        CommanderBlock block = Elite::DefaultCommander();
        block.At(Field::Competition) = cok;
        block.bytes[static_cast<std::size_t>(Field::Cash) + 2u] = cash2;
        block.bytes[static_cast<std::size_t>(Field::Kills) + 1u] = kills;
        block.At(Field::SaveCount) = static_cast<std::uint8_t>(cok ^ cash2);
        blocks.push_back(block);
      }
    }
  }

  return blocks;
}
} // namespace

TEST_CLASS(SavingACommanderMatchesTheShippedGame)
{
public:
  /*
   * 6502: SV1, run up to the Kernal call.
   *
   * Everything the save does before the bytes leave: `LSR SVC`, the block copy into the file
   * image, all THREE checksums, and the competition number. The run stops at KERNALSETUP, which
   * is the first instruction that would touch hardware -- and by then CHK2 has already been
   * written, four instructions earlier.
   */
  TEST_METHOD(TheSavedFileAndCompetitionNumberMatchTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t sv1 = oracle.Label("SV1");
    const std::uint16_t kernalSetup = oracle.Label("KERNALSETUP");
    const std::uint16_t tp = oracle.Label("TP");
    const std::uint16_t na = oracle.Label("NA%");
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t k = oracle.Label("K");
    const std::uint16_t svc = oracle.Label("SVC");

    static constexpr std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> NAME = { 'B', 'E', 'L', 'L',
                                                                                   13,  0,   0,   0 };
    std::uint32_t compared = 0;

    for (const CommanderBlock& original : Commanders())
    {
      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("GTNMEW"));
      cpu.AddTrap(oracle.Label("DETOK"));
      cpu.AddTrap(oracle.Label("BPRNT"));
      cpu.AddTrap(oracle.Label("TT67"));

      // TRNME runs for real, so the name arrives the way the game puts it there.
      for (std::size_t index = 0; index < NAME.size(); ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(inwk + 5 + index)] = NAME[index];
      }
      for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(tp + index)] = original.bytes[index];
      }
      cpu.memory[svc] = original.At(Field::SaveCount);

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      cpu.pc = sv1;

      bool reached = false;
      for (int step = 0; step < 100'000; ++step)
      {
        if (cpu.pc == kernalSetup)
        {
          reached = true;
          break;
        }
        Assert::IsTrue(cpu.Step(), L"SV1 should not reach an unimplemented opcode");
      }
      Assert::IsTrue(reached, L"SV1 should reach the Kernal call");

      // ---- the port ------------------------------------------------------------------------
      CommanderBlock block = original;
      MemoryStore store;
      const Elite::SaveOutcome outcome = Elite::SaveCommanderTo(store, block, NAME);

      const std::wstring where = Widen("SV1 (save count " + std::to_string(original.At(Field::SaveCount))
                                       + ", flags " + std::to_string(original.At(Field::Competition)) + ")");

      Assert::IsTrue(outcome.written, (where + L": the store should have been written").c_str());

      // 6502: LSR SVC -- halved, and it is the LIVE commander that changes.
      Assert::AreEqual(cpu.memory[svc], block.At(Field::SaveCount), (where + L": the save count").c_str());

      /*
       * The whole file image, which is where the defect was. SaveCommander wrote CHK3 and CHK and
       * stopped; SV1 also writes CHK2 = CHK EOR &A9, four instructions after the competition
       * number is printed. Nothing caught it because LoadCommander only READS CHK2 to set the
       * tampered flag, and a round trip through a port that got it wrong on both sides agreed
       * with itself.
       */
      for (std::size_t index = 0; index < Elite::COMMANDER_FILE_SIZE; ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(na + index)], store.file[index],
                         (where + L": file byte " + std::to_wstring(index)).c_str());
      }

      // 6502: K to K+3 -- the competition number, in the order the stores left them.
      for (std::size_t index = 0; index < 4; ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(k + index)], outcome.competition.value[index],
                         (where + L": competition byte " + std::to_wstring(index)).c_str());
      }

      ++compared;
    }

    Logger::WriteMessage(("SV1: " + std::to_string(compared)
                          + " commanders saved, file byte for byte plus the competition number\n")
                           .c_str());
  }

  /*
   * 6502: YESNO -- and the point of testing it is that it ignores everything else.
   *
   * The original loops on any key that is neither "Y" nor "N", so a routine that treated an
   * unknown key as "no" would let a stray press wipe a commander.
   */
  TEST_METHOD(TheYesNoPromptIgnoresEverythingElse)
  {
    struct Case
    {
      std::vector<std::uint8_t> keys;
      bool expected;
      std::size_t consumed;
    };

    const std::vector<Case> CASES = {
      { { 'Y' }, true, 1 },
      { { 'N' }, false, 1 },
      { { 'A', 'B', ' ', 13, 'Y' }, true, 5 },
      { { 'y', 'n', 'N' }, false, 3 },  // lower case is not accepted, which the game relies on
      { { 27, 'N' }, false, 2 },        // even ESCAPE is just another key here
    };

    for (const Case& item : CASES)
    {
      ScriptedKeys keys(item.keys);
      const bool answer = Elite::AskYesNo(keys);
      Assert::IsFalse(keys.Overran(), L"YESNO asked for more keys than the script holds");
      Assert::AreEqual(item.expected, answer, L"the answer");
      Assert::AreEqual(item.consumed, keys.Taken(), L"how many keys were read");
    }
  }

  /*
   * 6502: JAMESON -- the default commander back over the SAVE IMAGE, not over the live one.
   *
   * Compared against the shipped routine byte for byte, which also pins the fact that the image
   * it writes is a complete file: eight bytes of name and then the block.
   */
  TEST_METHOD(ResettingToTheDefaultMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t na = oracle.Label("NA%");

    Cpu6502 cpu = oracle.Fresh();
    for (std::size_t index = 0; index < Elite::COMMANDER_FILE_SIZE; ++index)
    {
      cpu.memory[static_cast<std::uint16_t>(na + index)] = 0x5C;
    }
    cpu.a = cpu.x = cpu.y = 0;
    cpu.sp = 0xFD;
    const auto run = cpu.CallSubroutine(oracle.Label("JAMESON"), 10'000);
    Assert::IsTrue(run.completed, L"JAMESON should return");

    std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> file{};
    file.fill(0x5C);
    Elite::ResetToDefaultCommander(file);

    for (std::size_t index = 0; index < file.size(); ++index)
    {
      Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(na + index)], file[index],
                       (L"JAMESON: file byte " + std::to_wstring(index)).c_str());
    }
  }

  /*
   * The round trip through a store, including the two ways it fails.
   *
   * A write that the store refuses and a read it cannot satisfy are both reported, and neither is
   * confused with a checksum that does not match -- which is the third failure and the only one
   * the original has an opinion about.
   */
  TEST_METHOD(TheRoundTripThroughAStoreReportsEveryFailure)
  {
    CommanderBlock block = Elite::DefaultCommander();
    const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();

    MemoryStore store;
    std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> loadedName = name;

    const Elite::SaveOutcome saved = Elite::SaveCommanderTo(store, block, name);
    Assert::IsTrue(saved.written, L"the save should succeed");

    CommanderBlock loaded;
    Assert::IsTrue(Elite::LoadCommanderFrom(store, loaded, loadedName), L"the round trip should load");

    // Everything but the checksum byte, which DFAULT's loop stops one short of (§6.14).
    for (std::size_t index = 0; index + 1 < Elite::COMMANDER_BLOCK_SIZE; ++index)
    {
      if (index == static_cast<std::size_t>(Field::Competition)
          || index == static_cast<std::size_t>(Field::Checksum2Byte)
          || index == static_cast<std::size_t>(Field::Checksum3Byte))
      {
        continue; // written by the save and loaded back; the flags are checked below
      }
      Assert::AreEqual(block.bytes[index], loaded.bytes[index],
                       (L"round trip byte " + std::to_wstring(index)).c_str());
    }

    /*
     * Bit 6 says the commander came from a file; bit 7 says the file's CHK2 disagreed with the
     * checksum EORed with &A9. A save that writes CHK2 correctly must leave bit 7 CLEAR -- which
     * is the property the missing store broke, and the reason it was worth finding.
     */
    Assert::AreEqual<std::uint8_t>(0x40, loaded.At(Field::Competition),
                                   L"loaded from a file, and not flagged as tampered");

    store.failReads = true;
    CommanderBlock unread;
    Assert::IsFalse(Elite::LoadCommanderFrom(store, unread, loadedName), L"a read failure is reported");

    store.failReads = false;
    store.failWrites = true;
    CommanderBlock another = Elite::DefaultCommander();
    Assert::IsFalse(Elite::SaveCommanderTo(store, another, name).written, L"a write failure is reported");
  }
};

} // namespace GameLogicTests

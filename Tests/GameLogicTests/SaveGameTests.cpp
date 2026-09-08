#include "pch.h"

#include "NullSeams.h"


#include "Commander.h"
#include "Controls.h"
#include "ExtendedTokens.h"
#include "Rng.h"
#include "SaveGame.h"
#include "StateTokens.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Galaxy.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::CharacterPrinter;
using Elite::Commander;
using Elite::CompetitionNumber;
using Elite::Field;
using Elite::Keyboard;
using Elite::TokenPrinter;

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

    class ScriptedKeys : public Keyboard
    {
    public:
      explicit ScriptedKeys(std::vector<std::uint8_t> _keys) noexcept
        : m_keys(std::move(_keys))
      {
      }

      /// 6502: FLKB, which was `LineEntryEffects`'s until M3-b-3d, and the matrix walk, which the
      /// disk menu never reaches -- `TT217` is the whole of what it reads.
      void Flush() override
      {
        ++flushes;
      }
      [[nodiscard]] bool Held(std::size_t) override
      {
        return false;
      }

      [[nodiscard]] std::uint8_t NextKey() override
      {
        if (m_taken >= m_keys.size())
        {
          /*
           * A port that asks for more keys than the game did is the failure this records, and it has
           * to be allowed to finish so the assertion can report it. RETURN ends a typed line and "N"
           * both answers "are you sure" and leaves the menu, so alternating the two gets out of every
           * loop in this slice.
           */
          m_overrun = true;
          return ((m_extra++ % 2u) == 0u) ? static_cast<std::uint8_t>(13) : static_cast<std::uint8_t>('N');
        }
        return m_keys[m_taken++];
      }
      [[nodiscard]] std::size_t Taken() const noexcept
      {
        return m_taken;
      }
      [[nodiscard]] bool Overran() const noexcept
      {
        return m_overrun;
      }

      int flushes = 0; ///< 6502: FLKB, which `MenuEffects` counted until M3-b-3d

    private:
      std::vector<std::uint8_t> m_keys;
      std::size_t m_taken = 0;
      std::size_t m_extra = 0;
      bool m_overrun = false;
    };

  } // namespace

  TEST_CLASS(SavingACommanderMatchesTheShippedGame)
  {
  public:
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
        {{'Y'}, true, 1},
        {{'N'}, false, 1},
        {{'A', 'B', ' ', 13, 'Y'}, true, 5},
        {{'y', 'n', 'N'}, false, 3}, // lower case is not accepted, which the game relies on
        {{27, 'N'}, false, 2},       // even ESCAPE is just another key here
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
     * The round trip through a store, including the two ways it fails.
     *
     * A write that the store refuses and a read it cannot satisfy are both reported, and neither is
     * confused with a checksum that does not match -- which is the third failure and the only one
     * the original has an opinion about.
     */
    TEST_METHOD(TheRoundTripThroughAStoreReportsEveryFailure)
    {
      Commander block = Elite::DefaultCommander();
      const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();

      MemoryStore store;
      std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> loadedName = name;

      const Elite::SaveOutcome saved = Elite::SaveCommanderTo(store, block, name);
      Assert::IsTrue(saved.written, L"the save should succeed");

      Commander loaded;
      Assert::IsTrue(Elite::LoadCommanderFrom(store, loaded, loadedName), L"the round trip should load");

      // Everything but the checksum byte, which DFAULT's loop stops one short of (§6.14).
      for (std::size_t index = 0; index + 1 < Elite::COMMANDER_BLOCK_SIZE; ++index)
      {
        if (index == static_cast<std::size_t>(Field::Competition) || index == static_cast<std::size_t>(Field::Checksum2Byte) ||
            index == static_cast<std::size_t>(Field::Checksum3Byte))
        {
          continue; // written by the save and loaded back; the flags are checked below
        }
        Assert::AreEqual(block.ToBytes()[index], loaded.ToBytes()[index], (L"round trip byte " + std::to_wstring(index)).c_str());
      }

      /*
       * Bit 6 is the C64's platform stamp, which DFAULT sets on anything it loads; bit 7 says the
       * file's CHK2 disagreed with the checksum EORed with &A9. A save that writes CHK2 correctly
       * must leave bit 7 CLEAR -- which is the property the missing store broke, and the reason it
       * was worth finding.
       */
      Assert::AreEqual<std::uint8_t>(0x40, loaded.competition, L"loaded from a file, and not flagged as tampered");

      store.failReads = true;
      Commander unread;
      Assert::IsFalse(Elite::LoadCommanderFrom(store, unread, loadedName), L"a read failure is reported");

      store.failReads = false;
      store.failWrites = true;
      Commander another = Elite::DefaultCommander();
      Assert::IsFalse(Elite::SaveCommanderTo(store, another, name).written, L"a write failure is reported");
    }
  };

  /*
   * The disk access menu, against the game (slice 2d).
   *
   * SVE is five options wrapped around routines this slice already proved one at a time, so what
   * is left to compare is the SHAPE: which key reaches which leaf, what each leaf prints, how many
   * keys it swallows, what it leaves in the two commanders, and the carry -- which is the only
   * thing SVE actually returns and the least predictable part of it.
   *
   * The shipped routine runs whole. Only the Kernal is stood in for: the two calls that touch a
   * device, the setup around them, and the handful of control-code routines that leave the text
   * system. Everything else -- DETOK, MT26, GTNMEW, TRNME, BPRNT, CHECK, DFAULT, JAMESON, YESNO --
   * is the game's own code, which is why this comparison is worth making at all.
   */
  namespace
  {
    /// 6502: KERNALSVE = &FFD8 and KERNALLOAD = &FFD5. Constants in the source, not labels.
    constexpr std::uint16_t KERNAL_SAVE = 0xFFD8;
    constexpr std::uint16_t KERNAL_LOAD = 0xFFD5;

    /// 6502: TAP% = &CF00 -- the staging area LOD reads into before copying to NA%+8.
    constexpr std::uint16_t TAPE_BUFFER = 0xCF00;

    /// The commander the fixture's device hands back, which is deliberately not the default one.
    Commander FileCommander()
    {
      Commander block = Elite::DefaultCommander();
      block.cash.tenths = (123456);
      block.fuel.tenths = 42;
      block.galaxyNumber = 3;
      block.saveCount = 0x60;
      block.kills.hi = 0x11;
      return block;
    }

    std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> FileImage()
    {
      static constexpr std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> WHOEVER = {'X', 'X', 'X', 13, 0, 0, 0, 0};
      std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> file{};
      Elite::SaveCommander(FileCommander(), WHOEVER, file);
      return file;
    }

    /// The port's side of the Kernal: one file, and the two ways it can go wrong.
    class DeviceStore : public Elite::CommanderStore
    {
    public:
      bool Write(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name,
                 std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE> _file) override
      {
        for (std::size_t index = 0; index < _name.size(); ++index)
        {
          wroteName[index] = _name[index];
        }
        for (std::size_t index = 0; index < _file.size(); ++index)
        {
          wrote[index] = _file[index];
        }
        ++writes;
        return !failDevice;
      }

      bool Read(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _name,
                std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE> _outFile) override
      {
        for (std::size_t index = 0; index < _name.size(); ++index)
        {
          readName[index] = _name[index];
        }
        ++reads;
        if (failDevice)
        {
          return false;
        }
        const auto file = FileImage();
        for (std::size_t index = 0; index < _outFile.size(); ++index)
        {
          _outFile[index] = file[index];
        }
        // 6502: the first byte of the block, which LOD tests for bit 7 and nothing else. Only the
        // FIRST read is spoiled, so a script can pick the wrong file and then the right one.
        if (badFile && reads == 1)
        {
          _outFile[Elite::COMMANDER_NAME_SIZE] = static_cast<std::uint8_t>(_outFile[Elite::COMMANDER_NAME_SIZE] | 0x80u);
        }
        return true;
      }

      std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> wrote{};
      std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> wroteName{};
      std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> readName{};
      int writes = 0;
      int reads = 0;
      bool failDevice = false;
      bool badFile = false;
    };

    /// 6502: DELAY, recorded rather than performed. `FLKB` was here until M3-b-3d and is counted
    /// by `ScriptedKeys`, which is the port that answers it now.
    class MenuEffects : public Elite::Presenter
    {
    public:
      void Present() override {}
      void HoldFlightFrame(std::uint8_t) override {}
      void HoldTitleFrame(std::uint8_t) override {}
      void WaitFrames(std::uint8_t) override
      {
        ++waits;
      }
      int waits = 0;
    };

    /// Every character with the cursor it was printed at, exactly as the docked screens compare.
    struct StampedSink : public Elite::TextSink
    {
      void Put(std::uint8_t _character) override
      {
        const std::uint32_t column = (cursor != nullptr) ? cursor->column : 0u;
        const std::uint32_t row = (cursor != nullptr) ? cursor->row : 0u;
        stamped.push_back(static_cast<std::uint32_t>(_character) | (column << 8) | (row << 16));
      }

      Elite::TextState* cursor = nullptr;
      std::vector<std::uint32_t> stamped;
    };

    /// What one run of the shipped SVE left behind.
    struct ShippedMenu
    {
      bool completed = false;
      bool carry = false;
      std::size_t keysTaken = 0;
      int reads = 0;
      int writes = 0;
      std::vector<std::uint32_t> printed;
      std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> image{};
      std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name{};
      Commander block;
      std::uint8_t disk = 0;
      std::array<std::uint8_t, 4> competition{}; ///< 6502: K to K+3
    };

  } // namespace

} // namespace GameLogicTests

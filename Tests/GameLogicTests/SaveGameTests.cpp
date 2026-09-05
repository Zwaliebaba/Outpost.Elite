#include "pch.h"

#include "OracleImage.h"

#include "Commander.h"
#include "ExtendedTokens.h"
#include "Rng.h"
#include "SaveGame.h"
#include "StateTokens.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::CharacterPrinter;
using Elite::CommanderBlock;
using Elite::CompetitionNumber;
using Elite::Field;
using Elite::KeySource;
using Elite::TokenPrinter;
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
      explicit ScriptedKeys(std::vector<std::uint8_t> _keys) noexcept
        : m_keys(std::move(_keys))
      {
      }
      std::uint8_t NextKey() override
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

    private:
      std::vector<std::uint8_t> m_keys;
      std::size_t m_taken = 0;
      std::size_t m_extra = 0;
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
      for (const std::uint8_t cok : {0x00, 0x40, 0xC0, 0x5A})
      {
        for (const std::uint8_t cash2 : {0x00, 0x01, 0x7F, 0xFF})
        {
          for (const std::uint8_t kills : {0x00, 0x01, 0xA9, 0xFF})
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

      static constexpr std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> NAME = {'B', 'E', 'L', 'L', 13, 0, 0, 0};
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

        const std::wstring where = Widen("SV1 (save count " + std::to_string(original.At(Field::SaveCount)) + ", flags " +
                                         std::to_string(original.At(Field::Competition)) + ")");

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

      Logger::WriteMessage(
        ("SV1: " + std::to_string(compared) + " commanders saved, file byte for byte plus the competition number\n").c_str());
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
        if (index == static_cast<std::size_t>(Field::Competition) || index == static_cast<std::size_t>(Field::Checksum2Byte) ||
            index == static_cast<std::size_t>(Field::Checksum3Byte))
        {
          continue; // written by the save and loaded back; the flags are checked below
        }
        Assert::AreEqual(block.bytes[index], loaded.bytes[index], (L"round trip byte " + std::to_wstring(index)).c_str());
      }

      /*
       * Bit 6 is the C64's platform stamp, which DFAULT sets on anything it loads; bit 7 says the
       * file's CHK2 disagreed with the checksum EORed with &A9. A save that writes CHK2 correctly
       * must leave bit 7 CLEAR -- which is the property the missing store broke, and the reason it
       * was worth finding.
       */
      Assert::AreEqual<std::uint8_t>(0x40, loaded.At(Field::Competition), L"loaded from a file, and not flagged as tampered");

      store.failReads = true;
      CommanderBlock unread;
      Assert::IsFalse(Elite::LoadCommanderFrom(store, unread, loadedName), L"a read failure is reported");

      store.failReads = false;
      store.failWrites = true;
      CommanderBlock another = Elite::DefaultCommander();
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
    CommanderBlock FileCommander()
    {
      CommanderBlock block = Elite::DefaultCommander();
      block.SetCash(123456);
      block.At(Field::Fuel) = 42;
      block.At(Field::GalaxyNumber) = 3;
      block.At(Field::SaveCount) = 0x60;
      block.bytes[static_cast<std::size_t>(Field::Kills) + 1u] = 0x11;
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

    /// 6502: DELAY and FLKB, recorded rather than performed.
    class MenuEffects : public Elite::LineEntryEffects
    {
    public:
      void WaitFrames(std::uint8_t) override
      {
        ++waits;
      }
      void FlushKeyboard() override
      {
        ++flushes;
      }
      int waits = 0;
      int flushes = 0;
    };

    /// The control codes that leave the text system. Every one of them is trapped on the other side.
    class IgnoredControls : public Elite::ControlCodes
    {
    public:
      void Run(std::uint8_t _code) override
      {
        codes.push_back(_code);
      }
      std::vector<std::uint8_t> codes;
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

    std::string Legible(const std::vector<std::uint32_t>& _stamped)
    {
      std::string text;
      for (const std::uint32_t entry : _stamped)
      {
        const std::uint8_t character = static_cast<std::uint8_t>(entry);
        text += (character >= 32 && character < 127) ? static_cast<char>(character) : '.';
      }
      return text;
    }

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
      CommanderBlock block;
      std::uint8_t disk = 0;
      std::array<std::uint8_t, 4> competition{}; ///< 6502: K to K+3
    };

    /*
     * Run SVE with the device and the screen stood in for.
     *
     * The keyboard is answered at `t` rather than at TT217, because SVE and YESNO call the inner
     * entry point directly and MT26 reaches it through the outer one -- so `t` is the only address
     * every key in the menu passes through. TT217's own tail is performed here (Y from YSAV, X from
     * the key) so that a routine entering at either label sees what the real one would leave.
     */
    ShippedMenu RunShippedMenu(const OracleImage& _oracle, const std::wstring& _where, const std::vector<std::uint8_t>& _keys,
                               const CommanderBlock& _live, std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE> _liveName,
                               std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE> _image, bool _useDisk, bool _failDevice,
                               bool _badFile, std::uint8_t _numberWidth)
    {
      constexpr std::uint16_t STOP = 0xFFF9;

      Cpu6502 cpu = _oracle.Fresh();
      const std::uint16_t chpr = _oracle.Label("CHPR");
      const std::uint16_t keyRead = _oracle.Label("t");
      const std::uint16_t ysav = _oracle.Label("YSAV");
      const std::uint16_t na = _oracle.Label("NA%");
      const std::uint16_t name = _oracle.Label("NAME");
      const std::uint16_t tp = _oracle.Label("TP");
      const std::uint16_t disk = _oracle.Label("DISK");
      const std::uint16_t bprnt = _oracle.Label("BPRNT");
      const std::uint16_t k = _oracle.Label("K");

      cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
      // The seams: two of them are the C64's own hardware waits, and the rest are the control-code
      // routines that leave the text system. DOXC rather than MT8, because the port splits MT8 the
      // same way -- the column is the canvas's and the sentence-case flag is the text system's.
      for (const char* seam :
           {"DELAY", "FLKB", "MT9", "NLIN4", "DOXC", "DOYC", "FILEPR", "OTHERFILEPR", "KERNALSETUP", "SETL1", "SWAPPZERO"})
      {
        cpu.AddTrap(_oracle.Label(seam));
      }
      cpu.watch = {_oracle.Label("XC"), _oracle.Label("YC"), 0, 0};

      // The text system's own state, which lives in a loaded block rather than in zero page -- so
      // leaving it alone compares the port against whatever the binary happens to hold.
      for (const char* byte : {"DTW1", "DTW2", "DTW3", "DTW4", "DTW5", "DTW6", "QQ17"})
      {
        cpu.memory[_oracle.Label(byte)] = 0;
      }
      cpu.memory[_oracle.Label("DTW8")] = 0xFF;
      for (std::uint16_t index = 0; index < Elite::CharacterPrinter::BUFFER_SIZE; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(_oracle.Label("BUF") + index)] = 0;
      }

      cpu.memory[_oracle.Label("XC")] = 1;
      cpu.memory[_oracle.Label("YC")] = 1;
      cpu.memory[_oracle.Label("COL2")] = 0;
      cpu.memory[disk] = _useDisk ? 0xFF : 0x00;

      /*
       * 6502: U -- BPRNT's field width, and SV1 never sets it.
       *
       * It is a scratch byte in zero page that ZERO does not clear, so the competition number comes
       * out padded to whatever the last caller of BPRNT left behind. Seeding it here is what makes
       * that dependency a comparison rather than an accident: the scripts vary it, and the port has
       * to be handed the same value.
       */
      cpu.memory[_oracle.Label("U")] = _numberWidth;

      for (std::size_t index = 0; index < Elite::COMMANDER_FILE_SIZE; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(na + index)] = _image[index];
      }
      for (std::size_t index = 0; index < Elite::COMMANDER_NAME_SIZE; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(name + index)] = _liveName[index];
      }
      for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(tp + index)] = _live.bytes[index];
      }

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      const std::uint8_t entrySp = cpu.sp;
      const std::uint16_t ret = static_cast<std::uint16_t>(STOP - 1);
      cpu.memory[static_cast<std::uint16_t>(0x0100 + cpu.sp)] = static_cast<std::uint8_t>(ret >> 8);
      --cpu.sp;
      cpu.memory[static_cast<std::uint16_t>(0x0100 + cpu.sp)] = static_cast<std::uint8_t>(ret & 0xFFu);
      --cpu.sp;
      cpu.pc = _oracle.Label("SVE");

      ShippedMenu run{};

      const auto ReturnFromCall = [&]()
      {
        const std::uint8_t lo = cpu.memory[static_cast<std::uint16_t>(0x0100 + ((cpu.sp + 1u) & 0xFFu))];
        const std::uint8_t hi = cpu.memory[static_cast<std::uint16_t>(0x0100 + ((cpu.sp + 2u) & 0xFFu))];
        cpu.sp = static_cast<std::uint8_t>(cpu.sp + 2u);
        cpu.pc = static_cast<std::uint16_t>((lo | (hi << 8)) + 1);
      };

      for (std::uint32_t step = 0; step < 3'000'000; ++step)
      {
        if (cpu.pc == STOP || cpu.sp == entrySp)
        {
          run.completed = true;
          break;
        }

        /*
         * 6502: K to K+3, read as BPRNT is entered rather than afterwards.
         *
         * BPRNT prints by repeated subtraction FROM K, so by the time it returns the number is gone.
         * A test that read K at the end would compare the port's competition number against zero.
         */
        if (cpu.pc == bprnt)
        {
          for (std::size_t index = 0; index < run.competition.size(); ++index)
          {
            run.competition[index] = cpu.memory[static_cast<std::uint16_t>(k + index)];
          }
        }

        if (cpu.pc == keyRead)
        {
          Assert::IsTrue(run.keysTaken < _keys.size(), (_where + L": the shipped menu asked for more keys than the script holds").c_str());
          // 6502: LDA TRANTABLE,X / LDY YSAV / TAX -- TT217's tail, whichever label was entered.
          cpu.a = _keys[run.keysTaken++];
          cpu.x = cpu.a;
          cpu.y = cpu.memory[ysav];
          ReturnFromCall();
          continue;
        }

        if (cpu.pc == KERNAL_LOAD || cpu.pc == KERNAL_SAVE)
        {
          if (cpu.pc == KERNAL_LOAD)
          {
            ++run.reads;
            const auto file = FileImage();
            for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
            {
              cpu.memory[static_cast<std::uint16_t>(TAPE_BUFFER + index)] = file[Elite::COMMANDER_NAME_SIZE + index];
            }
            if (_badFile && run.reads == 1)
            {
              cpu.memory[TAPE_BUFFER] = static_cast<std::uint8_t>(cpu.memory[TAPE_BUFFER] | 0x80u);
            }
          }
          else
          {
            ++run.writes;
          }
          cpu.c = _failDevice; // 6502: the Kernal sets the carry on failure, and SV1 keeps it in a PHP
          ReturnFromCall();
          continue;
        }

        Assert::IsTrue(cpu.Step(), L"SVE should not reach an unimplemented opcode");
      }

      run.carry = cpu.c;
      run.disk = cpu.memory[disk];
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        if (hit.address == chpr)
        {
          run.printed.push_back(static_cast<std::uint32_t>(hit.a) | (static_cast<std::uint32_t>(hit.watched[0]) << 8) |
                                (static_cast<std::uint32_t>(hit.watched[1]) << 16));
        }
      }
      for (std::size_t index = 0; index < Elite::COMMANDER_FILE_SIZE; ++index)
      {
        run.image[index] = cpu.memory[static_cast<std::uint16_t>(na + index)];
      }
      for (std::size_t index = 0; index < Elite::COMMANDER_NAME_SIZE; ++index)
      {
        run.name[index] = cpu.memory[static_cast<std::uint16_t>(name + index)];
      }
      for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
      {
        run.block.bytes[index] = cpu.memory[static_cast<std::uint16_t>(tp + index)];
      }
      return run;
    }
  } // namespace

  TEST_CLASS(TheDiskAccessMenuMatchesTheShippedGame)
  {
  public:
    /*
     * Every leaf of SVE, and the two ways back into it.
     *
     * The scripts walk each of the five options, both answers to "are you sure", the media toggle
     * (which is the only path that redisplays the menu without an error), a device that refuses a
     * save and one that refuses a load, a file that is not a commander -- and, after each of those
     * failures, one more key. That last key is the point of the whole test: LOD's error paths jump
     * back to SVE without unwinding, so what happens next is decided by a stack frame the player
     * cannot see.
     */
    TEST_METHOD(EveryPathThroughTheMenuMatchesTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }

      using Outcome = Elite::DiskMenuOutcome;

      struct Script
      {
        const char* what;
        std::vector<std::uint8_t> keys;
        Outcome outcome = Outcome::Left;
        std::uint8_t useDisk = 0;
        bool failDevice = false;
        bool badFile = false;
        std::uint8_t numberWidth = 0; ///< 6502: U, which SV1 inherits rather than setting
      };

      const std::vector<Script> SCRIPTS = {
        {"5 leaves", {'5'}, Outcome::Left},
        {"0 leaves, being below the range", {'0'}, Outcome::Left},
        {"a letter leaves", {'A'}, Outcome::Left},
        {"3 toggles the media and redisplays", {'3', '5'}, Outcome::Left},
        {"3 twice comes back to tape", {'3', '3', '5'}, Outcome::Left},
        {"3 from disk", {'3', '5'}, Outcome::Left, true},
        {"4 then N leaves without resetting", {'4', 'N'}, Outcome::Left},
        {"4 then anything then Y resets", {'4', 'Q', '?', 'Y'}, Outcome::Reset},
        {"1 loads a commander", {'1', 'B', 'E', 'L', 'L', 13}, Outcome::Loaded},
        {"1 with nothing typed keeps the name", {'1', 13}, Outcome::Loaded},
        {"1 with ESCAPE still loads", {'1', 'Z', 27}, Outcome::Loaded},
        {"2 saves and waits for a key", {'2', 'B', 'E', 'L', 'L', 13, ' '}, Outcome::Saved},
        {"2 with nothing typed", {'2', 13, ' '}, Outcome::Saved},
        // U wider than the number pads it; U narrower cannot make it shorter, because BPRNT stops
        // suppressing leading zeros once it has printed one digit.
        {"2 with U at eleven", {'2', 'W', 13, ' '}, Outcome::Saved, false, false, false, 11},
        {"2 with U at three", {'2', 'W', 13, ' '}, Outcome::Saved, false, false, false, 3},
        {"a save the device refuses", {'2', 'B', 'E', 'L', 'L', 13, ' ', '5'}, Outcome::Left, false, true},
        {"a load the device refuses, then 5", {'1', 'B', 'E', 'L', 'L', 13, ' ', '5'}, Outcome::Left, false, true},
        {"a load the device refuses, then 4 and N", {'1', 'B', 'E', 'L', 'L', 13, ' ', '4', 'N'}, Outcome::Left, false, true},
        {"a file that is not a commander, then 5", {'1', 'B', 'E', 'L', 'L', 13, ' ', '5'}, Outcome::Left, false, false, true},
        {"a bad file, then a good load", {'1', 'B', 'A', 'D', 13, ' ', '1', 'O', 'K', 13}, Outcome::Loaded, false, false, true},
        {"a bad file, then a save", {'1', 'B', 'A', 'D', 13, ' ', '2', 'Z', 13, ' '}, Outcome::Saved, false, false, true},
      };

      static constexpr std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> LIVE_NAME = {'J', 'A', 'M', 'E', 'S', 'O', 'N', 13};
      std::uint32_t compared = 0;

      for (const Script& script : SCRIPTS)
      {
        const std::wstring where = Widen(std::string("SVE: ") + script.what);

        const OracleImage& oracle = OracleImage::Instance();

        // The live commander and the save image start out different, so a routine that wrote the
        // wrong one of the two would be visible rather than a no-op.
        CommanderBlock live = Elite::DefaultCommander();
        live.SetCash(7770);
        live.At(Field::Fuel) = 55;
        std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> image{};
        CommanderBlock saved = Elite::DefaultCommander();
        saved.At(Field::GalaxyNumber) = 1;
        static constexpr std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> IMAGE_NAME = {'O', 'L', 'D', 13, 0, 0, 0, 0};
        Elite::SaveCommander(saved, IMAGE_NAME, image);

        const ShippedMenu shipped = RunShippedMenu(oracle, where, script.keys, live, LIVE_NAME, image, script.useDisk, script.failDevice,
                                                   script.badFile, script.numberWidth);
        Assert::IsTrue(shipped.completed, (where + L": SVE should return").c_str());

        // ---- the port ------------------------------------------------------------------------
        StampedSink sink;
        Elite::TextState text;
        text.column = 1;
        text.row = 1;
        sink.cursor = &text;

        CommanderBlock portBlock = live;
        std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> portName = LIVE_NAME;
        std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> portImage = image;
        std::array<std::uint8_t, 16> buffer{};

        Elite::Rng rng;
        IgnoredControls controls;
        Elite::CharacterPrinter characters(sink);
        TokenPrinter recursive(characters);
        recursive.SetCursor(&text);
        Elite::SystemSeeds current{};
        Elite::SystemSeeds selected{};
        Elite::StateTokens values(recursive, text, portBlock, portName, current, selected, false);
        recursive.SetValueTokens(&values);
        Elite::ExtendedTokenPrinter extended(characters, recursive, rng, &controls);

        ScriptedKeys keys(script.keys);
        MenuEffects effects;
        DeviceStore store;
        store.failDevice = script.failDevice;
        store.badFile = script.badFile;
        Elite::NumberWorkspace numbers;
        numbers.u = script.numberWidth; // 6502: U, exactly as it was seeded on the other side
        std::uint8_t useDisk = script.useDisk ? std::uint8_t{0xFFu} : std::uint8_t{0};

        Elite::SaveScreen screen{recursive, characters, extended, sink, text, keys, effects, store, numbers};

        const Elite::DiskMenuResult result = Elite::DiskAccessMenu(screen, portBlock, portName, portImage, buffer, useDisk);

        // ---- compare -------------------------------------------------------------------------
        Assert::IsFalse(keys.Overran(), (where + L": the port asked for more keys than the script holds").c_str());
        Assert::AreEqual(shipped.keysTaken, keys.Taken(), (where + L": how many keys were read").c_str());

        Assert::AreEqual(shipped.printed.size(), sink.stamped.size(),
                         (where + L": how many characters were printed -- game \"" + Widen(Legible(shipped.printed)) + L"\", port \"" +
                          Widen(Legible(sink.stamped)) + L"\"")
                           .c_str());
        for (std::size_t index = 0; index < shipped.printed.size(); ++index)
        {
          Assert::AreEqual(shipped.printed[index], sink.stamped[index],
                           (where + L": character " + std::to_wstring(index) + L" -- game \"" + Widen(Legible(shipped.printed)) +
                            L"\", port \"" + Widen(Legible(sink.stamped)) + L"\"")
                             .c_str());
        }

        // 6502: the carry, which is the whole of SVE's return value.
        Assert::AreEqual(shipped.carry, result.newCommander, (where + L": the carry on return").c_str());

        // The leaf the menu reached. The oracle cannot say this directly -- the original returns a
        // carry and nothing else -- so it is the port's reading of which label ran, pinned here so
        // a change that swapped two of them would have to be deliberate.
        Assert::AreEqual(static_cast<int>(script.outcome), static_cast<int>(result.outcome), (where + L": which leaf was reached").c_str());

        // 6502: K to K+3 -- only written by a save, and left alone by everything else.
        if (script.outcome == Outcome::Saved)
        {
          for (std::size_t index = 0; index < shipped.competition.size(); ++index)
          {
            Assert::AreEqual(shipped.competition[index], result.competition.value[index],
                             (where + L": competition byte " + std::to_wstring(index)).c_str());
          }
        }

        // 6502: DISK -- 0 for tape, &FF for disk.
        Assert::AreEqual<std::uint8_t>(shipped.disk, useDisk ? 0xFFu : 0x00u, (where + L": the media flag").c_str());

        // 6502: NA% -- the last saved commander, name and block.
        for (std::size_t index = 0; index < Elite::COMMANDER_FILE_SIZE; ++index)
        {
          Assert::AreEqual(shipped.image[index], portImage[index], (where + L": save image byte " + std::to_wstring(index)).c_str());
        }

        // 6502: NAME and TP -- the commander being played, which only DFAULT writes.
        for (std::size_t index = 0; index < Elite::COMMANDER_NAME_SIZE; ++index)
        {
          Assert::AreEqual(shipped.name[index], portName[index], (where + L": live name byte " + std::to_wstring(index)).c_str());
        }
        for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
        {
          Assert::AreEqual(shipped.block.bytes[index], portBlock.bytes[index],
                           (where + L": live commander byte " + std::to_wstring(index)).c_str());
        }

        // The device saw the same traffic, in the same direction.
        Assert::AreEqual(shipped.reads, store.reads, (where + L": how many reads").c_str());
        Assert::AreEqual(shipped.writes, store.writes, (where + L": how many writes").c_str());

        ++compared;
      }

      Logger::WriteMessage(("SVE: " + std::to_string(compared) + " paths through the disk access menu\n").c_str());
    }
  };

} // namespace GameLogicTests

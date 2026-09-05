#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "LookupTables.h"
#include "PauseScreen.h"

#include <algorithm>
#include <array>
#include <set>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

namespace GameLogicTests
{

  /*
   * Slice 4e: the pause screen, and the thirteen configuration toggles behind it.
   *
   * WHAT THIS SUITE EXISTS TO CATCH is an ordering mistake that nothing else could see. `DKS3`
   * pairs entry Y of `TGINT` with the byte Y after `DAMP`, and the port does not hold those bytes
   * contiguously -- it holds thirteen pointers in what it believes is the assembler's order. The
   * belief is the risk, so it is not asserted anywhere: all 256 key codes are pressed at every one
   * of the thirteen positions and the whole run is compared against the shipped routine's. A
   * pointer in the wrong slot fails on the first key that reaches it.
   */
  TEST_CLASS(ThePauseScreen)
  {
  public:
    /*
     * 6502: DKS3 -- one toggle, swept over every key code and every position.
     *
     * 3,328 cases, and the assertion that matters is not that a byte flipped: it is that the
     * SAME byte flipped on both sides. The oracle's thirteen live at `DAMP` through `MUSILLY` and
     * the port's are wherever six headers put them, so the comparison is position by position.
     */
    TEST_METHOD(TheTogglesMatchDKS3)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t dks3 = oracle.Label("DKS3");
      const std::uint16_t damp = oracle.Label("DAMP");

      std::uint32_t compared = 0;
      std::uint32_t flipped = 0;

      for (std::size_t at = 0; at < Elite::OPTION_COUNT; ++at)
      {
        for (std::uint32_t key = 0; key < 256u; ++key)
        {
          Cpu6502 cpu = oracle.Fresh();
          for (const char* seam : {"BELL", "DELAY", "NOISE", "WSCAN"})
          {
            std::uint16_t address = 0;
            if (oracle.TryLabel(seam, address))
            {
              cpu.AddTrap(address);
            }
          }

          // The thirteen bytes given DIFFERENT values, so a toggle at the wrong position writes a
          // byte the comparison can see rather than one that happened to match.
          std::array<std::uint8_t, Elite::OPTION_COUNT> ours{};
          for (std::size_t byte = 0; byte < Elite::OPTION_COUNT; ++byte)
          {
            const std::uint8_t value = static_cast<std::uint8_t>(0x11u * (byte + 1u));
            ours[byte] = value;
            cpu.memory[static_cast<std::uint16_t>(damp + byte)] = value;
          }

          Elite::OptionBlock block{};
          for (std::size_t byte = 0; byte < Elite::OPTION_COUNT; ++byte)
          {
            block[byte] = &ours[byte];
          }

          cpu.x = static_cast<std::uint8_t>(key);
          cpu.y = static_cast<std::uint8_t>(at);
          const Elite::Testing::RunResult run = cpu.CallSubroutine(dks3, 20'000);
          Assert::IsTrue(run.completed, L"DKS3 returned");

          const bool ourFlip = Elite::ToggleOption(block, static_cast<std::uint8_t>(key), at);

          const std::wstring where = WidenText("DKS3 key " + std::to_string(key) + " at " + std::to_string(at));

          for (std::size_t byte = 0; byte < Elite::OPTION_COUNT; ++byte)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(damp + byte)], ours[byte],
                             (where + L": DAMP+" + std::to_wstring(byte)).c_str());
          }

          if (ourFlip)
          {
            ++flipped;
          }
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(Elite::OPTION_COUNT * 256u, compared, L"the whole sweep ran");

      // Thirteen positions, one key each, and every one of them distinct -- which is also a check
      // that `TGINT` has no repeats, because a repeat would flip two bytes for one key.
      Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(Elite::OPTION_COUNT), flipped,
                                      L"and exactly one key in 256 matched at each position");
    }

    /*
     * 6502: DK6 through nosillytog -- the two loops, which are ten toggles and then three more.
     *
     * The second loop is behind `BIT PATG / BPL nosillytog`, and `BIT` tests BIT 7 -- so a `PATG`
     * of 1 does NOT open it and a `PATG` of 255 does. That is the reason `DKS3` flips with
     * `EOR #&FF`: the option bytes have to be 0 or 255 for the readers that use `BIT` and `BMI`,
     * and half the game reads them with `AND` instead, which would be satisfied by a 1.
     *
     * Swept over every key and both answers to `PATG`, so the three music toggles are shown to be
     * unreachable with it clear and reachable with it set -- and `PATG` is itself one of the ten,
     * so pressing its key inside the loop changes what the rest of the same pass can do.
     */
    TEST_METHOD(TheOptionLoopsMatchDKL4)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t dk6 = oracle.Label("DK6");
      const std::uint16_t nosillytog = oracle.Label("nosillytog");
      const std::uint16_t damp = oracle.Label("DAMP");

      std::uint32_t compared = 0;
      std::uint32_t reachedSecondLoop = 0;

      for (const std::uint8_t patg : {std::uint8_t{0}, std::uint8_t{0xFF}})
      {
        for (std::uint32_t key = 0; key < 256u; ++key)
        {
          Cpu6502 cpu = oracle.Fresh();
          for (const char* seam : {"BELL", "DELAY", "NOISE", "WSCAN"})
          {
            std::uint16_t address = 0;
            if (oracle.TryLabel(seam, address))
            {
              cpu.AddTrap(address);
            }
          }

          std::array<std::uint8_t, Elite::OPTION_COUNT> ours{};
          for (std::size_t byte = 0; byte < Elite::OPTION_COUNT; ++byte)
          {
            const std::uint8_t value = (byte == Elite::OPTION_PATG) ? patg : static_cast<std::uint8_t>(0x11u * (byte + 1u));
            ours[byte] = value;
            cpu.memory[static_cast<std::uint16_t>(damp + byte)] = value;
          }

          Elite::OptionBlock block{};
          for (std::size_t byte = 0; byte < Elite::OPTION_COUNT; ++byte)
          {
            block[byte] = &ours[byte];
          }

          cpu.x = static_cast<std::uint8_t>(key);
          const Elite::Testing::RunResult run = cpu.CallSubroutine(dk6, 200'000, nosillytog);
          Assert::IsTrue(run.completed, L"the loops reached nosillytog");

          const std::uint8_t frames = Elite::ApplyOptionKey(block, static_cast<std::uint8_t>(key));

          const std::wstring where = WidenText("DKL4 key " + std::to_string(key) + " patg " + std::to_string(patg));

          for (std::size_t byte = 0; byte < Elite::OPTION_COUNT; ++byte)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(damp + byte)], ours[byte],
                             (where + L": DAMP+" + std::to_wstring(byte)).c_str());
          }

          if (frames != 0u && key >= Elite::OPTION_KEY_TABLE[Elite::OPTION_COUNT_ALWAYS])
          {
            ++reachedSecondLoop;
          }
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(2u * 256u, compared, L"the whole sweep ran");
      Assert::IsTrue(reachedSecondLoop > 0u, L"and the loop behind PATG was entered");
    }

    /*
     * 6502: FREEZE -- one pass round the pause loop, for every key code.
     *
     * `RDKEY` is trapped, so the key is put into X directly: the original blocks on the keyboard
     * and an interpreter has no keyboard, which is the same reason the port hands one pass back
     * rather than looping. `DELAY` and `BELL` are trapped for the same reason `RunLoopTail`'s were.
     *
     * WHERE IT STOPS is `DK2`, the `RTS` -- which only the resume key reaches. Every other key ends
     * `BNE FREEZE`, so the oracle goes round again and reads the SAME key from the trapped `RDKEY`,
     * and would loop for ever. So the run is stopped at `FREEZE` itself: the second arrival there
     * is the original saying "still paused", and reaching `DK2` instead is "resumed". `DEATH2` is
     * trapped, and hitting that trap is "quit".
     */
    TEST_METHOD(ThePauseLoopMatchesFREEZE)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t freeze = oracle.Label("FREEZE");
      const std::uint16_t damp = oracle.Label("DAMP");
      const std::uint16_t dnoiz = oracle.Label("DNOIZ");
      const std::uint16_t mutokold = oracle.Label("MUTOKOLD");
      const std::uint16_t autoFlag = oracle.Label("auto");
      const std::uint16_t death2 = oracle.Label("DEATH2");
      const std::uint16_t dk7 = oracle.Label("DK7");

      std::uint32_t compared = 0;
      std::set<std::string> outcomes;

      for (const std::uint8_t docking : {std::uint8_t{0}, std::uint8_t{0xFF}})
      {
        for (const std::uint8_t patg : {std::uint8_t{0}, std::uint8_t{0xFF}})
        {
          for (std::uint32_t key = 0; key < 256u; ++key)
          {
            Cpu6502 cpu = oracle.Fresh();
            /*
             * `SETL1` IS NOT TRAPPED, and that is not an oversight. `coffeeex` ends `JMP SETL1`,
             * and a trap pops a return address the `JMP` never pushed -- which unbalances the
             * stack and sends the eventual `RTS` somewhere arbitrary. It is self-modifying code
             * inside the interrupt handler, so in an interpreter it is three harmless stores.
             */
            for (const char* seam : {"BELL", "DELAY", "NOISE", "NOISE2", "WSCAN", "RDKEY", "SOFLUSH", "BDENTRY"})
            {
              std::uint16_t address = 0;
              if (oracle.TryLabel(seam, address))
              {
                cpu.AddTrap(address);
              }
            }
            cpu.AddTrap(death2);

            std::array<std::uint8_t, Elite::OPTION_COUNT> ours{};
            for (std::size_t byte = 0; byte < Elite::OPTION_COUNT; ++byte)
            {
              const std::uint8_t value = (byte == Elite::OPTION_PATG) ? patg : static_cast<std::uint8_t>(0x11u * (byte + 1u));
              ours[byte] = value;
              cpu.memory[static_cast<std::uint16_t>(damp + byte)] = value;
            }

            /*
             * `MUTOKOLD` STARTS EQUAL TO `MUTOK`, which is the only state the game can be in:
             * `MUTOKCH` writes it every time it runs, so the two differ for exactly one pass after
             * the switch moves. Seeding them apart made `MUTOKCH` fire on every key, and its
             * excursion through `stopbd` and `startbd` CLOBBERS X -- so the `CPX #&07` and
             * `CPX #&0D` below it tested a register the music player had overwritten and the quit
             * key stopped working. §6.95's rule, and the failure looked like a port bug for as
             * long as it took to read the trap log.
             */
            std::uint8_t sound = 0;
            std::uint8_t mutokOld = ours[Elite::OPTION_MUTOK];
            cpu.memory[dnoiz] = 0;
            cpu.memory[mutokold] = mutokOld;
            cpu.memory[autoFlag] = docking;

            Elite::OptionBlock block{};
            for (std::size_t byte = 0; byte < Elite::OPTION_COUNT; ++byte)
            {
              block[byte] = &ours[byte];
            }

            // `RDKEY` is trapped, so X is what the loop reads -- and X is what the caller sets.
            cpu.x = static_cast<std::uint8_t>(key);
            const Elite::Testing::RunResult run = cpu.CallSubroutine(freeze, 60'000, dk7);

            const Elite::PausePass pass = Elite::PressPauseKey(block, sound, mutokOld, docking, static_cast<std::uint8_t>(key));

            const std::wstring where =
              WidenText("FREEZE key " + std::to_string(key) + " patg " + std::to_string(patg) + " auto " + std::to_string(docking));

            for (std::size_t byte = 0; byte < Elite::OPTION_COUNT; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(damp + byte)], ours[byte],
                               (where + L": DAMP+" + std::to_wstring(byte)).c_str());
            }
            Assert::AreEqual(cpu.memory[dnoiz], sound, (where + L": DNOIZ").c_str());
            Assert::AreEqual(cpu.memory[mutokold], mutokOld, (where + L": MUTOKOLD").c_str());

            /*
             * STOPPED AT `DK7`, which is exactly one pass: the toggles and `MUTOKCH` have run and
             * the three key tests below it have not. Letting it loop would toggle the same option
             * over and over -- an even number of passes leaves it looking untouched, which is what
             * the sweep saw first.
             *
             * And the outcome is read from the ORACLE'S OWN X, because that is the register `CPX
             * #&07` and `CPX #&0D` compare. It is not always the key that was pressed: `MUTOKCH`
             * runs before this point and its excursion through `startbd` does `LDX #HI(musicstart)`,
             * so on the one pass where the music switch moves, the quit and resume tests look at a
             * byte the music player left behind. Reading X rather than assuming the key is what
             * makes that observable instead of invisible.
             */
            Assert::IsTrue(run.completed, (where + L": one pass reached DK7").c_str());

            const std::uint8_t oracleX = cpu.x;
            const Elite::PauseOutcome expected = (oracleX == Elite::QUIT_KEY)     ? Elite::PauseOutcome::Quit
                                                 : (oracleX == Elite::RESUME_KEY) ? Elite::PauseOutcome::Resumed
                                                                                  : Elite::PauseOutcome::Paused;
            Assert::IsTrue(pass.outcome == expected, (where + L": which way it went").c_str());

            outcomes.insert(std::to_string(static_cast<int>(pass.outcome)) + "/" + std::to_string(static_cast<int>(pass.music)) + "/" +
                            std::to_string(pass.delayFrames) + "/" + std::to_string(sound));
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(2u * 2u * 256u, compared, L"the whole sweep ran");
      Assert::IsTrue(outcomes.size() >= 6u, L"and the resume, the quit, both sound keys and a toggle were reached");
    }
  };

} // namespace GameLogicTests

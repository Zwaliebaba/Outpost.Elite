#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "LookupTables.h"
#include "PauseScreen.h"

#include <array>
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
  };

} // namespace GameLogicTests

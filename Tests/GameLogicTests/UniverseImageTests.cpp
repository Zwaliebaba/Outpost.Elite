#include "pch.h"

#include "Cpu6502.h"
#include "FlightUniverse.h"
#include "OracleImage.h"
#include "UniverseImage.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The universe image itself (Design/Modernize.md slice M0-b).
 *
 * Every other suite uses the image to compare a routine; this one checks the image. Four things,
 * and each is the property a later slice will lean on: the labels the table names all exist in
 * this build, a round trip through the interpreter's memory loses nothing, the hash is stable and
 * notices one byte, and the table agrees with itself -- what it writes is what it reads back.
 */
namespace GameLogicTests
{

  TEST_CLASS(TheUniverseImage)
  {
  public:
    /*
     * A label the table names that this build does not have would come back as address zero and
     * quietly compare zero page against a field. `Where`'s constructor asks for every label through
     * `OracleImage::Label`, which returns zero for an unknown one, so the check is that no cell sits
     * at zero -- the only label at zero page zero would be a bug on both sides.
     */
    TEST_METHOD(EveryCellHasAnAddress)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);

      Universe universe;
      universe.spriteRegistersAreOurs = true; // so the sprite cells are in the table too
      const std::vector<Cell> cells = ImageCells(universe, at);

      Assert::IsTrue(cells.size() > 500u, L"the table is far shorter than the state it describes");

      // 6502: LSX2 then LSY2 -- `PlanetSunState::ball` is one block because `BLINE` indexes across
      // the join, and the one `LSX2` run covers both halves only if the join is where the port says.
      Assert::AreEqual<std::uint32_t>(at.lsx2 + Elite::BALL_HEAP_SIZE, at.lsy2, L"LSY2 is not immediately after LSX2 in this build");
      for (const Cell& cell : cells)
      {
        Assert::AreNotEqual(std::uint16_t{0}, cell.address, (L"cell " + cell.name + L" resolved to address zero").c_str());
        Assert::IsTrue(static_cast<bool>(cell.get), (L"cell " + cell.name + L" has no getter").c_str());
        Assert::IsTrue(static_cast<bool>(cell.set), (L"cell " + cell.name + L" has no setter").c_str());
      }
    }

    /// Seed a universe, write it into memory, read it back into a blank one: same hash, no difference.
    TEST_METHOD(MaterialiseThenAbsorbRoundTrips)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);

      Universe seeded;
      Seed(seeded, 7u);
      seeded.spriteRegistersAreOurs = true;
      for (std::size_t sprite = 0; sprite < Elite::SPRITE_COUNT; ++sprite)
      {
        seeded.video.x[sprite] = static_cast<std::uint16_t>(0x120u + 3u * sprite);
        seeded.video.y[sprite] = static_cast<std::uint8_t>(0x40u + sprite);
      }

      Cpu6502 cpu = oracle.Fresh();
      Materialise(seeded, cpu, at);

      Universe absorbed;
      absorbed.spriteRegistersAreOurs = true;
      Absorb(cpu, absorbed, at);

      Assert::AreEqual(Hash(seeded, at), Hash(absorbed, at), L"a round trip through memory changed the image");
      Assert::IsTrue(Compare(cpu, absorbed, at).empty(), L"the absorbed universe disagrees with the memory it was read from");
      Assert::IsTrue(Compare(cpu, seeded, at).empty(), L"the seeded universe disagrees with the memory it was written to");

      // The sprite x is nine bits and crosses two registers; both halves have to come back.
      for (std::size_t sprite = Elite::FIRST_TRUMBLE_SPRITE; sprite < Elite::SPRITE_COUNT; ++sprite)
      {
        Assert::AreEqual(seeded.video.x[sprite], absorbed.video.x[sprite], L"a sprite's ninth x bit was lost");
      }
    }

    /// Two seeds give one hash; one byte of stardust gives another. The whole replay rests on this.
    TEST_METHOD(TheHashIsStableAndNoticesOneByte)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);

      Universe first;
      Seed(first, 3u);
      Universe second;
      Seed(second, 3u);
      Assert::AreEqual(Hash(first, at), Hash(second, at), L"the same seed hashed differently");

      second.dust.x[5] = static_cast<std::uint8_t>(second.dust.x[5] ^ 0x01u);
      Assert::AreNotEqual(Hash(first, at), Hash(second, at), L"one changed byte of stardust was not noticed");

      // A seeded constant is not state: the Coriolis's address is the same in every universe.
      Universe third;
      Seed(third, 3u);
      Assert::AreEqual(Hash(first, at), Hash(third, at));
    }

    /*
     * The table against the two functions it replaced. `Mirror` and `CompareState` are the bridge
     * now, so this is the bridge against itself -- which is still worth having, because a cell whose
     * getter and setter disagree (a mask applied on one side only) would pass a round trip of the
     * seeded universe and fail here on the memory the game leaves behind.
     */
    TEST_METHOD(WhatIsWrittenIsWhatIsCompared)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);

      Universe universe;
      Seed(universe, 11u);

      Cpu6502 cpu = oracle.Fresh();
      Mirror(universe, cpu, at);
      CompareState(cpu, universe, at, L"straight after Mirror");

      // And one byte the game would have changed shows up as exactly one difference, by name.
      cpu.memory[at.lsp] = static_cast<std::uint8_t>(universe.heaps.lsp + 1u);
      const std::vector<Difference> differences = Compare(cpu, universe, at);
      Assert::AreEqual(std::size_t{1}, differences.size(), L"one changed byte should be one difference");
      Assert::IsTrue(differences.front().name == L"LSP", (L"the difference was " + differences.front().name).c_str());
      Assert::AreEqual(at.lsp, differences.front().address);
    }
  };

} // namespace GameLogicTests

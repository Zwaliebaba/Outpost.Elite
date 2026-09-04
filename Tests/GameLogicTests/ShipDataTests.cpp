#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "LookupTables.h"
#include "ShipBlueprint.h"
#include "ShipSlot.h"

#include <algorithm>
#include <cstdint>
#include <array>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The ship data region, and why it is one region (slice 3a).
 *
 * This suite exists to PIN A FINDING rather than to exercise a routine. The plan's 3a row lists
 * the motion and slot routines and says nothing about the blueprints, and the ledger files them
 * under 3b because that is what they are ABOUT -- the vertices and edges a ship is drawn from.
 * But `MVEIT` reads byte 15 of the blueprint on every iteration to clamp acceleration, and
 * `NWSHP` reads bytes 5, 14 and 19 before a ship exists at all, so 3a TOUCHES them and cannot be
 * compared against the shipped game without them. That is §6.12's pattern for the seventh time.
 *
 * Extracting them then turned up the reason they cannot be thirty-three arrays, which is what
 * most of this file measures.
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

/// Every distinct blueprint the pointer table names, in address order. Taken from `XX21` rather
/// than from the `SHIP_` labels, because the labels are the thing this file finds unreliable.
std::vector<std::uint16_t> BlueprintsInAddressOrder()
{
  std::set<std::uint16_t> distinct;
  for (int type = 1; type <= Elite::SHIP_TYPE_COUNT; ++type)
  {
    const std::uint16_t address = Elite::BlueprintAddress(static_cast<std::uint8_t>(type));
    if (address != 0)
    {
      distinct.insert(address);
    }
  }
  return { distinct.begin(), distinct.end() };
}
} // namespace

TEST_CLASS(TheShipDataRegion)
{
public:
  /// The pointer table, against the shipped one, for every type a ship slot can hold.
  TEST_METHOD(EveryBlueprintAddressIsTheOneXX21Holds)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Cpu6502 cpu = oracle.Fresh();
    const std::uint16_t table = oracle.Label("XX21");

    Assert::AreEqual<std::uint16_t>(Elite::SHIP_DATA_BASE, table,
                                    L"the region's base is where XX21 sits");

    std::uint32_t carried = 0;
    for (int type = 1; type <= Elite::SHIP_TYPE_COUNT; ++type)
    {
      // 6502: ASL A / TAY / LDA XX21-2,Y and XX21-1,Y -- the table is indexed from one.
      const std::uint16_t entry = static_cast<std::uint16_t>(table + (type - 1) * 2);
      const std::uint16_t expected =
        static_cast<std::uint16_t>(cpu.memory[entry] | (cpu.memory[entry + 1] << 8));

      const std::wstring where = Widen("ship type " + std::to_string(type));
      Assert::AreEqual(expected, Elite::BlueprintAddress(static_cast<std::uint8_t>(type)),
                       (where + L": the blueprint address").c_str());

      if (expected != 0)
      {
        ++carried;
        Assert::IsTrue(expected >= Elite::SHIP_DATA_BASE
                         && expected < Elite::SHIP_DATA_BASE + Elite::SHIP_DATA.size(),
                       (where + L": the blueprint is inside the extracted region").c_str());
      }
    }

    // A count rather than a range: a table that had come out all zeroes would satisfy every
    // assertion above, and "every one of the 33 types carries a blueprint" is the fact worth
    // pinning -- there is no hole in this build's table.
    Assert::AreEqual<std::uint32_t>(Elite::SHIP_TYPE_COUNT, carried,
                                    L"every ship type carries a blueprint");

    /*
     * PAST THE TABLE IS NOT EMPTY, IT IS `E%`, and this assertion is the one that cost the most
     * to learn. The first draft of this suite walked types 1 to 39 on the assumption that the
     * table was as long as the ship types the SERIES has; it is 33 long, because the source says
     * `NTY=33:D%=&D000:E%=D%+2*NTY`. Types 35, 38 and 39 read E%'s default-flag bytes and return
     * them as addresses -- 1, 24865 and 41120 -- which are zero page and the middle of two code
     * blocks, and which are plausible enough that the first thing they produced was a search for
     * a blueprint outside the extracted region rather than a suspicion of the index.
     */
    for (const int beyond : { Elite::SHIP_TYPE_COUNT + 1, Elite::SHIP_TYPE_COUNT + 2, 39, 255 })
    {
      Assert::AreEqual<std::uint16_t>(0, Elite::BlueprintAddress(static_cast<std::uint8_t>(beyond)),
                                      L"a type this build does not carry has no blueprint");
    }
    Assert::AreEqual<std::uint16_t>(0, Elite::BlueprintAddress(0), L"and nor does the empty slot");
  }

  /*
   * TWO BLUEPRINTS OVERRUN THEIR NEIGHBOUR, and this is the assertion the extraction's shape
   * rests on. The header says how long a blueprint is -- twenty bytes, then `(XX0),8` of
   * vertices, four times `(XX0),9` of edges and `(XX0),12` of faces -- and for thirty of the
   * thirty-three that is exactly the distance to the next blueprint.
   *
   * For the splinter and the Thargon it is 24 and 60 bytes MORE, so their edges and faces are
   * read out of the ship that follows them. Both are also the only two blueprints in the build
   * with no `SHIP_x_EDGES` label, so the label set cannot settle it either.
   *
   * This is asserted rather than worked around because a port that sliced the region into
   * per-ship arrays would truncate those two and be wrong in a way no other test would notice --
   * the ships would simply lose their far edges. Keeping the region whole and indexing by address
   * makes the overrun reproduce, which is what ADR-001 asks for.
   */
  TEST_METHOD(TheHeaderExtentAgreesWithTheLayoutExceptWhereItFamouslyDoesNot)
  {
    if (OracleMissing())
    {
      return;
    }

    const std::vector<std::uint16_t> blueprints = BlueprintsInAddressOrder();
    Assert::AreEqual<std::size_t>(33, blueprints.size(), L"thirty-three distinct blueprints");

    std::vector<std::string> disagreements;
    for (std::size_t index = 0; index + 1 < blueprints.size(); ++index)
    {
      const std::uint16_t start = blueprints[index];
      const int gap = static_cast<int>(blueprints[index + 1]) - static_cast<int>(start);
      const int extent = static_cast<int>(Elite::ShipBlueprintExtent(start));

      if (gap != extent)
      {
        disagreements.push_back("blueprint at " + std::to_string(start) + ": header says " +
                                std::to_string(extent) + ", gap is " + std::to_string(gap) +
                                " (" + std::to_string(gap - extent) + ")");
      }
    }

    for (const std::string& line : disagreements)
    {
      Logger::WriteMessage((line + "\n").c_str());
    }

    // Three, and not fewer: if the data ever stopped disagreeing, the reasoning behind extracting
    // one region rather than thirty-three arrays would have changed and should be re-read.
    Assert::AreEqual<std::size_t>(3, disagreements.size(),
                                  L"three blueprints whose header disagrees with the layout");

    // And the two that overrun do so by the measured amount, so a change of ship data is a test
    // failure with a number in it rather than a count that happens to stay at three.
    const std::vector<std::uint16_t> OVERRUNNING = { 54643, 60349 }; // the splinter and the Thargon
    for (const std::uint16_t start : OVERRUNNING)
    {
      const auto found = std::find(blueprints.begin(), blueprints.end(), start);
      Assert::IsTrue(found != blueprints.end(), L"the overrunning blueprint is still there");
      const int gap = static_cast<int>(*(found + 1)) - static_cast<int>(start);
      Assert::IsTrue(Elite::ShipBlueprintExtent(start) > gap,
                     Widen("blueprint at " + std::to_string(start) + " still overruns").c_str());
    }
  }

  /// The region reads back byte for byte, and outside it reads zero rather than running off.
  TEST_METHOD(TheRegionIsAddressedTheWayTheGameAddressesIt)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Cpu6502 cpu = oracle.Fresh();

    for (std::size_t offset = 0; offset < Elite::SHIP_DATA.size(); ++offset)
    {
      const std::uint16_t address = static_cast<std::uint16_t>(Elite::SHIP_DATA_BASE + offset);
      if (cpu.memory[address] != Elite::ShipByte(address))
      {
        Assert::Fail(Widen("ship data differs at " + std::to_string(address)).c_str());
      }
    }

    Assert::AreEqual<std::uint8_t>(0, Elite::ShipByte(Elite::SHIP_DATA_BASE - 1),
                                   L"below the region reads zero");
    Assert::AreEqual<std::uint8_t>(
      0, Elite::ShipByte(static_cast<std::uint16_t>(Elite::SHIP_DATA_BASE + Elite::SHIP_DATA.size())),
      L"and above it");

    // 6502: E% -- the per-type default flags NWSHP ORs into NEWB. Inside the region, which is why
    // it does not need extracting separately.
    Assert::AreEqual<std::uint16_t>(oracle.Label("E%"), Elite::SHIP_DEFAULT_FLAGS,
                                    L"E% is where the constant says");
    Assert::AreEqual(cpu.memory[oracle.Label("E%")], Elite::ShipByte(Elite::SHIP_DEFAULT_FLAGS),
                     L"and reads back through the region");
  }
};

/*
 * The two arithmetic routines 3a needs and phase 1 does not have.
 *
 * `TIDY` calls `NORM` every sixteenth iteration of the main loop to stop a ship's orientation
 * vectors drifting out of shape as `MVEIT`'s rounding accumulates, and `MV40` -- the path a
 * planet or a sun takes through `MVEIT` -- reaches `MULT3`. Neither appears in the plan's 3a row,
 * which is the same §6.12 omission as the blueprints.
 */
/*
 * The bubble's shape, against the assembled layout.
 *
 * Constants rather than behaviour, and worth a test because both of them are the kind that a port
 * gets from the wrong place. `NOSH` appears as both 10 and 20 in the upstream `original-sources`
 * listings, which serve several versions of the game, so grepping them gives whichever comes
 * first; `NI%` is 37 and the obvious guess from a 36-byte-looking workspace is 36. Neither is
 * settled by reading, and the binary settles both.
 */
TEST_CLASS(TheBubblesShape)
{
public:
  TEST_METHOD(TheSlotCountAndBlockSizeAreWhatTheBinaryLaysOut)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();

    /*
     * 6502: FRIN is NOSH + 1 bytes -- one per slot plus the terminator the free-slot scan stops
     * on -- and MANY is the next thing in memory. So the gap between the two labels IS the slot
     * count, measured rather than taken from a source file.
     */
    const std::uint16_t frin = oracle.Label("FRIN");
    const std::uint16_t many = oracle.Label("MANY");
    Assert::AreEqual<std::uint16_t>(Elite::MAX_SHIPS + 1u, static_cast<std::uint16_t>(many - frin),
                                    L"FRIN holds one byte per slot plus a terminator");

    // 6502: UNIV -- two bytes a slot, and nothing else between it and the next label.
    Assert::AreEqual<std::size_t>(Elite::MAX_SHIPS, std::size_t{ Elite::MAX_SHIPS },
                                  L"the bubble holds as many blocks as there are slots");

    // 6502: INWK is at zero page 9 and is NI% bytes, so it ends at 9 + 37.
    Assert::AreEqual<std::uint16_t>(9, oracle.Label("INWK"), L"INWK is where the port assumes");
    Assert::AreEqual<std::size_t>(Elite::SHIP_BLOCK_SIZE, Elite::ShipBlock{}.bytes.size(),
                                  L"a ship block is NI% bytes");

    // The counter is indexed by ship type, so it has to reach the last one.
    Elite::Bubble bubble;
    Assert::AreEqual<std::size_t>(Elite::SHIP_TYPE_COUNT + 1u, bubble.counts.size(),
                                  L"MANY is indexed by type, so type 33 must fit");
  }

  /// 6502: GINF -- slot to block, and the bound the original does not have.
  TEST_METHOD(EverySlotHasItsOwnBlockAndNothingBeyondThemDoes)
  {
    Elite::Bubble bubble;

    std::set<Elite::ShipBlock*> distinct;
    for (std::uint8_t slot = 0; slot < Elite::MAX_SHIPS; ++slot)
    {
      Elite::ShipBlock* block = Elite::SlotBlock(bubble, slot);
      Assert::IsNotNull(block, L"every slot in range has a block");
      distinct.insert(block);
    }
    Assert::AreEqual<std::size_t>(Elite::MAX_SHIPS, distinct.size(), L"and they are all different");

    Assert::IsNull(Elite::SlotBlock(bubble, Elite::MAX_SHIPS), L"one past the last slot has none");
    Assert::IsNull(Elite::SlotBlock(bubble, 255), L"and nor does anything beyond it");
  }
};

TEST_CLASS(TheMotionArithmetic)
{
public:
  /*
   * 6502: NORM, over a sweep of vectors.
   *
   * The sweep is deliberately coarse and deliberately includes the awkward values rather than
   * being random: zero (which makes the square root zero and the division degenerate), the
   * sign-magnitude boundary at 128, and the largest components, because those are where a port
   * that got the carry wrong differs and the middle of the range is where it agrees.
   */
  TEST_METHOD(TheNormaliserMatchesNORM)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t xx15 = oracle.Label("XX15");
    const std::uint16_t norm = oracle.Label("NORM");

    const std::uint8_t VALUES[] = { 0, 1, 2, 31, 32, 63, 96, 127, 128, 129, 160, 200, 254, 255 };

    std::uint32_t compared = 0;
    for (const std::uint8_t x : VALUES)
    {
      for (const std::uint8_t y : VALUES)
      {
        for (const std::uint8_t z : VALUES)
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.memory[xx15] = x;
          cpu.memory[static_cast<std::uint16_t>(xx15 + 1)] = y;
          cpu.memory[static_cast<std::uint16_t>(xx15 + 2)] = z;

          const Elite::Testing::RunResult run = cpu.CallSubroutine(norm);
          Assert::IsTrue(run.completed, L"NORM returned");

          std::array<std::uint8_t, 3> vector = { x, y, z };
          Elite::MathWorkspace work;
          Elite::Normalise(work, vector);

          const std::wstring where = Widen("NORM(" + std::to_string(x) + ", " + std::to_string(y)
                                           + ", " + std::to_string(z) + ")");
          Assert::AreEqual(cpu.memory[xx15], vector[0], (where + L": x").c_str());
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(xx15 + 1)], vector[1],
                           (where + L": y").c_str());
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(xx15 + 2)], vector[2],
                           (where + L": z").c_str());
          ++compared;
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(14u * 14u * 14u, compared, L"the whole sweep ran");
  }

  /// 6502: MULT3 -- K(4) = (A P+1 P) * Q, over a sweep that includes the zero divisor MU5 exits on.
  TEST_METHOD(TheWideMultiplyMatchesMULT3)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t mult3 = oracle.Label("MULT3");
    const std::uint16_t pp = oracle.Label("P");
    const std::uint16_t qq = oracle.Label("Q");
    const std::uint16_t kk = oracle.Label("K");

    const std::uint8_t VALUES[] = { 0, 1, 3, 64, 127, 128, 129, 200, 255 };

    std::uint32_t compared = 0;
    for (const std::uint8_t a : VALUES)
    {
      for (const std::uint8_t p1 : VALUES)
      {
        for (const std::uint8_t p0 : VALUES)
        {
          for (const std::uint8_t q : VALUES)
          {
            Cpu6502 cpu = oracle.Fresh();
            cpu.memory[pp] = p0;
            cpu.memory[static_cast<std::uint16_t>(pp + 1)] = p1;
            cpu.memory[qq] = q;
            cpu.a = a;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(mult3);
            Assert::IsTrue(run.completed, L"MULT3 returned");

            Elite::MathWorkspace work;
            work.p = p0;
            work.p1 = p1;
            work.q = q;
            Elite::MultiplySignedToK(work, a);

            const std::wstring where =
              Widen("MULT3(" + std::to_string(a) + " " + std::to_string(p1) + " "
                    + std::to_string(p0) + " * " + std::to_string(q) + ")");
            for (int byte = 0; byte < 4; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(kk + byte)],
                               work.k[byte],
                               (where + L": K+" + std::to_wstring(byte)).c_str());
            }
            ++compared;
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(9u * 9u * 9u * 9u, compared, L"the whole sweep ran");
  }
};

} // namespace GameLogicTests

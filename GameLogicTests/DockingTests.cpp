#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t CHECK_DOCKING_ALIGNMENT = 0x2D0F;
constexpr std::uint16_t STATION = 0x20F5;          // commanderFileList, free for a made-up station's slot
constexpr std::uint16_t STATION_SPIN_ANGLE = 0x0E; // in the slot
constexpr std::uint16_t STATION_SPIN = 0x100;
constexpr std::uint16_t TOLERANCE = 0x20;

// The player's angles, and the station's ship type.
struct Approach
{
  std::uint16_t pitch;
  std::uint16_t yaw;
  std::uint16_t roll;
  std::uint8_t type;
};

} // namespace

// Constructed inputs for docking by hand (plan §6.3): the alignments no replay flies.
TEST_CLASS(DockingTests)
{
public:
  // Either pitch, the roll against the spin and half a turn from it, a type 0 station's negated spin, and
  // misses at each test.
  TEST_METHOD(CheckDockingAlignmentAgreesOnEveryAlignment)
  {
    ComparisonRig rig("CheckDockingAlignment");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    ram.Write16(data, static_cast<std::uint16_t>(STATION + STATION_SPIN_ANGLE), STATION_SPIN);
    const std::initializer_list<Approach> approaches = {{0x010, 0x400, 0x700, 0}, {0x7F0, 0x3F0, 0x300, 0}, {0x000, 0x400, 0x500, 0},
                                                        {0x400, 0x000, 0x100, 2}, {0x410, 0x7F0, 0x500, 2}, {0x200, 0x000, 0x000, 2},
                                                        {0x000, 0x200, 0x000, 2}, {0x400, 0x400, 0x100, 2}};
    for (const Approach& approach : approaches)
    {
      ram.Write16(data, DS.playerPitchAngle.offset, approach.pitch);
      ram.Write16(data, DS.playerYawAngle.offset, approach.yaw);
      ram.Write16(data, DS.playerRollAngle.offset, approach.roll);
      ram.Write8(data, STATION, static_cast<std::uint8_t>(approach.type << 1));
      rig.Call(CHECK_DOCKING_ALIGNMENT, {.bx = TOLERANCE, .di = STATION});
    }
    rig.AssertAllAgreed(CHECK_DOCKING_ALIGNMENT, approaches.size());
  }
};

} // namespace GameLogicTests

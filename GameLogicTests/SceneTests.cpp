#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"

#include <array>
#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t PROJECT_VERTICES = 0x2340;
constexpr std::uint16_t RUN_VERTEX_PROGRAM = 0x3A40;
constexpr std::uint16_t DRAW_VISIBLE_FACES = 0x3AB3;
constexpr std::uint16_t CHECK_SHIP_IN_RANGE = 0x3BEA;
constexpr std::uint16_t TRANSFORM_SHIP = 0x3C7E;
constexpr std::uint16_t TRANSFORM_AND_DRAW_OBJECTS = 0x3D25;
constexpr std::uint16_t TRANSFORM_TO_VIEW_WITH_BLIP = 0x3ED7;
constexpr std::uint16_t TRANSFORM_TO_VIEW = 0x3EE3;
constexpr std::uint16_t DRAW_SUN_OR_PLANET = 0x3F4F;

constexpr std::uint16_t SLOT_BYTES = 0x40;
// At the title screen the turning ship is in slot 2, the station's, and every other slot is empty.
constexpr std::size_t TITLE_SHIP = 2;
constexpr std::size_t SPARE = 4;
constexpr std::size_t DEBRIS = 21;

// Byte 0 of a slot: the type in bits 1-5, active in bit 0.
constexpr std::uint8_t SUN = 0x1E << 1 | 1;
constexpr std::uint8_t PLANET = 0x1F << 1 | 1;
constexpr std::uint8_t CORIOLIS = 1 << 1 | 1;
constexpr std::uint8_t THARGON = 0x07 << 1 | 1;
constexpr std::uint8_t SPLINTER = 0x0B << 1 | 1;
constexpr std::uint8_t BARREL = 0x11 << 1 | 1;
constexpr std::uint8_t ESCAPE_POD = 0x15 << 1 | 1;

[[nodiscard]] std::uint16_t Slot(std::size_t _index) noexcept
{
  return static_cast<std::uint16_t>(Elite::DS.shipSlots.offset + _index * SLOT_BYTES);
}

[[nodiscard]] std::uint16_t At(std::uint16_t _base, std::uint16_t _bytes) noexcept
{
  return static_cast<std::uint16_t>(_base + _bytes);
}

void PutByte(ComparisonRig& _rig, std::uint16_t _offset, std::uint8_t _value)
{
  _rig.Host().Ram().Write8(Elite::DataSegment(_rig.Program()), _offset, _value);
}

void PutWord(ComparisonRig& _rig, std::uint16_t _offset, std::uint16_t _value)
{
  _rig.Host().Ram().Write16(Elite::DataSegment(_rig.Program()), _offset, _value);
}

void CopySlot(ComparisonRig& _rig, std::size_t _from, std::size_t _to)
{
  const std::uint16_t data = Elite::DataSegment(_rig.Program());
  for (std::uint16_t index = 0; index < SLOT_BYTES; ++index)
  {
    PutByte(_rig, At(Slot(_to), index), _rig.Host().Ram().Read8(data, At(Slot(_from), index)));
  }
}

// A slot's 16-bit position, its 24-bit high bytes the sign extension, and its view position.
void PlaceSlot(ComparisonRig& _rig, std::size_t _index, std::int16_t _x, std::int16_t _y, std::int16_t _z)
{
  const std::array<std::int16_t, 3> position = {_x, _y, _z};
  for (std::uint16_t axis = 0; axis < 3; ++axis)
  {
    const auto value = static_cast<std::uint16_t>(position[axis]);
    PutByte(_rig, At(Slot(_index), static_cast<std::uint16_t>(1 + axis)), static_cast<std::uint8_t>(position[axis] < 0 ? 0xFF : 0));
    PutWord(_rig, At(Slot(_index), static_cast<std::uint16_t>(4 + axis * 2)), value);
    PutWord(_rig, At(Slot(_index), static_cast<std::uint16_t>(0x10 + axis * 2)), value);
  }
}

// The original ran each of _offsets while the routine at _entry was compared with it.
void AssertExecuted(ComparisonRig& _rig, std::uint16_t _entry, std::initializer_list<std::uint16_t> _offsets)
{
  const auto& hooks = _rig.Host().Native().Hooks();
  const auto found = hooks.find(Machine::Memory::Linear(_rig.Program().loadSegment, _entry));
  Assert::IsTrue(found != hooks.end(), L"the routine is ported");
  for (const std::uint16_t offset : _offsets)
  {
    Assert::IsTrue(found->second.executed.Contains(offset), (L"reached CS:" + std::to_wstring(offset)).c_str());
  }
}

} // namespace

// Constructed inputs for the ported 3d routines (plan §6.3), from the title screen's state: what the
// replays do not reach.
TEST_CLASS(SceneTests)
{
public:
  // A vertex nearer than nearPlaneZ (50) gets x = 8000h; one 256 times further off-axis than away
  // overflows both divides, which take a memory operand, so the trap saturates only AL.
  TEST_METHOD(ProjectVerticesAgreesNearThePlaneAndThroughTheTrap)
  {
    ComparisonRig rig("ProjectVertices");
    const std::array<std::array<std::uint16_t, 3>, 4> vertices = {
      {{100, 0xFFCE, 10}, {0x7000, 0x8000, 60}, {300, 0xFF38, 400}, {0xFFFF, 1, 0x7FFF}}};
    for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
    {
      for (std::size_t axis = 0; axis < 3; ++axis)
      {
        PutWord(rig, At(Elite::DS.vertexBuffer.At(vertex), static_cast<std::uint16_t>(axis * 2)), vertices[vertex][axis]);
      }
    }
    PutWord(rig, Elite::DS.projectedVertexCount.offset, static_cast<std::uint16_t>(vertices.size()));
    rig.Call(PROJECT_VERTICES, {.si = 0x1234});
    PutWord(rig, Elite::DS.projectedVertexCount.offset, 0);
    rig.Call(PROJECT_VERTICES, {.si = 0x1234});
    rig.AssertAllAgreed(PROJECT_VERTICES, 2);
    AssertExecuted(rig, PROJECT_VERTICES, {0x23B2, 0x23B6});
  }

  // A count byte, then ops: the vertex in bits 0-5, and 00h load, 40h store, 80h average, C0h add. Each
  // program ends on a different op; the last is empty.
  TEST_METHOD(RunVertexProgramAgreesOnEveryOp)
  {
    ComparisonRig rig("RunVertexProgram");
    const std::array<std::uint16_t, 18> coordinates = {0x7000, 0x9000, 0xFFFB, 3,      0x8001, 0x7FFF, 0x1234, 0xFEDC, 0x0001,
                                                       0xFFFF, 0x4000, 0x4000, 0xC000, 0x0002, 0x0101, 0x8000, 0x7FFE, 0x0011};
    for (std::size_t index = 0; index < coordinates.size(); ++index)
    {
      PutWord(rig, At(Elite::DS.vertexBuffer.At(1), static_cast<std::uint16_t>(index * 2)), coordinates[index]);
    }
    const std::uint16_t program = Elite::DS.commanderFileList.offset;
    const std::vector<std::vector<std::uint8_t>> programs = {{5, 0x01, 0xC2, 0x83, 0x44, 0xC3}, {1, 0x02}, {2, 0x41, 0x82}, {0}};
    for (const std::vector<std::uint8_t>& bytes : programs)
    {
      std::uint16_t offset = program;
      for (const std::uint8_t byte : bytes)
      {
        PutByte(rig, offset, byte);
        offset = At(offset, 1);
      }
      rig.Call(RUN_VERTEX_PROGRAM, {.bx = 0x1111, .dx = 0x8222, .si = program, .bp = 0x7333});
    }
    rig.AssertAllAgreed(RUN_VERTEX_PROGRAM, programs.size());
    AssertExecuted(rig, RUN_VERTEX_PROGRAM, {0x3A69, 0x3A6B, 0x3A6E, 0x3A71, 0x3A73, 0x3A7E, 0x3A9A});
  }

  TEST_METHOD(DrawVisibleFacesAgreesWithNoFaces)
  {
    ComparisonRig rig("DrawVisibleFaces");
    rig.Call(DRAW_VISIBLE_FACES, {.cx = 0, .si = 0x1234});
    rig.AssertAllAgreed(DRAW_VISIBLE_FACES, 1);
    AssertExecuted(rig, DRAW_VISIBLE_FACES, {0x3AB7});
  }

  // A ship whose x is 65536 more than its low word says: IsObjectNear calls it far and erases its blip.
  TEST_METHOD(CheckShipInRangeAgreesForAFarShip)
  {
    ComparisonRig rig("CheckShipInRange");
    CopySlot(rig, TITLE_SHIP, SPARE);
    PutByte(rig, At(Slot(SPARE), 1), 1);
    rig.Call(CHECK_SHIP_IN_RANGE, {.di = Slot(SPARE)});
    rig.AssertAllAgreed(CHECK_SHIP_IN_RANGE, 1);
    AssertExecuted(rig, CHECK_SHIP_IN_RANGE, {0x3BE5, 0x3BE8, 0x3BE9});
  }

  // The left, rear and right views turn (x, z) by -viewAngle. The blip is a debris slot's, which
  // UpdateScannerBlip skips.
  TEST_METHOD(TransformToViewAgreesInEveryView)
  {
    ComparisonRig rig("TransformToView");
    const std::initializer_list<std::uint16_t> views = {0x200, 0x400, 0x600};
    for (const std::uint16_t view : views)
    {
      PutWord(rig, Elite::DS.viewAngle.offset, view);
      rig.Call(TRANSFORM_TO_VIEW, {.ax = 0x1234, .bx = 0xFEDC, .cx = 0x0400});
      rig.Call(TRANSFORM_TO_VIEW_WITH_BLIP, {.ax = 0xF000, .bx = 0x0100, .cx = 0x8000, .di = Slot(DEBRIS)});
    }
    PutWord(rig, Elite::DS.viewAngle.offset, 0);
    rig.AssertAllAgreed(TRANSFORM_TO_VIEW, views.size());
    rig.AssertAllAgreed(TRANSFORM_TO_VIEW_WITH_BLIP, views.size());
    AssertExecuted(rig, TRANSFORM_TO_VIEW,
                   {0x3EEE, 0x3EEF, 0x3EF0, 0x3EF1, 0x3EF4, 0x3EF6, 0x3EF9, 0x3EFA, 0x3EFB, 0x3EFE, 0x3F00, 0x3F01});
  }

  // With fuel scoops, TransformShip offers each ship to TryScoopObject: outside its box on every side,
  // and inside it as each kind it scoops, with room in the hold and without. At the title screen the
  // camera is unrotated, so the view position is the position.
  TEST_METHOD(TransformShipAgreesWithFuelScoops)
  {
    ComparisonRig rig("TransformShip");
    std::uint64_t calls = 0;
    const auto transform = [&rig, &calls](std::uint8_t _type, std::uint8_t _state, std::int16_t _x, std::int16_t _y, std::int16_t _z)
    {
      CopySlot(rig, TITLE_SHIP, SPARE);
      PutByte(rig, Slot(SPARE), _type);
      PutByte(rig, At(Slot(SPARE), 0x1E), _state);
      PlaceSlot(rig, SPARE, _x, _y, _z);
      rig.Call(TRANSFORM_SHIP, {.di = Slot(SPARE)});
      ++calls;
    };
    PutByte(rig, Elite::DS.fuelScoopsFitted.offset, 1);
    PutByte(rig, Elite::DS.gameOverFrames.offset, 1);
    transform(BARREL, 0, 0, 0x40, 0x40);
    PutByte(rig, Elite::DS.gameOverFrames.offset, 0);
    // Past each edge of the box in turn, then inside it with x and z negative.
    const std::array<std::array<std::int16_t, 3>, 7> positions = {
      {{0, -5, 0x40}, {0, 0x10, 0x40}, {0, 0x100, 0x40}, {-0x100, 0x40, 0x40}, {0, 0x40, 0x100}, {0, 0x40, -0x100}, {-0x10, 0x40, -0x10}}};
    for (const std::array<std::int16_t, 3>& position : positions)
    {
      transform(BARREL, 0, position[0], position[1], position[2]);
    }
    const std::uint8_t ship = rig.Host().Ram().Read8(Elite::DataSegment(rig.Program()), Slot(TITLE_SHIP));
    for (const std::uint8_t used : {std::uint8_t{0}, std::uint8_t{20}})
    {
      PutByte(rig, Elite::DS.cargoUsedTonnes.offset, used);
      for (const std::uint8_t type : {ship, BARREL, SPLINTER, ESCAPE_POD, THARGON})
      {
        transform(type, 0, 0, 0x40, 0x40);
      }
      transform(BARREL, 0x40, 0, 0x40, 0x40);
      transform(SPLINTER, 0x10, 0, 0x40, 0x40);
    }
    PutByte(rig, Elite::DS.largeCargoBayFitted.offset, 1);
    // NextRandom made to give 80 (cargo 80 / 24 = 3, which becomes 11), then 1, 0 and 1: every precious
    // metal and the gems overflow their caps, and the last draw is below 28h (alloys).
    const auto seed = [&rig](std::uint16_t _first)
    {
      PutWord(rig, Elite::DS.randomState0.offset, _first);
      PutWord(rig, Elite::DS.randomState1.offset, 0);
      PutWord(rig, Elite::DS.randomState2.offset, 0);
    };
    seed(80);
    transform(BARREL, 0, 0, 0x40, 0x40);
    seed(1);
    PutByte(rig, Elite::DS.cargoGemStonesGrams.offset, 0xFA);
    PutByte(rig, Elite::DS.cargoGoldKg.offset, 0xFA);
    PutByte(rig, Elite::DS.cargoPlatinumKg.offset, 0xFA);
    transform(SPLINTER, 0x10, 0, 0x40, 0x40);
    rig.AssertAllAgreed(TRANSFORM_SHIP, calls);
    AssertExecuted(rig, TRANSFORM_SHIP, {0x3C92, 0x3C97, 0x3C99});
  }

  // The title ship copied into slots 3-8: one off the scanner for its 255th frame, removed; four
  // flashing, on and off, at the end of a phase and not; one far enough that its size passes its
  // level of detail, drawn as a dot. Slot 2 becomes a Coriolis station at depth 1 (UpdateCompass does
  // nothing before the title is shown), near, then at three distances DrawDistantStation takes.
  TEST_METHOD(TransformAndDrawObjectsAgreesForEveryWayAnObjectIsDrawn)
  {
    ComparisonRig rig("TransformAndDrawObjects");
    for (std::size_t slot = 3; slot <= 8; ++slot)
    {
      CopySlot(rig, TITLE_SHIP, slot);
    }
    PutByte(rig, At(Slot(3), 0x34), 0xFF);
    PutByte(rig, At(Slot(3), 0x1E), 0);
    const std::array<std::array<std::uint8_t, 2>, 4> flashes = {{{0xA2, 1}, {0xE2, 2}, {0xE2, 1}, {0xA2, 2}}};
    for (std::size_t flash = 0; flash < flashes.size(); ++flash)
    {
      PutByte(rig, At(Slot(4 + flash), 0x1E), flashes[flash][0]);
      PutByte(rig, At(Slot(4 + flash), 0x2C), flashes[flash][1]);
    }
    PlaceSlot(rig, 8, 0, 0, 0x1800);
    PutByte(rig, Slot(TITLE_SHIP), CORIOLIS);
    PutByte(rig, At(Slot(TITLE_SHIP), 0x3D), 1);
    PutWord(rig, At(Slot(TITLE_SHIP), 0x20), 0);
    PutWord(rig, At(Slot(TITLE_SHIP), 0x22), 0);
    PutByte(rig, Elite::DS.compassTargetIsStation.offset, 1);
    PutByte(rig, Elite::DS.shipSlotCount.offset, 9);
    const std::initializer_list<std::uint16_t> distances = {0x1000, 0x1B58, 0x2000, 0x2200};
    for (const std::uint16_t distance : distances)
    {
      PutWord(rig, At(Slot(TITLE_SHIP), 0x24), distance);
      rig.Call(TRANSFORM_AND_DRAW_OBJECTS, {});
    }
    rig.AssertAllAgreed(TRANSFORM_AND_DRAW_OBJECTS, distances.size());
    AssertExecuted(rig, TRANSFORM_AND_DRAW_OBJECTS,
                   {0x3D5E, 0x3D61, 0x3DF4, 0x3DF8, 0x3DFA, 0x3DFB, 0x3DFE, 0x3DFF, 0x3E01, 0x3E04, 0x3E07, 0x3E09, 0x3E0B, 0x3E0E,
                    0x3E11, 0x3E13, 0x3E16, 0x3E19, 0x3E1B, 0x3E24, 0x3E28, 0x3E2A, 0x3E2D, 0x3E2F, 0x3E33, 0x3E37, 0x3E39, 0x3E3C,
                    0x3E3E, 0x3E42, 0x3E8C, 0x3E8D, 0x3E90, 0x3E93, 0x3E96, 0x3E99, 0x3E9B, 0x3E9D, 0x3EA2, 0x3EA5, 0x3EA8, 0x3EA9});
  }

  // The sun 256 units ahead at scale shifts 8, 6 and 5: fringes 3 and 7, fuel scooping that fills the
  // tank, and a radius of 255 that kills. Then a supernova: its heat taken from the cabin temperature,
  // then grown past 255, killing and detonating an energy bomb in the safe zone, on a drawn ship with
  // two fragments in slot 3 and an undrawn one in slot 4. Last the planet, near enough to crash into.
  TEST_METHOD(DrawSunOrPlanetAgreesFromTheFringeToTheHeatDeath)
  {
    ComparisonRig rig("DrawSunOrPlanet");
    std::uint64_t calls = 0;
    const auto draw = [&rig, &calls](std::size_t _slot, std::uint8_t _shift)
    {
      PutByte(rig, At(Slot(_slot), 0x0A), _shift);
      rig.Call(DRAW_SUN_OR_PLANET, {.di = Slot(_slot)});
      ++calls;
    };
    CopySlot(rig, TITLE_SHIP, 3);
    PutByte(rig, At(Slot(3), 0x2D), 2);
    CopySlot(rig, TITLE_SHIP, 4);
    PutByte(rig, Slot(4), 0x75);
    PutByte(rig, Elite::DS.objectSlotCount.offset, 5);
    PutByte(rig, Elite::DS.safeZoneFlags.offset, 1);
    PutByte(rig, Elite::DS.legalStatus.offset, 0xF0);
    PutByte(rig, Slot(0), SUN);
    PutByte(rig, At(Slot(0), 0x0B), 2);
    PlaceSlot(rig, 0, 0, 0, 0x100);
    PutByte(rig, Elite::DS.fuelScoopsFitted.offset, 1);
    PutByte(rig, Elite::DS.fuel.offset, 0xFC);
    for (const std::uint8_t shift : {std::uint8_t{8}, std::uint8_t{6}, std::uint8_t{5}})
    {
      draw(0, shift);
    }
    PutByte(rig, Elite::DS.fuelScoopsFitted.offset, 0);
    draw(0, 6);
    PutWord(rig, Elite::DS.supernovaFrames.offset, 1);
    PutByte(rig, Elite::DS.supernovaHeat.offset, 0);
    PutByte(rig, Elite::DS.cabinTemperature.offset, 2);
    draw(0, 8);
    PutByte(rig, Elite::DS.supernovaHeat.offset, 0xF0);
    draw(0, 8);
    PutByte(rig, Slot(1), PLANET);
    PutByte(rig, At(Slot(1), 0x0B), 1);
    PlaceSlot(rig, 1, 0, 0, 0x100);
    draw(1, 0);
    rig.AssertAllAgreed(DRAW_SUN_OR_PLANET, calls);
    AssertExecuted(rig, DRAW_SUN_OR_PLANET,
                   {0x3F5E, 0x3F62, 0x3F64, 0x3F68, 0x3F6D, 0x3F6F, 0x3F72, 0x3F75, 0x3F78, 0x3F7A, 0x3F7C, 0x3F7E, 0x3F80, 0x3F84, 0x3F86,
                    0x3F89, 0x3F8C, 0x3F8E, 0x3F91, 0x3F93, 0x3FB1, 0x3FB6, 0x3FB9, 0x3FBB, 0x3FC0, 0x3FC5, 0x3FCA, 0x3FCC, 0x3FD1, 0x3FD3,
                    0x3FD8, 0x3FDB, 0x3FDF, 0x3FE5, 0x3FE8, 0x3FEA, 0x3FED, 0x3FEF, 0x4020, 0x4021, 0x4024, 0x4025, 0x4027});
  }
};

} // namespace GameLogicTests

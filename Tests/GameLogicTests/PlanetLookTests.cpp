#include "pch.h"

#include "PlanetLook.h"

#include <cstdint>
#include <set>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint8_t GALAXIES = 8;
constexpr unsigned SYSTEMS = 256;
constexpr std::uint8_t LAVE_GALAXY = 0;
constexpr std::uint8_t LAVE = 7;

} // namespace

// The looks of the systems' planets and skies (ADR-035, D45, D64), each from its galaxy and its number in integer arithmetic.
TEST_CLASS(PlanetLookTests)
{
public:
  // A system looks the same every time; Lave, where a new game starts, is a terrestrial world, its seed pinned so that a change
  // to the arithmetic, which repaints every system, is seen.
  TEST_METHOD(EachSystemLooksTheSameEveryTime)
  {
    Assert::IsTrue(Elite::PlanetLookOf(LAVE_GALAXY, LAVE) == Elite::PlanetLookOf(LAVE_GALAXY, LAVE));
    Assert::IsTrue(Elite::SkyLookOf(LAVE_GALAXY, LAVE) == Elite::SkyLookOf(LAVE_GALAXY, LAVE));
    const Elite::PlanetLook lave = Elite::PlanetLookOf(LAVE_GALAXY, LAVE);
    Assert::IsTrue(lave.kind == Elite::PlanetKind::Terrestrial, L"Lave is terrestrial");
    Assert::AreEqual(0x4BA42193u, lave.seed, L"Lave's seed");
    Assert::AreEqual(0x8ABE3466u, Elite::SkyLookOf(LAVE_GALAXY, LAVE).seed, L"Lave's sky's seed");
    Assert::IsFalse(Elite::PlanetLookOf(LAVE_GALAXY, LAVE) == Elite::PlanetLookOf(LAVE_GALAXY, LAVE + 1), L"its neighbour differs");
    Assert::IsFalse(Elite::PlanetLookOf(LAVE_GALAXY, LAVE) == Elite::PlanetLookOf(LAVE_GALAXY + 1, LAVE), L"so does its namesake");
  }

  // Across the eight galaxies, 762 of the 2,048 planets are gas giants, near the three in eight the arithmetic aims at; every
  // share is within its range, and the seeds differ from system to system.
  TEST_METHOD(TheGalaxiesHoldBothKinds)
  {
    unsigned gasGiants = 0;
    std::set<std::uint32_t> seeds;
    for (std::uint8_t galaxy = 0; galaxy < GALAXIES; ++galaxy)
    {
      for (unsigned system = 0; system < SYSTEMS; ++system)
      {
        const Elite::PlanetLook look = Elite::PlanetLookOf(galaxy, static_cast<std::uint8_t>(system));
        gasGiants += look.kind == Elite::PlanetKind::GasGiant ? 1 : 0;
        seeds.insert(look.seed);
        Assert::IsTrue(look.seaLevel >= 90 && look.seaLevel <= 190, L"the sea's level");
        Assert::IsTrue(look.cloudCover >= 10 && look.cloudCover <= 80, L"the cloud's cover");
        Assert::IsTrue(look.bands >= 4 && look.bands <= 12, L"the bands");
        Assert::IsTrue(look.turbulence >= 70 && look.turbulence <= 220, L"the turbulence");
        const Elite::SkyLook sky = Elite::SkyLookOf(galaxy, static_cast<std::uint8_t>(system));
        Assert::IsTrue(sky.nebulaDensity >= 60 && sky.nebulaDensity <= 240 && sky.starDensity >= 25 && sky.starDensity <= 65, L"the sky");
      }
    }
    Assert::AreEqual(762u, gasGiants, L"the gas giants");
    Assert::AreEqual(std::size_t{GALAXIES} * SYSTEMS, seeds.size(), L"a seed for each system");
  }
};

} // namespace GameLogicTests

#include "pch.h"

#include "AnswerFile.h"
#include "DataOverlay.h"
#include "Galaxy.h"
#include "ReferenceRig.h"

#include <format>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

/// The galaxies' known answers, from the repository's root. The file's header documents its format and states the count.
constexpr std::string_view GALAXY_ANSWERS_FILE = "GameLogicTests/GalaxyAnswers.txt";
constexpr std::size_t GALAXIES = 8;
constexpr std::size_t SYSTEMS = 256;

// System _system of galaxy _galaxy, as the game describes it: its seeds (LoadSystemSeeds) and, from them, its record and its
// name (DescribeSystem). The answer is its galaxy, its index, its name, its place (x, and y as the galaxy holds it, twice the
// chart's row), its economy, government, tech level, population, productivity and radius as the record holds them, and the
// system the galactic chart's cursor selects on its place (MoveCursorToSystem, SelectSystemAtCursor), as the status and data
// screens select one: itself, or an earlier system at the same place.
[[nodiscard]] std::string SystemAnswer(Elite::GameState& _state, std::uint8_t _galaxy, std::uint8_t _system)
{
  _state.Data().commanderBlock.galaxyNumber = _galaxy;
  _state.Data().commanderBlock.chartIsShortRange = 0;
  Elite::LoadSystemSeeds(_state, _system);
  const std::uint8_t x = _state.Data().systemSeed1Bytes.systemX;
  const std::uint8_t y = _state.Data().systemSeed0Bytes.systemY;
  Elite::MoveCursorToSystem(_state);
  Elite::DescribeSystem(_state);

  std::string name;
  for (std::size_t index = 0; index < _state.Data().commanderBlock.selectedSystemNameLength; ++index)
  {
    name += static_cast<char>(_state.Byte(DS.selectedSystemName.At(index)));
  }
  const Elite::GameData::CommanderBlock& commander = _state.Data().commanderBlock;
  std::string answer = std::format("{} {} {} {} {} {} {} {} {} {} {}", _galaxy, _system, name, x, y,
                                   std::uint8_t{commander.selectedEconomy}, std::uint8_t{commander.selectedGovernment},
                                   std::uint8_t{commander.selectedTechLevel}, std::uint8_t{commander.selectedPopulationTenthsBillion},
                                   std::uint16_t{commander.selectedProductivityMillionCredits}, std::uint16_t{commander.selectedRadiusKm});
  (void)Elite::SelectSystemAtCursor(_state, 0);
  return answer + std::format(" {}", std::uint8_t{commander.selectedSystemIndex});
}

} // namespace

// Known answers per subsystem (D33): the galaxies, from the faithful code while the corpus still vouched for it.
TEST_CLASS(GalaxyAnswerTests)
{
public:
  // All eight galaxies' 256 systems each: name, place, economy, government, tech level, population, productivity and
  // radius, and the system the chart's cursor selects on its place, as the game's own routines give them, are the answers
  // recorded in GALAXY_ANSWERS_FILE: 2,048 of them, and Lave, system 7 of the first galaxy, as published.
  TEST_METHOD(EverySystemIsTheOriginals)
  {
    ReferenceRig rig("GalaxyAnswers");
    std::vector<std::string> answers;
    for (std::size_t galaxy = 0; galaxy < GALAXIES; ++galaxy)
    {
      for (std::size_t system = 0; system < SYSTEMS; ++system)
      {
        answers.push_back(SystemAnswer(rig.State(), static_cast<std::uint8_t>(galaxy), static_cast<std::uint8_t>(system)));
      }
    }
    // Lave, at 20, 173: economy 2, which this build's names (economyNames) call Rich Agricultural, and government 3, a
    // Dictatorship, as published for the BBC Micro (plan §6.4). Its tech level, population, productivity and radius are this
    // build's own, not the BBC Micro's published 5 (held as 4), 2.5 billion, 7000 M CR and 4116 km.
    Assert::AreEqual("0 7 LAVE 20 173 2 3 5 28 13552 6276 7", answers[7].c_str());
    CheckSubsystemAnswers(GALAXY_ANSWERS_FILE, answers, GALAXIES * SYSTEMS);
  }
};

} // namespace GameLogicTests

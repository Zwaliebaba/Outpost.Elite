// GameLogic/DockedTables.h
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace Elite
{

// The tables and constants this subsystem reads, which nothing writes (D33, D38): source since the original's image went (Phase 7,
// step 5).
// Docked.cpp alone includes this, so they are in an unnamed namespace (D38).
namespace
{

/// 8 texts, each after the one before's NUL.
constexpr std::array<std::string_view, 8> SUPERNOVA_REFUSED_LINES = {
  std::string_view{"We hope you die a horribly long"}, // [0]
  std::string_view{"lingering death at the hands of"}, // [1]
  std::string_view{"a slimey green lobstoid or a"},    // [2]
  std::string_view{"vicious furry goat-shrew from"},   // [3]
  std::string_view{"Xafolif if you're not blown to"},  // [4]
  std::string_view{"bits by our sun!"},                // [5]
  std::string_view{" "},                               // [6]
  std::string_view{"Press any key to continue!"},      // [7]
};

/// 8 texts, each after the one before's NUL.
constexpr std::array<std::string_view, 8> SUPERNOVA_ACCEPTED_LINES = {
  std::string_view{"Thank you!"},                      // [0]
  std::string_view{"I'm afraid that we've had to"},    // [1]
  std::string_view{"dump any cargo you had to get"},   // [2]
  std::string_view{"all of us refugees in. If you"},   // [3]
  std::string_view{"just drop us off at the nearest"}, // [4]
  std::string_view{"system, that'll be fine!"},        // [5]
  std::string_view{" "},                               // [6]
  std::string_view{"Press any key to continue!"},      // [7]
};

/// 7 texts, each after the one before's NUL.
constexpr std::array<std::string_view, 7> MASK_DESTROYED_LINES = {
  std::string_view{"It was unfortunate that you could"}, // [0]
  std::string_view{"only destroy the masking ship and"}, // [1]
  std::string_view{"not actually retrieve the mask"},    // [2]
  std::string_view{"device. We shall never know any-"},  // [3]
  std::string_view{"thing about it now!"},               // [4]
  std::string_view{" "},                                 // [5]
  std::string_view{"Press any key to continue!"},        // [6]
};

/// 4 texts, each after the one before's NUL.
constexpr std::array<std::string_view, 4> MASK_FLED_LINES = {
  std::string_view{"Running away from the mask ship"}, // [0]
  std::string_view{"was probably a good idea!"},       // [1]
  std::string_view{" "},                               // [2]
  std::string_view{"Press any key to continue!"},      // [3]
};

/// 8 texts, each after the one before's NUL.
constexpr std::array<std::string_view, 8> MASK_RECOVERED_LINES = {
  std::string_view{"This masking device looks very"},      // [0]
  std::string_view{"interesting. We will fit a modified"}, // [1]
  std::string_view{"version to your space craft as a"},    // [2]
  std::string_view{"reward! Press 'N' to activate it"},    // [3]
  std::string_view{"but be careful as it uses a lot of"},  // [4]
  std::string_view{"energy!"},                             // [5]
  std::string_view{" "},                                   // [6]
  std::string_view{"Press any key to continue!"},          // [7]
};

/// archangelTitle: 'ARCHANGEL', ended by the forceMisjump byte
constexpr std::array<std::uint8_t, 9> ARCHANGEL_TITLE = {
  0x41, 0x52, 0x43, 0x48, 0x41, 0x4E, 0x47, 0x45, // [0]
  0x4C,                                           // [8]
};

/// fuelTextPrefix: 'Fuel:' and its padding, which runs on into the fuel's units digit (change 3c names it)
constexpr std::string_view FUEL_TEXT_PREFIX{"Fuel:         ", 14};

/// fuelTextPoint: the fuel text's point, between its two digits (change 3c names it)
constexpr std::string_view FUEL_TEXT_POINT{".", 1};

/// ratingThresholds: byte; 2, 4, 9, 20, 35, 90, 155, 230, 255 against killCount
constexpr std::array<std::uint8_t, 9> RATING_THRESHOLDS = {
  0x02, 0x04, 0x09, 0x14, 0x23, 0x5A, 0x9B, 0xE6, // [0]
  0xFF,                                           // [8]
};

/// missileNameOpen: 'Missile (', which runs on into the missile count's digit (change 3c names it)
constexpr std::string_view MISSILE_NAME_OPEN{"Missile (", 9};

/// supernovaBriefingText: 5 lines ending "Will you help us (Y/N)?"
constexpr std::array<std::string_view, 5> SUPERNOVA_BRIEFING_TEXT = {
  std::string_view{"Our sun is going supernova!"},     // [0]
  std::string_view{"You are advised to hyper-space"},  // [1]
  std::string_view{"out of this system immediately!"}, // [2]
  std::string_view{" "},                               // [3]
  std::string_view{"Will you help us (Y/N)?"},         // [4]
};

/// supernovaDebriefOpen: 'Thank you for saving us.', then 'Please accept 1', which runs on into refugeeRewardDigit
/// (change 3c names it)
constexpr std::string_view SUPERNOVA_DEBRIEF_OPEN{"Thank you for saving us.\000Please accept 1", 40};

/// supernovaDebriefClose: '00 Credits!', a blank line and 'Press any key to continue!', after refugeeRewardDigit (change
/// 3c names it)
constexpr std::string_view SUPERNOVA_DEBRIEF_CLOSE{"00 Credits!\000 \000Press any key to continue!\000", 41};

/// maskBriefingText: 9 lines on the invisible masking device
constexpr std::array<std::string_view, 9> MASK_BRIEFING_TEXT = {
  std::string_view{"We're getting reports of a new"},      // [0]
  std::string_view{"invisible masking device in a"},       // [1]
  std::string_view{"system nearby, which allows space"},   // [2]
  std::string_view{"craft to become totally invisible."},  // [3]
  std::string_view{"We would like you to go and find"},    // [4]
  std::string_view{"out what's happening and if"},         // [5]
  std::string_view{"possible, retrieve the mask device!"}, // [6]
  std::string_view{" "},                                   // [7]
  std::string_view{"Press any key to continue!"},          // [8]
};

/// invasionBriefingText: 12 lines on the Thargoid-invaded system
constexpr std::array<std::string_view, 12> INVASION_BRIEFING_TEXT = {
  std::string_view{"Intelligence reports that a near-"}, // [0]
  std::string_view{"by system has been invaded by"},     // [1]
  std::string_view{"alien thargoid ships and that"},     // [2]
  std::string_view{"the coriolis orbiting space"},       // [3]
  std::string_view{"station is under attack. We"},       // [4]
  std::string_view{"suggest that you destroy the"},      // [5]
  std::string_view{"space station and not try to dock"}, // [6]
  std::string_view{"with it if you come across heavy"},  // [7]
  std::string_view{"alien concentrations in that"},      // [8]
  std::string_view{"system!"},                           // [9]
  std::string_view{" "},                                 // [10]
  std::string_view{"Press any key to continue!"},        // [11]
};

/// pressAnyKeyText: 'Press any key to start game' (title)
constexpr std::string_view PRESS_ANY_KEY_TEXT = "Press any key to start game";

/// titleShipDistances: word; title display distance per ship type
constexpr std::array<std::uint16_t, 30> TITLE_SHIP_DISTANCES = {
  0x0514, 0x0514, 0x0190, 0x0190, 0x0190, 0x0190, 0x0190, 0x0190, // [0]
  0x0190, 0x0190, 0x0190, 0x0190, 0x0118, 0x0190, 0x0190, 0x0190, // [8]
  0x0190, 0x0190, 0x0190, 0x0190, 0x0190, 0x0190, 0x0190, 0x0190, // [16]
  0x0190, 0x0190, 0x01CC, 0x0190, 0x0190, 0x0190,                 // [24]
};

} // namespace

} // namespace Elite

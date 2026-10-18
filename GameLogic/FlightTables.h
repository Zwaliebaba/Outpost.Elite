// GameLogic/FlightTables.h
#pragma once

#include "Message.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace Elite
{

// The tables and constants this subsystem reads, which nothing writes (D33, D38): source since the original's image went (Phase 7,
// step 5).
// Flight.cpp alone includes this, so they are in an unnamed namespace (D38).
namespace
{

/// warningTexts: each warning's message, by the warning's number, the order of warningChecks' checks (SetWarning).
constexpr std::array<Message, 4> WARNING_MESSAGES = {
  Message::MissileWarning,     // warningTexts[0]
  Message::AltitudeWarning,    // warningTexts[1]
  Message::TemperatureWarning, // warningTexts[2]
  Message::EnergyWarning,      // warningTexts[3]
};

/// stardustColor: drawColor for stardust; 3 in the file
constexpr std::uint8_t STARDUST_COLOR = 0x03;

/// collisionRanges: word; box half-size per object type for CheckCollisions: 275 for types 0-1 (stations), 100 for the
/// rest
constexpr std::array<std::uint16_t, 32> COLLISION_RANGES = {
  0x0113, 0x0113, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, // [0]
  0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, // [8]
  0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, // [16]
  0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, 0x0064, // [24]
};

/// sqrtTable: byte; about sqrt(n) rounded, n=0-128 (n=57 and 73 hold 7 and 8); UpdateCompass reads n=65-98. Sits just
/// past the 42 vertices, so a 43rd would overwrite it
constexpr std::array<std::uint8_t, 129> SQRT_TABLE = {
  0x00, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, // [0]
  0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, // [8]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, // [16]
  0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, // [24]
  0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, // [32]
  0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, // [40]
  0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, // [48]
  0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, // [56]
  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, // [64]
  0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, // [72]
  0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, // [80]
  0x09, 0x09, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, // [88]
  0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, // [96]
  0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, // [104]
  0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, // [112]
  0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, // [120]
  0x0B,                                           // [128]
};

/// dashboardPixelMasks: byte; 0x80, 0x20, 0x08, 0x02: a colour-2 pixel at each position in a byte of four; DashboardPixelMask
constexpr std::array<std::uint8_t, 4> DASHBOARD_PIXEL_MASKS = {
  0x80, 0x20, 0x08, 0x02, // [0]
};

/// pauseMenuText: word buffer offset, then zero-terminated text; the pause screen's lines: SPACE..Restart Game,
/// A......Abort Game, R Keyboard Recentre, D Keyboard Damping, Y Reverse Y control, B Reverse X and Y, S Sound Toggle, F1/F10.Set Frame
/// Rate (F8)
constexpr std::string_view PAUSE_MENU_TEXT{"\010\006SPACE..Restart Game\000\010\010A......Abort Game\000\010\014R......Keyboard Recentre"
                                           "\000\010\016D......Keyboard Damping\000\010\020Y......Reverse Y control\000\010\022B......Reve"
                                           "rse X and Y\000\010\024S......Sound Toggle\000\010\030F1/F10.Set Frame Rate (F",
                                           195};

/// recenterLetter: 'R', the letter the pause screen shows for keyboardRecenter, after it (change 3c names it)
constexpr std::string_view RECENTER_LETTER{"R", 1};

/// dampingLetter: 'D', the letter the pause screen shows for keyboardDamping, after it (change 3c names it)
constexpr std::string_view DAMPING_LETTER{"D", 1};

/// yReverseLetter: 'Y', the letter the pause screen shows for reverseYControl, after it (change 3c names it)
constexpr std::string_view Y_REVERSE_LETTER{"Y", 1};

/// xyReverseLetter: 'B', the letter the pause screen shows for reverseXAndY, after it (change 3c names it)
constexpr std::string_view XY_REVERSE_LETTER{"B", 1};

/// soundLetter: 'S', the letter the pause screen shows for soundEnabled, after it (change 3c names it)
constexpr std::string_view SOUND_LETTER{"S", 1};

/// frameTimeChoices: byte; 200, 125, 100, 83, 67, 56, 50, 40, 30, 1 ms, chosen with F1-F10 on the pause screen
constexpr std::array<std::uint8_t, 10> FRAME_TIME_CHOICES = {
  0xC8, 0x7D, 0x64, 0x53, 0x43, 0x38, 0x32, 0x28, // [0]
  0x1E, 0x01,                                     // [8]
};

} // namespace

} // namespace Elite

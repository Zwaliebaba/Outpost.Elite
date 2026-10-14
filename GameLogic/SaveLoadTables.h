// GameLogic/SaveLoadTables.h
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace Elite
{

// The tables and constants this subsystem reads, which nothing writes (D33, D38): source since the 1987 image went (Phase 7,
// step 5).
// SaveLoad.cpp alone includes this, so they are in an unnamed namespace (D38).
namespace
{

/// inputDeviceNames (DS:8670): the keyboard's and the mouse's; the joystick's went with D81.
constexpr std::string_view KEYBOARD_DEVICE_NAME = "Keyboard    "; // DS:85E9
constexpr std::string_view MOUSE_DEVICE_NAME = "Mouse       ";    // DS:860F

/// diskOperationNames (DS:86D9): each pointer's text, up to its NUL.
constexpr std::array<std::string_view, 4> DISK_OPERATION_NAMES = {
  std::string_view{"LOADED: "},    // DS:86E1
  std::string_view{"SAVED: "},     // DS:86EA
  std::string_view{"DELETED: "},   // DS:86F2
  std::string_view{"CATALOGUED "}, // DS:86FC
};

/// DS:8667
constexpr std::string_view DEVICE_TEXT = "DEVICE: ";

/// DS:8676
constexpr std::string_view FILE_NAME_PROMPT_TEXT = "FILENAME?          ";

/// DS:869C
constexpr std::string_view EXIT_QUESTION_TEXT = "Are you sure (Y/N)?";

/// DS:86B0
constexpr std::string_view RETRY_TEXT = "ERROR: Retry (Y/N)?";

/// DS:86C4
constexpr std::string_view BLANK_STATUS_TEXT = "                    ";

/// DS:8708
constexpr std::string_view CATALOGUE_OK_TEXT = "OK";

/// DS:870B
constexpr std::string_view NO_MOUSE_TEXT = "ERROR: No Mouse!    ";

/// DS:8720: 2 texts, each after the one before's NUL.
constexpr std::array<std::string_view, 2> MOUSE_HELP_LINES = {
  std::string_view{"Can not find mouse driver"},            // DS:8720
  std::string_view{"Exit to DOS and install a driver.   "}, // DS:873A
};

/// DS:8811
constexpr std::string_view BLANK_LINE_TEXT = "                                    ";

/// DS:884E
constexpr std::string_view FILE_NAME_ERROR_TEXT = "FILENAME ERROR      ";

/// commanderFilePattern (DS:20EF in 1987): *.cdr
constexpr std::array<std::uint8_t, 6> COMMANDER_FILE_PATTERN = {
  0x2A, 0x2E, 0x63, 0x64, 0x72, 0x00, // DS:20EF
};

/// versionString (DS:7B50 in 1987): Release:1? 1-10-87
constexpr std::string_view VERSION_STRING = "Release:1? 1-10-87";

/// discControlFrame (DS:7BA5 in 1987): frame bytes, then "DISC/CONTROL"
constexpr std::string_view DISC_CONTROL_FRAME = "_ZDISC/CONTROL";

/// discMenuText (DS:85A9 in 1987): count byte (6), then the Disc/Control menu lines
constexpr std::array<std::string_view, 6> DISC_MENU_TEXT = {
  std::string_view{"Press key for disc or device options"},  // DS:85AA
  std::string_view{},                                        // DS:85CF
  std::string_view{"L...Load Commander   K...Keyboard    "}, // DS:85D0
  std::string_view{"S...Save Commander   M...Mouse       "}, // DS:85F6
  std::string_view{"D...Delete Commander                "},  // DS:861C, its J...Joystick gone with D81
  std::string_view{"C...Catalogue Disc   E...Exit to DOS"},  // DS:8642
};

} // namespace

} // namespace Elite

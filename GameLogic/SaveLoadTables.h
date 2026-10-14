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

/// inputDeviceNames: the keyboard's and the mouse's; the joystick's went with D81.
constexpr std::string_view KEYBOARD_DEVICE_NAME = "Keyboard    ";
constexpr std::string_view MOUSE_DEVICE_NAME = "Mouse       ";

/// diskOperationNames: each pointer's text, up to its NUL.
constexpr std::array<std::string_view, 4> DISK_OPERATION_NAMES = {
  std::string_view{"LOADED: "},    // [0]
  std::string_view{"SAVED: "},     // [1]
  std::string_view{"DELETED: "},   // [2]
  std::string_view{"CATALOGUED "}, // [3]
};

constexpr std::string_view DEVICE_TEXT = "DEVICE: ";

constexpr std::string_view FILE_NAME_PROMPT_TEXT = "FILENAME?          ";

constexpr std::string_view EXIT_QUESTION_TEXT = "Are you sure (Y/N)?";

constexpr std::string_view RETRY_TEXT = "ERROR: Retry (Y/N)?";

constexpr std::string_view BLANK_STATUS_TEXT = "                    ";

constexpr std::string_view CATALOGUE_OK_TEXT = "OK";

constexpr std::string_view NO_MOUSE_TEXT = "ERROR: No Mouse!    ";

/// 2 texts, each after the one before's NUL.
constexpr std::array<std::string_view, 2> MOUSE_HELP_LINES = {
  std::string_view{"Can not find mouse driver"},            // [0]
  std::string_view{"Exit to DOS and install a driver.   "}, // [1]
};

constexpr std::string_view BLANK_LINE_TEXT = "                                    ";

constexpr std::string_view FILE_NAME_ERROR_TEXT = "FILENAME ERROR      ";

/// versionString: Release:1? 1-10-87
constexpr std::string_view VERSION_STRING = "Release:1? 1-10-87";

/// discControlFrame: frame bytes, then "DISC/CONTROL"
constexpr std::string_view DISC_CONTROL_FRAME = "_ZDISC/CONTROL";

/// discMenuText: count byte (6), then the Disc/Control menu lines
constexpr std::array<std::string_view, 6> DISC_MENU_TEXT = {
  std::string_view{"Press key for disc or device options"},  // [0]
  std::string_view{},                                        // [1]
  std::string_view{"L...Load Commander   K...Keyboard    "}, // [2]
  std::string_view{"S...Save Commander   M...Mouse       "}, // [3]
  std::string_view{"D...Delete Commander                "},  // its J...Joystick gone with D81
  std::string_view{"C...Catalogue Disc   E...Exit to DOS"},  // [5]
};

} // namespace

} // namespace Elite

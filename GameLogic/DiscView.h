// GameLogic/DiscView.h
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Elite
{

/// D10's menu, its items 1 to 6 in order (D102): load, save, delete, the catalogue, quit, and the input device.
enum class DiscItem : std::uint8_t
{
  Load,
  Save,
  Delete,
  Catalogue,
  Quit,
  Device,
};

/// What D10 waits for besides its menu (D117 to D119): the name a commander is saved under, or a Y/N question.
enum class DiscPrompt : std::uint8_t
{
  None,
  Name,      ///< Save commander's name, typed
  Overwrite, ///< the name is on the disc already: overwrite it?
  Delete,    ///< delete the selected file?
  Quit,      ///< quit the game?
  Retry,     ///< the disc failed: try again?
};

/// What D10's strip says after an operation or a refusal. The shell words it.
enum class DiscNote : std::uint8_t
{
  None,
  Loaded,     ///< subject loaded
  Saved,      ///< subject saved
  Deleted,    ///< subject deleted
  Catalogued, ///< the disc read again
  Failed,     ///< the disc failed on subject, and no retry
  NoFiles,    ///< nothing to load or delete: the catalogue is empty
  BadName,    ///< a name starts with a letter
  NoMouse,    ///< no mouse is attached
  Keyboard,   ///< the keyboard steers
  Mouse,      ///< the mouse steers
};

/// A commander file as the catalogue lists it.
struct DiscFile
{
  std::string name;       ///< JAMESON.CDR, as the store keys it
  std::uint16_t year = 0; ///< the day it was saved, from its stamp
  std::uint8_t month = 0;
  std::uint8_t day = 0;
  std::optional<std::uint32_t> creditsTenths; ///< the commander's cash in it, or nothing when it cannot be read
};

/// The input devices D10 offers, its rows in order: the keyboard, then the mouse.
inline constexpr std::uint8_t DISC_DEVICE_COUNT = 2;

/// What the shell's click on D10 is on (D83).
enum class DiscTarget : std::uint8_t
{
  Item,    ///< a menu item, index 0 to 5
  Row,     ///< a file's or a device's row, by index
  Primary, ///< the lit button
  Back,    ///< ESC · BACK, or N · NO at a question
};

/// A click on one of D10's controls, from the shell.
struct DiscClick
{
  DiscTarget target = DiscTarget::Item;
  std::size_t index = 0;
};

/// What D10 shows, for the shell (R9): plain values, nothing of the engine's and nothing of the game's state. The game publishes
/// one each time it changes (Host::ShowDisc).
struct DiscView
{
  std::uint64_t sequence = 0; ///< from 1, one more for each view published
  std::string systemName;     ///< the current system's name, as the game stores it
  bool dodo = false;          ///< docked at a Dodo, not a Coriolis
  std::uint32_t creditsTenths = 0;

  DiscItem item = DiscItem::Load;
  std::vector<DiscFile> files;  ///< the .CDR files on the disc, in the order of their names
  std::size_t selected = 0;     ///< the file selected, when there are any
  std::uint8_t device = 0;      ///< the device selected on Input device's panel: 0 the keyboard, 1 the mouse
  std::uint8_t deviceInUse = 0; ///< the device that steers now, as device counts
  DiscPrompt prompt = DiscPrompt::None;
  std::string typed;   ///< Save commander's name, as typed
  std::string subject; ///< the file the question or the note is about
  DiscNote note = DiscNote::None;
};

} // namespace Elite

// GameLogic/DiscScreen.h
#pragma once

#include "Docked.h"
#include "DiscView.h"
#include "Task.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Elite
{

class FileStore;
class GameState;
class Host;
struct KeyPress;

/// The most letters a commander's name takes: a DOS name's base.
inline constexpr std::size_t DISC_NAME_LETTERS_MOST = 8;

/// D10's own state, which the original has no counterpart for in this shape: the item, the file and the device selected, the
/// question asked, the name typed and the strip.
struct DiscScreen
{
  DiscItem item = DiscItem::Load;
  std::size_t file = 0;
  std::uint8_t device = 0;
  DiscPrompt prompt = DiscPrompt::None;
  std::string typed;
  std::string subject;
  DiscNote note = DiscNote::None;
};

/// What a key or a click asks of D10.
enum class DiscAction : std::uint8_t
{
  None,
  Choose,  ///< an item, by its key 1 to 6 or a click
  Up,      ///< the file or the device above
  Down,    ///< the file or the device below
  Pick,    ///< a file or a device, by a click on its row
  Act,     ///< Enter: load, save, delete or choose; nothing at a Y/N question
  Primary, ///< a click on the panel's lit button: as Enter, and Y at a question
  Back,    ///< Esc or a click on ESC · BACK: the question cancelled, or back to the status screen
  Yes,     ///< Y at a question
  No,      ///< N at a question
  Type,    ///< a letter or a digit typed into the name
  Erase,   ///< Backspace in the name
};

struct DiscInput
{
  DiscAction action = DiscAction::None;
  DiscItem item = DiscItem::Load; ///< for Choose
  std::size_t index = 0;          ///< for Pick: the file's or the device's row
  std::uint8_t character = 0;     ///< for Type, upper case
};

/// What D10 knows of the game and the disc to step with.
struct DiscFacts
{
  std::vector<DiscFile> files;
  std::uint8_t deviceInUse = 0; ///< 0 the keyboard, 1 the mouse
  bool mouse = false;           ///< a mouse is attached
  std::string commanderName;    ///< the name Save commander starts from: the commander's, JAMESON
};

/// What a step asks the routine to do on D10's behalf.
enum class DiscRequest : std::uint8_t
{
  None,
  Load,      ///< subject loaded, through Start (D78)
  Save,      ///< subject saved, through Start
  Delete,    ///< subject deleted, through Start
  Retry,     ///< the operation that failed, again
  Quit,      ///< the game ended (D80)
  Catalogue, ///< the disc read again
  Device,    ///< the device selected steers
  Leave,     ///< back to the status screen
};

struct DiscStep
{
  DiscScreen screen;
  DiscRequest request = DiscRequest::None;
};

/// The action of _key on D10 while _prompt waits: in the name, its letters and digits, Backspace, Enter and Esc; at a Y/N
/// question, Y, N and Esc; otherwise 1 to 6, the arrows, Enter and Esc. Nothing for the F-keys, which are the screens'.
[[nodiscard]] DiscInput DiscKey(const KeyPress& _key, DiscPrompt _prompt);

/// The action of a click on D10.
[[nodiscard]] DiscInput DiscClickInput(const DiscClick& _click) noexcept;

/// _screen after _input, with _facts: the item, the selection, the question and the name, and what the routine is to do.
[[nodiscard]] DiscStep StepDisc(const DiscScreen& _screen, const DiscInput& _input, const DiscFacts& _facts);

/// The commander files in _files: each .CDR file with no attribute, in the order of their names, at most 40, with the day its stamp
/// holds and the cash the commander in it holds, read through the .cdr serializer (D78) without changing the game.
[[nodiscard]] std::vector<DiscFile> CommanderFilesOf(FileStore& _files);

/// What D10 shows of _screen with _facts, and the game's _state.
[[nodiscard]] DiscView DiscViewOf(const GameState& _state, const DiscScreen& _screen, const DiscFacts& _facts);

/// How D10 ends: it left for the disk or the quit, through LeaveGameLoopForDisk, which returns past RunTitleAndDocked; otherwise
/// the key that closed it, F6's for Esc.
struct DiscExit
{
  bool leaves = false;
  ScreenKey key{};
};

/// D10, Disc access (D82, D102, D117 to D119), which Esc opens docked, in place of the original's Disc/Control menu. It opens on
/// Load commander with the catalogue, or, back from Start's disk work, on what that came to: the file loaded, saved or deleted,
/// or the question whether to retry it. Each tick it takes the clicks the shell sent (Host::Click(DiscClick)) and one key, steps
/// (StepDisc), and publishes the view (Host::ShowDisc). A load, a save or a delete leaves for Start as the original's menu did
/// (LeaveGameLoopForDisk), so that a loaded commander starts the game loop again; so does the quit.
[[nodiscard]] Task<DiscExit> ShowDiscScreen(GameState& _state, Host& _host);

} // namespace Elite

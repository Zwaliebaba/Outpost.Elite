#include "pch.h"

#include "DiscScreen.h"

#include "DockedKeys.h"
#include "FileStore.h"
#include "GameState.h"
#include "Host.h"
#include "Input.h"
#include "SaveLoad.h"
#include "SharedTables.h"
#include "StateWriter.h"
#include "Text.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Elite
{

namespace
{

// The keys D10 reads, as scan codes.
constexpr std::uint8_t SCAN_ESC = 0x01;
constexpr std::uint8_t SCAN_1 = 0x02;
constexpr std::uint8_t SCAN_6 = 0x07;
constexpr std::uint8_t SCAN_Y = 0x15;
constexpr std::uint8_t SCAN_N = 0x31;
constexpr std::uint8_t SCAN_UP = 0x48;
constexpr std::uint8_t SCAN_DOWN = 0x50;

// The most files the catalogue lists, as the original's did.
constexpr std::size_t MOST_COMMANDER_FILES = 0x28;
constexpr std::string_view COMMANDER_FILE_EXTENSION = ".CDR"; // as the store keys names, upper case

// ".CDR" and its NUL, written after a name in commanderFileName, a byte at a time, as the original's prompt wrote them.
constexpr std::array<std::uint8_t, 5> COMMANDER_EXTENSION = {'.', 'C', 'D', 'R', 0};

// inputDevice: the keyboard or the mouse, as the original numbered them; its 1, the joystick, went with D81.
constexpr std::uint8_t KEYBOARD_DEVICE = 0;
constexpr std::uint8_t MOUSE_DEVICE = 2;
constexpr std::uint8_t MOUSE_ROW = 1; // D10's device rows: the keyboard, then the mouse

// A stamp's date: (year - 1980) << 9 | month << 5 | day.
constexpr std::uint16_t STAMP_EPOCH_YEAR = 1980;
constexpr unsigned STAMP_YEAR_SHIFT = 9;
constexpr unsigned STAMP_MONTH_SHIFT = 5;
constexpr std::uint16_t STAMP_MONTH_MASK = 0x0F;
constexpr std::uint16_t STAMP_DAY_MASK = 0x1F;

// A stored tech level from which the station is a Dodo.
constexpr std::uint8_t DODO_TECH_LEVEL = 9;

// Whether _character may stand in a commander's name: a digit or an upper-case letter.
[[nodiscard]] constexpr bool IsNameCharacter(std::uint8_t _character) noexcept
{
  return (_character >= '0' && _character <= '9') || (_character >= 'A' && _character <= 'Z');
}

// Whether _item's panel is the catalogue, whose rows are files.
[[nodiscard]] constexpr bool ListsFiles(DiscItem _item) noexcept
{
  return _item == DiscItem::Load || _item == DiscItem::Catalogue || _item == DiscItem::Delete;
}

// Whether _prompt is a Y/N question.
[[nodiscard]] constexpr bool AsksYesOrNo(DiscPrompt _prompt) noexcept
{
  return _prompt == DiscPrompt::Overwrite || _prompt == DiscPrompt::Delete || _prompt == DiscPrompt::Quit || _prompt == DiscPrompt::Retry;
}

// The index of _name in _files, or nothing.
[[nodiscard]] std::optional<std::size_t> IndexOf(const std::vector<DiscFile>& _files, std::string_view _name)
{
  const auto found = std::ranges::find(_files, _name, &DiscFile::name);
  if (found == _files.end())
  {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - _files.begin());
}

// D10 as it opens (D119): Load commander, the catalogue's first file selected, the device that steers on its panel.
[[nodiscard]] DiscScreen Opening(const DiscFacts& _facts)
{
  DiscScreen screen;
  screen.device = _facts.deviceInUse;
  return screen;
}

// _screen on _item, as choosing it leaves it.
[[nodiscard]] DiscStep Choose(const DiscScreen& _screen, DiscItem _item, const DiscFacts& _facts)
{
  DiscStep step{.screen = _screen};
  DiscScreen& screen = step.screen;
  screen.item = _item;
  screen.prompt = DiscPrompt::None;
  screen.subject.clear();
  screen.note = DiscNote::None;
  switch (_item)
  {
  case DiscItem::Save:
    // The commander's name to start from, held to what a name takes (D118).
    screen.prompt = DiscPrompt::Name;
    screen.typed.clear();
    for (const char character : _facts.commanderName)
    {
      if (screen.typed.size() < DISC_NAME_LETTERS_MOST && IsNameCharacter(static_cast<std::uint8_t>(character)))
      {
        screen.typed.push_back(character);
      }
    }
    break;
  case DiscItem::Catalogue:
    step.request = DiscRequest::Catalogue;
    screen.note = DiscNote::Catalogued;
    break;
  case DiscItem::Quit:
    screen.prompt = DiscPrompt::Quit;
    break;
  case DiscItem::Device:
    screen.device = _facts.deviceInUse;
    break;
  case DiscItem::Load:
  case DiscItem::Delete:
    break;
  }
  return step;
}

// Enter on _screen: the item's action.
[[nodiscard]] DiscStep Act(const DiscScreen& _screen, const DiscFacts& _facts)
{
  DiscStep step{.screen = _screen};
  DiscScreen& screen = step.screen;
  if (screen.prompt == DiscPrompt::Name)
  {
    if (screen.typed.empty() || screen.typed.front() < 'A')
    {
      screen.note = DiscNote::BadName;
      return step;
    }
    screen.subject = screen.typed + std::string(COMMANDER_FILE_EXTENSION);
    screen.note = DiscNote::None;
    if (IndexOf(_facts.files, screen.subject))
    {
      screen.prompt = DiscPrompt::Overwrite;
      return step;
    }
    step.request = DiscRequest::Save;
    return step;
  }
  if (screen.prompt != DiscPrompt::None)
  {
    return step;
  }
  if (ListsFiles(screen.item))
  {
    if (_facts.files.empty())
    {
      screen.note = DiscNote::NoFiles;
      return step;
    }
    screen.subject = _facts.files[std::min(screen.file, _facts.files.size() - 1)].name;
    screen.note = DiscNote::None;
    if (screen.item == DiscItem::Delete)
    {
      screen.prompt = DiscPrompt::Delete;
      return step;
    }
    step.request = DiscRequest::Load;
    return step;
  }
  switch (screen.item)
  {
  case DiscItem::Save:
    return Choose(screen, DiscItem::Save, _facts);
  case DiscItem::Quit:
    screen.prompt = DiscPrompt::Quit;
    return step;
  case DiscItem::Device:
    if (screen.device == MOUSE_ROW && !_facts.mouse)
    {
      screen.note = DiscNote::NoMouse;
      return step;
    }
    screen.note = screen.device == MOUSE_ROW ? DiscNote::Mouse : DiscNote::Keyboard;
    step.request = DiscRequest::Device;
    return step;
  case DiscItem::Load:
  case DiscItem::Delete:
  case DiscItem::Catalogue:
    break;
  }
  return step;
}

// Y at _screen's question.
[[nodiscard]] DiscStep Yes(const DiscScreen& _screen)
{
  DiscStep step{.screen = _screen};
  switch (_screen.prompt)
  {
  case DiscPrompt::Overwrite:
    step.request = DiscRequest::Save;
    break;
  case DiscPrompt::Delete:
    step.request = DiscRequest::Delete;
    break;
  case DiscPrompt::Quit:
    step.request = DiscRequest::Quit;
    break;
  case DiscPrompt::Retry:
    step.request = DiscRequest::Retry;
    break;
  case DiscPrompt::None:
  case DiscPrompt::Name:
    break;
  }
  return step;
}

// N or Esc at _screen's question, or Esc in the name (D119): the question cancelled.
[[nodiscard]] DiscStep Cancel(const DiscScreen& _screen, const DiscFacts& _facts)
{
  DiscStep step{.screen = _screen};
  DiscScreen& screen = step.screen;
  switch (_screen.prompt)
  {
  case DiscPrompt::Name:
  case DiscPrompt::Quit:
    screen = Opening(_facts);
    screen.file = _screen.file;
    break;
  case DiscPrompt::Overwrite:
    screen.prompt = DiscPrompt::Name;
    screen.subject.clear();
    break;
  case DiscPrompt::Delete:
    screen.prompt = DiscPrompt::None;
    screen.subject.clear();
    break;
  case DiscPrompt::Retry:
    screen.prompt = DiscPrompt::None;
    screen.note = DiscNote::Failed;
    if (screen.item == DiscItem::Save)
    {
      screen.item = DiscItem::Load;
    }
    break;
  case DiscPrompt::None:
    step.request = DiscRequest::Leave;
    break;
  }
  return step;
}

// The rows of _screen's panel: its files, or its devices; none for the others.
[[nodiscard]] std::size_t RowsOf(const DiscScreen& _screen, const DiscFacts& _facts) noexcept
{
  if (ListsFiles(_screen.item))
  {
    return _facts.files.size();
  }
  return _screen.item == DiscItem::Device ? DISC_DEVICE_COUNT : 0;
}

// _screen with row _index of its panel selected.
void Select(DiscScreen& _screen, std::size_t _index)
{
  if (_screen.item == DiscItem::Device)
  {
    _screen.device = static_cast<std::uint8_t>(_index);
    return;
  }
  _screen.file = _index;
}

// The row of _screen's panel selected.
[[nodiscard]] std::size_t SelectedRow(const DiscScreen& _screen) noexcept
{
  return _screen.item == DiscItem::Device ? _screen.device : _screen.file;
}

// The cash of the commander in the file _name of _files, read through the .cdr serializer (D78) without changing the game, or
// nothing when it cannot be read whole.
[[nodiscard]] std::optional<std::uint32_t> CreditsIn(FileStore& _files, std::string_view _name)
{
  std::unique_ptr<OpenFile> file;
  if (_files.Open(_name, FileAccess::Read, file) != DosError::None)
  {
    return std::nullopt;
  }
  std::array<std::uint8_t, COMMANDER_FILE_BYTES> bytes{};
  std::uint32_t count = 0;
  if (file->Read(bytes, count) != DosError::None || count != bytes.size())
  {
    return std::nullopt;
  }
  CommanderState commander{};
  ReadStateBytes(commander, bytes);
  return commander.creditsTenths;
}

// The device inputDevice names, as D10's rows count it.
[[nodiscard]] std::uint8_t DeviceRowOf(std::uint8_t _inputDevice) noexcept
{
  return _inputDevice == MOUSE_DEVICE ? MOUSE_ROW : std::uint8_t{0};
}

// What D10 knows of _state and _host's disc.
[[nodiscard]] DiscFacts FactsOf(const GameState& _state, Host& _host)
{
  DiscFacts facts;
  facts.files = CommanderFilesOf(_host.Files());
  facts.deviceInUse = DeviceRowOf(_state.Data().saveLoad.inputDevice);
  facts.mouse = IsMouseDriverInstalled(_host);
  facts.commanderName = std::string(TextWithin(_state.Data().commander.defaultCommanderName, 0));
  return facts;
}

// _name and ".CDR" after it into commanderFileName, a byte at a time, as the original's prompt left them: what Start's disk work
// reads (PerformDiskRequest). The bytes after its NUL stay as they were.
void WriteCommanderFileName(GameState& _state, std::string_view _subject)
{
  const std::span<std::uint8_t> name(_state.Data().saveLoad.commanderFileName);
  const std::string_view base = _subject.substr(0, _subject.find('.'));
  std::size_t letter = 0;
  for (const char character : base)
  {
    name[letter] = static_cast<std::uint8_t>(character);
    ++letter;
  }
  for (const std::uint8_t character : COMMANDER_EXTENSION)
  {
    name[letter] = character;
    ++letter;
  }
}

// LeaveGameLoopForDisk: diskOperation=_operation, and resumeAtDiskMenu=1, so that D10 shows what Start's disk work came to when it
// opens again. The original dropped the return addresses of its call and the one under it, so that it returned through GameLoop's
// to Start: its callers' return (DiscExit::leaves).
DiscExit LeaveGameLoopForDisk(GameState& _state, std::uint8_t _operation)
{
  _state.Data().saveLoad.diskOperation = _operation;
  _state.Data().saveLoad.resumeAtDiskMenu = 1;
  return DiscExit{.leaves = true};
}

// The item whose operation _operation is.
[[nodiscard]] DiscItem ItemOf(std::uint8_t _operation) noexcept
{
  switch (_operation)
  {
  case DISK_SAVE:
    return DiscItem::Save;
  case DISK_DELETE:
    return DiscItem::Delete;
  default:
    return DiscItem::Load;
  }
}

// D10 back from Start's disk work (ShowDiskResult): what it came to, or whether to retry it. A loaded name becomes the
// commander's, as the original's eight bytes of it, its '.' a NUL.
[[nodiscard]] DiscScreen Resumed(GameState& _state, const DiscFacts& _facts)
{
  const std::uint8_t operation = _state.Data().saveLoad.diskOperation;
  const std::span<std::uint8_t> name(_state.Data().saveLoad.commanderFileName);
  DiscScreen screen = Opening(_facts);
  screen.subject = std::string(TextIn(name));
  screen.item = ItemOf(operation);
  if (_state.Data().disk.diskError != 0)
  {
    screen.prompt = DiscPrompt::Retry;
    return screen;
  }
  if (const std::optional<std::size_t> index = IndexOf(_facts.files, screen.subject))
  {
    screen.file = *index;
  }
  screen.note = operation == DISK_SAVE ? DiscNote::Saved : operation == DISK_DELETE ? DiscNote::Deleted : DiscNote::Loaded;
  if (operation == DISK_SAVE)
  {
    // The commander saved, selected in the catalogue it now heads or joins.
    screen.item = DiscItem::Load;
  }
  // The name without its extension: a NUL over its '.', which the search, going up from the second letter, looks for.
  std::size_t letter = 1;
  for (; letter < name.size() && name[letter] != '.'; ++letter)
  {
  }
  if (letter < name.size())
  {
    name[letter] = 0;
  }
  if (operation == DISK_LOAD)
  {
    const std::span<std::uint8_t> commander(_state.Data().commander.defaultCommanderName);
    std::copy_n(name.begin(), DISC_NAME_LETTERS_MOST, commander.begin());
  }
  return screen;
}

// Hides D10 when its routine ends, however it ends.
class DiscShown
{
public:
  explicit DiscShown(Host& _host) noexcept
    : m_host(_host)
  {
  }

  DiscShown(const DiscShown&) = delete;
  DiscShown& operator=(const DiscShown&) = delete;

  ~DiscShown()
  {
    m_host.HideDisc();
  }

private:
  Host& m_host;
};

} // namespace

DiscInput DiscKey(const KeyPress& _key, DiscPrompt _prompt)
{
  if (_key.scanCode == SCAN_ESC)
  {
    return DiscInput{.action = DiscAction::Back};
  }
  if (_prompt == DiscPrompt::Name)
  {
    const std::optional<std::uint8_t> typed = TypedCharacter(_key);
    if (!typed)
    {
      return DiscInput{};
    }
    if (*typed == TYPED_ENTER)
    {
      return DiscInput{.action = DiscAction::Act};
    }
    if (*typed == TYPED_BACKSPACE)
    {
      return DiscInput{.action = DiscAction::Erase};
    }
    const auto character = static_cast<std::uint8_t>(*typed >= 'a' && *typed <= 'z' ? *typed - ('a' - 'A') : *typed);
    if (!IsNameCharacter(character))
    {
      return DiscInput{};
    }
    return DiscInput{.action = DiscAction::Type, .character = character};
  }
  if (AsksYesOrNo(_prompt))
  {
    if (_key.scanCode == SCAN_Y)
    {
      return DiscInput{.action = DiscAction::Yes};
    }
    if (_key.scanCode == SCAN_N)
    {
      return DiscInput{.action = DiscAction::No};
    }
    return DiscInput{};
  }
  if (_key.scanCode >= SCAN_1 && _key.scanCode <= SCAN_6)
  {
    return DiscInput{.action = DiscAction::Choose, .item = static_cast<DiscItem>(_key.scanCode - SCAN_1)};
  }
  switch (_key.scanCode)
  {
  case SCAN_UP:
    return DiscInput{.action = DiscAction::Up};
  case SCAN_DOWN:
    return DiscInput{.action = DiscAction::Down};
  default:
    break;
  }
  const std::optional<std::uint8_t> typed = TypedCharacter(_key);
  if (typed && *typed == TYPED_ENTER)
  {
    return DiscInput{.action = DiscAction::Act};
  }
  return DiscInput{};
}

DiscInput DiscClickInput(const DiscClick& _click) noexcept
{
  switch (_click.target)
  {
  case DiscTarget::Item:
    return DiscInput{.action = DiscAction::Choose, .item = static_cast<DiscItem>(std::min<std::size_t>(_click.index, 5))};
  case DiscTarget::Row:
    return DiscInput{.action = DiscAction::Pick, .index = _click.index};
  case DiscTarget::Primary:
    return DiscInput{.action = DiscAction::Primary};
  case DiscTarget::Back:
    return DiscInput{.action = DiscAction::Back};
  }
  return DiscInput{};
}

DiscStep StepDisc(const DiscScreen& _screen, const DiscInput& _input, const DiscFacts& _facts)
{
  DiscStep step{.screen = _screen};
  DiscScreen& screen = step.screen;
  const std::size_t rows = RowsOf(_screen, _facts);
  switch (_input.action)
  {
  case DiscAction::None:
    break;
  case DiscAction::Choose:
    return Choose(_screen, _input.item, _facts);
  case DiscAction::Up:
    if (_screen.prompt == DiscPrompt::None && rows != 0)
    {
      const std::size_t at = std::min(SelectedRow(_screen), rows - 1);
      Select(screen, at == 0 ? 0 : at - 1);
    }
    break;
  case DiscAction::Down:
    if (_screen.prompt == DiscPrompt::None && rows != 0)
    {
      Select(screen, std::min(SelectedRow(_screen) + 1, rows - 1));
    }
    break;
  case DiscAction::Pick:
    if (_screen.prompt == DiscPrompt::None && _input.index < rows)
    {
      Select(screen, _input.index);
    }
    break;
  case DiscAction::Act:
    return Act(_screen, _facts);
  case DiscAction::Primary:
    return AsksYesOrNo(_screen.prompt) ? Yes(_screen) : Act(_screen, _facts);
  case DiscAction::Back:
  case DiscAction::No:
    if (_input.action == DiscAction::No && !AsksYesOrNo(_screen.prompt))
    {
      break;
    }
    return Cancel(_screen, _facts);
  case DiscAction::Yes:
    if (AsksYesOrNo(_screen.prompt))
    {
      return Yes(_screen);
    }
    break;
  case DiscAction::Type:
    if (_screen.prompt == DiscPrompt::Name && screen.typed.size() < DISC_NAME_LETTERS_MOST)
    {
      screen.typed.push_back(static_cast<char>(_input.character));
      screen.note = DiscNote::None;
    }
    break;
  case DiscAction::Erase:
    if (_screen.prompt == DiscPrompt::Name && !screen.typed.empty())
    {
      screen.typed.pop_back();
      screen.note = DiscNote::None;
    }
    break;
  }
  return step;
}

std::vector<DiscFile> CommanderFilesOf(FileStore& _files)
{
  std::vector<DiscFile> files;
  for (const FileEntry& entry : _files.List())
  {
    const std::size_t dot = entry.name.find('.');
    if (entry.attribute != 0 || dot == std::string::npos || std::string_view(entry.name).substr(dot) != COMMANDER_FILE_EXTENSION)
    {
      continue;
    }
    DiscFile file;
    file.name = entry.name;
    file.year = static_cast<std::uint16_t>(STAMP_EPOCH_YEAR + (entry.stamp.date >> STAMP_YEAR_SHIFT));
    file.month = static_cast<std::uint8_t>((entry.stamp.date >> STAMP_MONTH_SHIFT) & STAMP_MONTH_MASK);
    file.day = static_cast<std::uint8_t>(entry.stamp.date & STAMP_DAY_MASK);
    file.creditsTenths = CreditsIn(_files, entry.name);
    files.push_back(std::move(file));
    if (files.size() == MOST_COMMANDER_FILES)
    {
      break;
    }
  }
  return files;
}

DiscView DiscViewOf(const GameState& _state, const DiscScreen& _screen, const DiscFacts& _facts)
{
  const CommanderState& commander = _state.Data().commander;
  DiscView view;
  view.systemName = std::string(TextWithin(commander.currentSystem.name, 0));
  view.dodo = commander.currentSystem.techLevel >= DODO_TECH_LEVEL;
  view.creditsTenths = commander.creditsTenths;
  view.item = _screen.item;
  view.files = _facts.files;
  view.selected = _facts.files.empty() ? 0 : std::min(_screen.file, _facts.files.size() - 1);
  view.device = _screen.device;
  view.deviceInUse = _facts.deviceInUse;
  view.prompt = _screen.prompt;
  view.typed = _screen.typed;
  view.subject = _screen.subject;
  view.note = _screen.note;
  return view;
}

Task<DiscExit> ShowDiscScreen(GameState& _state, Host& _host)
{
  const DiscShown shown(_host);
  DiscFacts facts = FactsOf(_state, _host);
  DiscScreen screen = _state.Data().saveLoad.resumeAtDiskMenu == 0 ? Opening(facts) : Resumed(_state, facts);
  const auto publish = [&]() { _host.ShowDisc(DiscViewOf(_state, screen, facts)); };
  // What a step asks for, done: nothing when D10 stays open, otherwise how it ends.
  const auto perform = [&](const DiscStep& _step) -> std::optional<DiscExit>
  {
    screen = _step.screen;
    switch (_step.request)
    {
    case DiscRequest::None:
      break;
    case DiscRequest::Load:
      WriteCommanderFileName(_state, screen.subject);
      return LeaveGameLoopForDisk(_state, DISK_LOAD);
    case DiscRequest::Save:
      WriteCommanderFileName(_state, screen.subject);
      return LeaveGameLoopForDisk(_state, DISK_SAVE);
    case DiscRequest::Delete:
      WriteCommanderFileName(_state, screen.subject);
      return LeaveGameLoopForDisk(_state, DISK_DELETE);
    case DiscRequest::Retry:
      return LeaveGameLoopForDisk(_state, _state.Data().saveLoad.diskOperation);
    case DiscRequest::Quit:
      return LeaveGameLoopForDisk(_state, DISK_QUIT);
    case DiscRequest::Catalogue:
      facts.files = CommanderFilesOf(_host.Files());
      break;
    case DiscRequest::Device:
      _state.Data().saveLoad.inputDevice = screen.device == MOUSE_ROW ? MOUSE_DEVICE : KEYBOARD_DEVICE;
      facts.deviceInUse = screen.device;
      break;
    case DiscRequest::Leave:
      // Esc: back to the status screen (D119), as F6.
      _state.Data().saveLoad.resumeAtDiskMenu = 0;
      return DiscExit{.key = ScreenKey{DockedKeyOf(DockedScreen::Status)}};
    }
    return std::nullopt;
  };
  publish();
  for (;;)
  {
    co_await WaitForTimerTick(_state, _host);
    bool changed = false;
    while (const std::optional<DiscClick> click = _host.TakeDiscClick())
    {
      if (const std::optional<DiscExit> exit = perform(StepDisc(screen, DiscClickInput(*click), facts)))
      {
        co_return *exit;
      }
      changed = true;
    }
    const KeyPress key = GetKey(_state);
    if (key.scanCode != 0)
    {
      const DiscInput input = DiscKey(key, screen.prompt);
      if (input.action != DiscAction::None)
      {
        if (const std::optional<DiscExit> exit = perform(StepDisc(screen, input, facts)))
        {
          co_return *exit;
        }
        changed = true;
      }
      else if (screen.prompt == DiscPrompt::None && key.scanCode != DockedKeyOf(DockedScreen::Disc) &&
               ClosesScreen(key.scanCode, ScreenPlace::Docked))
      {
        _state.Data().saveLoad.resumeAtDiskMenu = 0;
        co_return DiscExit{.key = ScreenKey{key.scanCode}};
      }
    }
    if (changed)
    {
      publish();
    }
  }
}

} // namespace Elite

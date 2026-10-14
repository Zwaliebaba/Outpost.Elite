// GameLogic/SaveLoadState.h
#pragma once

#include "CommanderState.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What SaveLoad.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct SaveLoadState
{
  /// what ShowDiscControlScreen asks Start to do, written: 0 Exit to DOS (Start calls WipeProgram, then exits), 1 lo...
  std::uint8_t diskOperation;
  /// file names without extension, NUL-terminated, written by PerformDiskRequest's catalogue
  std::uint8_t commanderFileList[370];
  /// names in commanderFileList, capped at 40
  std::uint8_t commanderFileCount;
  /// write pointer into commanderFileList
  std::uint16_t commanderFileListNext;
  /// copy of commanderBlock taken by SaveStartupCommander
  CommanderState startupCommander;
  /// '12345678.CDR' template; load, save and delete use it
  std::uint8_t commanderFileName[13];
  /// 0 keyboard, 2 mouse (names); 1987's 1, the joystick, went with D81
  std::uint8_t inputDevice;
  /// 1 after LeaveGameLoopForDisk; makes RunTitleAndDocked reopen ShowDiscControlScreen to show the result; cleared when an F-key leaves it
  std::uint8_t resumeAtDiskMenu;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("diskOperation", _self.diskOperation);
    _visitor("commanderFileList", _self.commanderFileList);
    _visitor("commanderFileCount", _self.commanderFileCount);
    _visitor("commanderFileListNext", _self.commanderFileListNext);
    _visitor("startupCommander", _self.startupCommander);
    _visitor("commanderFileName", _self.commanderFileName);
    _visitor("inputDevice", _self.inputDevice);
    _visitor("resumeAtDiskMenu", _self.resumeAtDiskMenu);
  }

  template <typename Visitor> void Visit(Visitor& _visitor)
  {
    VisitMembers(*this, _visitor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor) const
  {
    VisitMembers(*this, _visitor);
  }
};

static_assert(std::is_trivially_copyable_v<SaveLoadState> && std::is_standard_layout_v<SaveLoadState>);

} // namespace Elite

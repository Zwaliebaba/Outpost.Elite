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
  /// what ShowDiscControlScreen asks Start to do, written at 0x7E90: 0 Exit to DOS (Start calls WipeProgram at 0x0072, then exits), 1 lo...
  std::uint8_t diskOperation;
  /// int 21h AH=1Ah buffer; find-first/next put the attribute at DS:2084 and the name at DS:208D
  std::uint8_t diskTransferArea[43];
  /// file names without extension, NUL-terminated, written by PerformDiskRequest's catalogue
  std::uint8_t commanderFileList[370];
  /// names in commanderFileList, capped at 40 (0x03E3)
  std::uint8_t commanderFileCount;
  /// write pointer into commanderFileList
  std::uint16_t commanderFileListNext;
  /// copy of commanderBlock taken by SaveStartupCommander
  CommanderState startupCommander;
  /// '12345678.CDR' template; load, save and delete use it
  std::uint8_t commanderFileName[13];
  /// 1 after Esc, J, A: the Amstrad joystick, whose moves arrive as scan codes 0x77-0x7C; ReadSteering tests it at 0x7607
  std::uint8_t joystickIsAmstrad;
  /// 0 keyboard, 1 joystick, 2 mouse (names DS:8670)
  std::uint8_t inputDevice;
  /// set when a scan code 0x77-0x7C arrives during Amstrad joystick detection
  std::uint8_t amstradJoystickMoved;
  /// ReadJoystickAxes' X count with the stick centred, taken at selection (67BA); ReadJoystickSteering scales against it
  std::uint16_t joystickCenterX;
  /// as joystickCenterX, for Y
  std::uint16_t joystickCenterY;
  /// 1 after 0x7E90; makes RunTitleAndDocked reopen ShowDiscControlScreen to show the result; cleared when an F-key leaves it
  std::uint8_t resumeAtDiskMenu;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("diskOperation", _self.diskOperation);
    _visitor("diskTransferArea", _self.diskTransferArea);
    _visitor("commanderFileList", _self.commanderFileList);
    _visitor("commanderFileCount", _self.commanderFileCount);
    _visitor("commanderFileListNext", _self.commanderFileListNext);
    _visitor("startupCommander", _self.startupCommander);
    _visitor("commanderFileName", _self.commanderFileName);
    _visitor("joystickIsAmstrad", _self.joystickIsAmstrad);
    _visitor("inputDevice", _self.inputDevice);
    _visitor("amstradJoystickMoved", _self.amstradJoystickMoved);
    _visitor("joystickCenterX", _self.joystickCenterX);
    _visitor("joystickCenterY", _self.joystickCenterY);
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

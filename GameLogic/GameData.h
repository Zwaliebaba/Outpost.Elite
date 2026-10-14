// GameLogic/GameData.h
#pragma once

#include "AiState.h"
#include "CombatState.h"
#include "DockedState.h"
#include "DockingState.h"
#include "EquipmentState.h"
#include "FlightState.h"
#include "GalaxyState.h"
#include "HyperspaceState.h"
#include "InputState.h"
#include "MarketState.h"
#include "MathsState.h"
#include "SaveLoadState.h"
#include "SceneState.h"
#include "SoundState.h"
#include "StartUpState.h"
#include "TextState.h"
#include "TimerState.h"
#include "VideoState.h"
#include "PlayerShipState.h"
#include "MessagesState.h"
#include "AudioState.h"
#include "MenusState.h"
#include "DiskState.h"
#include "DrawingState.h"
#include "WorldState.h"
#include "FlowState.h"
#include "JumpState.h"
#include "KeyboardState.h"
#include "CommanderState.h"

#include <cstddef>
#include <type_traits>

namespace Elite
{

/// The game's state (D19, D38): the reference's data segment distributed, a struct of its groups' structs, each what one subsystem writes
/// or what several share, which the GameState holds. Every member is a plain byte, word or double word at the width the
/// original computes it at, so every sum wraps where it did: native code reads and writes members (`_state.Data().commander.fuel = fuel`), and passes one by reference to a helper that writes it. The state
/// starts as SetStartState sets it (StartState.h).
struct GameData
{
  AiState ai;
  CombatState combat;
  DockedState docked;
  DockingState docking;
  EquipmentState equipment;
  FlightState flight;
  GalaxyState galaxy;
  HyperspaceState hyperspace;
  InputState input;
  MarketState market;
  MathsState maths;
  SaveLoadState saveLoad;
  SceneState scene;
  SoundState sound;
  StartUpState startUp;
  TextState text;
  TimerState timer;
  VideoState video;
  PlayerShipState playerShip;
  MessagesState messages;
  AudioState audio;
  MenusState menus;
  DiskState disk;
  DrawingState drawing;
  WorldState world;
  FlowState flow;
  JumpState jump;
  KeyboardState keyboard;
  CommanderState commander;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("ai", _self.ai);
    _visitor("combat", _self.combat);
    _visitor("docked", _self.docked);
    _visitor("docking", _self.docking);
    _visitor("equipment", _self.equipment);
    _visitor("flight", _self.flight);
    _visitor("galaxy", _self.galaxy);
    _visitor("hyperspace", _self.hyperspace);
    _visitor("input", _self.input);
    _visitor("market", _self.market);
    _visitor("maths", _self.maths);
    _visitor("saveLoad", _self.saveLoad);
    _visitor("scene", _self.scene);
    _visitor("sound", _self.sound);
    _visitor("startUp", _self.startUp);
    _visitor("text", _self.text);
    _visitor("timer", _self.timer);
    _visitor("video", _self.video);
    _visitor("playerShip", _self.playerShip);
    _visitor("messages", _self.messages);
    _visitor("audio", _self.audio);
    _visitor("menus", _self.menus);
    _visitor("disk", _self.disk);
    _visitor("drawing", _self.drawing);
    _visitor("world", _self.world);
    _visitor("flow", _self.flow);
    _visitor("jump", _self.jump);
    _visitor("keyboard", _self.keyboard);
    _visitor("commander", _self.commander);
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

static_assert(std::is_trivially_copyable_v<GameData> && std::is_standard_layout_v<GameData>);

} // namespace Elite

// GameLogic/Equipment.h
#pragma once

#include "Docked.h"
#include "Flight.h"
#include "GameState.h"
#include "Hardware.h"
#include "Input.h"
#include "Text.h"

#include <cstdint>
#include <optional>

namespace Elite
{

// The reference's equipment routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): buying and fitting equipment. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

/// Whether _key ends a menu screen: Esc, or F1 to F10.
[[nodiscard]] bool IsScreenKey(std::uint8_t _key) noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// What TryScoopObject did.
struct Scoop
{
  bool inBox;                                ///< the view position is in the scoop's box, and the object was offered to the hold
  std::optional<DashboardPixel> removedBlip; ///< once it is scooped, what RemoveObject erased of its blip
};

/// TryScoopObject (CS:4401): freeCargoTonnes, and the object in _slot scooped into the hold if its view position _view is in
/// the scoop's box: a barrel's random product or its masking device, a splinter's alloys or precious metals, an escape pod's
/// slaves, a Thargon's alien items, each removed (RemoveObject) with its message, or the message that the hold is full or the
/// object cannot be scooped.
Scoop TryScoopObject(GameState& _state, SpaceObject& _slot, const Vector& _view);

/// PayForEquipmentItem (CS:65A3): menuSelectedRow's price off creditsTenths (SubtractCredits): fuel's by the tank's emptiness,
/// by a divide whose trap saves _bx, its caller's BX, with the row in BL. Returns whether the credits were enough, and are now
/// less the price.
[[nodiscard]] bool PayForEquipmentItem(GameState& _state, std::uint16_t _bx);

/// LaunchEscapePod (CS:2F0F): an abandoned Cobra in a free ship slot, or one ReclaimShipSlot makes, the player turned away
/// at speed 20 and moved 12 frames, the escape pod and the cargo hold gone.
void LaunchEscapePod(GameState& _state);

/// SelectLaserType (CS:633B): selectedLaserType 0-3 for the laser at menuSelectedRow, counted up a type at a time as
/// the original does. Returns whether the row is a laser's.
[[nodiscard]] bool SelectLaserType(GameState& _state);

/// ShowEquipShipScreen (CS:5BF2): the F4 screen, each item up to the system's tech level with its price into screenPrices and
/// its resale price, then RunEquipShipMenu from the first item's row, which returns the key that closes it. _backward is the
/// direction flag, which DrawDockedFrame goes by. Waits for keys as a rule (ADR-015).
ScreenKey ShowEquipShipScreen(GameState& _state, Hardware& _hardware, bool _backward);

/// RunEquipShipMenu (CS:6111), with OpenEquipMessage, ShowEquipMessage, ReportNotEnoughCredits and the buying and selling it
/// runs into as one unit (ADR-012 item 14): the equipment menu from the row whose first attribute byte is at B800:_firstRow,
/// the cursor moved by the steering and the arrow keys, and B and S to buy and sell, each with its message. Returns Esc or the
/// F-key, other than F4, that ends it. Waits for keys as a rule (ADR-015).
ScreenKey RunEquipShipMenu(GameState& _state, Hardware& _hardware, std::uint16_t _firstRow);

/// ChooseMountToFitLaser (CS:63B2): the mount menu, and the mount a bought laser goes on, chosen with the steering or the
/// arrow keys and Space. Waits for keys as a rule (ADR-015).
void ChooseMountToFitLaser(GameState& _state, Hardware& _hardware);

/// ChooseMountToRemoveLaser (CS:646F): the mount menu, and the mount a sold laser comes off, as ChooseMountToFitLaser.
void ChooseMountToRemoveLaser(GameState& _state, Hardware& _hardware);

/// DrawLaserMountMenu (CS:6367): the mount header, each of the four mounts' laser or "Free" on the line after it, and
/// FORE's box highlighted; selectedLaserMount = 0. Returns where the last mount's text ends.
PrintedText DrawLaserMountMenu(GameState& _state);

/// RedrawEquipHelpText (CS:653F): the equipment screen's help lines again, over the mount menu, and textAttribute put
/// back to the menu's.
PrintedLines RedrawEquipHelpText(GameState& _state);

/// PaintLaserMountBox (CS:6564): textAttribute over the 8x3 cells of the mount box whose first attribute byte is at
/// B800:_box.
void PaintLaserMountBox(GameState& _state, std::uint16_t _box);

/// ClearEquipmentSellPrice (CS:6946): "-" in menuSelectedRow's resale column, once the last of an item is sold.
PrintedText ClearEquipmentSellPrice(GameState& _state);

/// ShowEquipmentSellPrice (CS:6972): menuSelectedRow's resale price computed by ComputeResalePrice, stored after its
/// price in screenPrices, and printed in its row's resale column.
PrintedText ShowEquipmentSellPrice(GameState& _state);

// What the menus that wait share, at each one's own addresses: no hook, and no entry.

/// Where the menu's cursor is, after StartMenu or a move.
struct MenuCursor
{
  std::uint16_t row;   ///< the selected row's first attribute byte on the text page
  PrintedText credits; ///< where PrintCreditsOnMessageLine stopped on the way
};

/// The menus' start: the credits on the message line, then row 1 selected and highlighted from menuFirstRowAttr.
MenuCursor StartMenu(GameState& _state);

/// The cursor up a row from the row at _row, from the first round to the last.
MenuCursor MoveMenuCursorUp(GameState& _state, std::uint16_t _row);

/// The cursor down a row from the row at _row, from the last round to the first.
MenuCursor MoveMenuCursorDown(GameState& _state, std::uint16_t _row);

/// What a menu that waits holds in registers from one turn of its loop to the next: where its cursor is, in SI, and AL, which
/// ReadSteering fires the stick with and GetKey shifts each key's Shift into (AlAfterKey).
struct MenuLoop
{
  std::uint16_t row; ///< the selected row's first attribute byte on the text page
  std::uint8_t al;
};

/// ReadSteering, fired with _loop's AL, and the cursor moved up or down a row if its pitch says so. Returns the cursor, and AL as
/// the original leaves it: the roll, or once the cursor moved, the attribute ToggleMenuRowHighlight left.
MenuLoop SteerMenuCursor(GameState& _state, Hardware& _hardware, MenuLoop _loop);

/// 100 timer ticks, each turn of the LOOP jumping back to CS:_tickLoop with its count (ADR-015), then GetKey. Returns the key.
KeyPress PollMenuKey(GameState& _state, Hardware& _hardware, std::uint16_t _tickLoop);

} // namespace Elite

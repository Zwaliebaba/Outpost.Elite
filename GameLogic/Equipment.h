// GameLogic/Equipment.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's equipment routines, ported (plan §5 Phase 3, ADR-010): buying and fitting equipment. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> EquipmentEntries() noexcept;

/// LaunchEscapePod (CS:2F0F): an abandoned Cobra in a slot, the player turned away at speed 20 and moved 12 frames, the
/// escape pod and the cargo hold gone. Clobbers all.
void LaunchEscapePod(Guest& _guest);

/// TryScoopObject (CS:4401): freeCargoTonnes, and the object at DI scooped into the hold if its view position AX, BX, CX
/// is in the scoop's box, with its message. Preserves AX, BX and CX.
void TryScoopObject(Guest& _guest);

/// ShowEquipShipScreen (CS:5BF2): the F4 screen, its prices into screenPrices, then RunEquipShipMenu. Waits. Out: AH=the
/// key that ended it.
void ShowEquipShipScreen(Guest& _guest);

/// SelectLaserType (CS:633B): selectedLaserType 0-3 for the laser at menuSelectedRow. Out: ZF=1 if it is a laser.
void SelectLaserType(Guest& _guest);

/// RunEquipShipMenu (CS:6111): the equipment menu from the row at SI: the cursor, and B and S to buy and sell. Waits.
/// Out: AH=Esc or the F-key that ended it.
void RunEquipShipMenu(Guest& _guest);

/// DrawLaserMountMenu (CS:6367): the four mounts and what each holds, FORE highlighted. Out: selectedLaserMount=0,
/// SI=1E5h.
void DrawLaserMountMenu(Guest& _guest);

/// ChooseMountToFitLaser (CS:63B2): the mount a bought laser goes on, chosen with the cursor and Space. Waits.
void ChooseMountToFitLaser(Guest& _guest);

/// ChooseMountToRemoveLaser (CS:646F): the mount a sold laser comes off, chosen with the cursor and Space. Waits.
void ChooseMountToRemoveLaser(Guest& _guest);

/// RedrawEquipHelpText (CS:653F): the equipment screen's help lines again, over the mount menu.
void RedrawEquipHelpText(Guest& _guest);

/// PaintLaserMountBox (CS:6564): textAttribute over the 8x3 cells of the mount box at ES:SI. SI comes back unchanged.
void PaintLaserMountBox(Guest& _guest);

/// PayForEquipmentItem (CS:65A3): menuSelectedRow's price (fuel by the tank's emptiness) off creditsTenths, and
/// creditBalanceText formatted. Out: CF=1, and the credits unchanged, when they are not enough.
void PayForEquipmentItem(Guest& _guest);

/// ClearEquipmentSellPrice (CS:6946): "-" in menuSelectedRow's resale column, once the last of an item is sold.
void ClearEquipmentSellPrice(Guest& _guest);

/// ShowEquipmentSellPrice (CS:6972): menuSelectedRow's resale price computed, stored and printed in its row.
void ShowEquipmentSellPrice(Guest& _guest);

// What the menus that wait share (RunEquipShipMenu, RunCargoTradeMenu and the two mount choosers): the same code at
// each one's own addresses. Only a routine hooked as one that waits may call them (Guest::LoopTurn).

/// The original's backward jump to CS:_target: the end of a loop's turn (Guest::LoopTurn), with IP where it lands.
void JumpBack(Guest& _guest, std::uint16_t _target);

/// The menus' start: the credits on the message line, row 1 selected and highlighted from menuFirstRowAttr.
void StartMenu(Guest& _guest);

/// ReadSteering, and the cursor moved up or down a row if its pitch says so.
void SteerMenuCursor(Guest& _guest);

/// The cursor up a row, from the first to the last.
void MoveMenuCursorUp(Guest& _guest);

/// The cursor down a row, from the last to the first.
void MoveMenuCursorDown(Guest& _guest);

/// 100 timer ticks, LOOP jumping back to CS:_tickLoop, then GetKey. Returns whether a key came, in AH.
[[nodiscard]] bool PollMenuKey(Guest& _guest, std::uint16_t _tickLoop);

/// Whether _key ends a menu screen: Esc, or F1 to F10.
[[nodiscard]] bool IsScreenKey(std::uint8_t _key) noexcept;

} // namespace Elite

// GameLogic/Equipment.h
#pragma once

#include "GameState.h"
#include "NativeEntry.h"
#include "Text.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's equipment routines, ported (plan §5 Phase 3, ADR-010): buying and fitting equipment. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> EquipmentEntries() noexcept;

/// TryScoopObject (CS:4401): freeCargoTonnes, and the object at DI scooped into the hold if its view position AX, BX, CX
/// is in the scoop's box, with its message. Preserves AX, BX and CX.
void TryScoopObject(Guest& _guest);

/// ShowEquipShipScreen (CS:5BF2): the F4 screen, its prices into screenPrices, then RunEquipShipMenu. Waits. Out: AH=the
/// key that ended it.
void ShowEquipShipScreen(Guest& _guest);

/// RunEquipShipMenu (CS:6111): the equipment menu from the row at SI: the cursor, and B and S to buy and sell. Waits.
/// Out: AH=Esc or the F-key that ended it.
void RunEquipShipMenu(Guest& _guest);

/// ChooseMountToFitLaser (CS:63B2): the mount a bought laser goes on, chosen with the cursor and Space. Waits.
void ChooseMountToFitLaser(Guest& _guest);

/// ChooseMountToRemoveLaser (CS:646F): the mount a sold laser comes off, chosen with the cursor and Space. Waits.
void ChooseMountToRemoveLaser(Guest& _guest);

/// PayForEquipmentItem (CS:65A3): menuSelectedRow's price (fuel by the tank's emptiness) off creditsTenths, and
/// creditBalanceText formatted. Out: CF=1, and the credits unchanged, when they are not enough.
void PayForEquipmentItem(Guest& _guest);

// What the menus that wait share (RunEquipShipMenu, RunCargoTradeMenu and the two mount choosers): the same code at
// each one's own addresses. Only a routine hooked as one that waits may call them (Guest::LoopTurn).

/// StartMenu on the registers: SI, DI, ES and AX as the original leaves them.
void StartMenuOnRegisters(Guest& _guest);

/// ReadSteering, and the cursor moved up or down a row if its pitch says so.
void SteerMenuCursor(Guest& _guest);

/// MoveMenuCursorUp from the row at SI, on the registers: SI, DI, ES and AX as the original leaves them.
void MoveMenuCursorUpOnRegisters(Guest& _guest);

/// MoveMenuCursorDown from the row at SI, on the registers: SI, DI, ES and AX as the original leaves them.
void MoveMenuCursorDownOnRegisters(Guest& _guest);

/// 100 timer ticks, LOOP jumping back to CS:_tickLoop, then GetKey. Returns whether a key came, in AH.
[[nodiscard]] bool PollMenuKey(Guest& _guest, std::uint16_t _tickLoop);

/// Whether _key ends a menu screen: Esc, or F1 to F10.
[[nodiscard]] bool IsScreenKey(std::uint8_t _key) noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// LaunchEscapePod (CS:2F0F): an abandoned Cobra in a free ship slot, or one ReclaimShipSlot makes, the player turned away
/// at speed 20 and moved 12 frames, the escape pod and the cargo hold gone.
void LaunchEscapePod(GameState& _state);

/// SelectLaserType (CS:633B): selectedLaserType 0-3 for the laser at menuSelectedRow, counted up a type at a time as
/// the original does. Returns whether the row is a laser's.
[[nodiscard]] bool SelectLaserType(GameState& _state);

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

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void LaunchEscapePodEntry(Guest& _guest);         ///< Clobbers all but DS.
void SelectLaserTypeEntry(Guest& _guest);         ///< Out: ZF=1 if the row is a laser's.
void DrawLaserMountMenuEntry(Guest& _guest);      ///< Out: SI=1E5h.
void RedrawEquipHelpTextEntry(Guest& _guest);     ///< Out: AX=1F00h, CX=0.
void PaintLaserMountBoxEntry(Guest& _guest);      ///< In: SI=_box, ES=B800h. Out: AL=textAttribute, DL=0, CX=0.
void ClearEquipmentSellPriceEntry(Guest& _guest); ///< Out: AX=7900h, SI and DI where it stops printing, ES=B800h.
void ShowEquipmentSellPriceEntry(Guest& _guest);  ///< Out: as ClearEquipmentSellPriceEntry, BX the price's slot.

} // namespace Elite

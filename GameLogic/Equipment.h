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

/// SelectLaserType (CS:633B): selectedLaserType 0-3 for the laser at menuSelectedRow. Out: ZF=1 if it is a laser.
void SelectLaserType(Guest& _guest);

/// PayForEquipmentItem (CS:65A3): menuSelectedRow's price (fuel by the tank's emptiness) off creditsTenths, and
/// creditBalanceText formatted. Out: CF=1, and the credits unchanged, when they are not enough.
void PayForEquipmentItem(Guest& _guest);

/// ShowEquipmentSellPrice (CS:6972): menuSelectedRow's resale price computed, stored and printed in its row.
void ShowEquipmentSellPrice(Guest& _guest);

} // namespace Elite

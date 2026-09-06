#pragma once

#include "pch.h"

#include "Controls.h"
#include "Dashboard.h"
#include "FlightLoop.h"
#include "MarketScreen.h"
#include "Presenter.h"
#include "NameEntry.h"
#include "SaveGame.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "StartUp.h"
#include "TextPrint.h"
#include "ViewChange.h"

#include <cstdint>
#include <span>

/*
 * The seams `Elite::Ports` names, all answered with nothing (slice M3-a-3).
 *
 * `Ports` is one struct over the whole library, so every fixture has to supply every reference in
 * it -- and most fixtures reach four or five. This is what the rest get: an object that satisfies
 * the interface and does not act, so a suite says which seams it cares about by passing its own
 * recorder for those and this for the others.
 *
 * IT IS NOT A RECORDER AND MUST NOT BECOME ONE. A test that wants to know whether a seam was
 * reached passes something that counts; this exists so that the seams a routine cannot reach cost
 * a fixture nothing to declare. `CommanderStore` answering false is the one place it makes a
 * CHOICE, and false is the branch a fixture that reached it would have to explain.
 *
 * `WaitFrames` used to satisfy `StartUpEffects` and `LineEntryEffects` with one override, because
 * `DELAY` is one routine in the game and the two interfaces were two views of it. It is
 * `Presenter`'s alone since M3-b-3b, which is that observation made in the library rather than in
 * every implementation of it.
 */
namespace GameLogicTests
{

  struct NullSeams : Elite::ShipDrawEffects,
                     Elite::SpawnChildEffects,
                     Elite::StartUpEffects,
                     Elite::KeySource,
                     Elite::LineEntryEffects,
                     Elite::Presenter,
                     Elite::CommanderStore
  {
    // Elite::ShipDrawEffects
    void DrawPlanetOrSun() override {}
    void DrawExplosion() override {}

    // Elite::SpawnChildEffects
    bool SpawnChild(std::uint8_t, Elite::ShipType) override { return false; }

    // Elite::StartUpEffects
    void ClearKeyLogger() override {}
    Elite::TitleKey ScanTitleKeys(Elite::KeyLogger&) override { return {}; }
    std::uint8_t ShowTitleScreen(std::uint8_t, Elite::ShipType, std::uint8_t) override { return 0; }

    // Elite::Presenter
    void WaitFrames(std::uint8_t) override {}

    // Elite::KeySource -- `TT217` BLOCKS in the game, so a fixture that reached it would hang
    // rather than fail; this answers a key nothing dispatches.
    std::uint8_t NextKey() override { return 0; }

    // Elite::LineEntryEffects
    void FlushKeyboard() override {}

    // Elite::CommanderStore -- no fixture here touches a file, and false is "the device failed".
    bool Write(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>,
               std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE>) override
    {
      return false;
    }
    bool Read(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>, std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE>) override
    {
      return false;
    }
  };

} // namespace GameLogicTests

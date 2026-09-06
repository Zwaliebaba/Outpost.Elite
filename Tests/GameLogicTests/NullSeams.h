#pragma once

#include "pch.h"

#include "Controls.h"
#include "Dashboard.h"
#include "FlightLoop.h"
#include "MarketScreen.h"
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
 * The ten seams `Elite::Ports` names, all answered with nothing (slice M3-a-3).
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
 * `WaitFrames` satisfies `StartUpEffects` and `LineEntryEffects` with one override, because
 * `DELAY` is one routine in the game and the two interfaces are two views of it -- the same
 * arrangement `SetRasterMode` has across `SightEffects` and `ExplosionEffects`.
 */
namespace GameLogicTests
{

  struct NullSeams : Elite::ShipDrawEffects,
                     Elite::FlightLoopEffects,
                     Elite::StartUpEffects,
                     Elite::SightEffects,
                     Elite::ViewEffects,
                     Elite::KeySource,
                     Elite::TradeScreenEffects,
                     Elite::LineEntryEffects,
                     Elite::CommanderStore
  {
    // Elite::ShipDrawEffects
    void DrawPlanetOrSun() override {}
    void DrawExplosion() override {}

    // Elite::FlightLoopEffects, and Elite::DashboardEffects and Elite::SpawnChildEffects under it
    bool PlaySound(std::uint8_t, bool) override { return false; }
    bool PlaySoundPitched(std::uint8_t, std::uint8_t, std::uint8_t) override { return false; }
    void StopSound(std::uint8_t) override {}
    void StartDockingMusic() override {}
    void StopDockingMusic() override {}
    bool SpawnChild(std::uint8_t, Elite::ShipType) override { return false; }

    // Elite::StartUpEffects
    void ClearKeyLogger() override {}
    void StartTheme() override {}
    void StopTheme() override {}
    Elite::TitleKey ScanTitleKeys(Elite::KeyLogger&) override { return {}; }
    void WaitFrames(std::uint8_t) override {}
    std::uint8_t ShowTitleScreen(std::uint8_t, Elite::ShipType, std::uint8_t) override { return 0; }

    // Elite::SightEffects
    void SetRasterMode(std::uint8_t) override {}
    void SetSightColour(std::uint8_t) override {}
    void SetSpritesEnabled(std::uint8_t) override {}
    void MaskSprites(std::uint8_t) override {}

    // Elite::ViewEffects -- `PlaySound` is `DashboardEffects`' as well, one `NOISE` in the game
    void SetPalette(std::uint8_t) override {}

    // Elite::KeySource -- `TT217` BLOCKS in the game, so a fixture that reached it would hang
    // rather than fail; this answers a key nothing dispatches.
    std::uint8_t NextKey() override { return 0; }

    // Elite::TradeScreenEffects, and Elite::LineEntryEffects's FlushKeyboard under it
    void SetUpTradeScreen(std::uint8_t) override {}
    void ClearBottomRows() override {}
    void ClearToView(std::uint8_t) override {}
    void BeepAndPause() override {}
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

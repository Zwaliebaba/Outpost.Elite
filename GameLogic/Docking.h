#pragma once

#include "Ports.h"
#include "Universe.h"

#include "Dashboard.h"
#include "Flight.h"
#include "StartUp.h"

#include <cstdint>

namespace Elite
{

  /*
   * Docking at the station (slice 2e).
   *
   * DOENTRY, and the ledger had it filed under the program's entry point -- `Game.cpp
   * (entry)`, alongside `COLD` and `BRKBK`, marked "Replace". It is nothing of the sort. DOENTRY is
   * what runs when the ship arrives at a station: it resets the flight variables, shows the docking
   * tunnel, and then decides which of six mission briefings the player has just earned. The name is
   * the trap -- "do entry" reads like a boot vector and means "entering the station".
   *
   * That makes the sixth stale scope line this port has found, and the first that was wrong about
   * what a routine IS rather than about what it needs.
   */

  /*
   * DELTA, GNTMP, QQ22+1, FSH, ASH and ENERGY -- what arriving resets.
   *
   * Six bytes, three to zero and three to &FF, and the six are not a group in the original: they
   * are in three different workspaces and are written by six consecutive stores that share one
   * accumulator each. Five of them are `FlightStatus`, which slice 3d-b named for the seven
   * routines that READ them rather than for this one that resets them; the sixth is `DELTA`, which
   * is `FlightState`'s, and having it here as well was one 6502 byte in two C++ fields (§6.64).
   */

  /// The pause after the tunnel -- forty-four VERTICAL SYNCS, so 0.88 seconds on PAL and
  /// 0.73 on NTSC (§6.17). It is what makes the docking tunnel readable.
  inline constexpr std::uint8_t DOCKING_PAUSE_FRAMES = 44;

  /// Which of DOENTRY's seven exits is taken. Named for the label, because two of them lead to the
  /// same screen from opposite ends of a mission.
  enum class DockingOutcome
  {
    DockingBay,      ///< A tail call to BAY, and nothing happened
    BriefMission1,   ///< The Constrictor is offered
    DebriefMission1, ///< DEBRIEF -- and paid for
    BriefMission2,   ///< The Thargoid plans are offered
    CollectPlans,    ///< Arriving at Ceerdi to pick them up
    DebriefMission2, ///< Arriving at Birera to deliver them
    OfferTrumbles,
  };

  /*
   * The tests between reading the mission byte and leaving for a briefing -- which briefing,
   * if any, docking has earned.
   *
   * A decision and nothing else, so it is separable from the arrival it is half of, and it reads
   * only the commander block: the mission bits in TP, the kill tally, the galaxy, the coordinates
   * and the cash. Three things about it are worth knowing.
   *
   * MISSION 1'S TWO BITS MEAN FOUR THINGS, not two. `%00` is not started, `%01` is in progress,
   * `%11` is "in progress AND complete", which is the state the routine reads as "you have only
   * just finished it" and answers with the debriefing -- and `%10` is finished and paid. So the
   * pair is a small state machine and the routine walks it with `AND #%00000011` and one compare.
   *
   * THE COMBAT RANK TEST READS THE HIGH BYTE OF THE TALLY ONLY. A zero high byte refuses anyone
   * with fewer than 256 kills, and mission 2 wants that byte at 5, which is 1,280 -- both
   * expressed as a byte, so the low byte of the tally never matters to either.
   *
   * AND THE TRUMBLES TEST COMPARES ONE BYTE OF FOUR. It reads the SECOND LEAST significant byte
   * of a four-byte big-endian value against 196 and ignores the two above it, so the condition is
   * not "at least 5017.6 credits" -- it is `(tenths >> 8) & 255 >= 196`, which is a BAND that
   * recurs every 6553.6 credits. A player with 5017.6 credits is offered the mission and one with
   * 10,000 is not. The upstream source's own two comments on those three instructions disagree
   * with each other about which threshold it is; neither is right, and the sweep in the tests
   * walks the values that tell them apart.
   */
  [[nodiscard]] DockingOutcome MissionOnDocking(const Commander& _commander) noexcept;

  struct DockingResult
  {
    DockingOutcome outcome = DockingOutcome::DockingBay;

    /// What the tail call to BAY produced. Only the DockingBay outcome reaches it; every
    /// briefing is a tail call of its own and the docking bay is what it eventually returns to.
    ForcedKey bay{};
  };

  /*
   * Arrive at the station.
   *
   * `RES2`, then `LAUN`, then six stores, then a pause, then the dispatch. Note that RES2 is
   * called here on its own, where the cold start reaches it twice through two fall-throughs
   * (§6.25) -- so the same routine is one call on arrival and two on a restart.
   *
   * `LAUN` is the tunnel and it is drawn HERE rather than behind a seam, which is why the universe
   * is an argument at all: the commander, the status and the flight state came in separately until
   * the tunnel was ported, and every one of them is inside `Universe` since M3-a.
   */
  [[nodiscard]] DockingResult DockAtStation(Universe& _universe, Ports& _ports, std::uint8_t _view,
                                            bool _hyperspaceHeld) noexcept;

} // namespace Elite

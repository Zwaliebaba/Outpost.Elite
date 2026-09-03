#include "pch.h"

#include "Docking.h"

/*
 * Docking at the station (slice 2e).
 */

namespace Elite
{

namespace
{
/// 6502: AND #%00000011 and AND #%00001111 -- the mission bits, read two ways by one routine.
constexpr std::uint8_t MISSION_1_BITS = 0x03;
constexpr std::uint8_t MISSION_BITS = 0x0F;

/// 6502: AND #%00010000 -- bit 4, which says the Trumbles have already been offered.
constexpr std::uint8_t TRUMBLES_OFFERED = 0x10;

/*
 * 6502: the four states bits 0 and 1 of TP hold for mission 1.
 *
 * Not a flag and a spare: `%11` is both bits set at once, and the routine reads it as "you have
 * only just finished" -- the state that earns the debriefing. `%10` is what it becomes afterwards.
 */
constexpr std::uint8_t MISSION_1_NOT_STARTED = 0x00;
constexpr std::uint8_t MISSION_1_JUST_FINISHED = 0x03;

/// 6502: CMP #%00000010 / CMP #%00000110 / CMP #%00001010 -- bits 0 to 3, and the three stages of
/// mission 2 that DOENTRY can act on.
constexpr std::uint8_t MISSION_2_NOT_STARTED = 0x02;
constexpr std::uint8_t MISSION_2_AWAITING_PLANS = 0x06;
constexpr std::uint8_t MISSION_2_CARRYING_PLANS = 0x0A;

/// 6502: LDA TALLY+1 / BEQ and CMP #5 / BCC -- the two ranks, both as the HIGH byte of the tally
/// alone, so they are 256 and 1,280 kills and the low byte never matters.
constexpr std::uint8_t RANK_FOR_MISSION_1 = 1;
constexpr std::uint8_t RANK_FOR_MISSION_2 = 5;

/// 6502: LDA GCNT / LSR A / BNE -- galaxy 0 or 1, expressed as a shift rather than a compare.
constexpr std::uint8_t GALAXY_FOR_MISSION_2 = 2;

/// 6502: CMP #215 / CMP #84 and CMP #63 / CMP #72 -- Ceerdi and Birera, as coordinates rather
/// than as system numbers, so the check survives the generator producing them anywhere.
constexpr std::uint8_t CEERDI_X = 215;
constexpr std::uint8_t CEERDI_Y = 84;
constexpr std::uint8_t BIRERA_X = 63;
constexpr std::uint8_t BIRERA_Y = 72;

/// 6502: CMP #&C4 against CASH+2 -- one byte of four, which is the whole finding.
constexpr std::uint8_t TRUMBLES_CASH_BYTE = 0xC4;

/*
 * 6502: EN4 and EN6 -- the tail every path that has not earned a briefing falls into.
 *
 * Eight branches in DOENTRY reach EN4, which is why it is a function here: the alternative is the
 * same three tests written eight times, and a port that forgot one of them would offer the
 * Trumbles from seven states and not the eighth.
 *
 * `LDA CASH+2 / CMP #&C4` is the one worth stopping on. CASH is FOUR bytes, most significant
 * first, holding tenths of a credit -- and this reads the third of them and ignores the two above
 * it. So the test is not "have you got 5017.6 credits": it is whether bits 8 to 15 of the tenths
 * are at least 196, which is true in a band 1536 credits wide that recurs every 6553.6. Rich
 * enough and you stop qualifying; get richer still and you qualify again.
 */
[[nodiscard]] DockingOutcome TrumblesOrBay(const CommanderBlock& _commander) noexcept
{
  const std::uint8_t cashByte = _commander.bytes[static_cast<std::size_t>(Field::Cash) + 2u];
  if (cashByte < TRUMBLES_CASH_BYTE)
  {
    return DockingOutcome::DockingBay;
  }

  // 6502: LDA TP / AND #%00010000 / BNE EN6 -- offered once, and the bit remembers it.
  if ((_commander.At(Field::MissionProgress) & TRUMBLES_OFFERED) != 0u)
  {
    return DockingOutcome::DockingBay;
  }

  return DockingOutcome::OfferTrumbles;
}
} // namespace

DockingOutcome MissionOnDocking(const CommanderBlock& _commander) noexcept
{
  const std::uint8_t missions = _commander.At(Field::MissionProgress);
  const std::uint8_t killsHigh = _commander.bytes[static_cast<std::size_t>(Field::Kills) + 1u];
  const std::uint8_t galaxy = _commander.At(Field::GalaxyNumber);

  // 6502: LDA TP / AND #%00000011 / BNE EN1.
  const std::uint8_t mission1 = static_cast<std::uint8_t>(missions & MISSION_1_BITS);
  if (mission1 == MISSION_1_NOT_STARTED)
  {
    // 6502: LDA TALLY+1 / BEQ EN4 -- 256 kills, as a byte.
    if (killsHigh < RANK_FOR_MISSION_1)
    {
      return TrumblesOrBay(_commander);
    }

    // 6502: LDA GCNT / LSR A / BNE EN4 -- galaxy 0 or 1, and a shift says so in three bytes.
    if ((galaxy >> 1) != 0u)
    {
      return TrumblesOrBay(_commander);
    }

    return DockingOutcome::BriefMission1;
  }

  // 6502: EN1 -- CMP #%00000011 / BNE EN2. Both bits set is "in progress AND complete".
  if (mission1 == MISSION_1_JUST_FINISHED)
  {
    return DockingOutcome::DebriefMission1;
  }

  // 6502: EN2 -- LDA GCNT / CMP #2 / BNE EN4.
  if (galaxy != GALAXY_FOR_MISSION_2)
  {
    return TrumblesOrBay(_commander);
  }

  // 6502: LDA TP / AND #%00001111 -- FOUR bits now, where the first test used two.
  const std::uint8_t stage = static_cast<std::uint8_t>(missions & MISSION_BITS);

  if (stage == MISSION_2_NOT_STARTED)
  {
    // 6502: LDA TALLY+1 / CMP #5 / BCC EN4 -- 1,280 kills.
    if (killsHigh < RANK_FOR_MISSION_2)
    {
      return TrumblesOrBay(_commander);
    }
    return DockingOutcome::BriefMission2;
  }

  // 6502: EN3 -- CMP #%00000110 / BNE EN5, then Ceerdi's coordinates.
  if (stage == MISSION_2_AWAITING_PLANS)
  {
    if (_commander.At(Field::SystemX) != CEERDI_X || _commander.At(Field::SystemY) != CEERDI_Y)
    {
      return TrumblesOrBay(_commander);
    }
    return DockingOutcome::CollectPlans;
  }

  // 6502: EN5 -- CMP #%00001010 / BNE EN4, then Birera's.
  if (stage == MISSION_2_CARRYING_PLANS)
  {
    if (_commander.At(Field::SystemX) != BIRERA_X || _commander.At(Field::SystemY) != BIRERA_Y)
    {
      return TrumblesOrBay(_commander);
    }
    return DockingOutcome::DebriefMission2;
  }

  return TrumblesOrBay(_commander);
}

DockingResult DockAtStation(StartUpEffects& _effects, CommanderBlock& _commander, DockedShip& _ship,
                            std::uint8_t& _dockedFlag, std::uint8_t _view, bool _hyperspaceHeld) noexcept
{
  // 6502: JSR RES2 -- once here, where the cold start reaches it twice (§6.25).
  _effects.ResetShip();

  // 6502: JSR LAUN.
  _effects.ShowDockingTunnel();

  /*
   * 6502: LDA #0 / STA DELTA / STA GNTMP / STA QQ22+1 / LDA #&FF / STA FSH / STA ASH / STA ENERGY.
   *
   * Two loads and six stores. Three instructions between the first two stores are commented out
   * in the original -- ALPHA, BETA, ALP1 and BET1, the roll and pitch -- and RES2 has already
   * zeroed them, which is presumably why.
   */
  _ship.speed = 0;
  _ship.laserTemperature = 0;
  _ship.hyperspaceCountdown = 0;
  _ship.forwardShield = 0xFF;
  _ship.aftShield = 0xFF;
  _ship.energy = 0xFF;

  // 6502: LDY #44 / JSR DELAY.
  _effects.WaitFrames(DOCKING_PAUSE_FRAMES);

  DockingResult result{};
  result.outcome = MissionOnDocking(_commander);

  /*
   * 6502: EN6 -- JMP BAY, and only this exit reaches it. Every briefing is a tail call that ends
   * somewhere of its own; the counter BAY's dispatch reads is the one zeroed four lines above.
   */
  if (result.outcome == DockingOutcome::DockingBay)
  {
    result.bay = EnterDockingBay(_dockedFlag, _view, _ship.hyperspaceCountdown, _hyperspaceHeld);
  }

  return result;
}

} // namespace Elite

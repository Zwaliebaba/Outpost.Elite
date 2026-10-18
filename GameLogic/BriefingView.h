// GameLogic/BriefingView.h
#pragma once

#include "StatusView.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Elite
{

/// A mission's transmission (D09): the briefing that gives it, or the debriefing that closes it.
enum class BriefingKind : std::uint8_t
{
  Briefing,
  Debriefing,
};

/// What the transmission waits for: Space, or any key, to acknowledge it; or the supernova's Y or N.
enum class BriefingAnswer : std::uint8_t
{
  Acknowledge,
  YesOrNo,
};

/// What a debriefing gives: the credits, the masking device fitted, or the anti-ECM emulator and the Archangel title; nothing for
/// a briefing, or a mask ship destroyed or fled from.
enum class BriefingReward : std::uint8_t
{
  None,
  Credits,
  MaskingDevice,
  AntiEcmAndArchangel,
};

/// What D09, a mission's briefing or debriefing over D08 (D82, D101, D105, ADR-068), shows: plain values for the shell (R9), which
/// words the titles and the labels. The game publishes one while the original routine waits for its key (Host::ShowBriefing).
struct BriefingView
{
  std::uint64_t sequence = 0; ///< one more for each view published
  StatusView status;          ///< D08 under it, as the game stands while it waits
  std::uint8_t mission = 1;   ///< 1 the supernova's refugees, 2 the masking device, 3 the Thargoid invasion
  BriefingKind kind = BriefingKind::Briefing;
  std::vector<std::string> paragraphs; ///< the game's text, its lines joined and its "Press any key" left out
  BriefingAnswer answer = BriefingAnswer::Acknowledge;
  BriefingReward reward = BriefingReward::None;
  std::uint32_t rewardTenths = 0; ///< for Credits
};

} // namespace Elite

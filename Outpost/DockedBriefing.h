// Outpost/DockedBriefing.h
#pragma once

#include "BriefingView.h"
#include "Canvas.h"
#include "DockedFrame.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Outpost
{

/// D09, a mission's briefing or debriefing (ADR-068, D82, D101, D105, `Design/Canvas/docked/09-mission-briefing.png`), drawn from the
/// game's BriefingView as a modal over D08, in 1920 x 1080 design units: the transmission's tab and header, who it is from, the
/// mission's title, the game's text, what a debriefing gives, and the button that answers it. A click on a button is sent as its
/// key, as the original routine waits for a key.
class DockedBriefing
{
public:
  /// The transmission of _view over D08, with its frame, into _canvas: the button under _pointer, if any, shown as hovered.
  static void Draw(const Elite::BriefingView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                   Engine::Canvas& _canvas);

  /// The scan code a click at _point sends while _view waits: Space on SPACE · ACKNOWLEDGE, Y or N on the supernova's buttons;
  /// nothing elsewhere.
  [[nodiscard]] static std::optional<std::uint8_t> ScanCodeAt(const Elite::BriefingView& _view, Engine::Point _point) noexcept;

  /// The mission's title, which the game's texts do not give (ADR-068): Supernova, The masking device, The Thargoid invasion.
  [[nodiscard]] static std::wstring TitleOf(const Elite::BriefingView& _view);

  /// Who the transmission is from: the present system's people for the supernova, Navy Intelligence otherwise.
  [[nodiscard]] static std::wstring SenderOf(const Elite::BriefingView& _view);

  /// What a debriefing gives, as its REWARD box says it, or nothing.
  [[nodiscard]] static std::wstring RewardOf(const Elite::BriefingView& _view);
};

} // namespace Outpost

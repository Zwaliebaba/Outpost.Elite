#include "pch.h"

#include "Frame.h"

#include "Picture.h"
#include "Presenter.h"

namespace Elite
{

  void EndFrame(Picture* _frame) noexcept
  {
    /*
     * A MARK AND NOT A MEMSET, which is the whole of RN-1's second finding.
     *
     * `Picture::EndFrame` only records that the frame is over; the next write that LANDS clears
     * first. An eager clear here would be identical on the glass and wrong to everything that looks
     * BETWEEN two passes -- the presenter a moment later, the replay's checkpoint, every recorded
     * picture digest -- because all of them would see a blank surface rather than the frame that
     * was just finished.
     *
     * Null is a real argument and not a missing one: three of the nineteen present sites have no
     * frame in scope, because what they are presenting has nothing of the frame on it.
     */
    if (_frame != nullptr)
    {
      _frame->EndFrame();
    }
  }

  void WaitFrames(Presenter& _present, Picture* _frame, std::uint8_t _frames) noexcept
  {
    _present.WaitFrames(_frames);
    EndFrame(_frame);
  }

  void PresentFrame(Presenter& _present, Picture* _frame) noexcept
  {
    _present.Present();
    EndFrame(_frame);
  }

  void HoldFlightFrame(Presenter& _present, Picture* _frame, std::uint8_t _ships) noexcept
  {
    _present.HoldFlightFrame(_ships);
    EndFrame(_frame);
  }

  void HoldTitleFrame(Presenter& _present, Picture* _frame, std::uint8_t _distance) noexcept
  {
    _present.HoldTitleFrame(_distance);
    EndFrame(_frame);
  }

} // namespace Elite

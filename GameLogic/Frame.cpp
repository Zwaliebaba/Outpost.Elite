#include "pch.h"

#include "Frame.h"

#include "Picture.h"
#include "Presenter.h"

namespace Elite
{

  void EndFrame(Picture* _frame) noexcept
  {
    /*
     * `if constexpr` rather than a runtime test, so that with the clear off this compiles to
     * nothing at all and the first commit cannot be accused of costing a frame's memset.
     */
    if constexpr (CLEAR_THE_FRAME)
    {
      if (_frame != nullptr)
      {
        _frame->Clear();
      }
    }
    else
    {
      static_cast<void>(_frame);
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

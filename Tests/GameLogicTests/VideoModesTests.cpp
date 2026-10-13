#include "pch.h"

#include "Cga.h"
#include "GameState.h"
#include "Timing.h"
#include "VideoModes.h"

#include <array>
#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The IBM PC BIOS's CRTC tables (VIDEO_PARMS).
constexpr std::array<std::uint8_t, 16> CRTC_40X25 = {0x38, 0x28, 0x2D, 0x0A, 0x1F, 0x06, 0x19, 0x1C,
                                                     0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<std::uint8_t, 16> CRTC_80X25 = {0x71, 0x50, 0x5A, 0x0A, 0x1F, 0x06, 0x19, 0x1C,
                                                     0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<std::uint8_t, 16> CRTC_GRAPHICS = {0x38, 0x28, 0x2D, 0x0A, 0x7F, 0x06, 0x64, 0x70,
                                                        0x02, 0x01, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00};

// The BIOS's video services on their own, over a GameState's video memory and a CGA on its own clock.
class VideoRig
{
public:
  VideoRig()
    : m_state(std::make_unique<Elite::GameState>()),
      m_cga(m_clock),
      m_modes(*m_state, m_cga)
  {
  }

  [[nodiscard]] Elite::VideoModes& Services() noexcept
  {
    return m_modes;
  }

  [[nodiscard]] Elite::Cga& Card() noexcept
  {
    return m_cga;
  }

  void FillVideo(std::uint8_t _value)
  {
    for (std::uint32_t offset = 0; offset < Elite::Cga::VIDEO_MEMORY_BYTES; ++offset)
    {
      m_state->SetVideoByte(static_cast<std::uint16_t>(offset), _value);
    }
  }

  // Whether video memory holds _even at every even offset and _odd at every odd one.
  [[nodiscard]] bool VideoHolds(std::uint8_t _even, std::uint8_t _odd) const
  {
    for (std::uint32_t offset = 0; offset < Elite::Cga::VIDEO_MEMORY_BYTES; ++offset)
    {
      if (m_state->VideoByte(static_cast<std::uint16_t>(offset)) != ((offset & 1) == 0 ? _even : _odd))
      {
        return false;
      }
    }
    return true;
  }

  // Whether the CGA's CRTC registers 0-15 are _table's.
  [[nodiscard]] bool CrtcHolds(const std::array<std::uint8_t, 16>& _table) const noexcept
  {
    for (std::size_t index = 0; index < _table.size(); ++index)
    {
      if (m_cga.Crtc(static_cast<Elite::CrtcRegister>(index)) != _table[index])
      {
        return false;
      }
    }
    return true;
  }

private:
  Elite::Cycles m_clock = 0;
  std::unique_ptr<Elite::GameState> m_state; // over 100 KiB
  Elite::Cga m_cga;
  Elite::VideoModes m_modes;
};

} // namespace

// The BIOS's video services (VideoModes, ADR-041): the emulated PC's int 10h, and the little of the data area it reads back.
TEST_CLASS(VideoModesTests)
{
public:
  // What the BIOS leaves for DOS: video mode 3 set on the card and in video memory.
  TEST_METHOD(PowerOnStateIsWhatTheBiosLeavesForDos)
  {
    VideoRig rig;
    Assert::IsTrue(rig.CrtcHolds(CRTC_80X25), L"the 80x25 CRTC table");
    Assert::AreEqual(0x29u, std::uint32_t{rig.Card().ModeControl()});
    Assert::AreEqual(0x30u, std::uint32_t{rig.Card().ColorSelect()});
    Assert::IsTrue(rig.VideoHolds(0x20, 0x07), L"spaces, light grey on black");
  }

  // SetGraphicsMode (CS:7D1E): mode 4 for flight, the title and the charts.
  TEST_METHOD(SetMode4ProgramsTheCgaAndClearsVideoMemory)
  {
    VideoRig rig;
    rig.FillVideo(0x55);

    Assert::IsTrue(rig.Services().SetMode(4));

    Assert::IsTrue(rig.CrtcHolds(CRTC_GRAPHICS), L"the graphics CRTC table");
    Assert::AreEqual(0x2Au, std::uint32_t{rig.Card().ModeControl()});
    Assert::AreEqual(0x30u, std::uint32_t{rig.Card().ColorSelect()});
    Assert::IsTrue(rig.VideoHolds(0x00, 0x00), L"all 16 KiB cleared to zero");
  }

  // SetTextMode (CS:7D31): mode 1 for the docked screens.
  TEST_METHOD(SetMode1ProgramsTheCgaAndBlanksTheText)
  {
    VideoRig rig;
    rig.Services().WriteTeletype('O');
    rig.Services().WriteTeletype('K');
    rig.FillVideo(0x55);

    Assert::IsTrue(rig.Services().SetMode(1));

    Assert::IsTrue(rig.CrtcHolds(CRTC_40X25), L"the 40x25 CRTC table");
    Assert::AreEqual(0x28u, std::uint32_t{rig.Card().ModeControl()});
    Assert::IsTrue(rig.VideoHolds(0x20, 0x07), L"spaces, light grey on black");
  }

  // SetGraphicsMode's next call, int 10h AH=0Bh BH=1: palette 0, colour select's bit 5 cleared from the data area's copy.
  TEST_METHOD(PaletteIsReadModifyWrite)
  {
    VideoRig rig;
    Assert::IsTrue(rig.Services().SetMode(4));

    rig.Services().SelectPalette(0);
    Assert::AreEqual(0x10u, std::uint32_t{rig.Card().ColorSelect()}, L"palette 0: bit 5 cleared");
    rig.Services().SelectPalette(1);
    Assert::AreEqual(0x30u, std::uint32_t{rig.Card().ColorSelect()}, L"palette 1: bit 5 set again");
  }

  // A mode above 6 the BIOS refuses here, and nothing changes. The game never asks for one: Hardware throws std::logic_error.
  TEST_METHOD(UnsupportedModeChangesNothing)
  {
    VideoRig rig;
    Assert::IsFalse(rig.Services().SetMode(7));
    Assert::IsTrue(rig.CrtcHolds(CRTC_80X25), L"still mode 3");
    Assert::IsTrue(rig.VideoHolds(0x20, 0x07), L"and its spaces, light grey on black");
  }
};

} // namespace GameLogicTests

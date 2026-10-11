#include "pch.h"

#include "Bios.h"
#include "Cga.h"
#include "GameState.h"
#include "Timing.h"

#include <array>
#include <memory>
#include <optional>

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

// What SET_MODE writes: mode control (video off), the 16 CRTC registers by index and value, mode control, colour select.
constexpr std::uint64_t MODE_SET_PORT_WRITES = 1 + 16 * 2 + 1 + 1;

// The ticks at noon, the default start moment: 43,200 s at 1,193,181.67 / 65,536 ticks a second.
constexpr std::uint32_t NOON_TICKS = 0x000C0059;

// The BIOS on its own, over a GameState's video memory and a CGA on its own clock, powered on at noon.
class BiosRig
{
public:
  BiosRig()
    : m_state(std::make_unique<Elite::GameState>()),
      m_cga(m_clock),
      m_bios(*m_state, m_cga, Elite::Bios::TimerTicksAt(12 * 60 * 60 * 100))
  {
  }

  [[nodiscard]] Elite::Bios& Services() noexcept
  {
    return m_bios;
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
  Elite::Bios m_bios;
};

} // namespace

// The BIOS's services and its data area (Bios, D33): the emulated PC's int 10h, 16h and 1Ah, and the data area they keep,
// which is no longer memory. What the ROM's code wrote is counted as the emulated PC counted it: the data area's bytes a
// write changed, and the OUTs it made.
TEST_CLASS(BiosTests)
{
public:
  // What the BIOS leaves for DOS: video mode 3 set on the card and in video memory, the cursor home, the keyboard buffer
  // empty, and the tick count the start moment's time of day.
  TEST_METHOD(PowerOnStateIsWhatTheBiosLeavesForDos)
  {
    BiosRig rig;
    Assert::IsTrue(rig.CrtcHolds(CRTC_80X25), L"the 80x25 CRTC table");
    Assert::AreEqual(0x29u, std::uint32_t{rig.Card().ModeControl()});
    Assert::AreEqual(0x30u, std::uint32_t{rig.Card().ColorSelect()});
    Assert::IsTrue(rig.VideoHolds(0x20, 0x07), L"spaces, light grey on black");
    Assert::IsTrue(rig.Services().TeletypeAvailable(), L"a text mode");
    Assert::AreEqual(0u, std::uint32_t{rig.Services().CursorColumn()});
    Assert::IsFalse(rig.Services().PeekKey().has_value(), L"the keyboard buffer empty");
    Assert::AreEqual(NOON_TICKS, rig.Services().Ticks(), L"ticks at noon: 786,521");
    Assert::AreEqual(Elite::Bios::TICKS_PER_DAY - 1, Elite::Bios::TimerTicksAt(8'639'999), L"never a whole day");
  }

  // SetGraphicsMode (CS:7D1E): mode 4 for flight, the title and the charts.
  TEST_METHOD(SetMode4ProgramsTheCgaAndClearsVideoMemory)
  {
    BiosRig rig;
    rig.FillVideo(0x55);
    const std::uint64_t writes = rig.Services().PortWrites();
    const std::uint64_t changes = rig.Services().ChangeCount();

    Assert::IsTrue(rig.Services().SetMode(4));

    Assert::AreEqual(MODE_SET_PORT_WRITES, rig.Services().PortWrites() - writes, L"the CGA register writes");
    Assert::IsTrue(rig.CrtcHolds(CRTC_GRAPHICS), L"the graphics CRTC table");
    Assert::AreEqual(0x2Au, std::uint32_t{rig.Card().ModeControl()});
    Assert::AreEqual(0x30u, std::uint32_t{rig.Card().ColorSelect()});
    Assert::IsTrue(rig.VideoHolds(0x00, 0x00), L"all 16 KiB cleared to zero");
    Assert::IsFalse(rig.Services().TeletypeAvailable(), L"a graphics mode");
    Assert::IsTrue(rig.Services().ChangeCount() > changes, L"the data area's mode, columns and page size, counted");
  }

  // SetTextMode (CS:7D31): mode 1 for the docked screens.
  TEST_METHOD(SetMode1ProgramsTheCgaAndBlanksTheText)
  {
    BiosRig rig;
    for (const char character : {'O', 'K'})
    {
      Assert::IsTrue(rig.Services().WriteTeletype(static_cast<std::uint8_t>(character)));
    }
    Assert::AreEqual(2u, std::uint32_t{rig.Services().CursorColumn()});
    rig.FillVideo(0x55);
    const std::uint64_t writes = rig.Services().PortWrites();

    Assert::IsTrue(rig.Services().SetMode(1));

    Assert::AreEqual(MODE_SET_PORT_WRITES, rig.Services().PortWrites() - writes, L"the CGA register writes");
    Assert::IsTrue(rig.CrtcHolds(CRTC_40X25), L"the 40x25 CRTC table");
    Assert::AreEqual(0x28u, std::uint32_t{rig.Card().ModeControl()});
    Assert::IsTrue(rig.VideoHolds(0x20, 0x07), L"spaces, light grey on black");
    Assert::AreEqual(0u, std::uint32_t{rig.Services().CursorColumn()}, L"cursor home");
  }

  // SetGraphicsMode's next call, int 10h AH=0Bh BH=1: palette 0, colour select's bit 5 cleared from the data area's copy.
  TEST_METHOD(PaletteIsReadModifyWrite)
  {
    BiosRig rig;
    Assert::IsTrue(rig.Services().SetMode(4));
    const std::uint64_t writes = rig.Services().PortWrites();

    rig.Services().SelectPalette(0);
    Assert::AreEqual(0x10u, std::uint32_t{rig.Card().ColorSelect()}, L"palette 0: bit 5 cleared");
    Assert::AreEqual(std::uint64_t{1}, rig.Services().PortWrites() - writes, L"one OUT to 3D9h");
    rig.Services().SelectPalette(1);
    Assert::AreEqual(0x30u, std::uint32_t{rig.Card().ColorSelect()}, L"palette 1: bit 5 set again");
  }

  // A mode above 6 the BIOS refuses here, and nothing changes; Hardware stops the run.
  TEST_METHOD(UnsupportedModeChangesNothing)
  {
    BiosRig rig;
    const std::uint64_t writes = rig.Services().PortWrites();
    const std::uint64_t changes = rig.Services().ChangeCount();
    Assert::IsFalse(rig.Services().SetMode(7));
    Assert::AreEqual(writes, rig.Services().PortWrites());
    Assert::AreEqual(changes, rig.Services().ChangeCount());
    Assert::IsTrue(rig.CrtcHolds(CRTC_80X25));
    Assert::IsTrue(rig.Services().TeletypeAvailable(), L"still mode 3");
  }

  // ExitToDos (CS:00B9) flushes the keyboard: AH=01h until ZF, AH=00h for each key. The ROM's own int 9 keeps no key, so a
  // key is in the buffer only when a test puts one there.
  TEST_METHOD(KeyboardBufferPeeksAndReads)
  {
    BiosRig rig;
    Assert::IsFalse(rig.Services().PeekKey().has_value(), L"no key");
    Assert::IsFalse(rig.Services().ReadKey().has_value(), L"and reading none would wait for ever: Hardware stops the run");

    const std::uint64_t changes = rig.Services().ChangeCount();
    rig.Services().InsertKey(0x1E61);
    Assert::AreEqual(changes + 3, rig.Services().ChangeCount(), L"the key's two bytes and the tail's low byte");
    Assert::AreEqual(0x1E61u, std::uint32_t{rig.Services().PeekKey().value_or(0)});
    Assert::AreEqual(0x1E61u, std::uint32_t{rig.Services().PeekKey().value_or(0)}, L"a peek leaves it there");
    Assert::AreEqual(0x1E61u, std::uint32_t{rig.Services().ReadKey().value_or(0)});
    Assert::IsFalse(rig.Services().PeekKey().has_value(), L"a read takes it out");
  }

  // RestoreTimerInterrupt (CS:016B) reads the count and sets it back a day when it is over one. The timer handler's count
  // starts again from 0 once a day has passed, and sets the flag that a read reports and clears.
  TEST_METHOD(TimeOfDayReadsAndSetsTheTickCount)
  {
    BiosRig rig;
    rig.Services().SetClock(0x001800AF);
    Assert::AreEqual(0x001800AFu, rig.Services().Ticks());

    const Elite::Bios::Tick tick = rig.Services().CountTick();
    Assert::IsTrue(tick.dayCheck && tick.dayEnded && !tick.carried, L"the day's last tick");
    const Elite::BiosClock clock = rig.Services().ReadClock();
    Assert::AreEqual(0u, clock.ticks);
    Assert::AreEqual(1u, std::uint32_t{clock.rolledOver}, L"midnight has passed");
    Assert::AreEqual(0u, std::uint32_t{rig.Services().ReadClock().rolledOver}, L"the flag is cleared by reading it");
  }
};

} // namespace GameLogicTests

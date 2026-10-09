#include "pch.h"

#include "ServiceRig.h"

#include "Firmware.h"

#include <array>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint16_t BIOS_DATA = Machine::Firmware::DATA_SEGMENT;
constexpr std::uint16_t VIDEO = 0xB800;
constexpr std::uint32_t VIDEO_BYTES = 0x4000;

// The IBM PC BIOS's CRTC tables (VIDEO_PARMS).
constexpr std::array<std::uint8_t, 16> CRTC_40X25 = {0x38, 0x28, 0x2D, 0x0A, 0x1F, 0x06, 0x19, 0x1C,
                                                     0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<std::uint8_t, 16> CRTC_GRAPHICS = {0x38, 0x28, 0x2D, 0x0A, 0x7F, 0x06, 0x64, 0x70,
                                                        0x02, 0x01, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00};

// What SET_MODE sends: video off, the 16 CRTC registers, the mode control, the colour select.
std::vector<PortWrite> ModeSetWrites(const std::array<std::uint8_t, 16>& _crtc, std::uint8_t _modeControl, std::uint8_t _colorSelect)
{
  std::vector<PortWrite> writes = {{0x3D8, 0x00}};
  for (std::size_t index = 0; index < _crtc.size(); ++index)
  {
    writes.push_back({0x3D4, static_cast<std::uint8_t>(index)});
    writes.push_back({0x3D5, _crtc[index]});
  }
  writes.push_back({0x3D8, _modeControl});
  writes.push_back({0x3D9, _colorSelect});
  return writes;
}

void FillVideo(Machine::Memory& _memory, std::uint8_t _value)
{
  for (std::uint32_t offset = 0; offset < VIDEO_BYTES; ++offset)
  {
    _memory.Write8(VIDEO, static_cast<std::uint16_t>(offset), _value);
  }
}

// Whether video memory holds _even at every even offset and _odd at every odd one.
bool VideoHolds(Machine::Memory& _memory, std::uint8_t _even, std::uint8_t _odd)
{
  for (std::uint32_t offset = 0; offset < VIDEO_BYTES; ++offset)
  {
    if (_memory.Read8(VIDEO, static_cast<std::uint16_t>(offset)) != ((offset & 1) == 0 ? _even : _odd))
    {
      return false;
    }
  }
  return true;
}

std::uint32_t BiosByte(ServiceRig& _rig, std::uint16_t _offset)
{
  return _rig.Ram().Read8(BIOS_DATA, _offset);
}

std::uint32_t BiosWord(ServiceRig& _rig, std::uint16_t _offset)
{
  return _rig.Ram().Read16(BIOS_DATA, _offset);
}

} // namespace

TEST_CLASS(BiosTests)
{
public:
  // SetGraphicsMode (CS:7D1E): mode 4 for flight, the title and the charts.
  TEST_METHOD(SetMode4ProgramsTheCgaAndClearsVideoMemory)
  {
    ServiceRig rig("BiosMode4");
    FillVideo(rig.Ram(), 0x55);
    rig.Regs().ax = 0x0004;

    rig.Interrupt(0x10);

    Assert::IsTrue(rig.Bus().Writes() == ModeSetWrites(CRTC_GRAPHICS, 0x2A, 0x30), L"the CGA register writes");
    Assert::IsTrue(VideoHolds(rig.Ram(), 0x00, 0x00), L"all 16 KB cleared to zero");
    Assert::AreEqual(4u, BiosByte(rig, Machine::Firmware::VIDEO_MODE));
    Assert::AreEqual(40u, BiosWord(rig, Machine::Firmware::VIDEO_COLUMNS));
    Assert::AreEqual(16384u, BiosWord(rig, Machine::Firmware::VIDEO_PAGE_BYTES));
    Assert::AreEqual(0x2Au, BiosByte(rig, Machine::Firmware::MODE_CONTROL));
    Assert::AreEqual(0x30u, BiosByte(rig, Machine::Firmware::COLOR_SELECT));
    Assert::AreEqual(0u, BiosWord(rig, Machine::Firmware::CURSOR_POSITIONS));
    Assert::AreEqual(0x0030u, std::uint32_t{rig.Regs().ax}, L"AX as SET_MODE leaves it");
    Assert::IsFalse(rig.Services().Fault().has_value());
  }

  // SetTextMode (CS:7D31): mode 1 for the docked screens.
  TEST_METHOD(SetMode1ProgramsTheCgaAndBlanksTheText)
  {
    ServiceRig rig("BiosMode1");
    FillVideo(rig.Ram(), 0x55);
    rig.Ram().Write16(BIOS_DATA, Machine::Firmware::CURSOR_POSITIONS, 0x0A05);
    rig.Regs().ax = 0x0001;

    rig.Interrupt(0x10);

    Assert::IsTrue(rig.Bus().Writes() == ModeSetWrites(CRTC_40X25, 0x28, 0x30), L"the CGA register writes");
    Assert::IsTrue(VideoHolds(rig.Ram(), 0x20, 0x07), L"spaces, light grey on black");
    Assert::AreEqual(1u, BiosByte(rig, Machine::Firmware::VIDEO_MODE));
    Assert::AreEqual(40u, BiosWord(rig, Machine::Firmware::VIDEO_COLUMNS));
    Assert::AreEqual(2048u, BiosWord(rig, Machine::Firmware::VIDEO_PAGE_BYTES));
    Assert::AreEqual(0x28u, BiosByte(rig, Machine::Firmware::MODE_CONTROL));
    Assert::AreEqual(0u, BiosWord(rig, Machine::Firmware::CURSOR_POSITIONS), L"cursor home");
  }

  // SetGraphicsMode's next call: AH=0Bh BH=1 BL=0 picks palette 0.
  TEST_METHOD(PaletteAndBackgroundAreReadModifyWrite)
  {
    ServiceRig rig("BiosPalette");
    rig.Regs().ax = 0x0004;
    rig.Interrupt(0x10);
    rig.Bus().Clear();

    rig.Regs().ax = 0x0B00;
    rig.Regs().bx = 0x0100;
    rig.Interrupt(0x10);
    Assert::IsTrue(rig.Bus().Writes() == std::vector<PortWrite>{{0x3D9, 0x10}}, L"palette 0: bit 5 cleared");
    Assert::AreEqual(0x0410u, std::uint32_t{rig.Regs().ax}, L"AH = the mode, AL = the value written");

    rig.Bus().Clear();
    rig.Regs().ax = 0x0B00;
    rig.Regs().bx = 0x0009;
    rig.Interrupt(0x10);
    Assert::IsTrue(rig.Bus().Writes() == std::vector<PortWrite>{{0x3D9, 0x09}}, L"background 9");
    Assert::AreEqual(0x09u, BiosByte(rig, Machine::Firmware::COLOR_SELECT));

    rig.Bus().Clear();
    rig.Regs().ax = 0x0B00;
    rig.Regs().bx = 0x0200;
    rig.Interrupt(0x10);
    Assert::IsTrue(rig.Bus().Writes().empty());
    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnknownSubfunction));
  }

  TEST_METHOD(UnsupportedModeFaultsAndChangesNothing)
  {
    ServiceRig rig("BiosMode7");
    rig.Regs().ax = 0x0007;

    rig.Interrupt(0x10);

    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnsupportedRequest));
    Assert::IsTrue(rig.Bus().Writes().empty());
    Assert::AreEqual(3u, BiosByte(rig, Machine::Firmware::VIDEO_MODE));
    Assert::AreEqual(0x0007u, std::uint32_t{rig.Regs().ax});
  }

  TEST_METHOD(UnknownVideoFunctionFaultsWithWhereItCameFrom)
  {
    ServiceRig rig("BiosUnknown");
    rig.Regs().ax = 0x0E41; // teletype: not part of the reference's surface

    rig.Interrupt(0x10);

    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnknownFunction));
    const Machine::ServiceFault fault = rig.Services().Fault().value_or(Machine::ServiceFault{});
    Assert::AreEqual(0x10u, std::uint32_t{fault.vector});
    Assert::AreEqual(0x0Eu, std::uint32_t{fault.ah});
    Assert::AreEqual(0x41u, std::uint32_t{fault.al});
    Assert::AreEqual(std::uint32_t{ServiceRig::CODE_SEGMENT}, std::uint32_t{fault.cs});
    Assert::AreEqual(0u, std::uint32_t{fault.ip}, L"the INT itself");
    Assert::AreEqual(0x0E41u, std::uint32_t{rig.Regs().ax}, L"nothing serviced");
    Assert::AreEqual(2u, std::uint32_t{rig.Regs().ip}, L"execution continues after the INT");
    Assert::AreEqual(0u, BiosWord(rig, Machine::Firmware::CURSOR_POSITIONS));
  }

  // ExitToDos (CS:00B9) flushes the keyboard: AH=01h until ZF, AH=00h for each key.
  TEST_METHOD(KeyboardStatusSaysNoKeyAndReadingNoneFaults)
  {
    ServiceRig rig("BiosKeyboard");
    rig.Regs().ax = 0x0100;
    rig.Interrupt(0x16);
    Assert::IsTrue(rig.Zero(), L"no key");
    Assert::IsFalse(rig.Services().Fault().has_value());

    rig.Regs().ax = 0x0000;
    rig.Interrupt(0x16);
    Assert::IsTrue(rig.Faulted(Machine::FaultKind::WouldBlock));
    rig.Services().ClearFault();

    // A key in the buffer, as the BIOS's own int 9 would have left it.
    rig.Ram().Write16(BIOS_DATA, 0x1E, 0x1E61);
    rig.Ram().Write16(BIOS_DATA, Machine::Firmware::KEYBOARD_TAIL, 0x20);
    rig.Regs().ax = 0x0100;
    rig.Interrupt(0x16);
    Assert::IsFalse(rig.Zero());
    Assert::AreEqual(0x1E61u, std::uint32_t{rig.Regs().ax});
    rig.Regs().ax = 0x0000;
    rig.Interrupt(0x16);
    Assert::AreEqual(0x1E61u, std::uint32_t{rig.Regs().ax});
    Assert::AreEqual(0x20u, BiosWord(rig, Machine::Firmware::KEYBOARD_HEAD));
    Assert::IsFalse(rig.Services().Fault().has_value());
  }

  // RestoreTimerInterrupt (CS:016B) reads the count and sets it back a day when it is over one.
  TEST_METHOD(TimeOfDayReadsAndSetsTheTickCount)
  {
    ServiceRig rig("BiosTime");
    rig.Regs().ax = 0x0100;
    rig.Regs().cx = 0x0018;
    rig.Regs().dx = 0x00AF;
    rig.Interrupt(0x1A);
    Assert::AreEqual(0x00AFu, BiosWord(rig, Machine::Firmware::TIMER_TICKS));
    Assert::AreEqual(0x0018u, BiosWord(rig, Machine::Firmware::TIMER_TICKS + 2));

    rig.Ram().Write8(BIOS_DATA, Machine::Firmware::TIMER_ROLLOVER, 1);
    rig.Regs().ax = 0x0000;
    rig.Regs().cx = 0;
    rig.Regs().dx = 0;
    rig.Interrupt(0x1A);
    Assert::AreEqual(0x0018u, std::uint32_t{rig.Regs().cx});
    Assert::AreEqual(0x00AFu, std::uint32_t{rig.Regs().dx});
    Assert::AreEqual(0x0001u, std::uint32_t{rig.Regs().ax}, L"AL = the 24-hour flag, AH = 0");
    Assert::AreEqual(0u, BiosByte(rig, Machine::Firmware::TIMER_ROLLOVER), L"the flag is cleared by reading it");

    rig.Regs().ax = 0x0200;
    rig.Interrupt(0x1A);
    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnknownFunction));
  }
};

} // namespace MachineTests

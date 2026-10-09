#include "pch.h"

#include "ServiceRig.h"

#include "Firmware.h"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint16_t ROM = Machine::Firmware::ROM_SEGMENT;
constexpr std::uint16_t BIOS_DATA = Machine::Firmware::DATA_SEGMENT;

std::uint32_t VectorOffset(Machine::Memory& _memory, std::uint32_t _vector)
{
  return _memory.Read16(_vector * 4u);
}

std::uint32_t VectorSegment(Machine::Memory& _memory, std::uint32_t _vector)
{
  return _memory.Read16(_vector * 4u + 2u);
}

std::uint32_t Ticks(Machine::Memory& _memory)
{
  return _memory.Read16(BIOS_DATA, Machine::Firmware::TIMER_TICKS) |
         (std::uint32_t{_memory.Read16(BIOS_DATA, Machine::Firmware::TIMER_TICKS + 2)} << 16);
}

void SetTicks(Machine::Memory& _memory, std::uint32_t _ticks)
{
  _memory.Write16(BIOS_DATA, Machine::Firmware::TIMER_TICKS, static_cast<std::uint16_t>(_ticks & 0xFFFF));
  _memory.Write16(BIOS_DATA, Machine::Firmware::TIMER_TICKS + 2, static_cast<std::uint16_t>(_ticks >> 16));
}

// Calls the game's IsMouseDriverInstalled from CODE:0000 and returns ZF after it returns.
bool MouseCheckSetsZero(ServiceRig& _rig)
{
  _rig.Ram().Write8(ServiceRig::CODE_SEGMENT, 0, 0xE8); // call 0100h
  _rig.Ram().Write16(ServiceRig::CODE_SEGMENT, 1, 0x00FD);
  for (std::size_t index = 0; index < IS_MOUSE_DRIVER_INSTALLED.size(); ++index)
  {
    _rig.Ram().Write8(ServiceRig::CODE_SEGMENT, static_cast<std::uint16_t>(0x100 + index), IS_MOUSE_DRIVER_INSTALLED[index]);
  }
  _rig.Regs().ip = 0;
  Assert::IsTrue(_rig.RunUntil(ServiceRig::CODE_SEGMENT, 3), L"the routine returns");
  return _rig.Zero();
}

} // namespace

TEST_CLASS(FirmwareTests)
{
public:
  TEST_METHOD(EveryVectorPointsAtItsOwnIretInTheRom)
  {
    ServiceRig rig("FirmwareVectors");
    for (std::uint32_t vector = 0; vector < 256; ++vector)
    {
      Assert::AreEqual(std::uint32_t{ROM}, VectorSegment(rig.Ram(), vector));
      if (vector == 0x08 || vector == 0x09 || vector == 0x22)
      {
        continue;
      }
      Assert::AreEqual(Machine::Firmware::IRET_STUBS_OFFSET + vector, VectorOffset(rig.Ram(), vector));
      Assert::AreEqual(0xCFu, std::uint32_t{rig.Ram().Read8(ROM, static_cast<std::uint16_t>(VectorOffset(rig.Ram(), vector)))});
    }
    Assert::AreEqual(std::uint32_t{Machine::Firmware::TIMER_HANDLER_OFFSET}, VectorOffset(rig.Ram(), 0x08));
    Assert::AreEqual(std::uint32_t{Machine::Firmware::KEYBOARD_HANDLER_OFFSET}, VectorOffset(rig.Ram(), 0x09));
    Assert::AreEqual(std::uint32_t{Machine::Firmware::HALT_OFFSET}, VectorOffset(rig.Ram(), 0x22), L"terminate address");
    Assert::AreEqual(0xFAu, std::uint32_t{rig.Ram().Read8(ROM, Machine::Firmware::HALT_OFFSET)}, L"cli");
    Assert::AreEqual(0xF4u, std::uint32_t{rig.Ram().Read8(ROM, Machine::Firmware::HALT_OFFSET + 1)}, L"hlt");
  }

  TEST_METHOD(ModelByteIsAnIbmPcAndTheRomHasNoAmstradString)
  {
    ServiceRig rig("FirmwareModel");
    Assert::AreEqual(0xFFu, std::uint32_t{rig.Ram().Read8(0xF000, 0xFFFE)});
    // Start's check: repe cmpsb of "Amstrad" against FC00:0016.
    constexpr std::string_view AMSTRAD = "Amstrad";
    bool same = true;
    for (std::size_t index = 0; index < AMSTRAD.size(); ++index)
    {
      same = same && rig.Ram().Read8(0xFC00, static_cast<std::uint16_t>(0x16 + index)) == static_cast<std::uint8_t>(AMSTRAD[index]);
    }
    Assert::IsFalse(same, L"FC00:0016 must not hold \"Amstrad\"");
  }

  TEST_METHOD(BiosDataAreaDescribesTheMachine)
  {
    ServiceRig rig("FirmwareData");
    Machine::Memory& ram = rig.Ram();
    Assert::AreEqual(0x106Du, std::uint32_t{ram.Read16(BIOS_DATA, Machine::Firmware::EQUIPMENT)}, L"equipment");
    Assert::AreEqual(640u, std::uint32_t{ram.Read16(BIOS_DATA, Machine::Firmware::MEMORY_SIZE_KB)});
    Assert::AreEqual(0x1Eu, std::uint32_t{ram.Read16(BIOS_DATA, Machine::Firmware::KEYBOARD_HEAD)}, L"keyboard buffer empty");
    Assert::AreEqual(0x1Eu, std::uint32_t{ram.Read16(BIOS_DATA, Machine::Firmware::KEYBOARD_TAIL)}, L"keyboard buffer empty");
    Assert::AreEqual(3u, std::uint32_t{ram.Read8(BIOS_DATA, Machine::Firmware::VIDEO_MODE)});
    Assert::AreEqual(80u, std::uint32_t{ram.Read16(BIOS_DATA, Machine::Firmware::VIDEO_COLUMNS)});
    Assert::AreEqual(4096u, std::uint32_t{ram.Read16(BIOS_DATA, Machine::Firmware::VIDEO_PAGE_BYTES)});
    Assert::AreEqual(0u, std::uint32_t{ram.Read16(BIOS_DATA, Machine::Firmware::CURSOR_POSITIONS)});
    Assert::AreEqual(0x0607u, std::uint32_t{ram.Read16(BIOS_DATA, Machine::Firmware::CURSOR_SHAPE)});
    Assert::AreEqual(0x3D4u, std::uint32_t{ram.Read16(BIOS_DATA, Machine::Firmware::CRTC_ADDRESS)});
    // The default start moment is 12:00:00.00: 43,200 s at 1,193,181.67 / 65,536 ticks a second.
    Assert::AreEqual(0x000C0059u, Ticks(ram), L"ticks at noon: 786,521");
    Assert::AreEqual(Machine::Firmware::TimerTicksAt(4'320'000), Ticks(ram));
    Assert::AreEqual(Machine::Firmware::TICKS_PER_DAY - 1, Machine::Firmware::TimerTicksAt(8'639'999), L"never a whole day");
  }

  TEST_METHOD(TimerHandlerRunsOnTheCpuCountingATickAndSendingEoi)
  {
    ServiceRig rig("FirmwareTimer");
    SetTicks(rig.Ram(), 0x0000FFFF);
    rig.Regs().ax = 0x1234;
    rig.Regs().ds = 0x5678;

    rig.Interrupt(0x08);
    Assert::AreEqual(std::uint32_t{ROM}, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(std::uint32_t{Machine::Firmware::TIMER_HANDLER_OFFSET}, std::uint32_t{rig.Regs().ip});
    Assert::IsTrue(rig.RunUntil(ServiceRig::CODE_SEGMENT, 2), L"the handler returns to the interrupted code");

    Assert::AreEqual(0x00010000u, Ticks(rig.Ram()), L"the carry reaches the high word");
    Assert::AreEqual(0u, std::uint32_t{rig.Ram().Read8(BIOS_DATA, Machine::Firmware::TIMER_ROLLOVER)});
    Assert::IsTrue(rig.Bus().Writes() == std::vector<PortWrite>{{0x20, 0x20}}, L"one OUT: EOI to the 8259");
    Assert::AreEqual(0x1234u, std::uint32_t{rig.Regs().ax}, L"AX preserved");
    Assert::AreEqual(0x5678u, std::uint32_t{rig.Regs().ds}, L"DS preserved");
    Assert::AreEqual(std::uint32_t{ServiceRig::STACK_TOP}, std::uint32_t{rig.Regs().sp}, L"stack balanced");
    Assert::IsFalse(rig.Services().Fault().has_value());
  }

  TEST_METHOD(TimerHandlerStartsAgainAfterTwentyFourHours)
  {
    ServiceRig rig("FirmwareMidnight");
    SetTicks(rig.Ram(), Machine::Firmware::TICKS_PER_DAY - 1);

    rig.Interrupt(0x08);
    Assert::IsTrue(rig.RunUntil(ServiceRig::CODE_SEGMENT, 2));

    Assert::AreEqual(0u, Ticks(rig.Ram()));
    Assert::AreEqual(1u, std::uint32_t{rig.Ram().Read8(BIOS_DATA, Machine::Firmware::TIMER_ROLLOVER)}, L"TIMER_OFL");
  }

  TEST_METHOD(KeyboardHandlerAcknowledgesTheKeyAndDiscardsIt)
  {
    ServiceRig rig("FirmwareKeyboard");
    rig.Bus().SetInput(0x60, 0x1E);
    rig.Bus().SetInput(0x61, 0x4C);
    rig.Regs().ax = 0xBEEF;

    rig.Interrupt(0x09);
    Assert::IsTrue(rig.RunUntil(ServiceRig::CODE_SEGMENT, 2));

    Assert::IsTrue(rig.Bus().Reads() == std::vector<std::uint16_t>{0x60, 0x61}, L"scan code, then the control port");
    const std::vector<PortWrite> expected = {{0x61, 0xCC}, {0x61, 0x4C}, {0x20, 0x20}};
    Assert::IsTrue(rig.Bus().Writes() == expected, L"61h bit 7 high, then low, then EOI");
    Assert::AreEqual(0xBEEFu, std::uint32_t{rig.Regs().ax});
    Assert::AreEqual(0x1Eu, std::uint32_t{rig.Ram().Read16(BIOS_DATA, Machine::Firmware::KEYBOARD_TAIL)}, L"nothing buffered");
  }

  // The game's own test (CS:02D4), run on the CPU: ZF=1 means no driver.
  TEST_METHOD(GamesMouseCheckSeesNoDriverWithoutOne)
  {
    ServiceRig rig("FirmwareNoMouse", false);
    Assert::AreEqual(0x33u + Machine::Firmware::IRET_STUBS_OFFSET, VectorOffset(rig.Ram(), 0x33));
    Assert::IsTrue(MouseCheckSetsZero(rig));
  }

  TEST_METHOD(GamesMouseCheckSeesTheDriverWithOne)
  {
    ServiceRig rig("FirmwareMouse", true);
    Assert::AreEqual(std::uint32_t{Machine::Firmware::MOUSE_DRIVER_OFFSET}, VectorOffset(rig.Ram(), 0x33));
    Assert::IsTrue(rig.Ram().Read8(ROM, Machine::Firmware::MOUSE_DRIVER_OFFSET) != 0xCF, L"the driver entry is not an IRET");
    Assert::IsFalse(MouseCheckSetsZero(rig));
  }
};

} // namespace MachineTests

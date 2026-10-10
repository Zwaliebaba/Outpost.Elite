#include "pch.h"

#include "PcRig.h"
#include "ServiceRig.h"

#include "Firmware.h"
#include "Pc.h"
#include "PortRouter.h"

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

// What the game's own test, IsMouseDriverInstalled (CS:02D4), reads, and the ZF it leaves: set, for no driver, when the int
// 33h vector is zero or its first byte is CFh, an IRET.
bool MouseCheckSetsZero(const Machine::Memory& _memory)
{
  const std::uint16_t offset = _memory.Read16(0x33u * 4);
  const std::uint16_t segment = _memory.Read16(0x33u * 4 + 2);
  return (offset | segment) == 0 || _memory.Read8(segment, offset) == 0xCF;
}

// A machine with a stack at STACK:STACK_TOP, interrupts on and AX and DS given, from which a test makes an interrupt as the
// 8088 enters one (Pc::CallInterrupt): the ROM's handler, native on the Pc's Dispatcher (NativeFirmware.h), runs to its
// IRET, and every port access it makes is logged.
class HandlerRig
{
public:
  static constexpr std::uint16_t STACK_SEGMENT = 0x4000;
  static constexpr std::uint16_t STACK_TOP = 0x0100;

  HandlerRig(std::string_view _name, std::uint16_t _ax, std::uint16_t _ds)
    : m_rig(_name)
  {
    Machine::Registers& regs = m_rig.Host().Processor().Regs();
    regs.cs = ServiceRig::CODE_SEGMENT;
    regs.ip = 0;
    regs.ss = STACK_SEGMENT;
    regs.sp = STACK_TOP;
    regs.ax = _ax;
    regs.ds = _ds;
    regs.flags = static_cast<std::uint16_t>(Machine::FLAGS_FIXED_ONES | Machine::FLAG_INTERRUPT);
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return m_rig.Host();
  }

  void Interrupt(std::uint8_t _vector)
  {
    m_rig.Host().Ports().SetLog(&m_ports);
    m_rig.Host().CallInterrupt(_vector);
    m_rig.Host().Ports().SetLog(nullptr);
  }

  /// The ports read, in order.
  [[nodiscard]] std::vector<std::uint16_t> Reads() const
  {
    std::vector<std::uint16_t> reads;
    for (const Machine::PortRouter::Access& access : m_ports)
    {
      if (!access.write)
        reads.push_back(access.port);
    }
    return reads;
  }

  /// The byte writes, in order.
  [[nodiscard]] std::vector<PortWrite> Writes() const
  {
    std::vector<PortWrite> writes;
    for (const Machine::PortRouter::Access& access : m_ports)
    {
      if (access.write)
        writes.push_back(PortWrite{access.port, static_cast<std::uint8_t>(access.value)});
    }
    return writes;
  }

private:
  PcRig m_rig;
  std::vector<Machine::PortRouter::Access> m_ports;
};

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

  // The ROM's timer handler, native on the Dispatcher in the ROM code's place: a tick counted, its carry into the high
  // word, and EOI sent, with the registers and the stack as they were.
  TEST_METHOD(TimerHandlerCountsATickAndSendsEoi)
  {
    HandlerRig rig("FirmwareTimer", 0x1234, 0x5678);
    Machine::Pc& pc = rig.Host();
    SetTicks(pc.Ram(), 0x0000FFFF);
    Assert::AreEqual(std::uint32_t{ROM}, VectorSegment(pc.Ram(), 0x08));
    Assert::AreEqual(std::uint32_t{Machine::Firmware::TIMER_HANDLER_OFFSET}, VectorOffset(pc.Ram(), 0x08));

    rig.Interrupt(0x08);

    Assert::AreEqual(0x00010000u, Ticks(pc.Ram()), L"the carry reaches the high word");
    Assert::AreEqual(0u, std::uint32_t{pc.Ram().Read8(BIOS_DATA, Machine::Firmware::TIMER_ROLLOVER)});
    Assert::IsTrue(rig.Writes() == std::vector<PortWrite>{{0x20, 0x20}}, L"one OUT: EOI to the 8259");
    const Machine::Registers& regs = pc.Processor().Regs();
    Assert::AreEqual(0x1234u, std::uint32_t{regs.ax}, L"AX preserved");
    Assert::AreEqual(0x5678u, std::uint32_t{regs.ds}, L"DS preserved");
    Assert::AreEqual(std::uint32_t{HandlerRig::STACK_TOP}, std::uint32_t{regs.sp}, L"stack balanced");
    Assert::IsFalse(pc.Services().Fault().has_value());
  }

  TEST_METHOD(TimerHandlerStartsAgainAfterTwentyFourHours)
  {
    HandlerRig rig("FirmwareMidnight", 0, 0);
    SetTicks(rig.Host().Ram(), Machine::Firmware::TICKS_PER_DAY - 1);

    rig.Interrupt(0x08);

    Assert::AreEqual(0u, Ticks(rig.Host().Ram()));
    Assert::AreEqual(1u, std::uint32_t{rig.Host().Ram().Read8(BIOS_DATA, Machine::Firmware::TIMER_ROLLOVER)}, L"TIMER_OFL");
  }

  // The ROM's keyboard handler, native in the ROM code's place, with a key latched in the 8255: the scan code read, the key
  // acknowledged on port 61h, and EOI sent; the key is not buffered.
  TEST_METHOD(KeyboardHandlerAcknowledgesTheKeyAndDiscardsIt)
  {
    HandlerRig rig("FirmwareKeyboard", 0xBEEF, 0);
    Machine::Pc& pc = rig.Host();
    pc.KeyboardController().Inject(0x1E);
    while (!pc.KeyboardController().LatchFull())
      pc.Idle(Machine::NO_EVENT);
    Assert::AreEqual(0x1Eu, std::uint32_t{pc.Ports().In8(0x60)}, L"the key latched");
    Assert::AreEqual(0x4Cu, std::uint32_t{pc.Ports().In8(0x61)}, L"port 61h as the BIOS leaves it");

    rig.Interrupt(0x09);

    Assert::IsTrue(rig.Reads() == std::vector<std::uint16_t>{0x60, 0x61}, L"scan code, then the control port");
    const std::vector<PortWrite> expected = {{0x61, 0xCC}, {0x61, 0x4C}, {0x20, 0x20}};
    Assert::IsTrue(rig.Writes() == expected, L"61h bit 7 high, then low, then EOI");
    Assert::AreEqual(0xBEEFu, std::uint32_t{pc.Processor().Regs().ax});
    Assert::AreEqual(0x1Eu, std::uint32_t{pc.Ram().Read16(BIOS_DATA, Machine::Firmware::KEYBOARD_TAIL)}, L"nothing buffered");
  }

  // What the game's own test (CS:02D4) reads of the int 33h vector: ZF=1, no driver.
  TEST_METHOD(GamesMouseCheckSeesNoDriverWithoutOne)
  {
    ServiceRig rig("FirmwareNoMouse", false);
    Assert::AreEqual(0x33u + Machine::Firmware::IRET_STUBS_OFFSET, VectorOffset(rig.Ram(), 0x33));
    Assert::IsTrue(MouseCheckSetsZero(rig.Ram()));
  }

  TEST_METHOD(GamesMouseCheckSeesTheDriverWithOne)
  {
    ServiceRig rig("FirmwareMouse", true);
    Assert::AreEqual(std::uint32_t{Machine::Firmware::MOUSE_DRIVER_OFFSET}, VectorOffset(rig.Ram(), 0x33));
    Assert::IsTrue(rig.Ram().Read8(ROM, Machine::Firmware::MOUSE_DRIVER_OFFSET) != 0xCF, L"the driver entry is not an IRET");
    Assert::IsFalse(MouseCheckSetsZero(rig.Ram()));
  }
};

} // namespace MachineTests

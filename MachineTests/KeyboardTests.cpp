#include "pch.h"

#include "Keyboard.h"
#include "Pic.h"
#include "Pit.h"
#include "Speaker.h"
#include "Timing.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint16_t PORT_A = 0x60;
constexpr std::uint16_t PORT_B = 0x61;
constexpr std::uint16_t PORT_C = 0x62;
constexpr Machine::Cycles DELAY = Machine::Keyboard::DELIVERY_DELAY_CYCLES;

// The XT's PPI with the devices port 0x61 drives.
class KeyboardRig
{
public:
  KeyboardRig()
    : m_speaker(m_clock),
      m_timer(m_clock, m_pic, m_speaker),
      m_keyboard(m_clock, m_pic, m_timer, m_speaker)
  {
  }

  [[nodiscard]] Machine::Keyboard& Ppi() noexcept
  {
    return m_keyboard;
  }

  [[nodiscard]] Machine::Pic& Controller() noexcept
  {
    return m_pic;
  }

  [[nodiscard]] Machine::Pit& Timer() noexcept
  {
    return m_timer;
  }

  [[nodiscard]] Machine::Speaker& Sound() noexcept
  {
    return m_speaker;
  }

  void StepTo(Machine::Cycles _cycle)
  {
    m_clock = _cycle;
    m_keyboard.Advance();
  }

  [[nodiscard]] bool Irq1Requested() const noexcept
  {
    return (m_pic.RequestRegister() & 0x02u) != 0;
  }

  // What ReadScanCode (0x7463) does: read port 0x60, pulse port 0x61 bit 7, send EOI.
  [[nodiscard]] std::uint32_t ServiceKeyboard()
  {
    (void)m_pic.AcknowledgeInterrupt();
    const std::uint32_t code = m_keyboard.In8(PORT_A);
    const std::uint8_t portB = m_keyboard.In8(PORT_B);
    m_keyboard.Out8(PORT_B, static_cast<std::uint8_t>(portB | 0x80));
    m_keyboard.Out8(PORT_B, static_cast<std::uint8_t>(portB & 0x7F));
    m_pic.Out8(0x20, 0x20);
    return code;
  }

private:
  Machine::Cycles m_clock = 0;
  Machine::Pic m_pic;
  Machine::Speaker m_speaker;
  Machine::Pit m_timer;
  Machine::Keyboard m_keyboard;
};

} // namespace

TEST_CLASS(KeyboardTests)
{
public:
  TEST_METHOD(DelayIsOneMillisecond)
  {
    Assert::AreEqual(Machine::MicrosecondsToCycles(1'000), DELAY);
    Assert::AreEqual(Machine::Cycles{4772}, DELAY);
  }

  // A code injected with the latch empty arrives DELIVERY_DELAY_CYCLES later, in port 0x60, with IRQ1.
  TEST_METHOD(CodeArrivesAfterTheDelayWithIrq1)
  {
    KeyboardRig rig;
    rig.StepTo(100);
    rig.Ppi().Inject(0x1E); // A, make
    rig.StepTo(100 + DELAY - 1);
    Assert::IsFalse(rig.Irq1Requested());
    Assert::IsFalse(rig.Ppi().LatchFull());
    Assert::AreEqual(0x00u, std::uint32_t{rig.Ppi().In8(PORT_A)}, L"an empty latch reads 0");

    rig.StepTo(100 + DELAY);
    Assert::IsTrue(rig.Irq1Requested());
    Assert::IsTrue(rig.Ppi().LatchFull());
    Assert::AreEqual(0x1Eu, std::uint32_t{rig.Ppi().In8(PORT_A)});
    Assert::AreEqual(0x1Eu, std::uint32_t{rig.Ppi().In8(PORT_A)}, L"reading does not empty the latch");
  }

  // The latch holds one code until port 0x61 bit 7 is pulsed; the next code comes a delay after.
  TEST_METHOD(NextCodeWaitsForTheAcknowledgePulse)
  {
    KeyboardRig rig;
    rig.Ppi().Inject(0x1E);
    rig.Ppi().Inject(0x9E); // A, break
    rig.StepTo(DELAY);
    Assert::IsTrue(rig.Irq1Requested());

    rig.StepTo(50 * DELAY);
    Assert::AreEqual(std::size_t{1}, rig.Ppi().QueuedCodes(), L"nothing more while the latch is full");
    Assert::AreEqual(0x1Eu, rig.ServiceKeyboard());
    Assert::IsFalse(rig.Ppi().LatchFull(), L"the pulse empties the latch");
    Assert::IsFalse(rig.Irq1Requested());

    rig.StepTo(51 * DELAY - 1);
    Assert::IsFalse(rig.Irq1Requested());
    rig.StepTo(51 * DELAY);
    Assert::IsTrue(rig.Irq1Requested());
    Assert::AreEqual(0x9Eu, rig.ServiceKeyboard());
    Assert::AreEqual(std::size_t{0}, rig.Ppi().QueuedCodes());
  }

  // While bit 7 is held high the keyboard is held clear and delivers nothing.
  TEST_METHOD(HoldingBit7KeepsTheKeyboardQuiet)
  {
    KeyboardRig rig;
    rig.Ppi().Out8(PORT_B, 0xCC);
    rig.Ppi().Inject(0x1C);
    rig.StepTo(10 * DELAY);
    Assert::IsFalse(rig.Ppi().LatchFull());
    rig.Ppi().Out8(PORT_B, 0x4C);
    rig.StepTo(11 * DELAY - 1);
    Assert::IsFalse(rig.Ppi().LatchFull());
    rig.StepTo(11 * DELAY);
    Assert::IsTrue(rig.Ppi().LatchFull());
    Assert::AreEqual(0x1Cu, std::uint32_t{rig.Ppi().In8(PORT_A)});
  }

  // Port 0x61 reads back what was written; bit 0 gates channel 2 and bits 0-1 reach the speaker.
  TEST_METHOD(PortBReadsBackAndDrivesGateAndSpeaker)
  {
    KeyboardRig rig;
    Assert::AreEqual(0x4Cu, std::uint32_t{rig.Ppi().In8(PORT_B)}, L"power-on value");
    const std::size_t events = rig.Sound().Events().size();

    rig.StepTo(1000);
    rig.Ppi().Out8(PORT_B, 0x4F);
    Assert::AreEqual(0x4Fu, std::uint32_t{rig.Ppi().In8(PORT_B)});
    Assert::IsTrue(rig.Timer().ChannelState(2).gate);
    const Machine::Speaker::Event& on = rig.Sound().Events().back();
    Assert::AreEqual(Machine::Cycles{1000}, on.at);
    Assert::IsTrue(on.gate && on.data);
    Assert::AreEqual(events + 1, rig.Sound().Events().size(), L"the gate and the bits at one cycle are one event");

    rig.StepTo(2000);
    rig.Ppi().Out8(PORT_B, 0xCF);
    rig.Ppi().Out8(PORT_B, 0x4F);
    Assert::AreEqual(events + 1, rig.Sound().Events().size(), L"the acknowledge pulse leaves the speaker alone");

    rig.Ppi().Out8(PORT_B, 0x4D);
    Assert::IsTrue(rig.Timer().ChannelState(2).gate);
    Assert::IsFalse(rig.Sound().Events().back().data);
  }

  // Port 0x62 bit 5 is channel 2's output.
  TEST_METHOD(PortCReadsTimer2Output)
  {
    KeyboardRig rig;
    Assert::AreEqual(0x20u, std::uint32_t{rig.Ppi().In8(PORT_C)}, L"gate low: the output is high");
    rig.Timer().Out8(0x43, 0xB0); // channel 2, mode 0: output low until the count runs out
    Assert::AreEqual(0x00u, std::uint32_t{rig.Ppi().In8(PORT_C)});
  }
};

} // namespace MachineTests

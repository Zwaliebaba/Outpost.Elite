#include "pch.h"

#include "InterruptController.h"
#include "IntervalTimer.h"
#include "Keyboard.h"
#include "Speaker.h"
#include "Timing.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr Elite::Cycles DELAY = Elite::Keyboard::DELIVERY_DELAY_CYCLES;
constexpr std::uint8_t ACKNOWLEDGE = 0x80; // the system control port's bit 7

// The XT's keyboard and its 8255, with the devices the system control port drives.
class KeyboardRig
{
public:
  KeyboardRig()
    : m_speaker(m_clock),
      m_timer(m_clock, m_controller, m_speaker),
      m_keyboard(m_clock, m_controller, m_timer, m_speaker)
  {
  }

  [[nodiscard]] Elite::Keyboard& Device() noexcept
  {
    return m_keyboard;
  }

  [[nodiscard]] Elite::IntervalTimer& Timer() noexcept
  {
    return m_timer;
  }

  [[nodiscard]] Elite::Speaker& Sound() noexcept
  {
    return m_speaker;
  }

  void StepTo(Elite::Cycles _cycle)
  {
    m_clock = _cycle;
    m_keyboard.Advance();
  }

  [[nodiscard]] bool Irq1Requested() const noexcept
  {
    return (m_controller.RequestRegister() & 0x02u) != 0;
  }

  // What ReadScanCode (0x7443) does: read the scan code, pulse the system control port's bit 7, end the interrupt.
  [[nodiscard]] std::uint32_t ServiceKeyboard()
  {
    (void)m_controller.Acknowledge();
    const std::uint32_t code = m_keyboard.Data();
    const std::uint8_t control = m_keyboard.SystemControl();
    m_keyboard.SetSystemControl(static_cast<std::uint8_t>(control | ACKNOWLEDGE));
    m_keyboard.SetSystemControl(static_cast<std::uint8_t>(control & ~ACKNOWLEDGE));
    m_controller.EndOfInterrupt();
    return code;
  }

private:
  Elite::Cycles m_clock = 0;
  Elite::InterruptController m_controller;
  Elite::Speaker m_speaker;
  Elite::IntervalTimer m_timer;
  Elite::Keyboard m_keyboard;
};

} // namespace

// The keyboard and the system control port (Keyboard, D33): the emulated PC's 8255 without its ports.
TEST_CLASS(KeyboardTests)
{
public:
  TEST_METHOD(DelayIsOneMillisecond)
  {
    Assert::AreEqual(Elite::MicrosecondsToCycles(1'000), DELAY);
    Assert::AreEqual(Elite::Cycles{4772}, DELAY);
  }

  // A code injected with the latch empty arrives DELIVERY_DELAY_CYCLES later, in the data port, with IRQ 1.
  TEST_METHOD(CodeArrivesAfterTheDelayWithIrq1)
  {
    KeyboardRig rig;
    rig.StepTo(100);
    rig.Device().Inject(0x1E); // A, make
    Assert::AreEqual(100 + DELAY, rig.Device().NextDeliveryAt(), L"due a delay on");
    rig.StepTo(100 + DELAY - 1);
    Assert::IsFalse(rig.Irq1Requested());
    Assert::IsFalse(rig.Device().LatchFull());
    Assert::AreEqual(0x00u, std::uint32_t{rig.Device().Data()}, L"an empty latch reads 0");

    rig.StepTo(100 + DELAY);
    Assert::IsTrue(rig.Irq1Requested());
    Assert::IsTrue(rig.Device().LatchFull());
    Assert::AreEqual(0x1Eu, std::uint32_t{rig.Device().Data()});
    Assert::AreEqual(0x1Eu, std::uint32_t{rig.Device().Data()}, L"reading does not empty the latch");
    Assert::AreEqual(Elite::NO_EVENT, rig.Device().NextDeliveryAt(), L"nothing more is due");
  }

  // The latch holds one code until the system control port's bit 7 is pulsed; the next code comes a delay after.
  TEST_METHOD(NextCodeWaitsForTheAcknowledgePulse)
  {
    KeyboardRig rig;
    rig.Device().Inject(0x1E);
    rig.Device().Inject(0x9E); // A, break
    rig.StepTo(DELAY);
    Assert::IsTrue(rig.Irq1Requested());

    rig.StepTo(50 * DELAY);
    Assert::AreEqual(std::size_t{1}, rig.Device().QueuedCodes(), L"nothing more while the latch is full");
    Assert::AreEqual(0x1Eu, rig.ServiceKeyboard());
    Assert::IsFalse(rig.Device().LatchFull(), L"the pulse empties the latch");
    Assert::IsFalse(rig.Irq1Requested());

    rig.StepTo(51 * DELAY - 1);
    Assert::IsFalse(rig.Irq1Requested());
    rig.StepTo(51 * DELAY);
    Assert::IsTrue(rig.Irq1Requested());
    Assert::AreEqual(0x9Eu, rig.ServiceKeyboard());
    Assert::AreEqual(std::size_t{0}, rig.Device().QueuedCodes());
  }

  // While bit 7 is held high the keyboard is held clear and delivers nothing.
  TEST_METHOD(HoldingBit7KeepsTheKeyboardQuiet)
  {
    KeyboardRig rig;
    rig.Device().SetSystemControl(0xCC);
    rig.Device().Inject(0x1C);
    rig.StepTo(10 * DELAY);
    Assert::IsFalse(rig.Device().LatchFull());
    rig.Device().SetSystemControl(0x4C);
    rig.StepTo(11 * DELAY - 1);
    Assert::IsFalse(rig.Device().LatchFull());
    rig.StepTo(11 * DELAY);
    Assert::IsTrue(rig.Device().LatchFull());
    Assert::AreEqual(0x1Cu, std::uint32_t{rig.Device().Data()});
  }

  // The system control port reads back what was written; bit 0 gates channel 2 and bits 0-1 reach the speaker.
  TEST_METHOD(SystemControlReadsBackAndDrivesGateAndSpeaker)
  {
    KeyboardRig rig;
    Assert::AreEqual(0x4Cu, std::uint32_t{rig.Device().SystemControl()}, L"power-on value");
    const std::size_t events = rig.Sound().Events().size();

    rig.StepTo(1000);
    rig.Device().SetSystemControl(0x4F);
    Assert::AreEqual(0x4Fu, std::uint32_t{rig.Device().SystemControl()});
    Assert::IsTrue(rig.Timer().ChannelState(2).gate);
    const Elite::Speaker::Event& on = rig.Sound().Events().back();
    Assert::AreEqual(Elite::Cycles{1000}, on.at);
    Assert::IsTrue(on.gate && on.data);
    Assert::AreEqual(events + 1, rig.Sound().Events().size(), L"the gate and the bits at one cycle are one event");

    rig.StepTo(2000);
    rig.Device().SetSystemControl(0xCF);
    rig.Device().SetSystemControl(0x4F);
    Assert::AreEqual(events + 1, rig.Sound().Events().size(), L"the acknowledge pulse leaves the speaker alone");

    rig.Device().SetSystemControl(0x4D);
    Assert::IsTrue(rig.Timer().ChannelState(2).gate);
    Assert::IsFalse(rig.Sound().Events().back().data);
  }
};

} // namespace GameLogicTests

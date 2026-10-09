#include "pch.h"

#include "PortBus.h"
#include "PortRouter.h"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

// Records what reaches it, answers reads with the low byte of the port, and decodes 16-bit reads
// itself so that the router's choice between one 16-bit access and two 8-bit ones is visible.
class RecordingDevice final : public Machine::PortBus
{
public:
  [[nodiscard]] std::uint8_t In8(std::uint16_t _port) override
  {
    m_reads.push_back(_port);
    return static_cast<std::uint8_t>(_port & 0xFF);
  }

  void Out8(std::uint16_t _port, std::uint8_t _value) override
  {
    m_writes.push_back((static_cast<std::uint32_t>(_port) << 8) | _value);
  }

  [[nodiscard]] std::uint16_t In16(std::uint16_t _port) override
  {
    ++m_wideReads;
    return static_cast<std::uint16_t>(_port ^ 0xFFFF);
  }

  [[nodiscard]] const std::vector<std::uint16_t>& Reads() const noexcept
  {
    return m_reads;
  }

  [[nodiscard]] const std::vector<std::uint32_t>& Writes() const noexcept
  {
    return m_writes;
  }

  [[nodiscard]] std::size_t WideReads() const noexcept
  {
    return m_wideReads;
  }

private:
  std::vector<std::uint16_t> m_reads;
  std::vector<std::uint32_t> m_writes;
  std::size_t m_wideReads = 0;
};

} // namespace

TEST_CLASS(PortRouterTests)
{
public:
  TEST_METHOD(MappedPortsReachTheirDevice)
  {
    Machine::PortRouter router;
    RecordingDevice timer;
    RecordingDevice video;
    Assert::IsTrue(router.Map(0x40, 0x43, timer));
    Assert::IsTrue(router.Map(0x3D0, 0x3DF, video));

    Assert::AreEqual(0x42u, std::uint32_t{router.In8(0x42)});
    router.Out8(0x3D9, 0x30);
    Assert::AreEqual(std::size_t{1}, timer.Reads().size());
    Assert::AreEqual(0x3D930u, video.Writes().at(0));
    Assert::IsTrue(router.Unmapped().empty());
  }

  // An unmapped read is the floating bus, 0xFF; an unmapped write goes nowhere; both are counted.
  TEST_METHOD(UnmappedPortsAreCountedPerPort)
  {
    Machine::PortRouter router;
    RecordingDevice device;
    Assert::IsTrue(router.Map(0x60, 0x63, device));

    Assert::AreEqual(0xFFu, std::uint32_t{router.In8(0x201)});
    Assert::AreEqual(0xFFu, std::uint32_t{router.In8(0x201)});
    router.Out8(0x201, 0x00);
    router.Out8(0x378, 0x55);
    Assert::AreEqual(0xFFFFu, std::uint32_t{router.In16(0x64)}, L"two unmapped bytes");

    const auto& unmapped = router.Unmapped();
    Assert::AreEqual(std::size_t{4}, unmapped.size());
    Assert::AreEqual(std::uint64_t{2}, unmapped.at(0x201).reads);
    Assert::AreEqual(std::uint64_t{1}, unmapped.at(0x201).writes);
    Assert::AreEqual(std::uint64_t{0}, unmapped.at(0x378).reads);
    Assert::AreEqual(std::uint64_t{1}, unmapped.at(0x378).writes);
    Assert::AreEqual(std::uint64_t{1}, unmapped.at(0x64).reads);
    Assert::AreEqual(std::uint64_t{1}, unmapped.at(0x65).reads);
    Assert::IsTrue(device.Reads().empty());

    router.ClearUnmapped();
    Assert::IsTrue(router.Unmapped().empty());
  }

  TEST_METHOD(OverlappingOrEmptyRangesAreRefused)
  {
    Machine::PortRouter router;
    RecordingDevice first;
    RecordingDevice second;
    Assert::IsTrue(router.Map(0x20, 0x21, first));
    Assert::IsFalse(router.Map(0x21, 0x22, second));
    Assert::IsFalse(router.Map(0x30, 0x2F, second));
    Assert::IsTrue(router.Map(0x22, 0x22, second));
    Assert::AreEqual(0x22u, std::uint32_t{router.In8(0x22)});
    Assert::AreEqual(std::size_t{1}, second.Reads().size(), L"the refused map changed nothing");
  }

  // A 16-bit access within one device is the device's to decode; across two it is split in bytes.
  TEST_METHOD(WideAccessGoesWholeToOneDeviceOrSplits)
  {
    Machine::PortRouter router;
    RecordingDevice crtc;
    RecordingDevice next;
    Assert::IsTrue(router.Map(0x3D4, 0x3D5, crtc));
    Assert::IsTrue(router.Map(0x3D6, 0x3D7, next));

    Assert::AreEqual(static_cast<std::uint32_t>(0x3D4 ^ 0xFFFF), std::uint32_t{router.In16(0x3D4)});
    Assert::AreEqual(std::size_t{1}, crtc.WideReads());

    Assert::AreEqual(0xD6D5u, std::uint32_t{router.In16(0x3D5)});
    Assert::AreEqual(std::size_t{1}, crtc.Reads().size());
    Assert::AreEqual(std::size_t{1}, next.Reads().size());

    router.Out16(0x3D4, 0x2A0E); // CRTC index 0E, data 2A: two bytes through the default Out16
    Assert::AreEqual(0x3D40Eu, crtc.Writes().at(0));
    Assert::AreEqual(0x3D52Au, crtc.Writes().at(1));
  }
};

} // namespace MachineTests

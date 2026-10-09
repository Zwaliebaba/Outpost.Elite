#include "pch.h"

#include "ServiceRig.h"

#include "Dos.h"
#include "Firmware.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint16_t BIOS_DATA = Machine::Firmware::DATA_SEGMENT;
constexpr std::uint16_t VIDEO = 0xB800;
constexpr std::uint16_t NAME = 0x0100;
constexpr std::uint16_t BUFFER = 0x0200;
// Where the game keeps its DTA and its commander-file pattern (PerformDiskRequest, ListCommanderFiles).
constexpr std::uint16_t GAME_DTA = 0x206F;
constexpr std::uint16_t GAME_PATTERN = 0x20EF;

std::uint32_t Ax(ServiceRig& _rig)
{
  return _rig.Regs().ax;
}

// Creates a file with int 21h AH=3Ch and returns the handle (asserting success).
std::uint16_t CreateFile(ServiceRig& _rig, std::string_view _name, std::uint16_t _attribute = 0)
{
  _rig.PutString(NAME, _name);
  _rig.Regs().cx = _attribute;
  _rig.Dos(0x3C);
  Assert::IsFalse(_rig.Carry(), L"create");
  return _rig.Regs().ax;
}

void WriteBytes(ServiceRig& _rig, std::uint16_t _handle, std::string_view _bytes)
{
  _rig.PutString(BUFFER, _bytes, false);
  _rig.Regs().bx = _handle;
  _rig.Regs().cx = static_cast<std::uint16_t>(_bytes.size());
  _rig.Dos(0x40);
  Assert::IsFalse(_rig.Carry(), L"write");
}

void CloseFile(ServiceRig& _rig, std::uint16_t _handle)
{
  _rig.Regs().bx = _handle;
  _rig.Dos(0x3E);
  Assert::IsFalse(_rig.Carry(), L"close");
}

std::string ReadText(ServiceRig& _rig, std::uint16_t _segment, std::uint16_t _offset, std::size_t _bytes)
{
  std::string text;
  for (std::size_t index = 0; index < _bytes; ++index)
  {
    text.push_back(static_cast<char>(_rig.Ram().Read8(_segment, static_cast<std::uint16_t>(_offset + index))));
  }
  return text;
}

// The name a find left in the DTA at DATA:GAME_DTA.
std::string FoundName(ServiceRig& _rig)
{
  std::string name;
  for (std::uint16_t offset = 0x1E; offset < 0x1E + 13; ++offset)
  {
    const std::uint8_t character = _rig.Ram().Read8(ServiceRig::DATA_SEGMENT, static_cast<std::uint16_t>(GAME_DTA + offset));
    if (character == 0)
    {
      break;
    }
    name.push_back(static_cast<char>(character));
  }
  return name;
}

void FindFirst(ServiceRig& _rig, std::string_view _pattern, std::uint16_t _attribute)
{
  _rig.PutString(GAME_DTA, "", false);
  _rig.Dos(0x1A);
  _rig.PutString(GAME_PATTERN, _pattern);
  _rig.Regs().cx = _attribute;
  _rig.Dos(0x4E);
}

} // namespace

TEST_CLASS(DosTests)
{
public:
  // Start (CS:000E) refuses to run when AL is 0.
  TEST_METHOD(VersionIs330)
  {
    ServiceRig rig("DosVersion");
    rig.Dos(0x30);
    Assert::AreEqual(0x1E03u, Ax(rig), L"AL = 3, AH = 30");
    Assert::AreEqual(0u, std::uint32_t{rig.Regs().bx});
    Assert::AreEqual(0u, std::uint32_t{rig.Regs().cx});
  }

  // ExitToDos (CS:00AD) prints the exit message in mode 2 this way.
  TEST_METHOD(PrintStringWritesAtTheCursorThroughTheBios)
  {
    ServiceRig rig("DosPrint");
    rig.PutString(NAME, "AB\r\nC$", false);
    rig.Dos(0x09);

    Assert::AreEqual(std::uint32_t{'A'}, std::uint32_t{rig.Ram().Read8(VIDEO, 0)});
    Assert::AreEqual(0x07u, std::uint32_t{rig.Ram().Read8(VIDEO, 1)}, L"the attribute is kept");
    Assert::AreEqual(std::uint32_t{'B'}, std::uint32_t{rig.Ram().Read8(VIDEO, 2)});
    Assert::AreEqual(std::uint32_t{'C'}, std::uint32_t{rig.Ram().Read8(VIDEO, 160)}, L"row 1, column 0");
    Assert::AreEqual(0x0101u, std::uint32_t{rig.Ram().Read16(BIOS_DATA, Machine::Firmware::CURSOR_POSITIONS)});
    Assert::AreEqual(0x0924u, Ax(rig), L"AL = '$'");
    const std::vector<PortWrite>& writes = rig.Bus().Writes();
    Assert::IsTrue(writes.size() >= 4);
    const std::vector<PortWrite> cursor(writes.end() - 4, writes.end());
    Assert::IsTrue(cursor == std::vector<PortWrite>{{0x3D4, 0x0E}, {0x3D5, 0x00}, {0x3D4, 0x0F}, {0x3D5, 0x51}}, L"CRTC cursor at 81");
    Assert::IsFalse(rig.Services().Fault().has_value());
  }

  TEST_METHOD(PrintStringScrollsAtTheBottomAndExpandsTabs)
  {
    ServiceRig rig("DosScroll");
    rig.Ram().Write8(VIDEO, 0, 'Q');
    rig.Ram().Write8(VIDEO, 160, 'R');
    rig.Ram().Write8(VIDEO, 24 * 160 + 1, 0x1F);
    rig.Ram().Write16(BIOS_DATA, Machine::Firmware::CURSOR_POSITIONS, 0x1800);
    rig.PutString(NAME, "Z\n\tX$", false);
    rig.Dos(0x09);

    Assert::AreEqual(std::uint32_t{'R'}, std::uint32_t{rig.Ram().Read8(VIDEO, 0)}, L"row 1 moved to row 0");
    Assert::AreEqual(std::uint32_t{'Z'}, std::uint32_t{rig.Ram().Read8(VIDEO, 23 * 160)}, L"row 24 moved to row 23");
    Assert::AreEqual(0x1Fu, std::uint32_t{rig.Ram().Read8(VIDEO, 23 * 160 + 1)});
    Assert::AreEqual(std::uint32_t{' '}, std::uint32_t{rig.Ram().Read8(VIDEO, 24 * 160)}, L"the new row is blank");
    Assert::AreEqual(0x07u, std::uint32_t{rig.Ram().Read8(VIDEO, 24 * 160 + 1)}, L"with the attribute under the cursor");
    // After the line feed the cursor is at row 24, column 1; the tab runs to column 8.
    Assert::AreEqual(std::uint32_t{'X'}, std::uint32_t{rig.Ram().Read8(VIDEO, 24 * 160 + 16)});
    Assert::AreEqual(0x1809u, std::uint32_t{rig.Ram().Read16(BIOS_DATA, Machine::Firmware::CURSOR_POSITIONS)});
  }

  TEST_METHOD(PrintStringInAGraphicsModeFaultsAndPrintsNothing)
  {
    ServiceRig rig("DosPrintGraphics");
    rig.Regs().ax = 0x0004;
    rig.Interrupt(0x10);
    rig.PutString(NAME, "Hi$", false);
    rig.Dos(0x09);

    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnsupportedRequest));
    Assert::AreEqual(0u, std::uint32_t{rig.Ram().Read8(VIDEO, 0)});
    Assert::AreEqual(0x0900u, Ax(rig));
  }

  // The copy-protection prompt's line input (CS:051A), which ADR-001's D5 patch skips: a fault, not a wait.
  TEST_METHOD(BufferedInputFaults)
  {
    ServiceRig rig("DosBufferedInput");
    rig.PutString(NAME, "\x10", false);
    rig.Dos(0x0C, 0x0A);
    Assert::IsTrue(rig.Faulted(Machine::FaultKind::WouldBlock));
  }

  TEST_METHOD(TimeIsTheStartMomentPlusMachineTime)
  {
    ServiceRig rig("DosTime");
    rig.Clock() = 431'931'764; // 90.5 s at 14.31818 MHz / 3
    rig.Dos(0x2C);
    Assert::AreEqual(0x0C01u, std::uint32_t{rig.Regs().cx}, L"12:01");
    Assert::AreEqual(0x1E32u, std::uint32_t{rig.Regs().dx}, L"30.50 s");

    Machine::Dos::DateTime start;
    start.year = 1987;
    start.month = 12;
    start.day = 31;
    start.hour = 23;
    start.minute = 59;
    start.second = 59;
    start.hundredths = 99;
    ServiceRig late("DosNewYear", false, start);
    late.Clock() = 95'456; // two hundredths
    const Machine::Dos::DateTime now = late.Services().DosKernel().Now();
    Assert::AreEqual(1988u, std::uint32_t{now.year});
    Assert::AreEqual(1u, std::uint32_t{now.month});
    Assert::AreEqual(1u, std::uint32_t{now.day});
    Assert::AreEqual(0u, std::uint32_t{now.hour});
    Assert::AreEqual(0u, std::uint32_t{now.minute});
    Assert::AreEqual(0u, std::uint32_t{now.second});
    Assert::AreEqual(1u, std::uint32_t{now.hundredths});
    const Machine::FileStamp stamp = Machine::Dos::Stamp(now);
    Assert::AreEqual(std::uint32_t{(8u << 9) | (1u << 5) | 1u}, std::uint32_t{stamp.date});
    Assert::AreEqual(0u, std::uint32_t{stamp.time});
  }

  // SaveCommanderFile then LoadCommanderFile, call for call.
  TEST_METHOD(CreateWriteCloseOpenReadClose)
  {
    ServiceRig rig("DosFiles");
    const std::uint16_t handle = CreateFile(rig, "jameson.cdr");
    Assert::AreEqual(5u, std::uint32_t{handle}, L"handles start at 5");
    Assert::IsTrue(rig.Files().Exists("JAMESON.CDR"), L"the host file is upper case");
    WriteBytes(rig, handle, "ABCD");
    Assert::AreEqual(4u, Ax(rig));
    CloseFile(rig, handle);
    Assert::IsTrue(rig.Files().ReadFile("JAMESON.CDR") == "ABCD");

    rig.PutString(NAME, "JAMESON.CDR");
    rig.Dos(0x3D, 0x00);
    Assert::IsFalse(rig.Carry());
    Assert::AreEqual(5u, Ax(rig));
    rig.Regs().bx = 5;
    rig.Regs().cx = 10;
    rig.Regs().dx = BUFFER;
    rig.Dos(0x3F);
    Assert::IsFalse(rig.Carry());
    Assert::AreEqual(4u, Ax(rig), L"only four bytes");
    Assert::IsTrue(ReadText(rig, ServiceRig::DATA_SEGMENT, BUFFER, 4) == "ABCD");
    rig.Regs().cx = 10;
    rig.Dos(0x3F);
    Assert::AreEqual(0u, Ax(rig), L"end of file");
    CloseFile(rig, 5);

    rig.Regs().bx = 5;
    rig.Dos(0x3E);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(6u, Ax(rig), L"invalid handle");
    Assert::IsFalse(rig.Services().Fault().has_value());
  }

  TEST_METHOD(OpenReportsDosErrors)
  {
    ServiceRig rig("DosOpenErrors");
    rig.PutString(NAME, "MISSING.CDR");
    rig.Dos(0x3D, 0x00);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(2u, Ax(rig), L"file not found");

    CloseFile(rig, CreateFile(rig, "LOCKED.CDR"));
    rig.PutString(NAME, "LOCKED.CDR");
    rig.Dos(0x3D, 0x03);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(0x0Cu, Ax(rig), L"invalid access code");

    rig.Regs().cx = Machine::FileStore::ATTRIBUTE_READ_ONLY;
    rig.Dos(0x43, 0x01);
    Assert::IsFalse(rig.Carry());
    rig.Dos(0x3D, 0x01);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(5u, Ax(rig), L"access denied: read-only");
    rig.Dos(0x3D, 0x00);
    Assert::IsFalse(rig.Carry(), L"reading a read-only file is fine");
  }

  TEST_METHOD(NamesWithPathsOrDrivesAreRefused)
  {
    ServiceRig rig("DosPaths");
    for (const std::string_view name : {"..\\X.CDR", "C:X.CDR", "SUB/X.CDR", "..", "\\X.CDR"})
    {
      rig.PutString(NAME, name);
      rig.Regs().cx = 0;
      rig.Dos(0x3C);
      Assert::IsTrue(rig.Carry());
      Assert::AreEqual(3u, Ax(rig), L"path not found");
    }
    rig.PutString(NAME, "TOOLONGNAME.CDR");
    rig.Dos(0x3C);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(2u, Ax(rig), L"a long name is refused, not truncated");

    std::error_code error;
    Assert::IsFalse(std::filesystem::exists(rig.Files().Path().parent_path() / "X.CDR", error));
    Assert::IsTrue(std::filesystem::is_empty(rig.Files().Path(), error), L"nothing was created");
  }

  // DeleteCommanderFile (CS:03F2).
  TEST_METHOD(DeleteRemovesTheFile)
  {
    ServiceRig rig("DosDelete");
    CloseFile(rig, CreateFile(rig, "GONE.CDR"));
    rig.PutString(NAME, "gone.cdr");
    rig.Dos(0x41);
    Assert::IsFalse(rig.Carry());
    Assert::IsFalse(rig.Files().Exists("GONE.CDR"));
    rig.Dos(0x41);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(2u, Ax(rig));
  }

  // SaveCommanderFile clears the attributes after writing (CS:0382); LoadCommanderFile refuses a file with any set.
  TEST_METHOD(AttributesFollowTheGamesSaveAndLoad)
  {
    ServiceRig rig("DosAttributes");
    const std::uint16_t handle = CreateFile(rig, "SAVE.CDR");
    WriteBytes(rig, handle, "x");
    CloseFile(rig, handle);

    rig.PutString(NAME, "SAVE.CDR");
    rig.Dos(0x43, 0x00);
    Assert::IsFalse(rig.Carry());
    Assert::AreEqual(0x20u, std::uint32_t{rig.Regs().cx}, L"archive, as DOS sets it on a new file");
    rig.Regs().cx = 0;
    rig.Dos(0x43, 0x01);
    Assert::IsFalse(rig.Carry());
    rig.Dos(0x43, 0x00);
    Assert::AreEqual(0u, std::uint32_t{rig.Regs().cx});
    Assert::AreEqual(0u, Ax(rig), L"AX = CX");

    rig.Files().WriteFile("OLD.CDR", "y");
    rig.PutString(NAME, "OLD.CDR");
    rig.Dos(0x43, 0x00);
    Assert::AreEqual(0u, std::uint32_t{rig.Regs().cx}, L"a file the store never touched reads as attribute 0");

    rig.Regs().cx = Machine::FileStore::ATTRIBUTE_DIRECTORY;
    rig.Dos(0x43, 0x01);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(5u, Ax(rig));

    rig.Dos(0x43, 0x02);
    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnknownSubfunction));
  }

  TEST_METHOD(FifteenHandlesAndNoMore)
  {
    ServiceRig rig("DosHandles");
    CloseFile(rig, CreateFile(rig, "A.CDR"));
    rig.PutString(NAME, "A.CDR");
    for (std::uint32_t handle = 5; handle < 20; ++handle)
    {
      rig.Dos(0x3D, 0x00);
      Assert::IsFalse(rig.Carry());
      Assert::AreEqual(handle, Ax(rig));
    }
    rig.Dos(0x3D, 0x00);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(4u, Ax(rig), L"too many open files");
  }

  // ListCommanderFiles (CS:03A8): the game's DTA, its pattern, and the CX it happens to leave.
  TEST_METHOD(FindFirstAndNextListTheCommanderFiles)
  {
    ServiceRig rig("DosFind");
    rig.Files().WriteFile("JAMESON.CDR", "abc");
    rig.Files().WriteFile("zed.cdr", "12345");
    rig.Files().WriteFile("ELITE01.HI", "x");
    rig.Files().WriteFile("TOOLONGNAME.CDR", "x");
    std::error_code error;
    std::filesystem::create_directory(rig.Files().Path() / "SUB.CDR", error);

    FindFirst(rig, "*.cdr", 0xFFE7);
    Assert::IsFalse(rig.Carry());
    Assert::AreEqual(0u, Ax(rig));
    Assert::IsTrue(FoundName(rig) == "JAMESON.CDR");
    Machine::Memory& ram = rig.Ram();
    constexpr std::uint16_t DATA = ServiceRig::DATA_SEGMENT;
    Assert::AreEqual(3u, std::uint32_t{ram.Read8(DATA, GAME_DTA)}, L"drive C:");
    Assert::IsTrue(ReadText(rig, DATA, GAME_DTA + 1, 11) == "????????CDR", L"the search template");
    Assert::AreEqual(0xE7u, std::uint32_t{ram.Read8(DATA, GAME_DTA + 0x0C)}, L"the search attribute");
    Assert::AreEqual(0u, std::uint32_t{ram.Read8(DATA, GAME_DTA + 0x15)}, L"attribute: the game lists only 0");
    Assert::AreEqual(0u, std::uint32_t{ram.Read16(DATA, GAME_DTA + 0x16)}, L"time");
    Assert::AreEqual(0x0021u, std::uint32_t{ram.Read16(DATA, GAME_DTA + 0x18)}, L"date: 1980-01-01");
    Assert::AreEqual(3u, std::uint32_t{ram.Read16(DATA, GAME_DTA + 0x1A)}, L"size");
    Assert::AreEqual(0u, std::uint32_t{ram.Read16(DATA, GAME_DTA + 0x1C)});

    rig.Dos(0x4F);
    Assert::IsFalse(rig.Carry());
    Assert::IsTrue(FoundName(rig) == "ZED.CDR", L"upper case, whatever the host spelling");
    Assert::AreEqual(5u, std::uint32_t{ram.Read16(DATA, GAME_DTA + 0x1A)});

    rig.Dos(0x4F);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(0x12u, Ax(rig), L"no more files");

    FindFirst(rig, "*.XYZ", 0);
    Assert::IsTrue(rig.Carry());
    Assert::AreEqual(0x12u, Ax(rig), L"no match is 'no more files', which the game takes as success");

    FindFirst(rig, "ELITE??.*", 0);
    Assert::IsTrue(FoundName(rig) == "ELITE01.HI");

    FindFirst(rig, "..\\*.CDR", 0);
    Assert::AreEqual(3u, Ax(rig));
  }

  TEST_METHOD(FindSkipsHiddenFilesUnlessAskedFor)
  {
    ServiceRig rig("DosFindHidden");
    rig.Files().WriteFile("A.CDR", "a");
    rig.Files().WriteFile("B.CDR", "b");
    rig.PutString(NAME, "A.CDR");
    rig.Regs().cx = Machine::FileStore::ATTRIBUTE_HIDDEN;
    rig.Dos(0x43, 0x01);

    FindFirst(rig, "*.CDR", 0);
    Assert::IsTrue(FoundName(rig) == "B.CDR");
    rig.Dos(0x4F);
    Assert::IsTrue(rig.Carry());

    FindFirst(rig, "*.CDR", Machine::FileStore::ATTRIBUTE_HIDDEN);
    Assert::IsTrue(FoundName(rig) == "A.CDR");
    Assert::AreEqual(2u, std::uint32_t{rig.Ram().Read8(ServiceRig::DATA_SEGMENT, GAME_DTA + 0x15)});
  }

  TEST_METHOD(UnknownFunctionFaultsAndKeepsTheFirst)
  {
    ServiceRig rig("DosUnknown");
    const std::uint16_t flags = rig.Regs().flags;
    rig.Dos(0x2A, 0x07);

    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnknownFunction));
    const Machine::ServiceFault fault = rig.Services().Fault().value_or(Machine::ServiceFault{});
    Assert::AreEqual(0x21u, std::uint32_t{fault.vector});
    Assert::AreEqual(0x2Au, std::uint32_t{fault.ah});
    Assert::AreEqual(0x07u, std::uint32_t{fault.al});
    Assert::AreEqual(std::uint32_t{ServiceRig::CODE_SEGMENT}, std::uint32_t{fault.cs});
    Assert::AreEqual(0u, std::uint32_t{fault.ip});
    Assert::AreEqual(0x2A07u, Ax(rig), L"nothing serviced");
    Assert::AreEqual(std::uint32_t{flags}, std::uint32_t{rig.Regs().flags});

    rig.Dos(0x4C);
    Assert::AreEqual(0x2Au, std::uint32_t{rig.Services().Fault().value_or(Machine::ServiceFault{}).ah}, L"the first fault is kept");
  }

  TEST_METHOD(StandardHandlesAndZeroByteWritesFault)
  {
    ServiceRig rig("DosStandardHandles");
    rig.PutString(BUFFER, "x", false);
    rig.Regs().bx = 1;
    rig.Regs().cx = 1;
    rig.Dos(0x40);
    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnsupportedRequest), L"stdout is not provided");
    rig.Services().ClearFault();

    const std::uint16_t handle = CreateFile(rig, "TRUNC.CDR");
    rig.Regs().bx = handle;
    rig.Regs().cx = 0;
    rig.Dos(0x40);
    Assert::IsTrue(rig.Faulted(Machine::FaultKind::UnsupportedRequest), L"a truncating write is not provided");
  }
};

} // namespace MachineTests

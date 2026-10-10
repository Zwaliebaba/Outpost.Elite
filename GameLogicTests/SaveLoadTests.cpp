#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "TwinRig.h"

#include <array>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t CRITICAL_ERROR_INTERRUPT = 0x02F0;
constexpr std::uint16_t SHOW_DISK_ERROR = 0x0470;
constexpr std::uint16_t PRINT_COMMANDER_CATALOGUE = 0x68CE;

// The interrupt table's words for int 24h, DOS's critical-error handler, and int 33h, the mouse driver.
constexpr std::uint8_t CRITICAL_ERROR_VECTOR = 0x24;
constexpr std::uint16_t CRITICAL_ERROR_VECTOR_OFFSET = 0x0090;
constexpr std::uint16_t CRITICAL_ERROR_VECTOR_SEGMENT = 0x0092;
constexpr std::uint16_t MOUSE_VECTOR_OFFSET = 0x00CC;
constexpr std::uint16_t MOUSE_VECTOR_SEGMENT = 0x00CE;
constexpr std::uint16_t INTERRUPT_TABLE_SEGMENT = 0;

// screenLayout's bit 1: the text page shows.
constexpr std::uint8_t TEXT_LAYOUT = 2;
constexpr std::uint8_t COCKPIT_LAYOUT = 0;

// The last byte of keyBuffer: the write address wraps after it.
constexpr std::uint16_t KEY_BUFFER_WRAP_MASK = 0x000F;

// Alt and PrtSc held for a moment: GetKey saves a screenshot while both are down. PrtSc also types '*' at a prompt.
constexpr std::string_view SCREENSHOT_KEYS = "down Alt_L; down KP_Multiply; wait 0.02; up KP_Multiply; up Alt_L; wait 0.05";
constexpr std::string_view QUICK_SCREENSHOT_KEYS = "down Alt_L; down KP_Multiply; wait 0.002; up KP_Multiply; up Alt_L; wait 0.05";

void SetDataByte(Machine::Pc& _pc, const Machine::LoadedProgram& _program, std::uint16_t _offset, std::uint8_t _value)
{
  _pc.Ram().Write8(Elite::DataSegment(_program), _offset, _value);
}

void SetDataWord(Machine::Pc& _pc, const Machine::LoadedProgram& _program, std::uint16_t _offset, std::uint16_t _value)
{
  _pc.Ram().Write16(Elite::DataSegment(_program), _offset, _value);
}

// _code at the end of keyBuffer, as ReadScanCode puts a key there: how a test sends a code no replay can name, such as
// the Amstrad joystick's 77h-7Ch.
void QueueScanCode(Machine::Pc& _pc, const Machine::LoadedProgram& _program, std::uint8_t _code)
{
  Machine::Memory& ram = _pc.Ram();
  const std::uint16_t data = Elite::DataSegment(_program);
  const std::uint16_t write = ram.Read16(data, Elite::DS.keyBufferWrite.offset);
  ram.Write8(data, write, _code);
  const auto next = static_cast<std::uint16_t>(write + 1);
  ram.Write16(data, Elite::DS.keyBufferWrite.offset, (next & KEY_BUFFER_WRAP_MASK) == 0 ? Elite::DS.keyBuffer.offset : next);
  ram.Write8(data, Elite::DS.keyBufferCount.offset, static_cast<std::uint8_t>(ram.Read8(data, Elite::DS.keyBufferCount.offset) + 1));
}

// Every file in _directory, by name, with its bytes; a directory as an empty name's worth of nothing but its name.
[[nodiscard]] std::map<std::string, std::string> Contents(const std::filesystem::path& _directory)
{
  std::map<std::string, std::string> contents;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(_directory))
  {
    std::string bytes = "<directory>";
    if (entry.is_regular_file())
    {
      std::ifstream file(entry.path(), std::ios::binary);
      bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    contents[entry.path().filename().string()] = bytes;
  }
  return contents;
}

// The Disc/Control menu's every path but leaving for DOS: the catalogue empty and with a name, the version, keys that do
// nothing, a mouse driver absent and present, the IBM stick and the Amstrad's, saving, deleting, a load that DOS fails
// and its retry, names cut at eight characters and edited, and every kind of bad name, each rejected into the menu's
// key loop from inside PromptCommanderFileName.
void PlayEveryDiscMenuPath(TwinRig& _rig)
{
  _rig.Play("key space; wait 3.3\nkey Escape; wait 0.1\nkey c; wait 0.1\ndigest no-names");
  _rig.Play("key v; wait 0.05\nkey q; wait 0.05\nkey Next; wait 0.05\nkey m; wait 0.05\ndigest no-mouse");
  // A mouse driver: int 33h pointing at code that is not an IRET.
  const Machine::Memory& ram = _rig.Original().Ram();
  const std::uint16_t mouseOffset = ram.Read16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_OFFSET);
  const std::uint16_t mouseSegment = ram.Read16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_SEGMENT);
  _rig.Both(
    [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      _pc.Ram().Write16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_OFFSET, 0);
      _pc.Ram().Write16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_SEGMENT, _program.loadSegment);
    });
  _rig.Play("key m; wait 0.05\ndigest mouse\nkey k; wait 0.05");
  _rig.Both(
    [mouseOffset, mouseSegment](Machine::Pc& _pc, const Machine::LoadedProgram&)
    {
      _pc.Ram().Write16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_OFFSET, mouseOffset);
      _pc.Ram().Write16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_SEGMENT, mouseSegment);
    });
  // The IBM stick, with nothing in the port and then with a stick in it.
  _rig.Play("key j; wait 0.05; key q; wait 0.05; key i; wait 0.05; key space; wait 0.1\ndigest no-stick");
  constexpr std::array<std::size_t, 2> AXES = {0, 1};
  _rig.Both(
    [&AXES](Machine::Pc& _pc, const Machine::LoadedProgram&)
    {
      for (const std::size_t axis : AXES)
        _pc.Joystick().SetAxisResistance(axis, Machine::GamePort::MAXIMUM_OHMS / 2);
    });
  _rig.Play("key j; wait 0.05; key i; wait 0.05; key space; wait 0.1\ndigest stick\nkey k; wait 0.05");
  _rig.Both(
    [&AXES](Machine::Pc& _pc, const Machine::LoadedProgram&)
    {
      for (const std::size_t axis : AXES)
        _pc.Joystick().SetAxisResistance(axis, Machine::GamePort::AXIS_DISCONNECTED);
    });
  // The Amstrad's, which never moves, and then sends codes from beyond the keyboard's before a key.
  _rig.Play("key j; wait 0.05; key a; wait 0.05; key q; wait 0.05\ndigest amstrad-still\nkey j; wait 0.05; key a; wait 0.05");
  _rig.Both(
    [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      constexpr std::array<std::uint8_t, 3> CODES = {0x60, 0x7D, 0x77};
      for (const std::uint8_t code : CODES)
        QueueScanCode(_pc, _program, code);
    });
  _rig.Play("key q; wait 0.05\ndigest amstrad-moved\nkey k; wait 0.05");
  // Leaving for DOS, thought better of.
  _rig.Play("key e; wait 0.05; key q; wait 0.05; key n; wait 0.05\ndigest stayed");
  // AB1 saved, catalogued and deleted; then Z, which is not there, loaded, retried and given up.
  _rig.Play("key s; wait 0.05; key a; down Shift_L; key b; up Shift_L; key 1; key Return; wait 0.1\ndigest saved\n"
            "key c; wait 0.1\ndigest catalogued\n"
            "key d; wait 0.05; key a; key b; key 1; key Return; wait 0.1\ndigest deleted\n"
            "key l; wait 0.05; key z; key Return; wait 0.1\nkey q; wait 0.05; key y; wait 0.1\nkey n; wait 0.05\ndigest not-loaded");
  // Nine characters typed keep eight, and a backspace takes one back: ABCDEFGI.
  _rig.Play("key s; wait 0.05; key a; key b; key c; key d; key e; key f; key g; key h; key i; key BackSpace; key BackSpace; key i;"
            " key Return; wait 0.1\ndigest long-name");
  // Bad names: none, a digit or a bracket first, and then a minus, a semicolon or a bracket after a letter.
  _rig.Play("key s; wait 0.05; key Return; wait 0.05\nkey s; wait 0.05; key 1; key Return; wait 0.05\n"
            "key s; wait 0.05; key bracketleft; key Return; wait 0.05\nkey s; wait 0.05; key a; key minus; key Return; wait 0.05\n"
            "key s; wait 0.05; key a; key semicolon; key Return; wait 0.05\n"
            "key s; wait 0.05; key a; key bracketleft; key Return; wait 0.05\ndigest bad-names");
  // From inside the last prompt's frame: a good name, saved; then, back in the menu after another bad one, the
  // catalogue, and a function key out of the menu.
  _rig.Play("key s; wait 0.05; key a; key Return; wait 0.1\ndigest saved-a\nkey s; wait 0.05; key Return; wait 0.05\n"
            "key c; wait 0.1\ndigest catalogued-again\nkey s; wait 0.05; key Return; wait 0.05\nkey F9; wait 0.1\ndigest status");
  Assert::IsTrue(Contents(_rig.Files(false)) == Contents(_rig.Files(true)), L"the twins wrote the same files");
}

} // namespace

// The save-load routines (plan §6.3): constructed calls of the work routines, and twin runs of the Disc/Control menu
// that reach what no replay does.
TEST_CLASS(SaveLoadTests)
{
public:
  // DOS calls int 24h on a critical error; this machine's DOS never does, so the handler is called as DOS would,
  // with DS anything but the game's.
  TEST_METHOD(CriticalErrorInterruptAgrees)
  {
    ComparisonRig rig("CriticalErrorInterrupt");
    Machine::Memory& ram = rig.Host().Ram();
    ram.Write16(INTERRUPT_TABLE_SEGMENT, CRITICAL_ERROR_VECTOR_OFFSET, CRITICAL_ERROR_INTERRUPT);
    ram.Write16(INTERRUPT_TABLE_SEGMENT, CRITICAL_ERROR_VECTOR_SEGMENT, rig.Program().loadSegment);
    SetDataByte(rig.Host(), rig.Program(), Elite::DS.diskError.offset, 0);
    Machine::Registers& regs = rig.Host().Processor().Regs();
    const Machine::Registers saved = regs;
    regs.ax = 0x1A02;
    regs.ds = Elite::PSP_SEGMENT;
    rig.Host().CallInterrupt(CRITICAL_ERROR_VECTOR);
    regs = saved;
    rig.AssertAllAgreed(CRITICAL_ERROR_INTERRUPT, 1);
    Assert::AreEqual(std::uint8_t{1}, ram.Read8(Elite::DataSegment(rig.Program()), Elite::DS.diskError.offset));
  }

  // DISC ERROR on the text page and drawn on a graphics screen; SaveScreenshot calls it only after DOS failed.
  TEST_METHOD(ShowDiskErrorAgreesOnBothScreens)
  {
    ComparisonRig rig("ShowDiskError");
    const std::initializer_list<std::uint8_t> layouts = {TEXT_LAYOUT, COCKPIT_LAYOUT};
    for (const std::uint8_t layout : layouts)
    {
      SetDataByte(rig.Host(), rig.Program(), Elite::DS.screenLayout.offset, layout);
      SetDataByte(rig.Host(), rig.Program(), Elite::DS.diskError.offset, 1);
      rig.Call(SHOW_DISK_ERROR, {});
    }
    rig.AssertAllAgreed(SHOW_DISK_ERROR, layouts.size());
  }

  // No names, and 40, the most: four columns of ten.
  TEST_METHOD(PrintCommanderCatalogueAgreesFromNoneToForty)
  {
    ComparisonRig rig("PrintCommanderCatalogue");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    std::uint16_t next = Elite::DS.commanderFileList.offset;
    for (int name = 0; name < 40; ++name)
    {
      ram.Write8(data, next++, static_cast<std::uint8_t>('A' + name % 26));
      ram.Write8(data, next++, static_cast<std::uint8_t>('0' + name / 10));
      ram.Write8(data, next++, static_cast<std::uint8_t>('0' + name % 10));
      ram.Write8(data, next++, 0);
    }
    const std::initializer_list<std::uint8_t> counts = {0, 40, 13};
    for (const std::uint8_t count : counts)
    {
      ram.Write8(data, Elite::DS.commanderFileCount.offset, count);
      rig.Call(PRINT_COMMANDER_CATALOGUE, {});
    }
    rig.AssertAllAgreed(PRINT_COMMANDER_CATALOGUE, counts.size());
  }

  // Every path of the Disc/Control menu but leaving for DOS, each call of a work routine compared.
  TEST_METHOD(DiscMenuAgreesOnEveryPath)
  {
    TwinRig rig("TwinDiscMenu");
    PlayEveryDiscMenuPath(rig);
  }

  // The same uncompared. PerformDiskRequest calls DOS, so a compared call of it keeps the original's outcome (ADR-010
  // item 4): only here does its native code load, fail to load, list and delete.
  TEST_METHOD(DiscMenuAgreesOnEveryPathUncompared)
  {
    TwinRig rig("TwinDiscMenuUncompared", {.compared = false});
    PlayEveryDiscMenuPath(rig);
  }

  // Screenshots with SaveScreenshot's native code: of the galactic chart and of the file name prompt's text page, each
  // also with a directory where DOS must create the file, so that DISC ERROR shows; then E and Y at the menu, which
  // ends the program. Both twins must write the same files.
  TEST_METHOD(ScreenshotsAndLeavingForDosAgreeUncompared)
  {
    TwinRig twin("TwinScreenshots", {.compared = false});
    twin.Play("key space; wait 3.3\nkey F5; wait 0.1");
    twin.Play(SCREENSHOT_KEYS);
    // The numbers run '99', then '00', the name of a directory.
    twin.Both([](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
              { SetDataWord(_pc, _program, Elite::DS.screenshotNumber.offset, 0x3939); });
    for (const bool native : {false, true})
      std::filesystem::create_directories(twin.Files(native) / "ELITE00.HI");
    twin.Play(SCREENSHOT_KEYS);
    twin.Play("key Escape; wait 0.05; key s; wait 0.05");
    // '09', then '10'; then '11', a directory.
    twin.Both([](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
              { SetDataWord(_pc, _program, Elite::DS.screenshotNumber.offset, 0x3930); });
    twin.Play(QUICK_SCREENSHOT_KEYS);
    for (const bool native : {false, true})
      std::filesystem::create_directories(twin.Files(native) / "ELITE11.LO");
    twin.Play(QUICK_SCREENSHOT_KEYS);
    twin.Play("key BackSpace; key BackSpace; key x; key Return; wait 0.1\ndigest saved");
    twin.Play("key e; wait 0.05; key y; wait 0.5", Machine::StopReason::Terminated);
    const std::map<std::string, std::string> original = Contents(twin.Files(false));
    Assert::IsTrue(original == Contents(twin.Files(true)), L"the twins wrote the same files");
    Assert::IsTrue(original.contains("ELITE01.HI") && original.contains("ELITE10.LO") && original.contains("X.CDR"), L"the files");
  }

  // A screenshot in flight, which PollScreenDumpKey takes from RunFlight's loop while Alt and PrtSc are down: its
  // path to SaveScreenshot, which writes through DOS and so is never compared.
  TEST_METHOD(ScreenshotInFlightAgreesUncompared)
  {
    TwinRig twin("TwinFlightScreenshot", {.compared = false});
    twin.Play("key space; wait 3.3\nkey F1; wait 2\ndigest launched");
    twin.Play("down Alt_L; down KP_Multiply; wait 0.1; up KP_Multiply; up Alt_L; wait 0.1\ndigest dumped");
    const std::map<std::string, std::string> original = Contents(twin.Files(false));
    Assert::IsTrue(original == Contents(twin.Files(true)), L"the twins wrote the same files");
    Assert::IsTrue(original.contains("ELITE01.HI"), L"the screenshot");
  }
};

} // namespace GameLogicTests

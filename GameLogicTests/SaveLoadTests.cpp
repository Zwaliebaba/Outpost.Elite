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

[[nodiscard]] std::wstring Widen(std::string_view _text)
{
  return std::wstring(_text.begin(), _text.end());
}

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

// The directory ReferenceRig named _machine keeps DOS's files in (ScratchDirectory).
[[nodiscard]] std::filesystem::path FilesOf(std::string_view _machine)
{
  return std::filesystem::temp_directory_path() / ("OutpostEliteGameLogicTests-" + std::string(_machine));
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

// The reference twice from one boot, as TwinRig has it, for the runs TwinRig cannot take. The native twin's calls are
// not compared: a compared call of a routine that reaches DOS runs the original and keeps its outcome (ADR-010 item
// 4), so only an uncompared twin runs SaveScreenshot's native code. And a step may end the program, as long as it ends
// both. Every step must stop both twins the same way with the same digests, and they must end in one state. What the
// interpreted twin ran is saved as _name's offsets.
class UncomparedTwin
{
public:
  explicit UncomparedTwin(std::string_view _name)
    : m_name(_name),
      m_original(m_name + "-Original"),
      m_native(m_name + "-Native"),
      m_originalPlayer(m_original.Host(), m_original.Program()),
      m_nativePlayer(m_native.Host(), m_native.Program())
  {
    Assert::IsTrue(m_original.Loaded() && m_native.Loaded(), L"ELITES.EXE at the repository root");
    Elite::InstallNativeRoutines(m_native.Host(), m_native.Program());
    static_cast<void>(Play("wait 3"));
    m_original.Host().Processor().SetExecutionMap(&m_executed);
  }

  UncomparedTwin(const UncomparedTwin&) = delete;
  UncomparedTwin& operator=(const UncomparedTwin&) = delete;
  UncomparedTwin(UncomparedTwin&&) = delete;
  UncomparedTwin& operator=(UncomparedTwin&&) = delete;

  ~UncomparedTwin()
  {
    SaveExecutedOffsets(m_name, m_executed, m_original.Program());
  }

  template <typename Change> void Both(Change _change)
  {
    _change(m_original.Host(), m_original.Program());
    _change(m_native.Host(), m_native.Program());
  }

  /// Plays _steps on both, until the end or a step that stops them. Returns why the last step stopped.
  [[nodiscard]] Machine::StopReason Play(std::string_view _steps)
  {
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(_steps, steps, error), Widen(error).c_str());
    Machine::StopReason stopped = Machine::StopReason::Reached;
    for (const Elite::Step& step : steps)
    {
      std::string original;
      std::string native;
      const std::wstring where = Widen(m_name) + L" line " + std::to_wstring(step.line);
      stopped = m_originalPlayer.Play(step, original);
      Assert::IsTrue(m_nativePlayer.Play(step, native) == stopped, (where + L": the twins stopped differently").c_str());
      Assert::IsTrue(original == native, (where + L": the digests differ").c_str());
      if (stopped != Machine::StopReason::Reached)
      {
        break;
      }
    }
    Assert::IsTrue(Elite::GameStateDigest(m_original.Host(), m_original.Program()) ==
                     Elite::GameStateDigest(m_native.Host(), m_native.Program()),
                   (Widen(m_name) + L": the two end in different states").c_str());
    return stopped;
  }

  [[nodiscard]] std::filesystem::path Files(bool _native) const
  {
    return FilesOf(m_name + (_native ? "-Native" : "-Original"));
  }

private:
  std::string m_name;
  // What the interpreted twin runs once booted, made before the machine that marks it.
  std::vector<std::uint8_t> m_executed;
  ReferenceRig m_original;
  ReferenceRig m_native;
  Elite::ReplayPlayer m_originalPlayer;
  Elite::ReplayPlayer m_nativePlayer;
};

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

  // The Disc/Control menu's every path but leaving for DOS: the catalogue empty and with a name, the version, keys that do
  // nothing, a mouse driver absent and present, the IBM stick and the Amstrad's, saving, deleting, a load that DOS fails
  // and its retry, names cut at eight characters and edited, and every kind of bad name, each rejected into the menu's
  // key loop from inside PromptCommanderFileName.
  TEST_METHOD(DiscMenuAgreesOnEveryPath)
  {
    TwinRig rig("TwinDiscMenu");
    rig.Play("key space; wait 3.3\nkey Escape; wait 0.1\nkey c; wait 0.1\ndigest no-names");
    rig.Play("key v; wait 0.05\nkey q; wait 0.05\nkey Next; wait 0.05\nkey m; wait 0.05\ndigest no-mouse");
    // A mouse driver: int 33h pointing at code that is not an IRET.
    const Machine::Memory& ram = rig.Original().Ram();
    const std::uint16_t mouseOffset = ram.Read16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_OFFSET);
    const std::uint16_t mouseSegment = ram.Read16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_SEGMENT);
    rig.Both(
      [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
      {
        _pc.Ram().Write16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_OFFSET, 0);
        _pc.Ram().Write16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_SEGMENT, _program.loadSegment);
      });
    rig.Play("key m; wait 0.05\ndigest mouse\nkey k; wait 0.05");
    rig.Both(
      [mouseOffset, mouseSegment](Machine::Pc& _pc, const Machine::LoadedProgram&)
      {
        _pc.Ram().Write16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_OFFSET, mouseOffset);
        _pc.Ram().Write16(INTERRUPT_TABLE_SEGMENT, MOUSE_VECTOR_SEGMENT, mouseSegment);
      });
    // The IBM stick, with nothing in the port and then with a stick in it.
    rig.Play("key j; wait 0.05; key q; wait 0.05; key i; wait 0.05; key space; wait 0.1\ndigest no-stick");
    constexpr std::array<std::size_t, 2> AXES = {0, 1};
    rig.Both(
      [&AXES](Machine::Pc& _pc, const Machine::LoadedProgram&)
      {
        for (const std::size_t axis : AXES)
          _pc.Joystick().SetAxisResistance(axis, Machine::GamePort::MAXIMUM_OHMS / 2);
      });
    rig.Play("key j; wait 0.05; key i; wait 0.05; key space; wait 0.1\ndigest stick\nkey k; wait 0.05");
    rig.Both(
      [&AXES](Machine::Pc& _pc, const Machine::LoadedProgram&)
      {
        for (const std::size_t axis : AXES)
          _pc.Joystick().SetAxisResistance(axis, Machine::GamePort::AXIS_DISCONNECTED);
      });
    // The Amstrad's, which never moves, and then sends codes from beyond the keyboard's before a key.
    rig.Play("key j; wait 0.05; key a; wait 0.05; key q; wait 0.05\ndigest amstrad-still\nkey j; wait 0.05; key a; wait 0.05");
    rig.Both(
      [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
      {
        constexpr std::array<std::uint8_t, 3> CODES = {0x60, 0x7D, 0x77};
        for (const std::uint8_t code : CODES)
          QueueScanCode(_pc, _program, code);
      });
    rig.Play("key q; wait 0.05\ndigest amstrad-moved\nkey k; wait 0.05");
    // Leaving for DOS, thought better of.
    rig.Play("key e; wait 0.05; key q; wait 0.05; key n; wait 0.05\ndigest stayed");
    // AB1 saved, catalogued and deleted; then Z, which is not there, loaded, retried and given up.
    rig.Play("key s; wait 0.05; key a; down Shift_L; key b; up Shift_L; key 1; key Return; wait 0.1\ndigest saved\n"
             "key c; wait 0.1\ndigest catalogued\n"
             "key d; wait 0.05; key a; key b; key 1; key Return; wait 0.1\ndigest deleted\n"
             "key l; wait 0.05; key z; key Return; wait 0.1\nkey q; wait 0.05; key y; wait 0.1\nkey n; wait 0.05\ndigest not-loaded");
    // Nine characters typed keep eight, and a backspace takes one back: ABCDEFGI.
    rig.Play("key s; wait 0.05; key a; key b; key c; key d; key e; key f; key g; key h; key i; key BackSpace; key BackSpace; key i;"
             " key Return; wait 0.1\ndigest long-name");
    // Bad names: none, a digit or a bracket first, and then a minus, a semicolon or a bracket after a letter.
    rig.Play("key s; wait 0.05; key Return; wait 0.05\nkey s; wait 0.05; key 1; key Return; wait 0.05\n"
             "key s; wait 0.05; key bracketleft; key Return; wait 0.05\nkey s; wait 0.05; key a; key minus; key Return; wait 0.05\n"
             "key s; wait 0.05; key a; key semicolon; key Return; wait 0.05\n"
             "key s; wait 0.05; key a; key bracketleft; key Return; wait 0.05\ndigest bad-names");
    // From inside the last prompt's frame: a good name, saved; then, back in the menu after another bad one, the
    // catalogue, and a function key out of the menu.
    rig.Play("key s; wait 0.05; key a; key Return; wait 0.1\ndigest saved-a\nkey s; wait 0.05; key Return; wait 0.05\n"
             "key c; wait 0.1\ndigest catalogued-again\nkey s; wait 0.05; key Return; wait 0.05\nkey F9; wait 0.1\ndigest status");
  }

  // Screenshots with SaveScreenshot's native code: of the galactic chart and of the file name prompt's text page, each
  // also with a directory where DOS must create the file, so that DISC ERROR shows; then E and Y at the menu, which
  // ends the program. Both twins must write the same files.
  TEST_METHOD(ScreenshotsAndLeavingForDosAgreeUncompared)
  {
    UncomparedTwin twin("TwinScreenshots");
    Assert::IsTrue(twin.Play("key space; wait 3.3\nkey F5; wait 0.1") == Machine::StopReason::Reached, L"charted");
    Assert::IsTrue(twin.Play(SCREENSHOT_KEYS) == Machine::StopReason::Reached, L"the chart saved");
    // The numbers run '99', then '00', the name of a directory.
    twin.Both([](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
              { SetDataWord(_pc, _program, Elite::DS.screenshotNumber.offset, 0x3939); });
    for (const bool native : {false, true})
      std::filesystem::create_directories(twin.Files(native) / "ELITE00.HI");
    Assert::IsTrue(twin.Play(SCREENSHOT_KEYS) == Machine::StopReason::Reached, L"the chart failed");
    Assert::IsTrue(twin.Play("key Escape; wait 0.05; key s; wait 0.05") == Machine::StopReason::Reached, L"prompted");
    // '09', then '10'; then '11', a directory.
    twin.Both([](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
              { SetDataWord(_pc, _program, Elite::DS.screenshotNumber.offset, 0x3930); });
    Assert::IsTrue(twin.Play(QUICK_SCREENSHOT_KEYS) == Machine::StopReason::Reached, L"the prompt saved");
    for (const bool native : {false, true})
      std::filesystem::create_directories(twin.Files(native) / "ELITE11.LO");
    Assert::IsTrue(twin.Play(QUICK_SCREENSHOT_KEYS) == Machine::StopReason::Reached, L"the prompt failed");
    Assert::IsTrue(twin.Play("key BackSpace; key BackSpace; key x; key Return; wait 0.1\ndigest saved") == Machine::StopReason::Reached,
                   L"saved X");
    Assert::IsTrue(twin.Play("key e; wait 0.05; key y; wait 0.5") == Machine::StopReason::Terminated, L"the program ended");
    const std::map<std::string, std::string> original = Contents(twin.Files(false));
    Assert::IsTrue(original == Contents(twin.Files(true)), L"the twins wrote the same files");
    Assert::IsTrue(original.contains("ELITE01.HI") && original.contains("ELITE10.LO") && original.contains("X.CDR"), L"the files");
  }
};

} // namespace GameLogicTests

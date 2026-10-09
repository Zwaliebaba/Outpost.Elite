#include "pch.h"

#include "ServiceRig.h"

#include "ExeLoader.h"
#include "Firmware.h"

#include <initializer_list>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::size_t REFERENCE_BYTES = 98'144;
constexpr std::size_t REFERENCE_HEADER_BYTES = 0x40;
constexpr std::uint16_t REFERENCE_DATA_SEGMENT = 0x08F4;

// A word of the image, by offset. A plain aggregate rather than std::pair: brace-initializing a
// pair of 16-bit words from int literals narrows inside MSVC's <utility>, which /W4 /WX rejects.
struct ImageWord
{
  std::uint16_t offset;
  std::uint16_t value;
};

// A small MZ executable: a two-paragraph header, then _code as the image, entry 0000:0000, stack 0000:0100.
// Each relocation is the offset and segment of the word to relocate.
std::vector<std::uint8_t> MakeExecutable(const std::vector<std::uint8_t>& _code, std::uint16_t _minAllocation, std::uint16_t _maxAllocation,
                                         const std::vector<ImageWord>& _relocations = {})
{
  std::vector<std::uint8_t> file(0x20, 0);
  const auto put = [&file](std::size_t _offset, std::uint16_t _value)
  {
    file[_offset] = static_cast<std::uint8_t>(_value & 0xFF);
    file[_offset + 1] = static_cast<std::uint8_t>(_value >> 8);
  };
  const std::size_t bytes = file.size() + _code.size();
  put(0x00, 0x5A4D);
  put(0x02, static_cast<std::uint16_t>(bytes % 512));
  put(0x04, static_cast<std::uint16_t>((bytes + 511) / 512));
  put(0x06, static_cast<std::uint16_t>(_relocations.size()));
  put(0x08, 0x0002);
  put(0x0A, _minAllocation);
  put(0x0C, _maxAllocation);
  put(0x0E, 0x0000);
  put(0x10, 0x0100);
  put(0x14, 0x0000);
  put(0x16, 0x0000);
  put(0x18, 0x001C);
  Assert::IsTrue(_relocations.size() <= 1, L"the header has room for one relocation");
  for (const auto& [offset, segment] : _relocations)
  {
    put(0x1C, offset);
    put(0x1E, segment);
  }
  file.insert(file.end(), _code.begin(), _code.end());
  return file;
}

Machine::LoadError Load(ServiceRig& _rig, const std::vector<std::uint8_t>& _file, const Machine::ExeLoader::Desc& _desc,
                        Machine::LoadedProgram& _program)
{
  return Machine::ExeLoader::Load(_rig.Ram(), _file, _desc, _program);
}

std::string ReadText(Machine::Memory& _memory, std::uint16_t _segment, std::uint16_t _offset, std::size_t _bytes)
{
  std::string text;
  for (std::size_t index = 0; index < _bytes; ++index)
  {
    text.push_back(static_cast<char>(_memory.Read8(_segment, static_cast<std::uint16_t>(_offset + index))));
  }
  return text;
}

} // namespace

TEST_CLASS(ExeLoaderTests)
{
public:
  // The reference has 7 relocations: five load DS with the data segment 08F4, one is the segment constant 140A of the
  // cockpit image (CS:7BE3, ShowCockpitScreen), and one is the 0000 in StartContinuousNoise's immediate (CS:7B5D).
  TEST_METHOD(LoadsTheReferenceAndAppliesItsSevenRelocations)
  {
    const std::vector<std::uint8_t> file = ReadReferenceBinary();
    Assert::AreEqual(REFERENCE_BYTES, file.size(), L"ELITES.EXE at the repository root");
    ServiceRig rig("LoaderReference");
    Machine::ExeLoader::Desc desc;
    desc.pspSegment = ServiceRig::PSP_SEGMENT;
    Machine::LoadedProgram program;

    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::None);

    Assert::AreEqual(0x1000u, std::uint32_t{program.pspSegment});
    Assert::AreEqual(0x1010u, std::uint32_t{program.loadSegment});
    Assert::AreEqual(0xA000u, std::uint32_t{program.memoryTopSegment}, L"maximum allocation FFFFh takes everything");
    Assert::AreEqual(98'080u, program.imageBytes);
    const std::vector<ImageWord> relocated = {{0x0007, 0x08F4}, {0x020A, 0x08F4}, {0x021E, 0x08F4}, {0x02F2, 0x08F4},
                                              {0x060E, 0x08F4}, {0x7B5D, 0x0000}, {0x7BE3, 0x140A}};
    for (const auto& [offset, value] : relocated)
    {
      Assert::AreEqual(std::uint32_t{value} + 0x1010u, std::uint32_t{rig.Ram().Read16(program.loadSegment, offset)});
    }
    // Everything else is the file's bytes.
    for (const std::uint16_t offset : std::initializer_list<std::uint16_t>{0x0000, 0x0005, 0x0009, 0x7BE5})
    {
      Assert::AreEqual(std::uint32_t{file[REFERENCE_HEADER_BYTES + offset]}, std::uint32_t{rig.Ram().Read8(program.loadSegment, offset)});
    }
    Assert::AreEqual(std::uint32_t{file.back()}, std::uint32_t{rig.Ram().Read8(Machine::Memory::Linear(program.loadSegment, 0) + 98'079u)});
    // The bytes the mouse test runs are the game's IsMouseDriverInstalled.
    for (std::size_t index = 0; index < IS_MOUSE_DRIVER_INSTALLED.size(); ++index)
    {
      Assert::AreEqual(std::uint32_t{IS_MOUSE_DRIVER_INSTALLED[index]},
                       std::uint32_t{rig.Ram().Read8(program.loadSegment, static_cast<std::uint16_t>(0x02D4 + index))});
    }
  }

  // MS-DOS's entry state, as DOSBox-X copies it, so that boot traces compare register for register.
  TEST_METHOD(StartsTheReferenceInDosRegisterState)
  {
    const std::vector<std::uint8_t> file = ReadReferenceBinary();
    Assert::AreEqual(REFERENCE_BYTES, file.size());
    ServiceRig rig("LoaderRegisters");
    Machine::ExeLoader::Desc desc;
    desc.pspSegment = 0x0192;
    Machine::LoadedProgram program;
    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::None);

    const Machine::Registers& regs = program.registers;
    Assert::AreEqual(0x01A2u, std::uint32_t{regs.cs}, L"CS = load segment + 0");
    Assert::AreEqual(0x0000u, std::uint32_t{regs.ip});
    Assert::AreEqual(0x01A2u + 0x13CAu, std::uint32_t{regs.ss}, L"SS = load segment + 13CA");
    Assert::AreEqual(0x03F8u, std::uint32_t{regs.sp});
    Assert::AreEqual(0x0192u, std::uint32_t{regs.ds});
    Assert::AreEqual(0x0192u, std::uint32_t{regs.es});
    Assert::AreEqual(0x0000u, std::uint32_t{regs.ax});
    Assert::AreEqual(0x0000u, std::uint32_t{regs.bx});
    Assert::AreEqual(0x00FFu, std::uint32_t{regs.cx});
    Assert::AreEqual(0x0192u, std::uint32_t{regs.dx}, L"DX = PSP");
    Assert::AreEqual(0x0000u, std::uint32_t{regs.si}, L"SI = IP");
    Assert::AreEqual(0x03F8u, std::uint32_t{regs.di}, L"DI = SP");
    Assert::AreEqual(0x091Cu, std::uint32_t{regs.bp});
    Assert::AreEqual(0xF202u, std::uint32_t{regs.flags});
    Assert::AreEqual(0x0000u, std::uint32_t{rig.Ram().Read16(regs.ss, 0x03F4)}, L"the RETF frame: IP");
    Assert::AreEqual(0x01A2u, std::uint32_t{rig.Ram().Read16(regs.ss, 0x03F6)}, L"the RETF frame: CS");
    Assert::IsTrue(program.environmentSegment < program.pspSegment, L"the environment sits below the PSP");

    desc.initialAx = 0x1234;
    desc.initialBx = 0x5678;
    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::None);
    Assert::AreEqual(0x1234u, std::uint32_t{program.registers.ax}, L"the caller's AX");
    Assert::AreEqual(0x5678u, std::uint32_t{program.registers.bx}, L"the caller's BX");
  }

  TEST_METHOD(BuildsThePspAndTheEnvironment)
  {
    const std::vector<std::uint8_t> file = ReadReferenceBinary();
    Assert::AreEqual(REFERENCE_BYTES, file.size());
    ServiceRig rig("LoaderPsp");
    Machine::ExeLoader::Desc desc;
    desc.commandTail = " cheat";
    Machine::LoadedProgram program;
    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::None);
    Machine::Memory& ram = rig.Ram();
    const std::uint16_t psp = program.pspSegment;

    Assert::AreEqual(0xCDu, std::uint32_t{ram.Read8(psp, 0x00)}, L"int 20h");
    Assert::AreEqual(0x20u, std::uint32_t{ram.Read8(psp, 0x01)});
    Assert::AreEqual(0xA000u, std::uint32_t{ram.Read16(psp, 0x02)}, L"memory top");
    Assert::AreEqual(std::uint32_t{Machine::Firmware::HALT_OFFSET}, std::uint32_t{ram.Read16(psp, 0x0A)}, L"terminate address");
    Assert::AreEqual(0xF000u, std::uint32_t{ram.Read16(psp, 0x0C)});
    Assert::AreEqual(std::uint32_t{psp}, std::uint32_t{ram.Read16(psp, 0x16)}, L"parent");
    Assert::IsTrue(ReadText(ram, psp, 0x18, 6) == std::string("\x01\x01\x01\x00\x02\xFF", 6), L"handles 0-4, then free");
    Assert::AreEqual(std::uint32_t{program.environmentSegment}, std::uint32_t{ram.Read16(psp, 0x2C)});
    Assert::AreEqual(20u, std::uint32_t{ram.Read16(psp, 0x32)});
    Assert::IsTrue(ReadText(ram, psp, 0x50, 3) == "\xCD\x21\xCB");
    Assert::IsTrue(ReadText(ram, psp, 0x5D, 11) == "           ", L"FCB 1, blank");
    // CheckCheatArgument (CS:02A5) wants length 6 and " cheat".
    Assert::AreEqual(6u, std::uint32_t{ram.Read8(psp, 0x80)});
    Assert::IsTrue(ReadText(ram, psp, 0x81, 7) == " cheat\r");

    const std::string environment = ReadText(ram, program.environmentSegment, 0, 40);
    const std::string expected = std::string("COMSPEC=C:\\COMMAND.COM\0\0\x01\0C:\\ELITES.EXE\0", 40);
    Assert::IsTrue(environment == expected);

    desc.commandTail = std::string_view("0123456789012345678901234567890123456789012345678901234567890123456789"
                                        "012345678901234567890123456789012345678901234567890123456789");
    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::CommandTailTooLong);
  }

  // ADR-001 D5: DS:25E4h, file offset B564h, from 00 to 01.
  TEST_METHOD(PatchByteAppliesD5OnlyOverTheExpectedValue)
  {
    const std::vector<std::uint8_t> file = ReadReferenceBinary();
    Assert::AreEqual(REFERENCE_BYTES, file.size());
    Assert::AreEqual(0u, std::uint32_t{file[0xB564]});
    ServiceRig rig("LoaderPatch");
    Machine::LoadedProgram program;
    Assert::IsTrue(Load(rig, file, Machine::ExeLoader::Desc{}, program) == Machine::LoadError::None);
    const auto dataSegment = static_cast<std::uint16_t>(program.loadSegment + REFERENCE_DATA_SEGMENT);

    Assert::IsTrue(Machine::ExeLoader::PatchByte(rig.Ram(), program, REFERENCE_DATA_SEGMENT, 0x25E4, 0x00, 0x01));
    Assert::AreEqual(1u, std::uint32_t{rig.Ram().Read8(dataSegment, 0x25E4)});
    Assert::IsFalse(Machine::ExeLoader::PatchByte(rig.Ram(), program, REFERENCE_DATA_SEGMENT, 0x25E4, 0x00, 0x02), L"not 00 any more");
    Assert::AreEqual(1u, std::uint32_t{rig.Ram().Read8(dataSegment, 0x25E4)}, L"and nothing was written");
  }

  TEST_METHOD(AllocationHonoursTheHeader)
  {
    ServiceRig rig("LoaderAllocation");
    const std::vector<std::uint8_t> code(0x30, 0x90); // three paragraphs of NOPs
    Machine::ExeLoader::Desc desc;
    Machine::LoadedProgram program;

    Assert::IsTrue(Load(rig, MakeExecutable(code, 0x10, 0x20), desc, program) == Machine::LoadError::None);
    Assert::AreEqual(0x1000u + 0x10u + 0x03u + 0x20u, std::uint32_t{program.memoryTopSegment}, L"the maximum when it fits");

    desc.memoryTopSegment = 0x1000 + 0x10 + 0x03 + 0x18;
    Assert::IsTrue(Load(rig, MakeExecutable(code, 0x10, 0x20), desc, program) == Machine::LoadError::None);
    Assert::AreEqual(std::uint32_t{desc.memoryTopSegment}, std::uint32_t{program.memoryTopSegment}, L"what there is, above the minimum");

    desc.memoryTopSegment = 0x1000 + 0x10 + 0x03 + 0x0F;
    rig.Ram().Write8(desc.pspSegment, 0, 0x5A);
    Assert::IsTrue(Load(rig, MakeExecutable(code, 0x10, 0x20), desc, program) == Machine::LoadError::DoesNotFit);
    Assert::AreEqual(0x5Au, std::uint32_t{rig.Ram().Read8(desc.pspSegment, 0)}, L"a failed load writes nothing");
  }

  TEST_METHOD(MalformedExecutablesAreRefused)
  {
    ServiceRig rig("LoaderMalformed");
    const Machine::ExeLoader::Desc desc{};
    Machine::LoadedProgram program;
    const std::vector<std::uint8_t> code(0x10, 0x90);

    std::vector<std::uint8_t> file = MakeExecutable(code, 0x10, 0x10);
    file[0] = 'X';
    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::NotExecutable);

    file = MakeExecutable(code, 0x10, 0x10);
    file.pop_back();
    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::Truncated);

    Assert::IsTrue(Load(rig, MakeExecutable(code, 0x10, 0x10, {{0x000F, 0x0000}}), desc, program) == Machine::LoadError::BadRelocation);
    Assert::IsTrue(Load(rig, MakeExecutable(code, 0x00, 0x00), desc, program) == Machine::LoadError::Unsupported, L"load high");

    Machine::ExeLoader::Desc low;
    low.pspSegment = Machine::ExeLoader::LOWEST_FREE_SEGMENT;
    Assert::IsTrue(Load(rig, MakeExecutable(code, 0x10, 0x10), low, program) == Machine::LoadError::PspTooLow);
  }

  // ExitToDos (CS:00C5): Start pushed PSP:0000, and the RETF lands on the PSP's CD 20.
  TEST_METHOD(ProgramEndsThroughRetfToThePsp)
  {
    ServiceRig rig("LoaderExit");
    // Start's first instructions, then straight to the exit: mov ax,ds; push ax; xor bx,bx; push bx; retf.
    const std::vector<std::uint8_t> code = {0x8C, 0xD8, 0x50, 0x33, 0xDB, 0x53, 0xCB};
    Machine::ExeLoader::Desc desc;
    desc.pspSegment = ServiceRig::PSP_SEGMENT;
    Machine::LoadedProgram program;
    Assert::IsTrue(Load(rig, MakeExecutable(code, 0x10, 0xFFFF), desc, program) == Machine::LoadError::None);
    rig.Services().StartProgram(program.pspSegment);
    rig.Regs() = program.registers;

    for (int step = 0; step < 6; ++step) // five instructions, then the PSP's int 20h
    {
      (void)rig.Processor().Step();
    }

    Assert::IsTrue(rig.Services().Terminated());
    Assert::AreEqual(0u, std::uint32_t{rig.Services().ExitCode()});
    Assert::IsFalse(rig.Services().Fault().has_value());
    Assert::AreEqual(0xF000u, std::uint32_t{rig.Regs().cs}, L"gone to the terminate address");
    Assert::AreEqual(std::uint32_t{Machine::Firmware::HALT_OFFSET}, std::uint32_t{rig.Regs().ip});
    (void)rig.Processor().Step();
    (void)rig.Processor().Step();
    Assert::IsTrue(rig.Processor().Halted(), L"and halted there");
  }
};

} // namespace MachineTests

#include "pch.h"

#include "PcRig.h"
#include "ServiceRig.h"

#include "ExeLoader.h"
#include "Firmware.h"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

// A word of the image, by offset. A plain aggregate rather than std::pair: brace-initializing a
// pair of 16-bit words from int literals narrows inside MSVC's <utility>, which /W4 /WX rejects.
struct ImageWord
{
  std::uint16_t offset;
  std::uint16_t value;
};

// Where an executable starts and where its stack is, relative to its load segment, as its MZ header says.
struct Entry
{
  std::uint16_t cs = 0x0000;
  std::uint16_t ip = 0x0000;
  std::uint16_t ss = 0x0000;
  std::uint16_t sp = 0x0100;
};

// A small MZ executable: a header of whole paragraphs holding the relocation table, then _code as the image. Each
// relocation is the offset and segment, from the image's start, of a word the load adds the load segment to.
std::vector<std::uint8_t> MakeExecutable(const std::vector<std::uint8_t>& _code, std::uint16_t _minAllocation, std::uint16_t _maxAllocation,
                                         const std::vector<ImageWord>& _relocations = {}, const Entry& _entry = {})
{
  constexpr std::size_t RELOCATION_TABLE = 0x1C;
  const std::size_t headerBytes = (RELOCATION_TABLE + 4 * _relocations.size() + 15) / 16 * 16;
  std::vector<std::uint8_t> file(headerBytes, 0);
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
  put(0x08, static_cast<std::uint16_t>(headerBytes / 16));
  put(0x0A, _minAllocation);
  put(0x0C, _maxAllocation);
  put(0x0E, _entry.ss);
  put(0x10, _entry.sp);
  put(0x14, _entry.ip);
  put(0x16, _entry.cs);
  put(0x18, static_cast<std::uint16_t>(RELOCATION_TABLE));
  std::size_t at = RELOCATION_TABLE;
  for (const auto& [offset, segment] : _relocations)
  {
    put(at, offset);
    put(at + 2, segment);
    at += 4;
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
  // Each relocation adds the load segment to its word, wherever its segment and offset put it in the image; every other
  // byte is the file's.
  TEST_METHOD(AppliesEveryRelocation)
  {
    std::vector<std::uint8_t> code(0x40, 0x90);
    const auto put = [&code](std::size_t _offset, std::uint16_t _value)
    {
      code[_offset] = static_cast<std::uint8_t>(_value & 0xFF);
      code[_offset + 1] = static_cast<std::uint8_t>(_value >> 8);
    };
    put(0x0002, 0x08F4);
    put(0x0010, 0x0000);
    put(0x0024, 0x140A);
    const std::vector<std::uint8_t> file = MakeExecutable(code, 0x10, 0xFFFF, {{0x0002, 0x0000}, {0x0010, 0x0000}, {0x0004, 0x0002}});
    ServiceRig rig("LoaderRelocations");
    Machine::ExeLoader::Desc desc;
    desc.pspSegment = ServiceRig::PSP_SEGMENT;
    Machine::LoadedProgram program;

    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::None);

    Assert::AreEqual(0x1000u, std::uint32_t{program.pspSegment});
    Assert::AreEqual(0x1010u, std::uint32_t{program.loadSegment});
    Assert::AreEqual(0xA000u, std::uint32_t{program.memoryTopSegment}, L"maximum allocation FFFFh takes everything");
    Assert::AreEqual(static_cast<std::uint32_t>(code.size()), program.imageBytes);
    const std::vector<ImageWord> relocated = {{0x0002, 0x08F4}, {0x0010, 0x0000}, {0x0024, 0x140A}};
    for (const auto& [offset, value] : relocated)
    {
      Assert::AreEqual(std::uint32_t{value} + 0x1010u, std::uint32_t{rig.Ram().Read16(program.loadSegment, offset)});
    }
    for (const std::uint16_t offset : std::initializer_list<std::uint16_t>{0x0000, 0x0005, 0x0012, 0x0026, 0x003F})
    {
      Assert::AreEqual(std::uint32_t{code[offset]}, std::uint32_t{rig.Ram().Read8(program.loadSegment, offset)}, L"the file's byte");
    }
  }

  // MS-DOS's entry state, as DOSBox-X copies it, so that boot traces compare register for register.
  TEST_METHOD(StartsInDosRegisterState)
  {
    const std::vector<std::uint8_t> file =
      MakeExecutable(std::vector<std::uint8_t>(0x40, 0x90), 0x10, 0xFFFF, {}, {0x0001, 0x0004, 0x0003, 0x0080});
    ServiceRig rig("LoaderRegisters");
    Machine::ExeLoader::Desc desc;
    desc.pspSegment = 0x0192;
    Machine::LoadedProgram program;
    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::None);

    const Machine::Registers& regs = program.registers;
    Assert::AreEqual(0x01A3u, std::uint32_t{regs.cs}, L"CS = load segment + 1");
    Assert::AreEqual(0x0004u, std::uint32_t{regs.ip});
    Assert::AreEqual(0x01A5u, std::uint32_t{regs.ss}, L"SS = load segment + 3");
    Assert::AreEqual(0x0080u, std::uint32_t{regs.sp});
    Assert::AreEqual(0x0192u, std::uint32_t{regs.ds});
    Assert::AreEqual(0x0192u, std::uint32_t{regs.es});
    Assert::AreEqual(0x0000u, std::uint32_t{regs.ax});
    Assert::AreEqual(0x0000u, std::uint32_t{regs.bx});
    Assert::AreEqual(0x00FFu, std::uint32_t{regs.cx});
    Assert::AreEqual(0x0192u, std::uint32_t{regs.dx}, L"DX = PSP");
    Assert::AreEqual(0x0004u, std::uint32_t{regs.si}, L"SI = IP");
    Assert::AreEqual(0x0080u, std::uint32_t{regs.di}, L"DI = SP");
    Assert::AreEqual(0x091Cu, std::uint32_t{regs.bp});
    Assert::AreEqual(0xF202u, std::uint32_t{regs.flags});
    Assert::AreEqual(0x0004u, std::uint32_t{rig.Ram().Read16(regs.ss, 0x007C)}, L"the RETF frame: IP");
    Assert::AreEqual(0x01A3u, std::uint32_t{rig.Ram().Read16(regs.ss, 0x007E)}, L"the RETF frame: CS");
    Assert::IsTrue(program.environmentSegment < program.pspSegment, L"the environment sits below the PSP");

    desc.initialAx = 0x1234;
    desc.initialBx = 0x5678;
    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::None);
    Assert::AreEqual(0x1234u, std::uint32_t{program.registers.ax}, L"the caller's AX");
    Assert::AreEqual(0x5678u, std::uint32_t{program.registers.bx}, L"the caller's BX");
  }

  TEST_METHOD(BuildsThePspAndTheEnvironment)
  {
    const std::vector<std::uint8_t> file = MakeExecutable(std::vector<std::uint8_t>(0x10, 0x90), 0x10, 0xFFFF);
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
    // The reference's CheckCheatArgument (CS:02A5) wants length 6 and " cheat".
    Assert::AreEqual(6u, std::uint32_t{ram.Read8(psp, 0x80)});
    Assert::IsTrue(ReadText(ram, psp, 0x81, 7) == " cheat\r");

    const std::string environment = ReadText(ram, program.environmentSegment, 0, 40);
    const std::string expected = std::string("COMSPEC=C:\\COMMAND.COM\0\0\x01\0C:\\ELITES.EXE\0", 40);
    Assert::IsTrue(environment == expected);

    desc.commandTail = std::string_view("0123456789012345678901234567890123456789012345678901234567890123456789"
                                        "012345678901234567890123456789012345678901234567890123456789");
    Assert::IsTrue(Load(rig, file, desc, program) == Machine::LoadError::CommandTailTooLong);
  }

  // PatchByte writes only over the byte it expects, as ADR-001's D5 does at the reference's DS:25E4h (08F4:25E4).
  TEST_METHOD(PatchByteWritesOnlyOverTheExpectedValue)
  {
    std::vector<std::uint8_t> code(0x40, 0x90);
    code[0x25] = 0x00;
    ServiceRig rig("LoaderPatch");
    Machine::LoadedProgram program;
    Assert::IsTrue(Load(rig, MakeExecutable(code, 0x10, 0xFFFF), Machine::ExeLoader::Desc{}, program) == Machine::LoadError::None);
    const auto segment = static_cast<std::uint16_t>(program.loadSegment + 0x0002);

    Assert::IsTrue(Machine::ExeLoader::PatchByte(rig.Ram(), program, 0x0002, 0x0005, 0x00, 0x01));
    Assert::AreEqual(1u, std::uint32_t{rig.Ram().Read8(segment, 0x0005)});
    Assert::IsFalse(Machine::ExeLoader::PatchByte(rig.Ram(), program, 0x0002, 0x0005, 0x00, 0x02), L"not 00 any more");
    Assert::AreEqual(1u, std::uint32_t{rig.Ram().Read8(segment, 0x0005)}, L"and nothing was written");
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

  // Parsing and placing are separate (Executable): a file parses into its header's facts, a view of its image and its
  // relocations, and that description loads as the file does. A description is checked as a file is: one with a
  // relocation outside its image is refused, and nothing is written.
  TEST_METHOD(ParsesAFileIntoTheDescriptionItLoads)
  {
    const std::vector<std::uint8_t> code = {0x90, 0x90, 0x34, 0x12, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    const std::vector<std::uint8_t> file = MakeExecutable(code, 0x10, 0x20, {{0x0002, 0x0000}});
    Machine::Executable executable;
    Assert::IsTrue(Machine::ExeLoader::Parse(file, executable) == Machine::LoadError::None);
    Assert::AreEqual(0x0000u, std::uint32_t{executable.initialCs});
    Assert::AreEqual(0x0000u, std::uint32_t{executable.initialIp});
    Assert::AreEqual(0x0000u, std::uint32_t{executable.initialSs});
    Assert::AreEqual(0x0100u, std::uint32_t{executable.initialSp});
    Assert::AreEqual(0x10u, std::uint32_t{executable.minAllocationParagraphs});
    Assert::AreEqual(0x20u, std::uint32_t{executable.maxAllocationParagraphs});
    Assert::IsTrue(executable.image.data() == file.data() + 0x20 && executable.image.size() == code.size(), L"a view of the file's image");
    Assert::AreEqual(std::size_t{1}, executable.relocations.size());
    Assert::AreEqual(0x0002u, std::uint32_t{executable.relocations[0].offset});
    Assert::AreEqual(0x0000u, std::uint32_t{executable.relocations[0].segment});

    ServiceRig fromFile("LoaderParsedFile");
    ServiceRig fromDescription("LoaderParsedDescription");
    const Machine::ExeLoader::Desc desc{};
    Machine::LoadedProgram filed;
    Machine::LoadedProgram described;
    Assert::IsTrue(Load(fromFile, file, desc, filed) == Machine::LoadError::None);
    Assert::IsTrue(Machine::ExeLoader::Load(fromDescription.Ram(), executable, desc, described) == Machine::LoadError::None);
    Assert::IsTrue(filed == described, L"loaded at the same place, in the same registers");
    Assert::IsTrue(std::ranges::equal(fromFile.Ram().Bytes(), fromDescription.Ram().Bytes()), L"with the same bytes");
    Assert::AreEqual(0x1234u + described.loadSegment, std::uint32_t{fromDescription.Ram().Read16(described.loadSegment, 0x0002)},
                     L"relocated");

    executable.relocations.push_back({0x000F, 0x0000});
    fromDescription.Ram().Write8(desc.pspSegment, 0, 0x5A);
    Assert::IsTrue(Machine::ExeLoader::Load(fromDescription.Ram(), executable, desc, described) == Machine::LoadError::BadRelocation);
    Assert::AreEqual(0x5Au, std::uint32_t{fromDescription.Ram().Read8(desc.pspSegment, 0)}, L"a refused load writes nothing");
  }

  // ExitToDos (CS:00C5): Start pushed PSP:0000, and the RETF lands on the PSP's CD 20. The program's entry is native, as
  // the reference's Start is, and the Pc's Dispatcher runs the PSP's INT 20h (DispatcherTests).
  TEST_METHOD(ProgramEndsThroughRetfToThePsp)
  {
    PcRig rig("LoaderExit");
    Machine::Pc& pc = rig.Host();
    Machine::ExeLoader::Desc desc;
    desc.pspSegment = ServiceRig::PSP_SEGMENT;
    Machine::LoadedProgram program;
    Assert::IsTrue(pc.Load(MakeExecutable({0x90}, 0x10, 0xFFFF), desc, program) == Machine::LoadError::None);
    // Start's first instructions, then straight to the exit: mov ax,ds; push ax; xor bx,bx; push bx; retf.
    pc.Hook(program.registers.cs, program.registers.ip, "Start",
            [](Machine::Pc& _pc)
            {
              Machine::Registers& regs = _pc.Processor().Regs();
              regs.ax = regs.ds;
              regs.bx = 0;
              for (const std::uint16_t word : {regs.ax, regs.bx})
              {
                regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
                _pc.Ram().Write16(regs.ss, regs.sp, word);
              }
              _pc.ReturnFar();
            });

    Assert::IsTrue(pc.RunUntil(1'000) == Machine::StopReason::Terminated, L"the PSP's int 20h ends the program");
    Assert::AreEqual(0u, std::uint32_t{pc.Services().ExitCode()});
    Assert::IsFalse(pc.Services().Fault().has_value());
    Assert::AreEqual(0xF000u, std::uint32_t{pc.Processor().Regs().cs}, L"gone to the terminate address");
    Assert::AreEqual(std::uint32_t{Machine::Firmware::HALT_OFFSET}, std::uint32_t{pc.Processor().Regs().ip});
  }
};

} // namespace MachineTests

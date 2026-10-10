#include "pch.h"

#include "DirectoryFileStore.h"
#include "Pc.h"
#include "Reference.h"
#include "ReferenceImage.h"
#include "ReferenceRig.h"
#include "Sha256.h"

#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

TEST_CLASS(ReferenceImageTests)
{
public:
  // D3: the generated image is ELITES.EXE's, so the hosts load it and nothing reads the file at runtime. The file is
  // read here, checked against ADR-007's SHA-256, and loaded from the file into one PC and from the generated tables into
  // another, as DOS would with the same Desc. All of memory, where each load put the program and the registers it starts
  // in must agree, and so must the writes each load counted, which paced time counts too (ADR-018 item 7).
  TEST_METHOD(TheGeneratedImageLoadsAsTheReferenceDoes)
  {
    const std::vector<std::uint8_t> file = Elite::ReadWholeFile(Elite::FindInRepository(Elite::REFERENCE_FILE_NAME));
    Assert::AreEqual(std::string(Elite::REFERENCE_SHA256).c_str(), Machine::Sha256::ToHex(Machine::Sha256::Of(file)).c_str(), false,
                     L"ELITES.EXE at the repository root, and the reference (ADR-007)");

    ScratchDirectory fileDirectory("ImageFromFile");
    ScratchDirectory tableDirectory("ImageFromTables");
    Machine::DirectoryFileStore fileStore(fileDirectory.Path());
    Machine::DirectoryFileStore tableStore(tableDirectory.Path());
    Machine::Pc::Desc power;
    power.startMoment = Elite::START_MOMENT;
    const auto fromFile = std::make_unique<Machine::Pc>(fileStore, power);
    const auto fromTables = std::make_unique<Machine::Pc>(tableStore, power);
    Machine::ExeLoader::Desc desc;
    desc.pspSegment = Elite::PSP_SEGMENT;
    Machine::LoadedProgram filed;
    Machine::LoadedProgram generated;
    Assert::IsTrue(fromFile->Load(file, desc, filed) == Machine::LoadError::None, L"the file loads");
    Assert::IsTrue(fromTables->Load(Elite::ReferenceExecutable(), desc, generated) == Machine::LoadError::None, L"the tables load");

    std::uint32_t firstDifference = Machine::Memory::SIZE_BYTES;
    for (std::uint32_t linear = 0; linear < Machine::Memory::SIZE_BYTES && firstDifference == Machine::Memory::SIZE_BYTES; ++linear)
    {
      if (fromFile->Ram().Read8(linear) != fromTables->Ram().Read8(linear))
      {
        firstDifference = linear;
      }
    }
    Assert::AreEqual(Machine::Memory::SIZE_BYTES, firstDifference, L"all 1 MiB of memory agrees: the first address where it differs");
    Assert::AreEqual(fromFile->Ram().ChangeCount(), fromTables->Ram().ChangeCount(), L"each load changed as many bytes");

    Assert::AreEqual(std::uint32_t{filed.pspSegment}, std::uint32_t{generated.pspSegment}, L"PSP");
    Assert::AreEqual(std::uint32_t{filed.loadSegment}, std::uint32_t{generated.loadSegment}, L"load segment");
    Assert::AreEqual(std::uint32_t{filed.environmentSegment}, std::uint32_t{generated.environmentSegment}, L"environment");
    Assert::AreEqual(std::uint32_t{filed.memoryTopSegment}, std::uint32_t{generated.memoryTopSegment}, L"memory top");
    Assert::AreEqual(filed.imageBytes, generated.imageBytes, L"image size");
    Assert::IsTrue(filed.registers == generated.registers, L"the registers the program starts in");
    Assert::IsTrue(filed == generated, L"and every other field of the loaded program");
    Assert::IsTrue(fromFile->Processor().Regs() == fromTables->Processor().Regs(), L"the processor's registers");
  }
};

} // namespace GameLogicTests

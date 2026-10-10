// GameLogicTests/ReferenceRig.h
#pragma once

#include "Cpu.h"
#include "DirectoryFileStore.h"
#include "Dispatcher.h"
#include "Pc.h"
#include "Reference.h"

#include <memory>
#include <string_view>
#include <system_error>

namespace GameLogicTests
{

/// A directory under the system's temporary directory, emptied when made and removed when done.
class ScratchDirectory
{
public:
  explicit ScratchDirectory(std::string_view _name)
    : m_path(std::filesystem::temp_directory_path() / ("OutpostEliteGameLogicTests-" + std::string(_name)))
  {
    std::error_code error;
    std::filesystem::remove_all(m_path, error);
    std::filesystem::create_directories(m_path, error);
  }

  ScratchDirectory(const ScratchDirectory&) = delete;
  ScratchDirectory& operator=(const ScratchDirectory&) = delete;

  ~ScratchDirectory()
  {
    try
    {
      std::error_code error;
      std::filesystem::remove_all(m_path, error);
    }
    catch (...)
    {
      // Left behind; the next run removes it before it starts.
      return;
    }
  }

  [[nodiscard]] const std::filesystem::path& Path() const noexcept
  {
    return m_path;
  }

private:
  std::filesystem::path m_path;
};

/// The reference loaded on a PC in paced time (ADR-008), as replays run it: the PSP where DOSBox-X puts
/// it, the clock starting at midnight on 1 January 1980 unless _startMoment says otherwise, DOS's files
/// in a scratch directory, and the program interpreted unless _makeProcessor makes a Dispatcher (ADR-011).
class ReferenceRig
{
public:
  explicit ReferenceRig(std::string_view _name, const Machine::Dos::DateTime& _startMoment = Elite::START_MOMENT,
                        Machine::ProcessorFactory _makeProcessor = &Machine::MakeCpu)
    : m_directory(_name),
      m_files(m_directory.Path()),
      m_pc(std::make_unique<Machine::Pc>(m_files, Desc(_startMoment), _makeProcessor))
  {
    m_pc->SetTimeMode(Machine::TimeMode::Paced);
    const std::vector<std::uint8_t> file = Elite::ReadWholeFile(Elite::FindInRepository(Elite::REFERENCE_FILE_NAME));
    m_loaded = Elite::LoadReference(*m_pc, file, Elite::PSP_SEGMENT, m_program) == Elite::ReferenceFailure::None;
  }

  [[nodiscard]] bool Loaded() const noexcept
  {
    return m_loaded;
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return *m_pc;
  }

  [[nodiscard]] const Machine::LoadedProgram& Program() const noexcept
  {
    return m_program;
  }

  /// Where DOS's files are.
  [[nodiscard]] const std::filesystem::path& Files() const noexcept
  {
    return m_directory.Path();
  }

private:
  [[nodiscard]] static Machine::Pc::Desc Desc(const Machine::Dos::DateTime& _startMoment) noexcept
  {
    Machine::Pc::Desc desc;
    desc.startMoment = _startMoment;
    return desc;
  }

  ScratchDirectory m_directory;
  Machine::DirectoryFileStore m_files;
  std::unique_ptr<Machine::Pc> m_pc;
  Machine::LoadedProgram m_program;
  bool m_loaded = false;
};

} // namespace GameLogicTests

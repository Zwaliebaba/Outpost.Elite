// MachineTests/PcRig.h
#pragma once

#include "DirectoryFileStore.h"
#include "Pc.h"
#include "ServiceRig.h"

#include <initializer_list>
#include <string_view>
#include <vector>

namespace MachineTests
{

/// A minimal MZ executable: a two-paragraph header, no relocations, the code at CS:IP 0000:0000, and
/// a stack at 0010:0100 inside the 32 paragraphs of minimum allocation.
inline std::vector<std::uint8_t> TinyExe(std::initializer_list<std::uint8_t> _code)
{
  constexpr std::size_t HEADER_BYTES = 0x20;
  const std::size_t fileBytes = HEADER_BYTES + _code.size();
  std::vector<std::uint8_t> file(HEADER_BYTES, 0);
  const auto put16 = [&](std::size_t _offset, std::size_t _value)
  {
    file[_offset] = static_cast<std::uint8_t>(_value & 0xFF);
    file[_offset + 1] = static_cast<std::uint8_t>((_value >> 8) & 0xFF);
  };
  file[0] = 'M';
  file[1] = 'Z';
  put16(0x02, fileBytes % 512);
  put16(0x04, (fileBytes + 511) / 512);
  put16(0x08, HEADER_BYTES / 16);
  put16(0x0A, 0x20);   // minimum allocation, paragraphs
  put16(0x0C, 0xFFFF); // maximum allocation
  put16(0x0E, 0x0010); // SS
  put16(0x10, 0x0100); // SP
  put16(0x18, 0x1C);   // relocation table offset
  file.insert(file.end(), _code);
  return file;
}

/// A PC with its files in a scratch directory and the clock starting at midnight. Its program runs on the Pc's
/// Dispatcher, so every entry it reaches must be hooked (Pc::Hook).
class PcRig
{
public:
  explicit PcRig(std::string_view _name)
    : m_directory(_name),
      m_files(m_directory.Path()),
      m_pc(m_files, Desc())
  {
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return m_pc;
  }

  [[nodiscard]] Machine::LoadedProgram Load(const std::vector<std::uint8_t>& _file)
  {
    Machine::LoadedProgram program;
    Microsoft::VisualStudio::CppUnitTestFramework::Assert::IsTrue(
      m_pc.Load(_file, Machine::ExeLoader::Desc{}, program) == Machine::LoadError::None, L"loads");
    return program;
  }

private:
  [[nodiscard]] static Machine::Pc::Desc Desc() noexcept
  {
    Machine::Pc::Desc desc;
    desc.startMoment = Machine::Dos::DateTime{1980, 1, 1, 0, 0, 0, 0};
    return desc;
  }

  ScratchDirectory m_directory;
  Machine::DirectoryFileStore m_files;
  Machine::Pc m_pc;
};

} // namespace MachineTests

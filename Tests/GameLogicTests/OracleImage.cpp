#include "pch.h"

#include "OracleImage.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Elite::Testing
{

namespace
{

/*
 * Find the repository root by walking up from this DLL until Outpost.slnx appears.
 *
 * Deliberately not a config file and not an environment variable: the paths inside the
 * repository are fixed, the only unknown is where the repository is, and the running binary
 * already knows that. A config file here would be one more thing to keep in step with reality
 * on every machine, for no information the process does not already have.
 */
std::filesystem::path FindRepositoryRoot()
{
  wchar_t modulePath[MAX_PATH] = {};
  HMODULE module = nullptr;

  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&FindRepositoryRoot), &module)
      == 0)
  {
    return {};
  }

  if (GetModuleFileNameW(module, modulePath, MAX_PATH) == 0)
  {
    return {};
  }

  std::error_code error;
  std::filesystem::path directory = std::filesystem::path(modulePath).parent_path();

  for (int depth = 0; depth < 12 && !directory.empty(); ++depth)
  {
    if (std::filesystem::exists(directory / "Outpost.slnx", error))
    {
      return directory;
    }
    const std::filesystem::path parent = directory.parent_path();
    if (parent == directory)
    {
      break;
    }
    directory = parent;
  }

  return {};
}

/// Reads a "NAME<tab>NUMBER" table, skipping blank lines and '#' comments.
bool ReadTable(const std::filesystem::path& _path, std::vector<std::pair<std::string, std::uint32_t>>& _outRows)
{
  std::ifstream file(_path);
  if (!file.is_open())
  {
    return false;
  }

  std::string line;
  while (std::getline(file, line))
  {
    if (line.empty() || line[0] == '#')
    {
      continue;
    }

    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos)
    {
      continue;
    }

    std::string name = line.substr(0, tab);
    const std::string value = line.substr(tab + 1);

    try
    {
      _outRows.emplace_back(std::move(name), static_cast<std::uint32_t>(std::stoul(value)));
    }
    catch (const std::exception&)
    {
      // A malformed row is the generator's problem, not this reader's. Skip it; the caller
      // notices through a missing label rather than through an exception crossing a test.
      continue;
    }
  }

  return true;
}

} // namespace

OracleImage::OracleImage()
{
  const std::filesystem::path root = FindRepositoryRoot();
  if (root.empty())
  {
    m_reason = "could not locate the repository root (no Outpost.slnx above the test binary)";
    return;
  }

  const std::filesystem::path reference = root / "Design" / "Reference";
  const std::filesystem::path labelsPath = reference / "Labels.txt";
  const std::filesystem::path binariesPath = reference / "Binaries.txt";
  const std::filesystem::path output =
    root / "Upstream" / "elite-source-code-library" / "versions" / "c64" / "3-assembled-output";

  std::vector<std::pair<std::string, std::uint32_t>> labelRows;
  if (!ReadTable(labelsPath, labelRows))
  {
    m_reason = "missing " + labelsPath.string() + " -- run: python tools/labels.py --assemble";
    return;
  }

  std::vector<std::pair<std::string, std::uint32_t>> binaryRows;
  if (!ReadTable(binariesPath, binaryRows))
  {
    m_reason = "missing " + binariesPath.string() + " -- run: python tools/labels.py --assemble";
    return;
  }

  for (const auto& [name, address] : labelRows)
  {
    if (address <= 0xFFFFu)
    {
      m_labels.emplace(name, static_cast<std::uint16_t>(address));
    }
  }

  for (const auto& [fileName, address] : binaryRows)
  {
    const std::filesystem::path binary = output / fileName;
    std::ifstream file(binary, std::ios::binary);
    if (!file.is_open())
    {
      m_reason = "missing " + binary.string() + " -- run: python tools/labels.py --assemble";
      return;
    }

    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (address + bytes.size() > m_memory.size())
    {
      std::ostringstream message;
      message << fileName << " loads at " << address << " and is " << bytes.size() << " bytes, which runs past 64K";
      m_reason = message.str();
      return;
    }

    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
      m_memory[address + index] = static_cast<std::uint8_t>(bytes[index]);
    }
    ++m_blockCount;
  }

  if (m_labels.empty() || m_blockCount == 0)
  {
    m_reason = "the label or binary tables are empty";
    return;
  }

  m_available = true;
}

const OracleImage& OracleImage::Instance()
{
  static const OracleImage instance;
  return instance;
}

bool OracleImage::TryLabel(const std::string& _name, std::uint16_t& _outAddress) const
{
  const auto found = m_labels.find(_name);
  if (found == m_labels.end())
  {
    return false;
  }
  _outAddress = found->second;
  return true;
}

std::uint16_t OracleImage::Label(const std::string& _name) const
{
  std::uint16_t address = 0;
  (void)TryLabel(_name, address);
  return address;
}

Cpu6502 OracleImage::Fresh() const
{
  Cpu6502 cpu;
  cpu.memory = m_memory;
  cpu.a = cpu.x = cpu.y = 0;
  cpu.sp = 0xFD;
  cpu.pc = 0;
  cpu.c = cpu.z = cpu.i = cpu.d = cpu.v = cpu.n = false;
  return cpu;
}

} // namespace Elite::Testing

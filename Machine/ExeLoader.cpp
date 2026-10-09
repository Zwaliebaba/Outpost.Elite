#include "pch.h"

#include "ExeLoader.h"

#include "Memory.h"

#include <algorithm>
#include <array>

namespace Machine
{

namespace
{

// The MZ header, by byte offset.
constexpr std::size_t HEADER_MIN_BYTES = 0x1C;
constexpr std::uint16_t SIGNATURE_MZ = 0x5A4D;
constexpr std::uint16_t SIGNATURE_ZM = 0x4D5A; // DOS takes either
constexpr std::size_t HEADER_LAST_PAGE_BYTES = 0x02;
constexpr std::size_t HEADER_PAGES = 0x04;
constexpr std::size_t HEADER_RELOCATIONS = 0x06;
constexpr std::size_t HEADER_PARAGRAPHS = 0x08;
constexpr std::size_t HEADER_MIN_ALLOCATION = 0x0A;
constexpr std::size_t HEADER_MAX_ALLOCATION = 0x0C;
constexpr std::size_t HEADER_SS = 0x0E;
constexpr std::size_t HEADER_SP = 0x10;
constexpr std::size_t HEADER_IP = 0x14;
constexpr std::size_t HEADER_CS = 0x16;
constexpr std::size_t HEADER_RELOCATION_TABLE = 0x18;
constexpr std::uint32_t PAGE_BYTES = 512;
constexpr std::uint32_t PARAGRAPH_BYTES = 16;
constexpr std::uint32_t RELOCATION_BYTES = 4;

// The PSP, by byte offset.
constexpr std::uint16_t PSP_BYTES = 0x100;
constexpr std::uint16_t PSP_MEMORY_TOP = 0x02;
constexpr std::uint16_t PSP_SAVED_VECTORS = 0x0A;
constexpr std::uint16_t PSP_PARENT = 0x16;
constexpr std::uint16_t PSP_HANDLE_TABLE = 0x18;
constexpr std::uint16_t PSP_ENVIRONMENT = 0x2C;
constexpr std::uint16_t PSP_HANDLE_COUNT = 0x32;
constexpr std::uint16_t PSP_HANDLE_POINTER = 0x34;
constexpr std::uint16_t PSP_PREVIOUS = 0x38;
constexpr std::uint16_t PSP_DOS_CALL = 0x50;
constexpr std::uint16_t PSP_FCB_1 = 0x5C;
constexpr std::uint16_t PSP_FCB_2 = 0x6C;
constexpr std::uint16_t PSP_COMMAND_TAIL = 0x80;
constexpr std::uint8_t FIRST_SAVED_VECTOR = 0x22;
constexpr std::uint8_t SAVED_VECTOR_COUNT = 3;
constexpr std::uint8_t HANDLE_COUNT = 20;
// Handles 0-4 are CON, CON, CON, AUX and PRN, as system file table entries 1, 1, 1, 0 and 2; FF is a free handle.
constexpr std::array<std::uint8_t, 5> STANDARD_HANDLES = {0x01, 0x01, 0x01, 0x00, 0x02};
constexpr std::uint8_t FREE_HANDLE = 0xFF;
constexpr std::array<std::uint8_t, 3> DOS_CALL = {0xCD, 0x21, 0xCB}; // int 21h; retf
constexpr std::uint8_t FCB_NAME_BYTES = 11;
constexpr std::uint8_t CARRIAGE_RETURN = 0x0D;

constexpr std::string_view ENVIRONMENT = "COMSPEC=C:\\COMMAND.COM";

// What MS-DOS leaves in the other registers at entry, as DOSBox-X copies it.
constexpr std::uint16_t ENTRY_CX = 0x00FF;
constexpr std::uint16_t ENTRY_BP = 0x091C;

std::uint16_t HeaderWord(std::span<const std::uint8_t> _file, std::size_t _offset) noexcept
{
  return static_cast<std::uint16_t>(_file[_offset] | (_file[_offset + 1] << 8));
}

// The environment: the one string, the empty string that ends the list, the count 1, the program's path.
std::uint32_t EnvironmentBytes(std::string_view _programPath) noexcept
{
  return static_cast<std::uint32_t>(ENVIRONMENT.size() + 1 + 1 + 2 + _programPath.size() + 1);
}

void WriteText(Memory& _memory, std::uint16_t _segment, std::uint16_t _offset, std::string_view _text) noexcept
{
  for (std::size_t index = 0; index < _text.size(); ++index)
  {
    _memory.Write8(_segment, static_cast<std::uint16_t>(_offset + index), static_cast<std::uint8_t>(_text[index]));
  }
}

} // namespace

LoadError ExeLoader::Load(Memory& _memory, std::span<const std::uint8_t> _file, const Desc& _desc, LoadedProgram& _program) noexcept
{
  // Check everything first, so that a failed load writes nothing.
  if (_file.size() < HEADER_MIN_BYTES)
  {
    return LoadError::NotExecutable;
  }
  const std::uint16_t signature = HeaderWord(_file, 0);
  const std::uint32_t lastPageBytes = HeaderWord(_file, HEADER_LAST_PAGE_BYTES);
  const std::uint32_t pages = HeaderWord(_file, HEADER_PAGES);
  const std::uint32_t relocations = HeaderWord(_file, HEADER_RELOCATIONS);
  const std::uint32_t headerBytes = std::uint32_t{HeaderWord(_file, HEADER_PARAGRAPHS)} * PARAGRAPH_BYTES;
  const std::uint32_t minAllocation = HeaderWord(_file, HEADER_MIN_ALLOCATION);
  const std::uint32_t maxAllocation = HeaderWord(_file, HEADER_MAX_ALLOCATION);
  const std::uint32_t relocationTable = HeaderWord(_file, HEADER_RELOCATION_TABLE);
  if ((signature != SIGNATURE_MZ && signature != SIGNATURE_ZM) || pages == 0 || lastPageBytes > PAGE_BYTES)
  {
    return LoadError::NotExecutable;
  }
  const std::uint32_t fileBytes = (pages - 1) * PAGE_BYTES + (lastPageBytes == 0 ? PAGE_BYTES : lastPageBytes);
  if (fileBytes > _file.size())
  {
    return LoadError::Truncated;
  }
  if (headerBytes < HEADER_MIN_BYTES || headerBytes > fileBytes)
  {
    return LoadError::NotExecutable;
  }
  const std::uint32_t imageBytes = fileBytes - headerBytes;
  if (relocationTable + relocations * RELOCATION_BYTES > _file.size())
  {
    return LoadError::Truncated;
  }
  for (std::uint32_t index = 0; index < relocations; ++index)
  {
    const std::size_t entry = relocationTable + index * RELOCATION_BYTES;
    const std::uint32_t position = std::uint32_t{HeaderWord(_file, entry + 2)} * PARAGRAPH_BYTES + HeaderWord(_file, entry);
    if (position + 2 > imageBytes)
    {
      return LoadError::BadRelocation;
    }
  }
  if (minAllocation == 0 && maxAllocation == 0)
  {
    return LoadError::Unsupported;
  }
  if (_desc.commandTail.size() > MAX_COMMAND_TAIL)
  {
    return LoadError::CommandTailTooLong;
  }

  const std::uint32_t psp = _desc.pspSegment;
  const std::uint32_t environmentParagraphs = (EnvironmentBytes(_desc.programPath) + PARAGRAPH_BYTES - 1) / PARAGRAPH_BYTES;
  if (psp < LOWEST_FREE_SEGMENT + environmentParagraphs)
  {
    return LoadError::PspTooLow;
  }
  const std::uint32_t top = _desc.memoryTopSegment;
  const std::uint32_t imageParagraphs = (imageBytes + PARAGRAPH_BYTES - 1) / PARAGRAPH_BYTES;
  const std::uint32_t needed = PSP_PARAGRAPHS + imageParagraphs + minAllocation;
  if (top <= psp || needed > top - psp)
  {
    return LoadError::DoesNotFit;
  }
  const std::uint32_t available = top - psp;
  const std::uint32_t allocated = std::min(available, std::max(needed, PSP_PARAGRAPHS + imageParagraphs + maxAllocation));

  // The image, relocated.
  const auto pspSegment = static_cast<std::uint16_t>(psp);
  const auto loadSegment = static_cast<std::uint16_t>(psp + PSP_PARAGRAPHS);
  const auto memoryTop = static_cast<std::uint16_t>(psp + allocated);
  const auto environmentSegment = static_cast<std::uint16_t>(psp - environmentParagraphs);
  const std::uint32_t loadBase = Memory::Linear(loadSegment, 0);
  _memory.Load(loadBase, _file.subspan(headerBytes, imageBytes));
  for (std::uint32_t index = 0; index < relocations; ++index)
  {
    const std::size_t entry = relocationTable + index * RELOCATION_BYTES;
    const std::uint32_t position = std::uint32_t{HeaderWord(_file, entry + 2)} * PARAGRAPH_BYTES + HeaderWord(_file, entry);
    const std::uint32_t linear = loadBase + position;
    _memory.Write16(linear, static_cast<std::uint16_t>(_memory.Read16(linear) + loadSegment));
  }

  // The PSP.
  for (std::uint16_t offset = 0; offset < PSP_BYTES; ++offset)
  {
    _memory.Write8(pspSegment, offset, 0);
  }
  _memory.Write8(pspSegment, 0, 0xCD);
  _memory.Write8(pspSegment, 1, 0x20);
  _memory.Write16(pspSegment, PSP_MEMORY_TOP, memoryTop);
  for (std::uint8_t index = 0; index < SAVED_VECTOR_COUNT; ++index)
  {
    const std::uint32_t vector = static_cast<std::uint32_t>(FIRST_SAVED_VECTOR + index) * 4u;
    const auto saved = static_cast<std::uint16_t>(PSP_SAVED_VECTORS + index * 4);
    _memory.Write16(pspSegment, saved, _memory.Read16(vector));
    _memory.Write16(pspSegment, static_cast<std::uint16_t>(saved + 2), _memory.Read16(vector + 2u));
  }
  _memory.Write16(pspSegment, PSP_PARENT, pspSegment);
  for (std::uint16_t handle = 0; handle < HANDLE_COUNT; ++handle)
  {
    const std::uint8_t entry = handle < STANDARD_HANDLES.size() ? STANDARD_HANDLES[handle] : FREE_HANDLE;
    _memory.Write8(pspSegment, static_cast<std::uint16_t>(PSP_HANDLE_TABLE + handle), entry);
  }
  _memory.Write16(pspSegment, PSP_ENVIRONMENT, environmentSegment);
  _memory.Write16(pspSegment, PSP_HANDLE_COUNT, HANDLE_COUNT);
  _memory.Write16(pspSegment, PSP_HANDLE_POINTER, PSP_HANDLE_TABLE);
  _memory.Write16(pspSegment, PSP_HANDLE_POINTER + 2, pspSegment);
  _memory.Write16(pspSegment, PSP_PREVIOUS, 0xFFFF);
  _memory.Write16(pspSegment, PSP_PREVIOUS + 2, 0xFFFF);
  for (std::size_t index = 0; index < DOS_CALL.size(); ++index)
  {
    _memory.Write8(pspSegment, static_cast<std::uint16_t>(PSP_DOS_CALL + index), DOS_CALL[index]);
  }
  for (const std::uint16_t fcb : {PSP_FCB_1, PSP_FCB_2})
  {
    for (std::uint16_t index = 1; index <= FCB_NAME_BYTES; ++index)
    {
      _memory.Write8(pspSegment, static_cast<std::uint16_t>(fcb + index), ' ');
    }
  }
  const auto tailBytes = static_cast<std::uint8_t>(_desc.commandTail.size());
  _memory.Write8(pspSegment, PSP_COMMAND_TAIL, tailBytes);
  WriteText(_memory, pspSegment, PSP_COMMAND_TAIL + 1, _desc.commandTail);
  _memory.Write8(pspSegment, static_cast<std::uint16_t>(PSP_COMMAND_TAIL + 1 + tailBytes), CARRIAGE_RETURN);

  // The environment.
  for (std::uint32_t offset = 0; offset < environmentParagraphs * PARAGRAPH_BYTES; ++offset)
  {
    _memory.Write8(environmentSegment, static_cast<std::uint16_t>(offset), 0);
  }
  WriteText(_memory, environmentSegment, 0, ENVIRONMENT);
  const auto count = static_cast<std::uint16_t>(ENVIRONMENT.size() + 2);
  _memory.Write16(environmentSegment, count, 1);
  WriteText(_memory, environmentSegment, static_cast<std::uint16_t>(count + 2), _desc.programPath);

  // The registers, and the RETF frame DOS enters the program through.
  Registers& regs = _program.registers;
  regs = Registers{};
  regs.cs = static_cast<std::uint16_t>(loadSegment + HeaderWord(_file, HEADER_CS));
  regs.ip = HeaderWord(_file, HEADER_IP);
  regs.ss = static_cast<std::uint16_t>(loadSegment + HeaderWord(_file, HEADER_SS));
  regs.sp = HeaderWord(_file, HEADER_SP);
  regs.ds = pspSegment;
  regs.es = pspSegment;
  regs.ax = _desc.initialAx;
  regs.bx = _desc.initialBx;
  regs.cx = ENTRY_CX;
  regs.dx = pspSegment;
  regs.si = regs.ip;
  regs.di = regs.sp;
  regs.bp = ENTRY_BP;
  regs.flags = static_cast<std::uint16_t>(FLAGS_FIXED_ONES | FLAG_INTERRUPT);
  _memory.Write16(regs.ss, static_cast<std::uint16_t>(regs.sp - 4), regs.ip);
  _memory.Write16(regs.ss, static_cast<std::uint16_t>(regs.sp - 2), regs.cs);

  _program.pspSegment = pspSegment;
  _program.loadSegment = loadSegment;
  _program.environmentSegment = environmentSegment;
  _program.memoryTopSegment = memoryTop;
  _program.imageBytes = imageBytes;
  return LoadError::None;
}

bool ExeLoader::PatchByte(Memory& _memory, const LoadedProgram& _program, std::uint16_t _imageSegment, std::uint16_t _offset,
                          std::uint8_t _expected, std::uint8_t _replacement) noexcept
{
  const auto segment = static_cast<std::uint16_t>(_program.loadSegment + _imageSegment);
  if (_memory.Read8(segment, _offset) != _expected)
  {
    return false;
  }
  _memory.Write8(segment, _offset, _replacement);
  return true;
}

} // namespace Machine

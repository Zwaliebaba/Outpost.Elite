#include "pch.h"

#include "TraceWriter.h"

#include "Registers.h"

#include <array>

namespace ReferenceRunner
{

TraceWriter::TraceWriter(const std::filesystem::path& _path, std::uint16_t _stopSegment, std::uint16_t _stopOffset)
  : m_out(_path, std::ios::binary | std::ios::trunc),
    m_stopSegment(_stopSegment),
    m_stopOffset(_stopOffset)
{
  m_out.write("XTRACE1", 8); // the terminating zero is the eighth byte
}

void TraceWriter::BeforeInstruction(const Machine::Registers& _registers)
{
  if (m_finished)
    return;
  if (_registers.cs == m_stopSegment && _registers.ip == m_stopOffset)
  {
    m_finished = true;
    m_out.flush();
    return;
  }
  const std::array<std::uint16_t, 14> words = {_registers.cs, _registers.ip, _registers.ax, _registers.bx,   _registers.cx,
                                               _registers.dx, _registers.si, _registers.di, _registers.bp,   _registers.sp,
                                               _registers.ds, _registers.es, _registers.ss, _registers.flags};
  std::array<char, words.size() * 2> bytes = {};
  for (std::size_t index = 0; index < words.size(); ++index)
  {
    bytes[index * 2] = static_cast<char>(words[index] & 0xFF);
    bytes[index * 2 + 1] = static_cast<char>(words[index] >> 8);
  }
  m_out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  ++m_records;
}

} // namespace ReferenceRunner

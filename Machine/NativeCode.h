// Machine/NativeCode.h
#pragma once

#include "Memory.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Machine
{

class Pc;

/// Whether a native routine can wait, which says where it runs (ADR-010 item 8).
enum class NativeWait : std::uint8_t
{
  Never, ///< work: it runs where it is called
  Always ///< it can wait, on every path or on some, or never returns: it runs on the native thread
};

/// A routine in C++ that stands in for the program's own code at an entry (ADR-010). It is entered as
/// the original is, with CS:IP at the entry and the caller's return address on the stack, and leaves
/// the way the original does: through Pc::ReturnNear, or whatever return the original makes.
using NativeRoutine = std::function<void(Pc&)>;

/// The native routines a Pc runs in place of the program's code (ADR-010). Pc owns one; it does the running, and this
/// keeps the books. Not thread-safe.
class NativeCode
{
public:
  struct Hook
  {
    std::string name;
    NativeRoutine routine;
    NativeWait wait = NativeWait::Never; ///< whether it can wait, and so where it runs (Pc::Hook)
    std::uint64_t calls = 0;             ///< times execution reached the entry
  };

  /// Adds _routine at _segment:_offset. Throws std::logic_error if there is one there already.
  void Add(std::uint16_t _segment, std::uint16_t _offset, std::string _name, NativeRoutine _routine, NativeWait _wait);

  /// A non-zero byte at the linear address of every entry: what the Dispatcher stops at. Empty until the
  /// first routine is added.
  [[nodiscard]] const std::vector<std::uint8_t>& Map() const noexcept
  {
    return m_map;
  }

  [[nodiscard]] Hook* At(std::uint32_t _linear) noexcept;

  /// Every routine, by the linear address of its entry.
  [[nodiscard]] const std::map<std::uint32_t, Hook>& Hooks() const noexcept
  {
    return m_hooks;
  }

  /// The native routine, the innermost, that waited past the end of a run off the native thread, or
  /// empty. It cannot stop there and go on in the next run, so the run stopped, StopReason::Overran
  /// (ADR-010 item 8): a routine that can wait must be hooked as one that does.
  [[nodiscard]] const std::string& Overran() const noexcept
  {
    return m_overran;
  }

  void SetOverran(std::string_view _routine)
  {
    if (m_overran.empty())
    {
      m_overran = _routine;
    }
  }

private:
  std::vector<std::uint8_t> m_map;
  std::map<std::uint32_t, Hook> m_hooks;
  std::string m_overran;
};

} // namespace Machine

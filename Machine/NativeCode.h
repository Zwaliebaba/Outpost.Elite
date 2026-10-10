// Machine/NativeCode.h
#pragma once

#include "Memory.h"
#include "Registers.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Machine
{

class Pc;

// Registers, as bits of NativeContract::clobbers. SP, SS, CS and IP are not among them: a routine
// must always return where the original does, with the stack as the original leaves it.
inline constexpr std::uint16_t REGISTER_AX = 0x0001;
inline constexpr std::uint16_t REGISTER_BX = 0x0002;
inline constexpr std::uint16_t REGISTER_CX = 0x0004;
inline constexpr std::uint16_t REGISTER_DX = 0x0008;
inline constexpr std::uint16_t REGISTER_SI = 0x0010;
inline constexpr std::uint16_t REGISTER_DI = 0x0020;
inline constexpr std::uint16_t REGISTER_BP = 0x0040;
inline constexpr std::uint16_t REGISTER_DS = 0x0080;
inline constexpr std::uint16_t REGISTER_ES = 0x0100;
inline constexpr std::uint16_t REGISTER_ALL = 0x01FF;

/// A routine's register contract, from Symbols.tsv (ADR-010): the registers it may leave as it likes, and the flags its
/// callers read. Every register a contract does not mention is one the routine leaves alone, and its callers may rely
/// on that. The comparison with the original held each routine to its contract until D7 deleted the comparison; the
/// entries that still pass one go with the register layer (ADR-012 item 4).
struct NativeContract
{
  std::uint16_t clobbers = 0; ///< REGISTER_* bits: registers the routine may leave as it likes
  std::uint16_t flags = 0;    ///< FLAG_* bits (Registers.h): flags its callers read
};

/// How the original at a hooked entry returns, which is what the native routine does to leave.
enum class NativeReturn : std::uint8_t
{
  Near,     ///< RET or RET n: the return offset on the stack, in the same code segment
  Far,      ///< RETF or RETF n: offset, then segment
  Interrupt ///< IRET: offset, segment, then flags; an interrupt handler's entry
};

/// Whether a native routine can wait, which says where it runs (ADR-010 item 8).
enum class NativeWait : std::uint8_t
{
  Never,     ///< work: it runs where it is called
  Sometimes, ///< it waits on some paths: it runs on the native thread
  Always     ///< it waits as a rule, or never returns: it runs on the native thread
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
    NativeContract contract;
    NativeReturn exit = NativeReturn::Near;
    NativeWait wait = NativeWait::Never; ///< whether it can wait, and so where it runs (Pc::Hook)
    std::uint16_t segment = 0;
    std::uint16_t offset = 0;
    std::uint64_t calls = 0; ///< times execution reached the entry
  };

  /// Adds _routine at _segment:_offset. Throws std::logic_error if there is one there already.
  void Add(std::uint16_t _segment, std::uint16_t _offset, std::string _name, NativeRoutine _routine, const NativeContract& _contract,
           NativeReturn _exit, NativeWait _wait);

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

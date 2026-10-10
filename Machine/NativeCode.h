// Machine/NativeCode.h
#pragma once

#include "Memory.h"
#include "PortRouter.h"
#include "Registers.h"
#include "Timing.h"

#include <array>
#include <cstddef>
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

/// What a native routine is held to when it is compared with the original (ADR-010), from the
/// routine's contract in Symbols.tsv. Every register must come out as the original leaves it, except
/// those the contract says it clobbers; a register a contract does not mention is one the routine
/// leaves alone, and its callers may rely on that. Flags are compared only where the contract names
/// them as results, because nearly every routine changes them and no caller reads them otherwise.
/// The replay digests check what this leaves out.
struct NativeContract
{
  std::uint16_t clobbers = 0; ///< REGISTER_* bits: registers the routine may leave as it likes
  std::uint16_t flags = 0;    ///< FLAG_* bits (Registers.h): flags its callers read
};

/// How the original at a hooked entry returns: what a comparison waits for to know its run is over,
/// and what the native routine does to leave.
enum class NativeReturn : std::uint8_t
{
  Near,     ///< RET or RET n: the return offset on the stack, in the same code segment
  Far,      ///< RETF or RETF n: offset, then segment
  Interrupt ///< IRET: offset, segment, then flags; an interrupt handler's entry
};

/// Whether a native routine can wait, which says where it runs and which of its calls are compared
/// with the original (ADR-010 item 8).
enum class NativeWait : std::uint8_t
{
  Never,     ///< work: it runs where it is called, and every call is compared
  Sometimes, ///< it waits on some paths: it runs on the native thread, and a call is compared when the original returns without waiting
  Always     ///< it waits as a rule, or never returns: it runs on the native thread and is never compared, what it calls is
};

/// A set of offsets in one segment, kept as a bitmap: a hook's coverage, which a comparison adds to for
/// every instruction the original runs.
class OffsetSet
{
public:
  void Insert(std::uint16_t _offset) noexcept
  {
    m_words[_offset / WORD_BITS] |= std::uint64_t{1} << (_offset % WORD_BITS);
  }

  [[nodiscard]] bool Contains(std::uint16_t _offset) const noexcept
  {
    return (m_words[_offset / WORD_BITS] & (std::uint64_t{1} << (_offset % WORD_BITS))) != 0;
  }

  /// Every offset in the set, lowest first.
  [[nodiscard]] std::vector<std::uint16_t> Offsets() const;

private:
  static constexpr std::size_t WORD_BITS = 64;

  std::array<std::uint64_t, 0x10000 / WORD_BITS> m_words{};
};

/// A routine in C++ that stands in for the program's own code at an entry (ADR-010). It is entered as
/// the original is, with CS:IP at the entry and the caller's return address on the stack, and leaves
/// the way the original does: through Pc::ReturnNear, or whatever return the original makes.
using NativeRoutine = std::function<void(Pc&)>;

/// The native routines a Pc runs in place of the program's code, and what comparing them with the
/// original found (ADR-010). Pc owns one; it does the running, and this keeps the books. Not
/// thread-safe.
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
    std::uint64_t calls = 0;        ///< times execution reached the entry
    std::uint64_t verified = 0;     ///< calls run both ways that agreed
    std::uint64_t unverifiable = 0; ///< calls whose original could not be undone: it waited, took an interrupt or called the services
    std::uint64_t mismatches = 0;   ///< calls run both ways that did not agree
    OffsetSet executed;             ///< offsets in the entry's segment the original ran in calls that were compared, its callees' included
  };

  /// One call where the native routine and the original did not agree.
  struct Mismatch
  {
    std::string routine;
    std::uint64_t call = 0; ///< the routine's call, counting from 1
    Cycles clock = 0;
    std::string difference;
  };

  /// What a journal can hold: the most bytes one routine's original may change and still be compared.
  static constexpr std::size_t JOURNAL_CAPACITY = 0x40000;
  /// Differences a Mismatch lists before it stops.
  static constexpr std::size_t MOST_DIFFERENCES = 8;

  /// Adds _routine at _segment:_offset. Throws std::logic_error if there is one there already.
  void Add(std::uint16_t _segment, std::uint16_t _offset, std::string _name, NativeRoutine _routine, const NativeContract& _contract,
           NativeReturn _exit, NativeWait _wait);

  /// A non-zero byte at the linear address of every entry: what the CPU stops at. Empty until the
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

  /// When on, every call of a native routine that is not already inside such a comparison runs the
  /// original first, all of it with no native routine in place, undoes it, runs the native routine,
  /// and compares the two. A native routine that only native code calls is compared through its
  /// callers. A routine that waits as a rule is not compared at all, and one that waits sometimes only
  /// on the calls where its original does not; what they call through hooks is. Whatever the
  /// comparison finds, the run carries on from the original's outcome.
  void SetVerifying(bool _verifying) noexcept
  {
    m_verifying = _verifying;
  }

  [[nodiscard]] bool Verifying() const noexcept
  {
    return m_verifying;
  }

  /// When on, Poison fills registers with marked values, so that code which reads a register a routine's
  /// contract leaves to it gets a value the original is unlikely to have left there, and a comparison or a digest
  /// shows it (ADR-012). A test switches it on; the game never does.
  void SetPoisoning(bool _poisoning) noexcept
  {
    m_poisoning = _poisoning;
  }

  [[nodiscard]] bool Poisoning() const noexcept
  {
    return m_poisoning;
  }

  /// When Poisoning(), sets each register in _registers (REGISTER_* bits) to its own marked value;
  /// otherwise leaves them as they are.
  void Poison(Registers& _registers, std::uint16_t _registersToPoison) const noexcept;

  /// The lowest linear address of the program's stack. Once a routine has returned, what lies from
  /// here up to SS:SP is dead, and the two runs may differ there. Zero (the default) means nothing is.
  void SetStackFloor(std::uint32_t _linear) noexcept
  {
    m_stackFloor = _linear;
  }

  [[nodiscard]] std::uint32_t StackFloor() const noexcept
  {
    return m_stackFloor;
  }

  [[nodiscard]] const std::vector<Mismatch>& Mismatches() const noexcept
  {
    return m_mismatches;
  }

  /// The native routine, the innermost, that waited past the end of a run off the native thread, or
  /// empty. It cannot stop there and go on in the next run, so the run stopped, StopReason::Overran
  /// (ADR-010 item 8): a routine that can wait must be hooked as one that does.
  [[nodiscard]] const std::string& Overran() const noexcept
  {
    return m_overran;
  }

  /// What a comparison found: an empty string when the native outcome is the original's, or the
  /// differences. _originalWrites and _originalAfter hold every byte the original changed and the
  /// value it left; _nativeWrites, every byte the native routine changed, starting from the same
  /// memory; _memory, the native outcome.
  [[nodiscard]] std::string Compare(const Hook& _hook, const Registers& _original, const Registers& _native,
                                    const WriteJournal& _originalWrites, std::span<const std::uint8_t> _originalAfter,
                                    const WriteJournal& _nativeWrites, const Memory& _memory);

  void AddMismatch(Mismatch _mismatch);
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
  std::vector<Mismatch> m_mismatches;
  std::uint32_t m_stackFloor = 0;
  std::vector<std::uint8_t> m_marks; // a byte per address: changed by a run Compare is looking at
  std::string m_overran;
  bool m_verifying = false;
  bool m_poisoning = false;
};

} // namespace Machine

// GameLogic/Counted.h
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Elite
{

/// The alignment of the block of memory that holds the data segment with its change count (GameState): a power of two no
/// smaller than the block, which is the count's 8 bytes and then GameData's 64 KiB. The count is the block's first member, so
/// rounding the address of any byte of the data segment down to a multiple of this finds it (ChangeCountAt).
inline constexpr std::size_t COUNTED_BLOCK_ALIGNMENT = 0x20000;

/// The change count of the block that holds the byte at _byte, which must be in a GameState's data segment: _byte's address
/// rounded down to COUNTED_BLOCK_ALIGNMENT, where the GameState keeps the count. Every member of GameData finds its own
/// GameState's count this way, at any depth of nesting and in any array, with no offset of its own and no state outside the
/// GameState.
[[nodiscard]] inline std::uint64_t& ChangeCountAt(std::uint8_t* _byte) noexcept
{
  const std::uintptr_t intoBlock = reinterpret_cast<std::uintptr_t>(_byte) & (COUNTED_BLOCK_ALIGNMENT - 1);
  return *reinterpret_cast<std::uint64_t*>(_byte - intoBlock);
}

/// Whether T is what a Counted holds: a byte, a word, or an enumeration whose values are one, as the game's stored indexes and
/// messages are where the original stored an address (D33, piece 3).
template <typename T> [[nodiscard]] consteval bool IsCountedValue() noexcept
{
  if constexpr (std::is_enum_v<T>)
  {
    return std::is_same_v<std::underlying_type_t<T>, std::uint8_t> || std::is_same_v<std::underlying_type_t<T>, std::uint16_t>;
  }
  else
  {
    return std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint16_t>;
  }
}

/// A byte or a word of the game's memory, as a member of GameData (D19): its bytes at the reference's layout, little-endian as
/// the 8088 keeps them, with an alignment of 1 so that it sits anywhere in the packed struct and can be passed by reference. A
/// field that holds an index or a message in place of the original's address holds it as an enumeration of a byte or a word.
///
/// It reads as a value (`_state.Data().commanderBlock.fuel`) and is written by assignment, and a write counts in its own
/// GameState's change count (ChangeCountAt) exactly when it changes the stored value. A write of the value already there counts
/// nothing, so a loop that only stores what it found still idles: paced time compares the count from turn to turn (ADR-015).
/// That makes assignment from a value the only way to write one:
/// - a copy from one to another (`a = b`) does not compile; write `a = T{b}`, which counts;
/// - nor do compound assignment and increment; write `a = static_cast<T>(a + 1)`;
/// - a helper that writes one takes it by reference.
///
/// It can be copied, as GameData must be trivially copyable for the reference's image to load into it as bytes, but only
/// explicitly. A copy is not in a GameState, and a write to one would count into whatever its address rounds down to, so the
/// copy constructor is explicit, as GameData's structs' and SpaceObject's are: `auto x = member`, a capture by copy, a
/// by-value parameter and a return by value do not compile. Read a value as `T x = member`.
template <typename T> class Counted
{
  static_assert(IsCountedValue<T>(), "a byte or a word of the 8088's memory");
  static_assert(std::endian::native == std::endian::little, "the reference's words are little-endian, and are copied as they are");

public:
  Counted() noexcept = default;
  explicit Counted(const Counted&) noexcept = default;
  Counted& operator=(const Counted&) = delete;

  /// The value stored.
  operator T() const noexcept
  {
    T value;
    std::memcpy(&value, m_bytes, sizeof(T));
    return value;
  }

  /// Stores _value, and counts the write in the GameState's change count when it changes the value stored.
  Counted& operator=(T _value) noexcept
  {
    if (static_cast<T>(*this) != _value)
    {
      std::memcpy(m_bytes, &_value, sizeof(T));
      ++ChangeCountAt(m_bytes);
    }
    return *this;
  }

private:
  std::uint8_t m_bytes[sizeof(T)];
};

static_assert(sizeof(Counted<std::uint8_t>) == 1 && alignof(Counted<std::uint8_t>) == 1);
static_assert(sizeof(Counted<std::uint16_t>) == 2 && alignof(Counted<std::uint16_t>) == 1);
static_assert(std::is_trivially_copyable_v<Counted<std::uint16_t>> && std::is_standard_layout_v<Counted<std::uint16_t>>);
static_assert(!std::is_convertible_v<const Counted<std::uint8_t>&, Counted<std::uint8_t>>, "a copy is only ever explicit");

} // namespace Elite

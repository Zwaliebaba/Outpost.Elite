// GameLogic/Counted.h
#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Elite
{

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

/// A byte or a word of the game's memory, as a member of GameData (D19, D38): its bytes little-endian as the 8088 keeps them,
/// with an alignment of 1 so that it sits anywhere in its group's struct, at its 1987 offset, and can be passed by reference. A
/// field that holds an index or a message in place of the original's address holds it as an enumeration of a byte or a word.
///
/// It reads as a value (`_state.Data().commander.fuel`) and is written by assignment from a value. Until the waits were declared
/// (ADR-039) a write counted in its GameState's change count when it changed the value (ADR-024); nothing counts now. The rules
/// the count needed stay, as they keep every write and every copy of the state explicit:
/// - a copy from one to another (`a = b`) does not compile; write `a = T{b}`;
/// - nor do compound assignment and increment; write `a = static_cast<T>(a + 1)`;
/// - a helper that writes one takes it by reference.
///
/// It can be copied, as GameData must be trivially copyable for the reference's image to load into it as bytes, but only
/// explicitly, as GameData's structs' and SpaceObject's copy constructors are: `auto x = member`, a capture by copy, a by-value
/// parameter and a return by value do not compile. Read a value as `T x = member`.
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

  /// Stores _value.
  Counted& operator=(T _value) noexcept
  {
    std::memcpy(m_bytes, &_value, sizeof(T));
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

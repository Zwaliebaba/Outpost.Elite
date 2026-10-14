// GameLogic/StateWriter.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Elite
{

/// The game's state written member by member (D76): each struct of the state lists its members by name, in their order
/// (Visit), and this writes each byte and word, little-endian, an array's elements in turn and a struct's members within it.
/// What it writes depends on the members and their order, not on how a struct lays them out: it is what the state's digest
/// hashes (GameStateDigest) and what the commander file holds (the .cdr format, D78).
class StateWriter
{
public:
  /// _member, which _name names, written: a byte or a word, an array of them, or a struct that lists its own.
  template <typename T> void operator()([[maybe_unused]] std::string_view _name, const T& _member)
  {
    if constexpr (std::is_array_v<T>)
    {
      for (const auto& element : _member)
      {
        (*this)(_name, element);
      }
    }
    else if constexpr (requires { _member.Visit(*this); })
    {
      _member.Visit(*this);
    }
    else
    {
      WriteValue(_member);
    }
  }

  /// What has been written, in order.
  [[nodiscard]] std::span<const std::uint8_t> Bytes() const noexcept
  {
    return m_bytes;
  }

private:
  template <typename U> void WriteValue(const U& _value)
  {
    const U value = _value;
    std::uint32_t bits = 0;
    if constexpr (std::is_enum_v<U>)
    {
      bits = static_cast<std::underlying_type_t<U>>(value);
    }
    else
    {
      bits = value;
    }
    for (std::size_t byte = 0; byte < sizeof(U); ++byte)
    {
      m_bytes.push_back(static_cast<std::uint8_t>(bits >> (byte * 8)));
    }
  }

  std::vector<std::uint8_t> m_bytes;
};

/// The game's state read back member by member, from what a StateWriter writes: each byte and word in the members' order, from
/// _bytes. Reading past their end throws, as a file shorter than the state it is read into is an error.
class StateReader
{
public:
  explicit StateReader(std::span<const std::uint8_t> _bytes) noexcept
    : m_bytes(_bytes)
  {
  }

  /// _member, which _name names, read: a byte or a word, an array of them, or a struct that lists its own.
  template <typename T> void operator()([[maybe_unused]] std::string_view _name, T& _member)
  {
    if constexpr (std::is_array_v<T>)
    {
      for (auto& element : _member)
      {
        (*this)(_name, element);
      }
    }
    else if constexpr (requires { _member.Visit(*this); })
    {
      _member.Visit(*this);
    }
    else
    {
      ReadValue(_member);
    }
  }

  /// How many bytes have been read.
  [[nodiscard]] std::size_t Count() const noexcept
  {
    return m_at;
  }

private:
  template <typename U> void ReadValue(U& _value)
  {
    if (m_bytes.size() - m_at < sizeof(U))
    {
      throw std::out_of_range("StateReader: the state is longer than the bytes it is read from");
    }
    std::uint32_t bits = 0;
    for (std::size_t byte = 0; byte < sizeof(U); ++byte)
    {
      bits |= std::uint32_t{m_bytes[m_at + byte]} << (byte * 8);
    }
    m_at += sizeof(U);
    if constexpr (std::is_enum_v<U>)
    {
      _value = static_cast<U>(static_cast<std::underlying_type_t<U>>(bits));
    }
    else
    {
      _value = static_cast<U>(bits);
    }
  }

  std::span<const std::uint8_t> m_bytes;
  std::size_t m_at = 0;
};

/// _value, a struct of the state, a SpaceObject, an array of them or a member, written member by member.
template <typename T> [[nodiscard]] std::vector<std::uint8_t> StateBytes(const T& _value)
{
  StateWriter writer;
  writer({}, _value);
  return {writer.Bytes().begin(), writer.Bytes().end()};
}

/// _value, a struct of the state, a SpaceObject, an array of them or a member, read member by member from _bytes, which must hold
/// at least as many as it writes. Returns how many it read.
template <typename T> std::size_t ReadStateBytes(T& _value, std::span<const std::uint8_t> _bytes)
{
  StateReader reader(_bytes);
  reader({}, _value);
  return reader.Count();
}

} // namespace Elite

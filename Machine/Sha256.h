// Machine/Sha256.h
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Machine
{

/// SHA-256 (FIPS 180-4), for the two places the host needs a strong digest of bytes: checking that
/// the file it was given is the reference ADR-001 names before it patches it, and fingerprinting the
/// machine's state so a replay can be checked against an earlier run (ADR-003).
///
/// Feed it with Update() as often as needed, then Finish() once; Finish() leaves it ready for a new
/// message. Of() does all three for bytes held in one piece.
class Sha256
{
public:
  static constexpr std::size_t DIGEST_BYTES = 32;
  using Digest = std::array<std::uint8_t, DIGEST_BYTES>;

  void Update(std::span<const std::uint8_t> _bytes) noexcept;
  [[nodiscard]] Digest Finish() noexcept;

  [[nodiscard]] static Digest Of(std::span<const std::uint8_t> _bytes) noexcept;
  /// Lower-case hex, the way sha256sum and Python's hexdigest() print it.
  [[nodiscard]] static std::string ToHex(const Digest& _digest);

private:
  static constexpr std::size_t BLOCK_BYTES = 64;
  static constexpr std::array<std::uint32_t, 8> INITIAL_STATE = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                                                                 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};

  void Compress() noexcept;

  std::array<std::uint32_t, 8> m_state = INITIAL_STATE;
  std::array<std::uint8_t, BLOCK_BYTES> m_block = {};
  std::size_t m_blockBytes = 0;
  std::uint64_t m_messageBytes = 0;
};

} // namespace Machine

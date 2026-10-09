#include "pch.h"

#include "Sha256.h"

#include <bit>

namespace Machine
{

namespace
{

// The first 32 bits of the fractional parts of the cube roots of the first 64 primes (FIPS 180-4 §4.2.2).
constexpr std::array<std::uint32_t, 64> ROUND_CONSTANTS = {
  0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5, 0xD807AA98, 0x12835B01, 0x243185BE,
  0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174, 0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA,
  0x5CB0A9DC, 0x76F988DA, 0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967, 0x27B70A85,
  0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85, 0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
  0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070, 0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F,
  0x682E6FF3, 0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};

} // namespace

void Sha256::Update(std::span<const std::uint8_t> _bytes) noexcept
{
  for (const std::uint8_t byte : _bytes)
  {
    m_block[m_blockBytes++] = byte;
    if (m_blockBytes == BLOCK_BYTES)
    {
      Compress();
      m_blockBytes = 0;
    }
  }
  m_messageBytes += _bytes.size();
}

Sha256::Digest Sha256::Finish() noexcept
{
  // Padding: a one bit, zeros up to 56 bytes into a block, then the message length in bits, big-endian.
  const std::uint64_t messageBits = m_messageBytes * 8;
  m_block[m_blockBytes++] = 0x80;
  if (m_blockBytes > BLOCK_BYTES - 8)
  {
    while (m_blockBytes < BLOCK_BYTES)
      m_block[m_blockBytes++] = 0;
    Compress();
    m_blockBytes = 0;
  }
  while (m_blockBytes < BLOCK_BYTES - 8)
    m_block[m_blockBytes++] = 0;
  for (int shift = 56; shift >= 0; shift -= 8)
    m_block[m_blockBytes++] = static_cast<std::uint8_t>(messageBits >> shift);
  Compress();

  Digest digest = {};
  for (std::size_t word = 0; word < m_state.size(); ++word)
  {
    for (std::size_t byte = 0; byte < 4; ++byte)
      digest[word * 4 + byte] = static_cast<std::uint8_t>(m_state[word] >> (24 - 8 * byte));
  }

  m_state = INITIAL_STATE;
  m_block = {};
  m_blockBytes = 0;
  m_messageBytes = 0;
  return digest;
}

Sha256::Digest Sha256::Of(std::span<const std::uint8_t> _bytes) noexcept
{
  Sha256 hash;
  hash.Update(_bytes);
  return hash.Finish();
}

std::string Sha256::ToHex(const Digest& _digest)
{
  static constexpr char DIGITS[] = "0123456789abcdef";
  std::string text;
  text.reserve(DIGEST_BYTES * 2);
  for (const std::uint8_t byte : _digest)
  {
    text.push_back(DIGITS[byte >> 4]);
    text.push_back(DIGITS[byte & 0x0F]);
  }
  return text;
}

void Sha256::Compress() noexcept
{
  std::array<std::uint32_t, 64> schedule = {};
  for (std::size_t word = 0; word < 16; ++word)
  {
    schedule[word] = (std::uint32_t{m_block[word * 4]} << 24) | (std::uint32_t{m_block[word * 4 + 1]} << 16) |
                     (std::uint32_t{m_block[word * 4 + 2]} << 8) | std::uint32_t{m_block[word * 4 + 3]};
  }
  for (std::size_t word = 16; word < schedule.size(); ++word)
  {
    const std::uint32_t early = schedule[word - 15];
    const std::uint32_t late = schedule[word - 2];
    const std::uint32_t sigma0 = std::rotr(early, 7) ^ std::rotr(early, 18) ^ (early >> 3);
    const std::uint32_t sigma1 = std::rotr(late, 17) ^ std::rotr(late, 19) ^ (late >> 10);
    schedule[word] = schedule[word - 16] + sigma0 + schedule[word - 7] + sigma1;
  }

  std::uint32_t a = m_state[0];
  std::uint32_t b = m_state[1];
  std::uint32_t c = m_state[2];
  std::uint32_t d = m_state[3];
  std::uint32_t e = m_state[4];
  std::uint32_t f = m_state[5];
  std::uint32_t g = m_state[6];
  std::uint32_t h = m_state[7];
  for (std::size_t round = 0; round < schedule.size(); ++round)
  {
    const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const std::uint32_t choose = (e & f) ^ (~e & g);
    const std::uint32_t first = h + sum1 + choose + ROUND_CONSTANTS[round] + schedule[round];
    const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t second = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + first;
    d = c;
    c = b;
    b = a;
    a = first + second;
  }
  m_state[0] += a;
  m_state[1] += b;
  m_state[2] += c;
  m_state[3] += d;
  m_state[4] += e;
  m_state[5] += f;
  m_state[6] += g;
  m_state[7] += h;
}

} // namespace Machine

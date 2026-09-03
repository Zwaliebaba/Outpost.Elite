#include "pch.h"

#include "Commander.h"

#include "EliteTypes.h"
#include "LookupTables.h"

/*
 * The commander (slice 2d).
 *
 * Two checksums, a byte layout, and the block the game starts from. The checksums are the
 * interesting part: both thread a carry through every step of a seventy-three-step loop, and the
 * second one rotates the accumulator through that same carry in the middle of each step. They
 * are the game's copy protection, so they were written to be hard to reproduce from a
 * description -- which makes them exactly the kind of thing to port against the original rather
 * than from a reading of it.
 */

namespace Elite
{

namespace
{
/// 6502: LDX #&49 -- the loop counts down from 73, and the accumulator STARTS at 73 because the
/// routine begins `TXA`. So the count is also the seed.
constexpr std::uint8_t CHECKSUM_STEPS = 0x49;

/// 6502: NA2%+8 -- the block begins after the eight bytes of name.
constexpr std::size_t BLOCK_IN_FILE = COMMANDER_NAME_SIZE;
} // namespace

std::uint32_t CommanderBlock::Cash() const noexcept
{
  // 6502: CASH is four bytes with the MOST significant first, which is the opposite of every
  // sixteen-bit value elsewhere in the game.
  const std::size_t at = static_cast<std::size_t>(Field::Cash);
  return (static_cast<std::uint32_t>(bytes[at]) << 24) | (static_cast<std::uint32_t>(bytes[at + 1]) << 16)
         | (static_cast<std::uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
}

void CommanderBlock::SetCash(std::uint32_t _tenths) noexcept
{
  const std::size_t at = static_cast<std::size_t>(Field::Cash);
  bytes[at] = static_cast<std::uint8_t>(_tenths >> 24);
  bytes[at + 1] = static_cast<std::uint8_t>(_tenths >> 16);
  bytes[at + 2] = static_cast<std::uint8_t>(_tenths >> 8);
  bytes[at + 3] = static_cast<std::uint8_t>(_tenths);
}

std::uint16_t CommanderBlock::Kills() const noexcept
{
  // 6502: TALLY -- and this one IS low byte first. The two conventions sit sixty bytes apart in
  // the same block.
  const std::size_t at = static_cast<std::size_t>(Field::Kills);
  return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

SystemSeeds CommanderBlock::GalaxySeeds() const noexcept
{
  SystemSeeds seeds;
  const std::size_t at = static_cast<std::size_t>(Field::GalaxySeeds);
  for (std::size_t index = 0; index < seeds.bytes.size(); ++index)
  {
    seeds.bytes[index] = bytes[at + index];
  }
  return seeds;
}

void CommanderBlock::SetGalaxySeeds(const SystemSeeds& _seeds) noexcept
{
  const std::size_t at = static_cast<std::size_t>(Field::GalaxySeeds);
  for (std::size_t index = 0; index < _seeds.bytes.size(); ++index)
  {
    bytes[at + index] = _seeds.bytes[index];
  }
}

std::uint8_t Checksum(const CommanderBlock& _block) noexcept
{
  /*
   * 6502: CHECK -- LDX #&49 / CLC / TXA / QUL2: ADC NA%+6,X / EOR NA%+7,X / DEX / BNE QUL2.
   *
   * NA%+7 is the block's first byte, so `ADC NA%+6,X` reads the byte BEFORE the one the EOR
   * reads. Each step therefore mixes a neighbouring pair, and the loop ends at X = 1 rather than
   * at 0 -- so the block's last three bytes, which are the checksums themselves, are never read.
   */
  std::uint8_t accumulator = CHECKSUM_STEPS;
  bool carry = false;

  for (int index = CHECKSUM_STEPS; index >= 1; --index)
  {
    const AddResult sum =
      AddWithCarry(accumulator, _block.bytes[static_cast<std::size_t>(index) - 1u], carry);
    accumulator = static_cast<std::uint8_t>(sum.value ^ _block.bytes[static_cast<std::size_t>(index)]);
    carry = sum.carry;
  }

  return accumulator;
}

std::uint8_t Checksum2(const CommanderBlock& _block) noexcept
{
  /*
   * 6502: CHECK2 -- STX T / EOR T / ROR A / ADC NA%+6,X / EOR NA%+7,X.
   *
   * The rotate is the whole difference. It shifts the carry the LAST addition produced into bit
   * 7 and hands bit 0 to the addition that follows, so within one step the carry is consumed,
   * replaced, and consumed again. Written as arithmetic this is not expressible, which is
   * presumably why it was chosen to protect the save file.
   */
  std::uint8_t accumulator = CHECKSUM_STEPS;
  bool carry = false;

  for (int index = CHECKSUM_STEPS; index >= 1; --index)
  {
    accumulator = static_cast<std::uint8_t>(accumulator ^ static_cast<std::uint8_t>(index));

    const ShiftResult rotated = RotateRight(accumulator, carry);
    const AddResult sum =
      AddWithCarry(rotated.value, _block.bytes[static_cast<std::size_t>(index) - 1u], rotated.carry);

    accumulator = static_cast<std::uint8_t>(sum.value ^ _block.bytes[static_cast<std::size_t>(index)]);
    carry = sum.carry;
  }

  return accumulator;
}

CommanderBlock DefaultCommander() noexcept
{
  CommanderBlock block;
  for (std::size_t index = 0; index < COMMANDER_BLOCK_SIZE; ++index)
  {
    block.bytes[index] = DEFAULT_COMMANDER[BLOCK_IN_FILE + index];
  }
  return block;
}

std::array<std::uint8_t, COMMANDER_NAME_SIZE> DefaultCommanderName() noexcept
{
  std::array<std::uint8_t, COMMANDER_NAME_SIZE> name{};
  for (std::size_t index = 0; index < name.size(); ++index)
  {
    name[index] = DEFAULT_COMMANDER[index];
  }
  return name;
}

void SaveCommander(const CommanderBlock& _block, std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name,
                   std::span<std::uint8_t, COMMANDER_FILE_SIZE> _outFile) noexcept
{
  /*
   * 6502: SVL1 -- LDA TP,X / STA NA%+7,X, then JSR CHECK2 / STA CHK3 / JSR CHECK / STA CHK.
   *
   * The block is copied first and the checksums are written over two of its bytes IN THE FILE.
   * The live commander at TP is untouched -- so this takes its block by const reference, and the
   * two stored bytes come out of the copy rather than out of the caller's commander.
   *
   * CHECK2 runs first. That happens not to matter, because CHECK reads only the first
   * seventy-four bytes and CHECK2's result lands in the seventy-sixth -- but the order is the
   * original's and reversing it would matter the moment either range changed.
   */
  for (std::size_t index = 0; index < COMMANDER_NAME_SIZE; ++index)
  {
    _outFile[index] = _name[index];
  }

  CommanderBlock image = _block;
  image.At(Field::Checksum3Byte) = Checksum2(image);
  image.At(Field::ChecksumByte) = Checksum(image);

  /*
   * 6502: PLA / EOR #&A9 / STA CHK2 -- the THIRD stored byte, and this port did not write it
   * until the save flow was built on top and the comparison found it missing.
   *
   * It sits four instructions past the competition number in SV1, well after the two CHECK calls,
   * which is why reading SVE from the top and stopping at them looked complete. Nothing caught
   * it: LoadCommander only READS CHK2, to decide whether to set the tampered bit in the
   * competition flags, and a round trip through a port that got it wrong on both sides agreed
   * with itself. A file saved that way loads into the original with bit 7 of COK set -- flagged
   * as tampered by the game's own copy protection.
   *
   * Writing it here rather than in the save flow is safe and is what the original does in effect:
   * both CHECK and CHECK2 read the block's first seventy-four bytes only, so byte seventy-four
   * cannot change what they returned.
   */
  image.At(Field::Checksum2Byte) = static_cast<std::uint8_t>(image.At(Field::ChecksumByte) ^ 0xA9u);

  for (std::size_t index = 0; index < COMMANDER_BLOCK_SIZE; ++index)
  {
    _outFile[BLOCK_IN_FILE + index] = image.bytes[index];
  }
}

bool LoadCommander(std::span<const std::uint8_t, COMMANDER_FILE_SIZE> _file, CommanderBlock& _outBlock,
                   std::span<std::uint8_t, COMMANDER_NAME_SIZE> _outName) noexcept
{
  /*
   * The file's own block. Both checksums are computed over THIS rather than over what is handed
   * back, because the original reads NA% -- the copy from disk -- and writes only to TP.
   */
  CommanderBlock image;
  for (std::size_t index = 0; index < COMMANDER_BLOCK_SIZE; ++index)
  {
    image.bytes[index] = _file[BLOCK_IN_FILE + index];
  }

  // 6502: QUL1 -- LDA NA%-1,X / STA YSAV2,X, which copies the name and the block together
  // because they are consecutive in both places.
  for (std::size_t index = 0; index < COMMANDER_NAME_SIZE; ++index)
  {
    _outName[index] = _file[index];
  }

  /*
   * 6502: the loop's `BNE QUL1` ends it at X = 1, so the last byte it moves is the block's
   * seventy-sixth and the seventy-seventh -- the block's own checksum -- is never loaded. What
   * the caller had there stays there. Nothing reads it before the next save recomputes it.
   */
  for (std::size_t index = 0; index + 1 < COMMANDER_BLOCK_SIZE; ++index)
  {
    _outBlock.bytes[index] = image.bytes[index];
  }

  /*
   * 6502: doitagain -- JSR CHECK / CMP CHK / BNE doitagain.
   *
   * The branch goes BACKWARDS to the check, not forwards to an error path, so a block whose
   * checksum is wrong spins here for ever. The port returns instead; the header says why.
   */
  if (Checksum(image) != image.At(Field::ChecksumByte))
  {
    return false;
  }

  /*
   * 6502: EOR #&A9 / TAX / LDA COK / CPX CHK2 / BEQ tZ / ORA #&80 / tZ: ORA #&40 / STA COK.
   *
   * The competition flags, and they are the interesting half of this routine. Bit 6 records that
   * the commander was loaded from a file at all; bit 7 records that the file's SECOND stored
   * checksum did not agree with the first one EORed with &A9 -- which a tampered file would fail.
   * So the game does not refuse such a file here: it remembers it, and the competition code
   * further on reads the flag. That is why the check below is not the only thing that matters.
   */
  const std::uint8_t stamp = static_cast<std::uint8_t>(image.At(Field::ChecksumByte) ^ 0xA9u);
  std::uint8_t competition = _outBlock.At(Field::Competition);
  if (stamp != image.At(Field::Checksum2Byte))
  {
    competition = static_cast<std::uint8_t>(competition | 0x80u);
  }
  competition = static_cast<std::uint8_t>(competition | 0x40u);
  _outBlock.At(Field::Competition) = competition;

  // 6502: JSR CHECK2 / CMP CHK3 / BNE doitagain -- and the same backwards branch.
  return Checksum2(image) == image.At(Field::Checksum3Byte);
}

} // namespace Elite

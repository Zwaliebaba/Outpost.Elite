#include "pch.h"

#include "SaveGame.h"

#include "EliteTypes.h"
#include "LookupTables.h"

/*
 * Saving and loading a commander (slice 2d).
 */

namespace Elite
{

namespace
{
/// 6502: ORA #%10000000 -- bit 7 forced on, so the first byte is never small.
constexpr std::uint8_t COMPETITION_HIGH_BIT = 0x80;

/// 6502: EOR #&5A and EOR #&A9. Two constants with no meaning beyond being hard to guess.
constexpr std::uint8_t COMPETITION_MIX = 0x5A;
constexpr std::uint8_t CHECKSUM_STAMP = 0xA9;

/// 6502: CMP #'Y' / CMP #'N'.
constexpr std::uint8_t KEY_YES = 'Y';
constexpr std::uint8_t KEY_NO = 'N';

/// 6502: NA2%+8 -- the block begins after the eight bytes of name, in the file and in NA2%.
constexpr std::size_t BLOCK_IN_FILE = COMMANDER_NAME_SIZE;
} // namespace

CompetitionNumber MakeCompetitionNumber(const CommanderBlock& _image) noexcept
{
  /*
   * 6502: PHA / ORA #%10000000 / STA K / EOR COK / STA K+2 / EOR CASH+2 / STA K+1 /
   *       EOR #&5A / EOR TALLY+1 / STA K+3, then later PLA / EOR #&A9 / STA CHK2.
   *
   * ONE ACCUMULATOR, four stores, and the stores are out of order. Each EOR builds on what the
   * last one left, so the four bytes are a chain rather than four independent expressions -- and
   * they are written to K, K+2, K+1, K+3, which is what stops the printed number reading as the
   * inputs in sequence.
   *
   * `CASH+2` is the THIRD of the four cash bytes, and cash is stored most significant first
   * (§6.14), so it is the second-least significant -- tens of credits. A player who spent a
   * little between saves gets a visibly different competition number; one who spent nothing gets
   * the same one.
   */
  const std::uint8_t checksum = _image.At(Field::ChecksumByte);

  CompetitionNumber result{};

  std::uint8_t accumulator = static_cast<std::uint8_t>(checksum | COMPETITION_HIGH_BIT);
  result.value[0] = accumulator;

  accumulator = static_cast<std::uint8_t>(accumulator ^ _image.At(Field::Competition));
  result.value[2] = accumulator;

  // 6502: EOR CASH+2 -- the third byte of the four, counting from the most significant.
  accumulator =
    static_cast<std::uint8_t>(accumulator ^ _image.bytes[static_cast<std::size_t>(Field::Cash) + 2u]);
  result.value[1] = accumulator;

  accumulator = static_cast<std::uint8_t>(accumulator ^ COMPETITION_MIX);
  accumulator = static_cast<std::uint8_t>(
    accumulator ^ _image.bytes[static_cast<std::size_t>(Field::Kills) + 1u]);
  result.value[3] = accumulator;

  // 6502: PLA / EOR #&A9 / STA CHK2 -- the checksum as it was BEFORE the chain above touched it.
  result.checksum2 = static_cast<std::uint8_t>(checksum ^ CHECKSUM_STAMP);

  return result;
}

bool AskYesNo(KeySource& _keys) noexcept
{
  // 6502: YESNO -- JSR t / CMP #'Y' / BEQ PL6 / CMP #'N' / BNE YESNO / CLC / RTS.
  for (;;)
  {
    const std::uint8_t key = _keys.NextKey();
    if (key == KEY_YES)
    {
      return true;
    }
    if (key == KEY_NO)
    {
      return false;
    }
  }
}

void ResetToDefaultCommander(std::span<std::uint8_t, COMMANDER_FILE_SIZE> _outFile) noexcept
{
  /*
   * 6502: JAMESON -- LDY #(NAEND%-NA2%) / JAMEL1: LDA NA2%,Y / STA NA%,Y / DEY / BPL JAMEL1,
   * then LDY #7 / STY oldlong.
   *
   * It writes over NA%, the SAVE IMAGE, and not over the commander at TP -- so the reset does
   * nothing until something loads that image. SVE's option 4 does exactly that: JAMESON, then a
   * jump to DFAULT.
   */
  for (std::size_t index = 0; index < COMMANDER_FILE_SIZE; ++index)
  {
    _outFile[index] = DEFAULT_COMMANDER[index];
  }
}

SaveOutcome SaveCommanderTo(CommanderStore& _store, CommanderBlock& _block,
                            std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name) noexcept
{
  /*
   * 6502: LSR SVC -- and this HALVES the save count rather than incrementing it.
   *
   * So it decays towards zero and a commander saved ten times looks like one saved once. It is
   * the live commander that changes here, not the file image, which is why this takes the block
   * by reference where SaveCommander takes it by const.
   */
  _block.At(Field::SaveCount) = static_cast<std::uint8_t>(_block.At(Field::SaveCount) >> 1);

  std::array<std::uint8_t, COMMANDER_FILE_SIZE> file{};
  SaveCommander(_block, _name, file);

  SaveOutcome outcome{};

  // The competition number reads the file's checksums, so it is worked out from the image rather
  // than from the block -- the same distinction SaveCommander's header makes.
  CommanderBlock image;
  for (std::size_t index = 0; index < COMMANDER_BLOCK_SIZE; ++index)
  {
    image.bytes[index] = file[BLOCK_IN_FILE + index];
  }
  outcome.competition = MakeCompetitionNumber(image);

  outcome.written = _store.Write(_name, file);
  return outcome;
}

bool LoadCommanderFrom(CommanderStore& _store, CommanderBlock& _outBlock,
                       std::span<std::uint8_t, COMMANDER_NAME_SIZE> _name) noexcept
{
  std::array<std::uint8_t, COMMANDER_FILE_SIZE> file{};
  if (!_store.Read(_name, file))
  {
    return false;
  }

  // 6502: DFAULT -- and the checksums are what decide, not the read.
  return LoadCommander(file, _outBlock, _name);
}

} // namespace Elite

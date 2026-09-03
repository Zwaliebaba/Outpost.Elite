#pragma once

#include "Commander.h"
#include "TextPrint.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

/*
 * Saving and loading a commander (slice 2d).
 *
 * 6502: SV1, LOD, YESNO and JAMESON. The C64 build's `SVE` is not a file write at all -- it is
 * the disk access menu -- and the only instructions in the whole flow that touch a device are two
 * Kernal calls. Everything around them is arithmetic, text and a keyboard, which is why this
 * waited on nothing but somebody looking.
 */

/*
 * 6502: KERNALSVE and KERNALLOAD -- where a commander file goes and comes from.
 *
 * The port's answer to the C64's Kernal. It is a seam for the reason `GameLogic` has all its
 * others: the determinism guard forbids file access here (AGENTS.md §5, checked by
 * tools/check_gamelogic.py), and a routine that reached for a disk could not be compared against
 * the original in an interpreter with no disk.
 *
 * Both calls report success the way the original does: the Kernal sets the carry on failure and
 * `SV1` preserves it with a PHP, so a caller can tell a failed save from a successful one.
 */
class CommanderStore
{
public:
  virtual ~CommanderStore() = default;

  /// 6502: SV1's `JSR KERNALSVE`. False when the write failed.
  virtual bool Write(std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name,
                     std::span<const std::uint8_t, COMMANDER_FILE_SIZE> _file) = 0;

  /// 6502: LOD's `JSR KERNALLOAD`. False when the file could not be read.
  virtual bool Read(std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name,
                    std::span<std::uint8_t, COMMANDER_FILE_SIZE> _outFile) = 0;
};

/*
 * 6502: SV1's K to K+3 -- the competition number, and the CHK2 that is computed with it.
 *
 * A four-byte number printed after every save, and it is not a serial: it is the commander's
 * checksum folded together with the competition flags, the third byte of the cash and the high
 * byte of the kill tally. Entrants sent it in, and it encoded enough of their commander to check
 * the claim.
 *
 * THE BYTES ARE FILLED OUT OF ORDER -- K, then K+2, then K+1, then K+3 -- each EOR building on the
 * accumulator the last store left. BPRNT then prints them most significant first, so the digits a
 * player reads interleave the four inputs rather than presenting them in the order they were
 * computed. That is not tidiness; it is what makes the number hard to forge by hand.
 */
struct CompetitionNumber
{
  std::array<std::uint8_t, 4> value{};  ///< 6502: K to K+3, which BPRNT prints most significant first
  std::uint8_t checksum2 = 0;           ///< 6502: CHK2 -- the checksum EOR &A9, stored in the file
};

/*
 * 6502: the instructions between `JSR CHECK` and `JSR KERNALSETUP` in SV1.
 *
 * Takes the FILE image rather than the live block, because the checksum it folds in is the one
 * SaveCommander wrote into the file and not anything the commander at TP holds.
 */
[[nodiscard]] CompetitionNumber MakeCompetitionNumber(const CommanderBlock& _image) noexcept;

/*
 * 6502: YESNO -- wait for "Y" or "N", and ignore everything else.
 *
 * Returns true for "Y". The original says so with the carry, and it gets it for free: `CMP #'Y'`
 * sets the carry when the key is 'Y' or higher, and the branch it takes lands on an RTS. So the
 * "yes" answer is the comparison's own flag rather than anything the routine sets.
 */
[[nodiscard]] bool AskYesNo(KeySource& _keys) noexcept;

/*
 * 6502: JAMESON -- put the default commander back.
 *
 * Copies NA2% over NA%, which is the SAVE IMAGE and not the live commander, so a caller has to
 * load it afterwards for the reset to take effect. `SVE`'s option 4 does exactly that: JAMESON
 * then DFAULT.
 */
void ResetToDefaultCommander(std::span<std::uint8_t, COMMANDER_FILE_SIZE> _outFile) noexcept;

/*
 * 6502: SV1 without its two Kernal calls -- everything a save does before the bytes leave.
 *
 * Halves the save count, builds the file image with all three checksums, works out the
 * competition number and hands the result to the store. The competition number is returned rather
 * than printed, because printing it is four token calls the caller already owns.
 *
 * `LSR SVC` is the one line worth pausing on: every save HALVES the count rather than
 * incrementing it, so it decays towards zero and a commander saved often looks the same as one
 * saved once. Whatever it was for, it is not a count of saves.
 */
struct SaveOutcome
{
  bool written = false;
  CompetitionNumber competition{};
};

[[nodiscard]] SaveOutcome SaveCommanderTo(CommanderStore& _store, CommanderBlock& _block,
                                          std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name) noexcept;

/*
 * 6502: `loading` without LOD's Kernal call -- read a file back and check it.
 *
 * Returns false when the store could not read it OR when either checksum disagrees, which are
 * different failures to a player and the same one to this routine. The original does not
 * distinguish them either: DFAULT's `BNE doitagain` spins for ever on a bad checksum, and
 * LoadCommander's header records why the port returns instead.
 */
[[nodiscard]] bool LoadCommanderFrom(CommanderStore& _store, CommanderBlock& _outBlock,
                                     std::span<std::uint8_t, COMMANDER_NAME_SIZE> _name) noexcept;

} // namespace Elite

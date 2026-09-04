#pragma once

#include "Commander.h"
#include "ExtendedTokens.h"
#include "NameEntry.h"
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
    virtual bool Read(std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name, std::span<std::uint8_t, COMMANDER_FILE_SIZE> _outFile) = 0;
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
    std::array<std::uint8_t, 4> value{}; ///< 6502: K to K+3, which BPRNT prints most significant first
    std::uint8_t checksum2 = 0;          ///< 6502: CHK2 -- the checksum EOR &A9, stored in the file
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

    /*
     * 6502: NA% after SVL1 -- the save image, and it outlives the write.
     *
     * SV1 calls DFAULT once the Kernal returns, and DFAULT reads NA% rather than re-reading the
     * disk, so the bytes have to be here for the menu to reproduce that. Returning them also says
     * something true about the original: the image the game keeps and the file on the device are
     * the same bytes, and only the image is ever read back.
     */
    std::array<std::uint8_t, COMMANDER_FILE_SIZE> image{};
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

  /*
   * Everything the disk access menu prints, reads and stores through.
   *
   * One struct for the same reason the trading screens have one: the alternative is a
   * nine-argument function. `chpr` is the CHARACTER printer rather than the token one, because
   * MT26 prints through CHPR directly and the distinction is load-bearing (§6.19).
   */
  struct SaveScreen
  {
    TokenPrinter& printer;
    CharacterPrinter& characters;
    ExtendedTokenPrinter& extended;
    TextSink& chpr;
    TextState& text;
    KeySource& keys;
    LineEntryEffects& effects;
    CommanderStore& store;

    /*
     * 6502: K and U -- and U is the reason this is a reference rather than a local.
     *
     * SV1 prints the competition number with `CLC / JSR BPRNT` and never sets U, which is BPRNT's
     * field width. U is a scratch byte in zero page that ZERO does not clear, so the number is
     * printed to whatever width the last caller of BPRNT happened to leave behind. The upstream
     * source says so in as many words. It is harmless -- the number always has ten digits, so all
     * that varies is a leading space -- but a port that chose a width here would be inventing one.
     */
    NumberWorkspace& numbers;
  };

  /// How the menu ended. 6502: which label it reached, and the carry it left.
  enum class DiskMenuOutcome
  {
    Left,   ///< 6502: feb13 -- any key but 1 to 4, and CLC
    Loaded, ///< 6502: `loading` -- SEC, and a different commander is in place
    Saved,  ///< 6502: SVEX after a successful save -- and CLC, even though DFAULT just ran
    Reset,  ///< 6502: option 4 -- JAMESON then DFAULT, so the default commander is loaded
  };

  /*
   * The leaf, the carry, and the competition number if one was worked out.
   *
   * `outcome` and `newCommander` DISAGREE after a failed load: the leaf is whichever one the
   * player reached, and the carry is set by the stack frame that failure left behind. That is not
   * an inconsistency in the port -- it is what the routine does, and the header above says how.
   */

  /// 6502: the four keys SVE compares against, in the order it compares them.
  inline constexpr std::uint8_t DISK_MENU_LOAD = '1';
  inline constexpr std::uint8_t DISK_MENU_SAVE = '2';
  inline constexpr std::uint8_t DISK_MENU_MEDIA = '3';
  inline constexpr std::uint8_t DISK_MENU_DEFAULT = '4';

  struct DiskMenuResult
  {
    DiskMenuOutcome outcome = DiskMenuOutcome::Left;
    bool newCommander = false;       ///< 6502: the carry on return
    CompetitionNumber competition{}; ///< 6502: K to K+3, when a save happened
  };

  /*
   * 6502: SVE -- the disk access menu, which is what the C64 build calls its save routine.
   *
   * Five options around routines that all exist by now, and three things about it are worth
   * knowing before reading it.
   *
   * IT IS A LOOP, and not only for option 3. Toggling the media redisplays the menu, obviously --
   * but so does every FAILURE: a save the Kernal refuses reaches `tapeerror`, prints an error,
   * waits for a key and jumps back to SVE, and a file that is not a commander reaches `ELT2F` and
   * does the same. There is no way to leave the menu by failing.
   *
   * AND A FAILED LOAD POISONS EVERY LATER EXIT. Those failures are inside `JSR LOD`, and they
   * leave by `JMP SVE` rather than by returning, so the menu is re-entered with LOD's return
   * address still on the stack. Whatever the player does next, its RTS lands back in `loading` at
   * `JSR TRNME / SEC / RTS` -- so leaving with "5" after a failed load renames the commander to
   * whatever was typed and reports a new commander loaded, which sends TT102 to restart the game
   * instead of to the docking bay. Nothing was loaded. The frame is pushed again on each failed
   * load, so the stack grows until it does not.
   *
   * SAVING RELOADS. After a successful write, SV1 calls DFAULT on the file it has just written and
   * waits for a key before returning. So a save is also a load: the commander that carries on is
   * the one DFAULT rebuilt, with the platform bit stamped into its competition flags and its own
   * checksum byte left at whatever the copy loop stopped short of.
   *
   * AND THE CARRY IS NOT WHAT IT LOOKS LIKE. `SVEX` and `feb13` both clear it, so a save and an
   * exit say "no new commander" -- the save in spite of the DFAULT it just ran. Option 1 sets it
   * with an explicit `SEC`. Option 4 sets it too, and NOTHING IN SVE WRITES IT: `JMP DFAULT` is a
   * tail call, and DFAULT's last comparison is `CMP CHK3` on the path where the two agree, which
   * leaves the carry set. So the flag that tells TT102 to restart the game is, for the reset, a
   * side effect of a checksum test three routines away.
   */
  /*
   * THE TWO COMMANDERS ARE BOTH ARGUMENTS, and keeping them apart is the whole reason this reads
   * the way it does.
   *
   *   `_block` and `_name`   6502: TP and NAME -- the commander being played
   *   `_image`               6502: NA% -- the last saved commander, as a file
   *
   * Nothing in the menu writes the live commander except DFAULT. A load fills the IMAGE and returns
   * with the carry set so the caller will run DFAULT; TRNME renames the IMAGE; JAMESON overwrites
   * the IMAGE. Collapsing the two -- which is tempting, since every caller does DFAULT immediately
   * -- changes what the menu PRINTS: option 2's line shows the live commander's name through
   * control code 4, so a save that the device then refuses would redisplay the menu under the new
   * name in a port that had folded them together, and under the old one in the game.
   *
   * `_buffer` is the line editor's, and it is what names the file: KERNALSETUP builds the filename
   * from INWK+5, which is where MT26 just wrote. On the load path TRNME has not run yet, so a
   * player who types a new name loads from THAT file while the image still holds the old one.
   */
  [[nodiscard]] DiskMenuResult DiskAccessMenu(SaveScreen& _screen, CommanderBlock& _block,
                                              std::span<std::uint8_t, COMMANDER_NAME_SIZE> _name,
                                              std::span<std::uint8_t, COMMANDER_FILE_SIZE> _image, std::span<std::uint8_t> _buffer,
                                              bool& _useDisk) noexcept;

} // namespace Elite
